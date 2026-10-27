#include "frame.h"

static bool text_live(const sc_arena *a, sc_str s) {
    return s.len == 0 || sc_slice_sub(a, s, 0, s.len).len == s.len;
}

static bool text_ok(const sc_arena *a, sc_str s, uint8_t font, uint16_t size) {
    return font < UI_FONT_COUNT && size > 0 && text_live(a, s);
}

static bool draw_ok(const sc_arena *a, ui_draw_item d) {
    if (d.kind == UI_DRAW_TEXT) {
        return text_ok(a, d.text, d.font, d.font_size);
    }
    return d.kind == UI_DRAW_LINE || (d.kind == UI_DRAW_RECT && d.w >= 0 && d.h >= 0);
}

static bool fail(frame_builder *b) {
    b->failed = true;
    return false;
}

bool frame_begin(frame_builder *b, sc_arena *a, size_t cap, size_t draw_cap) {
    b->arena = a;
    b->count = 0;
    b->draw_count = 0;
    b->draw_mark = 0;
    b->open = UI_NO_PARENT;
    b->depth = 0;
    b->failed = false;
    b->nodes = SC_TSLICE_ALLOC(ui_node, a, cap);
    b->draws = SC_TSLICE_ALLOC(ui_draw_item, a, draw_cap);
    if (cap == 0 || cap >= UI_NO_PARENT || draw_cap >= UI_NO_PARENT ||
        sc_tslice_len(b->nodes) != cap || sc_tslice_len(b->draws) != draw_cap) {
        return fail(b);
    }
    return true;
}

static bool append(frame_builder *b, ui_node n) {
    if (b->failed) {
        return false;
    }
    if (b->count >= sc_tslice_len(b->nodes)) {
        return fail(b);
    }
    if (n.kind == UI_NODE_TEXT && !text_ok(b->arena, n.text, n.font, n.font_size)) {
        return fail(b);
    }
    n.parent = b->open;
    if (!SC_TSLICE_SET(ui_node, b->arena, b->nodes, b->count, &n)) {
        return fail(b);
    }
    b->count++;
    return true;
}

bool frame_open(frame_builder *b, ui_node n) {
    n.kind = UI_NODE_BOX;
    if (!append(b, n)) {
        return false;
    }
    b->open = (uint32_t)(b->count - 1);
    b->depth++;
    return true;
}

bool frame_leaf(frame_builder *b, ui_node n) {
    if (n.kind == UI_NODE_CUSTOM) {
        return fail(b);
    }
    return append(b, n);
}

bool frame_text(frame_builder *b, sc_str text, uint8_t font, uint16_t size, ui_color color) {
    ui_node n = frame_box();
    n.kind = UI_NODE_TEXT;
    n.text = text;
    n.font = font;
    n.font_size = size;
    n.text_color = color;
    return append(b, n);
}

bool frame_draw(frame_builder *b, ui_draw_item d) {
    if (b->failed) {
        return false;
    }
    if (b->draw_count >= sc_tslice_len(b->draws) || !draw_ok(b->arena, d) ||
        !SC_TSLICE_SET(ui_draw_item, b->arena, b->draws, b->draw_count, &d)) {
        return fail(b);
    }
    b->draw_count++;
    return true;
}

bool frame_custom(frame_builder *b, ui_node n) {
    n.kind = UI_NODE_CUSTOM;
    n.draw_first = (uint32_t)b->draw_mark;
    n.draw_count = (uint32_t)(b->draw_count - b->draw_mark);
    if (!append(b, n)) {
        return false;
    }
    b->draw_mark = b->draw_count;
    return true;
}

bool frame_close(frame_builder *b) {
    ui_node n;
    if (b->failed) {
        return false;
    }
    if (b->depth == 0 || !SC_TSLICE_GET(ui_node, b->arena, b->nodes, b->open, &n)) {
        return fail(b);
    }
    b->open = n.parent;
    b->depth--;
    return true;
}

bool frame_end(frame_builder *b, ui_frame *out) {
    if (b->failed || b->depth != 0 || b->draw_mark != b->draw_count) {
        return fail(b);
    }
    out->nodes = b->nodes;
    out->count = b->count;
    out->draws = b->draws;
    out->draw_count = b->draw_count;
    return true;
}

ui_node frame_box(void) {
    ui_node n = {0};
    n.parent = UI_NO_PARENT;
    return n;
}

/* `p` is node i's parent: it must be node i - 1 or one of its ancestors,
 * because a depth-first list leaves only that chain of boxes open. Parents
 * before i were checked to come earlier, so the walk ends. */
static bool open_at(const sc_arena *a, ui_frame f, size_t i, uint32_t p) {
    size_t c = i - 1;
    ui_node n;
    while (c != p) {
        if (!SC_TSLICE_GET(ui_node, a, f.nodes, c, &n) || n.parent == UI_NO_PARENT) {
            return false;
        }
        c = n.parent;
    }
    return SC_TSLICE_GET(ui_node, a, f.nodes, p, &n) && n.kind == UI_NODE_BOX;
}

static bool draws_ok(const sc_arena *a, ui_frame f, ui_node n) {
    ui_draw_item d;
    if (n.draw_count > f.draw_count || n.draw_first > f.draw_count - n.draw_count) {
        return false;
    }
    for (size_t k = n.draw_first; k < (size_t)n.draw_first + n.draw_count; k++) {
        if (!SC_TSLICE_GET(ui_draw_item, a, f.draws, k, &d) || !draw_ok(a, d)) {
            return false;
        }
    }
    return true;
}

/* Whether a node before `i` already carries `tag`. Only tagged nodes pay for
 * this scan, and a frame tags a few dozen. */
static bool tag_taken(const sc_arena *a, ui_frame f, size_t i, uint32_t tag) {
    ui_node n;
    for (size_t k = 0; k < i; k++) {
        if (!SC_TSLICE_GET(ui_node, a, f.nodes, k, &n) || n.tag == tag) {
            return true;
        }
    }
    return false;
}

bool ui_frame_valid(const sc_arena *a, ui_frame f) {
    ui_node n;
    if (f.count > sc_tslice_len(f.nodes) || f.draw_count > sc_tslice_len(f.draws)) {
        return false;
    }
    for (size_t i = 0; i < f.count; i++) {
        if (!SC_TSLICE_GET(ui_node, a, f.nodes, i, &n) || n.kind > UI_NODE_CUSTOM) {
            return false;
        }
        if (n.kind == UI_NODE_TEXT && !text_ok(a, n.text, n.font, n.font_size)) {
            return false;
        }
        if (n.kind == UI_NODE_CUSTOM && !draws_ok(a, f, n)) {
            return false;
        }
        if (n.tag != 0 && (n.kind == UI_NODE_TEXT || tag_taken(a, f, i, n.tag))) {
            return false;
        }
        if (n.parent != UI_NO_PARENT && (n.parent >= i || !open_at(a, f, i, n.parent))) {
            return false;
        }
    }
    return true;
}

static bool holds(ui_rect r, int32_t x, int32_t y) {
    return (int64_t)x >= r.x && (int64_t)x < (int64_t)r.x + r.w && (int64_t)y >= r.y &&
           (int64_t)y < (int64_t)r.y + r.h;
}

uint32_t frame_hit(const ui_layout *l, int32_t x, int32_t y) {
    ui_rect r;
    uint32_t tag = 0;
    for (size_t i = 0; i < l->count; i++) {
        if (SC_TSLICE_GET(ui_rect, l->arena, l->rects, i, &r) && r.tag != 0 && holds(r, x, y)) {
            tag = r.tag;
        }
    }
    return tag;
}

bool frame_rect(const ui_layout *l, uint32_t tag, ui_rect *out) {
    ui_rect r;
    for (size_t i = 0; tag != 0 && i < l->count; i++) {
        if (SC_TSLICE_GET(ui_rect, l->arena, l->rects, i, &r) && r.tag == tag) {
            *out = r;
            return true;
        }
    }
    return false;
}

bool frame_mac_ctrl(uint16_t mods, uint8_t platform) {
    return platform == UI_PLATFORM_MAC && (mods & UI_MOD_CTRL) != 0;
}

bool frame_cmd(uint16_t mods, uint8_t platform) {
    return (mods & UI_MOD_CMD) != 0 && !frame_mac_ctrl(mods, platform);
}
