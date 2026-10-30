#include "links.h"

static link_slot *slot_at(link_set *s, size_t i) {
    switch (i) {
    case 0:
        return &s->s0;
    case 1:
        return &s->s1;
    case 2:
        return &s->s2;
    default:
        return &s->s3;
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
    bool ok = link_init(&sl->link, s->pool, BUDGET_QUERY, s->next_conn++, s->lim, s->ring, s->ring_arg);
    sl->link.password_arena = s->password_arena;
    sl->link.password = s->password;
    return ok;
}

bool links_open(link_set *s) {
    return new_link(s) && link_open_target(links_cur(s), s->engine, s->info_arena, s->info);
}

bool links_has_cur(const link_set *s) {
    return s->cur == 0   ? s->s0.used
           : s->cur == 1 ? s->s1.used
           : s->cur == 2 ? s->s2.used
                         : s->s3.used;
}

conn_link *links_cur(link_set *s) {
    return &slot_at(s, s->cur)->link;
}

conn_link *links_meta(link_set *s) {
    return s->has_meta ? &slot_at(s, s->meta)->link : NULL;
}

static bool takes_retiring_slot(const conn_link *l) {
    return !l->broken && l->ui.state != UI_CONN_NONE && l->ui.state != UI_CONN_CLOSED;
}

bool links_can_retire(link_set *s) {
    conn_link *m = links_meta(s);
    size_t need = 1u + (m != NULL && takes_retiring_slot(m) ? 1u : 0u);
    return s->pool->book.retiring + need <= BUDGET_RETIRING_SLOTS && free_slot(s) != LINKS_SLOTS;
}

bool links_meta_open(link_set *s) {
    conn_link *q = links_cur(s);
    size_t i = free_slot(s);
    if (s->has_meta || s->offline || !links_has_cur(s) || q->broken ||
        q->ui.state != UI_CONN_CONNECTED || i == LINKS_SLOTS) {
        return false;
    }
    link_slot *sl = slot_at(s, i);
    sl->used = true;
    s->meta = i;
    s->has_meta = true;
    conn_limits lim = {LINKS_META_WINDOW, LINKS_META_QUOTA, LINKS_META_ROWS};
    bool ok = link_init(&sl->link, s->pool, BUDGET_META, s->next_conn++, lim, s->ring, s->ring_arg);
    sl->link.password_arena = s->password_arena;
    sl->link.password = s->password;
    ok = ok && link_open_target(&sl->link, s->engine, s->info_arena, s->info);
    sl->link.broken = sl->link.broken || !ok;
    return ok;
}

static void release_slot(link_slot *sl) {
    (void)link_join(&sl->link);
    link_free(&sl->link);
    sl->used = false;
}

bool links_meta_can_retire(link_set *s) {
    conn_link *m = links_meta(s);
    return m == NULL || !takes_retiring_slot(m) || conn_pool_can_retire(&s->pool->book);
}

static bool retire_dropping(conn_link *m) {
    conn_ui shown = m->ui; /* before link_retire, which forgets the arenas */
    result kept;
    if (!link_retire(m, &kept)) {
        return false;
    }
    (void)link_drop_result(m, &kept, &shown);
    return true;
}

bool links_meta_retire(link_set *s) {
    conn_link *m = links_meta(s);
    if (m == NULL) {
        return true;
    }
    link_slot *sl = slot_at(s, s->meta);
    if (m->started && !retire_dropping(m)) {
        return false;
    }
    if (link_ended(m)) {
        release_slot(sl);
    }
    s->has_meta = false;
    return true;
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
    return links_meta_retire(s) && new_link(s);
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
    return link_open_target(links_cur(s), s->engine, s->info_arena, s->info);
}

bool links_cancel_connecting(link_set *s, link_handoff *out, bool *handed) {
    *handed = links_retire(s, out);
    return *handed || link_cancel(links_cur(s));
}

void links_drain(link_set *s) {
    for (size_t i = 0; i < LINKS_SLOTS; i++) {
        link_slot *sl = slot_at(s, i);
        if (i == s->cur || (s->has_meta && i == s->meta) || !sl->used) {
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

static void close_live(conn_link *c) {
    if (c->started && !c->broken) {
        (void)link_close(c);
    }
    if (c->broken) {
        result none;
        (void)link_retire(c, &none);
    }
}

bool links_close(link_set *s, uint32_t ms, links_wait_fn wait, void *wait_arg) {
    if (links_has_cur(s)) {
        close_live(links_cur(s));
    }
    conn_link *m = links_meta(s);
    if (m != NULL && m->broken) {
        (void)retire_dropping(m);
    } else if (m != NULL) {
        close_live(m);
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
    s->has_meta = s->has_meta && slot_at(s, s->meta)->used;
    return joined;
}
