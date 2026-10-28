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
 * million rows scrolled end to end, with the frame times logged); and the U13
 * scenes: query-cancelling (Cancel pressed mid-stream), query-disconnect-offer
 * (a cancel unanswered for 5 s offers Disconnect) and conn-reconnect (the
 * connection lost under rows, which stay, with Reconnect offered); and the U18
 * scene sidebar-meta-lost (the metadata link's backend ended: the sidebar's
 * error and its Retry); and the U19 scenes sidebar-light and sidebar-dark
 * (several schemas with every kind of relation, one table opened from the
 * sidebar) and sidebar-error (the Functions list, a function's definition
 * opened in a new tab, and a schema whose read failed, with its Retry row);
 * and the U20 scenes table-content-light and table-content-dark (a table
 * opened from the sidebar: its first page, its count and the pager),
 * table-structure (the same table's Structure), table-view-note (a view's
 * Content, with the note that its rows have no order) and table-back (page
 * 2, then back to the query view while the page runs, then forward: page 2
 * again); and the U21 scenes text-compose (a scripted composition and commit
 * through the adapter's IME translation) and text-clipboard (a clipboard write
 * read back, and refused writes that leave it alone), which check their events
 * and exit nonzero on a mismatch, then draw the empty shell; and the U22
 * scenes editor-light and editor-dark (two statements run with the caret then
 * moved into the second: highlighting, the statement band and a selection)
 * and editor-compose (the same with a scripted composition open at the caret).
 * Every query scene waits for the sidebar's schema list before it runs.
 *
 * --outline-tags, with any scene, outlines every tagged rectangle the adapter
 * reports, so a screenshot shows where clicks land (U16).
 */
#include "focus.h"
#include "frame.h"
#include "sc_ui.h"
#include "theme.h"
#include "meta.h"
#include "scene_sql.h"
#include "scene_text.h"
#include "view_table.h"
#include "view_editor.h"

#define FRAME_BYTES (4u << 20)
#define EVENT_BYTES (1u << 16)
#define CLIP_BYTES (EDITOR_CAP + 64u)
#define RUN_SQL_BYTES (EDITOR_CAP + 64u)
#define DEFS_BYTES EDITOR_CAP
#define TAB_TITLE_BYTES 256u
#define KEYWORD_BYTES (16u << 10)
#define GRID_BYTES (GRID_MAX_COLUMNS * 4u + 64u)
#define RESULT_QUOTA 128u   /* arenas one result set may hold: about 1.5 million int rows */
#define TAB_BYTES (TABS_MAX * (sizeof(query_tab) + NAV_MAX * sizeof(nav_entry) + 32u) + \
                   RESULT_QUOTA * sizeof(size_t) + 256u)
#define MEASURE_FRAMES 200u
#define RUN_LIMIT_CONNECTION 0u
#define GRID_SCRATCH GRID_SCRATCH_BYTES
#define FRAME_NODES 1024
#define FRAME_DRAWS 16384
#define LAYOUT_TAGS_MAX 64u
#define LAYOUT_BYTES (LAYOUT_TAGS_MAX * sizeof(ui_rect) + 64u)
#define ACTIVE_WAIT_MS 100u /* KTD6: the wait while a query is active */
#define IDLE_WAIT_MS 1000u
#define CLOSE_WAIT_MS 2000u /* plans U8 and U13: quit waits this long, in total, for BYE */
#define SCENE_WAIT_MS 15000u
#define FRAME_FAILURES 10u /* frames in a row that may fail before the app gives up */
#define CATALOG_BYTES (16u << 20)
#define CATALOG_KEEP_BYTES (64u << 10)
#define CATALOG_SQL_BYTES 4096u
#define PAGE_SQL_BYTES (32u << 10)

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

static sidebar_view sidebar_empty(sc_arena *fa, bool dark) {
    sidebar_view v = {0};
    v.dark = dark;
    v.status = SIDEBAR_CAPTION;
    v.message = SC_STR_LIT(fa, "");
    v.retry = SHELL_RECONNECT_NONE;
    v.retry_label = SC_STR_LIT(fa, "");
    return v;
}

/* The empty query view, before any connection. */
static bool shell_scene(sc_arena *fa, bool dark, ui_frame *out) {
    frame_builder b;
    shell_state s;
    s.dark = dark;
    s.show_sidebar = true;
    s.can_back = false;
    s.can_forward = false;
    s.bottom = NULL;
    s.bottom_ctx = NULL;
    s.object = SC_STR_LIT(fa, "Query");
    s.server = SC_STR_LIT(fa, "No server");
    s.connection = SHELL_OFFLINE;
    s.connection_label = SC_STR_LIT(fa, "Not connected");
    s.reconnect = SHELL_RECONNECT_NONE;
    s.reconnect_label = SC_STR_LIT(fa, "");
    s.result_label = SC_STR_LIT(fa, "Query Result");
    s.result_count = SC_STR_LIT(fa, "No rows");
    s.status = SHELL_STATUS_NONE;
    s.elapsed = SC_STR_LIT(fa, "");
    s.placeholder = SC_STR_LIT(fa, "Run a query to see its results here.");
    s.strip = NULL;
    s.strip_ctx = NULL;
    sidebar_view side = sidebar_empty(fa, dark);
    s.sidebar = sidebar_build;
    s.sidebar_ctx = &side;
    return frame_begin(&b, fa, FRAME_NODES, FRAME_DRAWS) && shell_build(&b, &s) &&
           frame_end(&b, out);
}

/* One result tab (plan U12): its SQL, its grid and its last result. The
 * tab that owns the link's run reads the link's result and conn_ui; any other
 * reads the result it kept and a copy of the conn_ui that ran it. */
typedef struct {
    editor ed;
    bool all;
    size_t run_from, run_to;
    uint64_t run_version;
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
    nav_stack nav;
    bool pending_page;
} query_tab;

/* The query window: one result pool (KTD19), and one current connection on
 * it shared by up to TABS_MAX tabs, with up to two retired ones still
 * draining (U13). Tabs are
 * values in `tabs`, the open ones first and then the spares, each with its own
 * arenas from the pools; a closed tab moves to the end and becomes a spare. */
typedef struct {
    link_pool pool;
    bool pooled;
    link_set links;    /* the current link, the metadata link and the retiring ones (links.h) */
    meta_side meta;
    bool custom_conninfo;
    sc_mem_pool editors, grids, books; /* a tab's editor, width cache and result book */
    bool pools;
    sc_arena *tab_arena; /* holds `tabs`, the book's waits and `freed` */
    sc_tslice tabs;    /* query_tab, TABS_MAX */
    sc_tslice freed;   /* size_t, RESULT_QUOTA: result_close's indices */
    tab_book book;     /* which tabs are open, selected, live and waiting (tabs.h) */
    sc_arena *scratch; /* the grids' detail-pane scratch, shared */
    sc_arena *pages;
    sc_arena edit_scratch, run_sql, keyword_a;
    sqlkw keywords;
    bool dragging;
    bool composing;
    uint8_t pointer_shape;
    ui_rect text_area;
    int32_t text_cursor;
    ui_layout layout;
    focus_area focus;
    tree_state tree;
    bool show_sidebar;
} app;

static conn_link *cur(app *a) {
    return links_cur(&a->links);
}

static bool link_usable(const conn_link *l) {
    ui_conn_state st = l->ui.state;
    return !l->broken && (st == UI_CONN_CONNECTED || st == UI_CONN_CONNECTING);
}

static bool get_tab(const app *a, size_t i, query_tab *t) {
    return SC_TSLICE_GET(query_tab, a->tab_arena, a->tabs, i, t);
}

static bool put_tab(app *a, size_t i, const query_tab *t) {
    return SC_TSLICE_SET(query_tab, a->tab_arena, a->tabs, i, t);
}

static bool showing_table(const query_tab *t) {
    nav_entry e;
    return nav_current(&t->nav, &e) && e.kind == NAV_TABLE;
}

/* A broken link gets no more replies, so its query is over too. */
static bool query_active(app *a) {
    return links_has_cur(&a->links) && conn_ui_active(&cur(a)->ui) && !cur(a)->broken;
}

static nav_run run_state(app *a, size_t i, const query_tab *t) {
    bool live = a->book.live == i;
    const conn_ui *u = live ? &cur(a)->ui : &t->shown;
    bool capped = u->q == Q_CAPPED && u->cap != PROTO_CAP_LIMIT;
    nav_run r = {(live && query_active(a)) || tab_book_waiting(&a->book, i), false, u->rows, capped};
    r.ended = !r.busy && t->ran && !conn_ui_active(u) && (u->q == Q_ROWS || capped);
    return r;
}

static void free_kept(app *a, query_tab *t) {
    if (!t->kept) {
        return;
    }
    size_t n = result_close(&t->res, a->tab_arena, a->freed);
    for (size_t i = 0; i < n; i++) {
        size_t idx = 0;
        if (SC_TSLICE_GET(size_t, a->tab_arena, a->freed, i, &idx)) {
            (void)link_pool_free_kept(&a->pool, idx);
        }
    }
    if (t->shown.terminal_kept) {
        (void)link_pool_free_kept(&a->pool, t->shown.terminal);
    }
    conn_ui blank = {0};
    t->shown = blank;
    t->kept = false;
}

static uint8_t refusal_notice(app *a) {
    conn_refuse why = cur(a)->refusal;
    return why == CONN_REFUSE_BUSY            ? QUERY_NOTICE_BUSY
           : why == CONN_REFUSE_NOT_CONNECTED ? QUERY_NOTICE_NOT_CONNECTED
                                              : QUERY_NOTICE_POOL;
}

static bool std_strings(const app *a) {
    return !a->meta.cat.session.loaded || a->meta.cat.session.standard_strings;
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
     * connection and three arenas its budget can pay for (RUN, COLUMNS and a
     * chunk), counting the ones its result frees. */
    conn_link *l = cur(a);
    size_t reclaim = t.kept ? t.shown.held_count + (t.shown.terminal_kept ? 1u : 0u) : 0u;
    tab_run_check can = tab_can_run(
        link_usable(l), budget_room(&a->pool.budget, l->ui.owner), reclaim);
    if (can != TAB_RUN_OK) {
        tab_book_refused(&a->book, i);
        t.notice = can == TAB_RUN_POOL ? QUERY_NOTICE_POOL : QUERY_NOTICE_NOT_CONNECTED;
        (void)put_tab(a, i, &t);
        return;
    }
    nav_entry e;
    bool table = nav_current(&t.nav, &e) && e.kind == NAV_TABLE;
    bool note = false;
    sc_str sql = {NULL, 0, 0};
    if (!table) {
        editor_set_std(&t.ed, std_strings(a));
        if (t.ed.version != t.run_version) {
            editor_run_range(&t.ed, t.all, &t.run_from, &t.run_to);
        }
        size_t n = t.run_to - t.run_from;
        sql = n <= LINK_SQL_MAX && sc_arena_reset(&a->run_sql) ? editor_copy(&t.ed, t.run_from, t.run_to, &a->run_sql)
                                                               : sql;
        if (sql.len != n) {
            tab_book_refused(&a->book, i);
            t.notice = n > LINK_SQL_MAX ? QUERY_NOTICE_RUN_TOO_BIG : QUERY_NOTICE_TOO_BIG;
            (void)put_tab(a, i, &t);
            return;
        }
    }
    if (table && (!sc_arena_reset(a->pages) ||
                  !page_sql(&a->meta.cat, e.schema, e.oid, e.page, a->pages, &sql, &note))) {
        tab_book_refused(&a->book, i);
        t.pending_page = true;
        (void)put_tab(a, i, &t);
        return;
    }
    const sc_arena *sa = table ? a->pages : &a->run_sql;
    free_kept(a, &t);
    link_adopt(l, t.res);
    bool sent = link_run_limit(l, sa, sql, t.limit);
    if (!sent || l->refused) {
        t.notice = sent ? refusal_notice(a) : QUERY_NOTICE_NOT_CONNECTED;
        (void)link_abandon(l); /* nothing went out; back to the idle result */
        tab_book_refused(&a->book, i);
        (void)put_tab(a, i, &t);
        return;
    }
    tab_book_started(&a->book, i);
    t.notice = QUERY_NOTICE_NONE;
    if (i == a->book.sel && !table) {
        a->focus = FOCUS_EDITOR;
    }
    t.ran = true;
    t.timing = true;
    t.started_ms = sc_clock_ms();
    t.elapsed_ms = 0;
    (void)put_tab(a, i, &t);
}

/* The live tab's run has ended (`ended`, its conn_ui copied before the link
 * forgot the arenas): `kept` is its result now. */
static void finish_live(app *a, size_t live, conn_ui ended, uint64_t req, uint64_t sets,
                        result kept) {
    query_tab t;
    if (!get_tab(a, live, &t)) {
        return;
    }
    t.shown = ended;
    t.req = req;
    t.sets = sets;
    t.res = kept;
    t.kept = true;
    if (t.timing) {
        t.elapsed_ms = sc_clock_ms() - t.started_ms;
        t.timing = false;
    }
    (void)put_tab(a, live, &t);
}

/* Once the live run ends its result passes to its tab, and the tab that has
 * waited longest starts. Waiting tabs wait on while the link is not
 * connected or connecting: they run once Reconnect connects (U13). */
static void settle(app *a) {
    conn_link *l = cur(a);
    size_t done = tab_book_finished(&a->book, query_active(a));
    if (done != TAB_NONE) {
        conn_ui ended = l->ui; /* before link_keep, which forgets the arenas */
        uint64_t req = l->res_req, sets = l->res_sets;
        result kept;
        (void)link_keep(l, &kept);
        finish_live(a, done, ended, req, sets, kept);
    }
    size_t next = tab_book_next(&a->book, query_active(a), link_usable(l));
    if (next != TAB_NONE) {
        start(a, next);
    }
}

/* The worker's doorbell (KTD6). */
static void ring(void *arg) {
    (void)arg;
    (void)ui_wake();
}

/* The current link retired (U13): the live tab keeps its rows and its run
 * ends Disconnected, or, with `requeue`, a run the link never sent (queued
 * when its connect failed) waits again, first. */
static void handed_over(app *a, const link_handoff *h, bool requeue) {
    size_t live = tab_book_finished(&a->book, false);
    if (live != TAB_NONE) {
        finish_live(a, live, h->ended, h->req, h->sets, h->kept);
        if (requeue && h->unsent) {
            tab_book_requeue(&a->book, live);
        }
    }
}

/* ⌥⌘. or the Disconnect button: at any time while the link connects, is
 * connected, or cancels. */
static bool disconnect(app *a) {
    link_handoff h;
    if (!links_disconnect(&a->links, &h)) {
        return false;
    }
    handed_over(a, &h, false);
    return true;
}

/* The Reconnect button: a link that was lost, failed or broke retires first,
 * then a new one opens with the same settings. Tabs keep their rows, and
 * waiting tabs run once it connects. */
static bool reconnect(app *a) {
    conn_link *l = cur(a);
    if (!a->links.offline && !query_reconnect_offered(l->ui.state, l->broken)) {
        return false;
    }
    link_handoff h;
    bool handed = false;
    bool ok = links_reconnect(&a->links, &h, &handed);
    if (handed) {
        handed_over(a, &h, true);
    }
    settle(a);
    return ok;
}

/* ⌘. or the Cancel button cancel the selected tab's run: a waiting tab
 * leaves the queue, a live one is cancelled, and one waiting for the link to
 * connect retires the link at once (KTD7), or, with both retiring slots
 * busy, leaves the link's queue. */
static bool cancel(app *a) {
    conn_link *l = cur(a);
    link_handoff h;
    bool handed = false;
    bool ok = false;
    switch (tab_book_cancel(&a->book, a->book.sel, l->ui.state == UI_CONN_CONNECTING)) {
    case TAB_CANCEL_DEQUEUED:
        return true;
    case TAB_CANCEL_QUERY:
        return link_cancel(l);
    case TAB_CANCEL_RETIRE:
        ok = links_cancel_connecting(&a->links, &h, &handed);
        if (handed) {
            handed_over(a, &h, false);
        }
        return ok;
    default:
        return false;
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

static void run_tab(app *a, size_t i, uint64_t limit) {
    query_tab t;
    if (!get_tab(a, i, &t)) {
        return;
    }
    t.limit = limit;
    tab_run_kind k = tab_book_run(&a->book, i, query_active(a));
    t.notice = k == TAB_RUN_BUSY ? QUERY_NOTICE_BUSY : QUERY_NOTICE_NONE;
    if (k == TAB_RUN_QUEUED && i == a->book.sel && !showing_table(&t)) {
        a->focus = FOCUS_EDITOR;
    }
    (void)put_tab(a, i, &t);
    if (k == TAB_RUN_START) {
        start(a, i);
    }
}

static void try_page(app *a, size_t i) {
    query_tab t;
    nav_entry e;
    if (!get_tab(a, i, &t) || !t.pending_page) {
        return;
    }
    if (!nav_current(&t.nav, &e) || nav_arrive(e) != NAV_SHOW_CONTENT) {
        t.pending_page = false;
        (void)put_tab(a, i, &t);
        return;
    }
    if (page_gone(&a->meta.cat, e.schema, e.oid)) {
        t.pending_page = false;
        (void)put_tab(a, i, &t);
        return;
    }
    if (!page_ready(&a->meta.cat, e.schema, e.oid)) {
        (void)catalog_want_detail(&a->meta.cat, e.schema, e.oid);
        return;
    }
    t.pending_page = false;
    if (put_tab(a, i, &t)) {
        run_tab(a, i, PAGE_SIZE);
    }
}

static void rewant(app *a, size_t i) {
    query_tab t;
    nav_entry e;
    if (get_tab(a, i, &t) && nav_current(&t.nav, &e) && e.kind == NAV_TABLE) {
        page_rewant(&a->meta.cat, e.schema, e.oid, e.total, i == a->book.sel);
    }
}

static void run_page(app *a, size_t i, query_tab *t) {
    t->pending_page = true;
    if (put_tab(a, i, t)) {
        try_page(a, i);
    }
}

static void run(app *a, uint64_t limit, bool all) {
    size_t i = a->book.sel;
    query_tab t;
    nav_entry e;
    if (!get_tab(a, i, &t) || !nav_current(&t.nav, &e)) {
        return;
    }
    t.all = all;
    if (e.kind == NAV_QUERY) {
        editor_run_range(&t.ed, all, &t.run_from, &t.run_to);
        t.run_version = t.ed.version;
    }
    if (!put_tab(a, i, &t)) {
        return;
    }
    if (e.kind == NAV_QUERY) {
        run_tab(a, i, limit);
        return;
    }
    if (e.view == NAV_CONTENT) {
        run_page(a, i, &t);
    }
}

/* Opens a tab after the last one and selects it. */
static bool new_tab(app *a) {
    query_tab t;
    size_t i = a->book.count;
    if (i >= TABS_MAX || !get_tab(a, i, &t) || !editor_clear(&t.ed) || !sc_arena_reset(t.grid.wa)) {
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
    t.limit = RUN_LIMIT_CONNECTION;
    t.ran = false;
    t.timing = false;
    t.started_ms = 0;
    t.elapsed_ms = 0;
    t.notice = QUERY_NOTICE_NONE;
    nav_reset(&t.nav);
    t.pending_page = false;
    t.grid.structure_y = 0;
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
        (void)link_abandon(cur(a));
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
        run(a, UINT64_MAX, false);
        return true;
    default:
        return false;
    }
}

static sidebar_view sidebar_of(sc_arena *fa, app *a, bool dark, uint32_t height, bool *lost) {
    sidebar_view v = sidebar_empty(fa, dark);
    catalog_show show = meta_shown(&a->meta);
    v.status = show == CATALOG_SHOW_CONNECTING ? SIDEBAR_CONNECTING
               : show == CATALOG_SHOW_LOADING  ? SIDEBAR_LOADING
               : show == CATALOG_SHOW_READY    ? SIDEBAR_READY
               : show == CATALOG_SHOW_ERROR    ? SIDEBAR_ERROR
                                               : SIDEBAR_DISCONNECTED;
    v.focused = focus_shown(a->focus, &a->layout) == FOCUS_SIDEBAR;
    v.cat = &a->meta.cat;
    v.tree = &a->tree;
    v.can_reload = show != CATALOG_SHOW_DISCONNECTED && meta_link(&a->meta) == CATALOG_LINK_UP;
    v.height = height;
    const sc_arena *ea = a->meta.cat.arena;
    sc_str msg = a->meta.cat.error;
    bool can = false;
    if (!meta_sidebar_error(&a->meta, fa, &ea, &msg, &can)) {
        return v;
    }
    v.message = text_clean_copy(fa, ea, msg, 160, lost);
    v.retry = can ? SHELL_RECONNECT_ENABLED : SHELL_RECONNECT_WAITING;
    v.retry_label = can ? SC_STR_LIT(fa, "Retry") : query_reconnect_label(fa, false);
    return v;
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
    conn_link *l = cur(a);
    bool live = a->book.live == a->book.sel;
    sc_tslice tabs = SC_TSLICE_ALLOC(view_tab, fa, a->book.count);
    bool ok = sc_tslice_len(tabs) == a->book.count;
    nav_entry e;
    bool table = nav_current(&t.nav, &e) && e.kind == NAV_TABLE;
    if (table && e.total.count_state == PAGE_COUNT_PENDING) {
        e.total = page_total_update(e.total, &a->meta.cat, e.schema, e.oid);
        if (e.total.count_state != PAGE_COUNT_PENDING) {
            (void)nav_set_current(&t.nav, e);
        }
    }
    for (size_t k = 0; ok && k < a->book.count; k++) {
        query_tab o = t;
        ok = k == a->book.sel || get_tab(a, k, &o);
        nav_entry oe;
        catalog_schema os;
        catalog_relation orl;
        bool named = nav_current(&o.nav, &oe) && oe.kind == NAV_TABLE &&
                     catalog_relation_of(&a->meta.cat, oe.schema, oe.oid, &os, &orl);
        size_t head = editor_len(&o.ed) < TAB_TITLE_BYTES ? editor_len(&o.ed) : TAB_TITLE_BYTES;
        view_tab vt = {named ? a->meta.cat.arena : fa,
                       named ? orl.name : sc_utf8_truncate(fa, editor_copy(&o.ed, 0, head, fa), head),
                       a->book.live == k && query_active(a), tab_book_waiting(&a->book, k)};
        ok = ok && SC_TSLICE_SET(view_tab, fa, tabs, k, &vt);
    }
    v.dark = dark;
    v.link = l;
    v.conn_state = a->links.offline ? UI_CONN_DISCONNECTED : l->ui.state;
    v.offline = a->links.offline;
    v.can_reconnect = links_can_retire(&a->links);
    v.ui = live ? &l->ui : &t.shown;
    v.res = live ? &l->res : &t.res;
    v.res_req = live ? l->res_req : t.req;
    v.res_sets = live ? l->res_sets : t.sets;
    v.waiting = tab_book_waiting(&a->book, a->book.sel);
    v.stop = (uint8_t)query_stop_for(v.ui, live, v.waiting);
    v.tabs_arena = fa;
    v.tabs = tabs;
    v.tab_count = a->book.count;
    v.tab_selected = a->book.sel;
    editor_set_std(&t.ed, std_strings(a));
    v.editor = &t.ed;
    v.keywords = &a->keywords;
    v.editor_focused = focus_shown(a->focus, &a->layout) == FOCUS_EDITOR;
    v.custom_conninfo = a->custom_conninfo;
    v.elapsed_ms = t.elapsed_ms;
    v.ran = t.ran;
    v.notice = t.notice;
    v.width = w;
    v.height = h;
    v.grid = &t.grid;
    nav_run r = run_state(a, a->book.sel, &t);
    v.can_back = nav_can_move(&t.nav, NAV_MOVE_BACK, r);
    v.can_forward = nav_can_move(&t.nav, NAV_MOVE_FORWARD, r);
    v.cat = &a->meta.cat;
    table_view tv = {&v, &e, false, false, {0}, {0}, false, false};
    if (table) {
        tv.found = catalog_relation_of(&a->meta.cat, e.schema, e.oid, &tv.schema, &tv.rel);
        tv.gone = page_gone(&a->meta.cat, e.schema, e.oid);
        tv.can_prev = nav_can_move(&t.nav, NAV_MOVE_PREV_PAGE, r);
        tv.can_next = nav_can_move(&t.nav, NAV_MOVE_NEXT_PAGE, r);
    }
    bool lost = false;
    sidebar_view side = sidebar_of(fa, a, dark, h, &lost);
    v.show_sidebar = a->show_sidebar;
    v.sidebar = &side;
    ok = ok && !lost && frame_begin(&b, fa, FRAME_NODES, FRAME_DRAWS) &&
         (table ? table_build(&b, &tv) : grid_build(&b, &v)) && frame_end(&b, out);
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

static void text_area(app *a, sc_arena *fa) {
    query_tab t;
    ui_rect r, line;
    int32_t cursor = 0;
    if (focus_shown(a->focus, &a->layout) != FOCUS_EDITOR || !frame_rect(&a->layout, FOCUS_TAG_EDITOR, &r) ||
        !get_tab(a, a->book.sel, &t) ||
        !view_editor_caret(fa, &t.ed, (uint32_t)r.w, (uint32_t)r.h, &line, &cursor)) {
        return;
    }
    line.x += r.x;
    line.y += r.y;
    if (line.x == a->text_area.x && line.y == a->text_area.y && line.w == a->text_area.w &&
        line.h == a->text_area.h && cursor == a->text_cursor) {
        return;
    }
    a->text_area = line;
    a->text_cursor = cursor;
    (void)ui_text_area(line.x, line.y, line.w, line.h, cursor);
}

static bool draw(sc_arena *fa, uint8_t scene, bool dark, app *a) {
    ui_frame f;
    bool ok = build(fa, scene, dark, a, &f) && ui_draw(fa, f, &a->layout);
    if (ok && scene == SCENE_QUERY) {
        text_area(a, fa);
    }
    return ok;
}

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
    uint8_t text_check;
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
             : SC_STR_EQ_LIT(a, v, "query-cancelling")      ? RUN_CANCELLING
             : SC_STR_EQ_LIT(a, v, "query-disconnect-offer") ? RUN_DISCONNECT_OFFER
             : SC_STR_EQ_LIT(a, v, "conn-reconnect")        ? RUN_RECONNECT
             : SC_STR_EQ_LIT(a, v, "sidebar-meta-lost")     ? RUN_META_LOST
             : SC_STR_EQ_LIT(a, v, "sidebar-light") || SC_STR_EQ_LIT(a, v, "sidebar-dark") ? RUN_SIDEBAR
             : SC_STR_EQ_LIT(a, v, "sidebar-error")         ? RUN_SIDEBAR_ERROR
             : SC_STR_EQ_LIT(a, v, "table-content-light") || SC_STR_EQ_LIT(a, v, "table-content-dark")
                 ? RUN_TABLE_CONTENT
             : SC_STR_EQ_LIT(a, v, "table-structure") ? RUN_TABLE_STRUCTURE
             : SC_STR_EQ_LIT(a, v, "table-view-note") ? RUN_TABLE_NOTE
             : SC_STR_EQ_LIT(a, v, "table-back")      ? RUN_TABLE_BACK
             : SC_STR_EQ_LIT(a, v, "editor-light") || SC_STR_EQ_LIT(a, v, "editor-dark") ? RUN_EDITOR
             : SC_STR_EQ_LIT(a, v, "editor-compose")  ? RUN_EDITOR_COMPOSE
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
    o->text_check = TEXT_CHECK_NONE;
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
        } else if (SC_STR_EQ_LIT(a, value, "text-compose")) {
            o->has_scene = true;
            o->scene = SCENE_SHELL;
            o->text_check = TEXT_CHECK_COMPOSE;
            o->theme_fixed = true;
        } else if (SC_STR_EQ_LIT(a, value, "text-clipboard")) {
            o->has_scene = true;
            o->scene = SCENE_SHELL;
            o->text_check = TEXT_CHECK_CLIPBOARD;
            o->theme_fixed = true;
        } else if (query_scene_named(a, value, o)) {
            o->has_scene = true;
            o->scene = SCENE_QUERY;
            o->dark = SC_STR_EQ_LIT(a, value, "grid-dark") || SC_STR_EQ_LIT(a, value, "sidebar-dark") ||
                      SC_STR_EQ_LIT(a, value, "table-content-dark") || SC_STR_EQ_LIT(a, value, "editor-dark");
            o->theme_fixed = true;
        } else {
            return false;
        }
    }
    return true;
}

static bool app_open(app *a, sc_arena *tab_arena, sc_arena *scratch_arena, sc_arena *info_arena,
                     const sc_arena *args_arena, sc_arena *records, sc_arena *keep, sc_arena *defs,
                     sc_arena *sql, sc_arena *pages, const options *o) {
    a->custom_conninfo = o->conninfo.len > 0;
    a->pooled = false;
    a->pools = false;
    a->tab_arena = tab_arena;
    a->scratch = scratch_arena;
    a->pages = pages;
    if (!ui_arena_new(&a->edit_scratch, EDITOR_SCRATCH_BYTES) || !ui_arena_new(&a->run_sql, RUN_SQL_BYTES) ||
        !ui_arena_new(&a->keyword_a, KEYWORD_BYTES) || !sqlkw_init(&a->keywords, &a->keyword_a)) {
        return false;
    }
    a->tabs = SC_TSLICE_ALLOC(query_tab, tab_arena, TABS_MAX);
    a->freed = SC_TSLICE_ALLOC(size_t, tab_arena, RESULT_QUOTA);
    if (!tab_book_init(&a->book, tab_arena) || sc_tslice_len(a->tabs) != TABS_MAX ||
        sc_tslice_len(a->freed) != RESULT_QUOTA) {
        return false;
    }
    if (!sc_mem_pool_new(&a->editors, TABS_MAX, EDITOR_BYTES).ok) {
        return false;
    }
    if (!sc_mem_pool_new(&a->grids, TABS_MAX, GRID_BYTES).ok) {
        (void)sc_mem_pool_free(&a->editors);
        return false;
    }
    if (!sc_mem_pool_new(&a->books, TABS_MAX, result_book_bytes(RESULT_QUOTA)).ok) {
        (void)sc_mem_pool_free(&a->grids);
        (void)sc_mem_pool_free(&a->editors);
        return false;
    }
    a->pools = true;
    for (size_t i = 0; i < TABS_MAX; i++) {
        query_tab t = {0};
        sc_arena *in = sc_mem_arena(&a->editors, i);
        sc_arena *wa = sc_mem_arena(&a->grids, i);
        sc_arena *book = sc_mem_arena(&a->books, i);
        if (in == NULL || wa == NULL || book == NULL || !editor_init(&t.ed, in, &a->edit_scratch, true) ||
            !result_init(&t.res, book, RESULT_QUOTA) || !nav_init(&t.nav, tab_arena)) {
            return false;
        }
        grid_state_init(&t.grid, wa, scratch_arena);
        if (!put_tab(a, i, &t)) {
            return false;
        }
    }
    a->pooled = true;
    if (!link_pool_init(&a->pool, budget_default_split())) {
        return false;
    }
    conn_limits lim = {4, RESULT_QUOTA, 10000};
    links_init(&a->links, &a->pool, lim, ring, NULL, o->wrong_password ? info_arena : args_arena,
               o->wrong_password ? SC_STR_LIT(info_arena, "password=wrong") : o->conninfo);
    return meta_init(&a->meta, &a->links, records, keep, defs, sql) && links_open(&a->links);
}

static void insert(query_tab *t, const sc_arena *sa, sc_str s, bool typed) {
    editor_status st = editor_insert(&t->ed, sa, s, typed, sc_clock_ms());
    t->notice = st == EDITOR_TOO_BIG     ? QUERY_NOTICE_TOO_BIG
                : st == EDITOR_NOT_TEXT  ? QUERY_NOTICE_NOT_TEXT
                                        : QUERY_NOTICE_NONE;
}

static void paste(query_tab *t, sc_arena *clip) {
    sc_str s;
    if (!sc_arena_reset(clip)) {
        return;
    }
    uint8_t got = ui_clipboard(clip, EDITOR_CAP, &s);
    if (got == UI_CLIP_TOO_BIG) {
        t->notice = QUERY_NOTICE_CLIP_TOO_BIG;
    } else if (got == UI_CLIP_OK) {
        insert(t, clip, s, false);
    }
}

static void copy(query_tab *t, sc_arena *clip, bool cut) {
    size_t from = 0, to = 0;
    editor_selection(&t->ed, &from, &to);
    if (from >= to || !sc_arena_reset(clip) ||
        ui_clipboard_set(clip, editor_copy(&t->ed, from, to, clip), EDITOR_CAP) != UI_CLIP_OK || !cut) {
        return;
    }
    sc_str none = {NULL, 0, clip->epoch};
    insert(t, clip, none, false);
}

static void notice(app *a, uint8_t n) {
    query_tab t;
    if (get_tab(a, a->book.sel, &t)) {
        t.notice = n;
        (void)put_tab(a, a->book.sel, &t);
    }
}

static void leave(app *a, size_t i) {
    query_tab t;
    if (tab_book_abandon(&a->book, i)) {
        (void)link_abandon(cur(a));
    }
    if (!get_tab(a, i, &t)) {
        return;
    }
    free_kept(a, &t);
    conn_ui blank = {0};
    t.shown = blank;
    t.req = 0;
    t.sets = 0;
    t.ran = false;
    t.timing = false;
    t.elapsed_ms = 0;
    t.notice = QUERY_NOTICE_NONE;
    t.pending_page = false;
    t.grid.structure_y = 0;
    t.grid.sel.selected = false;
    (void)put_tab(a, i, &t);
}

static void arrive(app *a, size_t i) {
    query_tab t;
    nav_entry e;
    if (!get_tab(a, i, &t) || !nav_current(&t.nav, &e)) {
        return;
    }
    switch (nav_arrive(e)) {
    case NAV_SHOW_CONTENT:
        run_page(a, i, &t);
        break;
    case NAV_SHOW_STRUCTURE:
        (void)catalog_want_detail(&a->meta.cat, e.schema, e.oid);
        break;
    default:
        a->focus = FOCUS_EDITOR;
        break;
    }
    (void)meta_poll(&a->meta);
    settle(a);
}

static bool navigate(app *a, nav_move m) {
    size_t i = a->book.sel;
    query_tab t;
    if (!get_tab(a, i, &t)) {
        return false;
    }
    nav_run r = run_state(a, i, &t);
    if (!nav_can_move(&t.nav, m, r)) {
        nav_run idle = r;
        idle.busy = false;
        bool busy = (m == NAV_MOVE_BACK || m == NAV_MOVE_FORWARD) && nav_can_move(&t.nav, m, idle);
        if (busy) {
            notice(a, QUERY_NOTICE_BUSY);
        }
        return busy;
    }
    leave(a, i);
    if (!get_tab(a, i, &t) || !nav_apply(&t.nav, m, r) || !put_tab(a, i, &t)) {
        return false;
    }
    arrive(a, i);
    return true;
}

static void open_object(app *a, tree_open o) {
    catalog *c = &a->meta.cat;
    query_tab t;
    if (o.kind == TREE_OPEN_FUNCTION) {
        if (a->book.count >= TABS_MAX) {
            notice(a, QUERY_NOTICE_TABS_FULL);
            return;
        }
        (void)catalog_want_definition(c, o.oid);
        return;
    }
    catalog_schema s;
    catalog_relation r;
    if (o.kind != TREE_OPEN_RELATION || !catalog_relation_of(c, o.schema, o.oid, &s, &r)) {
        return;
    }
    tab_open_kind where = tab_book_open_where(&a->book, o.new_tab, query_active(a));
    if (where == TAB_OPEN_FULL || where == TAB_OPEN_BUSY) {
        notice(a, where == TAB_OPEN_FULL ? QUERY_NOTICE_TABS_FULL : QUERY_NOTICE_BUSY);
        return;
    }
    if (where == TAB_OPEN_NEW && !new_tab(a)) {
        return;
    }
    size_t i = a->book.sel;
    leave(a, i);
    if (!get_tab(a, i, &t) || !nav_push(&t.nav, nav_table(o.schema, o.oid, page_total_open(&r))) ||
        !put_tab(a, i, &t)) {
        return;
    }
    page_open(c, o.schema, o.oid);
    focus_area kept = a->focus;
    arrive(a, i);
    a->focus = kept;
}

static bool tree_done(app *a, bool changed, tree_open o) {
    open_object(a, o);
    (void)meta_poll(&a->meta);
    return changed;
}

static bool definition_landed(app *a, sc_arena *scratch) {
    uint32_t oid = 0;
    const sc_arena *ta = NULL;
    sc_str text;
    query_tab t;
    if (!catalog_definition_text(&a->meta.cat, scratch, &oid, &ta, &text)) {
        return false;
    }
    if (a->book.count >= TABS_MAX) {
        notice(a, QUERY_NOTICE_TABS_FULL);
        return true;
    }
    if (!new_tab(a) || !get_tab(a, a->book.sel, &t)) {
        return false;
    }
    insert(&t, ta, text, false);
    editor_select(&t.ed, 0, 0);
    a->focus = FOCUS_EDITOR;
    return put_tab(a, a->book.sel, &t);
}

/* Returns true when the key changed what the window shows. */
static bool key(app *a, sc_arena *clip, ui_event ev) {
    query_tab t;
    if (!get_tab(a, a->book.sel, &t)) {
        return false;
    }
    a->focus = focus_shown(a->focus, &a->layout);
    switch (nav_key_for(ev.key, ev.mods, ui_platform())) {
    case NAV_KEY_BACK:
        return navigate(a, NAV_MOVE_BACK);
    case NAV_KEY_FORWARD:
        return navigate(a, NAV_MOVE_FORWARD);
    case NAV_KEY_SWITCH:
        return navigate(a, NAV_MOVE_SWITCH);
    default:
        break;
    }
    focus_key k = focus_route(a->focus, ev.key, ev.mods, ui_platform());
    if (k.to == FOCUS_TO_EDITOR && showing_table(&t)) {
        return false;
    }
    bool dirty = true;
    tree_open o;
    switch (k.to) {
    case FOCUS_TO_TAB:
        return tab_command(a, k.tab);
    case FOCUS_TO_SIDEBAR:
        a->show_sidebar = !a->show_sidebar;
        return true;
    case FOCUS_TO_TREE:
        dirty = tree_press(&a->tree, &a->meta.cat, k.tree, &o);
        return tree_done(a, dirty, o);
    case FOCUS_TO_RUN:
    case FOCUS_TO_RUN_ALL:
        run(a, RUN_LIMIT_CONNECTION, k.to == FOCUS_TO_RUN_ALL);
        return true;
    case FOCUS_TO_CANCEL:
        return cancel(a);
    case FOCUS_TO_DISCONNECT:
        return disconnect(a);
    case FOCUS_TO_DETAIL:
        t.grid.detail = !t.grid.detail;
        break;
    case FOCUS_TO_GRID:
        dirty = grid_move(&t.grid.sel, k.grid);
        break;
    case FOCUS_TO_EDITOR:
        switch (k.edit.cmd) {
        case EDITOR_CMD_COPY:
        case EDITOR_CMD_CUT:
            copy(&t, clip, k.edit.cmd == EDITOR_CMD_CUT);
            break;
        case EDITOR_CMD_PASTE:
            paste(&t, clip);
            break;
        case EDITOR_CMD_ESCAPE:
            dirty = editor_do(&t.ed, k.edit, sc_clock_ms());
            if (dirty) {
                a->composing = false;
                (void)ui_text_input(UI_TEXT_INPUT_PLAIN);
            }
            break;
        default:
            dirty = editor_do(&t.ed, k.edit, sc_clock_ms());
            t.notice = QUERY_NOTICE_NONE;
            break;
        }
        break;
    default:
        return false;
    }
    return put_tab(a, a->book.sel, &t) && dirty;
}

static bool editor_click(app *a, query_tab *t, sc_arena *scratch, ui_event ev, ui_rect r) {
    size_t off = view_editor_hit(scratch, &t->ed, ev.x - r.x, ev.y - r.y);
    if (ev.clicks >= 3u) {
        editor_select_line(&t->ed, off);
    } else if (ev.clicks == 2u) {
        editor_select_word(&t->ed, off);
    } else {
        editor_select(&t->ed, (ev.mods & UI_MOD_SHIFT) != 0 ? t->ed.anchor : off, off);
    }
    a->dragging = ev.clicks < 2u;
    return put_tab(a, a->book.sel, t);
}

static bool pointer_move(app *a, sc_arena *scratch, ui_event ev) {
    uint8_t shape = frame_hit(&a->layout, ev.x, ev.y) == FOCUS_TAG_EDITOR ? UI_CURSOR_TEXT : UI_CURSOR_DEFAULT;
    if (shape != a->pointer_shape) {
        a->pointer_shape = shape;
        (void)ui_cursor(shape);
    }
    ui_rect r;
    query_tab t;
    if (!a->dragging || !frame_rect(&a->layout, FOCUS_TAG_EDITOR, &r) || !get_tab(a, a->book.sel, &t)) {
        return false;
    }
    size_t off = view_editor_hit(scratch, &t.ed, ev.x - r.x, ev.y - r.y);
    if (off == t.ed.cursor) {
        return false;
    }
    editor_select(&t.ed, t.ed.anchor, off);
    return put_tab(a, a->book.sel, &t);
}

/* A left click on the strip selects, closes or opens a tab. Any other left
 * click focuses the area it lands on, and in the grid selects the row under
 * the pointer. Both are tested against the last frame's rectangles (KTD18). */
static bool pointer(app *a, sc_arena *scratch, ui_event ev) {
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
    if (ev.button == UI_BUTTON_LEFT) { /* the buttons (U13): tagged only while enabled */
        switch (tag) {
        case FOCUS_TAG_EXECUTE:
            run(a, RUN_LIMIT_CONNECTION, false);
            return true;
        case FOCUS_TAG_CANCEL:
            return cancel(a);
        case FOCUS_TAG_DISCONNECT:
            return disconnect(a);
        case FOCUS_TAG_RECONNECT:
        case FOCUS_TAG_RECONNECT_BAR:
            return reconnect(a);
        case FOCUS_TAG_META_RETRY:
            return meta_retry(&a->meta);
        case FOCUS_TAG_SIDEBAR_TABLES:
        case FOCUS_TAG_SIDEBAR_FUNCTIONS:
            a->focus = FOCUS_SIDEBAR;
            tree_set_mode(&a->tree, tag == FOCUS_TAG_SIDEBAR_TABLES ? TREE_TABLES : TREE_FUNCTIONS);
            return true;
        case FOCUS_TAG_SIDEBAR_RELOAD:
            a->focus = FOCUS_SIDEBAR;
            return meta_reload(&a->meta);
        case FOCUS_TAG_NAV_BACK:
            return navigate(a, NAV_MOVE_BACK);
        case FOCUS_TAG_NAV_FORWARD:
            return navigate(a, NAV_MOVE_FORWARD);
        case FOCUS_TAG_TABLE_CONTENT:
        case FOCUS_TAG_TABLE_STRUCTURE: {
            query_tab cur_t;
            nav_entry e;
            bool content = get_tab(a, a->book.sel, &cur_t) && nav_current(&cur_t.nav, &e) &&
                           e.view == NAV_CONTENT;
            return content != (tag == FOCUS_TAG_TABLE_CONTENT) && navigate(a, NAV_MOVE_SWITCH);
        }
        case FOCUS_TAG_PAGE_PREV:
            return navigate(a, NAV_MOVE_PREV_PAGE);
        case FOCUS_TAG_PAGE_NEXT:
            return navigate(a, NAV_MOVE_NEXT_PAGE);
        default:
            break;
        }
    }
    focus_area was = a->focus;
    a->focus = focus_click(a->focus, tag, ev.button);
    ui_rect r;
    query_tab t;
    if (ev.button == UI_BUTTON_LEFT && tag == FOCUS_TAG_SIDEBAR_ROWS &&
        frame_rect(&a->layout, FOCUS_TAG_SIDEBAR_ROWS, &r)) {
        tree_open o;
        bool changed = tree_click(&a->tree, &a->meta.cat, (int64_t)ev.y - r.y,
                                  frame_cmd(ev.mods, ui_platform()), &o);
        return tree_done(a, changed || was != a->focus, o);
    }
    if (ev.button == UI_BUTTON_LEFT && tag == FOCUS_TAG_EDITOR && frame_rect(&a->layout, FOCUS_TAG_EDITOR, &r) &&
        get_tab(a, a->book.sel, &t)) {
        return editor_click(a, &t, scratch, ev, r);
    }
    if (ev.button != UI_BUTTON_LEFT || tag != FOCUS_TAG_GRID ||
        !frame_rect(&a->layout, FOCUS_TAG_GRID, &r) || !get_tab(a, a->book.sel, &t)) {
        return was != a->focus;
    }
    bool dirty = grid_click(&t.grid.sel, (int64_t)ev.y - r.y) || was != a->focus;
    return put_tab(a, a->book.sel, &t) && dirty;
}

static bool wheel(app *a, ui_event ev) {
    query_tab t;
    uint32_t tag = frame_hit(&a->layout, ev.x, ev.y);
    if (tag == FOCUS_TAG_SIDEBAR_ROWS) {
        return tree_wheel(&a->tree, ev.wheel_y);
    }
    if (tag == FOCUS_TAG_EDITOR && ev.wheel_y != 0 && get_tab(a, a->book.sel, &t)) {
        int32_t lines = -ev.wheel_y / VIEW_EDITOR_LINE_HEIGHT;
        lines = lines != 0 ? lines : ev.wheel_y > 0 ? -1 : 1;
        bool moved = editor_wheel(&t.ed, lines);
        return put_tab(a, a->book.sel, &t) && moved;
    }
    if (tag == FOCUS_TAG_STRUCTURE && get_tab(a, a->book.sel, &t)) {
        uint64_t y = t.grid.structure_y;
        int64_t step = (int64_t)ev.wheel_y;
        t.grid.structure_y = step > 0 ? (y > (uint64_t)step ? y - (uint64_t)step : 0u) : y + (uint64_t)(-step);
        return put_tab(a, a->book.sel, &t) && y != t.grid.structure_y;
    }
    if (tag != FOCUS_TAG_GRID || !get_tab(a, a->book.sel, &t)) {
        return false;
    }
    bool moved = grid_wheel(&t.grid.sel, ev.wheel_x, ev.wheel_y);
    return put_tab(a, a->book.sel, &t) && moved;
}

static bool compose(app *a, const sc_arena *sa, ui_event ev) {
    query_tab t;
    a->focus = focus_shown(a->focus, &a->layout);
    if (!focus_takes_text(a->focus) || !get_tab(a, a->book.sel, &t) || showing_table(&t)) {
        return false;
    }
    editor_compose(&t.ed, sa, ev.text, ev.compose_start + ev.compose_len);
    a->composing = editor_compose_text(&t.ed).len > 0;
    return put_tab(a, a->book.sel, &t);
}

static bool end_compose(app *a) {
    query_tab t;
    if (!a->composing || (focus_shown(a->focus, &a->layout) == FOCUS_EDITOR && get_tab(a, a->book.sel, &t) &&
                          !showing_table(&t) && editor_compose_text(&t.ed).len > 0)) {
        return false;
    }
    a->composing = false;
    sc_str none = {NULL, 0, a->edit_scratch.epoch};
    for (size_t i = 0; i < a->book.count; i++) {
        if (get_tab(a, i, &t) && editor_compose_text(&t.ed).len > 0) {
            editor_compose(&t.ed, &a->edit_scratch, none, 0);
            (void)put_tab(a, i, &t);
        }
    }
    (void)ui_text_input(UI_TEXT_INPUT_PLAIN);
    return true;
}

static bool text_typed(app *a, const sc_arena *sa, sc_str s) {
    query_tab t;
    a->focus = focus_shown(a->focus, &a->layout);
    if (!focus_takes_text(a->focus) || !get_tab(a, a->book.sel, &t) || showing_table(&t)) {
        return false;
    }
    insert(&t, sa, s, true);
    a->composing = false;
    return put_tab(a, a->book.sel, &t);
}

/* Waits up to `ms` for one input event (UI_EVENT_NONE on a timeout), then
 * drains the worker's replies: after a ring, and on every timeout too, since a
 * ring can be lost (KTD6). True when a reply arrived. */
static bool wait_and_poll(app *a, sc_arena *events, uint32_t ms, ui_event *ev) {
    if (!sc_arena_reset(events) || !ui_wait(events, ms, ev)) {
        ev->kind = UI_EVENT_NONE;
    }
    if (!links_has_cur(&a->links)) {
        return false;
    }
    bool any = link_poll(cur(a));
    link_tick(cur(a));
    any = meta_poll(&a->meta) || any;
    any = definition_landed(a, events) || any;
    for (size_t i = 0; i < a->book.count; i++) {
        rewant(a, i);
        try_page(a, i);
    }
    links_drain(&a->links);
    tick(a);
    settle(a);
    return any;
}

/* What links_close waits on: the next input event, dropped. */
static void wait_ui(void *arg, uint32_t ms) {
    sc_arena *events = arg;
    ui_event ev;
    if (sc_arena_reset(events)) {
        (void)ui_wait(events, ms, &ev);
    }
}

/* Waits up to 2 s in total for every link's BYE (links_close). A worker
 * still blocked is left running: false then, and the caller exits the
 * process without ui_close, since that worker may still ring through
 * ui_wake. Kept results go with the pool. */
static bool app_close(app *a, sc_arena *events) {
    bool joined = links_close(&a->links, CLOSE_WAIT_MS, wait_ui, events);
    if (a->pooled && joined) {
        link_pool_free(&a->pool);
        a->pooled = false;
    }
    if (a->pools) {
        (void)sc_mem_pool_free(&a->books);
        (void)sc_mem_pool_free(&a->grids);
        (void)sc_mem_pool_free(&a->editors);
        a->pools = false;
    }
    return joined;
}

/* Types `sql` (live in `sa`) into the selected tab, as typing would. */
static bool type_sql(app *a, const sc_arena *sa, sc_str sql) {
    query_tab t;
    return get_tab(a, a->book.sel, &t) && editor_insert(&t.ed, sa, sql, false, sc_clock_ms()) == EDITOR_OK &&
           put_tab(a, a->book.sel, &t);
}

/* Waits until no run is live or waiting, or `until` says stop. */
static void wait_runs(app *a, sc_arena *events, uint64_t end) {
    ui_event ev;
    while ((a->book.live != TAB_NONE || query_active(a) ||
            tab_next_waiting(a->book.arena, a->book.waits, a->book.count) != SIZE_MAX) &&
           !cur(a)->broken && sc_clock_ms() < end) {
        (void)wait_and_poll(a, events, 50, &ev);
    }
}

/* Waits until the live run has `rows` rows, or `end`. */
static bool wait_rows(app *a, sc_arena *events, uint64_t rows, uint64_t end) {
    ui_event ev;
    while (cur(a)->ui.rows < rows && query_active(a) && sc_clock_ms() < end) {
        (void)wait_and_poll(a, events, 50, &ev);
    }
    return cur(a)->ui.rows >= rows && query_active(a);
}

static bool caret_at(app *a, sc_arena *scratch, sc_str find, size_t width) {
    query_tab t;
    size_t off = 0;
    if (!get_tab(a, a->book.sel, &t)) {
        return false;
    }
    sc_str all = editor_copy(&t.ed, 0, editor_len(&t.ed), scratch);
    if (!sc_str_find(scratch, all, find, &off)) {
        return false;
    }
    editor_select(&t.ed, off, off + width);
    a->focus = FOCUS_EDITOR;
    return put_tab(a, a->book.sel, &t);
}

/* Plays a scene: connects, types and runs its SQL, and waits for the end. */
static bool play(app *a, sc_arena *events, sc_arena *scratch, uint8_t run_kind) {
    ui_event ev;
    uint64_t end = sc_clock_ms() + SCENE_WAIT_MS;
    while (cur(a)->ui.state == UI_CONN_CONNECTING && !cur(a)->broken && sc_clock_ms() < end) {
        (void)wait_and_poll(a, events, 50, &ev);
    }
    /* The sidebar's schema list first, so each scene shows it (U18). */
    while ((meta_shown(&a->meta) == CATALOG_SHOW_CONNECTING ||
            meta_shown(&a->meta) == CATALOG_SHOW_LOADING) &&
           sc_clock_ms() < end) {
        (void)wait_and_poll(a, events, 50, &ev);
    }
    if (run_kind == RUN_NONE) {
        return cur(a)->ui.state != UI_CONN_CONNECTING;
    }
    bool editing = run_kind == RUN_EDITOR || run_kind == RUN_EDITOR_COMPOSE;
    if (!sc_arena_reset(scratch) || !type_sql(a, scratch, scene_sql(scratch, run_kind)) ||
        (editing && !caret_at(a, scratch, SC_STR_LIT(scratch, "relkind\n"), 0))) {
        return false;
    }
    run(a, run_kind == RUN_MILLION ? UINT64_MAX : RUN_LIMIT_CONNECTION, !editing);
    if (run_kind == RUN_CANCELLING || run_kind == RUN_DISCONNECT_OFFER) {
        return wait_rows(a, events, 20u, end); /* mid-stream: play_cancel goes on */
    }
    wait_runs(a, events, end);
    while (run_kind == RUN_META_LOST && meta_shown(&a->meta) != CATALOG_SHOW_ERROR &&
           sc_clock_ms() < end) {
        (void)wait_and_poll(a, events, 50, &ev);
    }
    query_tab t;
    return get_tab(a, a->book.sel, &t) && t.ran && a->book.live == TAB_NONE &&
           (run_kind != RUN_META_LOST || meta_shown(&a->meta) == CATALOG_SHOW_ERROR);
}

/* tabs-streaming: after the first tab's count, a second tab streams slowly
 * (one row per 20 ms) and a third queues behind it; the second is shown with
 * its first rows in. The app then quits mid-stream. */
static bool play_tabs(app *a, sc_arena *events, sc_arena *scratch) {
    uint64_t end = sc_clock_ms() + SCENE_WAIT_MS;
    if (!new_tab(a) || !sc_arena_reset(scratch) ||
        !type_sql(a, scratch,
                  SC_STR_LIT(scratch, "SELECT g AS n, md5(g::text) || left(pg_sleep(0.02)::text, 0) AS hash\n"
                                      "FROM generate_series(1, 3000) AS g;"))) {
        return false;
    }
    run(a, RUN_LIMIT_CONNECTION, true);
    (void)wait_rows(a, events, 20u, end);
    if (!new_tab(a) || !sc_arena_reset(scratch) ||
        !type_sql(a, scratch, SC_STR_LIT(scratch, "SELECT relname, relkind\nFROM pg_class\nORDER BY 1;"))) {
        return false;
    }
    run(a, RUN_LIMIT_CONNECTION, true);
    a->book.sel = 1;
    return query_active(a) && a->book.live == 1 && tab_book_waiting(&a->book, 2);
}

/* query-cancelling: Cancel (⌘.) mid-stream, drawn before any reply is read,
 * so the run shows Cancelling. query-disconnect-offer: the same, then 5 s with
 * the UI's clock running and the replies left unread, as when a blocking
 * cancel hangs, so Disconnect replaces Cancel. */
static bool play_cancel(app *a, sc_arena *events, uint8_t run_kind) {
    if (!cancel(a) || cur(a)->ui.q != Q_CANCELLING) {
        return false;
    }
    if (run_kind == RUN_CANCELLING) {
        return true;
    }
    uint64_t end = sc_clock_ms() + CONN_DISCONNECT_OFFER_MS + 100u;
    ui_event ev;
    while (sc_clock_ms() < end) {
        if (!sc_arena_reset(events) || !ui_wait(events, 50, &ev)) {
            ev.kind = UI_EVENT_NONE;
        }
        link_tick(cur(a));
        tick(a);
    }
    return cur(a)->ui.offer_disconnect && cur(a)->ui.q == Q_CANCELLING;
}

/* conn-reconnect, after its screenshot: Reconnect opens a new link, the tab's
 * rows stay, and a run in a new tab returns its row. */
static bool play_reconnect(app *a, sc_arena *events, sc_arena *scratch) {
    query_tab t;
    uint64_t end = sc_clock_ms() + SCENE_WAIT_MS;
    if (!get_tab(a, a->book.sel, &t) || !t.kept || t.res.rows == 0) {
        return false;
    }
    uint64_t rows = t.res.rows;
    ui_event ev;
    if (!reconnect(a)) {
        return false;
    }
    while (cur(a)->ui.state == UI_CONN_CONNECTING && sc_clock_ms() < end) {
        (void)wait_and_poll(a, events, 50, &ev);
    }
    if (cur(a)->ui.state != UI_CONN_CONNECTED || !new_tab(a) || !sc_arena_reset(scratch) ||
        !type_sql(a, scratch, SC_STR_LIT(scratch, "SELECT 1;"))) {
        return false;
    }
    run(a, RUN_LIMIT_CONNECTION, true);
    wait_runs(a, events, end);
    query_tab n;
    return get_tab(a, 0, &t) && t.kept && t.res.rows == rows && get_tab(a, a->book.sel, &n) &&
           n.shown.q == Q_ROWS && n.res.rows == 1;
}

static bool scene_schema(const catalog *c, const uint8_t *name, size_t n, size_t *out) {
    catalog_schema s;
    for (size_t i = 0; catalog_schema_at(c, i, &s); i++) {
        if (sc_str_eq_lit(c->arena, s.name, name, n)) {
            *out = i;
            return true;
        }
    }
    return false;
}

static bool scene_child(const catalog *c, size_t si, bool fn, const uint8_t *name, size_t n,
                        size_t *out) {
    catalog_schema s;
    catalog_relation r;
    catalog_function f;
    if (!catalog_schema_at(c, si, &s)) {
        return false;
    }
    for (size_t i = 0; fn ? catalog_function_at(c, &s, i, &f) : catalog_relation_at(c, &s, i, &r); i++) {
        if (sc_str_eq_lit(c->arena, fn ? f.name : r.name, name, n)) {
            *out = i;
            return true;
        }
    }
    return false;
}

static bool scene_loading(const catalog *c, size_t i, size_t j) {
    catalog_schema s, t;
    return !catalog_schema_at(c, i, &s) || !catalog_schema_at(c, j, &t) ||
           s.load == CATALOG_LOAD_PENDING || t.load == CATALOG_LOAD_PENDING;
}

typedef struct {
    const uint8_t *schema, *other, *child;
    size_t schema_len, other_len, child_len;
    bool fn;
} scene_pick;

static bool scene_open(app *a, sc_arena *events, sc_arena *fa, bool dark, scene_pick p, size_t *si,
                       size_t *oi, size_t *ci, uint64_t end) {
    catalog *c = &a->meta.cat;
    ui_event ev;
    if (!meta_reload(&a->meta)) {
        return false;
    }
    while (meta_shown(&a->meta) != CATALOG_SHOW_READY && sc_clock_ms() < end) {
        (void)wait_and_poll(a, events, 50, &ev);
    }
    if (!scene_schema(c, p.schema, p.schema_len, si) || !scene_schema(c, p.other, p.other_len, oi) ||
        !catalog_expand(c, *si) || !catalog_expand(c, *oi)) {
        return false;
    }
    (void)meta_poll(&a->meta);
    while (scene_loading(c, *si, *oi) && sc_clock_ms() < end) {
        (void)wait_and_poll(a, events, 50, &ev);
    }
    if (p.fn) {
        tree_set_mode(&a->tree, TREE_FUNCTIONS);
    }
    if (!scene_child(c, *si, p.fn, p.child, p.child_len, ci) || !draw(fa, SCENE_QUERY, dark, a)) {
        return false;
    }
    tree_row r = {p.fn ? TREE_ROW_FUNCTION : TREE_ROW_RELATION, *si, *ci};
    a->focus = FOCUS_SIDEBAR;
    tree_open o;
    if (!tree_set_selection(&a->tree, c, r) || !tree_press(&a->tree, c, TREE_KEY_OPEN, &o) ||
        !sc_arena_reset(events)) {
        return false;
    }
    (void)tree_done(a, true, o);
    return true;
}

static bool play_sidebar(app *a, sc_arena *events, sc_arena *fa, bool dark, uint8_t run_kind) {
    uint64_t end = sc_clock_ms() + SCENE_WAIT_MS;
    catalog *c = &a->meta.cat;
    bool error = run_kind == RUN_SIDEBAR_ERROR;
    ui_event ev;
    scene_pick p = {(const uint8_t *)"shop", error ? (const uint8_t *)"broken" : (const uint8_t *)"analytics",
                    error ? (const uint8_t *)"order_count" : (const uint8_t *)"customers",
                    4, error ? 6u : 9u, error ? 11u : 9u, error};
    size_t shop = 0, other = 0, child = 0;
    if (!scene_open(a, events, fa, dark, p, &shop, &other, &child, end)) {
        return false;
    }
    query_tab t;
    while (sc_clock_ms() < end && (error ? a->book.count < 2u
                                          : get_tab(a, a->book.sel, &t) && (t.pending_page || !t.ran))) {
        (void)wait_and_poll(a, events, 50, &ev);
    }
    wait_runs(a, events, end);
    catalog_schema b;
    if (dark) {
        a->focus = FOCUS_EDITOR;
    }
    return get_tab(a, a->book.sel, &t) &&
           (error ? a->book.count == 2u && catalog_schema_at(c, other, &b) && b.load == CATALOG_LOAD_FAILED
                  : t.kept && t.res.rows > 0);
}

static bool scene_page(app *a, sc_arena *events, uint64_t first, uint64_t end) {
    query_tab t;
    ui_event ev;
    while (sc_clock_ms() < end &&
           !(get_tab(a, a->book.sel, &t) && t.ran && !t.pending_page && a->book.live == TAB_NONE)) {
        (void)wait_and_poll(a, events, 50, &ev);
    }
    result_cursor k;
    if (!get_tab(a, a->book.sel, &t) || !t.kept || t.res.rows != PAGE_SIZE || !result_seek(&k, &t.res, 0)) {
        return false;
    }
    for (uint64_t r = 0; r < PAGE_SIZE; r++) {
        proto_cell c;
        uint64_t v = 0;
        if ((r > 0 && !result_next_row(&k)) || !result_next_cell(&k, &c) || c.null || c.bytes.len == 0) {
            return false;
        }
        for (size_t i = 0; i < c.bytes.len; i++) {
            uint8_t ch = 0;
            if (!sc_slice_get(k.rd.arena, c.bytes, i, &ch) || ch < '0' || ch > '9') {
                return false;
            }
            v = v * 10u + (uint64_t)(ch - '0');
        }
        if (v != first + r) {
            return false;
        }
    }
    return true;
}

static bool play_back(app *a, sc_arena *events, sc_arena *fa, uint64_t end) {
    query_tab t;
    nav_entry e;
    ui_event ev;
    if (!navigate(a, NAV_MOVE_NEXT_PAGE) || !scene_page(a, events, PAGE_SIZE + 1u, end) ||
        !get_tab(a, a->book.sel, &t) || !nav_current(&t.nav, &e) || e.page != 2u) {
        return false;
    }
    run(a, RUN_LIMIT_CONNECTION, true);
    if (a->book.live != a->book.sel || !query_active(a) || !navigate(a, NAV_MOVE_BACK)) {
        return false;
    }
    budget *pb = &a->pool.budget;
    while (sc_clock_ms() < end && (pb->used.floor > 0 || pb->used.growth > 0)) {
        (void)wait_and_poll(a, events, 50, &ev);
    }
    if (!sc_arena_reset(fa) || !get_tab(a, a->book.sel, &t) || !nav_current(&t.nav, &e) ||
        e.kind != NAV_QUERY || t.ran || t.kept || t.pending_page || a->book.live != TAB_NONE ||
        tab_book_waiting(&a->book, a->book.sel) || pb->used.floor > 0 || pb->used.growth > 0) {
        return false;
    }
    sc_str want = scene_sql(fa, RUN_TABLE_BACK);
    sc_str got = editor_copy(&t.ed, 0, editor_len(&t.ed), &a->run_sql);
    if (got.len != want.len) {
        return false;
    }
    for (size_t i = 0; i < got.len; i++) {
        uint8_t x = 0, y = 0;
        if (!sc_slice_get(&a->run_sql, got, i, &x) || !sc_slice_get(fa, want, i, &y) || x != y) {
            return false;
        }
    }
    return navigate(a, NAV_MOVE_FORWARD) && scene_page(a, events, PAGE_SIZE + 1u, end) &&
           get_tab(a, a->book.sel, &t) && nav_current(&t.nav, &e) && e.kind == NAV_TABLE && e.page == 2u;
}

static bool play_table(app *a, sc_arena *events, sc_arena *fa, bool dark, uint8_t run_kind) {
    uint64_t end = sc_clock_ms() + SCENE_WAIT_MS;
    catalog *c = &a->meta.cat;
    ui_event ev;
    bool note = run_kind == RUN_TABLE_NOTE;
    scene_pick p = {(const uint8_t *)"store", (const uint8_t *)"store",
                    note ? (const uint8_t *)"cheap_products" : (const uint8_t *)"products", 5, 5,
                    note ? 14u : 8u, false};
    size_t store = 0, other = 0, child = 0;
    if (!scene_open(a, events, fa, dark, p, &store, &other, &child, end)) {
        return false;
    }
    query_tab t;
    catalog_schema s;
    catalog_relation rel;
    bool done = false;
    while (!done && sc_clock_ms() < end) {
        (void)wait_and_poll(a, events, 50, &ev);
        done = get_tab(a, a->book.sel, &t) && t.ran && !t.pending_page && a->book.live == TAB_NONE &&
               catalog_schema_at(c, store, &s) && catalog_relation_at(c, &s, child, &rel) &&
               rel.count_load != CATALOG_LOAD_PENDING && rel.detail != CATALOG_LOAD_PENDING;
    }
    if (!done || !t.kept || t.res.rows == 0) {
        return false;
    }
    a->focus = FOCUS_GRID;
    if (run_kind == RUN_TABLE_BACK) {
        return play_back(a, events, fa, end);
    }
    return run_kind != RUN_TABLE_STRUCTURE || navigate(a, NAV_MOVE_SWITCH);
}

static bool play_editor(app *a, sc_arena *events, uint8_t run_kind) {
    query_tab t;
    if (!get_tab(a, a->book.sel, &t) || t.res.cols != 2u || !sc_arena_reset(events)) {
        return false;
    }
    size_t len = editor_len(&t.ed);
    if (run_kind == RUN_EDITOR) {
        return caret_at(a, events, SC_STR_LIT(events, "pg_class"), 8u);
    }
    if (!caret_at(a, events, SC_STR_LIT(events, "r' AND"), 0) || !sc_arena_reset(events) ||
        !ui_script_compose(events, SC_STR_LIT(events, "\xE3\x81\xAB\xE3\x81\xBB\xE3\x82\x93\xE3\x81\x94"), 1,
                           2)) {
        return false;
    }
    ui_event ev;
    while (ui_wait(events, 100u, &ev)) {
        if (ev.kind == UI_EVENT_COMPOSE) {
            (void)compose(a, events, ev);
        }
    }
    return get_tab(a, a->book.sel, &t) && editor_len(&t.ed) == len && editor_compose_text(&t.ed).len == 12u;
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
    /* Static: a worker still blocked at quit holds pointers into its links,
     * and the process exits around it after app_main returns (U13). */
    static app a;
    sc_arena fa, events, clip, tab_arena, grid_scratch, layout, info, records, keep, defs, sql, pages;
    if (!parse_args(args_arena, args, &o)) {
        return 2;
    }
    if (!ui_arena_new(&fa, FRAME_BYTES) || !ui_arena_new(&events, EVENT_BYTES) ||
        !ui_arena_new(&clip, CLIP_BYTES) || !ui_arena_new(&tab_arena, TAB_BYTES) ||
        !ui_arena_new(&grid_scratch, GRID_SCRATCH) || !ui_arena_new(&layout, LAYOUT_BYTES) ||
        !ui_arena_new(&info, 64) || !ui_arena_new(&records, CATALOG_BYTES) ||
        !ui_arena_new(&keep, CATALOG_KEEP_BYTES) || !ui_arena_new(&defs, DEFS_BYTES) ||
        !ui_arena_new(&sql, CATALOG_SQL_BYTES) || !ui_arena_new(&pages, PAGE_SQL_BYTES) ||
        !ui_open(&fa, SC_STR_LIT(&fa, "Safe C Explorer"), 1100, 720)) {
        return 1;
    }
    a.layout.arena = &layout;
    a.layout.rects = SC_TSLICE_ALLOC(ui_rect, &layout, LAYOUT_TAGS_MAX);
    a.layout.count = 0;
    a.focus = FOCUS_EDITOR;
    tree_init(&a.tree);
    a.show_sidebar = true;
    ui_outline_tags(o.outline);
    bool dark = o.has_scene && o.theme_fixed ? o.dark : ui_dark_theme();
    uint8_t scene = o.has_scene ? o.scene : SCENE_QUERY;
    bool ok = scene != SCENE_QUERY ||
              app_open(&a, &tab_arena, &grid_scratch, &info, args_arena, &records, &keep, &defs, &sql, &pages, &o);
    if (o.has_shot) {
        if (scene == SCENE_QUERY) {
            ok = ok && play(&a, &events, &clip, o.run) &&
                 (o.run != RUN_TABS || play_tabs(&a, &events, &clip)) &&
                 ((o.run != RUN_CANCELLING && o.run != RUN_DISCONNECT_OFFER) ||
                  play_cancel(&a, &events, o.run)) &&
                 (o.run != RUN_MILLION || measure(&a, &fa, &clip, dark)) &&
                 ((o.run != RUN_SIDEBAR && o.run != RUN_SIDEBAR_ERROR) ||
                  play_sidebar(&a, &events, &fa, dark, o.run)) &&
                 ((o.run != RUN_TABLE_CONTENT && o.run != RUN_TABLE_STRUCTURE && o.run != RUN_TABLE_NOTE &&
                   o.run != RUN_TABLE_BACK) ||
                  play_table(&a, &events, &fa, dark, o.run)) &&
                 ((o.run != RUN_EDITOR && o.run != RUN_EDITOR_COMPOSE) || play_editor(&a, &events, o.run)) &&
                 pose(&a, &fa, dark, &o);
        }
        ok = ok && (o.text_check != TEXT_CHECK_COMPOSE || scene_text_compose(&events)) &&
             (o.text_check != TEXT_CHECK_CLIPBOARD || scene_text_clipboard(&clip, EDITOR_CAP));
        ok = ok && ui_screenshot_next(args_arena, o.shot) && draw(&fa, scene, dark, &a);
        ok = ok && (o.run != RUN_RECONNECT || play_reconnect(&a, &events, &clip));
        uint64_t quit = sc_clock_ms();
        bool joined = app_close(&a, &events);
        if ((o.run == RUN_TABS || o.run == RUN_DISCONNECT_OFFER) &&
            sc_clock_ms() - quit > CLOSE_WAIT_MS) {
            ok = false; /* plans U12 and U13: quitting takes at most 2 s */
        }
        if (joined) {
            ui_close();
        }
        return ok && joined ? 0 : 1;
    }
    /* A frame that fails is skipped and the next event draws again; only a
     * run of failures ends the app. The headless path above stays strict. */
    uint32_t failures = ok && draw(&fa, scene, dark, &a) ? 0u : 1u;
    ok = ok && failures < FRAME_FAILURES;
    ui_event ev;
    while (ok) {
        uint32_t wait = query_active(&a) || a.meta.running ? ACTIVE_WAIT_MS : IDLE_WAIT_MS;
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
        if (scene == SCENE_QUERY && ev.kind == UI_EVENT_COMPOSE) {
            dirty = compose(&a, &events, ev) || dirty;
        }
        if (scene == SCENE_QUERY && ev.kind == UI_EVENT_POINTER_DOWN) {
            dirty = pointer(&a, &events, ev) || dirty;
        }
        if (scene == SCENE_QUERY && ev.kind == UI_EVENT_POINTER_MOVE) {
            dirty = pointer_move(&a, &events, ev) || dirty;
        }
        if (ev.kind == UI_EVENT_POINTER_UP) {
            a.dragging = false;
        }
        if (scene == SCENE_QUERY && ev.kind == UI_EVENT_WHEEL) {
            dirty = wheel(&a, ev) || dirty;
        }
        if (scene == SCENE_QUERY && ev.kind == UI_EVENT_KEY_DOWN) {
            dirty = key(&a, &clip, ev) || dirty;
        }
        dirty = (scene == SCENE_QUERY && end_compose(&a)) || dirty;
        if (dirty) {
            failures = draw(&fa, scene, dark, &a) ? 0u : failures + 1u;
            ok = failures < FRAME_FAILURES;
        }
    }
    /* A worker still blocked after 2 s is not joined: the process exits
     * around it (U13), and without ui_close, which it could still ring. */
    if (app_close(&a, &events)) {
        ui_close();
    }
    return ok ? 0 : 1;
}
