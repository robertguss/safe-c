#include "meta.h"

bool meta_init(meta_side *m, link_set *s, sc_arena *records, sc_arena *keep, sc_arena *sql) {
    meta_side z = {0};
    *m = z;
    m->links = s;
    m->sql = sql;
    return catalog_init(&m->cat, records, keep);
}

static catalog_link link_shown(const conn_link *l) {
    switch (l->broken ? UI_CONN_DISCONNECTED : l->ui.state) {
    case UI_CONN_CONNECTING:
        return CATALOG_LINK_CONNECTING;
    case UI_CONN_CONNECTED:
        return CATALOG_LINK_UP;
    case UI_CONN_AUTH_FAILED:
    case UI_CONN_UNREACHABLE:
    case UI_CONN_DISCONNECTED:
        return CATALOG_LINK_FAILED;
    default:
        return CATALOG_LINK_DOWN;
    }
}

catalog_link meta_query_link(link_set *s) {
    conn_link *q = links_cur(s);
    if (s->offline || !links_has_cur(s)) {
        return CATALOG_LINK_DOWN;
    }
    return link_shown(q);
}

catalog_link meta_link(meta_side *m) {
    conn_link *l = links_meta(m->links);
    if (l == NULL) {
        return CATALOG_LINK_DOWN;
    }
    return link_shown(l);
}

catalog_show meta_shown(meta_side *m) {
    return catalog_shown(&m->cat, meta_query_link(m->links), meta_link(m));
}

meta_error meta_error_of(meta_side *m, const sc_arena **a, sc_str *msg) {
    conn_link *l = links_meta(m->links);
    if (meta_shown(m) != CATALOG_SHOW_ERROR) {
        return META_ERROR_NONE;
    }
    if (meta_link(m) == CATALOG_LINK_FAILED && l != NULL) {
        bool failed = !l->broken &&
                      (l->ui.state == UI_CONN_AUTH_FAILED || l->ui.state == UI_CONN_UNREACHABLE);
        *a = link_failure_arena(l);
        *msg = l->failure;
        return failed ? META_ERROR_CONNECT : META_ERROR_LOST;
    }
    *a = m->cat.arena;
    *msg = m->cat.error;
    return META_ERROR_CATALOG;
}

static sc_str read_error(const sc_arena *ta) {
    sc_str none = {NULL, 0, ta != NULL ? ta->epoch : 0};
    proto_header h;
    proto_reader r;
    if (ta == NULL || !proto_read(ta, &h, &r)) {
        return none;
    }
    uint64_t kind = PROTO_ITEM_END;
    sc_str s;
    while (proto_get_u64(&r, &kind) && kind == PROTO_ITEM_TAG) {
        if (!proto_get_str(&r, &s)) {
            return none;
        }
    }
    return kind == PROTO_ITEM_ERROR && proto_get_str(&r, &s) && proto_get_str(&r, &s) ? s : none;
}

/* The read ended without rows: `lit` (a literal) says why when the server
 * did not. */
static void fail_read(meta_side *m, const sc_arena *ta, const uint8_t *lit, size_t n) {
    sc_str why = read_error(ta);
    if (why.len > 0) {
        catalog_fail(&m->cat, m->read, ta, why);
        return;
    }
    sc_str s = sc_arena_reset(m->sql) ? sc_str_from_lit(m->sql, lit, n) : why;
    catalog_fail(&m->cat, m->read, m->sql, s);
}
#define FAIL_WHY(m, ta, lit) \
    fail_read((m), (ta), (const uint8_t *)("" lit), sizeof("" lit) - 1)
#define FAIL_LIT(m, lit) FAIL_WHY((m), NULL, lit)

/* The running read has ended: its rows, or why not, go to the catalog, and
 * its arenas back to the pool. */
static void finish(meta_side *m, conn_link *l) {
    conn_ui shown = l->ui; /* before link_keep, which forgets the arenas */
    const sc_arena *ta = shown.terminal_kept ? link_arena(l, shown.terminal) : NULL;
    switch (shown.q) {
    case Q_ROWS:
    case Q_COMMAND_TAG:
        (void)catalog_take(&m->cat, m->read, &l->res);
        break;
    case Q_ERROR:
    case Q_ERROR_AFTER_ROWS:
        FAIL_WHY(m, ta, "The catalog read failed.");
        break;
    case Q_CAPPED:
        FAIL_LIT(m, "The catalog read returned more than the sidebar holds.");
        break;
    default:
        FAIL_LIT(m, "The catalog read did not finish.");
        break;
    }
    result kept;
    (void)link_keep(l, &kept);
    (void)link_drop_result(l, &kept, &shown);
    m->running = false;
}

static void start_next(meta_side *m, conn_link *l) {
    sc_str sql;
    if (!catalog_next(&m->cat, &m->read)) {
        return;
    }
    if (!sc_arena_reset(m->sql) || !catalog_sql(m->read, m->sql, &sql)) {
        FAIL_LIT(m, "The catalog read could not be written.");
        return;
    }
    if (!link_run(l, m->sql, sql) || l->refused) {
        FAIL_LIT(m, "The catalog read was refused.");
        return;
    }
    m->running = true;
}

bool meta_poll(meta_side *m) {
    conn_link *l = links_meta(m->links);
    if (l == NULL) {
        bool was = m->running;
        m->running = false;
        return links_meta_open(m->links) || was;
    }
    bool changed = link_poll(l);
    if (l->broken || l->ui.state != UI_CONN_CONNECTED) {
        return changed;
    }
    if (m->conn != l->ui.conn) {
        m->conn = l->ui.conn;
        m->running = false;
        (void)catalog_restart(&m->cat);
        changed = true;
    }
    if (m->running && !conn_ui_active(&l->ui)) {
        finish(m, l);
        changed = true;
    }
    if (!m->running) {
        start_next(m, l);
    }
    return changed;
}

bool meta_can_retry(meta_side *m) {
    return meta_link(m) != CATALOG_LINK_FAILED || links_meta_can_retire(m->links);
}

bool meta_sidebar_error(meta_side *m, sc_arena *fa, const sc_arena **a, sc_str *text, bool *retry) {
    const sc_arena *ea = m->cat.arena;
    sc_str msg = m->cat.error;
    meta_error why = meta_error_of(m, &ea, &msg);
    if (why == META_ERROR_NONE) {
        return false;
    }
    *a = why == META_ERROR_LOST || msg.len == 0 ? fa : ea;
    *text = why == META_ERROR_LOST ? SC_STR_LIT(fa, "The metadata connection was lost.")
            : msg.len == 0         ? SC_STR_LIT(fa, "The catalog could not be read.")
                                   : msg;
    *retry = meta_can_retry(m);
    return true;
}

bool meta_reload(meta_side *m) {
    conn_link *l = links_meta(m->links);
    if (meta_link(m) != CATALOG_LINK_UP || l == NULL || !catalog_restart(&m->cat)) {
        return false;
    }
    if (!m->running) {
        start_next(m, l);
    }
    return true;
}

bool meta_retry(meta_side *m) {
    if (meta_shown(m) != CATALOG_SHOW_ERROR) {
        return false;
    }
    if (meta_link(m) == CATALOG_LINK_FAILED) {
        if (!links_meta_retire(m->links)) {
            return false;
        }
        m->running = false;
        m->conn = 0;
        return true;
    }
    return meta_reload(m);
}
