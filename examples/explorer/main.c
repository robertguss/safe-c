/* The Postgres explorer (docs/plans/2026-10-25-2038-feat-postgres-explorer-plan.md).
 * Glue code: adapters/ui.c owns the C main and calls app_main here (KTD17).
 *
 *   explorer                                   opens the window
 *   explorer --scene hello --screenshot a.png  renders one scene and saves it (KTD15)
 */
#include "frame.h"
#include "sc_ui.h"

#define FRAME_BYTES (1u << 20)
#define EVENT_BYTES (1u << 16)
#define FRAME_NODES 1024
#define FRAME_DRAWS 8192

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

static bool draw(sc_arena *fa, bool dark) {
    ui_frame f;
    bool ok = sc_arena_reset(fa) && hello_scene(fa, dark, &f) && ui_draw(fa, f);
    return ok;
}

typedef struct {
    bool has_shot;
    sc_str shot;
} options;

/* Returns false on an unknown argument or scene. */
static bool parse_args(const sc_arena *a, sc_tslice args, options *o) {
    sc_str key, value;
    size_t n = sc_tslice_len(args);
    o->has_shot = false;
    for (size_t i = 1; i < n; i += 2) {
        if (!SC_TSLICE_GET(sc_str, a, args, i, &key) || i + 1 >= n ||
            !SC_TSLICE_GET(sc_str, a, args, i + 1, &value)) {
            return false;
        }
        if (SC_STR_EQ_LIT(a, key, "--screenshot")) {
            o->has_shot = true;
            o->shot = value;
        } else if (!SC_STR_EQ_LIT(a, key, "--scene") || !SC_STR_EQ_LIT(a, value, "hello")) {
            return false;
        }
    }
    return true;
}

int app_main(sc_arena *args_arena, sc_tslice args) {
    options o;
    sc_arena fa, events;
    if (!parse_args(args_arena, args, &o)) {
        return 2;
    }
    if (!ui_arena_new(&fa, FRAME_BYTES) || !ui_arena_new(&events, EVENT_BYTES) ||
        !ui_open(&fa, SC_STR_LIT(&fa, "Safe C Explorer"), 1100, 720)) {
        return 1;
    }
    bool dark = ui_dark_theme();
    if (o.has_shot) {
        bool ok = ui_screenshot_next(args_arena, o.shot) && draw(&fa, dark);
        ui_close();
        return ok ? 0 : 1;
    }
    bool ok = draw(&fa, dark);
    ui_event ev;
    while (ok) {
        if (!sc_arena_reset(&events)) {
            ok = false;
        } else if (ui_wait(&events, 1000, &ev)) {
            if (ev.kind == UI_EVENT_QUIT) {
                break;
            }
            if (ev.kind == UI_EVENT_THEME) {
                dark = ev.dark;
            }
            if (ev.kind == UI_EVENT_RESIZE || ev.kind == UI_EVENT_THEME ||
                ev.kind == UI_EVENT_EXPOSE) {
                ok = draw(&fa, dark);
            }
        }
    }
    ui_close();
    return ok ? 0 : 1;
}
