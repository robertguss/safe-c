/* The UI adapter's input half (subset rule R0): event translation, text input
 * and composition, the clipboard, the mouse cursor and the scripted events
 * headless scenes push. Drawing is in ui.c; ui_sdl.h is what they share. */
#include "ui_sdl.h"
#include <stdlib.h>
#include <string.h>

static Uint32 wake_type;
/* System cursors, made on first use. */
static SDL_Cursor *cursors[3];
/* Text the scripted events point at: SDL copies none of it, so it lives
 * until ui_close. */
#define SCRIPT_MAX 64
static char *script[SCRIPT_MAX];
static size_t script_count;

void ui_input_open(void) {
    wake_type = SDL_RegisterEvents(1);
    SDL_StartTextInput(ui_sdl_window());
}

void ui_input_close(void) {
    for (size_t i = 0; i < 3; i++) {
        SDL_DestroyCursor(cursors[i]);
        cursors[i] = NULL;
    }
    SDL_FlushEvents(SDL_EVENT_TEXT_EDITING, SDL_EVENT_TEXT_INPUT);
    for (size_t i = 0; i < script_count; i++) {
        free(script[i]);
        script[i] = NULL;
    }
    script_count = 0;
    wake_type = 0;
}

/* The byte offset of character `chars` in the `n` bytes at `p`, stepping over
 * UTF-8 continuation bytes; `n` when the text is shorter. */
static size_t byte_offset(const char *p, size_t n, int32_t chars) {
    size_t i = 0;
    for (int32_t c = 0; c < chars && i < n; c++) {
        i++;
        while (i < n && ((unsigned char)p[i] & 0xc0) == 0x80) {
            i++;
        }
    }
    return i;
}

/* --- events --------------------------------------------------------------- */

uint8_t ui_platform(void) {
#ifdef __APPLE__
    return UI_PLATFORM_MAC;
#else
    return UI_PLATFORM_OTHER;
#endif
}

static uint16_t mods_of(SDL_Keymod m) {
    SDL_Keymod cmd = ui_platform() == UI_PLATFORM_MAC ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    return (uint16_t)(((m & SDL_KMOD_SHIFT) ? UI_MOD_SHIFT : 0) |
                      ((m & SDL_KMOD_CTRL) ? UI_MOD_CTRL : 0) |
                      ((m & SDL_KMOD_ALT) ? UI_MOD_ALT : 0) |
                      ((m & cmd) ? UI_MOD_CMD : 0));
}

static uint8_t button_of(Uint8 b) {
    return b == SDL_BUTTON_LEFT     ? UI_BUTTON_LEFT
           : b == SDL_BUTTON_MIDDLE ? UI_BUTTON_MIDDLE
           : b == SDL_BUTTON_RIGHT  ? UI_BUTTON_RIGHT
                                    : 0;
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
        out->repeat = ev->key.repeat;
        return true;
    case SDL_EVENT_TEXT_INPUT:
        out->kind = UI_EVENT_TEXT;
        out->text = sc_str_from_lit(events, (const uint8_t *)ev->text.text, strlen(ev->text.text));
        return true;
    case SDL_EVENT_TEXT_EDITING: {
        const char *p = ev->edit.text == NULL ? "" : ev->edit.text;
        size_t n = strlen(p);
        out->kind = UI_EVENT_COMPOSE;
        out->text = sc_str_from_lit(events, (const uint8_t *)p, n);
        if (out->text.len != n) {
            return false; /* no room in `events` */
        }
        size_t start = ev->edit.start < 0 ? n : byte_offset(p, n, ev->edit.start);
        size_t end = ev->edit.length <= 0 ? start
                                          : start + byte_offset(p + start, n - start, ev->edit.length);
        out->compose_start = start;
        out->compose_len = end - start;
        return true;
    }
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
        out->button = button_of(ev->button.button);
        out->clicks = ev->button.clicks;
        out->mods = mods_of(SDL_GetModState());
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        out->kind = UI_EVENT_WHEEL;
        out->wheel_x = (int32_t)SDL_roundf(ev->wheel.x * 16.0f);
        out->wheel_y = (int32_t)SDL_roundf(ev->wheel.y * 16.0f);
        out->x = (int32_t)ev->wheel.mouse_x;
        out->y = (int32_t)ev->wheel.mouse_y;
        return true;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        ui_update_density();
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


uint8_t ui_clipboard_set(const sc_arena *a, sc_str text, size_t max) {
    if (!ui_live_str(a, text)) {
        return UI_CLIP_FAILED;
    }
    if (text.len > max) {
        return UI_CLIP_TOO_BIG;
    }
    if (text.len > 0 && memchr(text.ptr, 0, text.len) != NULL) {
        return UI_CLIP_NUL;
    }
    char *c = malloc(text.len + 1);
    if (c == NULL) {
        return UI_CLIP_FAILED;
    }
    if (text.len > 0) {
        memcpy(c, text.ptr, text.len);
    }
    c[text.len] = '\0';
    bool ok = SDL_SetClipboardText(c);
    free(c);
    return ok ? UI_CLIP_OK : UI_CLIP_FAILED;
}

bool ui_text_input(uint8_t mode) {
    SDL_Window *w = ui_sdl_window();
    if (w == NULL || mode > UI_TEXT_INPUT_PASSWORD) {
        return false;
    }
    SDL_PropertiesID props = SDL_CreateProperties();
    bool ok = props != 0 &&
              SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER,
                                    mode == UI_TEXT_INPUT_PASSWORD ? SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN
                                                                   : SDL_TEXTINPUT_TYPE_TEXT) &&
              SDL_StopTextInput(w) && SDL_StartTextInputWithProperties(w, props);
    SDL_DestroyProperties(props);
    return ok;
}

bool ui_text_area(int32_t x, int32_t y, int32_t w, int32_t h, int32_t cursor) {
    SDL_Rect r = {x, y, w, h};
    SDL_Window *win = ui_sdl_window();
    return win != NULL && SDL_SetTextInputArea(win, &r, cursor);
}

bool ui_cursor(uint8_t shape) {
    static const SDL_SystemCursor ids[3] = {SDL_SYSTEM_CURSOR_DEFAULT, SDL_SYSTEM_CURSOR_TEXT,
                                            SDL_SYSTEM_CURSOR_POINTER};
    if (shape > UI_CURSOR_POINTER) {
        return false;
    }
    if (cursors[shape] == NULL) {
        cursors[shape] = SDL_CreateSystemCursor(ids[shape]);
    }
    return cursors[shape] != NULL && SDL_SetCursor(cursors[shape]);
}

/* A NUL-terminated copy of `text` kept for a scripted event, or NULL. */
static char *script_text(const sc_arena *a, sc_str text) {
    if (script_count == SCRIPT_MAX || !ui_live_str(a, text) ||
        (text.len > 0 && memchr(text.ptr, 0, text.len) != NULL)) {
        return NULL;
    }
    char *c = malloc(text.len + 1);
    if (c != NULL) {
        if (text.len > 0) {
            memcpy(c, text.ptr, text.len);
        }
        c[text.len] = '\0';
        script[script_count++] = c;
    }
    return c;
}

static bool push_script(SDL_Event *ev) {
    SDL_Window *w = ui_sdl_window();
    ev->common.timestamp = SDL_GetTicksNS();
    if (w != NULL) {
        ev->text.windowID = SDL_GetWindowID(w);
    }
    return SDL_PushEvent(ev);
}

bool ui_script_compose(const sc_arena *a, sc_str text, int32_t start, int32_t length) {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_TEXT_EDITING;
    ev.edit.text = script_text(a, text);
    ev.edit.start = start;
    ev.edit.length = length;
    return ev.edit.text != NULL && push_script(&ev);
}

bool ui_script_text(const sc_arena *a, sc_str text) {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_TEXT_INPUT;
    ev.text.text = script_text(a, text);
    return ev.text.text != NULL && push_script(&ev);
}
