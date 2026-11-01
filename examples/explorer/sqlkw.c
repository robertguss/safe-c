#include "sqlkw.h"

static uint8_t sqlkw_upper(uint8_t c) {
    return c >= 'a' && c <= 'z' ? (uint8_t)(c - 32u) : c;
}

static bool sqlkw_cmp(const sc_arena *xa, sc_str x, const sc_arena *ya, sc_str y, int *out) {
    size_t n = x.len < y.len ? x.len : y.len;
    for (size_t i = 0; i < n; i++) {
        uint8_t a = 0;
        uint8_t b = 0;
        if (!sc_slice_get(xa, x, i, &a) || !sc_slice_get(ya, y, i, &b)) {
            return false;
        }
        a = sqlkw_upper(a);
        b = sqlkw_upper(b);
        if (a != b) {
            *out = a < b ? -1 : 1;
            return true;
        }
    }
    *out = x.len < y.len ? -1 : (x.len > y.len ? 1 : 0);
    return true;
}

static bool sqlkw_init_list(sqlkw *k, sc_arena *a, sc_str list) {
    if (list.len == 0) {
        return false;
    }
    size_t count = 1;
    for (size_t i = 0; i < list.len; i++) {
        uint8_t c = 0;
        if (!sc_slice_get(a, list, i, &c)) {
            return false;
        }
        count += c == ' ' ? 1u : 0u;
    }
    sc_tslice words = SC_TSLICE_ALLOC(sc_str, a, count);
    if (sc_tslice_len(words) != count) {
        return false;
    }
    size_t start = 0;
    size_t n = 0;
    sc_str prev = {NULL, 0, a->epoch};
    for (size_t i = 0; i <= list.len; i++) {
        uint8_t c = ' ';
        if (i < list.len && !sc_slice_get(a, list, i, &c)) {
            return false;
        }
        if (c != ' ') {
            continue;
        }
        sc_str w = sc_slice_sub(a, list, start, i - start);
        int order = 0;
        if (w.len == 0 || (n > 0 && (!sqlkw_cmp(a, prev, a, w, &order) || order >= 0)) ||
            !SC_TSLICE_SET(sc_str, a, words, n, &w)) {
            return false;
        }
        prev = w;
        n++;
        start = i + 1u;
    }
    k->arena = a;
    k->words = words;
    return true;
}

/* Keyword sets stay as SC_STR_LIT branches: a const char * table or raw
 * uint8_t array would fail R3 / R6 in application C. Dialect ids match
 * sc_db_engine (see engine_policy.h). */
bool sqlkw_init(sqlkw *k, sc_arena *a, uint8_t dialect) {
    k->dialect = dialect;
    sc_str list;
    if (dialect == SQLTOK_SQLITE) {
        list = SC_STR_LIT(a,
            "ABORT ACTION ADD AFTER ALL ALTER ALWAYS ANALYZE AND AS ASC ATTACH AUTOINCREMENT "
            "BEFORE BEGIN BETWEEN BY CASCADE CASE CAST CHECK COLLATE COLUMN COMMIT CONFLICT "
            "CONSTRAINT CREATE CROSS CURRENT CURRENT_DATE CURRENT_TIME CURRENT_TIMESTAMP "
            "DATABASE DEFAULT DEFERRABLE DEFERRED DELETE DESC DETACH DISTINCT DO DROP EACH ELSE "
            "END ESCAPE EXCEPT EXCLUDE EXCLUSIVE EXISTS EXPLAIN FAIL FILTER FIRST FOLLOWING FOR "
            "FOREIGN FROM FULL GENERATED GLOB GROUP GROUPS HAVING IF IGNORE IMMEDIATE IN INDEX "
            "INDEXED INITIALLY INNER INSERT INSTEAD INTERSECT INTO IS ISNULL JOIN KEY LAST LEFT "
            "LIKE LIMIT MATCH MATERIALIZED NATURAL NO NOT NOTHING NOTNULL NULL NULLS OF OFFSET "
            "ON OR ORDER OTHERS OUTER OVER PARTITION PLAN PRAGMA PRECEDING PRIMARY QUERY RAISE "
            "RANGE RECURSIVE REFERENCES REGEXP REINDEX RELEASE RENAME REPLACE RESTRICT RETURNING "
            "RIGHT ROLLBACK ROW ROWS SAVEPOINT SELECT SET TABLE TEMP TEMPORARY THEN TIES TO "
            "TRANSACTION TRIGGER UNBOUNDED UNION UNIQUE UPDATE USING VACUUM VALUES VIEW VIRTUAL "
            "WHEN WHERE WINDOW WITH WITHOUT");
    } else if (dialect == SQLTOK_DUCKDB) {
        list = SC_STR_LIT(a,
            "ALL ANALYSE ANALYZE AND ANTI ANY ARRAY AS ASC ASOF ASYMMETRIC AT AUTHORIZATION "
            "BETWEEN BIGINT BINARY BIT BOOLEAN BOTH BY CASE CAST CHAR CHARACTER CHECK COALESCE "
            "COLLATE COLLATION COLUMN COLUMNS CONCURRENTLY CONSTRAINT CREATE CROSS DEC DECIMAL "
            "DEFAULT DEFERRABLE DESC DESCRIBE DISTINCT DO ELSE END EXCEPT EXISTS EXTRACT FALSE "
            "FETCH FLOAT FOR FOREIGN FREEZE FROM FULL GENERATED GLOB GROUP GROUPING GROUPING_ID "
            "HAVING ILIKE IN INITIALLY INNER INOUT INT INTEGER INTERSECT INTERVAL INTO IS ISNULL "
            "JOIN LAMBDA LATERAL LEADING LEFT LIKE LIMIT MAP NATIONAL NATURAL NCHAR NONE NOT "
            "NOTNULL NULL NULLIF NUMERIC OFFSET ON ONLY OR ORDER OUT OUTER OVERLAPS OVERLAY "
            "PIVOT PIVOT_LONGER PIVOT_WIDER PLACING POSITION POSITIONAL PRECISION PRIMARY "
            "QUALIFY REAL REFERENCES RETURNING RIGHT ROW SELECT SEMI SETOF SHOW SIMILAR "
            "SMALLINT SOME STRUCT SUBSTRING SUMMARIZE SYMMETRIC TABLE TABLESAMPLE THEN TIME "
            "TIMESTAMP TO TRAILING TREAT TRIM TRUE TRY_CAST TUPLE UNION UNIQUE UNPACK UNPIVOT "
            "USING VALUES VARCHAR VARIADIC VERBOSE WHEN WHERE WINDOW WITH XMLATTRIBUTES "
            "XMLCONCAT XMLELEMENT XMLEXISTS XMLFOREST XMLNAMESPACES XMLPARSE XMLPI XMLROOT "
            "XMLSERIALIZE XMLTABLE");
    } else {
        list = SC_STR_LIT(a,
        "ABORT ALL ALTER ANALYSE ANALYZE AND ANY ARRAY AS ASC ASYMMETRIC ATOMIC "
        "AUTHORIZATION BEGIN BETWEEN BINARY BOTH BY CALL CASCADE CASE CAST CHECK COLLATE "
        "COLLATION COLUMN COMMENT COMMIT CONCURRENTLY CONFLICT CONSTRAINT COPY CREATE "
        "CROSS CURRENT_CATALOG CURRENT_DATE CURRENT_ROLE CURRENT_SCHEMA CURRENT_TIME "
        "CURRENT_TIMESTAMP CURRENT_USER DATABASE DECLARE DEFAULT DEFERRABLE DELETE DESC "
        "DISTINCT DO DROP ELSE END EXCEPT EXECUTE EXISTS EXPLAIN EXTENSION FALSE FETCH "
        "FILTER FOR FOREIGN FREEZE FROM FULL FUNCTION GRANT GROUP HAVING IF ILIKE IN INDEX "
        "INITIALLY INNER INSERT INTERSECT INTERVAL INTO IS ISNULL JOIN KEY LANGUAGE "
        "LATERAL LEADING LEFT LIKE LIMIT LOCALTIME LOCALTIMESTAMP MATERIALIZED NATURAL NOT "
        "NOTHING NOTNULL NULL OFFSET ON ONLY OR ORDER OUTER OVER OVERLAPS PARTITION "
        "PLACING PRIMARY PROCEDURE RECURSIVE REFERENCES REPLACE RESTRICT RETURNING RETURNS "
        "REVOKE RIGHT ROLLBACK ROW ROWS SCHEMA SELECT SEQUENCE SESSION_USER SET SHOW "
        "SIMILAR SOME START SYMMETRIC SYSTEM_USER TABLE TABLESAMPLE TEMP TEMPORARY THEN TO "
        "TRAILING TRANSACTION TRIGGER TRUE TRUNCATE TYPE UNION UNIQUE UPDATE USER USING "
        "VACUUM VALUES VARIADIC VERBOSE VIEW WHEN WHERE WINDOW WITH");
    }
    return sqlkw_init_list(k, a, list);
}

bool sqlkw_is(const sqlkw *k, const sc_arena *wa, sc_str word) {
    size_t lo = 0;
    size_t hi = sc_tslice_len(k->words);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        sc_str kw = {NULL, 0, 0};
        int order = 0;
        if (!SC_TSLICE_GET(sc_str, k->arena, k->words, mid, &kw) ||
            !sqlkw_cmp(wa, word, k->arena, kw, &order)) {
            return false;
        }
        if (order == 0) {
            return true;
        }
        if (order < 0) {
            hi = mid;
        } else {
            lo = mid + 1u;
        }
    }
    return false;
}
