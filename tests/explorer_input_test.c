/* Tests the explorer's SQL input box (examples/explorer/input.c) against the
 * plan's U8 scenarios. Core code, built in. */
#include "../examples/explorer/input.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

static alignas(8) uint8_t box_mem[INPUT_CAP + 64];
static alignas(8) uint8_t ev_mem[1u << 16];
static sc_arena box_a, ev_a;
static input_box in;

static void fresh(size_t cap) {
    sc_arena_init(&box_a, box_mem, sizeof box_mem);
    assert(input_init(&in, &box_a, cap));
}

/* Types `s` the way text events arrive: bytes from another arena. */
static input_status type(const char *s) {
    assert(sc_arena_reset(&ev_a));
    sc_str t = {NULL, 0, ev_a.epoch};
    if (s[0] != 0) {
        t = sc_str_from_lit(&ev_a, (const uint8_t *)s, strlen(s));
    }
    return input_insert(&in, &ev_a, t);
}

static bool is(const char *want) {
    sc_str t = input_text(&in);
    return t.len == strlen(want) && (t.len == 0 || memcmp(t.ptr, want, t.len) == 0);
}

static void type_and_backspace(void) {
    fresh(INPUT_CAP);
    assert(type("sel") == INPUT_OK);
    input_backspace(&in);
    assert(type("lect 1") == INPUT_OK);
    assert(is("select 1") && in.cursor == 8);
}

static void edit_in_the_middle(void) {
    fresh(INPUT_CAP);
    assert(type("selct 1") == INPUT_OK);
    for (int i = 0; i < 4; i++) {
        input_left(&in);
    }
    assert(type("e") == INPUT_OK);
    assert(is("select 1") && in.cursor == 4);
    input_backspace(&in); /* backspace in the middle removes the byte before */
    assert(is("selct 1") && in.cursor == 3);
}

static void edges_are_no_ops(void) {
    fresh(INPUT_CAP);
    input_backspace(&in);
    input_left(&in);
    input_up(&in);
    assert(is("") && in.cursor == 0);
    assert(type("ab") == INPUT_OK);
    input_right(&in); /* at the end */
    assert(in.cursor == 2);
    input_left(&in);
    input_left(&in);
    input_left(&in); /* past the start */
    assert(in.cursor == 0);
    input_backspace(&in);
    assert(is("ab"));
}

static void select_all_replaces(void) {
    fresh(INPUT_CAP);
    assert(type("select 1") == INPUT_OK);
    input_select_all(&in);
    assert(type("select 2") == INPUT_OK); /* a paste arrives the same way */
    assert(is("select 2") && in.cursor == 8 && !in.all);
    input_select_all(&in);
    input_backspace(&in);
    assert(is("") && in.cursor == 0);
    assert(type("x") == INPUT_OK);
    input_select_all(&in);
    input_left(&in); /* a move ends the selection */
    assert(type("y") == INPUT_OK);
    assert(is("yx"));
}

/* A paste that does not fit is refused whole; the buffer and the selection
 * are as they were. So is one with a byte outside ASCII. */
static void oversized_paste_refused(void) {
    static char big[2u * 1024u * 1024u + 1];
    memset(big, 'x', sizeof big - 1);
    fresh(INPUT_CAP);
    assert(type("select 1") == INPUT_OK);
    sc_arena huge;
    static alignas(8) uint8_t huge_mem[2u * 1024u * 1024u + 64];
    sc_arena_init(&huge, huge_mem, sizeof huge_mem);
    sc_str paste = sc_str_from_lit(&huge, (const uint8_t *)big, sizeof big - 1);
    assert(paste.len == sizeof big - 1);
    input_select_all(&in);
    assert(input_insert(&in, &huge, paste) == INPUT_TOO_BIG);
    assert(is("select 1") && in.all);

    /* Exactly the capacity fits; one byte more does not. */
    fresh(16);
    assert(type("0123456789") == INPUT_OK);
    assert(type("abcdef") == INPUT_OK && in.len == 16);
    assert(type("g") == INPUT_TOO_BIG && in.len == 16);
    input_select_all(&in);
    assert(type("0123456789abcdefg") == INPUT_TOO_BIG && in.len == 16);
    assert(type("0123456789abcdef") == INPUT_OK && in.len == 16);

    fresh(INPUT_CAP);
    assert(type("ab") == INPUT_OK);
    assert(type("caf\xc3\xa9") == INPUT_NOT_ASCII && is("ab"));
    assert(type("x\ry") == INPUT_NOT_ASCII && is("ab"));
    assert(type("\t\n~ ") == INPUT_OK && is("ab\t\n~ "));
}

static void lines_up_and_down(void) {
    fresh(INPUT_CAP);
    assert(type("select\n  1,\nx") == INPUT_OK); /* lines: 6, 4, 1 bytes */
    size_t line = 9, col = 9;
    input_cursor_at(&in, &line, &col);
    assert(line == 2 && col == 1);
    input_up(&in);
    input_cursor_at(&in, &line, &col);
    assert(line == 1 && col == 1 && in.cursor == 8);
    input_right(&in);
    input_right(&in);
    input_right(&in); /* end of "  1," */
    input_up(&in);
    input_cursor_at(&in, &line, &col);
    assert(line == 0 && col == 4 && in.cursor == 4);
    input_up(&in); /* first line: to the start */
    assert(in.cursor == 0);
    input_down(&in);
    input_cursor_at(&in, &line, &col);
    assert(line == 1 && col == 0 && in.cursor == 7);
    input_down(&in); /* column 0 of "x" */
    input_cursor_at(&in, &line, &col);
    assert(line == 2 && col == 0);
    input_right(&in);
    input_down(&in); /* last line: to the end */
    assert(in.cursor == in.len);
    in.cursor = 6; /* end of "select" */
    input_down(&in); /* line 1 has 4 bytes: clamp to its end */
    assert(in.cursor == 11);
    input_down(&in); /* line 2 has 1 byte */
    assert(in.cursor == 13);
    assert(type("\n") == INPUT_OK);
    input_cursor_at(&in, &line, &col);
    assert(line == 3 && col == 0);
}

/* The box's arena reset under it: every edit refuses, nothing is read. */
static void stale_box(void) {
    fresh(INPUT_CAP);
    assert(type("abc") == INPUT_OK);
    assert(sc_arena_reset(&box_a));
    assert(type("d") == INPUT_STALE);
    assert(input_text(&in).len == 0);
    input_backspace(&in);
    input_up(&in);
}

int main(void) {
    sc_arena_init(&ev_a, ev_mem, sizeof ev_mem);
    type_and_backspace();
    edit_in_the_middle();
    edges_are_no_ops();
    select_all_replaces();
    oversized_paste_refused();
    lines_up_and_down();
    stale_box();
    puts("explorer_input_test: ok");
    return 0;
}
