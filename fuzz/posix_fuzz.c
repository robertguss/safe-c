/* Fuzzes the errno adapter. Input bytes become a path under a directory that
 * does not exist, so every open fails and none can block on a special file, and
 * a read length against /dev/zero. Every call must return a result with a
 * positive err on failure and a zeroed value. */
#include "../adapters/posix.c"
#include <assert.h>
#include <stdalign.h>
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
    return 0;
}
