#include "tabs.h"

uint32_t tab_width(uint32_t strip_w, size_t count) {
    if (count == 0) {
        return 0;
    }
    uint32_t room = count < TABS_MAX && strip_w > TAB_PLUS_WIDTH ? strip_w - TAB_PLUS_WIDTH
                    : count < TABS_MAX                         ? 0u
                                                               : strip_w;
    uint32_t w = (uint32_t)(room / count);
    return w < TAB_MAX_WIDTH ? w : TAB_MAX_WIDTH;
}

tab_hit tab_hit_at(const ui_layout *l, size_t count, int32_t x, int32_t y, uint8_t button) {
    tab_hit h = {TAB_HIT_NONE, 0};
    uint32_t tag = button == UI_BUTTON_LEFT ? frame_hit(l, x, y) : 0;
    if (tag == TAB_TAG_NEW) {
        h.kind = TAB_HIT_NEW;
    } else if (tag >= TAB_TAG_SELECT && (size_t)(tag - TAB_TAG_SELECT) < count) {
        h.kind = TAB_HIT_SELECT;
        h.tab = (size_t)(tag - TAB_TAG_SELECT);
    } else if (tag >= TAB_TAG_CLOSE && (size_t)(tag - TAB_TAG_CLOSE) < count) {
        h.kind = TAB_HIT_CLOSE;
        h.tab = (size_t)(tag - TAB_TAG_CLOSE);
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

tab_key tab_key_for(uint32_t key, uint16_t mods, uint8_t platform) {
    tab_key k = {TAB_KEY_NONE, 0};
    bool mac_ctrl = platform == UI_PLATFORM_MAC && (mods & UI_MOD_CTRL) != 0;
    if ((mods & UI_MOD_CMD) == 0 || (mods & UI_MOD_ALT) != 0 || mac_ctrl) {
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

tab_run_check tab_can_run(bool usable, size_t free, size_t reclaim) {
    if (!usable) {
        return TAB_RUN_NOT_CONNECTED;
    }
    return free >= 2u || reclaim >= 2u - free ? TAB_RUN_OK : TAB_RUN_POOL;
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

/* ---- The tab book ---- */

static uint64_t wait_at(const tab_book *b, size_t i) {
    uint64_t w = 0;
    return SC_TSLICE_GET(uint64_t, b->arena, b->waits, i, &w) ? w : 0;
}

static void set_wait_at(tab_book *b, size_t i, uint64_t w) {
    (void)SC_TSLICE_SET(uint64_t, b->arena, b->waits, i, &w);
}

bool tab_book_init(tab_book *b, sc_arena *a) {
    b->count = 1;
    b->sel = 0;
    b->live = TAB_NONE;
    b->seq = 0;
    b->arena = a;
    b->waits = SC_TSLICE_ALLOC(uint64_t, a, TABS_MAX);
    if (sc_tslice_len(b->waits) != TABS_MAX) {
        return false;
    }
    for (size_t i = 0; i < TABS_MAX; i++) {
        set_wait_at(b, i, 0);
    }
    return true;
}

tab_run_kind tab_book_run(tab_book *b, size_t i, bool active) {
    if (b->live == i && active) {
        return TAB_RUN_BUSY;
    }
    if (b->live != TAB_NONE || active) {
        if (wait_at(b, i) == 0) {
            set_wait_at(b, i, ++b->seq);
        }
        return TAB_RUN_QUEUED;
    }
    return TAB_RUN_START;
}

void tab_book_started(tab_book *b, size_t i) {
    set_wait_at(b, i, 0);
    b->live = i;
}

void tab_book_refused(tab_book *b, size_t i) {
    set_wait_at(b, i, 0);
}

bool tab_book_waiting(const tab_book *b, size_t i) {
    return i < b->count && wait_at(b, i) != 0;
}

size_t tab_book_finished(tab_book *b, bool active) {
    if (b->live == TAB_NONE || active) {
        return TAB_NONE;
    }
    size_t done = b->live;
    b->live = TAB_NONE;
    return done;
}

size_t tab_book_next(const tab_book *b, bool active) {
    if (b->live != TAB_NONE || active) {
        return TAB_NONE;
    }
    size_t next = tab_next_waiting(b->arena, b->waits, b->count);
    return next == SIZE_MAX ? TAB_NONE : next;
}

bool tab_book_open(tab_book *b) {
    if (b->count >= TABS_MAX) {
        return false;
    }
    set_wait_at(b, b->count, 0);
    b->sel = b->count;
    b->count++;
    return true;
}

bool tab_book_close(tab_book *b, size_t i) {
    if (i >= b->count) {
        return false;
    }
    bool was_live = b->live == i;
    if (was_live) {
        b->live = TAB_NONE;
    } else if (b->live != TAB_NONE && b->live > i) {
        b->live--;
    }
    for (size_t k = i; k + 1u < b->count; k++) {
        set_wait_at(b, k, wait_at(b, k + 1u));
    }
    set_wait_at(b, b->count - 1u, 0);
    b->sel = tab_after_close(b->count, b->sel, i);
    b->count--;
    return was_live;
}
