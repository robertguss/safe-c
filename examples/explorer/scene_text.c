#include "scene_text.h"

#define SCRIPT_DRAIN_MS 100u

static bool text_fail(const sc_arena *a, sc_str why) {
    ui_log(a, why);
    return false;
}

static bool compose_right(const sc_arena *a, const ui_event *ev, uint32_t n) {
    if (n == 0) {
        return ev->text.len == 3u && SC_STR_EQ_LIT(a, ev->text, "に") && ev->compose_start == 3u &&
               ev->compose_len == 0;
    }
    bool word = ev->text.len == 12u && SC_STR_EQ_LIT(a, ev->text, "にほんご");
    if (n == 1u) {
        return word && ev->compose_start == 3u && ev->compose_len == 6u;
    }
    return n == 2u && word && ev->compose_start == 12u && ev->compose_len == 0;
}

static bool drain_password(sc_arena *events) {
    bool pushed = ui_script_compose(events, SC_STR_LIT(events, "ひみつ"), 1, 0) &&
                  ui_script_text(events, SC_STR_LIT(events, "x"));
    uint32_t composes = 0, texts = 0;
    ui_event ev;
    while (pushed && ui_wait(events, SCRIPT_DRAIN_MS, &ev)) {
        composes += ev.kind == UI_EVENT_COMPOSE ? 1u : 0;
        texts += ev.kind == UI_EVENT_TEXT && SC_STR_EQ_LIT(events, ev.text, "x") ? 1u : 0;
    }
    return pushed && composes == 0 && texts == 1u;
}

static bool drain_plain(sc_arena *events) {
    bool pushed = ui_script_compose(events, SC_STR_LIT(events, "に"), 1, 0);
    uint32_t composes = 0;
    bool right = true;
    ui_event ev;
    while (pushed && ui_wait(events, SCRIPT_DRAIN_MS, &ev)) {
        if (ev.kind == UI_EVENT_COMPOSE) {
            right = right && compose_right(events, &ev, 0);
            composes++;
        }
    }
    return pushed && right && composes == 1u;
}

bool scene_text_compose(sc_arena *events) {
    if (!sc_arena_reset(events)) {
        return false;
    }
    bool pushed = ui_text_area(10, 20, 200, 18, 0) &&
                  ui_script_compose(events, SC_STR_LIT(events, "に"), 1, 0) &&
                  ui_script_compose(events, SC_STR_LIT(events, "にほんご"), 1, 2) &&
                  ui_script_compose(events, SC_STR_LIT(events, "にほんご"), -1, -1) &&
                  ui_script_text(events, SC_STR_LIT(events, "日本"));
    if (!pushed) {
        return text_fail(events, SC_STR_LIT(events, "text-compose: scripted events refused"));
    }
    uint32_t composes = 0, texts = 0;
    bool right = true;
    ui_event ev;
    while (ui_wait(events, SCRIPT_DRAIN_MS, &ev)) {
        if (ev.kind == UI_EVENT_COMPOSE) {
            right = right && texts == 0 && compose_right(events, &ev, composes);
            composes++;
        }
        if (ev.kind == UI_EVENT_TEXT) {
            right = right && composes == 3u && texts == 0 && ev.text.len == 6u &&
                    SC_STR_EQ_LIT(events, ev.text, "日本");
            texts++;
        }
    }
    if (!right || composes != 3u || texts != 1u) {
        return text_fail(events, SC_STR_LIT(events, "text-compose: wrong compose or text events"));
    }
    if (!ui_text_input(UI_TEXT_INPUT_PASSWORD) || !drain_password(events)) {
        return text_fail(events, SC_STR_LIT(events, "text-compose: a composition reached a password field"));
    }
    if (!ui_text_input(UI_TEXT_INPUT_PLAIN) || !drain_plain(events)) {
        return text_fail(events, SC_STR_LIT(events, "text-compose: plain input lost its composition"));
    }
    return true;
}

bool scene_text_clipboard(sc_arena *clip, size_t cap) {
    if (!sc_arena_reset(clip)) {
        return false;
    }
    sc_str want = SC_STR_LIT(clip, "select 1");
    sc_str got = {NULL, 0, clip->epoch};
    if (ui_clipboard_set(clip, want, cap) != UI_CLIP_OK || ui_clipboard(clip, cap, &got) != UI_CLIP_OK ||
        got.len != 8u || !sc_str_eq(clip, got, want)) {
        return text_fail(clip, SC_STR_LIT(clip, "text-clipboard: select 1 did not read back"));
    }
    sc_str nul = SC_STR_LIT(clip, "select\0 2");
    sc_str big = SC_STR_LIT(clip, "select 3");
    bool refused = nul.len == 9u && ui_clipboard_set(clip, nul, cap) == UI_CLIP_NUL &&
                   ui_clipboard_set(clip, big, 7u) == UI_CLIP_TOO_BIG;
    sc_str kept = {NULL, 0, clip->epoch};
    if (!refused || ui_clipboard(clip, cap, &kept) != UI_CLIP_OK || !sc_str_eq(clip, kept, want)) {
        return text_fail(clip, SC_STR_LIT(clip, "text-clipboard: a refused write changed the clipboard"));
    }
    sc_str empty = {NULL, 0, clip->epoch};
    sc_str none = {NULL, 0, clip->epoch};
    if (ui_clipboard_set(clip, empty, cap) != UI_CLIP_OK || ui_clipboard(clip, cap, &none) != UI_CLIP_EMPTY) {
        return text_fail(clip, SC_STR_LIT(clip, "text-clipboard: an empty write did not clear the clipboard"));
    }
    return true;
}
