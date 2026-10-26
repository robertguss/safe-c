#ifndef EXPLORER_DETAIL_H
#define EXPLORER_DETAIL_H

/* The row-detail pane's words and line breaks (plan U11, R9): a type name for
 * each column's OID, and a value wrapped to the pane's width. The app's first
 * multi-line text layout. Core code: the measuring is a callback the glue
 * supplies. */

#include "safec/utf8.h"
#include "safec/tslice.h"

/* The type's name ("integer", "text", ...) for the common built-in OIDs, or
 * the OID as a number. */
sc_str detail_type_name(sc_arena *a, uint32_t oid);

/* A text's width in points, for `s` live in `a`. */
typedef uint32_t (*detail_measure_fn)(void *ctx, const sc_arena *a, sc_str s);

/* Wraps `s` (live in `sa`) into lines no wider than `width` points: a
 * newline ends a line (and is not part of it), and otherwise a line ends
 * after its last space before the character that would pass the width, or
 * at that character when the line has no space; never inside a character. A line
 * always holds at least one character. Invalid bytes break as one-byte
 * characters. Writes at most sc_tslice_len(lines) lines (sc_str views into
 * `s`, in `la`) and sets *count; *more says lines were left over. */
bool detail_wrap(const sc_arena *sa, sc_str s, uint32_t width, detail_measure_fn m, void *ctx,
                 sc_arena *la, sc_tslice lines, size_t *count, bool *more);

#endif
