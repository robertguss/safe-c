#include "../examples/explorer/conncfg.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

static alignas(8) uint8_t mem[131072], outmem[131072];

int main(void) {
    sc_arena a, out;
    sc_arena_init(&a, mem, sizeof mem);
    sc_arena_init(&out, outmem, sizeof outmem);
    sc_str text = SC_STR_LIT(&a, "[Prod]\r\nhost = db.example\r\nuser = a'b\\c\n[Local]\ndbname = dev\n");
    conncfg cfg;
    assert(conncfg_parse(&cfg, &a, text, &out) && cfg.count == 2 && cfg.error == CFG_OK);
    conncfg_entry e;
    assert(conncfg_at(&cfg, 0, &e) && e.error == CFG_OK && e.count == 2);
    sc_str info;
    assert(conncfg_info(&cfg, 0, &out, &info));
    assert(SC_STR_EQ_LIT(&out, info, "host='db.example' user='a\\'b\\\\c' "));
    assert(sc_arena_reset(&out));
    text = SC_STR_LIT(&a, "[Bad]\nhost = local\npassword = x\n[Good]\nport = 5432\n");
    assert(conncfg_parse(&cfg, &a, text, &out) && cfg.count == 2);
    assert(conncfg_at(&cfg, 0, &e) && e.error == CFG_PASSWORD && e.line == 3);
    assert(!conncfg_info(&cfg, 0, &out, &info));
    assert(conncfg_at(&cfg, 1, &e) && e.error == CFG_OK);
    const char *bad[] = {"[X]\nwat = x", "[X]\n[X]\n", "[X\n", "[X]\nhost=x\nhost=y", "[X]\nhost=\xff"};
    const conncfg_error errors[] = {CFG_KEY, CFG_DUPLICATE, CFG_SECTION, CFG_DUPLICATE, CFG_UTF8};
    for (size_t i = 0; i < 5; i++) {
        assert(sc_arena_reset(&out));
        text = sc_str_from_lit(&a, (const uint8_t *)bad[i], strlen(bad[i]));
        assert(conncfg_parse(&cfg, &a, text, &out));
        if (errors[i] == CFG_UTF8) assert(cfg.error == CFG_UTF8);
        else assert(conncfg_at(&cfg, cfg.count - 1, &e) && e.error == errors[i]);
    }
    assert(sc_arena_reset(&out));
    sc_buf b = sc_buf_new(sc_arena_alloc(&a, 8192));
    for (unsigned i = 0; i < 65; i++) {
        assert(SC_BUF_LIT(&a, &b, "[server") && sc_buf_u64(&a, &b, i) && SC_BUF_LIT(&a, &b, "]\n"));
    }
    assert(conncfg_parse(&cfg, &a, sc_buf_view(&a, b), &out) && cfg.count == 64 && cfg.error == CFG_LIMIT);
    puts("explorer_conncfg_test: ok");
}
