/* Tests the errno adapter (adapters/posix.c). Adapters are not in the library,
 * so the test builds the adapter in directly. */
#include "../adapters/posix.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_BYTES (1u << 16)

static alignas(8) uint8_t storage[ARENA_BYTES];
static sc_arena a;

static sc_str str_of(const char *text) {
    return sc_str_from_lit(&a, (const uint8_t *)text, strlen(text));
}

static void expect_err(sc_result_size r, int err) {
    assert(!r.ok && r.err == err && r.value == 0);
}

static char tmpl[] = "/tmp/sc-posix-test-XXXXXX";

static void open_errors(void) {
    expect_err(sc_posix_open_read(&a, str_of("/nonexistent-sc-posix")), ENOENT);
    expect_err(sc_posix_open_read(&a, str_of("/nonexistent-sc-posix/x")), ENOENT);
    expect_err(sc_posix_open_read(&a, (sc_str){NULL, 0, a.epoch}), EINVAL);

    uint8_t nul[] = {'/', 't', 0, 'x'};
    sc_str with_nul = sc_str_from_lit(&a, nul, sizeof nul);
    expect_err(sc_posix_open_read(&a, with_nul), EINVAL);

    sc_slice big = sc_arena_alloc(&a, PATH_MAX);
    assert(big.ptr != NULL);
    memset(big.ptr, 'a', big.len);
    expect_err(sc_posix_open_read(&a, big), ENAMETOOLONG);
    sc_slice fits = sc_slice_sub(&a, big, 0, PATH_MAX - 1);
    /* Under the limit it reaches the kernel, which rejects the 4095-byte name itself. */
    sc_result_size r = sc_posix_open_read(&a, fits);
    assert(!r.ok && r.err == ENAMETOOLONG);

    sc_str stale = str_of("/dev/null");
    assert(sc_arena_reset(&a));
    expect_err(sc_posix_open_read(&a, stale), EINVAL);
}

static void file_round_trip(void) {
    int fd = mkstemp(tmpl);
    assert(fd >= 0);
    static const char text[] = "hello, errno";
    assert(write(fd, text, sizeof text - 1) == (ssize_t)(sizeof text - 1));
    assert(close(fd) == 0);

    sc_result_size o = sc_posix_open_read(&a, str_of(tmpl));
    assert(o.ok && o.err == 0);
    int in = (int)o.value;

    sc_result_size size = sc_posix_file_size(in);
    assert(size.ok && size.value == sizeof text - 1);

    sc_slice dst = sc_arena_alloc(&a, 64);
    assert(dst.ptr != NULL);
    sc_result_size got = sc_posix_read(&a, in, dst);
    assert(got.ok && got.value == sizeof text - 1);
    assert(memcmp(dst.ptr, text, sizeof text - 1) == 0);

    got = sc_posix_read(&a, in, dst);
    assert(got.ok && got.value == 0); /* end of file */

    sc_result_size none = sc_posix_read(&a, in, (sc_slice){NULL, 0, a.epoch});
    assert(none.ok && none.value == 0);

    sc_slice stale = dst;
    assert(sc_arena_reset(&a));
    expect_err(sc_posix_read(&a, in, stale), EINVAL);

    sc_result_size c = sc_posix_close(in);
    assert(c.ok && c.value == 0);
    expect_err(sc_posix_close(in), EBADF);
    expect_err(sc_posix_file_size(in), EBADF);
    assert(unlink(tmpl) == 0);
}

static void read_errors(void) {
    sc_slice dst = sc_arena_alloc(&a, 16);
    assert(dst.ptr != NULL);
    expect_err(sc_posix_read(&a, -1, dst), EBADF);

    /* Reading a directory fails with EISDIR, and a directory has no file size. */
    sc_result_size d = sc_posix_open_read(&a, str_of("/tmp"));
    assert(d.ok);
    expect_err(sc_posix_read(&a, (int)d.value, dst), EISDIR);
    expect_err(sc_posix_file_size((int)d.value), EINVAL);
    assert(sc_posix_close((int)d.value).ok);
}

static void sysconf_cases(void) {
    sc_result_size page = sc_posix_sysconf(_SC_PAGESIZE);
    assert(page.ok && page.value >= 512 && (page.value & (page.value - 1)) == 0);
    expect_err(sc_posix_sysconf(-1), EINVAL);
}

static void pipe_read(void) {
    /* Bytes arrive, then end of file, from a pipe. */
    int p[2];
    assert(pipe(p) == 0);
    assert(write(p[1], "x", 1) == 1);
    assert(close(p[1]) == 0);
    sc_slice dst = sc_arena_alloc(&a, 8);
    sc_result_size got = sc_posix_read(&a, p[0], dst);
    assert(got.ok && got.value == 1 && dst.ptr[0] == 'x');
    got = sc_posix_read(&a, p[0], dst);
    assert(got.ok && got.value == 0);
    assert(close(p[0]) == 0);
}

int main(void) {
    sc_arena_init(&a, storage, sizeof storage);
    open_errors();
    assert(sc_arena_reset(&a));
    file_round_trip();
    assert(sc_arena_reset(&a));
    read_errors();
    sysconf_cases();
    pipe_read();
    puts("posix_test: ok");
    return 0;
}
