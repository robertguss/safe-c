#include "safec/buf.h"

/*@ requires valid_arena(a) && slice_storage(a, s);
    assigns \nothing;
    ensures \result <==> valid_slice(a, s);
*/
static bool buf_slice_live(const sc_arena *a, sc_slice s) {
    return s.epoch == a->epoch &&
           (s.len == 0 || (s.len <= a->used && s.ptr >= a->base &&
                          s.ptr <= a->base + (a->used - s.len)));
}

/*@ requires valid_arena(a) && \valid_read(b) && buf_shape(a, *b);
    assigns \nothing;
    ensures \result <==> buf_live(a, *b) && n <= b->dst.len - b->len;
*/
static bool buf_room(const sc_arena *a, const sc_buf *b, size_t n) {
    return !b->overflow && n <= b->dst.len - b->len && buf_slice_live(a, b->dst);
}

/*@ assigns \nothing;
    ensures \result == dec_len(v) && 1 <= \result <= 20;
*/
static size_t dec_count(uint64_t v) {
    size_t n = 1;
    uint64_t w = v;
    /*@ loop invariant 1 <= n <= 20;
        loop invariant pow10(n - 1) >= 1;
        loop invariant w == v / pow10(n - 1);
        loop invariant dec_len(v) == n - 1 + dec_len(w);
        loop invariant n == 20 ==> w < 10;
        loop assigns n, w;
        loop variant w;
    */
    while (w >= 10) {
        //@ assert w / 10 == v / pow10(n);
        w /= 10;
        n++;
    }
    return n;
}

/*@ requires n == dec_len(v) && 1 <= n <= 20;
    requires \valid(p + (0 .. n - 1));
    assigns p[0 .. n - 1];
    ensures dec_at(p, v);
*/
static void dec_write(uint8_t *p, size_t n, uint64_t v) {
    uint64_t d = 1;
    /*@ loop invariant 0 <= j <= n;
        loop invariant j < n ==> d == pow10(j);
        loop invariant \forall integer m; 0 <= m < j ==> p[n - 1 - m] == dec_digit(v, m);
        loop assigns j, d, p[0 .. n - 1];
        loop variant n - j;
    */
    for (size_t j = 0; j < n; j++) {
        p[n - 1 - j] = (uint8_t)('0' + v / d % 10);
        if (j + 1 < n)
            d *= 10;
    }
}

/*@ requires n == dec_len(v) && 1 <= n <= 20 && len == n + (n - 1) / 3;
    requires \valid(p + (0 .. len - 1));
    assigns p[0 .. len - 1];
    ensures sep_at(p, v);
*/
static void sep_write(uint8_t *p, size_t len, size_t n, uint64_t v) {
    uint64_t d = 1;
    size_t m = 0;
    /*@ loop invariant 0 <= r <= len;
        loop invariant m == r - r / 4;
        loop invariant m < n ==> d == pow10(m);
        loop invariant \forall integer q; 0 <= q < r ==>
          (q % 4 == 3 ? p[len - 1 - q] == ',' : p[len - 1 - q] == dec_digit(v, q - q / 4));
        loop assigns r, m, d, p[0 .. len - 1];
        loop variant len - r;
    */
    for (size_t r = 0; r < len; r++) {
        if (r % 4 == 3) {
            p[len - 1 - r] = ',';
        } else {
            p[len - 1 - r] = (uint8_t)('0' + v / d % 10);
            m++;
            if (m < n)
                d *= 10;
        }
    }
}

sc_buf sc_buf_new(sc_slice dst) {
    sc_buf b = {dst, 0, false};
    return b;
}

sc_str sc_buf_view(const sc_arena *a, sc_buf b) {
    sc_str empty = {NULL, 0, a->epoch};
    if (b.len == 0 || !buf_slice_live(a, b.dst))
        return empty;
    sc_str s = {b.dst.ptr, b.len, b.dst.epoch};
    return s;
}

bool sc_buf_lit(const sc_arena *a, sc_buf *b, const uint8_t *lit, size_t n) {
    if (!buf_room(a, b, n)) {
        b->overflow = true;
        return false;
    }
    uint8_t *p = b->dst.ptr + b->len;
    /*@ loop invariant 0 <= i <= n;
        loop invariant \forall integer k; 0 <= k < i ==> p[k] == lit[k];
        loop assigns i, p[0 .. n - 1];
        loop variant n - i;
    */
    for (size_t i = 0; i < n; i++)
        p[i] = lit[i];
    b->len += n;
    return true;
}

bool sc_buf_str(const sc_arena *a, sc_buf *b, const sc_arena *sa, sc_str s) {
    if (!buf_slice_live(sa, s) || !buf_room(a, b, s.len)) {
        b->overflow = true;
        return false;
    }
    uint8_t *p = b->dst.ptr + b->len;
    /*@ loop invariant 0 <= i <= s.len;
        loop invariant \forall integer k; 0 <= k < i ==> p[k] == s.ptr[k];
        loop assigns i, p[0 .. s.len - 1];
        loop variant s.len - i;
    */
    for (size_t i = 0; i < s.len; i++)
        p[i] = s.ptr[i];
    b->len += s.len;
    return true;
}

bool sc_buf_u64(const sc_arena *a, sc_buf *b, uint64_t v) {
    size_t n = dec_count(v);
    if (!buf_room(a, b, n)) {
        b->overflow = true;
        return false;
    }
    dec_write(b->dst.ptr + b->len, n, v);
    b->len += n;
    return true;
}

bool sc_buf_u64_sep(const sc_arena *a, sc_buf *b, uint64_t v) {
    size_t n = dec_count(v);
    size_t len = n + (n - 1) / 3;
    if (!buf_room(a, b, len)) {
        b->overflow = true;
        return false;
    }
    sep_write(b->dst.ptr + b->len, len, n, v);
    b->len += len;
    return true;
}

bool sc_buf_ms(const sc_arena *a, sc_buf *b, uint64_t ms) {
    uint64_t tenths = ms / 100 + (ms % 100 >= 50 ? 1u : 0u);
    uint64_t secs = ms / 1000 + (ms % 1000 >= 500 ? 1u : 0u);
    uint64_t head = ms < 1000 ? ms : tenths < 600 ? tenths / 10 : secs / 60;
    size_t n = dec_count(head);
    size_t r = ms < 1000 || tenths < 600 ? 0 : dec_count(secs % 60);
    size_t len = ms < 1000 ? n + 3 : tenths < 600 ? n + 4 : n + 5 + r + 2;
    //@ assert tenths == ms_tenths(ms) && secs == ms_secs(ms);
    //@ assert len == ms_len(ms);
    if (!buf_room(a, b, len)) {
        b->overflow = true;
        return false;
    }
    uint8_t *p = b->dst.ptr + b->len;
    dec_write(p, n, head);
    if (ms < 1000) {
        p[n] = ' ';
        p[n + 1] = 'm';
        p[n + 2] = 's';
        //@ assert dec_at(p, ms);
        //@ assert ms_at(p, ms);
    } else if (tenths < 600) {
        p[n] = '.';
        p[n + 1] = (uint8_t)('0' + tenths % 10);
        p[n + 2] = ' ';
        p[n + 3] = 's';
        //@ assert dec_at(p, ms_tenths(ms) / 10);
        //@ assert ms_at(p, ms);
    } else {
        p[n] = ' ';
        p[n + 1] = 'm';
        p[n + 2] = 'i';
        p[n + 3] = 'n';
        p[n + 4] = ' ';
        dec_write(p + n + 5, r, secs % 60);
        p[n + 5 + r] = ' ';
        p[n + 6 + r] = 's';
        //@ assert dec_at(p, ms_secs(ms) / 60);
        //@ assert dec_at(p + (n + 5), ms_secs(ms) % 60);
        //@ assert ms_at(p, ms);
    }
    b->len += len;
    return true;
}
