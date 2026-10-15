#include <stddef.h>
typedef struct { unsigned char *base; size_t cap; size_t used; } Arena;
typedef struct { unsigned char *ptr; size_t len; } Slice;
/*@ ghost unsigned arena_epoch; */
/*@ requires \valid(a);
    requires arena_epoch < 4000000000u;
    assigns a->used, arena_epoch;
    ensures a->used == 0 && arena_epoch == \old(arena_epoch) + 1; */
void arena_reset(Arena *a) { a->used = 0; /*@ ghost arena_epoch++; */ }
/*@ requires \valid_read(s.ptr + (0 .. s.len - 1)) && i < s.len;
    requires born == arena_epoch;
    assigns \nothing; */
unsigned char slice_get(Slice s, size_t i) /*@ ghost (unsigned born) */ { return s.ptr[i]; }
/*@ requires \valid(a) && \valid_read(s.ptr + (0 .. s.len - 1)) && 0 < s.len && arena_epoch < 4000000000u; */
unsigned char stale(Arena *a, Slice s) {
	/*@ ghost unsigned born = arena_epoch; */
	arena_reset(a);
	return slice_get(s, 0) /*@ ghost (born) */;
}
/*@ requires \valid_read(s.ptr + (0 .. s.len - 1)) && 0 < s.len; */
unsigned char fresh(Slice s) {
	/*@ ghost unsigned born = arena_epoch; */
	return slice_get(s, 0) /*@ ghost (born) */;
}
