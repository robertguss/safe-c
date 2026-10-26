/* Tests the explorer's palette (examples/explorer/theme.c) against the values
 * measured from Postico in the plan (U3, step 1). The expected values are
 * copied from the plan text, not from theme.c. */
#include "../examples/explorer/theme.c"
#include <assert.h>
#include <stdio.h>

typedef struct {
    uint32_t token, light, dark;
} expect;

static const expect measured[] = {
    {THEME_CONTENT, 0xFFFFFF, 0x1E1E1E},
    {THEME_ALT_ROW, 0xF4F5F5, 0x2A2A2B},
    {THEME_GRID_HEADER, 0xF7F7F8, 0x333333},
    {THEME_HEADER_LINE, 0xDFDFDF, 0x595959},
    {THEME_COLUMN_LINE, 0xF5F5F5, 0x353435},
    {THEME_ROW_SELECTION, 0x2962D9, 0x2457C9},
    {THEME_ACCENT, 0x3478F6, 0x397CF7},
    {THEME_SIDEBAR_BORDER, 0xD9D9D9, 0x666666},
    {THEME_SIDEBAR_SELECTION, 0xBAD6FC, 0x466287},
    {THEME_SIDEBAR_SELECTION_UNFOCUSED, 0xDCDCDC, 0x414141},
    {THEME_BOTTOM_BAR, 0xECECEC, 0x2F2F2F},
    {THEME_SECONDARY_TEXT, 0x7C7D7C, 0xA1A1A1},
};

int main(void) {
    size_t n = sizeof measured / sizeof measured[0];
    for (size_t i = 0; i < n; i++) {
        assert(theme_hex(false, measured[i].token) == measured[i].light);
        assert(theme_hex(true, measured[i].token) == measured[i].dark);
    }
    /* Every measured token differs between the modes, so a table copied from
     * one mode into the other fails above; the chosen ones must too, except
     * text drawn on a selection, which stays white in both. */
    for (uint32_t t = 0; t < THEME_TOKEN_COUNT; t++) {
        assert(theme_hex(false, t) != 0 && theme_hex(true, t) != 0);
        if (t != THEME_SELECTED_TEXT) {
            assert(theme_hex(false, t) != theme_hex(true, t));
        }
    }
    assert(theme_hex(false, THEME_SELECTED_TEXT) == theme_hex(true, THEME_SELECTED_TEXT));
    assert(theme_hex(false, THEME_TOKEN_COUNT) == 0);

    ui_color c = theme_color(false, THEME_ROW_SELECTION);
    assert(c.r == 0x29 && c.g == 0x62 && c.b == 0xD9 && c.a == 0xFF);
    c = theme_color(true, THEME_SECONDARY_TEXT);
    assert(c.r == 0xA1 && c.g == 0xA1 && c.b == 0xA1 && c.a == 0xFF);
    /* Metrics from the plan (U3, step 2). */
    assert(THEME_GRID_HEADER_HEIGHT == 19 && THEME_GRID_ROW_HEIGHT == 23);
    assert(THEME_SIDEBAR_ROW_HEIGHT == 23 && THEME_BOTTOM_BAR_HEIGHT == 30);
    assert(THEME_BUTTON_HEIGHT == 20 && THEME_BUTTON_RADIUS == 4 && THEME_INSET == 4);
    assert(THEME_TEXT_SIZE == 13 && THEME_MAX_AUTO_COLUMN_WIDTH == 300);
    puts("explorer_theme_test: ok");
    return 0;
}
