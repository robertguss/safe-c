/* Tests the explorer's result tabs (examples/explorer/tabs.c): the strip's
 * spans and hits, the selection after a close, the tab keys, titles and the
 * run queue (plan U12). Core code, built in. */
#include "../examples/explorer/tabs.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

alignas(8) static uint8_t mem[4096];
static sc_arena A;

static sc_str lit(const char *s) {
    return sc_str_from_lit(&A, (const uint8_t *)s, strlen(s));
}

static bool is(sc_str s, const char *want) {
    return s.len == strlen(want) && (s.len == 0 || memcmp(s.ptr, want, s.len) == 0);
}

static void spans(void) {
    /* 1000 points, 3 tabs: (1000 - 28) / 3 = 324, capped at 220. */
    tab_span t = tab_span_at(1000, 3, 2);
    assert(t.x == 440 && t.w == 220);
    assert(tab_plus_span(1000, 3).x == 660 && tab_plus_span(1000, 3).w == 28);
    /* 400 points, 4 tabs: (400 - 28) / 4 = 93 each, not capped. */
    t = tab_span_at(400, 4, 3);
    assert(t.x == 279 && t.w == 93);
    assert(tab_close_span(t).x == 285 && tab_close_span(t).w == 16);
    assert(tab_span_at(400, 4, 4).w == 0); /* past the end */
    /* Nine tabs: no +, and the tabs take the whole strip. */
    assert(tab_plus_span(900, TABS_MAX).w == 0);
    assert(tab_span_at(900, TABS_MAX, 8).x == 800 && tab_span_at(900, TABS_MAX, 8).w == 100);
    /* A tab too narrow for its close box has none. */
    assert(tab_close_span(tab_span_at(150, 8, 0)).w == 0);
}

static void hits(void) {
    /* 1000 points, 3 tabs of 220, + at 660..687. */
    tab_hit h = tab_hit_at(1000, 3, 250);
    assert(h.kind == TAB_HIT_SELECT && h.tab == 1);
    h = tab_hit_at(1000, 3, 220 + 6); /* tab 1's close box starts here */
    assert(h.kind == TAB_HIT_CLOSE && h.tab == 1);
    h = tab_hit_at(1000, 3, 220 + 6 + 16); /* just past it */
    assert(h.kind == TAB_HIT_SELECT && h.tab == 1);
    assert(tab_hit_at(1000, 3, 219).tab == 0 && tab_hit_at(1000, 3, 659).tab == 2);
    assert(tab_hit_at(1000, 3, 660).kind == TAB_HIT_NEW);
    assert(tab_hit_at(1000, 3, 687).kind == TAB_HIT_NEW);
    assert(tab_hit_at(1000, 3, 688).kind == TAB_HIT_NONE);
    assert(tab_hit_at(1000, 3, -1).kind == TAB_HIT_NONE);
    assert(tab_hit_at(1000, 3, 1000).kind == TAB_HIT_NONE);
    assert(tab_hit_at(1000, 0, 5).kind == TAB_HIT_NEW); /* no tabs: the + is first */
}

static void after_close(void) {
    assert(tab_after_close(5, 1, 3) == 1); /* before the closed one: unchanged */
    assert(tab_after_close(5, 4, 3) == 3); /* after it: shifts left */
    assert(tab_after_close(5, 3, 3) == 3); /* the closed one: its right neighbour */
    assert(tab_after_close(5, 4, 4) == 3); /* the last one: the new last */
    assert(tab_after_close(1, 0, 0) == 0);
}

static void keys(void) {
    assert(tab_key_for('t', UI_MOD_GUI).kind == TAB_KEY_NEW);
    assert(tab_key_for('T', UI_MOD_CTRL).kind == TAB_KEY_NEW);
    assert(tab_key_for('w', UI_MOD_GUI).kind == TAB_KEY_CLOSE);
    tab_key k = tab_key_for('1', UI_MOD_GUI);
    assert(k.kind == TAB_KEY_SELECT && k.tab == 0);
    k = tab_key_for('9', UI_MOD_GUI);
    assert(k.kind == TAB_KEY_SELECT && k.tab == 8);
    assert(tab_key_for('0', UI_MOD_GUI).kind == TAB_KEY_NONE);
    assert(tab_key_for(UI_KEY_RETURN, UI_MOD_GUI | UI_MOD_SHIFT).kind == TAB_KEY_LOAD_ALL);
    assert(tab_key_for(UI_KEY_KP_ENTER, UI_MOD_CTRL | UI_MOD_SHIFT).kind == TAB_KEY_LOAD_ALL);
    /* Plain ⌘↩ runs (the input box's key); no ⌘, or with ⌥, is no tab key. */
    assert(tab_key_for(UI_KEY_RETURN, UI_MOD_GUI).kind == TAB_KEY_NONE);
    assert(tab_key_for('t', 0).kind == TAB_KEY_NONE);
    assert(tab_key_for('t', UI_MOD_GUI | UI_MOD_ALT).kind == TAB_KEY_NONE);
    assert(tab_key_for('w', UI_MOD_GUI | UI_MOD_SHIFT).kind == TAB_KEY_NONE);
}

static void titles(void) {
    bool cut = true;
    assert(is(tab_title(&A, lit("SELECT 1"), 40, &cut), "SELECT 1") && !cut);
    /* Blank lines and the spaces round the first line go. */
    assert(is(tab_title(&A, lit("\n  \t\r\n  SELECT g  \r\nFROM t"), 40, &cut), "SELECT g") && cut);
    /* Text only on later blank-looking lines does not count as more. */
    assert(is(tab_title(&A, lit("SELECT 2\n   \n\t"), 40, &cut), "SELECT 2") && !cut);
    /* Cut at a character boundary, never inside the 2-byte é. */
    assert(is(tab_title(&A, lit("SELECT 'caf\xC3\xA9'"), 12, &cut), "SELECT 'caf") && cut);
    assert(is(tab_title(&A, lit("SELECT 'caf\xC3\xA9'"), 13, &cut), "SELECT 'caf\xC3\xA9") && cut);
    assert(is(tab_title(&A, lit("SELECT 'caf\xC3\xA9'"), 14, &cut), "SELECT 'caf\xC3\xA9'") && !cut);
    assert(is(tab_title(&A, lit(" \n\t "), 40, &cut), "") && !cut);
    assert(is(tab_title(&A, lit(""), 40, &cut), "") && !cut);
}

static void queue(void) {
    alignas(8) static uint8_t qm[256];
    sc_arena q;
    sc_arena_init(&q, qm, sizeof qm);
    sc_tslice w = SC_TSLICE_ALLOC(uint64_t, &q, 5);
    const uint64_t v[5] = {0, 7, 0, 3, 9};
    for (size_t i = 0; i < 5; i++) {
        assert(SC_TSLICE_SET(uint64_t, &q, w, i, &v[i]));
    }
    assert(tab_next_waiting(&q, w, 5) == 3); /* queued first */
    assert(tab_next_waiting(&q, w, 3) == 1); /* only the first three count */
    assert(tab_next_waiting(&q, w, 1) == SIZE_MAX);
    const uint64_t z = 0;
    assert(SC_TSLICE_SET(uint64_t, &q, w, 3, &z));
    assert(tab_next_waiting(&q, w, 5) == 1);
}

int main(void) {
    sc_arena_init(&A, mem, sizeof mem);
    spans();
    hits();
    after_close();
    keys();
    titles();
    queue();
    puts("explorer_tabs_test: ok");
    return 0;
}
