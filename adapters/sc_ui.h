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

sc_str ui_pref_path(sc_arena *a);

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
enum { UI_CLIP_OK = 0, UI_CLIP_EMPTY, UI_CLIP_TOO_BIG, UI_CLIP_NUL, UI_CLIP_FAILED };
uint8_t ui_clipboard(sc_arena *a, size_t max, sc_str *out);

/* Replaces the clipboard's text with `text` (live in `a`). Text longer than
 * `max` bytes is UI_CLIP_TOO_BIG, text holding a NUL byte is UI_CLIP_NUL, and
 * a dead string or a write SDL refuses is UI_CLIP_FAILED; each of those
 * leaves the clipboard as it was. Empty text clears it. */
uint8_t ui_clipboard_set(const sc_arena *a, sc_str text, size_t max);

/* How text input runs. ui_open starts it as UI_TEXT_INPUT_PLAIN, with the OS
 * drawing its candidate window and the app drawing the composition
 * (UI_EVENT_COMPOSE). UI_TEXT_INPUT_PASSWORD asks for a hidden password, and
 * the adapter drops every composition while it is on, so no UI_EVENT_COMPOSE
 * reaches the app; whether the OS hides its candidate window depends on the
 * platform (SDL 3.4 on macOS ignores the type). A refused switch to
 * UI_TEXT_INPUT_PASSWORD still drops compositions. */
enum { UI_TEXT_INPUT_PLAIN = 0, UI_TEXT_INPUT_PASSWORD = 1 };
bool ui_text_input(uint8_t mode);

/* Tells the OS where the text being edited sits, in points from the window's
 * top-left corner, with the caret `cursor` points right of `x`, so the
 * candidate window opens beside it. */
bool ui_text_area(int32_t x, int32_t y, int32_t w, int32_t h, int32_t cursor);

/* The mouse cursor's shape. False when the platform has no such cursor (the
 * offscreen driver has none). */
enum { UI_CURSOR_DEFAULT = 0, UI_CURSOR_TEXT, UI_CURSOR_POINTER, UI_CURSOR_RESIZE_EW, UI_CURSOR_RESIZE_NS };
bool ui_cursor(uint8_t shape);

/* Headless scenes: queue the events an IME would send, which ui_wait then
 * translates like real ones. ui_script_compose sends a composition with the
 * selection `start`, `length` in characters, as SDL gives them (-1 for not
 * set); ui_script_text a commit. Both return false unless the command line
 * holds `--scene`. Text holding a NUL byte, a dead string or more than 64
 * scripted events in all is refused. */
bool ui_script_compose(const sc_arena *a, sc_str text, int32_t start, int32_t length);
bool ui_script_text(const sc_arena *a, sc_str text);

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
