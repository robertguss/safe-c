/* Tests the explorer's frame builder (examples/explorer/frame.c). The builder
 * is application core code, not stdlib, so it is built into this test directly. */
#include "../examples/explorer/frame.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>

#define ARENA_BYTES (1u << 16)

static alignas(8) uint8_t storage[ARENA_BYTES];
static alignas(8) uint8_t other_storage[ARENA_BYTES];
static sc_arena a;
static sc_arena other;

static const ui_color black = {0, 0, 0, 255};

static ui_node node_at(ui_frame f, size_t i) {
    ui_node n;
    assert(SC_TSLICE_GET(ui_node, &a, f.nodes, i, &n));
    return n;
}

/* root(0) > panel(1) > "Hi"(2), then a second panel(3) back under root. */
static void nesting_records_parents(void) {
    frame_builder b;
    ui_frame f;
    assert(frame_begin(&b, &a, 8, 8));
    assert(frame_open(&b, frame_box()));
    assert(frame_open(&b, frame_box()));
    assert(frame_text(&b, SC_STR_LIT(&a, "Hi"), UI_FONT_SANS, 13, black));
    assert(frame_close(&b));
    assert(frame_open(&b, frame_box()));
    assert(frame_close(&b));
    assert(frame_close(&b));
    assert(frame_end(&b, &f));
    assert(f.count == 4);
    assert(node_at(f, 0).parent == UI_NO_PARENT);
    assert(node_at(f, 1).parent == 0);
    assert(node_at(f, 2).parent == 1 && node_at(f, 2).kind == UI_NODE_TEXT);
    assert(node_at(f, 3).parent == 0); /* not 1: the first panel was closed */
    assert(ui_frame_valid(&a, f));
}

/* Writes `parents` into a fresh frame of boxes, except node `leaf_at`, which
 * is a text node or, with `custom`, a custom node with no draws. */
static ui_frame by_hand_kind(const uint32_t *parents, size_t n, size_t leaf_at, bool custom) {
    ui_frame f = {SC_TSLICE_ALLOC(ui_node, &a, n), n, {{NULL, 0, a.epoch}, 0}, 0};
    for (size_t i = 0; i < n; i++) {
        ui_node node = frame_box();
        node.parent = parents[i];
        node.kind = i != leaf_at ? UI_NODE_BOX : custom ? UI_NODE_CUSTOM : UI_NODE_TEXT;
        node.font_size = 13;
        assert(SC_TSLICE_SET(ui_node, &a, f.nodes, i, &node));
    }
    return f;
}

static ui_frame by_hand(const uint32_t *parents, size_t n, size_t text_at) {
    return by_hand_kind(parents, n, text_at, false);
}

static void bad_parents_are_rejected(void) {
    const uint32_t ok[] = {UI_NO_PARENT, 0, 1, 0};
    const uint32_t self[] = {UI_NO_PARENT, 1};
    const uint32_t later[] = {UI_NO_PARENT, 2, 0};
    const uint32_t first_has_parent[] = {0};
    /* 3 names 1, but 2 is 1's sibling, so 1's subtree closed before 3. */
    const uint32_t closed[] = {UI_NO_PARENT, 0, 0, 1};
    const uint32_t under_text[] = {UI_NO_PARENT, 0, 1};
    assert(ui_frame_valid(&a, by_hand(ok, 4, 99)));
    assert(!ui_frame_valid(&a, by_hand(self, 2, 99)));
    assert(!ui_frame_valid(&a, by_hand(later, 3, 99)));
    assert(!ui_frame_valid(&a, by_hand(first_has_parent, 1, 99)));
    assert(!ui_frame_valid(&a, by_hand(closed, 4, 99)));
    assert(!ui_frame_valid(&a, by_hand(under_text, 3, 1)));
    assert(!ui_frame_valid(&a, by_hand_kind(under_text, 3, 1, true)));
    assert(ui_frame_valid(&a, by_hand_kind(ok, 4, 2, true)));
    ui_frame too_long = by_hand(ok, 4, 99);
    too_long.count = 5;
    assert(!ui_frame_valid(&a, too_long));
}

static void unbalanced_is_rejected(void) {
    frame_builder b;
    ui_frame f = {{{NULL, 0, 0}, 0}, 0, {{NULL, 0, 0}, 0}, 0};
    assert(frame_begin(&b, &a, 8, 8));
    assert(frame_open(&b, frame_box()));
    assert(frame_close(&b));
    assert(!frame_close(&b));
    assert(!frame_open(&b, frame_box())); /* the failure sticks */
    assert(!frame_end(&b, &f));
    assert(f.count == 0);

    assert(frame_begin(&b, &a, 8, 8));
    assert(frame_open(&b, frame_box()));
    assert(frame_open(&b, frame_box()));
    assert(frame_close(&b));
    assert(!frame_end(&b, &f));
    assert(f.count == 0);
}

static void full_builder_drops_the_frame(void) {
    frame_builder b;
    ui_frame f = {{{NULL, 0, 0}, 0}, 0, {{NULL, 0, 0}, 0}, 0};
    assert(frame_begin(&b, &a, 2, 0));
    assert(frame_open(&b, frame_box()));
    assert(frame_leaf(&b, frame_box()));
    assert(!frame_leaf(&b, frame_box()));
    assert(!frame_close(&b));
    assert(!frame_end(&b, &f));
    assert(f.count == 0);
    assert(!frame_begin(&b, &a, 0, 0));
    assert(!frame_begin(&b, &a, ARENA_BYTES, 0));
    assert(!frame_begin(&b, &a, 4, ARENA_BYTES));
}

static void text_lives_in_the_frame_arena(void) {
    frame_builder b;
    ui_frame f;
    sc_str hello = SC_STR_LIT(&a, "Hello");
    assert(frame_begin(&b, &a, 4, 4));
    assert(frame_text(&b, hello, UI_FONT_SANS, 13, black));
    assert(frame_text(&b, SC_STR_LIT(&a, "World"), UI_FONT_MONO, 13, black));
    assert(frame_end(&b, &f));
    sc_str back = node_at(f, 0).text;
    assert(sc_str_eq(&a, back, hello));
    assert(SC_STR_EQ_LIT(&a, back, "Hello"));
    assert(SC_STR_EQ_LIT(&a, node_at(f, 1).text, "World"));

    /* Bytes from another arena are refused, and so is the frame after a reset. */
    assert(frame_begin(&b, &a, 4, 4));
    assert(!frame_text(&b, SC_STR_LIT(&other, "elsewhere"), UI_FONT_SANS, 13, black));
    assert(ui_frame_valid(&a, f));
    assert(sc_arena_reset(&a));
    assert(!ui_frame_valid(&a, f));
}

static void fonts_are_checked(void) {
    frame_builder b;
    assert(frame_begin(&b, &a, 4, 0));
    assert(!frame_text(&b, SC_STR_LIT(&a, "x"), UI_FONT_COUNT, 13, black));
    assert(frame_begin(&b, &a, 4, 0));
    assert(!frame_text(&b, SC_STR_LIT(&a, "x"), UI_FONT_MONO, 0, black));

    /* A frame written by hand with a bad font is refused by the validator too. */
    const uint32_t parents[] = {UI_NO_PARENT, 0};
    ui_frame f = by_hand(parents, 2, 1);
    ui_node t;
    assert(ui_frame_valid(&a, f));
    assert(SC_TSLICE_GET(ui_node, &a, f.nodes, 1, &t));
    t.font = UI_FONT_COUNT;
    assert(SC_TSLICE_SET(ui_node, &a, f.nodes, 1, &t));
    assert(!ui_frame_valid(&a, f));
    t.font = UI_FONT_SANS;
    t.font_size = 0;
    assert(SC_TSLICE_SET(ui_node, &a, f.nodes, 1, &t));
    assert(!ui_frame_valid(&a, f));
}

static ui_draw_item rect(int32_t x, int32_t w) {
    ui_draw_item d = {0};
    d.kind = UI_DRAW_RECT;
    d.x = x;
    d.w = w;
    d.h = 10;
    return d;
}

static void custom_nodes_claim_their_draws(void) {
    frame_builder b;
    ui_frame f;
    assert(frame_begin(&b, &a, 8, 8));
    assert(frame_open(&b, frame_box()));
    assert(frame_draw(&b, rect(0, 5)));
    assert(frame_draw(&b, rect(7, 5)));
    assert(frame_custom(&b, frame_box()));
    assert(frame_draw(&b, rect(20, 5)));
    assert(frame_custom(&b, frame_box()));
    assert(frame_custom(&b, frame_box())); /* draws nothing */
    assert(frame_close(&b));
    assert(frame_end(&b, &f));
    assert(f.draw_count == 3);
    assert(node_at(f, 1).kind == UI_NODE_CUSTOM);
    assert(node_at(f, 1).draw_first == 0 && node_at(f, 1).draw_count == 2);
    assert(node_at(f, 2).draw_first == 2 && node_at(f, 2).draw_count == 1);
    assert(node_at(f, 3).draw_first == 3 && node_at(f, 3).draw_count == 0);
    assert(ui_frame_valid(&a, f));

    /* A custom node whose run runs past the list, or wraps, is refused. */
    ui_node c = node_at(f, 2);
    c.draw_count = 2;
    assert(SC_TSLICE_SET(ui_node, &a, f.nodes, 2, &c));
    assert(!ui_frame_valid(&a, f));
    c.draw_first = UINT32_MAX;
    c.draw_count = 4;
    assert(SC_TSLICE_SET(ui_node, &a, f.nodes, 2, &c));
    assert(!ui_frame_valid(&a, f));

    /* A draw no custom node claims fails the frame. */
    assert(frame_begin(&b, &a, 4, 4));
    assert(frame_custom(&b, frame_box()));
    assert(frame_draw(&b, rect(0, 5)));
    assert(!frame_end(&b, &f));

    /* Bad records are refused: a negative width, text from another arena, a
     * bad font, and a custom node added as a plain leaf. */
    assert(frame_begin(&b, &a, 4, 4));
    assert(!frame_draw(&b, rect(0, -1)));
    ui_draw_item t = {0};
    t.kind = UI_DRAW_TEXT;
    t.font_size = 12;
    t.text = SC_STR_LIT(&other, "elsewhere");
    assert(frame_begin(&b, &a, 4, 4));
    assert(!frame_draw(&b, t));
    t.text = SC_STR_LIT(&a, "here");
    assert(frame_begin(&b, &a, 4, 4));
    assert(frame_draw(&b, t));
    t.font = UI_FONT_COUNT;
    assert(!frame_draw(&b, t));
    ui_node leaf = frame_box();
    leaf.kind = UI_NODE_CUSTOM;
    assert(frame_begin(&b, &a, 4, 4));
    assert(!frame_leaf(&b, leaf));
    assert(frame_begin(&b, &a, 4, 1));
    assert(frame_draw(&b, rect(0, 5)));
    assert(!frame_draw(&b, rect(0, 5))); /* draw capacity */
}

int main(void) {
    sc_arena_init(&a, storage, ARENA_BYTES);
    sc_arena_init(&other, other_storage, ARENA_BYTES);
    nesting_records_parents();
    bad_parents_are_rejected();
    unbalanced_is_rejected();
    full_builder_drops_the_frame();
    text_lives_in_the_frame_arena();
    fonts_are_checked();
    custom_nodes_claim_their_draws();
    puts("explorer_frame_test: ok");
    return 0;
}
