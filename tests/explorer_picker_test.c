#include "../adapters/mem.c"
#include "../adapters/posix.c"
#include "../examples/explorer/conncfg.c"
#include "../examples/explorer/history.c"
#include "../examples/explorer/picker.c"
#include "../examples/explorer/store.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>

bool ui_arena_new(sc_arena *a, size_t cap) {
    (void)a;
    (void)cap;
    return false;
}

sc_str ui_pref_path(sc_arena *a) {
    return (sc_str){NULL, 0, a->epoch};
}

static alignas(8) uint8_t info[2u * CONNCFG_BYTES + 1024u], files[CONNCFG_BYTES],
    records[65536], paths[16384], secret[PASSWORD_BYTES], source[16384],
    history_data[HISTORY_BYTES], history_records[30000], disk[HISTORY_FILE_BYTES];

int main(void) {
    picker p = {0};
    sc_arena a, ha, hr, d;
    sc_arena_init(&p.info, info, sizeof info);
    sc_arena_init(&p.files, files, sizeof files);
    sc_arena_init(&p.records, records, sizeof records);
    sc_arena_init(&p.paths, paths, sizeof paths);
    sc_arena_init(&p.secret, secret, sizeof secret);
    sc_arena_init(&a, source, sizeof source);
    sc_arena_init(&ha, history_data, sizeof history_data);
    sc_arena_init(&hr, history_records, sizeof history_records);
    sc_arena_init(&d, disk, sizeof disk);
    p.visible = true;
    assert(conncfg_parse(&p.cfg, &p.files, SC_STR_LIT(&p.files, "[Saved]\nhost=localhost\n"), &p.records));
    assert(picker_choose(&p) && SC_STR_EQ_LIT(&p.info, p.connection, "host='localhost' "));
    assert(picker_dialog(&p) == 0 && picker_engine(&p, SC_DB_DUCKDB));
    ui_event ev = {0};
    ev.kind = UI_EVENT_FILE;
    ev.request = picker_dialog(&p);
    assert(ev.request != 0 && picker_dialog(&p) == 0);
    ev.file_status = UI_FILE_OPEN;
    ev.text = SC_STR_LIT(&a, " /tmp/日本語 # one.duckdb ");
    assert(picker_file(&p, &a, ev) && picker_choose(&p));
    assert(SC_STR_EQ_LIT(&p.info, p.connection, " /tmp/日本語 # one.duckdb "));
    sc_str original = p.connection;
    p.failed = true;
    ev.request = picker_dialog(&p);
    ev.file_status = UI_FILE_CANCEL;
    assert(!picker_file(&p, &a, ev) && !p.dialog_pending && p.failed && p.connection.ptr == original.ptr);
    ev.request = picker_dialog(&p);
    assert(picker_engine(&p, SC_DB_SQLITE));
    ev.file_status = UI_FILE_OPEN;
    assert(!picker_file(&p, &a, ev) && p.connection.len == 0);
    ev.request = picker_dialog(&p);
    p.visible = false;
    assert(!picker_file(&p, &a, ev));
    p.visible = true;
    assert(picker_file(&p, &a, ev));
    history h;
    assert(history_init(&h, &ha, &hr));
    sc_str sql = SC_STR_LIT(&a, "SELECT 1");
    assert(history_add(&h, &a, sql, &p.info, p.name, 1));
    assert(picker_local(&p, &a, SC_STR_LIT(&a, "/other/same.db")));
    assert(history_add(&h, &a, sql, &p.info, p.name, 2));
    assert(picker_engine(&p, SC_DB_DUCKDB) && picker_local(&p, &a, SC_STR_LIT(&a, "/other/same.db")));
    assert(history_add(&h, &a, sql, &p.info, p.name, 3));
    sc_str saved;
    assert(history_write(&h, &d, &saved) && history_load(&h, &d, saved) && h.count == 3);
    history_entry e;
    assert(history_get(&h, 1, &e) && SC_STR_EQ_LIT(h.arena, e.name, "SQLite: /other/same.db"));
    assert(history_get(&h, 2, &e) && SC_STR_EQ_LIT(h.arena, e.name, "DuckDB: /other/same.db"));
    p.connecting = true;
    assert(!picker_engine(&p, SC_DB_POSTGRESQL) && picker_dialog(&p) == 0);
    p.connecting = false;
    assert(picker_engine(&p, SC_DB_POSTGRESQL) && picker_choose(&p));
    assert(SC_STR_EQ_LIT(&p.info, p.connection, "host='localhost' "));
    puts("explorer_picker_test: ok");
}
