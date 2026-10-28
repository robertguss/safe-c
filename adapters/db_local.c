#define _XOPEN_SOURCE 700
#include "sc_local.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void sc_local_text_bytes(sc_local_text *out, const char *s, size_t n) {
    if (n >= (UINT64_C(1) << 62) - out->len) {
        out->failed = true;
        return;
    }
    size_t keep = out->len < out->cap ? out->cap - out->len : 0;
    if (keep > n) keep = n;
    if (keep > 0) memcpy(out->bytes + out->len, s, keep);
    out->len += n;
}

void sc_local_text_cstr(sc_local_text *out, const char *s) {
    sc_local_text_bytes(out, s, strlen(s));
}

void sc_local_text_hex(sc_local_text *out, const uint8_t *s, size_t n) {
    sc_local_text_cstr(out, "X'");
    static const char hex[] = "0123456789ABCDEF";
    size_t count = out->len < out->cap ? (out->cap - out->len + 1) / 2 : 0;
    if (count > n) count = n;
    for (size_t i = 0; i < count; i++) {
        char pair[] = {hex[s[i] >> 4], hex[s[i] & 15]};
        sc_local_text_bytes(out, pair, 2);
    }
    if (n - count > (SIZE_MAX - out->len) / 2) out->failed = true;
    else out->len += (n - count) * 2;
    sc_local_text_cstr(out, "'");
}

char *sc_local_copy(const sc_arena *a, sc_str s) {
    sc_slice bytes = sc_slice_sub(a, s, 0, s.len);
    if (bytes.len != s.len || s.len == SIZE_MAX ||
        (s.len > 0 && memchr(bytes.ptr, 0, bytes.len) != NULL)) return NULL;
    char *copy = malloc(s.len + 1);
    if (copy == NULL) return NULL;
    if (s.len > 0) memcpy(copy, bytes.ptr, s.len);
    copy[s.len] = 0;
    return copy;
}

void sc_local_error(sc_local *s, const char *code, const char *message) {
    snprintf(s->code, sizeof s->code, "%s", code);
    snprintf(s->error, sizeof s->error, "%s", message == NULL ? "Unknown engine error" : message);
}

bool sc_local_path(sc_local *s, const sc_arena *a, sc_str path) {
    char *input = sc_local_copy(a, path);
    if (input == NULL || input[0] == 0 || strncmp(input, "file:", 5) == 0 ||
        strcmp(input, ":memory:") == 0 || strstr(input, "://") != NULL) {
        free(input);
        sc_local_error(s, "File", "Choose an existing regular database file, not a URI or memory database.");
        return false;
    }
    s->path = realpath(input, NULL);
    free(input);
    if (s->path == NULL) {
        sc_local_error(s, "File", strerror(errno));
        return false;
    }
    struct stat st;
    if (stat(s->path, &st) != 0) {
        sc_local_error(s, "File", strerror(errno));
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        sc_local_error(s, "File", "The selected path is not a regular file.");
        return false;
    }
    return true;
}

void sc_local_clear_columns(sc_local *s) {
    for (size_t i = 0; i < s->cols && i < SC_LOCAL_COLUMNS; i++) {
        free(s->columns[i].name);
        free(s->columns[i].type);
    }
    free(s->columns);
    s->columns = NULL;
    s->cols = 0;
    s->rows = 0;
}

bool sc_local_reject_wide_schema(sc_local *s, size_t n) {
    if (n <= SC_LOCAL_COLUMNS) return false;
    sc_local_error(s, "Explorer", "The result exceeds Explorer's 1,664-column limit.");
    return true;
}

void sc_local_free(sc_local *s) {
    sc_local_clear_columns(s);
    free(s->path);
    free(s->sql);
}

bool sc_local_u64(const sc_arena *a, sc_buf *b, uint64_t n) {
    uint8_t bytes[8];
    for (size_t i = 0; i < 8; i++) bytes[i] = (uint8_t)(n >> (8 * i));
    return sc_buf_lit(a, b, bytes, sizeof bytes);
}

bool sc_local_string(const sc_arena *a, sc_buf *b, const char *s, size_t n) {
    return sc_local_u64(a, b, n) && (n == 0 || sc_buf_lit(a, b, (const uint8_t *)s, n));
}

bool sc_local_columns(sc_local *s, const sc_arena *a, sc_buf *b) {
    sc_buf before = *b;
    bool ok = s->cols <= SC_LOCAL_COLUMNS && sc_local_u64(a, b, s->cols);
    for (size_t i = 0; ok && i < s->cols; i++) {
        sc_local_column *c = &s->columns[i];
        ok = sc_local_string(a, b, c->name, strlen(c->name)) &&
             sc_local_string(a, b, c->type, strlen(c->type)) && sc_local_u64(a, b, c->numeric);
    }
    if (!ok) *b = before;
    return ok;
}

bool sc_local_put_error(sc_local *s, const sc_arena *a, sc_buf *b) {
    sc_buf before = *b;
    bool ok = sc_local_string(a, b, s->code, strlen(s->code)) &&
              sc_local_string(a, b, s->error, strlen(s->error)) &&
              sc_local_string(a, b, "", 0) && sc_local_string(a, b, "", 0) && sc_local_u64(a, b, s->position);
    if (!ok) *b = before;
    return ok;
}

bool sc_local_cell(const sc_arena *a, sc_buf *b, sc_local_text *text, bool null) {
    if (text->failed || text->len >= (UINT64_C(1) << 62)) return false;
    size_t keep = text->len < text->cap ? text->len : text->cap;
    uint64_t flags = null ? SC_DB_CELL_NULL :
                     text->len | (keep < text->len ? SC_DB_CELL_TRUNCATED : 0);
    return sc_local_u64(a, b, flags) && sc_local_string(a, b, text->bytes, null ? 0 : keep);
}
