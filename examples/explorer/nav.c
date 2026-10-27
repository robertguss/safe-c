#include "nav.h"

static nav_entry nav_query(void) {
    nav_entry e = {NAV_QUERY, 0, 0, NAV_CONTENT, 1, {false, 0, PAGE_COUNT_PENDING, 0}};
    return e;
}

static bool nav_put(nav_stack *n, size_t i, const nav_entry *e) {
    return SC_TSLICE_SET(nav_entry, n->arena, n->entries, i, e);
}

bool nav_init(nav_stack *n, sc_arena *a) {
    n->arena = a;
    n->entries = SC_TSLICE_ALLOC(nav_entry, a, NAV_MAX);
    if (sc_tslice_len(n->entries) != NAV_MAX) {
        return false;
    }
    nav_reset(n);
    return true;
}

void nav_reset(nav_stack *n) {
    nav_entry q = nav_query();
    n->count = nav_put(n, 0, &q) ? 1u : 0u;
    n->at = 0;
}

nav_entry nav_table(uint32_t schema, uint32_t oid, page_total total) {
    nav_entry e = {NAV_TABLE, schema, oid, NAV_CONTENT, 1, total};
    return e;
}

bool nav_push(nav_stack *n, nav_entry e) {
    if (n->count == 0) {
        return false;
    }
    if (n->at + 1u < NAV_MAX) {
        n->count = n->at + 2u;
        n->at++;
        return nav_put(n, n->at, &e);
    }
    for (size_t i = 0; i + 1u < NAV_MAX; i++) {
        nav_entry m;
        if (!SC_TSLICE_GET(nav_entry, n->arena, n->entries, i + 1u, &m) || !nav_put(n, i, &m)) {
            return false;
        }
    }
    n->count = NAV_MAX;
    n->at = NAV_MAX - 1u;
    return nav_put(n, n->at, &e);
}

bool nav_can_back(const nav_stack *n) {
    return n->at > 0;
}

bool nav_can_forward(const nav_stack *n) {
    return n->at + 1u < n->count;
}

bool nav_back(nav_stack *n) {
    if (!nav_can_back(n)) {
        return false;
    }
    n->at--;
    return true;
}

bool nav_forward(nav_stack *n) {
    if (!nav_can_forward(n)) {
        return false;
    }
    n->at++;
    return true;
}

bool nav_current(const nav_stack *n, nav_entry *out) {
    return n->count > 0 && SC_TSLICE_GET(nav_entry, n->arena, n->entries, n->at, out);
}

bool nav_set_current(nav_stack *n, nav_entry e) {
    return n->count > 0 && nav_put(n, n->at, &e);
}

bool nav_can_move(const nav_stack *n, nav_move m, nav_run r) {
    nav_entry e;
    if (!nav_current(n, &e)) {
        return false;
    }
    bool table = e.kind == NAV_TABLE;
    bool content = table && e.view == NAV_CONTENT && !r.busy;
    switch (m) {
    case NAV_MOVE_BACK:
        return nav_can_back(n) && (table || !r.busy);
    case NAV_MOVE_FORWARD:
        return nav_can_forward(n) && (table || !r.busy);
    case NAV_MOVE_SWITCH:
        return table;
    case NAV_MOVE_PREV_PAGE:
        return content && page_can_prev(e.page);
    case NAV_MOVE_NEXT_PAGE:
        return content && page_can_next(r.ended, r.rows, r.capped);
    default:
        return false;
    }
}

bool nav_apply(nav_stack *n, nav_move m, nav_run r) {
    nav_entry e;
    if (!nav_can_move(n, m, r) || !nav_current(n, &e)) {
        return false;
    }
    if (m == NAV_MOVE_BACK) {
        return nav_back(n);
    }
    if (m == NAV_MOVE_FORWARD) {
        return nav_forward(n);
    }
    if (m == NAV_MOVE_SWITCH) {
        e.view = e.view == NAV_CONTENT ? NAV_STRUCTURE : NAV_CONTENT;
    } else {
        e.page = m == NAV_MOVE_PREV_PAGE ? e.page - 1u : e.page + 1u;
    }
    return nav_set_current(n, e);
}

nav_arrival nav_arrive(nav_entry e) {
    if (e.kind == NAV_QUERY) {
        return NAV_SHOW_QUERY;
    }
    return e.view == NAV_STRUCTURE ? NAV_SHOW_STRUCTURE : NAV_SHOW_CONTENT;
}

nav_key nav_key_for(uint32_t key, uint16_t mods, uint8_t platform) {
    if (!frame_cmd(mods, platform) || (mods & UI_MOD_ALT) == 0 || (mods & UI_MOD_SHIFT) != 0) {
        return NAV_KEY_NONE;
    }
    if (key == UI_KEY_LEFT) {
        return NAV_KEY_BACK;
    }
    if (key == UI_KEY_RIGHT) {
        return NAV_KEY_FORWARD;
    }
    return key == 't' || key == 'T' ? NAV_KEY_SWITCH : NAV_KEY_NONE;
}
