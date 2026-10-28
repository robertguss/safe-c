#ifndef EXPLORER_VIEW_HISTORY_H
#define EXPLORER_VIEW_HISTORY_H

#include "frame.h"
#include "history.h"

#define HISTORY_TAG_BUTTON 0x420u
#define HISTORY_TAG_ROW 0x21000u

bool history_build(frame_builder *b, const history *h, size_t selected, bool failed, bool dark, uint32_t height);

#endif
