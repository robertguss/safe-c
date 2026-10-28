#include "conncfg.h"

sc_str cfg_trim(const sc_arena *a, sc_str s) {
    size_t start = 0, end = s.len;
    uint8_t c = 0;
    while (start < end && sc_slice_get(a, s, start, &c) && (c == ' ' || c == '\t' || c == '\r')) {
        start++;
    }
    while (end > start && sc_slice_get(a, s, end - 1u, &c) && (c == ' ' || c == '\t' || c == '\r')) {
        end--;
    }
    return sc_slice_sub(a, s, start, end - start);
}

bool cfg_line(const sc_arena *a, sc_str file, size_t *at, sc_str *line) {
    if (*at >= file.len) {
        return false;
    }
    size_t start = *at, end = file.len;
    bool comment = false;
    for (; *at < file.len; (*at)++) {
        uint8_t ch = 0;
        if (!sc_slice_get(a, file, *at, &ch)) {
            return false;
        }
        if (ch == '#' && !comment) {
            end = *at;
            comment = true;
        }
        if (ch == '\n') {
            if (!comment) end = *at;
            (*at)++;
            break;
        }
    }
    *line = cfg_trim(a, sc_slice_sub(a, file, start, end - start));
    return true;
}

bool cfg_pair(const sc_arena *a, sc_str line, conncfg_pair *out) {
    for (size_t i = 0; i < line.len; i++) {
        uint8_t ch = 0;
        if (!sc_slice_get(a, line, i, &ch)) return false;
        if (ch == '=') {
            conncfg_pair p = {cfg_trim(a, sc_slice_sub(a, line, 0, i)),
                              cfg_trim(a, sc_slice_sub(a, line, i + 1u, line.len - i - 1u))};
            if (p.key.len == 0) return false;
            *out = p;
            return true;
        }
    }
    return false;
}

static bool cfg_allowed(const sc_arena *a, sc_str key) {
    return SC_STR_EQ_LIT(a, key, "host") || SC_STR_EQ_LIT(a, key, "hostaddr") ||
           SC_STR_EQ_LIT(a, key, "port") || SC_STR_EQ_LIT(a, key, "dbname") ||
           SC_STR_EQ_LIT(a, key, "user") || SC_STR_EQ_LIT(a, key, "sslmode") ||
           SC_STR_EQ_LIT(a, key, "sslrootcert") || SC_STR_EQ_LIT(a, key, "options") ||
           SC_STR_EQ_LIT(a, key, "connect_timeout");
}

bool conncfg_at(const conncfg *cfg, size_t i, conncfg_entry *out) {
    return i < cfg->count && SC_TSLICE_GET(conncfg_entry, cfg->arena, cfg->entries, i, out);
}

static void cfg_bad(conncfg_entry *e, conncfg_error error, size_t line) {
    if (e->error == CFG_OK) {
        e->error = error;
        e->line = line;
    }
}

bool conncfg_parse(conncfg *cfg, const sc_arena *a, sc_str file, sc_arena *out) {
    conncfg z = {a, out, SC_TSLICE_ALLOC(conncfg_entry, out, CONNCFG_MAX), 0, 0, CFG_OK};
    *cfg = z;
    if (sc_tslice_len(cfg->entries) != CONNCFG_MAX) return false;
    if (file.len > CONNCFG_BYTES || !sc_utf8_valid(a, file)) {
        cfg->error = file.len > CONNCFG_BYTES ? CFG_LIMIT : CFG_UTF8;
        return true;
    }
    for (size_t i = 0; i < file.len; i++) {
        uint8_t ch = 0;
        if (!sc_slice_get(a, file, i, &ch) || ch == 0) {
            cfg->error = CFG_UTF8;
            return true;
        }
    }
    size_t at = 0, n = 0;
    sc_str line;
    while (cfg_line(a, file, &at, &line)) {
        n++;
        if (line.len == 0) continue;
        uint8_t ch = 0;
        if (!sc_slice_get(a, line, 0, &ch)) return false;
        if (ch == '[') {
            if (cfg->count == CONNCFG_MAX) {
                cfg->error = CFG_LIMIT;
                cfg->line = n;
                break;
            }
            conncfg_entry e = {line, SC_TSLICE_ALLOC(conncfg_pair, out, CONNCFG_KEYS), 0, n, CFG_OK};
            if (sc_tslice_len(e.pairs) != CONNCFG_KEYS) return false;
            uint8_t close = 0;
            if (line.len < 3 || !sc_slice_get(a, line, line.len - 1u, &close) || close != ']') {
                cfg_bad(&e, CFG_SECTION, n);
            } else {
                e.name = cfg_trim(a, sc_slice_sub(a, line, 1, line.len - 2u));
                if (e.name.len == 0) cfg_bad(&e, CFG_SECTION, n);
            }
            for (size_t i = 0; i < cfg->count; i++) {
                conncfg_entry prev;
                if (!conncfg_at(cfg, i, &prev)) return false;
                if (sc_str_eq(a, prev.name, e.name)) cfg_bad(&e, CFG_DUPLICATE, n);
            }
            if (!SC_TSLICE_SET(conncfg_entry, out, cfg->entries, cfg->count++, &e)) return false;
            continue;
        }
        if (cfg->count == 0) {
            cfg->error = CFG_SECTION;
            cfg->line = n;
            continue;
        }
        conncfg_entry e;
        conncfg_pair p;
        if (!conncfg_at(cfg, cfg->count - 1u, &e)) return false;
        if (!cfg_pair(a, line, &p)) {
            cfg_bad(&e, CFG_SYNTAX, n);
        } else if (SC_STR_EQ_LIT(a, p.key, "password")) {
            cfg_bad(&e, CFG_PASSWORD, n);
        } else if (!cfg_allowed(a, p.key)) {
            cfg_bad(&e, CFG_KEY, n);
        } else {
            for (size_t i = 0; i < e.count; i++) {
                conncfg_pair prev;
                if (!SC_TSLICE_GET(conncfg_pair, out, e.pairs, i, &prev)) return false;
                if (sc_str_eq(a, prev.key, p.key)) cfg_bad(&e, CFG_DUPLICATE, n);
            }
            if (e.error == CFG_OK && e.count < CONNCFG_KEYS &&
                !SC_TSLICE_SET(conncfg_pair, out, e.pairs, e.count++, &p)) return false;
        }
        if (!SC_TSLICE_SET(conncfg_entry, out, cfg->entries, cfg->count - 1u, &e)) return false;
    }
    return true;
}

bool conncfg_info(const conncfg *cfg, size_t i, sc_arena *out, sc_str *info) {
    conncfg_entry e;
    if (!conncfg_at(cfg, i, &e) || e.error != CFG_OK) return false;
    size_t cap = 0;
    for (size_t k = 0; k < e.count; k++) {
        conncfg_pair p;
        if (!SC_TSLICE_GET(conncfg_pair, cfg->arena, e.pairs, k, &p)) return false;
        cap += p.key.len + 2u * p.text.len + 4u;
    }
    sc_buf b = sc_buf_new(sc_arena_alloc(out, cap));
    if (b.dst.len != cap) return false;
    for (size_t k = 0; k < e.count; k++) {
        conncfg_pair p;
        if (!SC_TSLICE_GET(conncfg_pair, cfg->arena, e.pairs, k, &p) ||
            !sc_buf_str(out, &b, cfg->source, p.key) || !SC_BUF_LIT(out, &b, "='")) return false;
        for (size_t j = 0; j < p.text.len; j++) {
            uint8_t ch = 0;
            if (!sc_slice_get(cfg->source, p.text, j, &ch) ||
                ((ch == '\\' || ch == '\'') && !SC_BUF_LIT(out, &b, "\\")) ||
                !sc_buf_lit(out, &b, &ch, 1)) return false;
        }
        if (!SC_BUF_LIT(out, &b, "' ")) return false;
    }
    if (b.overflow) return false;
    *info = sc_buf_view(out, b);
    return true;
}

sc_str conncfg_message(sc_arena *a, conncfg_error error) {
    switch (error) {
    case CFG_PASSWORD: return SC_STR_LIT(a, "passwords are not stored; use ~/.pgpass");
    case CFG_KEY: return SC_STR_LIT(a, "unknown connection keyword");
    case CFG_DUPLICATE: return SC_STR_LIT(a, "duplicate name or keyword");
    case CFG_SECTION: return SC_STR_LIT(a, "expected [connection name]");
    case CFG_UTF8: return SC_STR_LIT(a, "file is not UTF-8 text");
    case CFG_LIMIT: return SC_STR_LIT(a, "at most 64 connections and 64 KiB");
    case CFG_SYNTAX: return SC_STR_LIT(a, "expected key = value");
    default: return (sc_str){NULL, 0, a->epoch};
    }
}
