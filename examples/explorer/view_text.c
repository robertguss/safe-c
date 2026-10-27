#include "view_text.h"
#include "../../adapters/sc_ui.h"

ui_draw_item view_item(uint8_t kind, int32_t x, int32_t y, int32_t w, int32_t h, ui_color c) {
    ui_draw_item d = {0};
    d.kind = kind;
    d.x = x;
    d.y = y;
    d.w = w;
    d.h = h;
    d.color = c;
    return d;
}

bool view_draw_text_in(frame_builder *b, sc_str t, uint8_t font, uint16_t size, int32_t x,
                       int32_t y, int32_t w, int32_t h, ui_color c) {
    if (t.len == 0) {
        return true;
    }
    ui_draw_item d = view_item(UI_DRAW_TEXT, x, y, w, h, c);
    d.text = t;
    d.font = font;
    d.font_size = size;
    return frame_draw(b, d);
}

uint32_t view_text_width(const sc_arena *a, sc_str t, uint8_t font, uint16_t size) {
    uint32_t w = 0, h = 0;
    if (t.len == 0 || !ui_text_size(a, t, font, size, &w, &h)) {
        return 0;
    }
    return w;
}

sc_str view_fit(frame_builder *b, sc_str s, uint8_t font, uint16_t size, uint32_t max_w) {
    sc_arena *fa = b->arena;
    if (s.len == 0 || view_text_width(fa, s, font, size) <= max_w) {
        return s;
    }
    uint32_t ell = view_text_width(fa, SC_STR_LIT(fa, "\xE2\x80\xA6"), font, size);
    size_t lo = 0, hi = s.len - 1; /* the longest prefix that fits with the ellipsis */
    while (lo < hi) {
        size_t mid = lo + (hi - lo + 1u) / 2u;
        if (view_text_width(fa, sc_slice_sub(fa, s, 0, mid), font, size) + ell <= max_w) {
            lo = mid;
        } else {
            hi = mid - 1u;
        }
    }
    return text_cut(fa, fa, s, lo, &b->failed);
}
