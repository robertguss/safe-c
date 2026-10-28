#ifndef EXPLORER_SCENE_TEXT_H
#define EXPLORER_SCENE_TEXT_H

#include "sc_ui.h"

enum { TEXT_CHECK_NONE = 0, TEXT_CHECK_COMPOSE, TEXT_CHECK_CLIPBOARD };

/* text-compose: ui_text_area succeeds. Composing `に` (the caret after it),
 * `にほんご` with characters 1..3 selected and `にほんご` with no selection, then
 * committing `日本`, arrive as three COMPOSE events (3 bytes, caret at byte 3;
 * 12 bytes, bytes 3..9; 12 bytes, caret at byte 12), then one TEXT event of 6
 * bytes, and nothing else of either kind. In password mode a composition
 * yields no COMPOSE event while a commit still arrives; back in plain mode a
 * composition arrives again. `events` is reset. */
bool scene_text_compose(sc_arena *events);

/* text-clipboard: `select 1` written to the clipboard reads back as the same 8
 * bytes; a write with a NUL byte and one over the cap are refused, and the
 * clipboard keeps `select 1`; an empty write then leaves it UI_CLIP_EMPTY.
 * `clip` is reset. */
bool scene_text_clipboard(sc_arena *clip, size_t cap);

#endif
