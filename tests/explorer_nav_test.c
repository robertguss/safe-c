/* Tests the explorer's view stack (examples/explorer/nav.c, plan U20, KTD23):
 * pushes past NAV_MAX, back then a push dropping the forward entries, what
 * arriving at each kind of entry does, the navigation keys, and when each
 * move is allowed: pages only past page 1 or after a full page ended, and
 * no move away from a query view whose run is live or waiting. Core code,
 * built in. */
#include "../examples/explorer/frame.c"
#include "../examples/explorer/proto.c"
#include "../examples/explorer/result.c"
#include "../examples/explorer/catalog.c"
#include "../examples/explorer/catalog_sql.c"
#include "../examples/explorer/page.c"
#include "../examples/explorer/nav.c"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>

alignas(8) static uint8_t mem[8192];
static sc_arena A;

static nav_entry table(uint32_t oid) {
    page_total t = {false, 0, PAGE_COUNT_PENDING, 0, PAGE_SIZE};
    return nav_table(2200, oid, t);
}

static nav_entry current(const nav_stack *n) {
    nav_entry e;
    assert(nav_current(n, &e));
    return e;
}

static void keeps_sixteen(void) {
    nav_stack n;
    assert(nav_init(&n, &A));
    assert(n.count == 1 && current(&n).kind == NAV_QUERY);
    assert(!nav_can_back(&n) && !nav_can_forward(&n));
    for (uint32_t i = 1; i <= 17; i++) {
        assert(nav_push(&n, table(i)));
    }
    assert(n.count == NAV_MAX && n.at == NAV_MAX - 1u && current(&n).oid == 17);
    size_t backs = 0;
    while (nav_back(&n)) {
        backs++;
    }
    assert(backs == NAV_MAX - 1u);
    nav_entry first = current(&n);
    assert(first.kind == NAV_TABLE && first.oid == 2);
    assert(nav_forward(&n) && current(&n).oid == 3);
    nav_reset(&n);
    assert(n.count == 1 && current(&n).kind == NAV_QUERY && !nav_can_forward(&n));
}

static void push_drops_forward(void) {
    nav_stack n;
    assert(nav_init(&n, &A));
    assert(nav_push(&n, table(10)) && nav_push(&n, table(11)) && nav_push(&n, table(12)));
    assert(nav_back(&n) && nav_back(&n) && current(&n).oid == 10);
    assert(nav_can_forward(&n));
    assert(nav_push(&n, table(20)));
    assert(!nav_can_forward(&n) && !nav_forward(&n) && n.count == 3 && current(&n).oid == 20);
    assert(nav_back(&n) && current(&n).oid == 10);
    assert(nav_back(&n) && current(&n).kind == NAV_QUERY && !nav_back(&n));
}

static void arrivals(void) {
    nav_stack n;
    assert(nav_init(&n, &A));
    assert(nav_arrive(current(&n)) == NAV_SHOW_QUERY);
    nav_entry t = table(5);
    t.total.count_state = PAGE_COUNT_KNOWN;
    t.total.count = 2500;
    assert(nav_push(&n, t));
    nav_entry e = current(&n);
    assert(nav_arrive(e) == NAV_SHOW_CONTENT && e.page == 1 && e.view == NAV_CONTENT);
    e.page = 3;
    assert(nav_set_current(&n, e));
    e.view = NAV_STRUCTURE;
    assert(nav_set_current(&n, e) && nav_arrive(current(&n)) == NAV_SHOW_STRUCTURE);
    assert(current(&n).page == 3 && current(&n).total.count == 2500);
    assert(nav_back(&n) && nav_arrive(current(&n)) == NAV_SHOW_QUERY);
    assert(nav_forward(&n) && current(&n).view == NAV_STRUCTURE && current(&n).page == 3);
}

static nav_run idle_run(uint64_t rows) {
    nav_run r = {false, true, rows, false};
    return r;
}

static void moves(void) {
    nav_stack n;
    assert(nav_init(&n, &A));
    nav_run busy = {true, false, 0, false};
    nav_run none = {false, false, 0, false};
    assert(!nav_can_move(&n, NAV_MOVE_BACK, none) && !nav_can_move(&n, NAV_MOVE_SWITCH, none));
    assert(!nav_can_move(&n, NAV_MOVE_NEXT_PAGE, idle_run(PAGE_SIZE)));
    assert(nav_push(&n, table(5)));
    assert(nav_can_move(&n, NAV_MOVE_BACK, busy) && !nav_can_move(&n, NAV_MOVE_FORWARD, none));
    assert(!nav_can_move(&n, NAV_MOVE_PREV_PAGE, idle_run(PAGE_SIZE)));
    assert(!nav_apply(&n, NAV_MOVE_NEXT_PAGE, idle_run(PAGE_SIZE - 1u)) && current(&n).page == 1);
    nav_run capped = idle_run(PAGE_SIZE);
    capped.capped = true;
    nav_run running = idle_run(PAGE_SIZE);
    running.ended = false;
    assert(!nav_can_move(&n, NAV_MOVE_NEXT_PAGE, capped) && !nav_can_move(&n, NAV_MOVE_NEXT_PAGE, running));
    nav_run ended_busy = idle_run(PAGE_SIZE);
    ended_busy.busy = true;
    assert(!nav_can_move(&n, NAV_MOVE_NEXT_PAGE, ended_busy));
    assert(nav_apply(&n, NAV_MOVE_NEXT_PAGE, idle_run(PAGE_SIZE)) && current(&n).page == 2);
    assert(!nav_can_move(&n, NAV_MOVE_PREV_PAGE, busy));
    assert(nav_apply(&n, NAV_MOVE_PREV_PAGE, idle_run(3)) && current(&n).page == 1);
    assert(nav_apply(&n, NAV_MOVE_SWITCH, busy) && current(&n).view == NAV_STRUCTURE);
    assert(!nav_can_move(&n, NAV_MOVE_NEXT_PAGE, idle_run(PAGE_SIZE)) &&
           !nav_can_move(&n, NAV_MOVE_PREV_PAGE, idle_run(PAGE_SIZE)));
    assert(nav_apply(&n, NAV_MOVE_SWITCH, none) && current(&n).view == NAV_CONTENT && n.count == 2);
    assert(nav_apply(&n, NAV_MOVE_BACK, busy) && current(&n).kind == NAV_QUERY);
    assert(!nav_can_move(&n, NAV_MOVE_FORWARD, busy) && !nav_apply(&n, NAV_MOVE_FORWARD, busy));
    assert(current(&n).kind == NAV_QUERY);
    assert(nav_apply(&n, NAV_MOVE_FORWARD, none) && current(&n).kind == NAV_TABLE);
    assert(nav_push(&n, table(6)) && nav_back(&n) && nav_back(&n));
    assert(nav_can_move(&n, NAV_MOVE_FORWARD, none) && !nav_can_move(&n, NAV_MOVE_FORWARD, busy));
}

static void keys(void) {
    uint16_t cmd_alt = UI_MOD_CMD | UI_MOD_ALT;
    assert(nav_key_for(UI_KEY_LEFT, cmd_alt, UI_PLATFORM_MAC) == NAV_KEY_BACK);
    assert(nav_key_for(UI_KEY_RIGHT, cmd_alt, UI_PLATFORM_MAC) == NAV_KEY_FORWARD);
    assert(nav_key_for('t', cmd_alt, UI_PLATFORM_MAC) == NAV_KEY_SWITCH);
    assert(nav_key_for('T', cmd_alt, UI_PLATFORM_MAC) == NAV_KEY_SWITCH);
    assert(nav_key_for(UI_KEY_LEFT, UI_MOD_CTRL | UI_MOD_CMD | UI_MOD_ALT, UI_PLATFORM_OTHER) == NAV_KEY_BACK);
    assert(nav_key_for(UI_KEY_LEFT, UI_MOD_CMD, UI_PLATFORM_MAC) == NAV_KEY_NONE);
    assert(nav_key_for(UI_KEY_LEFT, UI_MOD_ALT, UI_PLATFORM_MAC) == NAV_KEY_NONE);
    assert(nav_key_for('t', UI_MOD_CMD, UI_PLATFORM_MAC) == NAV_KEY_NONE);
    assert(nav_key_for('t', cmd_alt | UI_MOD_SHIFT, UI_PLATFORM_MAC) == NAV_KEY_NONE);
    assert(nav_key_for(UI_KEY_LEFT, UI_MOD_CTRL | UI_MOD_ALT, UI_PLATFORM_MAC) == NAV_KEY_NONE);
}

int main(void) {
    sc_arena_init(&A, mem, sizeof mem);
    keeps_sixteen();
    push_drops_forward();
    arrivals();
    moves();
    keys();
    puts("explorer_nav_test: ok");
    return 0;
}
