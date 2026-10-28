#include "tree.h"

void tree_init(tree_state *t) {
    tree_state z = {0};
    *t = z;
    t->mode = TREE_TABLES;
    t->g.row_h = THEME_SIDEBAR_ROW_HEIGHT;
}

void tree_set_mode(tree_state *t, tree_mode m) {
    if (t->mode == m) {
        return;
    }
    t->mode = m;
    t->selected = false;
    t->g.scroll_y = 0;
}

static bool tree_failed(const catalog_schema *s) {
    return s->load == CATALOG_LOAD_FAILED && s->load_left == 0;
}

bool tree_matches(const sc_arena *a, sc_str name, const sc_arena *fa, sc_str filter) {
    if (filter.len == 0) return true;
    if (filter.len > name.len) return false;
    for (size_t i = 0; i <= name.len - filter.len; i++) {
        bool same = true;
        for (size_t j = 0; same && j < filter.len; j++) {
            uint8_t x = 0, y = 0;
            same = sc_slice_get(a, name, i + j, &x) && sc_slice_get(fa, filter, j, &y);
            if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
            if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
            same = same && x == y;
        }
        if (same) return true;
    }
    return false;
}

size_t tree_unloaded(const catalog *c) {
    catalog_schema s;
    size_t n = 0;
    for (size_t i = 0; catalog_schema_at(c, i, &s); i++) if (s.load != CATALOG_LOAD_DONE) n++;
    return n;
}

static bool tree_child_matches(const catalog *c, const tree_state *t, const catalog_schema *s, size_t i) {
    catalog_relation rel;
    catalog_function fn;
    sc_str name;
    if (t->mode == TREE_TABLES) {
        if (!catalog_relation_at(c, s, i, &rel)) return false;
        name = rel.name;
    } else {
        if (!catalog_function_at(c, s, i, &fn)) return false;
        name = fn.name;
    }
    return tree_matches(c->arena, name, t->filter_arena, t->filter);
}

static uint64_t tree_filtered(const catalog *c, const tree_state *t, uint64_t index, tree_row *out,
                              const tree_row *wanted, uint64_t *position) {
    uint64_t count = 0;
    catalog_schema s;
    for (size_t i = 0; catalog_schema_at(c, i, &s); i++) {
        bool schema = tree_matches(c->arena, s.name, t->filter_arena, t->filter);
        size_t children = s.load == CATALOG_LOAD_DONE ? sc_tslice_len(t->mode == TREE_TABLES ? s.relations : s.functions) : 0;
        bool include = schema;
        for (size_t k = 0; !include && k < children; k++) include = tree_child_matches(c, t, &s, k);
        if (!include) continue;
        if (wanted != NULL && wanted->kind == TREE_ROW_SCHEMA && wanted->schema == i) *position = count;
        if (count == index) *out = (tree_row){TREE_ROW_SCHEMA, i, 0};
        count++;
        for (size_t k = 0; k < children; k++) {
            if (!schema && !tree_child_matches(c, t, &s, k)) continue;
            if (wanted != NULL && wanted->schema == i && wanted->child == k &&
                wanted->kind == (t->mode == TREE_TABLES ? TREE_ROW_RELATION : TREE_ROW_FUNCTION)) *position = count;
            if (count == index) *out = (tree_row){t->mode == TREE_TABLES ? TREE_ROW_RELATION : TREE_ROW_FUNCTION, i, k};
            count++;
        }
    }
    return count;
}

static uint64_t tree_rows(const catalog *c, const tree_state *t) {
    tree_row unused;
    return t->filter.len == 0 ? tree_count(c, t->mode) : tree_filtered(c, t, UINT64_MAX, &unused, NULL, NULL);
}

static bool tree_row_for(const catalog *c, const tree_state *t, uint64_t i, tree_row *out) {
    return t->filter.len == 0 ? tree_row_at(c, t->mode, i, out) : i < tree_filtered(c, t, i, out, NULL, NULL);
}

static bool tree_position(const catalog *c, const tree_state *t, tree_row r, uint64_t *out) {
    if (t->filter.len == 0) return tree_index_of(c, t->mode, r, out);
    tree_row unused;
    *out = UINT64_MAX;
    (void)tree_filtered(c, t, UINT64_MAX, &unused, &r, out);
    return *out != UINT64_MAX;
}

static uint64_t tree_children(const catalog_schema *s, tree_mode m) {
    if (!s->expanded) {
        return 0;
    }
    if (s->load == CATALOG_LOAD_DONE) {
        return sc_tslice_len(m == TREE_TABLES ? s->relations : s->functions);
    }
    return tree_failed(s) ? 2u : 1u;
}

static tree_row_kind tree_child_kind(tree_mode m) {
    return m == TREE_TABLES ? TREE_ROW_RELATION : TREE_ROW_FUNCTION;
}

static tree_row tree_child(const catalog_schema *s, tree_mode m, size_t si, uint64_t o) {
    tree_row r = {TREE_ROW_SCHEMA, si, 0};
    if (o == 0) {
        return r;
    }
    if (s->load == CATALOG_LOAD_DONE) {
        r.kind = tree_child_kind(m);
        r.child = (size_t)(o - 1u);
    } else if (tree_failed(s)) {
        r.kind = o == 1u ? TREE_ROW_ERROR : TREE_ROW_RETRY;
    } else {
        r.kind = TREE_ROW_LOADING;
    }
    return r;
}

uint64_t tree_count(const catalog *c, tree_mode m) {
    uint64_t n = 0;
    catalog_schema s;
    for (size_t i = 0; catalog_schema_at(c, i, &s); i++) {
        n += 1u + tree_children(&s, m);
    }
    return n;
}

static bool tree_locate(const catalog *c, tree_mode m, uint64_t i, size_t *si, uint64_t *off) {
    catalog_schema s;
    uint64_t at = 0;
    for (size_t k = 0; catalog_schema_at(c, k, &s); k++) {
        uint64_t block = 1u + tree_children(&s, m);
        if (i - at < block) {
            *si = k;
            *off = i - at;
            return true;
        }
        at += block;
    }
    return false;
}

bool tree_row_at(const catalog *c, tree_mode m, uint64_t i, tree_row *out) {
    catalog_schema s;
    size_t si = 0;
    uint64_t off = 0;
    if (!tree_locate(c, m, i, &si, &off) || !catalog_schema_at(c, si, &s)) {
        return false;
    }
    *out = tree_child(&s, m, si, off);
    return true;
}

bool tree_index_of(const catalog *c, tree_mode m, tree_row r, uint64_t *out) {
    catalog_schema s;
    uint64_t at = 0;
    for (size_t si = 0; si < r.schema; si++) {
        if (!catalog_schema_at(c, si, &s)) {
            return false;
        }
        at += 1u + tree_children(&s, m);
    }
    if (!catalog_schema_at(c, r.schema, &s)) {
        return false;
    }
    uint64_t n = tree_children(&s, m);
    bool done = s.load == CATALOG_LOAD_DONE, failed = tree_failed(&s);
    tree_row_kind child = tree_child_kind(m);
    uint64_t o = 0;
    switch (r.kind) {
    case TREE_ROW_SCHEMA:
        o = 0;
        break;
    case TREE_ROW_RELATION:
    case TREE_ROW_FUNCTION:
        if (!done || r.kind != child || r.child >= n) {
            return false;
        }
        o = (uint64_t)r.child + 1u;
        break;
    case TREE_ROW_LOADING:
        o = 1;
        if (n == 0 || done || failed) {
            return false;
        }
        break;
    case TREE_ROW_ERROR:
    case TREE_ROW_RETRY:
        o = r.kind == TREE_ROW_ERROR ? 1u : 2u;
        if (n == 0 || !failed) {
            return false;
        }
        break;
    default:
        return false;
    }
    *out = at + o;
    return true;
}

static bool tree_child_oid(const catalog *c, const catalog_schema *s, tree_row_kind k, size_t i,
                           uint32_t *oid) {
    catalog_relation r;
    catalog_function f;
    if (k == TREE_ROW_RELATION) {
        if (!catalog_relation_at(c, s, i, &r)) {
            return false;
        }
        *oid = r.oid;
        return true;
    }
    if (k != TREE_ROW_FUNCTION || !catalog_function_at(c, s, i, &f)) {
        return false;
    }
    *oid = f.oid;
    return true;
}

bool tree_selection(const catalog *c, const tree_state *t, tree_row *row, uint64_t *index) {
    catalog_schema s;
    bool child = t->sel.kind == TREE_ROW_RELATION || t->sel.kind == TREE_ROW_FUNCTION;
    tree_row r = {t->sel.kind, t->sel.schema_at, child ? t->sel.child_at : 0u};
    bool found = t->selected && catalog_schema_at(c, r.schema, &s) && s.oid == t->sel.schema;
    for (size_t si = 0; t->selected && !found && catalog_schema_at(c, si, &s); si++) {
        found = s.oid == t->sel.schema;
        r.schema = si;
    }
    if (!found) {
        return false;
    }
    uint32_t oid = 0;
    found = !child || (tree_child_oid(c, &s, r.kind, r.child, &oid) && oid == t->sel.oid);
    for (size_t i = 0; !found && tree_child_oid(c, &s, r.kind, i, &oid); i++) {
        found = oid == t->sel.oid;
        r.child = i;
    }
    if (!found || !tree_position(c, t, r, index)) {
        return false;
    }
    *row = r;
    return true;
}

bool tree_set_selection(tree_state *t, const catalog *c, tree_row r) {
    catalog_schema s;
    tree_sel sel = {r.kind, 0, 0, r.schema, r.child};
    uint64_t i = 0;
    if (!tree_position(c, t, r, &i) || !catalog_schema_at(c, r.schema, &s) ||
        ((r.kind == TREE_ROW_RELATION || r.kind == TREE_ROW_FUNCTION) &&
         !tree_child_oid(c, &s, r.kind, r.child, &sel.oid))) {
        return false;
    }
    sel.schema = s.oid;
    t->selected = true;
    t->sel = sel;
    return true;
}

size_t tree_visible(const catalog *c, tree_state *t, uint32_t height, const sc_arena *a, sc_tslice out,
                    grid_rows *vis) {
    t->g.rows = tree_rows(c, t);
    t->g.view_h = height;
    t->g = grid_clamp(t->g);
    *vis = grid_visible(t->g);
    t->drawn = t->g.rows;
    t->drawn_first = vis->first;
    uint64_t i = 0;
    tree_row sel;
    if (t->selected && !tree_selection(c, t, &sel, &i)) {
        t->selected = false;
    } else if (t->selected) {
        t->sel.schema_at = sel.schema;
        t->sel.child_at = sel.child;
    }
    catalog_schema s;
    size_t si = 0;
    uint64_t off = 0;
    size_t n = 0;
    size_t cap = sc_tslice_len(out);
    if (t->filter.len > 0) {
        for (uint64_t k = 0; k < vis->count && k < cap; k++) {
            tree_row row;
            if (!tree_row_for(c, t, vis->first + k, &row) || !SC_TSLICE_SET(tree_row, a, out, (size_t)k, &row)) break;
            n++;
        }
        return n;
    }
    bool found = tree_locate(c, t->mode, vis->first, &si, &off);
    while (found && n < vis->count && n < cap && catalog_schema_at(c, si, &s)) {
        if (off >= 1u + tree_children(&s, t->mode)) {
            si++;
            off = 0;
            continue;
        }
        tree_row r = tree_child(&s, t->mode, si, off);
        if (!SC_TSLICE_SET(tree_row, a, out, n, &r)) {
            break;
        }
        n++;
        off++;
    }
    return n;
}

tree_key tree_key_for(uint32_t key, uint16_t mods, uint8_t platform) {
    bool cmd = frame_cmd(mods, platform);
    if ((mods & (UI_MOD_ALT | UI_MOD_SHIFT)) != 0 || frame_mac_ctrl(mods, platform)) {
        return TREE_KEY_NONE;
    }
    switch (key) {
    case UI_KEY_RETURN:
    case UI_KEY_KP_ENTER:
        return cmd ? TREE_KEY_OPEN_NEW : TREE_KEY_OPEN;
    case UI_KEY_UP:
        return cmd ? TREE_KEY_NONE : TREE_KEY_UP;
    case UI_KEY_DOWN:
        return cmd ? TREE_KEY_NONE : TREE_KEY_DOWN;
    case UI_KEY_LEFT:
        return cmd ? TREE_KEY_NONE : TREE_KEY_LEFT;
    case UI_KEY_RIGHT:
        return cmd ? TREE_KEY_NONE : TREE_KEY_RIGHT;
    default:
        return TREE_KEY_NONE;
    }
}

static bool tree_select(tree_state *t, const catalog *c, uint64_t i) {
    tree_row r;
    if (!tree_row_for(c, t, i, &r) || !tree_set_selection(t, c, r)) {
        return false;
    }
    t->g.rows = tree_rows(c, t);
    t->g.scroll_y = grid_reveal(t->g, i);
    return true;
}

static bool tree_select_schema(tree_state *t, const catalog *c, size_t schema) {
    tree_row r = {TREE_ROW_SCHEMA, schema, 0};
    uint64_t i = 0;
    return tree_position(c, t, r, &i) && tree_select(t, c, i);
}

static bool tree_activate(tree_state *t, catalog *c, tree_row r, bool new_tab, tree_open *open) {
    catalog_schema s;
    if (!catalog_schema_at(c, r.schema, &s)) {
        return false;
    }
    switch (r.kind) {
    case TREE_ROW_SCHEMA:
        if (new_tab) {
            return false;
        }
        (void)(s.expanded ? catalog_collapse(c, r.schema) : catalog_expand(c, r.schema));
        return tree_select_schema(t, c, r.schema);
    case TREE_ROW_RETRY:
        if (new_tab) {
            return false;
        }
        (void)catalog_expand(c, r.schema);
        return tree_select_schema(t, c, r.schema);
    case TREE_ROW_RELATION: {
        catalog_relation rel;
        if (!catalog_relation_at(c, &s, r.child, &rel)) {
            return false;
        }
        tree_open o = {TREE_OPEN_RELATION, new_tab, s.oid, rel.oid};
        *open = o;
        return true;
    }
    case TREE_ROW_FUNCTION: {
        catalog_function f;
        if (!catalog_function_at(c, &s, r.child, &f)) {
            return false;
        }
        tree_open o = {TREE_OPEN_FUNCTION, true, s.oid, f.oid};
        *open = o;
        return true;
    }
    default:
        return false;
    }
}

bool tree_press(tree_state *t, catalog *c, tree_key k, tree_open *open) {
    tree_open none = {TREE_OPEN_NONE, false, 0, 0};
    *open = none;
    uint64_t n = tree_rows(c, t);
    uint64_t i = 0;
    tree_row sel = {TREE_ROW_SCHEMA, 0, 0};
    bool have = tree_selection(c, t, &sel, &i);
    catalog_schema s;
    t->g.rows = n;
    switch (k) {
    case TREE_KEY_UP:
    case TREE_KEY_DOWN:
        if (!have) {
            return tree_select(t, c, 0);
        }
        if (k == TREE_KEY_UP ? i == 0 : i + 1u >= n) {
            return false;
        }
        return tree_select(t, c, k == TREE_KEY_UP ? i - 1u : i + 1u);
    case TREE_KEY_LEFT:
        if (!have || !catalog_schema_at(c, sel.schema, &s)) {
            return false;
        }
        if (sel.kind != TREE_ROW_SCHEMA) {
            return tree_select_schema(t, c, sel.schema);
        }
        return s.expanded && catalog_collapse(c, sel.schema) && tree_select_schema(t, c, sel.schema);
    case TREE_KEY_RIGHT:
        if (!have || sel.kind != TREE_ROW_SCHEMA || !catalog_schema_at(c, sel.schema, &s) || s.expanded) {
            return false;
        }
        (void)catalog_expand(c, sel.schema);
        return tree_select_schema(t, c, sel.schema);
    case TREE_KEY_OPEN:
    case TREE_KEY_OPEN_NEW:
        return have && tree_activate(t, c, sel, k == TREE_KEY_OPEN_NEW, open);
    default:
        return false;
    }
}

bool tree_click(tree_state *t, catalog *c, int64_t y, bool cmd, tree_open *open) {
    tree_open none = {TREE_OPEN_NONE, false, 0, 0};
    *open = none;
    t->g.rows = tree_rows(c, t);
    uint64_t i = 0;
    tree_row r;
    if (t->g.rows != t->drawn || grid_visible(t->g).first != t->drawn_first || !grid_row_at(t->g, y, &i) ||
        !tree_row_for(c, t, i, &r) || !tree_select(t, c, i)) {
        return false;
    }
    (void)tree_activate(t, c, r, cmd, open);
    return true;
}

bool tree_wheel(tree_state *t, int32_t dy) {
    uint64_t before = t->g.scroll_y;
    if (dy > 0) {
        uint64_t up = (uint64_t)dy;
        t->g.scroll_y = t->g.scroll_y > up ? t->g.scroll_y - up : 0u;
    } else {
        uint64_t down = (uint64_t)(-(int64_t)dy);
        t->g.scroll_y = t->g.scroll_y <= UINT64_MAX - down ? t->g.scroll_y + down : UINT64_MAX;
    }
    t->g = grid_clamp(t->g);
    return t->g.scroll_y != before;
}

tree_glyph tree_rel_glyph(catalog_rel_kind k) {
    tree_glyph g = {TREE_ICON_TABLE, THEME_KIND_TABLE};
    switch (k) {
    case CATALOG_REL_PARTITIONED:
        g.icon = TREE_ICON_PARTITIONED;
        g.token = THEME_KIND_PARTITIONED;
        break;
    case CATALOG_REL_VIEW:
        g.icon = TREE_ICON_VIEW;
        g.token = THEME_KIND_VIEW;
        break;
    case CATALOG_REL_MATVIEW:
        g.icon = TREE_ICON_MATVIEW;
        g.token = THEME_KIND_MATVIEW;
        break;
    case CATALOG_REL_FOREIGN:
        g.icon = TREE_ICON_FOREIGN;
        g.token = THEME_KIND_FOREIGN;
        break;
    default:
        break;
    }
    return g;
}

tree_glyph tree_glyph_for(const catalog *c, tree_row r) {
    tree_glyph g = {TREE_ICON_NONE, THEME_SECONDARY_TEXT};
    catalog_schema s;
    catalog_relation rel;
    switch (r.kind) {
    case TREE_ROW_SCHEMA:
        g.icon = TREE_ICON_FOLDER;
        break;
    case TREE_ROW_RELATION:
        if (catalog_schema_at(c, r.schema, &s) && catalog_relation_at(c, &s, r.child, &rel)) {
            g = tree_rel_glyph(rel.kind);
        }
        break;
    case TREE_ROW_FUNCTION:
        g.icon = TREE_ICON_FUNCTION;
        g.token = THEME_KIND_FUNCTION;
        break;
    default:
        break;
    }
    return g;
}
