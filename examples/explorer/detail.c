#include "detail.h"
#include "safec/buf.h"

sc_str detail_type_name(sc_arena *a, uint32_t oid, bool *lost) {
    switch (oid) {
    case 16: return SC_STR_LIT(a, "boolean");
    case 17: return SC_STR_LIT(a, "bytea");
    case 18: return SC_STR_LIT(a, "char");
    case 19: return SC_STR_LIT(a, "name");
    case 20: return SC_STR_LIT(a, "bigint");
    case 21: return SC_STR_LIT(a, "smallint");
    case 23: return SC_STR_LIT(a, "integer");
    case 25: return SC_STR_LIT(a, "text");
    case 26: return SC_STR_LIT(a, "oid");
    case 114: return SC_STR_LIT(a, "json");
    case 700: return SC_STR_LIT(a, "real");
    case 701: return SC_STR_LIT(a, "double precision");
    case 1042: return SC_STR_LIT(a, "character");
    case 1043: return SC_STR_LIT(a, "character varying");
    case 1082: return SC_STR_LIT(a, "date");
    case 1083: return SC_STR_LIT(a, "time");
    case 1114: return SC_STR_LIT(a, "timestamp");
    case 1184: return SC_STR_LIT(a, "timestamp with time zone");
    case 1186: return SC_STR_LIT(a, "interval");
    case 1700: return SC_STR_LIT(a, "numeric");
    case 2950: return SC_STR_LIT(a, "uuid");
    case 3802: return SC_STR_LIT(a, "jsonb");
    default: {
        sc_buf b = sc_buf_new(sc_arena_alloc(a, 20));
        sc_str empty = {NULL, 0, a->epoch};
        if (!sc_buf_u64(a, &b, oid)) {
            *lost = true;
            return empty;
        }
        return sc_buf_view(a, b);
    }
    }
}

static bool put(sc_arena *la, sc_tslice lines, size_t *count, sc_str line) {
    if (*count >= sc_tslice_len(lines) || !SC_TSLICE_SET(sc_str, la, lines, *count, &line)) {
        return false;
    }
    (*count)++;
    return true;
}

bool detail_wrap(const sc_arena *sa, sc_str s, uint32_t width, detail_measure_fn m, void *ctx,
                 sc_arena *la, sc_tslice lines, size_t *count, bool *more) {
    *count = 0;
    *more = false;
    size_t start = 0, i = 0, space = 0; /* space: just past the line's last space, 0 for none */
    while (i < s.len) {
        sc_utf8_char c = sc_utf8_next(sa, s, i);
        if (c.len == 0) {
            return false; /* stale */
        }
        if (c.valid && c.cp == '\n') {
            if (!put(la, lines, count, sc_slice_sub(sa, s, start, i - start))) {
                *more = true;
                return true;
            }
            i += 1u;
            start = i;
            space = 0;
            continue;
        }
        /* Would this character pass the width? A line keeps at least one. */
        if (i > start && m(ctx, sa, sc_slice_sub(sa, s, start, i + c.len - start)) > width) {
            /* Break after the line's last space when it has one, else here. */
            size_t cut = space > start ? space : i;
            if (!put(la, lines, count, sc_slice_sub(sa, s, start, cut - start))) {
                *more = true;
                return true;
            }
            start = cut;
            space = 0;
        }
        if (c.valid && c.cp == ' ') {
            space = i + 1u;
        }
        i += c.len;
    }
    if (start < s.len || *count == 0 || s.len == 0) {
        sc_str rest = start < s.len ? sc_slice_sub(sa, s, start, s.len - start)
                                    : (sc_str){NULL, 0, s.epoch};
        if (!put(la, lines, count, rest)) {
            *more = true;
        }
    }
    return true;
}

sc_str detail_note(sc_arena *a, proto_cell c, bool *lost) {
    sc_str empty = {NULL, 0, a->epoch};
    if (c.null) {
        return SC_STR_LIT(a, "NULL");
    }
    if (!c.truncated) {
        return empty;
    }
    sc_buf b = sc_buf_new(sc_arena_alloc(a, 64));
    bool ok = sc_buf_lit(a, &b, (const uint8_t *)"truncated (", 11) && sc_buf_u64_sep(a, &b, c.len) &&
              sc_buf_lit(a, &b, (const uint8_t *)" bytes)", 7);
    if (!ok) {
        *lost = true;
        return empty;
    }
    return sc_buf_view(a, b);
}
