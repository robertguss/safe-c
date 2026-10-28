/* Fuzzes the errno adapter. Input bytes become a path under a directory that
 * does not exist, so every open fails and none can block on a special file, and
 * a read length against /dev/zero. Every call must return a result with a
 * positive err on failure and a zeroed value. */
#include "../adapters/posix.c"
#include <assert.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_BYTES 8192
#define PREFIX "/nonexistent-sc-posix/"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    alignas(8) uint8_t storage[ARENA_BYTES];
    sc_arena a;
    sc_arena_init(&a, storage, sizeof storage);

    size_t n = size < 5000 ? size : 5000;
    sc_slice path = sc_arena_alloc(&a, sizeof PREFIX - 1 + n);
    if (path.ptr != NULL) {
        memcpy(path.ptr, PREFIX, sizeof PREFIX - 1);
        if (n > 0)
            memcpy(path.ptr + sizeof PREFIX - 1, data, n);
        sc_result_size o = sc_posix_open_read(&a, path);
        assert(!o.ok && o.err > 0 && o.value == 0);
    }

    sc_result_size z = sc_posix_open_read(&a, (sc_str){NULL, 0, a.epoch});
    assert(!z.ok && z.err == EINVAL);

    int fd = open("/dev/zero", O_RDONLY | O_CLOEXEC);
    assert(fd >= 0);
    size_t want = size > 0 ? (size_t)data[0] * 16 : 0;
    sc_slice dst = sc_arena_alloc(&a, want);
    sc_result_size r = sc_posix_read(&a, fd, dst);
    if (dst.len == 0) {
        assert(r.ok && r.value == 0);
    } else {
        assert(r.ok && r.value == dst.len);
        for (size_t i = 0; i < dst.len; i++)
            assert(dst.ptr[i] == 0);
    }
    assert(sc_posix_file_size(fd).err == EINVAL);
    assert(sc_posix_close(fd).ok);

    int name = size >= 5 ? (int)(((unsigned)data[1] << 8) | data[2]) : 0;
    sc_result_size s = sc_posix_sysconf(name);
    assert(s.ok ? s.err == 0 : (s.err > 0 && s.value == 0));
    assert(sc_arena_reset(&a));
    char dir[] = "/tmp/sc-posix-fuzz-XXXXXX";
    assert(mkdtemp(dir));
    char temp[128], target[128];
    snprintf(temp, sizeof temp, "%s/%02x.tmp", dir, size ? data[0] : 0);
    snprintf(target, sizeof target, "%s/history", dir);
    sc_str tp = sc_str_from_lit(&a, (const uint8_t *)temp, strlen(temp));
    sc_str dst_path = sc_str_from_lit(&a, (const uint8_t *)target, strlen(target));
    sc_str bytes = sc_str_from_lit(&a, data, n);
    sc_result_size out = sc_posix_open_write(&a, tp);
    assert(out.ok && sc_posix_open_write(&a, tp).err == EEXIST);
    sc_result_size wrote = sc_posix_write(&a, (int)out.value, bytes);
    assert(wrote.ok && wrote.value == n && sc_posix_sync((int)out.value).ok);
    assert(sc_posix_close((int)out.value).ok && sc_posix_rename(&a, tp, dst_path).ok);
    int reference = open(target, O_RDONLY);
    assert(reference >= 0);
    struct stat st;
    assert(fstat(reference, &st) == 0 && (st.st_mode & 0777) == 0600 && (size_t)st.st_size == n);
    uint8_t readback[5000];
    assert(read(reference, readback, sizeof readback) == (ssize_t)n && memcmp(readback, data, n) == 0);
    assert(close(reference) == 0 && sc_posix_unlink(&a, dst_path).ok && sc_posix_unlink(&a, tp).ok);
    assert(rmdir(dir) == 0);
    return 0;
}
