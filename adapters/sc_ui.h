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

/* The smallest window the user can resize to, in points. */
#define UI_MIN_WIDTH 640
#define UI_MIN_HEIGHT 400

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

/* The clipboard's text. UI_CLIP_OK copies it into `a` as *out; text longer
 * than `max` bytes is UI_CLIP_TOO_BIG and copies nothing, so a paste is whole
 * or refused. An empty or unreadable clipboard is UI_CLIP_EMPTY. */
enum { UI_CLIP_OK = 0, UI_CLIP_EMPTY, UI_CLIP_TOO_BIG };
uint8_t ui_clipboard(sc_arena *a, size_t max, sc_str *out);

/* Lays out and draws one frame, and writes each tagged node's rectangle into
 * `out` (sc_ui_frame.h). Returns false, shows nothing and leaves `out` alone
 * when ui_frame_valid rejects the frame, when it has more tagged nodes than
 * `out` has room for, when layout fails, or when any text cannot be drawn (a
 * missing font, the text cache full). The caller may reset `frame_arena` once
 * this returns. */
bool ui_draw(const sc_arena *frame_arena, ui_frame f, ui_layout *out);

/* With `on`, every later frame also outlines each tagged rectangle it
 * reports, so a scene shows where clicks will land. */
void ui_outline_tags(bool on);

/* The platform, for the key maps (UI_PLATFORM_*). */
uint8_t ui_platform(void);

/* Defined by the app's core (examples/explorer/frame.c, unit-tested): whether
 * `f` has the shape sc_ui_frame.h describes. ui_draw calls it before it hands
 * the tree to Clay, so the adapter keeps no second copy. */
bool ui_frame_valid(const sc_arena *a, ui_frame f);

/* The next ui_draw also saves the rendered pixels to `path` as a PNG. */
bool ui_screenshot_next(const sc_arena *a, sc_str path);

/* Writes one line (live in `a`) to the log (stderr), as a measurement scene
 * reports its numbers. A dead or empty string writes nothing. */
void ui_log(const sc_arena *a, sc_str line);

/* The size `text` takes in points in the given font and size. */
bool ui_text_size(const sc_arena *a, sc_str text, uint8_t font, uint16_t size, uint32_t *w,
                  uint32_t *h);

/* The window size in points. */
void ui_window_size(uint32_t *w, uint32_t *h);

/* Whether the system theme is dark. */
bool ui_dark_theme(void);

#endif
