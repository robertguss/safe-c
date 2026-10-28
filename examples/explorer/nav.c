#include "nav.h"

static nav_entry nav_query(void) {
    nav_entry e = {0};
    e.kind = NAV_QUERY;
    e.page = 1;
    e.total.size = PAGE_SIZE;
    return e;
}

static bool nav_put(nav_stack *n, size_t i, const nav_entry *e) {
    return SC_TSLICE_SET(nav_entry, n->arena, n->entries, i, e);
}

bool nav_init(nav_stack *n, sc_arena *a) {
    nav_stack blank = {0};
    *n = blank;
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
    nav_entry e = {0};
    e.kind = NAV_TABLE;
    e.schema = schema;
    e.oid = oid;
    e.page = 1;
    e.total = total;
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

bool nav_open(nav_stack *n, const catalog *c, uint32_t schema, uint32_t oid, page_total total) {
    nav_entry e = nav_table(schema, oid, total);
    if (c->engine == SC_DB_POSTGRESQL) return nav_push(n, e);
    catalog_schema s;
    catalog_relation r;
    if (!catalog_relation_of(c, schema, oid, &s, &r) || s.name.len > NAV_NAME_BYTES ||
        r.name.len > NAV_NAME_BYTES) return false;
    if (n->names.len == 0) n->names = sc_arena_alloc(n->arena, NAV_NAMES_BYTES);
    if (n->names.len != NAV_NAMES_BYTES) return false;
    size_t slot = 0;
    for (; slot < NAV_MAX; slot++) {
        bool used = false;
        size_t first = n->at + 1u == NAV_MAX ? 1u : 0u;
        for (size_t i = first; !used && i <= n->at; i++) {
            nav_entry old;
            used = SC_TSLICE_GET(nav_entry, n->arena, n->entries, i, &old) &&
                   old.gen != 0 && old.name_slot == slot;
        }
        if (!used) break;
    }
    if (slot == NAV_MAX) return false;
    sc_buf schema_buf = sc_buf_new(sc_slice_sub(n->arena, n->names, slot * NAV_NAME_BYTES * 2u, NAV_NAME_BYTES));
    sc_buf name_buf = sc_buf_new(sc_slice_sub(n->arena, n->names, (slot * 2u + 1u) * NAV_NAME_BYTES, NAV_NAME_BYTES));
    if (!sc_buf_str(n->arena, &schema_buf, c->arena, s.name) ||
        !sc_buf_str(n->arena, &name_buf, c->arena, r.name)) return false;
    e.schema_name = sc_buf_view(n->arena, schema_buf);
    e.name = sc_buf_view(n->arena, name_buf);
    e.relation_kind = r.kind;
    e.gen = c->gen;
    e.name_slot = slot;
    return nav_push(n, e);
}

static bool nav_same(const sc_arena *a, sc_str x, const sc_arena *b, sc_str y) {
    if (x.len != y.len) return false;
    for (size_t i = 0; i < x.len; i++) {
        uint8_t one = 0, two = 0;
        if (!sc_slice_get(a, x, i, &one) || !sc_slice_get(b, y, i, &two) || one != two) return false;
    }
    return true;
}

void nav_resolve(nav_stack *n, catalog *c) {
    for (size_t i = 0; i < n->count; i++) {
        nav_entry e;
        if (!SC_TSLICE_GET(nav_entry, n->arena, n->entries, i, &e) || e.gen == 0 || e.gen == c->gen)
            continue;
        e.schema = e.oid = 0;
        e.gone = false;
        bool schema_found = false;
        catalog_schema s;
        for (size_t j = 0; !schema_found && catalog_schema_at(c, j, &s); j++) {
            if (!nav_same(n->arena, e.schema_name, c->arena, s.name)) continue;
            schema_found = true;
            e.schema = s.oid;
            if (s.load == CATALOG_LOAD_NONE) (void)catalog_expand(c, j);
            if (s.load != CATALOG_LOAD_DONE) break;
            bool found = false;
            catalog_relation r;
            for (size_t k = 0; !found && catalog_relation_at(c, &s, k, &r); k++) {
                found = r.kind == e.relation_kind && nav_same(n->arena, e.name, c->arena, r.name);
                if (found) e.oid = r.oid;
            }
            e.gen = c->gen;
            e.gone = !found;
        }
        if (!schema_found && c->schemas_load == CATALOG_LOAD_DONE) {
            e.gone = true;
            e.gen = c->gen;
        }
        (void)nav_put(n, i, &e);
    }
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
        return content && page_can_next(r.ended, r.rows, r.capped, e.total.size);
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
