#ifndef SAFEC_ADAPTER_UI_H
#define SAFEC_ADAPTER_UI_H

/* The explorer's UI adapter (adapters/ui.c, subset rule R0): SDL3 draws,
 * SDL3_ttf shapes text and Clay lays out, and none of them reaches app code.
 * The app describes each frame as ui_node records (sc_ui_frame.h) and gets
 * input back as ui_event records.
 *
 * Every function here runs on the main thread, except ui_wake. */

#include "sc_ui_frame.h"

/* Defined by the app. The adapter owns the C main (plan KTD17): it copies the
 * command-line arguments into `args_arena` as a tslice of sc_str, program name
 * first, and returns app_main's result as the exit status. */
int app_main(sc_arena *args_arena, sc_tslice args);

/* Sets up *out over `cap` bytes the adapter allocates. The storage lives until
 * the process exits; app code has no other way to get backing memory. */
bool ui_arena_new(sc_arena *out, size_t cap);

/* Opens the window, `width` by `height` points, and loads the fonts; fails if
 * any font file is missing. With SDL_VIDEO_DRIVER=offscreen it renders into
 * memory for screenshots. */
bool ui_open(const sc_arena *a, sc_str title, uint32_t width, uint32_t height);
void ui_close(void);

/* Waits up to `timeout_ms` for an input event; 0 only polls. Returns false on
 * a timeout. Text-input bytes are copied into `events`, which the caller resets
 * when it no longer needs them. */
bool ui_wait(sc_arena *events, uint32_t timeout_ms, ui_event *out);

/* Wakes a ui_wait on the main thread with UI_EVENT_WAKE. Safe from any thread,
 * but only between ui_open returning true and ui_close: join every thread that
 * calls it before ui_close. Returns false when the wake was dropped. */
bool ui_wake(void);

/* Lays out and draws one frame. Returns false and shows nothing when
 * ui_frame_valid rejects the frame, when layout fails, or when any text cannot
 * be drawn (a missing font, the text cache full). The caller may reset
 * `frame_arena` once this returns. */
bool ui_draw(const sc_arena *frame_arena, ui_frame f);

/* Defined by the app's core (examples/explorer/frame.c, unit-tested): whether
 * `f` has the shape sc_ui_frame.h describes. ui_draw calls it before it hands
 * the tree to Clay, so the adapter keeps no second copy. */
bool ui_frame_valid(const sc_arena *a, ui_frame f);

/* The next ui_draw also saves the rendered pixels to `path` as a PNG. */
bool ui_screenshot_next(const sc_arena *a, sc_str path);

/* The size `text` takes in points in the given font and size. */
bool ui_text_size(const sc_arena *a, sc_str text, uint8_t font, uint16_t size, uint32_t *w,
                  uint32_t *h);

/* The window size in points. */
void ui_window_size(uint32_t *w, uint32_t *h);

/* Whether the system theme is dark. */
bool ui_dark_theme(void);

#endif
