#include "budget.h"

typedef struct {
    uint8_t cls;
    uint8_t owner;
} budget_slot;

budget_split budget_default_split(void) {
    budget_split s = {BUDGET_CONTROL_ARENAS, BUDGET_META_ARENAS, BUDGET_FLOOR_ARENAS,
                      BUDGET_GROWTH_ARENAS};
    return s;
}

static bool sum(budget_split s, size_t *out) {
    size_t n = s.control;
    if (s.meta > SIZE_MAX - n) {
        return false;
    }
    n += s.meta;
    if (s.floor > SIZE_MAX - n) {
        return false;
    }
    n += s.floor;
    if (s.growth > SIZE_MAX - n) {
        return false;
    }
    *out = n + s.growth;
    return true;
}

static bool set_slot(budget *b, size_t i, budget_class c, budget_owner o) {
    budget_slot s = {(uint8_t)c, (uint8_t)o};
    return SC_TSLICE_SET(budget_slot, b->arena, b->slots, i, &s);
}

bool budget_init(budget *b, sc_arena *a, budget_split cap) {
    budget_split none = {0, 0, 0, 0};
    b->cap = cap;
    b->used = none;
    b->query_held = 0;
    b->meta_held = 0;
    b->retiring_held = 0;
    b->count = 0;
    b->arena = a;
    size_t n = 0;
    if (!sum(cap, &n) || n == 0) {
        return false;
    }
    b->slots = SC_TSLICE_ALLOC(budget_slot, a, n);
    if (sc_tslice_len(b->slots) != n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (!set_slot(b, i, BUDGET_FROM_NONE, BUDGET_QUERY)) {
            return false;
        }
    }
    b->count = n;
    return true;
}

size_t budget_spare(const budget *b, budget_class c) {
    switch (c) {
    case BUDGET_FROM_CONTROL:
        return b->cap.control - b->used.control;
    case BUDGET_FROM_META:
        return b->cap.meta - b->used.meta;
    case BUDGET_FROM_FLOOR:
        return b->cap.floor - b->used.floor;
    case BUDGET_FROM_GROWTH:
        return b->cap.growth - b->used.growth;
    default:
        return 0;
    }
}

budget_class budget_pick(const budget *b, budget_owner owner, budget_use use, size_t set,
                         size_t out) {
    if (owner == BUDGET_RETIRING) {
        return BUDGET_FROM_NONE;
    }
    if (use == BUDGET_USE_CONTROL) {
        return budget_spare(b, BUDGET_FROM_CONTROL) > 0 ? BUDGET_FROM_CONTROL : BUDGET_FROM_NONE;
    }
    if (owner == BUDGET_META) {
        return budget_spare(b, BUDGET_FROM_META) > 0 ? BUDGET_FROM_META : BUDGET_FROM_NONE;
    }
    bool floor = budget_spare(b, BUDGET_FROM_FLOOR) > 0;
    bool growth = budget_spare(b, BUDGET_FROM_GROWTH) > 0;
    if (use == BUDGET_USE_RUN) {
        return floor ? BUDGET_FROM_FLOOR : growth ? BUDGET_FROM_GROWTH : BUDGET_FROM_NONE;
    }
    if (growth) {
        return BUDGET_FROM_GROWTH;
    }
    return floor && out == 0 && set < BUDGET_FLOOR_GRANTS ? BUDGET_FROM_FLOOR : BUDGET_FROM_NONE;
}

budget_class budget_class_of(const budget *b, size_t i) {
    budget_slot s = {(uint8_t)BUDGET_FROM_NONE, 0};
    if (i >= b->count || !SC_TSLICE_GET(budget_slot, b->arena, b->slots, i, &s)) {
        return BUDGET_FROM_NONE;
    }
    return (budget_class)s.cls;
}

static size_t *used_of(budget *b, budget_class c) {
    switch (c) {
    case BUDGET_FROM_CONTROL:
        return &b->used.control;
    case BUDGET_FROM_META:
        return &b->used.meta;
    case BUDGET_FROM_FLOOR:
        return &b->used.floor;
    case BUDGET_FROM_GROWTH:
        return &b->used.growth;
    default:
        return NULL;
    }
}

static size_t *held_of(budget *b, budget_owner o) {
    return o == BUDGET_META       ? &b->meta_held
           : o == BUDGET_RETIRING ? &b->retiring_held
                                  : &b->query_held;
}

bool budget_take(budget *b, size_t i, budget_owner owner, budget_class c) {
    size_t *used = used_of(b, c);
    if (used == NULL || budget_spare(b, c) == 0 || i >= b->count ||
        budget_class_of(b, i) != BUDGET_FROM_NONE || !set_slot(b, i, c, owner)) {
        return false;
    }
    *used += 1u;
    *held_of(b, owner) += 1u;
    return true;
}

bool budget_retire(budget *b, size_t i) {
    budget_slot s = {(uint8_t)BUDGET_FROM_NONE, 0};
    if (i >= b->count || !SC_TSLICE_GET(budget_slot, b->arena, b->slots, i, &s)) {
        return false;
    }
    budget_class c = (budget_class)s.cls;
    budget_owner o = (budget_owner)s.owner;
    if (used_of(b, c) == NULL || *held_of(b, o) == 0) {
        return false;
    }
    if (c == BUDGET_FROM_FLOOR && budget_spare(b, BUDGET_FROM_GROWTH) > 0) {
        b->used.floor -= 1u;
        b->used.growth += 1u;
        c = BUDGET_FROM_GROWTH;
    }
    if (!set_slot(b, i, c, BUDGET_RETIRING)) {
        return false;
    }
    *held_of(b, o) -= 1u;
    b->retiring_held += 1u;
    return true;
}

bool budget_put(budget *b, size_t i) {
    budget_slot s = {(uint8_t)BUDGET_FROM_NONE, 0};
    if (i >= b->count || !SC_TSLICE_GET(budget_slot, b->arena, b->slots, i, &s)) {
        return false;
    }
    size_t *used = used_of(b, (budget_class)s.cls);
    budget_owner o = (budget_owner)s.owner;
    if (used == NULL || *used == 0 || *held_of(b, o) == 0 ||
        !set_slot(b, i, BUDGET_FROM_NONE, BUDGET_QUERY)) {
        return false;
    }
    *used -= 1u;
    *held_of(b, o) -= 1u;
    return true;
}

size_t budget_room(const budget *b, budget_owner owner) {
    if (owner == BUDGET_META) {
        return budget_spare(b, BUDGET_FROM_META);
    }
    if (owner == BUDGET_RETIRING) {
        return 0;
    }
    return budget_spare(b, BUDGET_FROM_FLOOR) + budget_spare(b, BUDGET_FROM_GROWTH);
}

size_t budget_limit(const budget *b, budget_owner owner) {
    return owner == BUDGET_META       ? b->cap.meta
           : owner == BUDGET_RETIRING ? 0
                                      : b->cap.floor + b->cap.growth;
}

size_t budget_held(const budget *b, budget_owner owner) {
    return owner == BUDGET_META       ? b->meta_held
           : owner == BUDGET_RETIRING ? b->retiring_held
                                      : b->query_held;
}
