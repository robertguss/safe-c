/* The explorer's UI adapter (subset rule R0). It owns the C main, every SDL3,
 * SDL3_ttf and Clay call, and all float and C-string work, so app code sees
 * only the types in sc_ui.h and sc_ui_frame.h. Clay is vendored at
 * third_party/clay.h (pin in third_party/CLAY_VERSION).
 *
 * Layout runs in points and drawing in pixels: the window asks for full pixel
 * density, fonts open at point size times the pixel density, and every
 * rectangle Clay returns is scaled once at draw time (plan KTD8). */
#define SDL_MAIN_HANDLED
#include "sc_ui.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdlib.h>
#include <string.h>

#define CLAY_IMPLEMENTATION
#include <clay.h> /* adapters/third_party, on the -isystem path: its warnings are not ours */

/* --- state ------------------------------------------------------------- */

static SDL_Window *window;
static SDL_Renderer *renderer;
static TTF_TextEngine *engine;
static float density = 1.0f; /* pixels per point */
static Uint32 wake_type;
static bool clay_failed;
static bool text_failed; /* a text in the current frame could not be shaped */
/* The frame being drawn, for CUSTOM render commands. */
static const sc_arena *draw_arena;
static ui_frame draw_frame;
static bool clipped;
static SDL_Rect clip_now;
static char *shot_path; /* set by ui_screenshot_next, cleared by save_shot */

static const char *const font_files[UI_FONT_COUNT] = {
    "fonts/Inter-Regular.ttf", "fonts/Inter-Medium.ttf", "fonts/JetBrainsMono-Regular.ttf"};

/* Open fonts, one per (font, point size), at size times density. */
#define FONT_SLOTS 32
static struct {
    uint8_t font;
    uint16_t size;
    TTF_Font *ttf;
} fonts[FONT_SLOTS];
static int font_count;

/* Shaped text, reused across frames. `live` holds the entries used this frame
 * and `prev` those used last frame; a hit in `prev` moves to `live`, and at the
 * end of a frame whatever is left in `prev` is destroyed (plan U2 step 5). */
/* A power of two. A frame shapes at most CLAY_ELEMENTS texts through Clay plus
 * one per draw record, and the spill list holds TEXT_SLOTS, so a frame with
 * more distinct texts than that fails rather than overflow. */
#define TEXT_SLOTS 16384
#define CLAY_ELEMENTS 8192
/* Clay caches word widths; it needs room for a few frames of distinct words
 * before it evicts, or every later layout fails (found in review). */
#define CLAY_WORDS 262144
typedef struct {
    TTF_Text *text; /* NULL with `moved` false marks an empty slot */
    bool moved;     /* in `prev` only: moved to `live`, a tombstone for probing */
    uint64_t hash;
    uint8_t font;
    uint16_t size;
    size_t len;
    char *bytes;
} text_entry;
static text_entry *live, *prev;
static size_t live_count;
/* Texts that did not fit in `live`; destroyed after the frame is drawn. */
static TTF_Text **spill;
static size_t spill_count;
static void *clay_mem;

/* --- arenas and main ---------------------------------------------------- */

bool ui_arena_new(sc_arena *out, size_t cap) {
    uint8_t *buf = cap > 0 ? malloc(cap) : NULL;
    if (buf == NULL) {
        return false;
    }
    sc_arena_init(out, buf, cap);
    return true;
}

int main(int argc, char **argv) {
    size_t bytes = 64 + (size_t)argc * (sizeof(sc_str) + 8);
    for (int i = 0; i < argc; i++) {
        bytes += strlen(argv[i]) + 8;
    }
    sc_arena args;
    if (!ui_arena_new(&args, bytes)) {
        return 1;
    }
    sc_tslice list = SC_TSLICE_ALLOC(sc_str, &args, (size_t)argc);
    for (int i = 0; i < argc; i++) {
        sc_str s = sc_str_from_lit(&args, (const uint8_t *)argv[i], strlen(argv[i]));
        SC_TSLICE_SET(sc_str, &args, list, (size_t)i, &s);
    }
    SDL_SetMainReady();
    return app_main(&args, list);
}

/* --- strings ------------------------------------------------------------ */

static bool live_str(const sc_arena *a, sc_str s) {
    return s.len == 0 || sc_slice_sub(a, s, 0, s.len).len == s.len;
}

/* A NUL-terminated copy for SDL, or NULL. The caller frees it. */
static char *c_string(const sc_arena *a, sc_str s) {
    if (!live_str(a, s)) {
        return NULL;
    }
    char *c = malloc(s.len + 1);
    if (c != NULL) {
        if (s.len > 0) {
            memcpy(c, s.ptr, s.len);
        }
        c[s.len] = '\0';
    }
    return c;
}

static uint64_t hash_bytes(const char *p, size_t n, uint8_t font, uint16_t size) {
    uint64_t h = 1469598103934665603ull ^ ((uint64_t)font << 16 | size);
    for (size_t i = 0; i < n; i++) {
        h = (h ^ (uint8_t)p[i]) * 1099511628211ull;
    }
    return h;
}

/* --- fonts and text cache ------------------------------------------------ */

static void flush_fonts(void) {
    for (size_t i = 0; i < TEXT_SLOTS; i++) {
        if (live[i].text != NULL) {
            TTF_DestroyText(live[i].text);
            free(live[i].bytes);
        }
        if (prev[i].text != NULL) {
            TTF_DestroyText(prev[i].text);
            free(prev[i].bytes);
        }
    }
    memset(live, 0, TEXT_SLOTS * sizeof *live);
    memset(prev, 0, TEXT_SLOTS * sizeof *prev);
    live_count = 0;
    for (int i = 0; i < font_count; i++) {
        TTF_CloseFont(fonts[i].ttf);
    }
    font_count = 0;
}

static TTF_Font *font_for(uint8_t font, uint16_t size) {
    if (font >= UI_FONT_COUNT || size == 0) {
        return NULL;
    }
    for (int i = 0; i < font_count; i++) {
        if (fonts[i].font == font && fonts[i].size == size) {
            return fonts[i].ttf;
        }
    }
    if (font_count == FONT_SLOTS) {
        return NULL;
    }
    char path[4096];
    const char *base = SDL_GetBasePath();
    SDL_snprintf(path, sizeof path, "%s%s", base == NULL ? "" : base, font_files[font]);
    TTF_Font *f = TTF_OpenFont(path, (float)size * density);
    if (f == NULL) {
        SDL_Log("ui: cannot open %s: %s", path, SDL_GetError());
        return NULL;
    }
    fonts[font_count].font = font;
    fonts[font_count].size = size;
    fonts[font_count].ttf = f;
    font_count++;
    return f;
}

static text_entry *slot_in(text_entry *table, uint64_t h, uint8_t font, uint16_t size,
                           const char *p, size_t n) {
    for (size_t i = (size_t)h & (TEXT_SLOTS - 1);; i = (i + 1) & (TEXT_SLOTS - 1)) {
        text_entry *e = &table[i];
        if (e->text == NULL && !e->moved) {
            return e;
        }
        if (e->text != NULL && e->hash == h && e->font == font && e->size == size &&
            e->len == n && memcmp(e->bytes, p, n) == 0) {
            return e;
        }
    }
}

static TTF_Text *shaped(const char *p, size_t n, uint8_t font, uint16_t size) {
    uint64_t h = hash_bytes(p, n, font, size);
    text_entry *e = slot_in(live, h, font, size, p, n);
    if (e->text != NULL) {
        return e->text;
    }
    text_entry *old = slot_in(prev, h, font, size, p, n);
    TTF_Text *t = old->text;
    char *copy = old->bytes;
    if (t != NULL) {
        *old = (text_entry){NULL, true, 0, 0, 0, 0, NULL};
    }
    if (t == NULL) {
        TTF_Font *f = font_for(font, size);
        t = f == NULL ? NULL : TTF_CreateText(engine, f, p, n);
        copy = t == NULL ? NULL : malloc(n + 1);
        if (copy != NULL && n > 0) {
            memcpy(copy, p, n);
        }
    }
    if (t == NULL) {
        return NULL;
    }
    if (copy == NULL || live_count >= TEXT_SLOTS / 4 * 3) {
        free(copy);
        if (spill_count == TEXT_SLOTS) {
            TTF_DestroyText(t);
            return NULL;
        }
        spill[spill_count++] = t;
        return t;
    }
    *e = (text_entry){t, false, h, font, size, n, copy};
    live_count++;
    return t;
}

/* Whatever `prev` still holds was not drawn this frame. */
static void end_text_frame(void) {
    for (size_t i = 0; i < TEXT_SLOTS; i++) {
        if (prev[i].text != NULL) {
            TTF_DestroyText(prev[i].text);
        }
        free(prev[i].bytes);
    }
    memset(prev, 0, TEXT_SLOTS * sizeof *prev);
    text_entry *t = prev;
    prev = live;
    live = t;
    live_count = 0;
    for (size_t i = 0; i < spill_count; i++) {
        TTF_DestroyText(spill[i]);
    }
    spill_count = 0;
}

/* --- Clay hooks ---------------------------------------------------------- */

static Clay_Dimensions measure(Clay_StringSlice s, Clay_TextElementConfig *c, void *ud) {
    (void)ud;
    int w = 0, h = 0;
    TTF_Font *f = font_for((uint8_t)c->fontId, c->fontSize);
    if (f != NULL && s.length > 0) {
        TTF_GetStringSize(f, s.chars, (size_t)s.length, &w, &h);
    }
    if (f != NULL && h == 0) {
        h = TTF_GetFontHeight(f);
    }
    return (Clay_Dimensions){(float)w / density, (float)h / density};
}

static void clay_error(Clay_ErrorData e) {
    clay_failed = true;
    SDL_Log("ui: clay: %.*s", (int)e.errorText.length, e.errorText.chars);
}

/* --- window ----------------------------------------------------------------- */

static void update_density(void) {
    float d = SDL_GetWindowPixelDensity(window);
    if (d <= 0.0f) {
        d = 1.0f;
    }
    if (d != density) {
        flush_fonts();
        Clay_ResetMeasureTextCache(); /* its widths came from the old fonts */
        density = d;
    }
}

bool ui_open(const sc_arena *a, sc_str title, uint32_t width, uint32_t height) {
    char *t = c_string(a, title);
    if (t == NULL || !SDL_Init(SDL_INIT_VIDEO) || !TTF_Init()) {
        free(t);
        return false;
    }
    window = SDL_CreateWindow(t, (int)width, (int)height,
                              SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
    free(t);
    if (window == NULL) {
        SDL_Log("ui: window: %s", SDL_GetError());
        goto fail;
    }
    /* Below this the fixed sidebar and bars no longer fit (found in review). */
    SDL_SetWindowMinimumSize(window, UI_MIN_WIDTH, UI_MIN_HEIGHT);
    /* Headless runs draw with the software renderer, so screenshots are stable. */
    bool offscreen = SDL_strcmp(SDL_GetCurrentVideoDriver(), "offscreen") == 0;
    renderer = SDL_CreateRenderer(window, offscreen ? "software" : NULL);
    if (renderer == NULL) {
        SDL_Log("ui: renderer: %s", SDL_GetError());
        goto fail;
    }
    if (!offscreen) {
        SDL_SetRenderVSync(renderer, 1);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    engine = TTF_CreateRendererTextEngine(renderer);
    live = calloc(TEXT_SLOTS, sizeof *live);
    prev = calloc(TEXT_SLOTS, sizeof *prev);
    spill = calloc(TEXT_SLOTS, sizeof *spill);
    density = SDL_GetWindowPixelDensity(window);
    if (density <= 0.0f) {
        density = 1.0f;
    }
    if (engine == NULL || live == NULL || prev == NULL || spill == NULL) {
        goto fail;
    }
    for (uint8_t i = 0; i < UI_FONT_COUNT; i++) {
        if (font_for(i, 13) == NULL) {
            goto fail;
        }
    }
    Clay_SetMaxElementCount(CLAY_ELEMENTS);
    Clay_SetMaxMeasureTextCacheWordCount(CLAY_WORDS);
    uint32_t clay_bytes = Clay_MinMemorySize();
    clay_mem = malloc(clay_bytes);
    if (clay_mem == NULL) {
        goto fail;
    }
    Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(clay_bytes, clay_mem),
                    (Clay_Dimensions){(float)width, (float)height},
                    (Clay_ErrorHandler){clay_error, NULL});
    Clay_SetMeasureTextFunction(measure, NULL);
    wake_type = SDL_RegisterEvents(1);
    SDL_StartTextInput(window);
    return true;
fail:
    ui_close();
    return false;
}

void ui_close(void) {
    if (live != NULL && prev != NULL) {
        flush_fonts();
    }
    TTF_DestroyRendererTextEngine(engine);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    Clay_SetCurrentContext(NULL);
    free(live);
    free(prev);
    free(spill);
    free(clay_mem);
    free(shot_path);
    live = NULL;
    prev = NULL;
    spill = NULL;
    clay_mem = NULL;
    shot_path = NULL;
    engine = NULL;
    renderer = NULL;
    window = NULL;
}

void ui_window_size(uint32_t *w, uint32_t *h) {
    int x = 0, y = 0;
    SDL_GetWindowSize(window, &x, &y);
    *w = (uint32_t)x;
    *h = (uint32_t)y;
}

bool ui_dark_theme(void) {
    return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK;
}

/* --- events --------------------------------------------------------------- */

static uint16_t mods_of(SDL_Keymod m) {
    return (uint16_t)(((m & SDL_KMOD_SHIFT) ? UI_MOD_SHIFT : 0) |
                      ((m & SDL_KMOD_CTRL) ? UI_MOD_CTRL : 0) |
                      ((m & SDL_KMOD_ALT) ? UI_MOD_ALT : 0) |
                      ((m & SDL_KMOD_GUI) ? UI_MOD_GUI : 0));
}

/* Fills *out from `ev`; false for events the app does not see. */
static bool translate(sc_arena *events, const SDL_Event *ev, ui_event *out) {
    memset(out, 0, sizeof *out);
    out->text = (sc_str){NULL, 0, events->epoch};
    switch (ev->type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        out->kind = UI_EVENT_QUIT;
        return true;
    case SDL_EVENT_KEY_DOWN:
        out->kind = UI_EVENT_KEY_DOWN;
        out->key = ev->key.key;
        out->mods = mods_of(ev->key.mod);
        return true;
    case SDL_EVENT_TEXT_INPUT:
        out->kind = UI_EVENT_TEXT;
        out->text = sc_str_from_lit(events, (const uint8_t *)ev->text.text, strlen(ev->text.text));
        return true;
    case SDL_EVENT_MOUSE_MOTION:
        out->kind = UI_EVENT_POINTER_MOVE;
        out->x = (int32_t)ev->motion.x;
        out->y = (int32_t)ev->motion.y;
        out->mods = mods_of(SDL_GetModState());
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        out->kind = ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? UI_EVENT_POINTER_DOWN
                                                            : UI_EVENT_POINTER_UP;
        out->x = (int32_t)ev->button.x;
        out->y = (int32_t)ev->button.y;
        out->mods = mods_of(SDL_GetModState());
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        out->kind = UI_EVENT_WHEEL;
        out->wheel_x = (int32_t)SDL_roundf(ev->wheel.x * 16.0f);
        out->wheel_y = (int32_t)SDL_roundf(ev->wheel.y * 16.0f);
        return true;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        update_density();
        out->kind = UI_EVENT_RESIZE;
        ui_window_size(&out->width, &out->height);
        return true;
    case SDL_EVENT_WINDOW_EXPOSED:
        out->kind = UI_EVENT_EXPOSE;
        return true;
    case SDL_EVENT_SYSTEM_THEME_CHANGED:
        out->kind = UI_EVENT_THEME;
        out->dark = ui_dark_theme();
        return true;
    default:
        if (ev->type == wake_type) {
            out->kind = UI_EVENT_WAKE;
            return true;
        }
        return false;
    }
}

bool ui_wait(sc_arena *events, uint32_t timeout_ms, ui_event *out) {
    Uint64 deadline = SDL_GetTicks() + timeout_ms;
    SDL_Event ev;
    for (;;) {
        Uint64 now = SDL_GetTicks();
        Sint32 left = now >= deadline ? 0 : (Sint32)SDL_min(deadline - now, (Uint64)SDL_MAX_SINT32);
        if (!SDL_WaitEventTimeout(&ev, left)) {
            return false;
        }
        if (translate(events, &ev, out)) {
            return true;
        }
    }
}

bool ui_wake(void) {
    SDL_Event ev;
    if (wake_type == 0) {
        return false;
    }
    SDL_zero(ev);
    ev.type = wake_type;
    return SDL_PushEvent(&ev);
}

uint8_t ui_clipboard(sc_arena *a, size_t max, sc_str *out) {
    char *text = SDL_GetClipboardText();
    size_t n = text == NULL ? 0 : strlen(text);
    uint8_t st = UI_CLIP_OK;
    if (n == 0) {
        st = UI_CLIP_EMPTY;
    } else if (n > max) {
        st = UI_CLIP_TOO_BIG;
    } else {
        sc_str s = sc_str_from_lit(a, (const uint8_t *)text, n);
        if (s.len != n) {
            st = UI_CLIP_TOO_BIG; /* no room in `a` */
        } else {
            *out = s;
        }
    }
    SDL_free(text);
    return st;
}

bool ui_text_size(const sc_arena *a, sc_str text, uint8_t font, uint16_t size, uint32_t *w,
                  uint32_t *h) {
    TTF_Font *f = font_for(font, size);
    int x = 0, y = 0;
    if (f == NULL || !live_str(a, text) ||
        (text.len > 0 && !TTF_GetStringSize(f, (const char *)text.ptr, text.len, &x, &y))) {
        return false;
    }
    if (y == 0) {
        y = TTF_GetFontHeight(f);
    }
    *w = (uint32_t)SDL_ceilf((float)x / density);
    *h = (uint32_t)SDL_ceilf((float)y / density);
    return true;
}

bool ui_screenshot_next(const sc_arena *a, sc_str path) {
    char *p = c_string(a, path);
    if (p == NULL) {
        return false;
    }
    free(shot_path);
    shot_path = p;
    return true;
}

/* --- frames --------------------------------------------------------------- */

static Clay_Color color_of(ui_color c) {
    return (Clay_Color){c.r, c.g, c.b, c.a};
}

static Clay_SizingAxis axis_of(ui_size s) {
    Clay_SizingAxis x = {0};
    if (s.mode == UI_SIZE_FIXED) {
        x.type = CLAY__SIZING_TYPE_FIXED;
        x.size.minMax = (Clay_SizingMinMax){s.min, s.min};
        return x;
    }
    x.type = s.mode == UI_SIZE_GROW ? CLAY__SIZING_TYPE_GROW : CLAY__SIZING_TYPE_FIT;
    x.size.minMax = (Clay_SizingMinMax){s.min, s.max == 0 ? CLAY__MAXFLOAT : (float)s.max};
    return x;
}

static Clay_LayoutAlignmentX align_x(uint8_t a) {
    return a == UI_ALIGN_END ? CLAY_ALIGN_X_RIGHT : a == UI_ALIGN_CENTER ? CLAY_ALIGN_X_CENTER
                                                                          : CLAY_ALIGN_X_LEFT;
}

static Clay_LayoutAlignmentY align_y(uint8_t a) {
    return a == UI_ALIGN_END ? CLAY_ALIGN_Y_BOTTOM : a == UI_ALIGN_CENTER ? CLAY_ALIGN_Y_CENTER
                                                                           : CLAY_ALIGN_Y_TOP;
}

static Clay_ElementDeclaration decl_of(const ui_node *n) {
    Clay_ElementDeclaration d;
    memset(&d, 0, sizeof d);
    d.layout.sizing.width = axis_of(n->width);
    d.layout.sizing.height = axis_of(n->height);
    d.layout.padding = (Clay_Padding){n->pad_left, n->pad_right, n->pad_top, n->pad_bottom};
    d.layout.childGap = n->gap;
    d.layout.childAlignment = (Clay_ChildAlignment){align_x(n->align_x), align_y(n->align_y)};
    d.layout.layoutDirection = n->direction == UI_COLUMN ? CLAY_TOP_TO_BOTTOM : CLAY_LEFT_TO_RIGHT;
    d.backgroundColor = color_of(n->background);
    float r = n->radius;
    d.cornerRadius = (Clay_CornerRadius){r, r, r, r};
    if (n->border > 0) {
        d.border.color = color_of(n->border_color);
        d.border.width = (Clay_BorderWidth){n->border, n->border, n->border, n->border, 0};
    }
    return d;
}

static void open_box(const ui_node *n) {
    Clay__OpenElement();
    Clay__ConfigureOpenElement(decl_of(n));
}

/* A custom node: Clay sizes it, and the CUSTOM render command carries its
 * index so render() can find its draw run. */
static void open_custom(const ui_node *n, size_t index) {
    Clay_ElementDeclaration d = decl_of(n);
    d.custom.customData = (void *)(uintptr_t)(index + 1);
    Clay__OpenElement();
    Clay__ConfigureOpenElement(d);
}

static void add_text(const ui_node *n) {
    Clay_TextElementConfig c;
    memset(&c, 0, sizeof c);
    c.textColor = color_of(n->text_color);
    c.fontId = n->font;
    c.fontSize = n->font_size;
    c.wrapMode = CLAY_TEXT_WRAP_NONE;
    Clay__OpenTextElement((Clay_String){false, (int32_t)n->text.len, (const char *)n->text.ptr}, c);
}

/* Closes the open elements (boxes and custom nodes; text closes itself) from
 * node `from` up to, not including, `to`. */
static void close_to(const sc_arena *a, ui_frame f, size_t from, uint32_t to) {
    ui_node n;
    size_t c = from;
    while (c != to && SC_TSLICE_GET(ui_node, a, f.nodes, c, &n)) {
        if (n.kind != UI_NODE_TEXT) {
            Clay__CloseElement();
        }
        c = n.parent;
    }
}

/* --- drawing ---------------------------------------------------------------- */

#define ARC_SEGMENTS 16

static SDL_FColor fcolor(Clay_Color c) {
    return (SDL_FColor){c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
}

/* A filled rectangle with four rounded corners as one triangle list, after the
 * Clay SDL3 renderer (renderers/SDL3/clay_renderer_SDL3.c, zlib licence). */
static void fill_rounded(SDL_FRect r, float radius, Clay_Color color) {
    enum { VERTS = 4 + 4 * ARC_SEGMENTS * 2 + 8, INDICES = 6 + 4 * ARC_SEGMENTS * 3 + 24 };
    SDL_Vertex v[VERTS];
    int idx[INDICES];
    int nv = 0, ni = 0;
    SDL_FColor c = fcolor(color);
    float rad = SDL_min(radius, SDL_min(r.w, r.h) / 2.0f);
    float cx[4] = {r.x + rad, r.x + r.w - rad, r.x + r.w - rad, r.x + rad};
    float cy[4] = {r.y + rad, r.y + rad, r.y + r.h - rad, r.y + r.h - rad};
    float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
    for (int k = 0; k < 4; k++) {
        v[nv++] = (SDL_Vertex){{cx[k], cy[k]}, c, {0, 0}};
    }
    int quad[6] = {0, 1, 3, 1, 2, 3};
    for (int k = 0; k < 6; k++) {
        idx[ni++] = quad[k];
    }
    float step = (SDL_PI_F / 2) / ARC_SEGMENTS;
    for (int i = 0; i < ARC_SEGMENTS; i++) {
        for (int k = 0; k < 4; k++) {
            float a1 = (float)i * step, a2 = (float)(i + 1) * step;
            v[nv++] = (SDL_Vertex){{cx[k] + SDL_cosf(a1) * rad * sx[k], cy[k] + SDL_sinf(a1) * rad * sy[k]}, c, {0, 0}};
            v[nv++] = (SDL_Vertex){{cx[k] + SDL_cosf(a2) * rad * sx[k], cy[k] + SDL_sinf(a2) * rad * sy[k]}, c, {0, 0}};
            idx[ni++] = k;
            idx[ni++] = nv - 2;
            idx[ni++] = nv - 1;
        }
    }
    /* The four edge strips between the corner centres and the outer edges. */
    SDL_FPoint edge[8] = {{cx[0], r.y},       {cx[1], r.y},       {r.x + r.w, cy[1]}, {r.x + r.w, cy[2]},
                          {cx[2], r.y + r.h}, {cx[3], r.y + r.h}, {r.x, cy[3]},       {r.x, cy[0]}};
    for (int k = 0; k < 4; k++) {
        v[nv++] = (SDL_Vertex){edge[2 * k], c, {0, 0}};
        v[nv++] = (SDL_Vertex){edge[2 * k + 1], c, {0, 0}};
        int a = k, b = (k + 1) % 4;
        int strip[6] = {a, nv - 2, nv - 1, b, a, nv - 1};
        for (int j = 0; j < 6; j++) {
            idx[ni++] = strip[j];
        }
    }
    SDL_RenderGeometry(renderer, NULL, v, nv, idx, ni);
}

static void stroke_arc(SDL_FPoint centre, float radius, float from_deg, float width, Clay_Color color) {
    SDL_SetRenderDrawColor(renderer, (Uint8)color.r, (Uint8)color.g, (Uint8)color.b, (Uint8)color.a);
    float start = from_deg * (SDL_PI_F / 180.0f), step = (SDL_PI_F / 2) / ARC_SEGMENTS;
    /* One 1-pixel ring per pixel of width, centred on pixel centres so it meets
     * the straight edges, which cover [edge, edge + width). */
    for (float t = 0.0f; t < width; t += 1.0f) {
        SDL_FPoint p[ARC_SEGMENTS + 1];
        float rr = SDL_max(radius - 0.5f - t, 0.5f);
        for (int i = 0; i <= ARC_SEGMENTS; i++) {
            float a = start + (float)i * step;
            p[i] = (SDL_FPoint){centre.x + SDL_cosf(a) * rr, centre.y + SDL_sinf(a) * rr};
        }
        SDL_RenderLines(renderer, p, ARC_SEGMENTS + 1);
    }
}

static void draw_border(SDL_FRect r, const Clay_BorderRenderData *b) {
    Clay_Color c = b->color;
    float rad = SDL_min(b->cornerRadius.topLeft * density, SDL_min(r.w, r.h) / 2.0f);
    float wl = b->width.left * density, wr = b->width.right * density;
    float wt = b->width.top * density, wb = b->width.bottom * density;
    SDL_SetRenderDrawColor(renderer, (Uint8)c.r, (Uint8)c.g, (Uint8)c.b, (Uint8)c.a);
    SDL_FRect edges[4] = {{r.x, r.y + rad, wl, r.h - 2 * rad},
                          {r.x + r.w - wr, r.y + rad, wr, r.h - 2 * rad},
                          {r.x + rad, r.y, r.w - 2 * rad, wt},
                          {r.x + rad, r.y + r.h - wb, r.w - 2 * rad, wb}};
    SDL_RenderFillRects(renderer, edges, 4);
    if (rad > 0) {
        stroke_arc((SDL_FPoint){r.x + rad, r.y + rad}, rad, 180, wt, c);
        stroke_arc((SDL_FPoint){r.x + r.w - rad, r.y + rad}, rad, 270, wt, c);
        stroke_arc((SDL_FPoint){r.x + r.w - rad, r.y + r.h - rad}, rad, 0, wb, c);
        stroke_arc((SDL_FPoint){r.x + rad, r.y + r.h - rad}, rad, 90, wb, c);
    }
}

static void set_clip(const SDL_Rect *c) {
    SDL_SetRenderClipRect(renderer, c);
}

/* Draws a custom node's run of ui_draw_item records, offset to its box and clipped
 * to it (inside any enclosing scroll clip). A straight LINE is filled as a
 * rectangle one point thick, so it stays crisp at 2x. */
static void draw_custom(SDL_FRect box, Clay_CustomRenderData *cd) {
    size_t index = (size_t)(uintptr_t)cd->customData - 1;
    ui_node n;
    ui_draw_item d;
    if (!SC_TSLICE_GET(ui_node, draw_arena, draw_frame.nodes, index, &n)) {
        return;
    }
    if (cd->backgroundColor.a > 0) {
        Clay_Color c = cd->backgroundColor;
        SDL_SetRenderDrawColor(renderer, (Uint8)c.r, (Uint8)c.g, (Uint8)c.b, (Uint8)c.a);
        SDL_RenderFillRect(renderer, &box);
    }
    SDL_Rect mine = {(int)box.x, (int)box.y, (int)box.w, (int)box.h}, both = mine;
    if (clipped && !SDL_GetRectIntersection(&mine, &clip_now, &both)) {
        return;
    }
    set_clip(&both);
    for (size_t k = n.draw_first; k < (size_t)n.draw_first + n.draw_count; k++) {
        if (!SC_TSLICE_GET(ui_draw_item, draw_arena, draw_frame.draws, k, &d)) {
            break;
        }
        float x = box.x + (float)d.x * density, y = box.y + (float)d.y * density;
        float w = (float)d.w * density, h = (float)d.h * density;
        SDL_SetRenderDrawColor(renderer, d.color.r, d.color.g, d.color.b, d.color.a);
        if (d.kind == UI_DRAW_RECT || (d.kind == UI_DRAW_LINE && (d.w == 0 || d.h == 0))) {
            SDL_FRect fr = d.kind == UI_DRAW_RECT ? (SDL_FRect){x, y, w, h}
                         : (SDL_FRect){x, y, d.w == 0 ? density : w, d.h == 0 ? density : h};
            SDL_RenderFillRect(renderer, &fr);
        } else if (d.kind == UI_DRAW_LINE) {
            SDL_RenderLine(renderer, x, y, x + w, y + h);
        } else {
            TTF_Text *t = shaped((const char *)d.text.ptr, d.text.len, d.font, d.font_size);
            if (t == NULL) {
                text_failed = true;
                continue;
            }
            TTF_SetTextColor(t, d.color.r, d.color.g, d.color.b, d.color.a);
            TTF_DrawRendererText(t, SDL_roundf(x), SDL_roundf(y));
        }
    }
    set_clip(clipped ? &clip_now : NULL);
}

static void render(Clay_RenderCommandArray *cmds) {
    for (int32_t i = 0; i < cmds->length; i++) {
        Clay_RenderCommand *rc = Clay_RenderCommandArray_Get(cmds, i);
        Clay_BoundingBox bb = rc->boundingBox;
        SDL_FRect r = {SDL_roundf(bb.x * density), SDL_roundf(bb.y * density),
                       SDL_roundf(bb.width * density), SDL_roundf(bb.height * density)};
        switch (rc->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
            Clay_RectangleRenderData *d = &rc->renderData.rectangle;
            Clay_Color c = d->backgroundColor;
            if (d->cornerRadius.topLeft > 0) {
                fill_rounded(r, d->cornerRadius.topLeft * density, c);
            } else {
                SDL_SetRenderDrawColor(renderer, (Uint8)c.r, (Uint8)c.g, (Uint8)c.b, (Uint8)c.a);
                SDL_RenderFillRect(renderer, &r);
            }
        } break;
        case CLAY_RENDER_COMMAND_TYPE_TEXT: {
            Clay_TextRenderData *d = &rc->renderData.text;
            TTF_Text *t = shaped(d->stringContents.chars, (size_t)d->stringContents.length,
                                 (uint8_t)d->fontId, d->fontSize);
            if (t == NULL) {
                text_failed = true;
            } else {
                Clay_Color c = d->textColor;
                TTF_SetTextColor(t, (Uint8)c.r, (Uint8)c.g, (Uint8)c.b, (Uint8)c.a);
                TTF_DrawRendererText(t, r.x, r.y);
            }
        } break;
        case CLAY_RENDER_COMMAND_TYPE_BORDER:
            draw_border(r, &rc->renderData.border);
            break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
            clip_now = (SDL_Rect){(int)r.x, (int)r.y, (int)r.w, (int)r.h};
            clipped = true;
            set_clip(&clip_now);
            break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
            clipped = false;
            set_clip(NULL);
            break;
        case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
            draw_custom(r, &rc->renderData.custom);
            break;
        default:
            break;
        }
    }
}

static bool save_shot(void) {
    SDL_Surface *s = SDL_RenderReadPixels(renderer, NULL);
    bool ok = s != NULL && SDL_SavePNG(s, shot_path);
    if (!ok) {
        SDL_Log("ui: screenshot %s: %s", shot_path, SDL_GetError());
    }
    SDL_DestroySurface(s);
    free(shot_path);
    shot_path = NULL;
    return ok;
}

bool ui_draw(const sc_arena *frame_arena, ui_frame f) {
    if (!ui_frame_valid(frame_arena, f)) {
        return false;
    }
    draw_arena = frame_arena;
    draw_frame = f;
    uint32_t w, h;
    ui_window_size(&w, &h);
    clay_failed = false;
    text_failed = false;
    clipped = false;
    Clay_SetLayoutDimensions((Clay_Dimensions){(float)w, (float)h});
    Clay_BeginLayout();
    /* An implicit root fills the window; top-level nodes stack inside it. */
    ui_node root = {0};
    root.width.mode = UI_SIZE_GROW;
    root.height.mode = UI_SIZE_GROW;
    root.direction = UI_COLUMN;
    open_box(&root);
    ui_node n;
    for (size_t i = 0; i < f.count; i++) {
        SC_TSLICE_GET(ui_node, frame_arena, f.nodes, i, &n);
        if (i > 0) {
            close_to(frame_arena, f, i - 1, n.parent);
        }
        if (n.kind == UI_NODE_BOX) {
            open_box(&n);
        } else if (n.kind == UI_NODE_CUSTOM) {
            open_custom(&n, i);
        } else {
            add_text(&n);
        }
    }
    if (f.count > 0) {
        close_to(frame_arena, f, f.count - 1, UI_NO_PARENT);
    }
    Clay__CloseElement();
    Clay_RenderCommandArray cmds = Clay_EndLayout(0.0f);

    /* A frame that failed anywhere is never shown or saved: the last good
     * frame stays on screen. */
    bool ok = !clay_failed;
    if (ok) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);
        render(&cmds);
        set_clip(NULL);
        ok = !text_failed;
    }
    if (ok && shot_path != NULL) {
        ok = save_shot();
    }
    if (ok) {
        SDL_RenderPresent(renderer);
    }
    end_text_frame();
    return ok;
}
