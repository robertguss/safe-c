#include "store.h"
#include "safec/buf.h"
#include <errno.h>

sc_str store_path(sc_arena *a, const sc_arena *pa, sc_str dir, const uint8_t *name, size_t n) {
    sc_buf b = sc_buf_new(sc_arena_alloc(a, dir.len + n));
    if (!sc_buf_str(a, &b, pa, dir) || !sc_buf_lit(a, &b, name, n)) {
        return (sc_str){NULL, 0, a->epoch};
    }
    return sc_buf_view(a, b);
}

sc_result_size store_read(const sc_arena *pa, sc_str path, sc_arena *out, size_t cap, sc_str *text) {
    sc_result_size fd = sc_posix_open_read(pa, path);
    if (!fd.ok) return fd;
    sc_result_size size = sc_posix_file_size((int)fd.value);
    if (size.ok) {
        if (size.value > cap) {
            (void)sc_posix_close((int)fd.value);
            return sc_err_size(EFBIG);
        }
        sc_slice bytes = sc_arena_alloc(out, size.value);
        bool ok = bytes.len == size.value;
        size_t at = 0;
        while (ok && at < bytes.len) {
            sc_slice rest = sc_slice_sub(out, bytes, at, bytes.len - at);
            sc_result_size got = sc_posix_read(out, (int)fd.value, rest);
            if (got.ok && got.value > 0) at += got.value;
            else ok = false;
        }
        bool closed = sc_posix_close((int)fd.value).ok;
        if (!ok || !closed) return sc_err_size(EIO);
        *text = bytes;
        return sc_ok_size(at);
    }
    (void)sc_posix_close((int)fd.value);
    return sc_err_size(size.err);
}

bool store_save(const sc_arena *pa, sc_str path, sc_str temp, const sc_arena *a, sc_str text) {
    if (!sc_posix_unlink(pa, temp).ok) return false;
    sc_result_size fd = sc_posix_open_write(pa, temp);
    if (!fd.ok) return false;
    bool written = sc_posix_write(a, (int)fd.value, text).ok && sc_posix_sync((int)fd.value).ok;
    bool closed = sc_posix_close((int)fd.value).ok;
    bool ok = written && closed && sc_posix_rename(pa, temp, path).ok;
    if (!ok) (void)sc_posix_unlink(pa, temp);
    return ok;
}
