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
 * query-error, query-error-rows, conn-auth-failed and conn-disconnected.
 */
#include "frame.h"
#include "sc_ui.h"
#include "view_grid.h"

#define FRAME_BYTES (4u << 20)
#define EVENT_BYTES (1u << 16)
#define INPUT_BYTES (INPUT_CAP + 64u)
#define CLIP_BYTES (INPUT_CAP + 64u)
#define FRAME_NODES 1024
#define FRAME_DRAWS 16384
#define RESULT_ARENAS 96u
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
    return frame_begin(&b, fa, FRAME_NODES, FRAME_DRAWS) && shell_build(&b, &s) &&
           frame_end(&b, out);
}

/* The query window: the connection, the input box and the last result. */
typedef struct {
    conn_link link;
    bool linked;       /* link_init succeeded; link_free is owed */
    input_box input;
    bool custom_conninfo;
    bool ran;          /* a run went out */
    bool timing;       /* the run is active; its clock is running */
    uint64_t started_ms;
    uint64_t elapsed_ms;
    uint8_t notice;
} app;

/* A broken link gets no more replies, so its query is over too. */
static bool query_active(const app *a) {
    return conn_ui_active(&a->link.ui) && !a->link.broken;
}

/* Advances the run's clock, and stops it once the run has ended. */
static void tick(app *a) {
    if (!a->timing) {
        return;
    }
    a->elapsed_ms = sc_clock_ms() - a->started_ms;
    a->timing = query_active(a);
}

static bool query_scene(sc_arena *fa, const app *a, bool dark, ui_frame *out) {
    frame_builder b;
    grid_view v;
    uint32_t w = 0, h = 0;
    ui_window_size(&w, &h);
    v.dark = dark;
    v.link = &a->link;
    v.input = &a->input;
    v.custom_conninfo = a->custom_conninfo;
    v.elapsed_ms = a->elapsed_ms;
    v.ran = a->ran;
    v.notice = a->notice;
    v.width = w;
    v.height = h;
    return frame_begin(&b, fa, FRAME_NODES, FRAME_DRAWS) && grid_build(&b, &v) &&
           frame_end(&b, out);
}

static bool draw(sc_arena *fa, uint8_t scene, bool dark, const app *a) {
    ui_frame f;
    if (!sc_arena_reset(fa)) {
        return false;
    }
    bool built = scene == SCENE_HELLO   ? hello_scene(fa, dark, &f)
                 : scene == SCENE_QUERY ? query_scene(fa, a, dark, &f)
                                        : shell_scene(fa, dark, &f);
    return built && ui_draw(fa, f);
}

/* What a U8 scene runs once connected. */
enum { RUN_NONE = 0, RUN_ROWS, RUN_WIDE, RUN_TAG, RUN_ERROR, RUN_ERROR_ROWS, RUN_TERMINATE };

typedef struct {
    bool has_shot;
    sc_str shot;
    bool has_scene;
    uint8_t scene;
    uint8_t run; /* RUN_*, for SCENE_QUERY */
    bool wrong_password;
    bool dark;
    bool theme_fixed; /* the scene names its theme; THEME events still switch it */
    sc_str conninfo;
} options;

static bool query_scene_named(const sc_arena *a, sc_str v, options *o) {
    o->run = SC_STR_EQ_LIT(a, v, "query-rows")          ? RUN_ROWS
             : SC_STR_EQ_LIT(a, v, "query-wide")        ? RUN_WIDE
             : SC_STR_EQ_LIT(a, v, "query-tag")         ? RUN_TAG
             : SC_STR_EQ_LIT(a, v, "query-error")       ? RUN_ERROR
             : SC_STR_EQ_LIT(a, v, "query-error-rows")  ? RUN_ERROR_ROWS
             : SC_STR_EQ_LIT(a, v, "conn-disconnected") ? RUN_TERMINATE
                                                        : RUN_NONE;
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
    o->wrong_password = false;
    o->dark = false;
    o->theme_fixed = false;
    o->conninfo = empty;
    for (size_t i = 1; i < n; i += 2) {
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
            o->dark = false;
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

static bool app_open(app *a, sc_arena *input_arena, sc_arena *scratch, const sc_arena *args_arena,
                     const options *o) {
    conn_limits lim = {4, 64, 10000};
    a->custom_conninfo = o->conninfo.len > 0;
    a->ran = false;
    a->timing = false;
    a->elapsed_ms = 0;
    a->notice = QUERY_NOTICE_NONE;
    a->linked = false;
    if (!input_init(&a->input, input_arena, INPUT_CAP)) {
        return false;
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

static void refused(app *a) {
    conn_refuse why = a->link.refusal;
    a->notice = why == CONN_REFUSE_BUSY            ? QUERY_NOTICE_BUSY
                : why == CONN_REFUSE_NOT_CONNECTED ? QUERY_NOTICE_NOT_CONNECTED
                                                   : QUERY_NOTICE_POOL;
}

static void run(app *a) {
    if (!link_run(&a->link, a->input.arena, input_text(&a->input))) {
        a->notice = QUERY_NOTICE_NOT_CONNECTED;
        return;
    }
    if (a->link.refused) {
        refused(a);
        return;
    }
    a->notice = QUERY_NOTICE_NONE;
    a->ran = true;
    a->timing = true;
    a->started_ms = sc_clock_ms();
    a->elapsed_ms = 0;
}

static void insert(app *a, const sc_arena *sa, sc_str s) {
    input_status st = input_insert(&a->input, sa, s);
    a->notice = st == INPUT_TOO_BIG     ? QUERY_NOTICE_TOO_BIG
                : st == INPUT_NOT_ASCII ? QUERY_NOTICE_NOT_ASCII
                                        : QUERY_NOTICE_NONE;
}

static void paste(app *a, sc_arena *clip) {
    sc_str s;
    if (!sc_arena_reset(clip)) {
        return;
    }
    uint8_t got = ui_clipboard(clip, INPUT_CAP, &s);
    if (got == UI_CLIP_TOO_BIG) {
        a->notice = QUERY_NOTICE_CLIP_TOO_BIG;
    } else if (got == UI_CLIP_OK) {
        insert(a, clip, s);
    }
}

/* Returns true when the key changed what the window shows. */
static bool key(app *a, sc_arena *events, sc_arena *clip, ui_event ev) {
    switch (input_key_for(ev.key, ev.mods)) {
    case INPUT_KEY_RUN:
        run(a);
        return true;
    case INPUT_KEY_NEWLINE:
        insert(a, events, SC_STR_LIT(events, "\n"));
        return true;
    case INPUT_KEY_TAB:
        insert(a, events, SC_STR_LIT(events, "\t"));
        return true;
    case INPUT_KEY_BACKSPACE:
        input_backspace(&a->input);
        return true;
    case INPUT_KEY_LEFT:
        input_left(&a->input);
        return true;
    case INPUT_KEY_RIGHT:
        input_right(&a->input);
        return true;
    case INPUT_KEY_UP:
        input_up(&a->input);
        return true;
    case INPUT_KEY_DOWN:
        input_down(&a->input);
        return true;
    case INPUT_KEY_SELECT_ALL:
        input_select_all(&a->input);
        return true;
    case INPUT_KEY_PASTE:
        paste(a, clip);
        return true;
    case INPUT_KEY_CANCEL:
        return query_active(a) && link_cancel(&a->link);
    default:
        return false;
    }
}

/* Waits up to `ms` for one input event (UI_EVENT_NONE on a timeout), then
 * drains the worker's replies: after a ring, and on every timeout too, since a
 * ring can be lost (KTD6). True when a reply arrived. */
static bool wait_and_poll(app *a, sc_arena *events, uint32_t ms, ui_event *ev) {
    if (!sc_arena_reset(events) || !ui_wait(events, ms, ev)) {
        ev->kind = UI_EVENT_NONE;
    }
    bool any = a->linked && link_poll(&a->link);
    tick(a);
    return any;
}

/* Sends CLOSE, waits up to 2 s for BYE, and joins the worker; ui_close must
 * wait for the join, since the worker rings through ui_wake. A worker that
 * sent no BYE in time is still joined: link_join closes its request channel,
 * which makes it return. */
static void app_close(app *a, sc_arena *events) {
    if (!a->linked) {
        return;
    }
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

static sc_str scene_sql(sc_arena *a, uint8_t run) {
    switch (run) {
    case RUN_ROWS:
        return SC_STR_LIT(a, "SELECT g AS id,\n"
                             "       CASE g WHEN 3 THEN U&'Zo\\00EB \\00C5ngstr\\00F6m'\n"
                             "              ELSE 'user ' || g END AS name,\n"
                             "       CASE WHEN g % 7 = 0 THEN NULL ELSE g * 1.25 END AS score,\n"
                             "       g % 2 = 0 AS even\n"
                             "FROM generate_series(1, 250) AS g;");
    case RUN_WIDE:
        return SC_STR_LIT(a, "SELECT g AS id, repeat('W', 2000) AS wide, repeat('m', 2000) AS next,\n"
                             "       repeat('a', 95) || U&'\\00E9tail' AS cut_in_a_character\n"
                             "FROM generate_series(1, 3) AS g;");
    case RUN_TAG:
        return SC_STR_LIT(a, "CREATE TEMP TABLE notes (id serial PRIMARY KEY, body text);");
    case RUN_ERROR:
        return SC_STR_LIT(a, "SELECT relnam\nFROM pg_class;");
    case RUN_ERROR_ROWS:
        return SC_STR_LIT(a, "SELECT g, 100 / (60 - g) AS ratio\nFROM generate_series(1, 100) AS g;");
    case RUN_TERMINATE:
        return SC_STR_LIT(a, "SELECT pg_terminate_backend(pg_backend_pid());");
    default:
        return SC_STR_LIT(a, "");
    }
}

/* Plays a U8 scene: connects, types and runs its SQL, and waits for the end.
 * The SQL goes in through the input box, as typing would. */
static bool play(app *a, sc_arena *events, sc_arena *scratch, uint8_t run_kind) {
    ui_event ev;
    uint64_t end = sc_clock_ms() + SCENE_WAIT_MS;
    while (a->link.ui.state == UI_CONN_CONNECTING && !a->link.broken && sc_clock_ms() < end) {
        (void)wait_and_poll(a, events, 50, &ev);
    }
    if (run_kind == RUN_NONE) {
        return a->link.ui.state != UI_CONN_CONNECTING;
    }
    if (!sc_arena_reset(scratch) ||
        input_insert(&a->input, scratch, scene_sql(scratch, run_kind)) != INPUT_OK) {
        return false;
    }
    run(a);
    while (query_active(a) && !a->link.broken && sc_clock_ms() < end) {
        (void)wait_and_poll(a, events, 50, &ev);
    }
    return a->ran && !query_active(a);
}

int app_main(sc_arena *args_arena, sc_tslice args) {
    options o;
    app a = {0};
    sc_arena fa, events, input_arena, clip;
    if (!parse_args(args_arena, args, &o)) {
        return 2;
    }
    if (!ui_arena_new(&fa, FRAME_BYTES) || !ui_arena_new(&events, EVENT_BYTES) ||
        !ui_arena_new(&input_arena, INPUT_BYTES) || !ui_arena_new(&clip, CLIP_BYTES) ||
        !ui_open(&fa, SC_STR_LIT(&fa, "Safe C Explorer"), 1100, 720)) {
        return 1;
    }
    bool dark = o.has_scene && o.theme_fixed ? o.dark : ui_dark_theme();
    uint8_t scene = o.has_scene ? o.scene : SCENE_QUERY;
    bool ok = scene != SCENE_QUERY || app_open(&a, &input_arena, &clip, args_arena, &o);
    if (o.has_shot) {
        ok = ok && (scene != SCENE_QUERY || play(&a, &events, &clip, o.run)) &&
             ui_screenshot_next(args_arena, o.shot) && draw(&fa, scene, dark, &a);
        app_close(&a, &events);
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
        bool dirty = wait_and_poll(&a, &events, wait, &ev) || a.timing;
        if (ev.kind == UI_EVENT_QUIT) {
            break;
        }
        if (ev.kind == UI_EVENT_THEME) {
            dark = ev.dark;
        }
        dirty = dirty || ev.kind == UI_EVENT_RESIZE || ev.kind == UI_EVENT_THEME ||
                ev.kind == UI_EVENT_EXPOSE;
        if (scene == SCENE_QUERY && ev.kind == UI_EVENT_TEXT) {
            insert(&a, &events, ev.text);
            dirty = true;
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
