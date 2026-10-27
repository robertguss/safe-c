#ifndef EXPLORER_SCENE_TEXT_H
#define EXPLORER_SCENE_TEXT_H

#include "sc_ui.h"

enum { TEXT_CHECK_NONE = 0, TEXT_CHECK_COMPOSE, TEXT_CHECK_CLIPBOARD };

/* text-compose: composing `に` (the caret after it) and committing `日本`
 * arrive as one COMPOSE event of 3 bytes with the caret at byte 3, then one
 * TEXT event of 6 bytes, and nothing else of either kind. `events` is reset. */
bool scene_text_compose(sc_arena *events);

/* text-clipboard: `select 1` written to the clipboard reads back as the same 8
 * bytes; a write with a NUL byte and one over the cap are refused, and the
 * clipboard keeps `select 1`. `clip` is reset. */
bool scene_text_clipboard(sc_arena *clip, size_t cap);

#endif
