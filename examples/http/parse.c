#include "parse.h"

typedef enum { LINE_OK, LINE_BAD, LINE_LONG, LINE_PARTIAL } line_status;

/* Finds the CRLF that ends the line starting at pos. On LINE_OK, *len is the
 * line's length without the CRLF. More than HTTP_MAX_LINE bytes before the
 * break is LINE_LONG, a bare CR or LF is LINE_BAD, and running out of bytes
 * first is LINE_PARTIAL. */
static line_status scan_line(const sc_arena *a, sc_str s, size_t pos, size_t *len) {
    for (size_t i = pos; i < s.len; i++) {
        if (i - pos > HTTP_MAX_LINE)
            return LINE_LONG;
        uint8_t b = 0;
        if (!sc_slice_get(a, s, i, &b) || b == '\n')
            return LINE_BAD;
        if (b == '\r') {
            uint8_t next = 0;
            if (i + 1 >= s.len)
                return LINE_PARTIAL;
            if (!sc_slice_get(a, s, i + 1, &next) || next != '\n')
                return LINE_BAD;
            *len = i - pos;
            return LINE_OK;
        }
    }
    size_t seen = s.len;
    seen -= pos;
    if (seen > HTTP_MAX_LINE)
        return LINE_LONG;
    return LINE_PARTIAL;
}

static bool is_control(uint8_t b) {
    return b < 0x20 || b == 0x7f;
}

static bool is_upper(uint8_t b) {
    return b >= 'A' && b <= 'Z';
}

static bool is_token(uint8_t b) {
    return (b >= 'a' && b <= 'z') || is_upper(b) || (b >= '0' && b <= '9') || b == '!' ||
           b == '#' || b == '$' || b == '%' || b == '&' || b == '\'' || b == '*' || b == '+' ||
           b == '-' || b == '.' || b == '^' || b == '_' || b == '`' || b == '|' || b == '~';
}

static bool is_blank(uint8_t b) {
    return b == ' ' || b == '\t';
}

static uint8_t lower(uint8_t b) {
    return is_upper(b) ? (uint8_t)(b + 32) : b;
}

/* Finds the first occurrence of byte c in s[0, len). */
static bool find_byte(const sc_arena *a, sc_str s, size_t len, uint8_t c, size_t *at) {
    for (size_t i = 0; i < len; i++) {
        uint8_t b = 0;
        if (!sc_slice_get(a, s, i, &b))
            return false;
        if (b == c) {
            *at = i;
            return true;
        }
    }
    return false;
}

/* Every byte of s satisfies pred. An empty s does not. */
static bool all_bytes(const sc_arena *a, sc_str s, bool (*pred)(uint8_t)) {
    if (s.len == 0)
        return false;
    for (size_t i = 0; i < s.len; i++) {
        uint8_t b = 0;
        if (!sc_slice_get(a, s, i, &b) || !pred(b))
            return false;
    }
    return true;
}

static bool is_target_byte(uint8_t b) {
    return !is_control(b) && b != ' ';
}

/* Splits one header line s[0, len) into its name and trimmed value. */
static bool split_header(const sc_arena *a, sc_str s, size_t len, sc_str *name, sc_str *value) {
    size_t colon = 0;
    if (!find_byte(a, s, len, ':', &colon))
        return false;
    sc_str n = sc_slice_sub(a, s, 0, colon);
    if (!all_bytes(a, n, is_token))
        return false;
    size_t start = colon + 1;
    size_t end = len;
    uint8_t b = 0;
    while (start < end && sc_slice_get(a, s, start, &b) && is_blank(b))
        start++;
    while (end > start && sc_slice_get(a, s, end - 1, &b) && is_blank(b))
        end--;
    sc_str v = sc_slice_sub(a, s, start, end - start);
    for (size_t i = 0; i < v.len; i++) {
        if (!sc_slice_get(a, v, i, &b) || (is_control(b) && b != '\t'))
            return false;
    }
    *name = n;
    *value = v;
    return true;
}

/* Equal ignoring ASCII case. */
static bool name_eq(const sc_arena *a, sc_str x, sc_str y) {
    if (x.len != y.len)
        return false;
    for (size_t i = 0; i < x.len; i++) {
        uint8_t p = 0;
        uint8_t q = 0;
        if (!sc_slice_get(a, x, i, &p) || !sc_slice_get(a, y, i, &q) || lower(p) != lower(q))
            return false;
    }
    return true;
}

static unsigned line_error(line_status st, unsigned long_status) {
    return st == LINE_LONG ? long_status : 400;
}

unsigned http_parse(sc_arena *a, sc_str head, http_request *out) {
    sc_str v11 = SC_STR_LIT(a, "HTTP/1.1");
    sc_str v10 = SC_STR_LIT(a, "HTTP/1.0");
    if (v11.len == 0 || v10.len == 0)
        return 500;
    if (head.len > HTTP_MAX_HEAD)
        return 431;

    size_t len = 0;
    line_status st = scan_line(a, head, 0, &len);
    if (st == LINE_PARTIAL)
        return head.len >= HTTP_MAX_HEAD ? 431 : 400;
    if (st != LINE_OK)
        return line_error(st, 414);

    /* method SP target SP version */
    size_t request_line = len + 2;
    sc_str line = sc_slice_sub(a, head, 0, len);
    size_t sp1 = 0;
    if (!find_byte(a, line, len, ' ', &sp1))
        return 400;
    sc_str rest = sc_slice_sub(a, line, sp1 + 1, len - sp1 - 1);
    size_t sp2 = 0;
    if (!find_byte(a, rest, rest.len, ' ', &sp2))
        return 400;
    http_request r;
    r.method = sc_slice_sub(a, line, 0, sp1);
    r.target = sc_slice_sub(a, rest, 0, sp2);
    r.version = sc_slice_sub(a, rest, sp2 + 1, rest.len - sp2 - 1);
    uint8_t first = 0;
    if (!all_bytes(a, r.method, is_upper) || !all_bytes(a, r.target, is_target_byte) ||
        !sc_slice_get(a, r.target, 0, &first) || first != '/' ||
        !(sc_str_eq(a, r.version, v11) || sc_str_eq(a, r.version, v10)))
        return 400;

    /* headers, then the blank line that ends the head */
    size_t pos = request_line;
    r.header_count = 0;
    for (;;) {
        st = scan_line(a, head, pos, &len);
        if (st == LINE_PARTIAL)
            return head.len >= HTTP_MAX_HEAD ? 431 : 400;
        if (st != LINE_OK)
            return line_error(st, 431);
        if (len == 0)
            break;
        sc_str line_s = sc_slice_sub(a, head, pos, len);
        sc_str name = {NULL, 0, 0};
        sc_str value = {NULL, 0, 0};
        if (!split_header(a, line_s, len, &name, &value))
            return 400;
        r.header_count++;
        if (r.header_count > HTTP_MAX_HEADERS)
            return 431;
        pos += len + 2;
    }
    if (pos + 2 != head.len)
        return 400;
    r.headers = sc_slice_sub(a, head, request_line, pos - request_line);
    *out = r;
    return 200;
}

bool http_header(const sc_arena *a, const http_request *r, sc_str name, sc_str *value) {
    size_t pos = 0;
    while (pos < r->headers.len) {
        size_t len = 0;
        if (scan_line(a, r->headers, pos, &len) != LINE_OK)
            return false;
        sc_str line = sc_slice_sub(a, r->headers, pos, len);
        sc_str n = {NULL, 0, 0};
        sc_str v = {NULL, 0, 0};
        if (!split_header(a, line, len, &n, &v))
            return false;
        if (name_eq(a, n, name)) {
            *value = v;
            return true;
        }
        pos += len + 2;
    }
    return false;
}
