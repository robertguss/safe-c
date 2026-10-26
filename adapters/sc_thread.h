#ifndef SAFEC_ADAPTER_THREAD_H
#define SAFEC_ADAPTER_THREAD_H

#include "safec/result.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/* The thread adapter (adapters/thread.c, subset rule R0): pthreads, a wake
 * pipe, an atomic flag and a monotonic clock. Failures come back as an
 * sc_result_size with the error number in `err`, like adapters/sc_posix.h; no
 * caller reads errno.
 *
 * This is the explorer's only way to wait on more than one thing (plan KTD6):
 * a worker polls its wake's read end together with its libpq socket, and the
 * UI thread rings the wake after every sc_chan_send. */

/* A started thread. The fields are private. */
typedef struct {
    uintptr_t id;
    bool running; /* started and not yet joined */
} sc_thread;

/* What a thread runs. `arg` is the pointer passed to sc_thread_start. */
typedef void (*sc_thread_fn)(void *arg);

/* Starts `fn(arg)` on a new thread whose stack holds at least `stack_bytes`
 * (rounded up to the system minimum and the page size; macOS gives a secondary
 * thread only 512 KiB unless asked). On failure *t is not running. The caller
 * keeps `arg` alive until the join. */
sc_result_size sc_thread_start(sc_thread *t, sc_thread_fn fn, void *arg, size_t stack_bytes);

/* Waits for the thread to return. A thread that is not running fails with
 * EINVAL. After the call *t is not running, even on failure. */
sc_result_size sc_thread_join(sc_thread *t);

/* A self-pipe: any thread rings it, one thread waits on it. Both ends are
 * non-blocking and close-on-exec. The fields are private; sc_wake_fd hands the
 * read end to a poll loop (adapters/pq.c). */
typedef struct {
    int read_fd, write_fd;
} sc_wake;

/* Opens the pipe. On failure both descriptors are -1. */
sc_result_size sc_wake_new(sc_wake *w);

/* Makes the next sc_wake_wait return at once. Safe from any thread. Rings
 * before a drain coalesce: when the pipe is already full of unread rings, a
 * ring still succeeds, because a wake is pending either way. */
sc_result_size sc_wake_ring(sc_wake w);

/* Waits until a ring is pending or `timeout_ms` passes; 0 only checks. The
 * value is 1 when a ring is pending and 0 on a timeout. Leaves the rings
 * pending: call sc_wake_drain, then look for work. */
sc_result_size sc_wake_wait(sc_wake w, uint32_t timeout_ms);

/* Consumes every pending ring. The value is 1 if any was pending, else 0.
 * Drain before looking for work, so a ring that lands during the look is
 * still pending for the next wait. */
sc_result_size sc_wake_drain(sc_wake w);

/* The read end, for a poll loop that waits on it with other descriptors. */
int sc_wake_fd(sc_wake w);

/* Closes both ends; both become -1. Join every thread that rings it first. */
void sc_wake_free(sc_wake *w);

/* A flag two threads can race on. Plain stores and loads would be a data race,
 * and the stdlib has no atomics outside chan and mailbox. Zero-initialised
 * (`sc_flag f = {0};`) is clear. The field is private. */
typedef struct {
    atomic_bool set;
} sc_flag;

/* Sets the flag. True only for the call that moved it from clear to set, so of
 * two racing setters exactly one sees true. */
bool sc_flag_set(sc_flag *f);

/* Clears the flag. A set that lands after the clear is seen by the next set
 * attempt as already set. */
void sc_flag_clear(sc_flag *f);

/* Whether the flag is set now. */
bool sc_flag_is_set(sc_flag *f);

/* Milliseconds on a clock that never goes backwards, from an arbitrary start. */
uint64_t sc_clock_ms(void);

/* Sleeps for at least `ms` milliseconds. */
void sc_sleep_ms(uint32_t ms);

#endif
