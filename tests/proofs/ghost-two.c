#include <stddef.h>
#include <limits.h>
typedef struct { unsigned char *base; size_t cap; size_t used; } Arena;
typedef struct { unsigned char *ptr; size_t len; } Slice;
/*@ ghost unsigned arena_epoch[2]; */
/*@ requires \valid(a);
    requires id < 2;
    requires arena_epoch[id] < UINT_MAX;
    assigns a->used, arena_epoch[id];
    ensures a->used == 0;
    ensures arena_epoch[id] == \old(arena_epoch[id]) + 1;
*/
void arena_reset(Arena *a) /*@ ghost (unsigned id) */ {
    a->used = 0;
    /*@ ghost arena_epoch[id]++; */
}
/*@ requires id < 2;
    requires \valid_read(s.ptr + (0 .. s.len - 1)) && i < s.len;
    requires live: born == arena_epoch[id];
    assigns \nothing;
    ensures \result == s.ptr[i];
*/
unsigned char slice_get(Slice s, size_t i) /*@ ghost (unsigned id, unsigned born) */ {
    return s.ptr[i];
}
/*@ requires \valid(a) && \valid(b) && \separated(a, b);
    requires \valid_read(s.ptr + (0 .. s.len - 1)) && 0 < s.len;
    requires arena_epoch[0] < UINT_MAX && arena_epoch[1] < UINT_MAX;
    assigns a->used, b->used, arena_epoch[0 .. 1];
*/
unsigned char stale_own(Arena *a, Arena *b, Slice s) {
    /*@ ghost unsigned born = arena_epoch[0]; */
    arena_reset(b) /*@ ghost (1) */;
    arena_reset(a) /*@ ghost (0) */;
    return slice_get(s, 0) /*@ ghost (0, born) */;
}
/*@ requires \valid(b);
    requires \valid_read(s.ptr + (0 .. s.len - 1)) && 0 < s.len;
    requires arena_epoch[1] < UINT_MAX;
    assigns b->used, arena_epoch[1];
    ensures \result == s.ptr[0];
*/
unsigned char fresh_other(Arena *b, Slice s) {
    /*@ ghost unsigned born = arena_epoch[0]; */
    arena_reset(b) /*@ ghost (1) */;
    return slice_get(s, 0) /*@ ghost (0, born) */;
}
/*@ requires \valid(a);
    requires \valid_read(s.ptr + (0 .. s.len - 1)) && 0 < s.len;
    requires arena_epoch[0] < UINT_MAX;
    assigns a->used, arena_epoch[0];
    ensures \result == s.ptr[0];
*/
unsigned char stale_recaptured(Arena *a, Slice s) {
    arena_reset(a) /*@ ghost (0) */;
    /*@ ghost unsigned born = arena_epoch[0]; */
    return slice_get(s, 0) /*@ ghost (0, born) */;
}
