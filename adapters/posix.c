/* errno adapter (subset rule R0). It makes the POSIX calls application code
 * would otherwise make itself, saves errno right after each one, and hands back
 * an sc_result_size. Unsafe C stays here: the path copy and the raw read buffer. */
#define _POSIX_C_SOURCE 200809L
#include "sc_posix.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* The bytes of `path` as a C string in `buf`, or the errno that rejects it. */
static int path_to_cstr(const sc_arena *a, sc_str path, char *buf) {
    if (path.len == 0)
        return EINVAL;
    if (path.len >= PATH_MAX)
        return ENAMETOOLONG;
    sc_slice live = sc_slice_sub(a, path, 0, path.len);
    if (live.ptr == NULL)
        return EINVAL;
    for (size_t i = 0; i < live.len; i++) {
        if (live.ptr[i] == 0)
            return EINVAL;
        buf[i] = (char)live.ptr[i];
    }
    buf[live.len] = 0;
    return 0;
}

sc_result_size sc_posix_open_read(const sc_arena *a, sc_str path) {
    char buf[PATH_MAX];
    int bad = path_to_cstr(a, path, buf);
    if (bad != 0)
        return sc_err_size(bad);
    int fd;
    do {
        fd = open(buf, O_RDONLY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0)
        return sc_err_size(errno);
    return sc_ok_size((size_t)fd);
}

sc_result_size sc_posix_read(const sc_arena *a, int fd, sc_slice dst) {
    if (dst.len == 0)
        return sc_ok_size(0);
    sc_slice live = sc_slice_sub(a, dst, 0, dst.len);
    if (live.ptr == NULL)
        return sc_err_size(EINVAL);
    size_t want = live.len < (size_t)SSIZE_MAX ? live.len : (size_t)SSIZE_MAX;
    ssize_t got;
    do {
        got = read(fd, live.ptr, want);
    } while (got < 0 && errno == EINTR);
    if (got < 0)
        return sc_err_size(errno);
    return sc_ok_size((size_t)got);
}

sc_result_size sc_posix_file_size(int fd) {
    struct stat st;
    if (fstat(fd, &st) != 0)
        return sc_err_size(errno);
    if (!S_ISREG(st.st_mode) || st.st_size < 0)
        return sc_err_size(EINVAL);
    return sc_ok_size((size_t)st.st_size);
}

sc_result_size sc_posix_close(int fd) {
    if (close(fd) != 0)
        return sc_err_size(errno);
    return sc_ok_size(0);
}

sc_result_size sc_posix_sysconf(int name) {
    errno = 0;
    long v = sysconf(name);
    if (v >= 0)
        return sc_ok_size((size_t)v);
    return sc_err_size(errno != 0 ? errno : ENOTSUP);
}

sc_result_size sc_posix_open_write(const sc_arena *a, sc_str path) {
    char buf[PATH_MAX];
    int bad = path_to_cstr(a, path, buf);
    if (bad != 0) return sc_err_size(bad);
    int fd;
    do {
        fd = open(buf, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    } while (fd < 0 && errno == EINTR);
    return fd < 0 ? sc_err_size(errno) : sc_ok_size((size_t)fd);
}

sc_result_size sc_posix_write(const sc_arena *a, int fd, sc_slice bytes) {
    if (bytes.epoch != a->epoch) return sc_err_size(EINVAL);
    sc_slice live = sc_slice_sub(a, bytes, 0, bytes.len);
    if (live.len != bytes.len) return sc_err_size(EINVAL);
    size_t at = 0;
    while (at < live.len) {
        size_t n = live.len - at < (size_t)SSIZE_MAX ? live.len - at : (size_t)SSIZE_MAX;
        ssize_t wrote = write(fd, live.ptr + at, n);
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote <= 0) return sc_err_size(wrote == 0 ? EIO : errno);
        at += (size_t)wrote;
    }
    return sc_ok_size(at);
}

sc_result_size sc_posix_sync(int fd) {
    int rc;
    do { rc = fsync(fd); } while (rc != 0 && errno == EINTR);
    return rc == 0 ? sc_ok_size(0) : sc_err_size(errno);
}

sc_result_size sc_posix_rename(const sc_arena *a, sc_str from, sc_str to) {
    char src[PATH_MAX], dst[PATH_MAX];
    int bad = path_to_cstr(a, from, src);
    if (bad == 0) bad = path_to_cstr(a, to, dst);
    if (bad != 0) return sc_err_size(bad);
    return rename(src, dst) == 0 ? sc_ok_size(0) : sc_err_size(errno);
}

sc_result_size sc_posix_unlink(const sc_arena *a, sc_str path) {
    char buf[PATH_MAX];
    int bad = path_to_cstr(a, path, buf);
    if (bad != 0) return sc_err_size(bad);
    if (unlink(buf) == 0 || errno == ENOENT) return sc_ok_size(0);
    return sc_err_size(errno);
}
