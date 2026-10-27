#include "theme.h"

static uint32_t light_hex(uint32_t token) {
    switch (token) {
    case THEME_CONTENT: return 0xFFFFFFu;
    case THEME_ALT_ROW: return 0xF4F5F5u;
    case THEME_GRID_HEADER: return 0xF7F7F8u;
    case THEME_HEADER_LINE: return 0xDFDFDFu;
    case THEME_COLUMN_LINE: return 0xF5F5F5u;
    case THEME_ROW_SELECTION: return 0x2962D9u;
    case THEME_ACCENT: return 0x3478F6u;
    case THEME_SIDEBAR_BORDER: return 0xD9D9D9u;
    case THEME_BOTTOM_BAR: return 0xECECECu;
    case THEME_SECONDARY_TEXT: return 0x7C7D7Cu;
    case THEME_TEXT: return 0x262626u;
    case THEME_TOOLBAR: return 0xE8E8E8u;
    case THEME_WINDOW: return 0xF0F0F0u;
    case THEME_SELECTED_TEXT: return 0xFFFFFFu;
    case THEME_SUCCESS: return 0x2E9E48u;
    case THEME_ERROR: return 0xD9342Bu;
    default: return 0;
    }
}

static uint32_t dark_hex(uint32_t token) {
    switch (token) {
    case THEME_CONTENT: return 0x1E1E1Eu;
    case THEME_ALT_ROW: return 0x2A2A2Bu;
    case THEME_GRID_HEADER: return 0x333333u;
    case THEME_HEADER_LINE: return 0x595959u;
    case THEME_COLUMN_LINE: return 0x353435u;
    case THEME_ROW_SELECTION: return 0x2457C9u;
    case THEME_ACCENT: return 0x397CF7u;
    case THEME_SIDEBAR_BORDER: return 0x666666u;
    case THEME_BOTTOM_BAR: return 0x2F2F2Fu;
    case THEME_SECONDARY_TEXT: return 0xA1A1A1u;
    case THEME_TEXT: return 0xE6E6E6u;
    case THEME_TOOLBAR: return 0x323232u;
    case THEME_WINDOW: return 0x262626u;
    case THEME_SELECTED_TEXT: return 0xFFFFFFu;
    case THEME_SUCCESS: return 0x3FB95Au;
    case THEME_ERROR: return 0xF0544Au;
    default: return 0;
    }
}

uint32_t theme_hex(bool dark, uint32_t token) {
    return dark ? dark_hex(token) : light_hex(token);
}

ui_color theme_color(bool dark, uint32_t token) {
    uint32_t h = theme_hex(dark, token);
    ui_color c = {(uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h, 0xFF};
    return c;
}
