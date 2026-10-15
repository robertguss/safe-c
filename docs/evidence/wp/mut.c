#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#define ARENA_ALIGN 8u

typedef struct {
	unsigned char *base;
	size_t cap;
	size_t used;
	unsigned epoch;
} Arena;

typedef struct {
	unsigned char *ptr;
	size_t len;
	unsigned epoch;
} Slice;

/*@ predicate valid_arena(Arena *a) =
      \valid(a) && a->used <= a->cap && a->used % ARENA_ALIGN == 0 &&
      (a->cap > 0 ==> \valid(a->base + (0 .. a->cap - 1))) &&
      \separated(a, a->base + (0 .. a->cap - 1));
    predicate valid_slice(Arena *a, Slice s) =
      s.epoch == a->epoch &&
      (s.len == 0 || (\valid(s.ptr + (0 .. s.len - 1)) &&
        s.ptr >= a->base && s.ptr + s.len <= a->base + a->used));
*/

/*@ requires \valid(a);
    requires cap % ARENA_ALIGN == 0;
    requires cap > 0 ==> \valid(buf + (0 .. cap - 1));
    requires \separated(a, buf + (0 .. cap - 1));
    assigns *a;
    ensures valid_arena(a);
    ensures a->base == buf && a->cap == cap && a->used == 0;
*/
void arena_init(Arena *a, unsigned char *buf, size_t cap) {
	a->base = buf;
	a->cap = cap;
	a->used = 0;
	a->epoch = 0;
}

/*@ requires valid_arena(a);
    assigns a->used;
    ensures valid_arena(a);
    behavior fits:
      assumes n > 0 && n <= a->cap - a->used && n <= SIZE_MAX - (ARENA_ALIGN - 1) &&
              (n + (ARENA_ALIGN - 1)) / ARENA_ALIGN * ARENA_ALIGN <= a->cap - a->used;
      ensures \result.ptr == a->base + \old(a->used);
      ensures \result.len == n && \result.epoch == a->epoch;
      ensures a->used >= \old(a->used) + n;
      ensures valid_slice(a, \result);
      ensures \forall integer i; 0 <= i < \old(a->used) ==> \separated(\result.ptr + (0 .. n - 1), a->base + i);
    behavior fails:
      assumes !(n > 0 && n <= a->cap - a->used && n <= SIZE_MAX - (ARENA_ALIGN - 1) &&
              (n + (ARENA_ALIGN - 1)) / ARENA_ALIGN * ARENA_ALIGN <= a->cap - a->used);
      ensures \result.ptr == \null && \result.len == 0;
      ensures a->used == \old(a->used);
    complete behaviors;
    disjoint behaviors;
*/
Slice arena_alloc(Arena *a, size_t n) {
	Slice s = { NULL, 0 };
	s.epoch = a->epoch;
	if (n == 0 || n > a->cap - a->used || n > SIZE_MAX - (ARENA_ALIGN - 1))
		return s;
	size_t rounded = (n + (ARENA_ALIGN - 1)) / ARENA_ALIGN * ARENA_ALIGN;
	if (rounded > a->cap - a->used)
		return s;
	s.ptr = a->base + a->used;
	s.len = n;
	a->used += rounded;
	return s;
}

/*@ requires valid_arena(a);
    requires a->epoch < UINT_MAX;
    assigns a->used, a->epoch;
    ensures valid_arena(a);
    ensures a->used == 0 && a->epoch == \old(a->epoch) + 1;
*/
void arena_reset(Arena *a) {
	a->used = 0;
	a->epoch++;
}

/*@ requires valid_arena(a) && valid_slice(a, s);
    requires i < s.len;
    assigns \nothing;
    ensures \result == s.ptr[i];
*/
unsigned char slice_get(Arena *a, Slice s, size_t i) {
	return s.ptr[i+1];
}

/*@ requires valid_arena(a) && valid_slice(a, dst) && valid_slice(a, src);
    requires src.len <= dst.len;
    requires \separated(dst.ptr + (0 .. dst.len - 1), src.ptr + (0 .. src.len - 1));
    assigns dst.ptr[0 .. src.len - 1];
    ensures \forall integer k; 0 <= k < src.len ==> dst.ptr[k] == src.ptr[k];
*/
void slice_copy(Arena *a, Slice dst, Slice src) {
	/*@ loop invariant 0 <= i <= src.len;
	    loop invariant \forall integer k; 0 <= k < i ==> dst.ptr[k] == src.ptr[k];
	    loop assigns i, dst.ptr[0 .. src.len - 1];
	    loop variant src.len - i;
	*/
	for (size_t i = 0; i < src.len; i++)
		dst.ptr[i] = src.ptr[i];
}
