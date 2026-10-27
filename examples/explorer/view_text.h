#ifndef EXPLORER_VIEW_TEXT_H
#define EXPLORER_VIEW_TEXT_H

#include "frame.h"
#include "query_text.h"

ui_draw_item view_item(uint8_t kind, int32_t x, int32_t y, int32_t w, int32_t h, ui_color c);

/* A text record; an empty string draws nothing and adds nothing. */
/* Text at (x, y), clipped to w by h from there when both are above 0. */
bool view_draw_text_in(frame_builder *b, sc_str t, uint8_t font, uint16_t size, int32_t x,
                       int32_t y, int32_t w, int32_t h, ui_color c);

uint32_t view_text_width(const sc_arena *a, sc_str t, uint8_t font, uint16_t size);

/* `s` (live in the frame arena) cut with an ellipsis to fit `max_w` points,
 * so no text widens its box: frame text is one line and does not wrap. */
sc_str view_fit(frame_builder *b, sc_str s, uint8_t font, uint16_t size, uint32_t max_w);

#endif
