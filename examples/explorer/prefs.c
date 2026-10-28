#include "prefs.h"

prefs prefs_parse(const sc_arena *a, sc_str file) {
    prefs p = {1, 1000, 0};
    if (file.len > CONNCFG_BYTES || !sc_utf8_valid(a, file)) { p.line = 1; return p; }
    size_t at = 0, n = 0;
    sc_str line;
    while (cfg_line(a, file, &at, &line)) {
        n++;
        if (line.len == 0) continue;
        conncfg_pair kv;
        if (!cfg_pair(a, line, &kv)) { if (p.line == 0) p.line = n; continue; }
        bool lines = SC_STR_EQ_LIT(a, kv.key, "lines_per_row");
        bool rows = SC_STR_EQ_LIT(a, kv.key, "rows_per_page");
        uint32_t value = 0;
        bool valid = kv.text.len > 0 && kv.text.len <= 5u;
        for (size_t i = 0; valid && i < kv.text.len; i++) {
            uint8_t ch = 0;
            valid = sc_slice_get(a, kv.text, i, &ch) && ch >= '0' && ch <= '9';
            if (valid) value = value * 10u + ch - '0';
        }
        valid = valid && ((lines && (value == 1u || value == 2u)) || (rows && value >= 100u && value <= 10000u));
        if (lines) p.lines_per_row = valid ? value : 1u;
        if (rows) p.rows_per_page = valid ? value : 1000u;
        if (!valid && p.line == 0) p.line = n;
    }
    return p;
}
