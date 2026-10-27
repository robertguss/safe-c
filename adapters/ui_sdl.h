#ifndef SAFEC_ADAPTER_UI_SDL_H
#define SAFEC_ADAPTER_UI_SDL_H

/* What the two halves of the UI adapter share (adapters/ui.c draws,
 * adapters/ui_input.c reads input). Adapter-private: app code never
 * includes it. */

#include "sc_ui.h"
#include <SDL3/SDL.h>

/* ui.c: the window, or NULL outside ui_open .. ui_close. */
SDL_Window *ui_sdl_window(void);
/* ui.c: rereads the pixel density, flushing fonts when it changed. */
void ui_update_density(void);
/* ui.c: whether `s` is empty or live in `a`. */
bool ui_live_str(const sc_arena *a, sc_str s);

/* ui_input.c: called by ui_open once the window is up, and by ui_close
 * (also on a failed open) before SDL quits. */
void ui_input_open(void);
void ui_input_close(void);

#endif
