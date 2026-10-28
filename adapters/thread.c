/* Thread adapter (subset rule R0). It makes the pthreads, pipe, poll and clock
 * calls application code cannot make itself, saves errno right after each one,
 * and hands back an sc_result_size. */
#define _POSIX_C_SOURCE 200809L
#include "sc_thread.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    sc_thread_fn fn;
    void *arg;
} thread_start;

static void *thread_main(void *p) {
    thread_start s = *(thread_start *)p;
    free(p);
    s.fn(s.arg);
    return NULL;
}

/* `n` rounded up to a multiple of `page`, or 0 if that overflows. */
static size_t round_up(size_t n, size_t page) {
    size_t r = n % page;
    if (r == 0)
        return n;
    return n > SIZE_MAX - (page - r) ? 0 : n + (page - r);
}

sc_result_size sc_thread_start(sc_thread *t, sc_thread_fn fn, void *arg, size_t stack_bytes) {
    t->running = false;
    t->id = 0;
    if (fn == NULL)
        return sc_err_size(EINVAL);
    long page = sysconf(_SC_PAGESIZE);
    /* glibc makes PTHREAD_STACK_MIN a sysconf call under _GNU_SOURCE. */
    size_t min = (size_t)PTHREAD_STACK_MIN;
    size_t stack = stack_bytes < min ? min : stack_bytes;
    stack = round_up(stack, page > 0 ? (size_t)page : 4096);
    if (stack == 0)
        return sc_err_size(EINVAL);
    thread_start *s = malloc(sizeof *s);
    if (s == NULL)
        return sc_err_size(ENOMEM);
    s->fn = fn;
    s->arg = arg;
    pthread_attr_t attr;
    int err = pthread_attr_init(&attr);
    if (err != 0) {
        free(s);
        return sc_err_size(err);
    }
    err = pthread_attr_setstacksize(&attr, stack);
    pthread_t id;
    if (err == 0)
        err = pthread_create(&id, &attr, thread_main, s);
    pthread_attr_destroy(&attr);
    if (err != 0) {
        free(s);
        return sc_err_size(err);
    }
    t->id = (uintptr_t)id;
    t->running = true;
    return sc_ok_size(0);
}

sc_result_size sc_thread_join(sc_thread *t) {
    if (!t->running)
        return sc_err_size(EINVAL);
    t->running = false;
    int err = pthread_join((pthread_t)t->id, NULL);
    if (err != 0)
        return sc_err_size(err);
    return sc_ok_size(0);
}

static int set_flags(int fd) {
    int fl = fcntl(fd, F_GETFL);
    if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0)
        return errno;
    int fdfl = fcntl(fd, F_GETFD);
    if (fdfl < 0 || fcntl(fd, F_SETFD, fdfl | FD_CLOEXEC) < 0)
        return errno;
    return 0;
}

sc_result_size sc_wake_new(sc_wake *w) {
    int p[2];
    w->read_fd = -1;
    w->write_fd = -1;
    if (pipe(p) != 0)
        return sc_err_size(errno);
    int err = set_flags(p[0]);
    if (err == 0)
        err = set_flags(p[1]);
    if (err != 0) {
        close(p[0]);
        close(p[1]);
        return sc_err_size(err);
    }
    w->read_fd = p[0];
    w->write_fd = p[1];
    return sc_ok_size(0);
}

sc_result_size sc_wake_ring(sc_wake w) {
    uint8_t one = 1;
    for (;;) {
        ssize_t n = write(w.write_fd, &one, 1);
        if (n == 1)
            return sc_ok_size(0);
        if (n < 0 && errno == EINTR)
            continue;
        /* A full pipe already holds unread rings, so the wake is pending. */
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return sc_ok_size(0);
        return sc_err_size(n < 0 ? errno : EIO);
    }
}

sc_result_size sc_wake_wait(sc_wake w, uint32_t timeout_ms) {
    /* poll skips a negative descriptor, which would turn a freed wake into a sleep. */
    if (w.read_fd < 0)
        return sc_err_size(EBADF);
    struct pollfd p = {.fd = w.read_fd, .events = POLLIN};
    int ms = timeout_ms > INT_MAX ? INT_MAX : (int)timeout_ms;
    uint64_t deadline = sc_clock_ms() + timeout_ms;
    for (;;) {
        int n = poll(&p, 1, ms);
        if (n > 0) {
            if (p.revents & POLLNVAL)
                return sc_err_size(EBADF);
            return sc_ok_size(1);
        }
        if (n == 0)
            return sc_ok_size(0);
        if (errno != EINTR)
            return sc_err_size(errno);
        uint64_t now = sc_clock_ms();
        ms = now >= deadline ? 0 : (int)(deadline - now > INT_MAX ? INT_MAX : deadline - now);
    }
}

sc_result_size sc_wake_drain(sc_wake w) {
    uint8_t buf[64];
    size_t any = 0;
    for (;;) {
        ssize_t n = read(w.read_fd, buf, sizeof buf);
        if (n > 0) {
            any = 1;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return sc_ok_size(any);
        /* 0 is end of file: the write end is gone, which only sc_wake_free does. */
        return sc_err_size(n < 0 ? errno : EPIPE);
    }
}

int sc_wake_fd(sc_wake w) {
    return w.read_fd;
}

void sc_wake_free(sc_wake *w) {
    if (w->read_fd >= 0)
        close(w->read_fd);
    if (w->write_fd >= 0)
        close(w->write_fd);
    w->read_fd = -1;
    w->write_fd = -1;
}

bool sc_flag_set(sc_flag *f) {
    return !atomic_exchange(&f->set, true);
}

void sc_flag_clear(sc_flag *f) {
    atomic_store(&f->set, false);
}

bool sc_flag_is_set(sc_flag *f) {
    return atomic_load(&f->set);
}

uint64_t sc_clock_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

uint64_t sc_clock_unix_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

uint64_t sc_clock_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

void sc_sleep_ms(uint32_t ms) {
    struct timespec ts = {.tv_sec = (time_t)(ms / 1000u), .tv_nsec = (long)(ms % 1000u) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}
