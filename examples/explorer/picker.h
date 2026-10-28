#ifndef EXPLORER_PICKER_H
#define EXPLORER_PICKER_H

#include "conncfg.h"
#include "frame.h"
#include "../../adapters/sc_db.h"
#include "../../adapters/sc_mem.h"

#define PICK_TAG_ROW 0x20000u
#define PICK_TAG_CONNECT 0x20100u
#define PICK_TAG_CANCEL 0x20101u
#define PICK_TAG_END 0x20102u
#define PICK_TAG_KEEP 0x20103u
#define PICK_TAG_ENGINE 0x20200u
#define PASSWORD_BYTES 4096u

typedef struct {
    conncfg cfg;
    sc_arena files, records, paths, info, secret;
    sc_str dir, path, connection, name, error;
    sc_buf password;
    sc_db_engine engine;
    uint64_t dialog;
    bool dialog_pending;
    size_t selected;
    bool visible, prompt, connecting, failed, confirm;
    size_t unsaved;
    bool file_error;
} picker;

bool picker_init(picker *p);
bool picker_choose(picker *p);
void picker_clear(picker *p);
bool picker_engine(picker *p, sc_db_engine engine);
bool picker_local(picker *p, const sc_arena *a, sc_str path);
uint64_t picker_dialog(picker *p);
bool picker_file(picker *p, const sc_arena *a, ui_event event);
bool picker_text(picker *p, const sc_arena *a, sc_str text);
bool picker_backspace(picker *p);
bool picker_build(frame_builder *b, const picker *p, bool dark, uint32_t width, uint32_t height,
                  const sc_arena *error_arena, sc_str error);

#endif
