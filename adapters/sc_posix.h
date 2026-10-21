#ifndef SAFEC_ADAPTER_POSIX_H
#define SAFEC_ADAPTER_POSIX_H

#include "safec/arena.h"
#include "safec/result.h"
#include "safec/str.h"
#include <unistd.h> /* the _SC_* names sc_posix_sysconf takes */

/* POSIX calls that report failure through errno, returned as values instead. A
 * failure is `ok == 0` with `err` the errno the call set, always positive; no
 * caller reads errno. A slice or string argument that is not live in its
 * arena fails with EINVAL, the same as an argument the call rejects. */

/* Opens `path` read-only with close-on-exec. The value is the file descriptor.
 * An empty path, a path with a NUL byte, or one of PATH_MAX bytes or more fails
 * (EINVAL, EINVAL, ENAMETOOLONG) without touching the file system. */
sc_result_size sc_posix_open_read(const sc_arena *a, sc_str path);

/* Reads up to dst.len bytes into dst, retrying when a signal interrupts the
 * call. The value is the number of bytes read; 0 with a non-empty dst is end of
 * file. An empty dst succeeds with 0 and reads nothing. */
sc_result_size sc_posix_read(const sc_arena *a, int fd, sc_slice dst);

/* The size in bytes of the regular file `fd` refers to. Any other kind of file
 * fails with EINVAL. */
sc_result_size sc_posix_file_size(int fd);

/* Closes `fd`; the value is 0. A descriptor is closed even when this fails. */
sc_result_size sc_posix_close(int fd);

/* sysconf(name) for a name that has a limit. A name the system rejects fails
 * with EINVAL, and one with no definite limit fails with ENOTSUP. */
sc_result_size sc_posix_sysconf(int name);

#endif
