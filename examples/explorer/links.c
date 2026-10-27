#include "links.h"

static link_slot *slot_at(link_set *s, size_t i) {
    switch (i) {
    case 0:
        return &s->s0;
    case 1:
        return &s->s1;
    default:
        return &s->s2;
    }
}

void links_init(link_set *s, link_pool *pool, conn_limits lim, worker_ring_fn ring, void *ring_arg,
                const sc_arena *info_arena, sc_str info) {
    link_set z = {0};
    *s = z;
    s->pool = pool;
    s->lim = lim;
    s->ring = ring;
    s->ring_arg = ring_arg;
    s->info_arena = info_arena;
    s->info = info;
    s->next_conn = 1;
}

/* A free link slot, or LINKS_SLOTS. */
static size_t free_slot(link_set *s) {
    for (size_t i = 0; i < LINKS_SLOTS; i++) {
        if (!slot_at(s, i)->used) {
            return i;
        }
    }
    return LINKS_SLOTS;
}

/* A new current link in a free slot, not yet open. */
static bool new_link(link_set *s) {
    size_t i = free_slot(s);
    if (i == LINKS_SLOTS) {
        return false;
    }
    link_slot *sl = slot_at(s, i);
    sl->used = true;
    s->cur = i;
    return link_init(&sl->link, s->pool, BUDGET_QUERY, s->next_conn++, s->lim, s->ring, s->ring_arg);
}

bool links_open(link_set *s) {
    return new_link(s) && link_open(links_cur(s), s->info_arena, s->info);
}

bool links_has_cur(const link_set *s) {
    return s->cur == 0 ? s->s0.used : s->cur == 1 ? s->s1.used : s->s2.used;
}

conn_link *links_cur(link_set *s) {
    return &slot_at(s, s->cur)->link;
}

bool links_can_retire(link_set *s) {
    return conn_pool_can_retire(&s->pool->book) && free_slot(s) != LINKS_SLOTS;
}

bool links_retire(link_set *s, link_handoff *out) {
    conn_link *l = links_cur(s);
    if (!links_can_retire(s)) {
        return false;
    }
    link_handoff h;
    h.ended = l->ui; /* before link_retire, which forgets the arenas */
    h.req = l->res_req;
    h.sets = l->res_sets;
    h.kept = l->res;
    h.unsent = l->ui.q == Q_QUEUED;
    if (!link_retire(l, &h.kept)) {
        return false;
    }
    h.ended.q = l->ui.q;
    *out = h;
    s->offline = true;
    return new_link(s);
}

bool links_disconnect(link_set *s, link_handoff *out) {
    ui_conn_state st = links_cur(s)->ui.state;
    return !s->offline && (st == UI_CONN_CONNECTING || st == UI_CONN_CONNECTED) &&
           links_retire(s, out);
}

bool links_reconnect(link_set *s, link_handoff *out, bool *handed) {
    *handed = false;
    if (!links_can_retire(s)) {
        return false;
    }
    if (!s->offline) {
        if (!links_retire(s, out)) {
            return false;
        }
        *handed = true;
    }
    s->offline = false;
    return link_open(links_cur(s), s->info_arena, s->info);
}

bool links_cancel_connecting(link_set *s, link_handoff *out, bool *handed) {
    *handed = links_retire(s, out);
    return *handed || link_cancel(links_cur(s));
}

static void release_slot(link_slot *sl) {
    (void)link_join(&sl->link);
    link_free(&sl->link);
    sl->used = false;
}

void links_drain(link_set *s) {
    for (size_t i = 0; i < LINKS_SLOTS; i++) {
        link_slot *sl = slot_at(s, i);
        if (i == s->cur || !sl->used) {
            continue;
        }
        (void)link_poll(&sl->link);
        if (link_ended(&sl->link)) {
            release_slot(sl);
        }
    }
}

bool links_running(link_set *s) {
    for (size_t i = 0; i < LINKS_SLOTS; i++) {
        link_slot *sl = slot_at(s, i);
        if (sl->used && !link_ended(&sl->link)) {
            return true;
        }
    }
    return false;
}

bool links_close(link_set *s, uint32_t ms, links_wait_fn wait, void *wait_arg) {
    if (links_has_cur(s)) {
        conn_link *c = links_cur(s);
        if (c->started && !c->broken) {
            (void)link_close(c);
        }
        if (c->broken) {
            result none;
            (void)link_retire(c, &none);
        }
    }
    uint64_t end = sc_clock_ms() + ms;
    while (links_running(s) && sc_clock_ms() < end) {
        wait(wait_arg, 50);
        for (size_t i = 0; i < LINKS_SLOTS; i++) {
            link_slot *sl = slot_at(s, i);
            if (sl->used) {
                (void)link_poll(&sl->link);
            }
        }
    }
    bool joined = true;
    for (size_t i = 0; i < LINKS_SLOTS; i++) {
        link_slot *sl = slot_at(s, i);
        if (!sl->used) {
            continue;
        }
        if (!link_ended(&sl->link)) {
            joined = false;
            continue;
        }
        release_slot(sl);
    }
    return joined;
}
