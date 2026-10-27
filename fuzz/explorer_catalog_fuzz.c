/* Fuzzes the explorer's catalog model (examples/explorer/catalog.c, plan U18).
 *
 * The input picks a read (timeout, session, schemas, relations, functions,
 * columns, constraints, indexes or a function's definition), a column count near the one it selects,
 * a row count, and each cell: NULL, or up to 15 bytes drawn from the input,
 * often mapped to digits, one-letter codes, "-1" or attnum lists so rows
 * parse, and sometimes marked truncated. The COLUMNS and
 * CHUNK messages sit in heap blocks of exactly their size, so a read past
 * either is an ASan error. catalog_take must refuse a result whose width is
 * not the read's, and the timeout read, a SET, refuses any columns; when it
 * accepts one, every stored string must lie inside the catalog arena and
 * equal its cell (with an ellipsis when truncated), and every row must be
 * stored. A definition is taken only when every line is whole, and is then
 * the lines joined with newlines; otherwise its failure is reported. Ends
 * with a restart: the old read is dropped. */
#include "../examples/explorer/proto.c"
#include "../examples/explorer/result.c"
#include "../examples/explorer/catalog.c"
#include <assert.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

#define ROWS 12u
#define COLS 9u
#define CELL 15u

static alignas(8) uint8_t build_mem[2][1u << 16];
static alignas(8) uint8_t book_mem[1u << 18];
static alignas(8) uint8_t rec_mem[1u << 18];
static alignas(8) uint8_t keep_mem[1u << 12];
static alignas(8) uint8_t defs_mem[1u << 12];
static alignas(8) uint8_t tmp_mem[256];

typedef struct {
    bool null;
    bool cut; /* sent truncated: the value was longer */
    size_t len;
    uint8_t bytes[CELL];
} cell_src;

static cell_src cells[ROWS][COLS];

static size_t pos;
static uint8_t next_byte(const uint8_t *data, size_t size) {
    return pos < size ? data[pos++] : 0;
}

/* What each read's columns hold, so most rows parse: a number, text, a
 * relkind, a reltuples estimate, a prokind or contype, a bool, an identity
 * code, a generated code, on/off, or an attnum list. */
static const char *const shapes[] = {"", "ntstt", "nt", "ntke", "ntctt", "nttbtiga", "tctlt", "tbbt", "t"};

static void plausible(cell_src *s, uint8_t shape, uint8_t b) {
    static const char *const codes[] = {"rpvmf", "fpaw", "puxcf"};
    s->null = shape == 't' && b % 7u == 0;
    if (s->null) {
        s->len = 0;
        return;
    }
    switch (shape) {
    case 'n':
    case 'e':
    case 'a':
        s->len = 1u + b % 5u;
        for (size_t i = 0; i < s->len; i++) {
            s->bytes[i] = (uint8_t)('0' + (b + i * 7u) % 10u);
        }
        if (shape == 'e' && b % 3u == 0) {
            s->len = 2;
            memcpy(s->bytes, "-1", 2);
        }
        break;
    case 'k':
    case 'c':
        s->len = 1;
        s->bytes[0] = (uint8_t)codes[shape == 'k' ? 0 : 1 + b % 2u][b % 4u];
        break;
    case 'b':
        s->len = 1;
        s->bytes[0] = b % 2u ? 't' : 'f';
        break;
    case 'l':
        s->len = b % 4u == 0 ? 0u : 2u * (b % 4u) - 1u;
        for (size_t i = 0; i < s->len; i++) {
            s->bytes[i] = i % 2u ? (uint8_t)' ' : (uint8_t)('1' + (b + i) % 9u);
        }
        break;
    case 'i':
    case 'g':
        s->len = b % 3u == 0 ? 0u : 1u;
        s->bytes[0] = shape == 'g' ? (b % 2u ? 's' : 'v') : b % 2u ? 'a' : 'd';
        break;
    case 's':
        s->len = b % 2u ? 2u : 3u;
        memcpy(s->bytes, b % 2u ? "on" : "off", s->len);
        break;
    default:
        break;
    }
}

static sc_str lit(sc_arena *a, const uint8_t *p, size_t n) {
    return n == 0 ? (sc_str){NULL, 0, a->epoch} : sc_str_from_lit(a, p, n);
}

/* A heap copy of the message in `src`, exactly its size, as a full arena. */
static sc_arena exact(const sc_arena *src, uint8_t **block) {
    *block = malloc(src->used);
    assert(*block != NULL);
    memcpy(*block, src->base, src->used);
    sc_arena a = {*block, src->used, src->used, src->epoch};
    return a;
}

static bool in_arena(const sc_arena *a, sc_str s) {
    return s.len == 0 || (s.ptr >= a->base && s.len <= a->used &&
                          (size_t)(s.ptr - a->base) <= a->used - s.len);
}

/* The record string `s` is inside the catalog and equals cell (r, c),
 * followed by an ellipsis when the cell was truncated. */
static void same_cell(const sc_arena *rec, sc_str s, size_t r, size_t c) {
    assert(in_arena(rec, s));
    const cell_src *k = &cells[r][c];
    size_t extra = k->cut ? 3u : 0u;
    assert(k->null ? s.len == 0 : s.len == k->len + extra && (k->len == 0 || memcmp(s.ptr, k->bytes, k->len) == 0));
    assert(!k->cut || memcmp(s.ptr + k->len, "\xE2\x80\xA6", 3) == 0);
}

static void check_relation(const sc_arena *rec, const catalog_relation *r, size_t row) {
    assert(in_arena(rec, r->name) && in_arena(rec, r->error));
    same_cell(rec, r->name, row, 1);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static sc_arena book, rec, keep, defs, tmp, b0, b1;
    sc_arena_init(&book, book_mem, sizeof book_mem);
    sc_arena_init(&rec, rec_mem, sizeof rec_mem);
    sc_arena_init(&keep, keep_mem, sizeof keep_mem);
    sc_arena_init(&defs, defs_mem, sizeof defs_mem);
    pos = 0;
    catalog_read_kind kind = (catalog_read_kind)(next_byte(data, size) % 9u);
    const uint64_t widths[] = {0, 5, 2, 4, 5, 8, 5, 4, 1};
    uint8_t wb = next_byte(data, size);
    uint64_t cols = widths[kind] + (wb % 8u == 0 ? 1u : 0u) - (wb % 8u == 1 && widths[kind] > 0 ? 1u : 0u);
    size_t rows = next_byte(data, size) % (ROWS + 1u);

    /* The catalog with a schema 2200 expanded and its relation 16400 asked for. */
    catalog c;
    assert(catalog_init(&c, &rec, &keep, &defs) && catalog_restart(&c));
    catalog_read tr, sr, schr, rr, fr, dr;
    assert(catalog_next(&c, &tr) && catalog_next(&c, &sr) && catalog_next(&c, &schr));
    for (size_t r = 0; r < ROWS; r++) {
        for (size_t k = 0; k < COLS; k++) {
            uint8_t ctl = next_byte(data, size);
            cell_src *s = &cells[r][k];
            s->null = ctl % 9u == 0;
            s->len = s->null ? 0 : (size_t)(ctl >> 4) % (CELL + 1u);
            for (size_t i = 0; i < s->len; i++) {
                uint8_t b = next_byte(data, size);
                s->bytes[i] = (ctl & 0x8u) != 0 ? (uint8_t)('0' + b % 10u) : b;
            }
            if (ctl % 9u == 1) { /* a one-letter code */
                s->len = 1;
                s->bytes[0] = (uint8_t)"rpvmfadstfn"[ctl % 11u];
            } else if (ctl % 9u == 2) {
                s->len = 2;
                memcpy(s->bytes, "-1", 2);
            } else if (ctl % 9u == 3) {
                s->len = 2;
                memcpy(s->bytes, "on", 2);
            } else if (ctl % 9u >= 4u && k < strlen(shapes[kind])) {
                plausible(s, (uint8_t)shapes[kind][k], next_byte(data, size));
            }
            s->cut = !s->null && ctl % 13u == 12u;
        }
    }

    sc_arena_init(&b0, build_mem[0], sizeof build_mem[0]);
    sc_arena_init(&b1, build_mem[1], sizeof build_mem[1]);
    proto_writer w;
    proto_header h = {PROTO_COLUMNS, 1, 1, 0, 0, 0};
    assert(proto_begin(&w, &b0, h) && proto_put_u64(&w, cols));
    for (uint64_t k = 0; k < cols; k++) {
        sc_arena_init(&tmp, tmp_mem, sizeof tmp_mem);
        assert(proto_put_str(&w, &tmp, lit(&tmp, (const uint8_t *)"c", 1)) && proto_put_u64(&w, 25));
    }
    assert(proto_end(&w));
    h.kind = PROTO_CHUNK;
    h.arg = rows;
    assert(proto_begin(&w, &b1, h));
    for (size_t r = 0; r < rows; r++) {
        for (size_t k = 0; k < cols; k++) {
            const cell_src *s = &cells[r][k % COLS];
            sc_arena_init(&tmp, tmp_mem, sizeof tmp_mem);
            uint64_t meta = s->null ? PROTO_CELL_NULL : s->cut ? (s->len + 1u) | PROTO_CELL_TRUNCATED : s->len;
            assert(proto_put_u64(&w, meta) &&
                   proto_put_str(&w, &tmp, lit(&tmp, s->bytes, s->len)));
        }
    }
    assert(proto_end(&w));
    uint8_t *cblock, *kblock;
    sc_arena ca = exact(&b0, &cblock), ka = exact(&b1, &kblock);
    result res;
    assert(result_init(&res, &book, 4) && result_set_columns(&res, &ca, 0) == RESULT_ADDED);
    assert(rows == 0 || result_add_chunk(&res, &ka, 1) == RESULT_ADDED);

    /* Fixed, well-formed answers for the reads before the fuzzed one. */
    catalog_read target = tr;
    if (kind != CATALOG_READ_TIMEOUT) {
        catalog_fail(&c, tr, &rec, c.error); /* the timeout is not needed */
        target = sr;
    }
    if (kind > CATALOG_READ_SESSION) {
        catalog_fail(&c, sr, &rec, c.error); /* nor the session */
        target = schr;
    }
    if (kind > CATALOG_READ_SCHEMAS) {
        /* schemas: one row, 2200 public */
        static alignas(8) uint8_t m0[512], m1[512], bk[1u << 17];
        sc_arena x0, x1, xb;
        sc_arena_init(&x0, m0, sizeof m0);
        sc_arena_init(&x1, m1, sizeof m1);
        sc_arena_init(&xb, bk, sizeof bk);
        proto_header hh = {PROTO_COLUMNS, 1, 1, 0, 0, 0};
        sc_arena_init(&tmp, tmp_mem, sizeof tmp_mem);
        assert(proto_begin(&w, &x0, hh) && proto_put_u64(&w, 2));
        assert(proto_put_str(&w, &tmp, lit(&tmp, (const uint8_t *)"a", 1)) && proto_put_u64(&w, 26));
        assert(proto_put_str(&w, &tmp, lit(&tmp, (const uint8_t *)"b", 1)) && proto_put_u64(&w, 19));
        assert(proto_end(&w));
        hh.kind = PROTO_CHUNK;
        hh.arg = 1;
        assert(proto_begin(&w, &x1, hh) && proto_put_u64(&w, 4) &&
               proto_put_str(&w, &tmp, lit(&tmp, (const uint8_t *)"2200", 4)) &&
               proto_put_u64(&w, 6) && proto_put_str(&w, &tmp, lit(&tmp, (const uint8_t *)"public", 6)) &&
               proto_end(&w));
        result sch;
        assert(result_init(&sch, &xb, 1) && result_set_columns(&sch, &x0, 0) == RESULT_ADDED &&
               result_add_chunk(&sch, &x1, 1) == RESULT_ADDED);
        assert(catalog_take(&c, schr, &sch) && catalog_expand(&c, 0));
        assert(catalog_next(&c, &rr) && catalog_next(&c, &fr));
        target = kind == CATALOG_READ_FUNCTIONS ? fr : rr;
        if (kind >= CATALOG_READ_COLUMNS) {
            /* relations: one row, 16400 t r -1 */
            sc_arena_init(&x0, m0, sizeof m0);
            sc_arena_init(&x1, m1, sizeof m1);
            sc_arena_init(&xb, bk, sizeof bk);
            hh.kind = PROTO_COLUMNS;
            hh.arg = 0;
            assert(proto_begin(&w, &x0, hh) && proto_put_u64(&w, 4));
            for (int k = 0; k < 4; k++) {
                assert(proto_put_str(&w, &tmp, lit(&tmp, (const uint8_t *)"a", 1)) && proto_put_u64(&w, 25));
            }
            assert(proto_end(&w));
            hh.kind = PROTO_CHUNK;
            hh.arg = 1;
            assert(proto_begin(&w, &x1, hh));
            const char *vals[] = {"16400", "t", "r", "-1"};
            for (int k = 0; k < 4; k++) {
                size_t n = strlen(vals[k]);
                assert(proto_put_u64(&w, n) && proto_put_str(&w, &tmp, lit(&tmp, (const uint8_t *)vals[k], n)));
            }
            assert(proto_end(&w));
            result rel;
            assert(result_init(&rel, &xb, 1) && result_set_columns(&rel, &x0, 0) == RESULT_ADDED &&
                   result_add_chunk(&rel, &x1, 1) == RESULT_ADDED);
            assert(catalog_take(&c, rr, &rel) && catalog_want_detail(&c, 2200, 16400));
            catalog_read cr, kr, ir;
            assert(catalog_next(&c, &cr) && catalog_next(&c, &kr) && catalog_next(&c, &ir));
            target = kind == CATALOG_READ_COLUMNS ? cr : kind == CATALOG_READ_CONSTRAINTS ? kr : ir;
        }
        if (kind == CATALOG_READ_DEFINITION) {
            assert(catalog_want_definition(&c, 16500) && catalog_next(&c, &target));
        }
    }
    dr = target;
    assert(dr.kind == kind);

    bool took = catalog_take(&c, target, &res);
    if (cols != widths[kind] || kind == CATALOG_READ_TIMEOUT) {
        assert(!took);
    }
    if (kind == CATALOG_READ_DEFINITION) {
        uint32_t oid = 0;
        bool ok = false;
        sc_str text;
        bool whole = cols == 1 && rows > 0;
        size_t total = 0;
        for (size_t i = 0; i < rows; i++) {
            whole = whole && !cells[i][0].null && !cells[i][0].cut;
            total += cells[i][0].len + (i > 0 ? 1u : 0u);
        }
        assert(took == whole);
        assert(catalog_take_definition(&c, &oid, &ok, &text) && oid == 16500 && ok == took);
        assert(in_arena(&defs, text) && (ok || text.len > 0));
        if (took) {
            assert(text.len == total);
            size_t at = 0;
            for (size_t i = 0; i < rows; i++) {
                assert(i == 0 || text.ptr[at++] == '\n');
                assert(cells[i][0].len == 0 || memcmp(text.ptr + at, cells[i][0].bytes, cells[i][0].len) == 0);
                at += cells[i][0].len;
            }
        }
        took = false;
    }
    if (took) {
        size_t si = 0, ri = 0;
        catalog_schema s;
        catalog_relation r;
        switch (kind) {
        case CATALOG_READ_SESSION:
            assert(rows == 1 && c.session.loaded);
            same_cell(&rec, c.session.version, 0, 1);
            same_cell(&rec, c.session.database, 0, 3);
            same_cell(&rec, c.session.user, 0, 4);
            break;
        case CATALOG_READ_SCHEMAS:
            assert(catalog_schema_count(&c) == rows);
            for (size_t i = 0; i < rows; i++) {
                assert(catalog_schema_at(&c, i, &s));
                same_cell(&rec, s.name, i, 1);
            }
            break;
        case CATALOG_READ_RELATIONS:
            assert(catalog_schema_at(&c, 0, &s));
            for (size_t i = 0; i < rows; i++) {
                assert(catalog_relation_at(&c, &s, i, &r));
                check_relation(&rec, &r, i);
            }
            assert(!catalog_relation_at(&c, &s, rows, &r));
            break;
        case CATALOG_READ_FUNCTIONS: {
            catalog_function f;
            assert(catalog_schema_at(&c, 0, &s));
            for (size_t i = 0; i < rows; i++) {
                assert(catalog_function_at(&c, &s, i, &f));
                same_cell(&rec, f.name, i, 1);
                same_cell(&rec, f.args, i, 3);
                same_cell(&rec, f.result, i, 4);
            }
            assert(!catalog_function_at(&c, &s, rows, &f));
            break;
        }
        default:
            assert(catalog_find_relation(&c, 2200, 16400, &si, &ri));
            assert(catalog_schema_at(&c, si, &s) && catalog_relation_at(&c, &s, ri, &r));
            for (size_t i = 0; i < rows; i++) {
                catalog_column col;
                catalog_constraint k;
                catalog_index x;
                if (kind == CATALOG_READ_COLUMNS) {
                    assert(catalog_column_at(&c, &r, i, &col));
                    same_cell(&rec, col.name, i, 1);
                    same_cell(&rec, col.type, i, 2);
                    same_cell(&rec, col.default_expr, i, 4);
                    assert(col.has_default == !cells[i][4].null);
                } else if (kind == CATALOG_READ_CONSTRAINTS) {
                    assert(catalog_constraint_at(&c, &r, i, &k));
                    same_cell(&rec, k.name, i, 0);
                    same_cell(&rec, k.def, i, 2);
                    same_cell(&rec, k.target, i, 4);
                    assert(!cells[i][3].cut && sc_tslice_len(k.columns) <= cells[i][3].len / 2u + 1u);
                    assert(sc_tslice_len(k.columns) == 0 || in_arena(&rec, k.columns.bytes));
                } else {
                    assert(catalog_index_at(&c, &r, i, &x));
                    same_cell(&rec, x.name, i, 0);
                    same_cell(&rec, x.def, i, 3);
                }
            }
            break;
        }
    }
    /* A restart drops the read: taking it again stores nothing. */
    assert(catalog_restart(&c) && !catalog_take(&c, dr, &res));
    free(cblock);
    free(kblock);
    return 0;
}
