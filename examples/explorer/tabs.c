#include "tabs.h"
#include "../../adapters/sc_ui_frame.h"

static uint32_t tab_width(uint32_t strip_w, size_t count) {
    if (count == 0) {
        return 0;
    }
    uint32_t room = count < TABS_MAX && strip_w > TAB_PLUS_WIDTH ? strip_w - TAB_PLUS_WIDTH
                    : count < TABS_MAX                         ? 0u
                                                               : strip_w;
    uint32_t w = (uint32_t)(room / count);
    return w < TAB_MAX_WIDTH ? w : TAB_MAX_WIDTH;
}

tab_span tab_span_at(uint32_t strip_w, size_t count, size_t i) {
    tab_span none = {0, 0};
    uint32_t w = tab_width(strip_w, count);
    if (i >= count || w == 0) {
        return none;
    }
    tab_span t = {(uint32_t)i * w, w};
    return t;
}

tab_span tab_close_span(tab_span t) {
    tab_span none = {t.x, 0};
    if (t.w < TAB_CLOSE_INSET + TAB_CLOSE_SIZE) {
        return none;
    }
    tab_span c = {t.x + TAB_CLOSE_INSET, TAB_CLOSE_SIZE};
    return c;
}

tab_span tab_plus_span(uint32_t strip_w, size_t count) {
    uint32_t end = (uint32_t)count * tab_width(strip_w, count);
    tab_span none = {end, 0};
    if (count >= TABS_MAX || end + TAB_PLUS_WIDTH > strip_w) {
        return none;
    }
    tab_span p = {end, TAB_PLUS_WIDTH};
    return p;
}

static bool inside(tab_span s, int64_t x) {
    return x >= (int64_t)s.x && x < (int64_t)s.x + (int64_t)s.w;
}

tab_hit tab_hit_at(uint32_t strip_w, size_t count, int64_t x) {
    tab_hit h = {TAB_HIT_NONE, 0};
    uint32_t w = tab_width(strip_w, count);
    if (x < 0 || x >= (int64_t)strip_w) {
        return h;
    }
    if (w > 0 && (uint64_t)x / w < count) {
        h.tab = (size_t)((uint64_t)x / w);
        h.kind = inside(tab_close_span(tab_span_at(strip_w, count, h.tab)), x) ? TAB_HIT_CLOSE
                                                                                : TAB_HIT_SELECT;
        return h;
    }
    if (inside(tab_plus_span(strip_w, count), x)) {
        h.kind = TAB_HIT_NEW;
    }
    return h;
}

size_t tab_after_close(size_t count, size_t selected, size_t closed) {
    if (count <= 1) {
        return 0;
    }
    if (selected < closed) {
        return selected;
    }
    if (selected > closed) {
        return selected - 1u;
    }
    return closed + 1u < count ? closed : closed - 1u;
}

tab_key tab_key_for(uint32_t key, uint16_t mods) {
    tab_key k = {TAB_KEY_NONE, 0};
    bool cmd = (mods & (UI_MOD_GUI | UI_MOD_CTRL)) != 0;
    if (!cmd || (mods & UI_MOD_ALT) != 0) {
        return k;
    }
    if (key >= 'A' && key <= 'Z') {
        key |= 0x20u;
    }
    bool shift = (mods & UI_MOD_SHIFT) != 0;
    if ((key == UI_KEY_RETURN || key == UI_KEY_KP_ENTER) && shift) {
        k.kind = TAB_KEY_LOAD_ALL;
    } else if (key == 't' && !shift) {
        k.kind = TAB_KEY_NEW;
    } else if (key == 'w' && !shift) {
        k.kind = TAB_KEY_CLOSE;
    } else if (key >= '1' && key <= '9' && !shift) {
        k.kind = TAB_KEY_SELECT;
        k.tab = (size_t)(key - '1');
    }
    return k;
}

static bool blank(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\r';
}

sc_str tab_title(const sc_arena *sa, sc_str sql, size_t max, bool *cut) {
    size_t start = 0, end = 0;
    bool found = false;
    *cut = false;
    for (size_t i = 0; i <= sql.len; i++) {
        uint8_t c = '\n';
        if (i < sql.len && !sc_slice_get(sa, sql, i, &c)) {
            break;
        }
        if (c == '\n') {
            if (found && end > start) {
                for (size_t j = i; j < sql.len; j++) { /* any text after this line */
                    uint8_t d = 0;
                    if (sc_slice_get(sa, sql, j, &d) && !blank(d) && d != '\n') {
                        *cut = true;
                        break;
                    }
                }
                break;
            }
            found = false;
            start = i + 1u;
            end = i + 1u;
        } else if (!blank(c)) {
            if (!found) {
                start = i;
            }
            found = true;
            end = i + 1u;
        }
    }
    if (!found || end <= start) {
        return sc_slice_sub(sa, sql, 0, 0);
    }
    sc_str line = sc_slice_sub(sa, sql, start, end - start);
    if (line.len > max) {
        *cut = true;
        line = sc_utf8_truncate(sa, line, max);
    }
    return line;
}

size_t tab_next_waiting(const sc_arena *a, sc_tslice waits, size_t count) {
    size_t best = SIZE_MAX;
    uint64_t low = UINT64_MAX;
    for (size_t i = 0; i < count; i++) {
        uint64_t w = 0;
        if (SC_TSLICE_GET(uint64_t, a, waits, i, &w) && w != 0 && w < low) {
            low = w;
            best = i;
        }
    }
    return best;
}
