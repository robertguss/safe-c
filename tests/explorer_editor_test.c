/* Tests the explorer's SQL editor (examples/explorer/editor.c) against the
 * plan's U22 scenarios. Core code, built in. */
#include "../examples/explorer/editor.c"
#include "../examples/explorer/editor_syntax.c"
#include "../examples/explorer/frame.c"
#include "../examples/explorer/sqlkw.c"
#include "../examples/explorer/sqltok.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

static alignas(8) uint8_t ed_mem[EDITOR_BYTES];
static alignas(8) uint8_t scratch_mem[EDITOR_SCRATCH_BYTES];
static alignas(8) uint8_t ev_mem[EDITOR_CAP + 64];
static alignas(8) uint8_t out_mem[EDITOR_CAP + 64];
static alignas(8) uint8_t kw_mem[16384];
static sc_arena ed_a, scratch_a, ev_a, out_a, kw_a;
static editor ed;
static sqlkw kw;
static uint64_t now;

static void fresh(void) {
    sc_arena_init(&ed_a, ed_mem, sizeof ed_mem);
    sc_arena_init(&scratch_a, scratch_mem, sizeof scratch_mem);
    assert(editor_init(&ed, &ed_a, &scratch_a, true));
    now = 1000;
}

static sc_str lit(const char *s, size_t n) {
    assert(sc_arena_reset(&ev_a));
    sc_str t = {NULL, 0, ev_a.epoch};
    return n > 0 ? sc_str_from_lit(&ev_a, (const uint8_t *)s, n) : t;
}

static editor_status paste(const char *s) {
    return editor_insert(&ed, &ev_a, lit(s, strlen(s)), false, now);
}

static void type(const char *s, uint64_t gap) {
    for (size_t i = 0; s[i] != 0; i++) {
        now += gap;
        assert(editor_insert(&ed, &ev_a, lit(s + i, 1), true, now) == EDITOR_OK);
    }
}

static bool key(editor_cmd c, bool extend) {
    editor_press p = {c, extend};
    return editor_do(&ed, p, now);
}

static bool is(const char *want) {
    size_t n = strlen(want);
    if (editor_len(&ed) != n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        uint8_t c = 0;
        if (!editor_byte(&ed, i, &c) || c != (uint8_t)want[i]) {
            return false;
        }
    }
    return true;
}

static bool runs(bool all, size_t from, size_t to) {
    size_t f = 1, t = 0;
    editor_run_range(&ed, all, &f, &t);
    return f == from && t == to;
}

static size_t undos(void) {
    size_t n = 0;
    while (key(EDITOR_CMD_UNDO, false)) {
        n++;
    }
    return n;
}

static void gap_buffer(void) {
    fresh();
    assert(paste("world") == EDITOR_OK);
    editor_select(&ed, 0, 0);
    assert(paste("hello ") == EDITOR_OK);
    editor_select(&ed, 11, 11);
    assert(paste("!") == EDITOR_OK);
    editor_select(&ed, 5, 5);
    assert(paste(",") == EDITOR_OK);
    editor_select(&ed, 0, 0);
    editor_select(&ed, 13, 13);
    assert(paste("?") == EDITOR_OK);
    assert(is("hello, world!?"));
    sc_str all = editor_copy(&ed, 0, editor_len(&ed), &out_a);
    assert(all.len == 14 && memcmp(all.ptr, "hello, world!?", 14) == 0);
    assert(paste("\r\nx\ry") == EDITOR_OK && is("hello, world!?\nxy"));
    assert(paste("a\x01") == EDITOR_NOT_TEXT && paste("\xC3") == EDITOR_NOT_TEXT && is("hello, world!?\nxy"));
}

static void undo_groups(void) {
    fresh();
    type("select", 100);
    assert(is("select") && undos() == 1 && is(""));
    fresh();
    type("sel", 100);
    now += 600;
    type("ect", 100);
    assert(undos() == 2 && is(""));
    fresh();
    type("select ", 100);
    assert(paste("1") == EDITOR_OK);
    type(" + 2", 100);
    assert(is("select 1 + 2") && key(EDITOR_CMD_UNDO, false) && is("select 1") &&
           key(EDITOR_CMD_UNDO, false) && is("select ") && key(EDITOR_CMD_UNDO, false) && is(""));
    fresh();
    type("select 1", 100);
    assert(runs(false, 0, 8));
    type("0", 10);
    assert(is("select 10") && key(EDITOR_CMD_UNDO, false) && is("select 1"));
    fresh();
    type("abcd", 10);
    now += 1000;
    assert(key(EDITOR_CMD_BACKSPACE, false) && key(EDITOR_CMD_BACKSPACE, false) && is("ab"));
    assert(key(EDITOR_CMD_UNDO, false) && is("abcd") && key(EDITOR_CMD_REDO, false) && is("ab"));
}

static void redo_and_ring(void) {
    fresh();
    type("ab", 10);
    now += 1000;
    type("cd", 10);
    assert(key(EDITOR_CMD_UNDO, false) && is("ab"));
    type("x", 10);
    assert(!key(EDITOR_CMD_REDO, false) && is("abx"));
    fresh();
    for (int i = 0; i < 101; i++) {
        char c[2] = {(char)('A' + i % 26), 0};
        editor_select(&ed, 0, editor_len(&ed));
        assert(paste(c) == EDITOR_OK);
    }
    assert(is("W") && undos() == 100 && is("A") && key(EDITOR_CMD_REDO, false) && is("B"));
}

static void full_replace(void) {
    static char big[EDITOR_CAP + 1];
    fresh();
    memset(big, 'a', EDITOR_CAP);
    assert(paste(big) == EDITOR_OK);
    assert(paste("b") == EDITOR_TOO_BIG);
    assert(key(EDITOR_CMD_SELECT_ALL, false));
    memset(big, 'b', EDITOR_CAP);
    assert(paste(big) == EDITOR_OK);
    uint8_t c = 0;
    assert(editor_len(&ed) == EDITOR_CAP && editor_byte(&ed, 7, &c) && c == 'b');
    assert(key(EDITOR_CMD_UNDO, false) && editor_len(&ed) == EDITOR_CAP && editor_byte(&ed, 7, &c) && c == 'a');
}

static void selection(void) {
    size_t from = 0, to = 0;
    fresh();
    assert(paste("abcdef") == EDITOR_OK);
    assert(key(EDITOR_CMD_DOC_START, false));
    for (int i = 0; i < 3; i++) {
        assert(key(EDITOR_CMD_RIGHT, true));
    }
    editor_selection(&ed, &from, &to);
    assert(from == 0 && to == 3);
    type("x", 10);
    assert(is("xdef"));
    fresh();
    assert(paste("foo_bar baz") == EDITOR_OK);
    assert(key(EDITOR_CMD_DOC_START, false) && key(EDITOR_CMD_WORD_RIGHT, false) && ed.cursor == 7);
    assert(key(EDITOR_CMD_WORD_RIGHT, false) && ed.cursor == 11 && key(EDITOR_CMD_WORD_LEFT, false) &&
           ed.cursor == 8);
    editor_select_word(&ed, 2);
    editor_selection(&ed, &from, &to);
    assert(from == 0 && to == 7);
    fresh();
    assert(paste("abcd\nefgh") == EDITOR_OK);
    editor_select(&ed, 2, 2);
    assert(key(EDITOR_CMD_DOWN, false) && ed.cursor == 7 && key(EDITOR_CMD_UP, false) && ed.cursor == 2);
    assert(key(EDITOR_CMD_LINE_END, false) && ed.cursor == 4 && key(EDITOR_CMD_LINE_START, true));
    editor_selection(&ed, &from, &to);
    assert(from == 0 && to == 4 && key(EDITOR_CMD_DOC_END, false) && ed.cursor == 9 && ed.anchor == 9);
    fresh();
    assert(paste("ab\ncd\n") == EDITOR_OK);
    editor_select_line(&ed, 4);
    editor_selection(&ed, &from, &to);
    assert(from == 3 && to == 6);
}

static void code_points(void) {
    fresh();
    assert(paste("a\xF0\x9F\x98\x80" "e\xCC\x81") == EDITOR_OK);
    assert(key(EDITOR_CMD_LEFT, false) && ed.cursor == 6);
    assert(key(EDITOR_CMD_LEFT, false) && ed.cursor == 5);
    assert(key(EDITOR_CMD_LEFT, false) && ed.cursor == 1);
    assert(key(EDITOR_CMD_RIGHT, false) && ed.cursor == 5);
    assert(key(EDITOR_CMD_BACKSPACE, false) && is("ae\xCC\x81"));
}

static bool stmt(const char *text, size_t caret, const char *want) {
    fresh();
    assert(paste(text) == EDITOR_OK);
    editor_select(&ed, caret, caret);
    size_t from = 0, to = 0;
    if (!editor_statement(&ed, &from, &to)) {
        return want == NULL;
    }
    return want != NULL && to - from == strlen(want) && memcmp(text + from, want, to - from) == 0;
}

static void statements(void) {
    assert(stmt("select 1;\n\nselect 2", 10, "select 1;"));
    assert(stmt("select 1;\n\nselect 2", 11, "select 2"));
    assert(stmt("  select 1", 0, "select 1"));
    assert(stmt("select 1; -- note\nselect 2;", 14, "select 1;"));
    assert(stmt("select ';'; select 2", 3, "select ';';"));
    assert(stmt("  -- only a comment\n", 3, NULL));
    fresh();
    assert(paste("select 1; select 2") == EDITOR_OK);
    editor_select(&ed, 7, 18);
    assert(runs(false, 7, 18) && runs(true, 0, 18));
    editor_select(&ed, 18, 18);
    assert(runs(false, 10, 18));
}

static void dialect_ranges(void) {
    fresh();
    assert(paste("select [a;b]; select 8;") == EDITOR_OK);
    editor_select(&ed, 0, 0);
    assert(runs(false, 0, 10));
    editor_set_syntax(&ed, true, SQLTOK_SQLITE);
    assert(runs(false, 0, strlen("select [a;b];")));
    editor_set_syntax(&ed, true, SQLTOK_POSTGRESQL);
    assert(runs(false, 0, 10));
    fresh();
    const char *trigger = "CREATE TRIGGER x AFTER INSERT ON t BEGIN SELECT end FROM t; "
                          "SELECT CASE WHEN 1 THEN 'a;b' END; SELECT begin FROM t; END;";
    assert(paste(trigger) == EDITOR_OK && paste(" SELECT 99;") == EDITOR_OK);
    editor_set_syntax(&ed, true, SQLTOK_SQLITE);
    editor_select(&ed, 65, 65);
    assert(runs(false, 0, strlen(trigger)));
    editor_select(&ed, 8, 15);
    assert(runs(false, 8, 15) && runs(true, 0, strlen(trigger) + strlen(" SELECT 99;")));
}

static void composition(void) {
    fresh();
    type("ab", 10);
    size_t steps = ed.count;
    sc_str c = lit("\xE3\x81\xAB", 3);
    editor_compose(&ed, &ev_a, c, 3);
    assert(editor_compose_text(&ed).len == 3 && is("ab"));
    assert(key(EDITOR_CMD_ESCAPE, false) && editor_compose_text(&ed).len == 0);
    assert(is("ab") && ed.count == steps && ed.done == steps);
    editor_compose(&ed, &ev_a, lit("\xE3\x81\xAB", 3), 3);
    type("x", 10);
    assert(is("abx") && editor_compose_text(&ed).len == 0);
    type("y", 10);
    assert(key(EDITOR_CMD_UNDO, false) && is("abx"));
}

static void line_commands(void) {
    fresh();
    assert(paste("select 1\nfrom t\n\nwhere x") == EDITOR_OK);
    editor_select(&ed, 2, 12);
    assert(key(EDITOR_CMD_COMMENT, false) && is("-- select 1\n-- from t\n\nwhere x"));
    assert(key(EDITOR_CMD_COMMENT, false) && is("select 1\nfrom t\n\nwhere x"));
    assert(key(EDITOR_CMD_INDENT, false) && is("  select 1\n  from t\n\nwhere x"));
    assert(key(EDITOR_CMD_OUTDENT, false) && is("select 1\nfrom t\n\nwhere x"));
    assert(!key(EDITOR_CMD_OUTDENT, false));
    editor_select(&ed, 19, 19);
    assert(key(EDITOR_CMD_INDENT, false) && is("select 1\nfrom t\n\n  where x") && ed.cursor == 21);
    assert(key(EDITOR_CMD_OUTDENT, false) && is("select 1\nfrom t\n\nwhere x") && ed.cursor == 19);
    assert(key(EDITOR_CMD_TAB, false) && is("select 1\nfrom t\n\nwh  ere x"));
    assert(key(EDITOR_CMD_UNDO, false) && key(EDITOR_CMD_UNDO, false) && is("select 1\nfrom t\n\n  where x"));
    editor_select(&ed, 0, editor_len(&ed));
    assert(key(EDITOR_CMD_COMMENT, false) && is("-- select 1\n-- from t\n\n--   where x"));
    assert(key(EDITOR_CMD_COMMENT, false) && is("select 1\nfrom t\n\n  where x"));
}

static void inks(size_t from, size_t to, uint8_t *out) {
    sc_tslice sp;
    assert(sc_arena_reset(&out_a));
    size_t n = editor_spans(&ed, &kw, from, to, &out_a, &sp);
    size_t at_ = from;
    for (size_t i = 0; i < n; i++) {
        editor_span s;
        assert(SC_TSLICE_GET(editor_span, &out_a, sp, i, &s) && s.from == at_ && s.to > s.from);
        for (size_t k = s.from; k < s.to; k++) {
            out[k - from] = s.ink;
        }
        at_ = s.to;
    }
    assert(at_ == to);
}

static void highlighting(void) {
    static uint8_t got[64];
    fresh();
    assert(paste("select 'a' -- c\nfrom t") == EDITOR_OK);
    inks(0, editor_len(&ed), got);
    assert(got[0] == EDITOR_INK_KEYWORD && got[7] == EDITOR_INK_STRING && got[12] == EDITOR_INK_COMMENT &&
           got[16] == EDITOR_INK_KEYWORD && got[21] == EDITOR_INK_TEXT);
    editor_select(&ed, 0, 0);
    assert(paste("/*") == EDITOR_OK);
    inks(0, editor_len(&ed), got);
    for (size_t i = 0; i < editor_len(&ed); i++) {
        assert(got[i] == EDITOR_INK_COMMENT);
    }
    editor_select(&ed, 18, 18);
    assert(paste("*/") == EDITOR_OK);
    inks(20, editor_len(&ed), got);
    assert(got[0] == EDITOR_INK_KEYWORD && got[5] == EDITOR_INK_TEXT);
}

static void dollar_relex(void) {
    static char body[100u * 1024u + 64u];
    fresh();
    size_t n = 0;
    memcpy(body, "select $f$", 10);
    n = 10;
    while (n < 100u * 1024u) {
        memcpy(body + n, "x;'\n", 4);
        n += 4;
    }
    memcpy(body + n, "$f$; select 1", 13);
    n += 13;
    body[n] = 0;
    assert(paste(body) == EDITOR_OK && ed.cp_count > 20);
    ed.relexed = 0;
    editor_select(&ed, 0, 0);
    assert(paste(" ") == EDITOR_OK);
    assert(ed.relexed <= 3u * EDITOR_WINDOW);
    static uint8_t got[64];
    inks(editor_len(&ed) - 12, editor_len(&ed), got);
    assert(got[0] == EDITOR_INK_STRING && got[4] == EDITOR_INK_KEYWORD);
    editor_select(&ed, 9, 9);
    assert(paste("g") == EDITOR_OK);
    inks(editor_len(&ed) - 12, editor_len(&ed), got);
    assert(got[0] == EDITOR_INK_STRING && got[4] == EDITOR_INK_STRING);
    memcpy(body, "select $b$", 10);
    memcpy(body + n - 13, "$a$", 3);
    fresh();
    assert(paste(body) == EDITOR_OK);
    inks(editor_len(&ed) - 8, editor_len(&ed), got);
    assert(got[0] == EDITOR_INK_STRING);
    editor_select(&ed, 8, 9);
    assert(paste("a") == EDITOR_OK);
    inks(editor_len(&ed) - 8, editor_len(&ed), got);
    assert(got[0] == EDITOR_INK_KEYWORD);
    fresh();
    n = 0;
    memcpy(body, "select $$x$$;\n", 14);
    for (n = 14; n < 100u * 1024u; n += 10) {
        memcpy(body + n, "select 1;\n", 10);
    }
    body[n] = 0;
    assert(paste(body) == EDITOR_OK && ed.cp_count > 20);
}

static void keys(void) {
    editor_press p = editor_press_for(UI_KEY_LEFT, UI_MOD_ALT | UI_MOD_SHIFT, UI_PLATFORM_MAC);
    assert(p.cmd == EDITOR_CMD_WORD_LEFT && p.extend);
    assert(editor_press_for(UI_KEY_UP, UI_MOD_CMD, UI_PLATFORM_MAC).cmd == EDITOR_CMD_DOC_START);
    assert(editor_press_for('z', UI_MOD_CMD | UI_MOD_SHIFT, UI_PLATFORM_MAC).cmd == EDITOR_CMD_REDO);
    assert(editor_press_for('a', UI_MOD_CTRL, UI_PLATFORM_MAC).cmd == EDITOR_CMD_LINE_START);
    assert(editor_press_for('a', UI_MOD_CTRL | UI_MOD_CMD, UI_PLATFORM_OTHER).cmd == EDITOR_CMD_SELECT_ALL);
    assert(editor_press_for(UI_KEY_RETURN, UI_MOD_CMD, UI_PLATFORM_MAC).cmd == EDITOR_CMD_NONE);
    assert(editor_press_for('/', UI_MOD_CMD, UI_PLATFORM_MAC).cmd == EDITOR_CMD_COMMENT);
}

static void scrolling(void) {
    fresh();
    for (int i = 0; i < 30; i++) {
        assert(paste("line\n") == EDITOR_OK);
    }
    editor_scroll(&ed, 10, 80);
    assert(ed.top == 21 && editor_line_of(&ed, ed.cursor) == 30);
    assert(key(EDITOR_CMD_DOC_START, false));
    editor_scroll(&ed, 10, 80);
    assert(ed.top == 0 && ed.top_off == 0);
    assert(editor_wheel(&ed, 5) && ed.top == 5 && ed.top_off == 25 && editor_line_after_top(&ed, 1) == 30);
    assert(editor_wheel(&ed, -2) && ed.top == 3 && ed.top_off == 15);
    editor_select(&ed, 0, 0);
    assert(paste("x\ny\n") == EDITOR_OK && ed.top == 0 && ed.lines == 33);
}

int main(void) {
    sc_arena_init(&ev_a, ev_mem, sizeof ev_mem);
    sc_arena_init(&out_a, out_mem, sizeof out_mem);
    sc_arena_init(&kw_a, kw_mem, sizeof kw_mem);
    assert(sqlkw_init(&kw, &kw_a, SQLTOK_POSTGRESQL));
    gap_buffer();
    undo_groups();
    redo_and_ring();
    full_replace();
    selection();
    code_points();
    statements();
    dialect_ranges();
    composition();
    line_commands();
    highlighting();
    dollar_relex();
    keys();
    scrolling();
    puts("explorer_editor_test: ok");
    return 0;
}
