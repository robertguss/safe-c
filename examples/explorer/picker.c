#include "picker.h"
#include "sc_ui.h"
#include "store.h"
#include <errno.h>

bool picker_init(picker *p) {
    if (!ui_arena_new(&p->files, CONNCFG_BYTES) || !ui_arena_new(&p->records, 65536) ||
        !ui_arena_new(&p->paths, 16384) || !ui_arena_new(&p->info, 2u * CONNCFG_BYTES + 1024u) ||
        !ui_arena_new(&p->secret, PASSWORD_BYTES)) return false;
    p->dir = ui_pref_path(&p->paths);
    if (p->dir.len == 0) return false;
    p->path = STORE_PATH(&p->paths, &p->paths, p->dir, "connections.conf");
    sc_str file = {NULL, 0, p->files.epoch};
    sc_result_size got = store_read(&p->paths, p->path, &p->files, CONNCFG_BYTES, &file);
    p->file_error = !got.ok && got.err != ENOENT;
    p->name = SC_STR_LIT(&p->paths, "Environment");
    p->connection = (sc_str){NULL, 0, p->info.epoch};
    picker_clear(p);
    return conncfg_parse(&p->cfg, &p->files, file, &p->records);
}

void picker_clear(picker *p) {
    (void)sc_mem_erase(&p->secret, p->password.dst);
    (void)sc_arena_reset(&p->secret);
    p->password = sc_buf_new(sc_arena_alloc(&p->secret, PASSWORD_BYTES));
}

bool picker_choose(picker *p) {
    if (!sc_arena_reset(&p->info)) return false;
    p->connection = (sc_str){NULL, 0, p->info.epoch};
    p->name = SC_STR_LIT(&p->paths, "Environment");
    if (p->selected < p->cfg.count) {
        conncfg_entry e;
        if (!conncfg_at(&p->cfg, p->selected, &e) || e.error != CFG_OK ||
            !conncfg_info(&p->cfg, p->selected, &p->info, &p->connection)) return false;
        p->name = e.name;
    }
    return true;
}

bool picker_text(picker *p, const sc_arena *a, sc_str text) {
    if (!p->prompt || p->connecting || !sc_utf8_valid(a, text)) return false;
    for (size_t i = 0; i < text.len; i++) {
        uint8_t ch = 0;
        if (!sc_slice_get(a, text, i, &ch) || ch < 32u || ch == 127u) return false;
    }
    if (text.len > p->password.dst.len - p->password.len) return false;
    return sc_buf_str(&p->secret, &p->password, a, text);
}

bool picker_backspace(picker *p) {
    if (p->password.len == 0 || p->connecting) return false;
    sc_str text = sc_buf_view(&p->secret, p->password);
    size_t at = sc_utf8_prev(&p->secret, text, text.len);
    (void)sc_mem_erase(&p->secret, sc_slice_sub(&p->secret, text, at, text.len - at));
    p->password.len = at;
    return true;
}
