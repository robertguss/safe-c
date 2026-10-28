#ifndef EXPLORER_VIEW_SIDEBAR_H
#define EXPLORER_VIEW_SIDEBAR_H

#include "tree.h"
#include "view_shell.h"

enum {
    SIDEBAR_CAPTION = 0,
    SIDEBAR_CONNECTING,
    SIDEBAR_LOADING,
    SIDEBAR_READY,
    SIDEBAR_ERROR,
    SIDEBAR_DISCONNECTED
};

typedef struct {
    bool dark;
    bool focused;
    uint8_t status;
    const catalog *cat;
    tree_state *tree;
    sc_str message;
    uint8_t retry;
    sc_str retry_label;
    bool can_reload;
    uint32_t height;
    bool filter_focused;
} sidebar_view;

uint32_t sidebar_rows_height(const sidebar_view *v);

bool sidebar_build(frame_builder *b, const void *ctx);

#endif
