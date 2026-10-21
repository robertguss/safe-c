#include <sc_posix.h>

/* Mo-shaped file and OS calls with no errno: diag.c's open, fstat and read, and
 * stack.c's sysconf. A failure is the result's err, never a read of errno. */
static sc_result_size load(const sc_arena *a, sc_str path, sc_slice dst) {
    sc_result_size opened = sc_posix_open_read(a, path);
    if (!opened.ok) {
        return opened;
    }
    int fd = (int)sc_result_size_unwrap(opened);
    sc_result_size size = sc_posix_file_size(fd);
    sc_result_size got = sc_posix_read(a, fd, dst);
    sc_result_size closed = sc_posix_close(fd);
    if (!size.ok) {
        return size;
    }
    if (!closed.ok) {
        return closed;
    }
    return got;
}

static size_t page_size(void) {
    return sc_result_size_or(sc_posix_sysconf(_SC_PAGESIZE), 4096);
}

size_t posix_user(const sc_arena *a, sc_str path, sc_slice dst) {
    sc_result_size r = load(a, path, dst);
    return sc_result_size_or(r, 0) + page_size();
}
