#include "../examples/explorer/conncfg.c"
#include "../examples/explorer/prefs.c"
#include <assert.h>
#include <stdalign.h>

int main(void) {
    alignas(8) uint8_t mem[2048];
    sc_arena a;
    sc_arena_init(&a, mem, sizeof mem);
    prefs p = prefs_parse(&a, SC_STR_LIT(&a, "# settings\r\nlines_per_row = 2\r\nrows_per_page=100\n"));
    assert(p.lines_per_row == 2 && p.rows_per_page == 100 && p.line == 0);
    p = prefs_parse(&a, SC_STR_LIT(&a, "lines_per_row=3\nrows_per_page=10001\n"));
    assert(p.lines_per_row == 1 && p.rows_per_page == 1000 && p.line == 1);
    p = prefs_parse(&a, SC_STR_LIT(&a, "rows_per_page=10000\nlines_per_row=1\n"));
    assert(p.rows_per_page == 10000 && p.lines_per_row == 1 && p.line == 0);
    p = prefs_parse(&a, SC_STR_LIT(&a, "rows_per_page=99\nlines_per_row=x\n"));
    assert(p.rows_per_page == 1000 && p.lines_per_row == 1 && p.line == 1);
}
