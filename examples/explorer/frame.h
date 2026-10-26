#ifndef EXPLORER_FRAME_H
#define EXPLORER_FRAME_H

/* Builds a frame (adapters/sc_ui_frame.h) in an arena: open a box, add its
 * children, close it. Core code: it includes no SDL and no adapter functions,
 * so tests/explorer_frame_test.c links it against the stdlib alone.
 *
 * The first failure sticks: every later call returns false, and frame_end
 * hands back no frame, so a frame that ran out of room is dropped whole rather
 * than drawn in part. */

#include "../../adapters/sc_ui_frame.h"

typedef struct {
    sc_arena *arena;
    sc_tslice nodes;
    size_t count;
    sc_tslice draws;
    size_t draw_count;
    size_t draw_mark; /* first draw record not yet claimed by a custom node */
    uint32_t open;    /* the innermost open box, or UI_NO_PARENT */
    size_t depth;
    bool failed;
} frame_builder;

/* Room for `cap` nodes and `draw_cap` draw records in `a`. */
bool frame_begin(frame_builder *b, sc_arena *a, size_t cap, size_t draw_cap);

/* Opens a box as a child of the innermost open box. `n.parent` and `n.kind`
 * are set by the builder. */
bool frame_open(frame_builder *b, ui_node n);

/* Adds a node with no children: a text node, or a box that stays empty. A
 * text node's bytes must be live in the builder's arena. */
bool frame_leaf(frame_builder *b, ui_node n);

/* Adds a text node: `text` in `font` at `size` points. */
bool frame_text(frame_builder *b, sc_str text, uint8_t font, uint16_t size, ui_color color);

/* Adds a draw record for the next custom node. */
bool frame_draw(frame_builder *b, ui_draw_item d);

/* Adds a custom node, sized by `n` like a box, that draws every record added
 * with frame_draw since the previous custom node. */
bool frame_custom(frame_builder *b, ui_node n);

/* Closes the innermost open box. Closing with none open fails. */
bool frame_close(frame_builder *b);

/* Hands back the frame. Fails if a box is still open, a draw record was never
 * claimed by a custom node, or any call failed. */
bool frame_end(frame_builder *b, ui_frame *out);

/* A box with every field zero: FIT on both axes, a row, nothing drawn. */
ui_node frame_box(void);

/* Whether `f` has the shape sc_ui_frame.h describes: each parent is an earlier
 * box whose subtree is still open at that node; each text is live in `a`, in a
 * known font at a non-zero size; each custom node's draw run lies inside the
 * frame's draw list. */
bool ui_frame_valid(const sc_arena *a, ui_frame f);

#endif
