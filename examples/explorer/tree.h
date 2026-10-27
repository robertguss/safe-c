#ifndef EXPLORER_TREE_H
#define EXPLORER_TREE_H

#include "catalog.h"
#include "frame.h"
#include "grid.h"
#include "theme.h"

typedef enum { TREE_TABLES, TREE_FUNCTIONS } tree_mode;

typedef enum {
    TREE_ROW_SCHEMA,
    TREE_ROW_RELATION,
    TREE_ROW_FUNCTION,
    TREE_ROW_LOADING,
    TREE_ROW_ERROR,
    TREE_ROW_RETRY
} tree_row_kind;

typedef struct {
    tree_row_kind kind;
    size_t schema;
    size_t child;
} tree_row;

typedef struct {
    tree_mode mode;
    grid_geom g;
    bool selected;
    tree_row sel;
} tree_state;

typedef enum {
    TREE_KEY_NONE,
    TREE_KEY_UP,
    TREE_KEY_DOWN,
    TREE_KEY_LEFT,
    TREE_KEY_RIGHT,
    TREE_KEY_OPEN,
    TREE_KEY_OPEN_NEW
} tree_key;

typedef enum { TREE_OPEN_NONE, TREE_OPEN_RELATION, TREE_OPEN_FUNCTION } tree_open_kind;

typedef struct {
    tree_open_kind kind;
    bool new_tab;
    uint32_t schema;
    uint32_t oid;
} tree_open;

typedef enum {
    TREE_ICON_FOLDER,
    TREE_ICON_TABLE,
    TREE_ICON_PARTITIONED,
    TREE_ICON_VIEW,
    TREE_ICON_MATVIEW,
    TREE_ICON_FOREIGN,
    TREE_ICON_FUNCTION,
    TREE_ICON_NONE
} tree_icon;

typedef struct {
    tree_icon icon;
    uint32_t token;
} tree_glyph;

void tree_init(tree_state *t);
void tree_set_mode(tree_state *t, tree_mode m);

uint64_t tree_count(const catalog *c, tree_mode m);
bool tree_row_at(const catalog *c, tree_mode m, uint64_t i, tree_row *out);
bool tree_index_of(const catalog *c, tree_mode m, tree_row r, uint64_t *out);

size_t tree_visible(const catalog *c, tree_state *t, uint32_t height, const sc_arena *a, sc_tslice out,
                    grid_rows *vis);

tree_key tree_key_for(uint32_t key, uint16_t mods, uint8_t platform);
bool tree_press(tree_state *t, catalog *c, tree_key k, tree_open *open);
bool tree_click(tree_state *t, catalog *c, int64_t y, bool cmd, tree_open *open);
bool tree_wheel(tree_state *t, int32_t dy);

tree_glyph tree_rel_glyph(catalog_rel_kind k);
tree_glyph tree_glyph_for(const catalog *c, tree_row r);

#endif
