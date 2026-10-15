#include <stddef.h>
#include <stdint.h>

typedef struct {
	unsigned char *base;
	size_t cap;
	size_t used;
} Arena;

/*@ predicate valid_arena(Arena *a) =
      \valid(a) && a->used <= a->cap &&
      (a->cap > 0 ==> \valid(a->base + (0 .. a->cap - 1)));
*/

/*@ requires \valid(a);
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
}

/*@ requires valid_arena(a);
    assigns a->used;
    ensures valid_arena(a);
    behavior fits:
      assumes n > 0 && n <= a->cap - a->used;
      ensures \result == a->base + \old(a->used);
      ensures a->used == \old(a->used) + n;
      ensures \valid(\result + (0 .. n - 1));
    behavior fails:
      assumes n == 0 || n > a->cap - a->used;
      ensures \result == \null;
      ensures a->used == \old(a->used);
    complete behaviors;
    disjoint behaviors;
*/
unsigned char *arena_alloc(Arena *a, size_t n) {
	if (n == 0 || n > a->cap - a->used)
		return NULL;
	unsigned char *p = a->base + a->used;
	a->used += n;
	return p;
}

/*@ requires valid_arena(a);
    assigns a->used;
    ensures valid_arena(a);
    ensures a->used == 0;
*/
void arena_reset(Arena *a) {
	a->used = 0;
}
