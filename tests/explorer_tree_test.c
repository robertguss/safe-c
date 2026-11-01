#include "../examples/explorer/frame.c"
#include "../examples/explorer/grid.c"
#include "../examples/explorer/proto.c"
#include "../examples/explorer/result.c"
#include "../examples/explorer/catalog.c"
#include "../examples/explorer/catalog_sql.c"
#include "../examples/explorer/catalog_duckdb.c"
#include "../examples/explorer/catalog_sqlite.c"
#include "../examples/explorer/theme.c"
#include "../examples/explorer/tree.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

#define BIG 10000u

static alignas(8) uint8_t col_mem[1u << 12], chunk_mem[1u << 20], book_mem[1u << 16],
    rec_mem[4u << 20], keep_mem[1u << 12], tmp_mem[1u << 10], rows_mem[1u << 12], defs_mem[1u << 10];
static sc_arena col_a, chunk_a, book, rec, keep, tmp, rows_a, defs;

static sc_str lit(sc_arena *a, const char *s) {
    size_t n = strlen(s);
    return n == 0 ? (sc_str){NULL, 0, a->epoch} : sc_str_from_lit(a, (const uint8_t *)s, n);
}

typedef const char *(*cell_fn)(uint64_t r, uint64_t c);

static result make(uint64_t cols, uint64_t rows, cell_fn cell) {
    result r;
    assert(sc_arena_reset(&book) && result_init(&r, &book, 4));
    assert(sc_arena_reset(&col_a) && sc_arena_reset(&chunk_a));
    proto_writer w;
    proto_header h = {PROTO_COLUMNS, 1, 1, 0, 0, 0};
    assert(proto_begin(&w, &col_a, h) && proto_put_u64(&w, cols));
    for (uint64_t c = 0; c < cols; c++) {
        assert(sc_arena_reset(&tmp) && proto_put_str(&w, &tmp, lit(&tmp, "c")) && proto_put_u64(&w, 25));
    }
    assert(proto_end(&w) && result_set_columns(&r, &col_a, 0) == RESULT_ADDED);
    if (rows == 0) {
        return r;
    }
    h.kind = PROTO_CHUNK;
    h.arg = rows;
    assert(proto_begin(&w, &chunk_a, h));
    for (uint64_t k = 0; k < rows * cols; k++) {
        const char *s = cell(k / cols, k % cols);
        assert(sc_arena_reset(&tmp) && proto_put_u64(&w, strlen(s)) && proto_put_str(&w, &tmp, lit(&tmp, s)));
    }
    assert(proto_end(&w) && result_add_chunk(&r, &chunk_a, 1) == RESULT_ADDED);
    return r;
}

static char buf[32];

static const char *const names3[] = {"a", "b", "c"}, *const oids3[] = {"100", "101", "102"};
static const char *const names4[] = {"a", "aa", "b", "c"}, *const oids4[] = {"100", "150", "101", "102"};
static const char *const *schema_names = names3, *const *schema_oids = oids3;

static const char *schema_cell(uint64_t r, uint64_t c) {
    return c == 0 ? schema_oids[r] : schema_names[r];
}

static uint64_t rel_base = 1000;

static const char *rel_cell(uint64_t r, uint64_t c) {
    static const char *const kinds[] = {"r", "v", "m", "p", "f"};
    switch (c) {
    case 0:
        snprintf(buf, sizeof buf, "%llu", (unsigned long long)(rel_base + r));
        return buf;
    case 1:
        snprintf(buf, sizeof buf, "t%05llu", (unsigned long long)r);
        return buf;
    case 2:
        return kinds[r % 5u];
    default:
        return "-1";
    }
}

static const char *fn_cell(uint64_t r, uint64_t c) {
    static const char *const cells[] = {"2000", "add", "f", "a integer, b integer", "integer"};
    (void)r;
    return cells[c];
}

static uint64_t relations;
static const char *no_cell(uint64_t r, uint64_t c) {
    (void)r;
    (void)c;
    return "";
}

/* A reload whose schema list is `n` of schema_names. */
static void schemas_landed(catalog *c, uint64_t n) {
    assert(catalog_restart(c));
    catalog_read t;
    result r = make(0, 0, no_cell);
    r.has_columns = false;
    assert(catalog_next(c, &t) && catalog_take(c, t, &r));
    assert(catalog_next(c, &t) && t.kind == CATALOG_READ_SESSION);
    assert(catalog_next(c, &t) && t.kind == CATALOG_READ_SCHEMAS);
    r = make(2, n, schema_cell);
    assert(catalog_take(c, t, &r) && catalog_schema_count(c) == n);
}

static void three_schemas(catalog *c) {
    assert(sc_arena_reset(&rec) && sc_arena_reset(&keep) && catalog_init(c, &rec, &keep, &defs));
    schema_names = names3;
    schema_oids = oids3;
    schemas_landed(c, 3);
}

static void load_b(catalog *c) {
    catalog_read rr, fr;
    assert(catalog_next(c, &rr) && rr.kind == CATALOG_READ_RELATIONS && rr.oid == 101);
    assert(catalog_next(c, &fr) && fr.kind == CATALOG_READ_FUNCTIONS);
    result r = make(4, relations, rel_cell);
    assert(catalog_take(c, rr, &r));
    r = make(5, 1, fn_cell);
    assert(catalog_take(c, fr, &r));
}

static tree_row row(const catalog *c, const tree_state *t, uint64_t i) {
    tree_row r;
    assert(tree_row_at(c, t->mode, i, &r));
    return r;
}

static bool is_row(tree_row r, tree_row_kind kind, size_t schema, size_t child) {
    return r.kind == kind && r.schema == schema && (kind == TREE_ROW_SCHEMA || r.child == child);
}

static uint64_t sel_index(const catalog *c, const tree_state *t) {
    uint64_t i = 0;
    tree_row r;
    assert(tree_selection(c, t, &r, &i));
    return i;
}

static tree_row sel_row(const catalog *c, const tree_state *t) {
    uint64_t i = 0;
    tree_row r;
    assert(tree_selection(c, t, &r, &i));
    return r;
}

static void visible(const catalog *c, tree_state *t, uint32_t h) {
    assert(sc_arena_reset(&rows_a));
    sc_tslice out = SC_TSLICE_ALLOC(tree_row, &rows_a, 64);
    grid_rows vis;
    (void)tree_visible(c, t, h, &rows_a, out, &vis);
}

static void flatten_and_collapse(void) {
    catalog c;
    tree_state t;
    tree_init(&t);
    three_schemas(&c);
    assert(tree_count(&c, TREE_TABLES) == 3);
    assert(catalog_expand(&c, 1));
    assert(tree_count(&c, TREE_TABLES) == 4 && row(&c, &t, 2).kind == TREE_ROW_LOADING);
    relations = 3;
    load_b(&c);
    assert(tree_count(&c, TREE_TABLES) == 6);
    assert(is_row(row(&c, &t, 0), TREE_ROW_SCHEMA, 0, 0) && is_row(row(&c, &t, 1), TREE_ROW_SCHEMA, 1, 0));
    assert(is_row(row(&c, &t, 2), TREE_ROW_RELATION, 1, 0) && is_row(row(&c, &t, 3), TREE_ROW_RELATION, 1, 1));
    assert(is_row(row(&c, &t, 4), TREE_ROW_RELATION, 1, 2) && is_row(row(&c, &t, 5), TREE_ROW_SCHEMA, 2, 0));
    tree_row past;
    assert(!tree_row_at(&c, TREE_TABLES, 6, &past));
    visible(&c, &t, 400);
    tree_open o;
    assert(tree_press(&t, &c, TREE_KEY_DOWN, &o) && o.kind == TREE_OPEN_NONE && sel_index(&c, &t) == 0);
    for (int i = 0; i < 3; i++) {
        assert(tree_press(&t, &c, TREE_KEY_DOWN, &o));
    }
    assert(sel_index(&c, &t) == 3 && is_row(sel_row(&c, &t), TREE_ROW_RELATION, 1, 1));
    assert(tree_press(&t, &c, TREE_KEY_UP, &o) && sel_index(&c, &t) == 2);
    assert(tree_press(&t, &c, TREE_KEY_LEFT, &o) && is_row(sel_row(&c, &t), TREE_ROW_SCHEMA, 1, 0));
    assert(tree_count(&c, TREE_TABLES) == 6);
    assert(tree_press(&t, &c, TREE_KEY_LEFT, &o) && tree_count(&c, TREE_TABLES) == 3);
    assert(is_row(sel_row(&c, &t), TREE_ROW_SCHEMA, 1, 0) && sel_index(&c, &t) == 1 && o.kind == TREE_OPEN_NONE);
    assert(!tree_press(&t, &c, TREE_KEY_LEFT, &o) && tree_count(&c, TREE_TABLES) == 3);
    assert(is_row(sel_row(&c, &t), TREE_ROW_SCHEMA, 1, 0));
    catalog_read none;
    assert(tree_press(&t, &c, TREE_KEY_RIGHT, &o) && tree_count(&c, TREE_TABLES) == 6);
    assert(!catalog_next(&c, &none));
    assert(!tree_press(&t, &c, TREE_KEY_RIGHT, &o));
    assert(tree_press(&t, &c, TREE_KEY_OPEN, &o) && tree_count(&c, TREE_TABLES) == 3);
    assert(tree_press(&t, &c, TREE_KEY_OPEN, &o) && tree_count(&c, TREE_TABLES) == 6 && o.kind == TREE_OPEN_NONE);
    assert(tree_press(&t, &c, TREE_KEY_UP, &o) && sel_index(&c, &t) == 0);
    assert(!tree_press(&t, &c, TREE_KEY_UP, &o) && sel_index(&c, &t) == 0);
    for (int i = 0; i < 5; i++) {
        assert(tree_press(&t, &c, TREE_KEY_DOWN, &o));
    }
    assert(!tree_press(&t, &c, TREE_KEY_DOWN, &o) && sel_index(&c, &t) == 5);

    tree_set_mode(&t, TREE_FUNCTIONS);
    assert(!t.selected && tree_count(&c, TREE_FUNCTIONS) == 4);
    assert(is_row(row(&c, &t, 2), TREE_ROW_FUNCTION, 1, 0) && is_row(row(&c, &t, 3), TREE_ROW_SCHEMA, 2, 0));
}

static void opens(void) {
    catalog c;
    tree_state t;
    tree_init(&t);
    three_schemas(&c);
    assert(catalog_expand(&c, 1));
    relations = 3;
    load_b(&c);
    visible(&c, &t, 400);
    tree_open o;
    for (int i = 0; i < 4; i++) {
        assert(tree_press(&t, &c, TREE_KEY_DOWN, &o));
    }
    assert(is_row(sel_row(&c, &t), TREE_ROW_RELATION, 1, 1));
    assert(tree_press(&t, &c, TREE_KEY_OPEN_NEW, &o) && o.kind == TREE_OPEN_RELATION && o.new_tab);
    assert(o.schema == 101 && o.oid == 1001);
    assert(tree_press(&t, &c, TREE_KEY_OPEN, &o) && o.kind == TREE_OPEN_RELATION && !o.new_tab);
    assert(o.schema == 101 && o.oid == 1001);
    assert(tree_click(&t, &c, 4 * THEME_SIDEBAR_ROW_HEIGHT + 3, false, &o));
    assert(o.kind == TREE_OPEN_RELATION && !o.new_tab && o.oid == 1002 && is_row(sel_row(&c, &t), TREE_ROW_RELATION, 1, 2));
    assert(tree_click(&t, &c, 2 * THEME_SIDEBAR_ROW_HEIGHT, true, &o));
    assert(o.kind == TREE_OPEN_RELATION && o.new_tab && o.oid == 1000);
    assert(tree_click(&t, &c, 1 * THEME_SIDEBAR_ROW_HEIGHT + 22, false, &o) && o.kind == TREE_OPEN_NONE);
    assert(is_row(sel_row(&c, &t), TREE_ROW_SCHEMA, 1, 0) && tree_count(&c, TREE_TABLES) == 3);
    visible(&c, &t, 400);
    assert(!tree_click(&t, &c, 3 * THEME_SIDEBAR_ROW_HEIGHT, false, &o) && o.kind == TREE_OPEN_NONE);
    assert(tree_click(&t, &c, 1 * THEME_SIDEBAR_ROW_HEIGHT, false, &o) && tree_count(&c, TREE_TABLES) == 6);
    tree_set_mode(&t, TREE_FUNCTIONS);
    visible(&c, &t, 400);
    assert(tree_click(&t, &c, 2 * THEME_SIDEBAR_ROW_HEIGHT, false, &o));
    assert(o.kind == TREE_OPEN_FUNCTION && o.new_tab && o.schema == 101 && o.oid == 2000);
    assert(tree_press(&t, &c, TREE_KEY_OPEN, &o) && o.kind == TREE_OPEN_FUNCTION && o.new_tab);
}

static void loading_and_failure(void) {
    catalog c;
    tree_state t;
    tree_init(&t);
    three_schemas(&c);
    assert(catalog_expand(&c, 0));
    catalog_read rr, fr;
    assert(catalog_next(&c, &rr) && catalog_next(&c, &fr));
    assert(tree_count(&c, TREE_TABLES) == 4 && is_row(row(&c, &t, 1), TREE_ROW_LOADING, 0, 0));
    assert(tree_count(&c, TREE_FUNCTIONS) == 4 && is_row(row(&c, &t, 1), TREE_ROW_LOADING, 0, 0));
    assert(sc_arena_reset(&tmp));
    catalog_fail(&c, rr, &tmp, lit(&tmp, "bigint out of range"));
    assert(tree_count(&c, TREE_TABLES) == 4 && is_row(row(&c, &t, 1), TREE_ROW_LOADING, 0, 0));
    tree_row err = {TREE_ROW_ERROR, 0, 0}, wait = {TREE_ROW_LOADING, 0, 0};
    uint64_t at = 0;
    assert(!tree_index_of(&c, TREE_TABLES, err, &at) && tree_index_of(&c, TREE_TABLES, wait, &at) && at == 1);
    result r = make(5, 0, no_cell);
    assert(catalog_take(&c, fr, &r));
    assert(tree_count(&c, TREE_TABLES) == 5);
    assert(is_row(row(&c, &t, 1), TREE_ROW_ERROR, 0, 0) && is_row(row(&c, &t, 2), TREE_ROW_RETRY, 0, 0));
    visible(&c, &t, 400);
    tree_open o;
    assert(tree_press(&t, &c, TREE_KEY_DOWN, &o) && tree_press(&t, &c, TREE_KEY_DOWN, &o));
    assert(is_row(sel_row(&c, &t), TREE_ROW_ERROR, 0, 0) && !tree_press(&t, &c, TREE_KEY_OPEN, &o));
    assert(tree_press(&t, &c, TREE_KEY_DOWN, &o) && is_row(sel_row(&c, &t), TREE_ROW_RETRY, 0, 0));
    assert(tree_press(&t, &c, TREE_KEY_OPEN, &o) && o.kind == TREE_OPEN_NONE);
    assert(catalog_next(&c, &rr) && rr.kind == CATALOG_READ_RELATIONS && rr.oid == 100);
    assert(tree_count(&c, TREE_TABLES) == 4 && is_row(row(&c, &t, 1), TREE_ROW_LOADING, 0, 0));
    assert(is_row(sel_row(&c, &t), TREE_ROW_SCHEMA, 0, 0) && sel_index(&c, &t) == 0);
}

static void long_schema(void) {
    catalog c;
    tree_state t;
    tree_init(&t);
    three_schemas(&c);
    assert(catalog_expand(&c, 0) && catalog_expand(&c, 1));
    catalog_read rr, fr;
    assert(catalog_next(&c, &rr) && catalog_next(&c, &fr));
    result r = make(4, 0, no_cell);
    assert(catalog_take(&c, rr, &r));
    r = make(5, 0, no_cell);
    assert(catalog_take(&c, fr, &r));
    relations = BIG;
    load_b(&c);
    assert(tree_count(&c, TREE_TABLES) == 3u + BIG);
    t.g.scroll_y = 5000u * THEME_SIDEBAR_ROW_HEIGHT;
    assert(sc_arena_reset(&rows_a));
    sc_tslice out = SC_TSLICE_ALLOC(tree_row, &rows_a, 64);
    grid_rows vis;
    size_t n = tree_visible(&c, &t, 900, &rows_a, out, &vis);
    assert(vis.first == 5000 && vis.top == 0 && n == vis.count);
    assert(n == (900u + THEME_SIDEBAR_ROW_HEIGHT - 1u) / THEME_SIDEBAR_ROW_HEIGHT && n <= 40u);
    tree_row first, last;
    assert(SC_TSLICE_GET(tree_row, &rows_a, out, 0, &first) && SC_TSLICE_GET(tree_row, &rows_a, out, n - 1u, &last));
    assert(is_row(first, TREE_ROW_RELATION, 1, 4998) && is_row(last, TREE_ROW_RELATION, 1, 4998 + n - 1u));
    sc_tslice small = SC_TSLICE_ALLOC(tree_row, &rows_a, 4);
    assert(tree_visible(&c, &t, 900, &rows_a, small, &vis) == 4);
    t.g.scroll_y = UINT64_MAX;
    n = tree_visible(&c, &t, 900, &rows_a, out, &vis);
    assert(SC_TSLICE_GET(tree_row, &rows_a, out, n - 1u, &last) && is_row(last, TREE_ROW_SCHEMA, 2, 0));
    assert(!tree_wheel(&t, -100 * THEME_SIDEBAR_ROW_HEIGHT) && tree_wheel(&t, 3 * THEME_SIDEBAR_ROW_HEIGHT));
    tree_open o;
    assert(tree_press(&t, &c, TREE_KEY_DOWN, &o) && sel_index(&c, &t) == 0 && t.g.scroll_y == 0);
}

static void keys(void) {
    const uint8_t mac = UI_PLATFORM_MAC, other = UI_PLATFORM_OTHER;
    assert(tree_key_for(UI_KEY_UP, 0, mac) == TREE_KEY_UP && tree_key_for(UI_KEY_DOWN, 0, other) == TREE_KEY_DOWN);
    assert(tree_key_for(UI_KEY_LEFT, 0, mac) == TREE_KEY_LEFT && tree_key_for(UI_KEY_RIGHT, 0, mac) == TREE_KEY_RIGHT);
    assert(tree_key_for(UI_KEY_RETURN, 0, mac) == TREE_KEY_OPEN && tree_key_for(UI_KEY_KP_ENTER, 0, mac) == TREE_KEY_OPEN);
    assert(tree_key_for(UI_KEY_RETURN, UI_MOD_CMD, mac) == TREE_KEY_OPEN_NEW);
    assert(tree_key_for(UI_KEY_RETURN, UI_MOD_CMD | UI_MOD_CTRL, other) == TREE_KEY_OPEN_NEW);
    assert(tree_key_for(UI_KEY_RETURN, UI_MOD_CTRL, mac) == TREE_KEY_NONE);
    assert(tree_key_for(UI_KEY_RETURN, UI_MOD_CMD | UI_MOD_SHIFT, mac) == TREE_KEY_NONE);
    assert(tree_key_for(UI_KEY_UP, UI_MOD_ALT, mac) == TREE_KEY_NONE && tree_key_for(UI_KEY_UP, UI_MOD_CMD, mac) == TREE_KEY_NONE);
    assert(tree_key_for('a', 0, mac) == TREE_KEY_NONE);
}

static void glyphs(void) {
    const catalog_rel_kind kinds[] = {CATALOG_REL_TABLE, CATALOG_REL_PARTITIONED, CATALOG_REL_VIEW,
                                      CATALOG_REL_MATVIEW, CATALOG_REL_FOREIGN};
    const tree_icon icons[] = {TREE_ICON_TABLE, TREE_ICON_PARTITIONED, TREE_ICON_VIEW, TREE_ICON_MATVIEW,
                               TREE_ICON_FOREIGN};
    for (size_t i = 0; i < 5; i++) {
        tree_glyph g = tree_rel_glyph(kinds[i]);
        assert(g.icon == icons[i]);
        for (int dark = 0; dark < 2; dark++) {
            assert(theme_hex(dark, g.token) != 0);
            for (size_t j = 0; j < i; j++) {
                tree_glyph h = tree_rel_glyph(kinds[j]);
                assert(h.token != g.token && theme_hex(dark, h.token) != theme_hex(dark, g.token));
            }
        }
    }
}

static void same_mode(void) {
    catalog c;
    tree_state t;
    tree_init(&t);
    three_schemas(&c);
    assert(catalog_expand(&c, 1));
    relations = 40;
    load_b(&c);
    visible(&c, &t, 200);
    tree_open o;
    for (int i = 0; i < 30; i++) {
        assert(tree_press(&t, &c, TREE_KEY_DOWN, &o));
    }
    uint64_t scroll = t.g.scroll_y;
    assert(scroll > 0 && sel_index(&c, &t) == 29);
    tree_set_mode(&t, TREE_TABLES);
    assert(t.mode == TREE_TABLES && sel_index(&c, &t) == 29 && t.g.scroll_y == scroll);
    tree_set_mode(&t, TREE_FUNCTIONS);
    assert(t.mode == TREE_FUNCTIONS && !t.selected && t.g.scroll_y == 0);
}

static void selection_by_oid(void) {
    catalog c;
    tree_state t;
    tree_init(&t);
    three_schemas(&c);
    assert(catalog_expand(&c, 1));
    relations = 3;
    load_b(&c);
    tree_open o;
    for (int i = 0; i < 4; i++) {
        assert(tree_press(&t, &c, TREE_KEY_DOWN, &o));
    }
    assert(sel_index(&c, &t) == 3);
    schema_names = names4;
    schema_oids = oids4;
    schemas_landed(&c, 4);
    rel_base = 999;
    load_b(&c);
    rel_base = 1000;
    assert(sel_index(&c, &t) == 5);
    assert(tree_press(&t, &c, TREE_KEY_OPEN, &o) && o.kind == TREE_OPEN_RELATION && o.schema == 101 && o.oid == 1001);
    assert(tree_press(&t, &c, TREE_KEY_LEFT, &o) && sel_index(&c, &t) == 2);
    schema_names = names3;
    schema_oids = oids3;
    schemas_landed(&c, 3);
    load_b(&c);
    assert(sel_index(&c, &t) == 1);
    visible(&c, &t, 400);
    assert(sel_index(&c, &t) == 1);
    c.engine = SC_DB_SQLITE;
    c.gen++;
    tree_row stale;
    uint64_t index = 0;
    assert(!tree_selection(&c, &t, &stale, &index));
    assert(!tree_press(&t, &c, TREE_KEY_OPEN, &o) && o.kind == TREE_OPEN_NONE);
    assert(!tree_click(&t, &c, THEME_SIDEBAR_ROW_HEIGHT, false, &o));
    visible(&c, &t, 400);
    assert(!t.selected && tree_click(&t, &c, THEME_SIDEBAR_ROW_HEIGHT, false, &o));
}

static void click_on_changed_rows(void) {
    catalog c;
    tree_state t;
    tree_init(&t);
    three_schemas(&c);
    visible(&c, &t, 400);
    assert(catalog_expand(&c, 0));
    tree_open o;
    catalog_schema s;
    assert(!tree_click(&t, &c, 2 * THEME_SIDEBAR_ROW_HEIGHT, false, &o) && o.kind == TREE_OPEN_NONE);
    assert(!t.selected && catalog_schema_at(&c, 1, &s) && !s.expanded);
    visible(&c, &t, 400);
    assert(tree_click(&t, &c, 2 * THEME_SIDEBAR_ROW_HEIGHT, false, &o) && sel_index(&c, &t) == 2);
    assert(catalog_schema_at(&c, 1, &s) && s.expanded);
}

static void glyph_per_row(void) {
    catalog c;
    tree_state t;
    tree_init(&t);
    three_schemas(&c);
    assert(catalog_expand(&c, 1));
    relations = 5;
    load_b(&c);
    const catalog_rel_kind kinds[] = {CATALOG_REL_TABLE, CATALOG_REL_VIEW, CATALOG_REL_MATVIEW,
                                      CATALOG_REL_PARTITIONED, CATALOG_REL_FOREIGN};
    for (uint64_t i = 0; i < 5; i++) {
        tree_glyph g = tree_glyph_for(&c, row(&c, &t, 2u + i)), want = tree_rel_glyph(kinds[i]);
        assert(g.icon == want.icon && g.token == want.token);
    }
    tree_glyph g = tree_glyph_for(&c, row(&c, &t, 1));
    assert(g.icon == TREE_ICON_FOLDER);
    tree_set_mode(&t, TREE_FUNCTIONS);
    g = tree_glyph_for(&c, row(&c, &t, 2));
    assert(row(&c, &t, 2).kind == TREE_ROW_FUNCTION && g.icon == TREE_ICON_FUNCTION && g.token == THEME_KIND_FUNCTION);
}

static void filtered_rows(void) {
    catalog c;
    tree_state t;
    tree_init(&t);
    three_schemas(&c);
    assert(catalog_expand(&c, 1));
    relations = 3;
    load_b(&c);
    assert(catalog_collapse(&c, 1));
    t.filter_arena = &tmp;
    t.filter = lit(&tmp, "T00001");
    visible(&c, &t, 400);
    assert(t.g.rows == 2 && tree_unloaded(&c) == 2);
    tree_open o;
    assert(tree_press(&t, &c, TREE_KEY_DOWN, &o) && sel_index(&c, &t) == 0);
    assert(tree_press(&t, &c, TREE_KEY_DOWN, &o) && sel_index(&c, &t) == 1);
    assert(is_row(sel_row(&c, &t), TREE_ROW_RELATION, 1, 1));
    assert(tree_press(&t, &c, TREE_KEY_OPEN, &o) && o.kind == TREE_OPEN_RELATION && o.oid == rel_base + 1u);
    t.filter = lit(&tmp, "missing");
    visible(&c, &t, 400);
    assert(t.g.rows == 0 && !t.selected);
    t.filter = lit(&tmp, "b");
    visible(&c, &t, 400);
    assert(t.g.rows == 4);
}

int main(void) {
    sc_arena_init(&col_a, col_mem, sizeof col_mem);
    sc_arena_init(&chunk_a, chunk_mem, sizeof chunk_mem);
    sc_arena_init(&book, book_mem, sizeof book_mem);
    sc_arena_init(&rec, rec_mem, sizeof rec_mem);
    sc_arena_init(&keep, keep_mem, sizeof keep_mem);
    sc_arena_init(&defs, defs_mem, sizeof defs_mem);
    sc_arena_init(&tmp, tmp_mem, sizeof tmp_mem);
    sc_arena_init(&rows_a, rows_mem, sizeof rows_mem);
    flatten_and_collapse();
    opens();
    loading_and_failure();
    long_schema();
    keys();
    glyphs();
    same_mode();
    selection_by_oid();
    click_on_changed_rows();
    glyph_per_row();
    filtered_rows();
    puts("explorer_tree_test: ok");
    return 0;
}
