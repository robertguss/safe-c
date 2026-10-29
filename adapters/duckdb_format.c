#define _POSIX_C_SOURCE 200809L
#include "sc_duckdb_format.h"
#define DUCKDB_V2_API_ALLOW_UNSTABLE 1
#include <duckdb_v2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *type_name(duckdb_type id) {
    static const char *names[] = {
        "INVALID", "BOOLEAN", "TINYINT", "SMALLINT", "INTEGER", "BIGINT", "UTINYINT",
        "USMALLINT", "UINTEGER", "UBIGINT", "FLOAT", "DOUBLE", "TIMESTAMP", "DATE", "TIME",
        "INTERVAL", "HUGEINT", "VARCHAR", "BLOB", "DECIMAL", "TIMESTAMP_S", "TIMESTAMP_MS",
        "TIMESTAMP_NS", "ENUM", "LIST", "STRUCT", "MAP", "UUID", "UNION", "BIT", "TIME WITH TIME ZONE",
        "TIMESTAMP WITH TIME ZONE", "UHUGEINT", "ARRAY", "ANY", "BIGNUM", "NULL", "STRING_LITERAL",
        "INTEGER_LITERAL", "TIME_NS", "GEOMETRY", "VARIANT", "TIMESTAMP_NS WITH TIME ZONE"
    };
    return (size_t)id < sizeof names / sizeof *names ? names[id] : "UNKNOWN";
}

static void label(sc_local_text *out, duckdb_logical_type type, size_t depth) {
    if (depth > 64) { out->failed = true; return; }
    char *alias = duckdb_logical_type_get_alias(type);
    if (alias != NULL && alias[0] != 0) {
        sc_local_text_cstr(out, alias);
        duckdb_free(alias);
        return;
    }
    duckdb_free(alias);
    duckdb_type id = duckdb_get_type_id(type);
    if (id == DUCKDB_TYPE_LIST || id == DUCKDB_TYPE_ARRAY) {
        duckdb_logical_type child = id == DUCKDB_TYPE_LIST ? duckdb_list_type_child_type(type) :
                                                                          duckdb_array_type_child_type(type);
        label(out, child, depth + 1);
        duckdb_destroy_logical_type(&child);
        char suffix[32];
        if (id == DUCKDB_TYPE_LIST) snprintf(suffix, sizeof suffix, "[]");
        else snprintf(suffix, sizeof suffix, "[%llu]", (unsigned long long)duckdb_array_type_array_size(type));
        sc_local_text_cstr(out, suffix);
    } else if (id == DUCKDB_TYPE_DECIMAL) {
        char decimal[32];
        snprintf(decimal, sizeof decimal, "DECIMAL(%u,%u)", duckdb_decimal_width(type), duckdb_decimal_scale(type));
        sc_local_text_cstr(out, decimal);
    } else if (id == DUCKDB_TYPE_STRUCT || id == DUCKDB_TYPE_UNION || id == DUCKDB_TYPE_MAP) {
        sc_local_text_cstr(out, type_name(id));
        sc_local_text_cstr(out, "(");
        size_t count = id == DUCKDB_TYPE_MAP ? 2 : id == DUCKDB_TYPE_STRUCT ?
                      duckdb_struct_type_child_count(type) : duckdb_union_type_member_count(type);
        for (size_t i = 0; i < count && !out->failed; i++) {
            if (i > 0) sc_local_text_cstr(out, ", ");
            if (id != DUCKDB_TYPE_MAP) {
                char *name = id == DUCKDB_TYPE_STRUCT ? duckdb_struct_type_child_name(type, i) :
                                                        duckdb_union_type_member_name(type, i);
                if (name == NULL) out->failed = true;
                else {
                    sc_local_text_cstr(out, name);
                    sc_local_text_cstr(out, " ");
                }
                duckdb_free(name);
            }
            duckdb_logical_type child = id == DUCKDB_TYPE_STRUCT ? duckdb_struct_type_child_type(type, i) :
                id == DUCKDB_TYPE_UNION ? duckdb_union_type_member_type(type, i) :
                i == 0 ? duckdb_map_type_key_type(type) : duckdb_map_type_value_type(type);
            label(out, child, depth + 1);
            duckdb_destroy_logical_type(&child);
        }
        sc_local_text_cstr(out, ")");
    } else sc_local_text_cstr(out, type_name(id));
}

char *sc_duckdb_type_label(duckdb_logical_type type) {
    sc_local_text text = {.cap = sizeof text.bytes - 1};
    label(&text, type, 0);
    if (text.failed || text.len > text.cap) return NULL;
    text.bytes[text.len] = 0;
    return strdup(text.bytes);
}

bool sc_duckdb_type_numeric(duckdb_logical_type type) {
    duckdb_type id = duckdb_get_type_id(type);
    return (id >= DUCKDB_TYPE_TINYINT && id <= DUCKDB_TYPE_DOUBLE) ||
           id == DUCKDB_TYPE_DECIMAL || id == DUCKDB_TYPE_HUGEINT ||
           id == DUCKDB_TYPE_UHUGEINT || id == DUCKDB_TYPE_BIGNUM;
}

static duckdb_value scalar(duckdb_vector vector, duckdb_logical_type type, size_t row) {
    void *data = duckdb_vector_get_data(vector);
    duckdb_type id = duckdb_get_type_id(type);
#define SCALAR(kind, ctype, ctor) case DUCKDB_TYPE_##kind: return duckdb_create_##ctor(((ctype *)data)[row])
    switch (id) {
    SCALAR(BOOLEAN, bool, bool);
    SCALAR(TINYINT, int8_t, int8);
    SCALAR(SMALLINT, int16_t, int16);
    SCALAR(INTEGER, int32_t, int32);
    SCALAR(BIGINT, int64_t, int64);
    SCALAR(UTINYINT, uint8_t, uint8);
    SCALAR(USMALLINT, uint16_t, uint16);
    SCALAR(UINTEGER, uint32_t, uint32);
    SCALAR(UBIGINT, uint64_t, uint64);
    SCALAR(HUGEINT, duckdb_hugeint, hugeint);
    SCALAR(UHUGEINT, duckdb_uhugeint, uhugeint);
    SCALAR(FLOAT, float, float);
    SCALAR(DOUBLE, double, double);
    SCALAR(DATE, duckdb_date, date);
    SCALAR(TIME, duckdb_time, time);
    SCALAR(TIME_NS, duckdb_time_ns, time_ns);
    SCALAR(TIME_TZ, duckdb_time_tz, time_tz_value);
    SCALAR(TIMESTAMP, duckdb_timestamp, timestamp);
    SCALAR(TIMESTAMP_S, duckdb_timestamp_s, timestamp_s);
    SCALAR(TIMESTAMP_MS, duckdb_timestamp_ms, timestamp_ms);
    SCALAR(TIMESTAMP_NS, duckdb_timestamp_ns, timestamp_ns);
    SCALAR(TIMESTAMP_TZ, duckdb_timestamp, timestamp_tz);
    SCALAR(TIMESTAMP_TZ_NS, duckdb_timestamp_ns, timestamp_tz_ns);
    SCALAR(INTERVAL, duckdb_interval, interval);
    case DUCKDB_TYPE_DECIMAL: {
        duckdb_hugeint n = {0};
        switch (duckdb_decimal_internal_type(type)) {
        case DUCKDB_TYPE_SMALLINT: n.lower = (uint64_t)((int16_t *)data)[row]; n.upper = ((int16_t *)data)[row] < 0 ? -1 : 0; break;
        case DUCKDB_TYPE_INTEGER: n.lower = (uint64_t)((int32_t *)data)[row]; n.upper = ((int32_t *)data)[row] < 0 ? -1 : 0; break;
        case DUCKDB_TYPE_BIGINT: n.lower = (uint64_t)((int64_t *)data)[row]; n.upper = ((int64_t *)data)[row] < 0 ? -1 : 0; break;
        default: n = ((duckdb_hugeint *)data)[row]; break;
        }
        return duckdb_create_decimal((duckdb_decimal){duckdb_decimal_width(type), duckdb_decimal_scale(type), n});
    }
    case DUCKDB_TYPE_UUID: {
        duckdb_hugeint n = ((duckdb_hugeint *)data)[row];
        return duckdb_create_uuid((duckdb_uhugeint){n.lower, (uint64_t)n.upper ^ (UINT64_C(1) << 63)});
    }
    default: return NULL;
    }
#undef SCALAR
}

static void quoted(sc_local_text *out, const char *s, size_t n) {
    sc_local_text_cstr(out, "'");
    for (size_t i = 0; i < n; i++) {
        if (i % 4096 == 0 && out->control != NULL && sc_db_control_cancelled(out->control)) {
            out->failed = true;
            break;
        }
        sc_local_text_bytes(out, s + i, 1);
        if (s[i] == '\'') sc_local_text_cstr(out, "'");
    }
    sc_local_text_cstr(out, "'");
}

static void format(sc_local_text *out, duckdb_vector vector, size_t row, size_t depth, bool nested) {
    if (depth > 64 || (out->control != NULL && sc_db_control_cancelled(out->control))) {
        out->failed = true;
        return;
    }
    if (!duckdb_validity_row_is_valid(duckdb_vector_get_validity(vector), row)) {
        sc_local_text_cstr(out, "NULL");
        return;
    }
    duckdb_logical_type type = duckdb_vector_get_column_type(vector);
    duckdb_type id = duckdb_get_type_id(type);
    if (id == DUCKDB_TYPE_VARCHAR || id == DUCKDB_TYPE_BLOB || id == DUCKDB_TYPE_GEOMETRY) {
        duckdb_string_t *strings = duckdb_vector_get_data(vector);
        const char *bytes = duckdb_string_t_data(&strings[row]);
        size_t len = duckdb_string_t_length(strings[row]);
        if (id != DUCKDB_TYPE_VARCHAR) sc_local_text_hex(out, (const uint8_t *)bytes, len);
        else if (nested) quoted(out, bytes, len);
        else sc_local_text_bytes(out, bytes, len);
    } else if (id == DUCKDB_TYPE_LIST || id == DUCKDB_TYPE_ARRAY || id == DUCKDB_TYPE_MAP) {
        size_t start, len;
        duckdb_vector child;
        if (id == DUCKDB_TYPE_ARRAY) {
            len = duckdb_array_type_array_size(type);
            start = row * len;
            child = duckdb_array_vector_get_child(vector);
        } else {
            duckdb_list_entry entry = ((duckdb_list_entry *)duckdb_vector_get_data(vector))[row];
            start = entry.offset;
            len = entry.length;
            child = duckdb_list_vector_get_child(vector);
        }
        sc_local_text_cstr(out, id == DUCKDB_TYPE_MAP ? "{" : "[");
        for (size_t i = 0; i < len && !out->failed; i++) {
            if (i > 0) sc_local_text_cstr(out, ", ");
            if (id == DUCKDB_TYPE_MAP) {
                format(out, duckdb_struct_vector_get_child(child, 0), start + i, depth + 1, true);
                sc_local_text_cstr(out, "=");
                format(out, duckdb_struct_vector_get_child(child, 1), start + i, depth + 1, true);
            } else format(out, child, start + i, depth + 1, true);
        }
        sc_local_text_cstr(out, id == DUCKDB_TYPE_MAP ? "}" : "]");
    } else if (id == DUCKDB_TYPE_STRUCT) {
        sc_local_text_cstr(out, "{");
        size_t count = duckdb_struct_type_child_count(type);
        for (size_t i = 0; i < count && !out->failed; i++) {
            if (i > 0) sc_local_text_cstr(out, ", ");
            char *name = duckdb_struct_type_child_name(type, i);
            if (name == NULL) out->failed = true;
            else quoted(out, name, strlen(name));
            duckdb_free(name);
            sc_local_text_cstr(out, ": ");
            format(out, duckdb_struct_vector_get_child(vector, i), row, depth + 1, true);
        }
        sc_local_text_cstr(out, "}");
    } else if (id == DUCKDB_TYPE_UNION) {
        duckdb_vector tags = duckdb_struct_vector_get_child(vector, 0);
        uint8_t tag = ((uint8_t *)duckdb_vector_get_data(tags))[row];
        format(out, duckdb_struct_vector_get_child(vector, tag + 1u), row, depth + 1, nested);
    } else if (id == DUCKDB_TYPE_ENUM || id == DUCKDB_TYPE_BIT ||
               id == DUCKDB_TYPE_BIGNUM || id == DUCKDB_TYPE_VARIANT) {
        duckdb_v2_value_handle value = NULL;
        idx_t len = 0;
        char *text = NULL;
        if (duckdb_v2_vector_get_value((duckdb_v2_vector_handle)vector, row, &value, NULL) != DUCKDB_V2_ERROR_NONE ||
            duckdb_v2_value_to_string(value, NULL, 0, &len, NULL) != DUCKDB_V2_ERROR_NONE ||
            (out->control != NULL && sc_db_control_cancelled(out->control))) out->failed = true;
        else text = malloc(len + 1);
        if (text == NULL ||
            duckdb_v2_value_to_string(value, text, len + 1, &len, NULL) != DUCKDB_V2_ERROR_NONE) out->failed = true;
        else if (nested && id == DUCKDB_TYPE_ENUM) quoted(out, text, len);
        else sc_local_text_bytes(out, text, len);
        free(text);
        (void)duckdb_v2_value_destroy(&value);
    } else {
        duckdb_value value = scalar(vector, type, row);
        char *text = value == NULL ? NULL : duckdb_get_varchar(value);
        if (text == NULL) out->failed = true;
        else sc_local_text_cstr(out, text);
        duckdb_free(text);
        duckdb_destroy_value(&value);
    }
    duckdb_destroy_logical_type(&type);
}

bool sc_duckdb_format(sc_local_text *out, duckdb_vector vector, size_t row) {
    format(out, vector, row, 0, false);
    return !out->failed;
}
