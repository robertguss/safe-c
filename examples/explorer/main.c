/* The Postgres explorer (docs/plans/2026-10-25-2038-feat-postgres-explorer-plan.md).
 * Glue code: adapters/ui.c owns the C main and calls app_main here (KTD17).
 *
 *   explorer [--conninfo STR]                  connects and opens the query window
 *   explorer --scene NAME --screenshot a.png   renders one scene and saves it (KTD15)
 *
 * An empty or missing --conninfo connects with the PG* environment and
 * ~/.pgpass, so no secret has to sit on the command line.
 *
 * Scenes: hello (fonts and a custom region), shell-light and shell-dark (the
 * Postico window, U3), and the U8 scenes, which connect with the PG*
 * environment (pgtest.sh's temporary server): query-rows, query-wide, query-tag,
 * query-error, query-error-rows, conn-auth-failed and conn-disconnected; and the
 * U11 grid scenes: grid-light, grid-dark, grid-null-wrap, grid-error-after-rows,
 * grid-capped and grid-detail; and the U12 scenes: tabs-streaming (three tabs,
 * one streaming and one waiting, then a quit mid-stream) and grid-million (a
 * million rows scrolled end to end, with the frame times logged).
 *
 * --outline-tags, with any scene, outlines every tagged rectangle the adapter
 * reports, so a screenshot shows where clicks land (U16).
 */
#include "focus.h"
#include "frame.h"
#include "sc_ui.h"
#include "theme.h"
#include "view_grid.h"

#define FRAME_BYTES (4u << 20)
#define EVENT_BYTES (1u << 16)
#define INPUT_BYTES (INPUT_CAP + 64u)
#define CLIP_BYTES (INPUT_CAP + 64u)
#define GRID_BYTES (GRID_MAX_COLUMNS * 4u + 64u)
#define RESULT_QUOTA 128u   /* arenas one result set may hold: about 1.5 million int rows */
#define TAB_BYTES (TABS_MAX * (sizeof(query_tab) + 16u) + RESULT_QUOTA * sizeof(size_t) + 256u)
#define MEASURE_FRAMES 200u
#define GRID_SCRATCH GRID_SCRATCH_BYTES
#define FRAME_NODES 1024
#define FRAME_DRAWS 16384
#define LAYOUT_TAGS_MAX 64u
#define LAYOUT_BYTES (LAYOUT_TAGS_MAX * sizeof(ui_rect) + 64u)
#define RESULT_ARENAS 160u  /* the quota, the window, a terminal and reserves, and room for a second tab */
#define ACTIVE_WAIT_MS 100u /* KTD6: the wait while a query is active */
#define IDLE_WAIT_MS 1000u
#define CLOSE_WAIT_MS 2000u /* plan U8: quit waits this long for BYE */
#define SCENE_WAIT_MS 15000u
#define FRAME_FAILURES 10u /* frames in a row that may fail before the app gives up */

typedef struct {
    ui_color page, ink, muted, rule, header;
} palette;

static palette palette_for(bool dark) {
    palette light = {{0xff, 0xff, 0xff, 0xff}, {0x1d, 0x1d, 0x1f, 0xff}, {0x6e, 0x6e, 0x73, 0xff},
                     {0xd8, 0xd8, 0xdc, 0xff}, {0xf2, 0xf2, 0xf4, 0xff}};
    palette night = {{0x1e, 0x1e, 0x1e, 0xff}, {0xf5, 0xf5, 0xf7, 0xff}, {0x98, 0x98, 0x9d, 0xff},
                     {0x3a, 0x3a, 0x3c, 0xff}, {0x2a, 0x2a, 0x2c, 0xff}};
    return dark ? night : light;
}

static ui_draw_item item(uint8_t kind, int32_t x, int32_t y, int32_t w, int32_t h, ui_color c) {
    ui_draw_item d = {0};
    d.kind = kind;
    d.x = x;
    d.y = y;
    d.w = w;
    d.h = h;
    d.color = c;
    return d;
}

static ui_draw_item label(sc_str text, uint8_t font, int32_t x, int32_t y, ui_color c) {
    ui_draw_item d = item(UI_DRAW_TEXT, x, y, 0, 0, c);
    d.text = text;
    d.font = font;
    d.font_size = 13;
    return d;
}

/* A two-row table drawn as a custom region, the way the result grid will be
 * (KTD3): a header band, rules, and cell text the app positions itself. */
static bool table_sample(frame_builder *b, sc_arena *fa, palette p) {
    ui_node region = frame_box();
    region.width.mode = UI_SIZE_FIXED;
    region.width.min = 320;
    region.height.mode = UI_SIZE_FIXED;
    region.height.min = 72;
    region.border = 1;
    region.border_color = p.rule;
    return frame_draw(b, item(UI_DRAW_RECT, 0, 0, 320, 24, p.header)) &&
           frame_draw(b, item(UI_DRAW_LINE, 0, 24, 320, 0, p.rule)) &&
           frame_draw(b, item(UI_DRAW_LINE, 0, 48, 320, 0, p.rule)) &&
           frame_draw(b, item(UI_DRAW_LINE, 64, 0, 0, 72, p.rule)) &&
           frame_draw(b, label(SC_STR_LIT(fa, "id"), UI_FONT_SANS_MEDIUM, 8, 4, p.muted)) &&
           frame_draw(b, label(SC_STR_LIT(fa, "name"), UI_FONT_SANS_MEDIUM, 72, 4, p.muted)) &&
           frame_draw(b, label(SC_STR_LIT(fa, "1"), UI_FONT_MONO, 8, 28, p.ink)) &&
           frame_draw(b, label(SC_STR_LIT(fa, "Ada Lovelace"), UI_FONT_SANS, 72, 28, p.ink)) &&
           frame_draw(b, label(SC_STR_LIT(fa, "2"), UI_FONT_MONO, 8, 52, p.ink)) &&
           frame_draw(b, label(SC_STR_LIT(fa, "Grace Hopper, whose name runs past the edge"),
                               UI_FONT_SANS, 72, 52, p.ink)) &&
           frame_custom(b, region);
}

/* "Hello" in the UI font and in the mono font, and a custom region: the U2
 * check that both fonts load and draw sharply and that draw records land in
 * their region, clipped to it. */
static bool hello_scene(sc_arena *fa, bool dark, ui_frame *out) {
    palette p = palette_for(dark);
    frame_builder b;
    ui_node page = frame_box();
    page.width.mode = UI_SIZE_GROW;
    page.height.mode = UI_SIZE_GROW;
    page.direction = UI_COLUMN;
    page.pad_left = 32;
    page.pad_right = 32;
    page.pad_top = 32;
    page.pad_bottom = 32;
    page.gap = 12;
    page.background = p.page;
    return frame_begin(&b, fa, FRAME_NODES, FRAME_DRAWS) && frame_open(&b, page) &&
           frame_text(&b, SC_STR_LIT(fa, "Hello"), UI_FONT_SANS, 32, p.ink) &&
           frame_text(&b, SC_STR_LIT(fa, "Hello from Inter, the interface font."), UI_FONT_SANS,
                      13, p.muted) &&
           frame_text(&b, SC_STR_LIT(fa, "Hello"), UI_FONT_MONO, 32, p.ink) &&
           frame_text(&b, SC_STR_LIT(fa, "SELECT 'hello' FROM JetBrains Mono;"), UI_FONT_MONO,
                      13, p.muted) &&
           table_sample(&b, fa, p) && frame_close(&b) && frame_end(&b, out);
}

enum { SCENE_SHELL = 0, SCENE_HELLO = 1, SCENE_QUERY = 2 };

/* The empty query view, before any connection. */
static bool shell_scene(sc_arena *fa, bool dark, ui_frame *out) {
    frame_builder b;
    shell_state s;
    s.dark = dark;
    s.show_sidebar = true;
    s.object = SC_STR_LIT(fa, "Query");
    s.server = SC_STR_LIT(fa, "No server");
    s.connection = SHELL_OFFLINE;
    s.connection_label = SC_STR_LIT(fa, "Not connected");
    s.result_label = SC_STR_LIT(fa, "Query Result");
    s.result_count = SC_STR_LIT(fa, "No rows");
    s.status = SHELL_STATUS_NONE;
    s.elapsed = SC_STR_LIT(fa, "");
    s.placeholder = SC_STR_LIT(fa, "Run a query to see its results here.");
    s.strip = NULL;
    s.strip_ctx = NULL;
    return frame_begin(&b, fa, FRAME_NODES, FRAME_DRAWS) && shell_build(&b, &s) &&
           frame_end(&b, out);
}

/* One result tab (plan U12): its SQL, its grid and its last result. The
 * tab that owns the link's run reads the link's result and conn_ui; any other
 * reads the result it kept and a copy of the conn_ui that ran it. */
typedef struct {
    input_box input;
    grid_state grid;
    result res;      /* the kept result, or an empty one on the tab's book */
    conn_ui shown;   /* the kept result's query, copied when it ended; blank before */
    uint64_t req, sets; /* the set `res` holds */
    bool kept;       /* `res` and `shown` hold pool arenas to free */
    uint64_t limit;  /* the waiting run's row limit; 0 is the connection's */
    bool ran;        /* a run went out */
    bool timing;     /* the run is active; its clock is running */
    uint64_t started_ms;
    uint64_t elapsed_ms;
    uint8_t notice;
} query_tab;

/* The query window: one connection shared by up to TABS_MAX tabs. Tabs are
 * values in `tabs`, the open ones first and then the spares, each with its own
 * arenas from the pools; a closed tab moves to the end and becomes a spare. */
typedef struct {
    conn_link link;
    bool linked;       /* link_init succeeded; link_free is owed */
    bool custom_conninfo;
    sc_mem_pool inputs, grids, books; /* a tab's input, width cache and result book */
    bool pools;
    sc_arena *tab_arena; /* holds `tabs`, the book's waits and `freed` */
    sc_tslice tabs;    /* query_tab, TABS_MAX */
    sc_tslice freed;   /* size_t, RESULT_QUOTA: result_close's indices */
    tab_book book;     /* which tabs are open, selected, live and waiting (tabs.h) */
    sc_arena *scratch; /* the grids' detail-pane scratch, shared */
    ui_layout layout;
    focus_area focus;
} app;

static bool get_tab(const app *a, size_t i, query_tab *t) {
    return SC_TSLICE_GET(query_tab, a->tab_arena, a->tabs, i, t);
}

static bool put_tab(app *a, size_t i, const query_tab *t) {
    return SC_TSLICE_SET(query_tab, a->tab_arena, a->tabs, i, t);
}

/* A broken link gets no more replies, so its query is over too. */
static bool query_active(const app *a) {
    return conn_ui_active(&a->link.ui) && !a->link.broken;
}

/* Frees the arenas a tab's kept result holds. A broken link frees nothing;
 * its pool goes when the app quits. */
static void free_kept(app *a, query_tab *t) {
    if (!t->kept) {
        return;
    }
    size_t n = result_close(&t->res, a->tab_arena, a->freed);
    for (size_t i = 0; i < n; i++) {
        size_t idx = 0;
        if (SC_TSLICE_GET(size_t, a->tab_arena, a->freed, i, &idx)) {
            (void)link_free_kept(&a->link, idx);
        }
    }
    if (t->shown.terminal_kept) {
        (void)link_free_kept(&a->link, t->shown.terminal);
    }
    conn_ui blank = {0};
    t->shown = blank;
    t->kept = false;
}

static uint8_t refusal_notice(const app *a) {
    conn_refuse why = a->link.refusal;
    return why == CONN_REFUSE_BUSY            ? QUERY_NOTICE_BUSY
           : why == CONN_REFUSE_NOT_CONNECTED ? QUERY_NOTICE_NOT_CONNECTED
                                              : QUERY_NOTICE_POOL;
}

/* Starts tab `i`'s run: its old result is freed first, so the run has the
 * arenas, and its empty result becomes the one the link fills. */
static void start(app *a, size_t i) {
    query_tab t;
    if (!get_tab(a, i, &t)) {
        return;
    }
    /* Refuse before freeing anything, so a refused run keeps the old result
     * (as conn.c does for a single result): the run needs a live or opening
     * connection and two free arenas, counting the ones its result frees. */
    ui_conn_state st = a->link.ui.state;
    size_t reclaim = t.kept ? t.shown.held_count + (t.shown.terminal_kept ? 1u : 0u) : 0u;
    tab_run_check can = tab_can_run(
        !a->link.broken && (st == UI_CONN_CONNECTED || st == UI_CONN_CONNECTING),
        a->link.book.free, reclaim);
    if (can != TAB_RUN_OK) {
        tab_book_refused(&a->book, i);
        t.notice = can == TAB_RUN_POOL ? QUERY_NOTICE_POOL : QUERY_NOTICE_NOT_CONNECTED;
        (void)put_tab(a, i, &t);
        return;
    }
    free_kept(a, &t);
    link_adopt(&a->link, t.res);
    bool sent = link_run_limit(&a->link, t.input.arena, input_text(&t.input), t.limit);
    if (!sent || a->link.refused) {
        t.notice = sent ? refusal_notice(a) : QUERY_NOTICE_NOT_CONNECTED;
        (void)link_abandon(&a->link); /* nothing went out; back to the idle result */
        tab_book_refused(&a->book, i);
        (void)put_tab(a, i, &t);
        return;
    }
    tab_book_started(&a->book, i);
    t.notice = QUERY_NOTICE_NONE;
    a->focus = FOCUS_EDITOR;
    t.ran = true;
    t.timing = true;
    t.started_ms = sc_clock_ms();
    t.elapsed_ms = 0;
    (void)put_tab(a, i, &t);
}

/* Once the live run ends its result passes to its tab, and the tab that has
 * waited longest starts. */
static void settle(app *a) {
    query_tab t;
    size_t done = tab_book_finished(&a->book, query_active(a));
    if (done != TAB_NONE && get_tab(a, done, &t)) {
        t.shown = a->link.ui; /* before link_keep, which forgets the arenas */
        if (a->link.broken && conn_ui_active(&t.shown)) {
            t.shown.q = Q_DISCONNECTED; /* no reply will end it */
        }
        t.req = a->link.res_req;
        t.sets = a->link.res_sets;
        (void)link_keep(&a->link, &t.res);
        t.kept = true;
        if (t.timing) {
            t.elapsed_ms = sc_clock_ms() - t.started_ms;
            t.timing = false;
        }
        (void)put_tab(a, done, &t);
    }
    size_t next = tab_book_next(&a->book, query_active(a));
    if (next != TAB_NONE) {
        start(a, next);
    }
}

/* Advances the live run's clock. */
static void tick(app *a) {
    query_tab t;
    if (a->book.live != TAB_NONE && get_tab(a, a->book.live, &t) && t.timing) {
        t.elapsed_ms = sc_clock_ms() - t.started_ms;
        (void)put_tab(a, a->book.live, &t);
    }
}

/* Runs the selected tab's SQL with `limit` (0: the connection's), or queues
 * it behind the connection's run. */
static void run(app *a, uint64_t limit) {
    size_t i = a->book.sel;
    query_tab t;
    if (!get_tab(a, i, &t)) {
        return;
    }
    t.limit = limit;
    tab_run_kind k = tab_book_run(&a->book, i, query_active(a));
    t.notice = k == TAB_RUN_BUSY ? QUERY_NOTICE_BUSY : QUERY_NOTICE_NONE;
    if (k == TAB_RUN_QUEUED) {
        a->focus = FOCUS_EDITOR;
    }
    (void)put_tab(a, i, &t);
    if (k == TAB_RUN_START) {
        start(a, i);
    }
}

/* Opens a tab after the last one and selects it. */
static bool new_tab(app *a) {
    query_tab t;
    size_t i = a->book.count;
    if (i >= TABS_MAX || !get_tab(a, i, &t) || !sc_arena_reset(t.input.arena) ||
        !input_init(&t.input, t.input.arena, INPUT_CAP) || !sc_arena_reset(t.grid.wa)) {
        return false;
    }
    sc_arena *wa = t.grid.wa;
    grid_state_init(&t.grid, wa, a->scratch);
    sc_tslice none = {{NULL, 0, 0}, 0};
    (void)result_close(&t.res, NULL, none);
    conn_ui blank = {0};
    t.shown = blank;
    t.req = 0;
    t.sets = 0;
    t.kept = false;
    t.limit = 0;
    t.ran = false;
    t.timing = false;
    t.started_ms = 0;
    t.elapsed_ms = 0;
    t.notice = QUERY_NOTICE_NONE;
    return put_tab(a, i, &t) && tab_book_open(&a->book);
}

/* Closes tab `i`: a live run is abandoned (CANCEL, its arenas freed now and
 * its late replies on arrival), a kept result freed. The last tab closed
 * leaves a fresh one. */
static void close_tab(app *a, size_t i) {
    query_tab t;
    size_t count = a->book.count;
    if (i >= count || !get_tab(a, i, &t)) {
        return;
    }
    if (tab_book_close(&a->book, i)) {
        (void)link_abandon(&a->link);
    }
    free_kept(a, &t);
    for (size_t k = i; k + 1u < count; k++) { /* the rest move left, as the book's did */
        query_tab n;
        if (get_tab(a, k + 1u, &n)) {
            (void)put_tab(a, k, &n);
        }
    }
    (void)put_tab(a, count - 1u, &t); /* a spare now */
    if (a->book.count == 0) {
        (void)new_tab(a);
    }
    settle(a); /* a waiting tab may start */
}

static bool tab_command(app *a, tab_key k) {
    switch (k.kind) {
    case TAB_KEY_NEW:
        return new_tab(a);
    case TAB_KEY_CLOSE:
        close_tab(a, a->book.sel);
        return true;
    case TAB_KEY_SELECT:
        if (k.tab >= a->book.count || k.tab == a->book.sel) {
            return false;
        }
        a->book.sel = k.tab;
        return true;
    case TAB_KEY_LOAD_ALL:
        run(a, UINT64_MAX);
        return true;
    default:
        return false;
    }
}

static bool query_scene(sc_arena *fa, app *a, bool dark, ui_frame *out, bool *grew) {
    frame_builder b;
    grid_view v;
    query_tab t;
    uint32_t w = 0, h = 0;
    *grew = false;
    if (!get_tab(a, a->book.sel, &t)) {
        return false;
    }
    ui_window_size(&w, &h);
    bool live = a->book.live == a->book.sel;
    sc_tslice tabs = SC_TSLICE_ALLOC(view_tab, fa, a->book.count);
    bool ok = sc_tslice_len(tabs) == a->book.count;
    for (size_t k = 0; ok && k < a->book.count; k++) {
        query_tab o = t;
        ok = k == a->book.sel || get_tab(a, k, &o);
        view_tab vt = {o.input.arena, input_text(&o.input), a->book.live == k && query_active(a),
                       tab_book_waiting(&a->book, k)};
        ok = ok && SC_TSLICE_SET(view_tab, fa, tabs, k, &vt);
    }
    v.dark = dark;
    v.link = &a->link;
    v.ui = live ? &a->link.ui : &t.shown;
    v.res = live ? &a->link.res : &t.res;
    v.res_req = live ? a->link.res_req : t.req;
    v.res_sets = live ? a->link.res_sets : t.sets;
    v.waiting = tab_book_waiting(&a->book, a->book.sel);
    v.tabs_arena = fa;
    v.tabs = tabs;
    v.tab_count = a->book.count;
    v.tab_selected = a->book.sel;
    v.input = &t.input;
    v.custom_conninfo = a->custom_conninfo;
    v.elapsed_ms = t.elapsed_ms;
    v.ran = t.ran;
    v.notice = t.notice;
    v.width = w;
    v.height = h;
    v.grid = &t.grid;
    ok = ok && frame_begin(&b, fa, FRAME_NODES, FRAME_DRAWS) && grid_build(&b, &v) &&
         frame_end(&b, out);
    *grew = t.grid.grew;
    return put_tab(a, a->book.sel, &t) && ok; /* the view updated the grid state */
}

static bool build(sc_arena *fa, uint8_t scene, bool dark, app *a, ui_frame *f) {
    bool grew = false;
    if (!sc_arena_reset(fa)) {
        return false;
    }
    bool built = scene == SCENE_HELLO   ? hello_scene(fa, dark, f)
                 : scene == SCENE_QUERY ? query_scene(fa, a, dark, f, &grew)
                                        : shell_scene(fa, dark, f);
    if (built && grew) { /* once, with the wider columns */
        built = sc_arena_reset(fa) && query_scene(fa, a, dark, f, &grew);
    }
    return built;
}

static bool draw(sc_arena *fa, uint8_t scene, bool dark, app *a) {
    ui_frame f;
    return build(fa, scene, dark, a, &f) && ui_draw(fa, f, &a->layout);
}

/* What a scene runs once connected. */
enum { RUN_NONE = 0, RUN_ROWS, RUN_WIDE, RUN_TAG, RUN_ERROR, RUN_ERROR_ROWS, RUN_TERMINATE,
       RUN_GRID, RUN_NULL_WRAP, RUN_CAPPED, RUN_DETAIL, RUN_TABS, RUN_MILLION };

typedef struct {
    bool has_shot;
    sc_str shot;
    bool has_scene;
    uint8_t scene;
    uint8_t run; /* RUN_*, for SCENE_QUERY */
    uint64_t select; /* a grid scene selects this row, after scrolling past `scroll` rows; 0: none */
    uint64_t scroll;
    bool detail;
    bool wrong_password;
    bool dark;
    bool theme_fixed; /* the scene names its theme; THEME events still switch it */
    bool outline;
    sc_str conninfo;
} options;

static bool query_scene_named(const sc_arena *a, sc_str v, options *o) {
    o->run = SC_STR_EQ_LIT(a, v, "query-rows")          ? RUN_ROWS
             : SC_STR_EQ_LIT(a, v, "query-wide")        ? RUN_WIDE
             : SC_STR_EQ_LIT(a, v, "query-tag")         ? RUN_TAG
             : SC_STR_EQ_LIT(a, v, "query-error")       ? RUN_ERROR
             : SC_STR_EQ_LIT(a, v, "query-error-rows")  ? RUN_ERROR_ROWS
             : SC_STR_EQ_LIT(a, v, "conn-disconnected") ? RUN_TERMINATE
             : SC_STR_EQ_LIT(a, v, "grid-light") || SC_STR_EQ_LIT(a, v, "grid-dark") ? RUN_GRID
             : SC_STR_EQ_LIT(a, v, "grid-null-wrap")        ? RUN_NULL_WRAP
             : SC_STR_EQ_LIT(a, v, "grid-error-after-rows") ? RUN_ERROR_ROWS
             : SC_STR_EQ_LIT(a, v, "grid-capped")           ? RUN_CAPPED
             : SC_STR_EQ_LIT(a, v, "grid-detail")           ? RUN_DETAIL
             : SC_STR_EQ_LIT(a, v, "tabs-streaming")        ? RUN_TABS
             : SC_STR_EQ_LIT(a, v, "grid-million")          ? RUN_MILLION
                                                            : RUN_NONE;
    /* Rows are 1-based here so 0 can mean none. */
    o->scroll = o->run == RUN_GRID ? 40u : o->run == RUN_CAPPED ? 9990u : 0;
    o->select = o->run == RUN_GRID ? 44u : o->run == RUN_NULL_WRAP ? 2u : o->run == RUN_DETAIL ? 1u : 0;
    o->detail = o->run == RUN_DETAIL;
    o->wrong_password = SC_STR_EQ_LIT(a, v, "conn-auth-failed");
    return o->run != RUN_NONE || o->wrong_password;
}

/* Returns false on an unknown argument or scene. */
static bool parse_args(const sc_arena *a, sc_tslice args, options *o) {
    sc_str key, value;
    size_t n = sc_tslice_len(args);
    sc_str empty = {NULL, 0, a->epoch};
    o->has_shot = false;
    o->has_scene = false;
    o->scene = SCENE_QUERY;
    o->run = RUN_NONE;
    o->select = 0;
    o->scroll = 0;
    o->detail = false;
    o->wrong_password = false;
    o->dark = false;
    o->theme_fixed = false;
    o->outline = false;
    o->conninfo = empty;
    for (size_t i = 1; i < n; i += 2) {
        if (SC_TSLICE_GET(sc_str, a, args, i, &key) && SC_STR_EQ_LIT(a, key, "--outline-tags")) {
            o->outline = true;
            i--; /* a flag: no value follows */
            continue;
        }
        if (!SC_TSLICE_GET(sc_str, a, args, i, &key) || i + 1 >= n ||
            !SC_TSLICE_GET(sc_str, a, args, i + 1, &value)) {
            return false;
        }
        if (SC_STR_EQ_LIT(a, key, "--screenshot")) {
            o->has_shot = true;
            o->shot = value;
        } else if (SC_STR_EQ_LIT(a, key, "--conninfo")) {
            o->conninfo = value;
        } else if (!SC_STR_EQ_LIT(a, key, "--scene")) {
            return false;
        } else if (SC_STR_EQ_LIT(a, value, "hello")) {
            /* hello follows the system theme; the other scenes name theirs. */
            o->has_scene = true;
            o->scene = SCENE_HELLO;
            o->theme_fixed = false;
        } else if (SC_STR_EQ_LIT(a, value, "shell-light") || SC_STR_EQ_LIT(a, value, "shell-dark")) {
            o->has_scene = true;
            o->scene = SCENE_SHELL;
            o->dark = SC_STR_EQ_LIT(a, value, "shell-dark");
            o->theme_fixed = true;
        } else if (query_scene_named(a, value, o)) {
            o->has_scene = true;
            o->scene = SCENE_QUERY;
            o->dark = SC_STR_EQ_LIT(a, value, "grid-dark");
            o->theme_fixed = true;
        } else {
            return false;
        }
    }
    return true;
}

/* The worker's doorbell (KTD6). */
static void ring(void *arg) {
    (void)arg;
    (void)ui_wake();
}

/* The connection, the tab pools and one open tab. */
static bool app_open(app *a, sc_arena *tab_arena, sc_arena *scratch_arena, sc_arena *scratch,
                     const sc_arena *args_arena, const options *o) {
    conn_limits lim = {4, RESULT_QUOTA, 10000};
    a->custom_conninfo = o->conninfo.len > 0;
    a->linked = false;
    a->pools = false;
    a->tab_arena = tab_arena;
    a->scratch = scratch_arena;
    a->tabs = SC_TSLICE_ALLOC(query_tab, tab_arena, TABS_MAX);
    a->freed = SC_TSLICE_ALLOC(size_t, tab_arena, RESULT_QUOTA);
    if (!tab_book_init(&a->book, tab_arena) || sc_tslice_len(a->tabs) != TABS_MAX ||
        sc_tslice_len(a->freed) != RESULT_QUOTA) {
        return false;
    }
    if (!sc_mem_pool_new(&a->inputs, TABS_MAX, INPUT_BYTES).ok) {
        return false;
    }
    if (!sc_mem_pool_new(&a->grids, TABS_MAX, GRID_BYTES).ok) {
        (void)sc_mem_pool_free(&a->inputs);
        return false;
    }
    if (!sc_mem_pool_new(&a->books, TABS_MAX, result_book_bytes(RESULT_QUOTA)).ok) {
        (void)sc_mem_pool_free(&a->grids);
        (void)sc_mem_pool_free(&a->inputs);
        return false;
    }
    a->pools = true;
    for (size_t i = 0; i < TABS_MAX; i++) {
        query_tab t = {0};
        sc_arena *in = sc_mem_arena(&a->inputs, i);
        sc_arena *wa = sc_mem_arena(&a->grids, i);
        sc_arena *book = sc_mem_arena(&a->books, i);
        if (in == NULL || wa == NULL || book == NULL || !input_init(&t.input, in, INPUT_CAP) ||
            !result_init(&t.res, book, RESULT_QUOTA)) {
            return false;
        }
        grid_state_init(&t.grid, wa, scratch_arena);
        if (!put_tab(a, i, &t)) {
            return false;
        }
    }
    a->linked = true; /* link_free is safe after a failed link_init */
    if (!link_init(&a->link, 1, RESULT_ARENAS, lim, ring, NULL)) {
        return false;
    }
    if (o->wrong_password) {
        return sc_arena_reset(scratch) &&
               link_open(&a->link, scratch, SC_STR_LIT(scratch, "password=wrong"));
    }
    return link_open(&a->link, args_arena, o->conninfo);
}

static void insert(query_tab *t, const sc_arena *sa, sc_str s) {
    input_status st = input_insert(&t->input, sa, s);
    t->notice = st == INPUT_TOO_BIG     ? QUERY_NOTICE_TOO_BIG
                : st == INPUT_NOT_TEXT  ? QUERY_NOTICE_NOT_TEXT
                                        : QUERY_NOTICE_NONE;
}

static void paste(query_tab *t, sc_arena *clip) {
    sc_str s;
    if (!sc_arena_reset(clip)) {
        return;
    }
    uint8_t got = ui_clipboard(clip, INPUT_CAP, &s);
    if (got == UI_CLIP_TOO_BIG) {
        t->notice = QUERY_NOTICE_CLIP_TOO_BIG;
    } else if (got == UI_CLIP_OK) {
        insert(t, clip, s);
    }
}

/* Returns true when the key changed what the window shows. */
static bool key(app *a, sc_arena *events, sc_arena *clip, ui_event ev) {
    query_tab t;
    if (!get_tab(a, a->book.sel, &t)) {
        return false;
    }
    bool mine = a->book.live == a->book.sel && query_active(a);
    a->focus = focus_shown(a->focus, &a->layout);
    focus_key k = focus_route(a->focus, ev.key, ev.mods, ui_platform(), mine);
    bool dirty = true;
    switch (k.to) {
    case FOCUS_TO_TAB:
        return tab_command(a, k.tab);
    case FOCUS_TO_RUN:
        run(a, 0);
        return true;
    case FOCUS_TO_CANCEL:
        return link_cancel(&a->link);
    case FOCUS_TO_DETAIL:
        t.grid.detail = !t.grid.detail;
        break;
    case FOCUS_TO_GRID:
        dirty = grid_move(&t.grid.sel, k.grid);
        break;
    case FOCUS_TO_EDITOR:
        switch (k.input) {
        case INPUT_KEY_NEWLINE:
            insert(&t, events, SC_STR_LIT(events, "\n"));
            break;
        case INPUT_KEY_TAB:
            insert(&t, events, SC_STR_LIT(events, "\t"));
            break;
        case INPUT_KEY_BACKSPACE:
            input_backspace(&t.input);
            break;
        case INPUT_KEY_LEFT:
            input_left(&t.input);
            break;
        case INPUT_KEY_RIGHT:
            input_right(&t.input);
            break;
        case INPUT_KEY_UP:
            input_up(&t.input);
            break;
        case INPUT_KEY_DOWN:
            input_down(&t.input);
            break;
        case INPUT_KEY_LINE_START:
            input_line_start(&t.input);
            break;
        case INPUT_KEY_LINE_END:
            input_line_end(&t.input);
            break;
        case INPUT_KEY_SELECT_ALL:
            input_select_all(&t.input);
            break;
        case INPUT_KEY_PASTE:
            paste(&t, clip);
            break;
        default:
            dirty = false;
            break;
        }
        break;
    default:
        return false;
    }
    return put_tab(a, a->book.sel, &t) && dirty;
}

/* A left click on the strip selects, closes or opens a tab. Any other left
 * click focuses the area it lands on, and in the grid selects the row under
 * the pointer. Both are tested against the last frame's rectangles (KTD18). */
static bool pointer(app *a, ui_event ev) {
    tab_hit hit = tab_hit_at(&a->layout, a->book.count, ev.x, ev.y, ev.button);
    if (hit.kind == TAB_HIT_SELECT) {
        bool changed = hit.tab != a->book.sel;
        a->book.sel = hit.tab;
        return changed;
    }
    if (hit.kind == TAB_HIT_CLOSE) {
        close_tab(a, hit.tab);
        return true;
    }
    if (hit.kind == TAB_HIT_NEW) {
        return new_tab(a);
    }
    uint32_t tag = frame_hit(&a->layout, ev.x, ev.y);
    focus_area was = a->focus;
    a->focus = focus_click(a->focus, tag, ev.button);
    ui_rect r;
    query_tab t;
    if (ev.button != UI_BUTTON_LEFT || tag != FOCUS_TAG_GRID ||
        !frame_rect(&a->layout, FOCUS_TAG_GRID, &r) || !get_tab(a, a->book.sel, &t)) {
        return was != a->focus;
    }
    bool dirty = grid_click(&t.grid.sel, (int64_t)ev.y - r.y) || was != a->focus;
    return put_tab(a, a->book.sel, &t) && dirty;
}

static bool wheel(app *a, ui_event ev) {
    query_tab t;
    if (frame_hit(&a->layout, ev.x, ev.y) != FOCUS_TAG_GRID || !get_tab(a, a->book.sel, &t)) {
        return false;
    }
    bool moved = grid_wheel(&t.grid.sel, ev.wheel_x, ev.wheel_y);
    return put_tab(a, a->book.sel, &t) && moved;
}

static bool text_typed(app *a, const sc_arena *sa, sc_str s) {
    query_tab t;
    a->focus = focus_shown(a->focus, &a->layout);
    if (!focus_takes_text(a->focus) || !get_tab(a, a->book.sel, &t)) {
        return false;
    }
    insert(&t, sa, s);
    return put_tab(a, a->book.sel, &t);
}

/* Waits up to `ms` for one input event (UI_EVENT_NONE on a timeout), then
 * drains the worker's replies: after a ring, and on every timeout too, since a
 * ring can be lost (KTD6). True when a reply arrived. */
static bool wait_and_poll(app *a, sc_arena *events, uint32_t ms, ui_event *ev) {
    if (!sc_arena_reset(events) || !ui_wait(events, ms, ev)) {
        ev->kind = UI_EVENT_NONE;
    }
    bool any = a->linked && link_poll(&a->link);
    if (a->linked) {
        tick(a);
        settle(a);
    }
    return any;
}

/* Sends CLOSE, waits up to 2 s for BYE, and joins the worker; ui_close must
 * wait for the join, since the worker rings through ui_wake. A worker that
 * sent no BYE in time is still joined: link_join closes its request channel,
 * which makes it return. Kept results go with the pool. */
static void app_close(app *a, sc_arena *events) {
    if (a->linked) {
        if (a->link.started) {
            (void)link_close(&a->link);
            uint64_t end = sc_clock_ms() + CLOSE_WAIT_MS;
            ui_event ev;
            while (a->link.ui.state != UI_CONN_CLOSED && !a->link.broken && sc_clock_ms() < end) {
                (void)wait_and_poll(a, events, 50, &ev);
            }
            (void)link_join(&a->link);
        }
        link_free(&a->link);
        a->linked = false;
    }
    if (a->pools) {
        (void)sc_mem_pool_free(&a->books);
        (void)sc_mem_pool_free(&a->grids);
        (void)sc_mem_pool_free(&a->inputs);
        a->pools = false;
    }
}

static sc_str scene_sql(sc_arena *a, uint8_t run) {
    switch (run) {
    case RUN_ROWS:
        return SC_STR_LIT(a, "SELECT g AS id,\n"
                             "       CASE g WHEN 3 THEN 'Zo\xC3\xAB \xC3\x85ngstr\xC3\xB6m'\n"
                             "              WHEN 5 THEN 'stacked e' || repeat(U&'\\0301', 12)\n"
                             "              ELSE 'user ' || g END AS name,\n"
                             "       CASE WHEN g % 7 = 0 THEN NULL ELSE g * 1.25 END AS score,\n"
                             "       g % 2 = 0 AS even\n"
                             "FROM generate_series(1, 250) AS g;");
    case RUN_WIDE:
        /* LATIN1 makes the server send caf\xE9 as one byte, not UTF-8: the cell
         * shows U+FFFD for it. */
        return SC_STR_LIT(a, "SET client_encoding TO 'LATIN1';\n"
                             "SELECT g AS id, 'caf\xC3\xA9' AS latin1, repeat('W', 2000) AS wide,\n"
                             "       repeat('m', 2000) AS next\n"
                             "FROM generate_series(1, 3) AS g;");
    case RUN_TAG:
        return SC_STR_LIT(a, "CREATE TEMP TABLE notes (id serial PRIMARY KEY, body text);");
    case RUN_ERROR:
        return SC_STR_LIT(a, "SELECT relnam\nFROM pg_class;");
    case RUN_ERROR_ROWS:
        return SC_STR_LIT(a, "SELECT g, 100 / (60 - g) AS ratio\nFROM generate_series(1, 100) AS g;");
    case RUN_TERMINATE:
        return SC_STR_LIT(a, "SELECT pg_terminate_backend(pg_backend_pid());");
    case RUN_GRID:
        return SC_STR_LIT(a, "SELECT g AS id, 'customer ' || g AS name,\n"
                             "       round(g * 19.99, 2) AS total,\n"
                             "       CASE WHEN g % 5 = 0 THEN NULL ELSE g % 3 = 0 END AS active,\n"
                             "       date '2026-01-01' + g AS since\n"
                             "FROM generate_series(1, 5000) AS g;");
    case RUN_NULL_WRAP:
        return SC_STR_LIT(a, "SELECT g AS id,\n"
                             "       CASE WHEN g % 2 = 0 THEN NULL\n"
                             "            ELSE 'line one' || chr(10) || 'line two' || chr(9) || 'tab' END AS note,\n"
                             "       CASE WHEN g % 3 = 0 THEN NULL ELSE g END AS maybe\n"
                             "FROM generate_series(1, 40) AS g;");
    case RUN_CAPPED:
        return SC_STR_LIT(a, "SELECT g AS n, md5(g::text) AS hash\nFROM generate_series(1, 50000) AS g;");
    case RUN_DETAIL:
        return SC_STR_LIT(a, "SELECT 42 AS id, 'Zo\xC3\xAB \xC3\x85ngstr\xC3\xB6m' AS name,\n"
                             "       NULL::text AS nickname,\n"
                             "       'A note that is long enough to wrap across several lines of the pane, '\n"
                             "       || 'with an emoji \xF0\x9F\x98\x80 in it.' || chr(10) || 'And a second paragraph.' AS note,\n"
                             "       repeat('x', 10240) AS big, now()::date - date '2026-01-01' AS days;");
    case RUN_TABS:
        /* The first tab of the scene; the others are in play_tabs. */
        return SC_STR_LIT(a, "SELECT count(*) AS tables FROM pg_class WHERE relkind = 'r';");
    case RUN_MILLION:
        return SC_STR_LIT(a, "SELECT g AS n\nFROM generate_series(1, 1000000) AS g;");
    default:
        return SC_STR_LIT(a, "");
    }
}

/* Types `sql` (live in `sa`) into the selected tab, as typing would. */
static bool type_sql(app *a, const sc_arena *sa, sc_str sql) {
    query_tab t;
    return get_tab(a, a->book.sel, &t) && input_insert(&t.input, sa, sql) == INPUT_OK &&
           put_tab(a, a->book.sel, &t);
}

/* Waits until no run is live or waiting, or `until` says stop. */
static void wait_runs(app *a, sc_arena *events, uint64_t end) {
    ui_event ev;
    while ((a->book.live != TAB_NONE || query_active(a) ||
            tab_next_waiting(a->book.arena, a->book.waits, a->book.count) != SIZE_MAX) &&
           !a->link.broken && sc_clock_ms() < end) {
        (void)wait_and_poll(a, events, 50, &ev);
    }
}

/* Plays a scene: connects, types and runs its SQL, and waits for the end. */
static bool play(app *a, sc_arena *events, sc_arena *scratch, uint8_t run_kind) {
    ui_event ev;
    uint64_t end = sc_clock_ms() + SCENE_WAIT_MS;
    while (a->link.ui.state == UI_CONN_CONNECTING && !a->link.broken && sc_clock_ms() < end) {
        (void)wait_and_poll(a, events, 50, &ev);
    }
    if (run_kind == RUN_NONE) {
        return a->link.ui.state != UI_CONN_CONNECTING;
    }
    if (!sc_arena_reset(scratch) || !type_sql(a, scratch, scene_sql(scratch, run_kind))) {
        return false;
    }
    run(a, run_kind == RUN_MILLION ? UINT64_MAX : 0);
    wait_runs(a, events, end);
    query_tab t;
    return get_tab(a, a->book.sel, &t) && t.ran && a->book.live == TAB_NONE;
}

/* tabs-streaming: after the first tab's count, a second tab streams slowly
 * (one row per 20 ms) and a third queues behind it; the second is shown with
 * its first rows in. The app then quits mid-stream. */
static bool play_tabs(app *a, sc_arena *events, sc_arena *scratch) {
    ui_event ev;
    uint64_t end = sc_clock_ms() + SCENE_WAIT_MS;
    if (!new_tab(a) || !sc_arena_reset(scratch) ||
        !type_sql(a, scratch,
                  SC_STR_LIT(scratch, "SELECT g AS n, md5(g::text) || left(pg_sleep(0.02)::text, 0) AS hash\n"
                                      "FROM generate_series(1, 3000) AS g;"))) {
        return false;
    }
    run(a, 0);
    while (a->link.ui.rows < 20u && query_active(a) && sc_clock_ms() < end) {
        (void)wait_and_poll(a, events, 50, &ev);
    }
    if (!new_tab(a) || !sc_arena_reset(scratch) ||
        !type_sql(a, scratch, SC_STR_LIT(scratch, "SELECT relname, relkind\nFROM pg_class\nORDER BY 1;"))) {
        return false;
    }
    run(a, 0);
    a->book.sel = 1;
    return query_active(a) && a->book.live == 1 && tab_book_waiting(&a->book, 2);
}

/* A grid scene's state after its run: one frame lays the grid out, then the
 * scroll, the selection and the pane are set as a user would set them. */
static bool pose(app *a, sc_arena *fa, bool dark, const options *o) {
    query_tab t;
    if (o->scroll == 0 && o->select == 0 && !o->detail) {
        return true;
    }
    if (!draw(fa, SCENE_QUERY, dark, a) || !get_tab(a, a->book.sel, &t)) {
        return false;
    }
    t.grid.detail = o->detail;
    a->focus = FOCUS_GRID;
    t.grid.sel.g.scroll_y = o->scroll * THEME_GRID_ROW_HEIGHT;
    if (o->select > 0) {
        t.grid.sel.selected = true;
        t.grid.sel.row = o->select - 1u;
        t.grid.sel.g.scroll_y = grid_reveal(t.grid.sel.g, o->select - 1u);
    }
    return put_tab(a, a->book.sel, &t);
}

/* Appends "<what> avg A µs, max M µs" to `l`. */
static void stat_line(text_line *l, sc_str what, const sc_arena *wa, uint64_t sum, uint64_t n,
                      uint64_t max) {
    text_clean(l, wa, what, 32);
    TEXT_LIT(l, " avg ");
    text_u64(l, n > 0 ? sum / n : 0, true);
    TEXT_LIT(l, " \xC2\xB5s, max ");
    text_u64(l, max, true);
    TEXT_LIT(l, " \xC2\xB5s");
}

/* grid-million (plan U12 step 5): scrolls a million-row result from top to
 * bottom in MEASURE_FRAMES steps, timing each frame's build and its build
 * plus software render, and logs both; then selects the last row. */
static bool measure(app *a, sc_arena *fa, sc_arena *log, bool dark) {
    query_tab t;
    if (!draw(fa, SCENE_QUERY, dark, a) || !get_tab(a, a->book.sel, &t)) {
        return false;
    }
    uint64_t max_y = grid_max_scroll_y(t.grid.sel.g);
    uint64_t build_sum = 0, build_max = 0, all_sum = 0, all_max = 0;
    for (uint64_t k = 0; k <= MEASURE_FRAMES; k++) {
        if (!get_tab(a, a->book.sel, &t)) {
            return false;
        }
        t.grid.sel.g.scroll_y = max_y / MEASURE_FRAMES * k + (k == MEASURE_FRAMES ? max_y % MEASURE_FRAMES : 0u);
        if (!put_tab(a, a->book.sel, &t)) {
            return false;
        }
        ui_frame f;
        uint64_t t0 = sc_clock_us();
        bool ok = build(fa, SCENE_QUERY, dark, a, &f);
        uint64_t t1 = sc_clock_us();
        ok = ok && ui_draw(fa, f, &a->layout);
        uint64_t t2 = sc_clock_us();
        if (!ok) {
            return false;
        }
        build_sum += t1 - t0;
        all_sum += t2 - t0;
        build_max = t1 - t0 > build_max ? t1 - t0 : build_max;
        all_max = t2 - t0 > all_max ? t2 - t0 : all_max;
    }
    if (!sc_arena_reset(log) || !get_tab(a, a->book.sel, &t)) {
        return false;
    }
    bool lost = false;
    text_line l = text_new(log, 256, &lost);
    TEXT_LIT(&l, "grid-million: ");
    text_u64(&l, t.res.rows, true);
    TEXT_LIT(&l, " rows in ");
    text_u64(&l, t.res.count, true);
    TEXT_LIT(&l, " chunks, ");
    text_u64(&l, MEASURE_FRAMES + 1u, true);
    TEXT_LIT(&l, " frames; ");
    stat_line(&l, SC_STR_LIT(log, "build"), log, build_sum, MEASURE_FRAMES + 1u, build_max);
    TEXT_LIT(&l, "; build and render");
    stat_line(&l, SC_STR_LIT(log, ""), log, all_sum, MEASURE_FRAMES + 1u, all_max);
    if (lost) {
        return false;
    }
    ui_log(log, text_view(&l));
    a->focus = FOCUS_GRID;
    (void)grid_move(&t.grid.sel, GRID_KEY_LAST);
    return put_tab(a, a->book.sel, &t);
}

int app_main(sc_arena *args_arena, sc_tslice args) {
    options o;
    app a = {0};
    sc_arena fa, events, clip, tab_arena, grid_scratch, layout;
    if (!parse_args(args_arena, args, &o)) {
        return 2;
    }
    if (!ui_arena_new(&fa, FRAME_BYTES) || !ui_arena_new(&events, EVENT_BYTES) ||
        !ui_arena_new(&clip, CLIP_BYTES) || !ui_arena_new(&tab_arena, TAB_BYTES) ||
        !ui_arena_new(&grid_scratch, GRID_SCRATCH) || !ui_arena_new(&layout, LAYOUT_BYTES) ||
        !ui_open(&fa, SC_STR_LIT(&fa, "Safe C Explorer"), 1100, 720)) {
        return 1;
    }
    a.layout.arena = &layout;
    a.layout.rects = SC_TSLICE_ALLOC(ui_rect, &layout, LAYOUT_TAGS_MAX);
    a.layout.count = 0;
    a.focus = FOCUS_EDITOR;
    ui_outline_tags(o.outline);
    bool dark = o.has_scene && o.theme_fixed ? o.dark : ui_dark_theme();
    uint8_t scene = o.has_scene ? o.scene : SCENE_QUERY;
    bool ok = scene != SCENE_QUERY ||
              app_open(&a, &tab_arena, &grid_scratch, &clip, args_arena, &o);
    if (o.has_shot) {
        if (scene == SCENE_QUERY) {
            ok = ok && play(&a, &events, &clip, o.run) &&
                 (o.run != RUN_TABS || play_tabs(&a, &events, &clip)) &&
                 (o.run != RUN_MILLION || measure(&a, &fa, &clip, dark)) &&
                 pose(&a, &fa, dark, &o);
        }
        ok = ok && ui_screenshot_next(args_arena, o.shot) && draw(&fa, scene, dark, &a);
        uint64_t quit = sc_clock_ms();
        app_close(&a, &events);
        if (o.run == RUN_TABS && sc_clock_ms() - quit > CLOSE_WAIT_MS) {
            ok = false; /* plan U12: quitting mid-stream takes under 2 s */
        }
        ui_close();
        return ok ? 0 : 1;
    }
    /* A frame that fails is skipped and the next event draws again; only a
     * run of failures ends the app. The headless path above stays strict. */
    uint32_t failures = ok && draw(&fa, scene, dark, &a) ? 0u : 1u;
    ok = ok && failures < FRAME_FAILURES;
    ui_event ev;
    while (ok) {
        uint32_t wait = a.linked && query_active(&a) ? ACTIVE_WAIT_MS : IDLE_WAIT_MS;
        bool dirty = wait_and_poll(&a, &events, wait, &ev) || a.book.live != TAB_NONE;
        if (ev.kind == UI_EVENT_QUIT) {
            break;
        }
        if (ev.kind == UI_EVENT_THEME) {
            dark = ev.dark;
        }
        dirty = dirty || ev.kind == UI_EVENT_RESIZE || ev.kind == UI_EVENT_THEME ||
                ev.kind == UI_EVENT_EXPOSE;
        if (scene == SCENE_QUERY && ev.kind == UI_EVENT_TEXT) {
            dirty = text_typed(&a, &events, ev.text) || dirty;
        }
        if (scene == SCENE_QUERY && ev.kind == UI_EVENT_POINTER_DOWN) {
            dirty = pointer(&a, ev) || dirty;
        }
        if (scene == SCENE_QUERY && ev.kind == UI_EVENT_WHEEL) {
            dirty = wheel(&a, ev) || dirty;
        }
        if (scene == SCENE_QUERY && ev.kind == UI_EVENT_KEY_DOWN) {
            dirty = key(&a, &events, &clip, ev) || dirty;
        }
        if (dirty) {
            failures = draw(&fa, scene, dark, &a) ? 0u : failures + 1u;
            ok = failures < FRAME_FAILURES;
        }
    }
    app_close(&a, &events);
    ui_close();
    return ok ? 0 : 1;
}
