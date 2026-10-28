#ifndef EXPLORER_PAGE_H
#define EXPLORER_PAGE_H

/* A table's Content pages and its Structure rows (plan U20). Core code: it
 * includes only the stdlib and the catalog model.
 *
 * A Content page is `SELECT * FROM "schema"."table" ORDER BY ... LIMIT
 * <PAGE_SIZE> OFFSET <(page - 1) * PAGE_SIZE>`, pages counted from 1,
 * identifiers always quoted (KTD21). It orders by the primary key's columns
 * in key order; with no key, a table or materialised view by ctid and a
 * partitioned table by tableoid, ctid. A view or a foreign table gets no
 * ORDER BY, and the caller shows a note that row order may change between
 * pages. A table's key comes from its columns read (catalog_want_detail),
 * so page_ready says when the page can be written.
 *
 * The pager's N (the product decision on it): the planner's estimate on
 * open, "Page k of ~N", or "Page k" when it is unknown; then a count(*) on
 * the metadata link (catalog_want_count), "Page k of N"; a count that timed
 * out shows "Page k" for the rest of the view's life. */

#include "catalog.h"

#define PAGE_SIZE 1000u

typedef enum { PAGE_COUNT_PENDING, PAGE_COUNT_KNOWN, PAGE_COUNT_TIMED_OUT } page_count;

/* A view's total: the estimate it opened with and its count, if any. */
typedef struct {
    bool estimate_known;
    uint64_t estimate;
    page_count count_state;
    uint64_t count;
} page_total;

/* The total a view of relation `r` opens with: its estimate, count pending. */
page_total page_total_open(const catalog_relation *r);

/* `t` with the count of relation `oid` in schema `schema` once it has
 * landed in the catalog: KNOWN, or TIMED_OUT when it failed. A count that
 * is no longer pending stays as it is. */
page_total page_total_update(page_total t, const catalog *c, uint32_t schema, uint32_t oid);

/* Whether page_sql can write relation `oid`'s page now: a view or foreign
 * table (page_unordered: no row order) needs no key, and a table's columns
 * read has landed or its detail read has ended. False when the records have
 * no such relation. */
bool page_ready(const catalog *c, uint32_t schema, uint32_t oid);
bool page_unordered(catalog_rel_kind k);

/* Page `page` (from 1) of relation `oid` of schema `schema`, in `a`.
 * `*note` says the relation has no row order (a view or foreign table).
 * False when the records have no such relation, `page` is 0, or there is no
 * room; `*out` is left alone then. */
bool page_sql(const catalog *c, uint32_t schema, uint32_t oid, uint64_t page, sc_arena *a,
              sc_str *out, bool *note);

/* Queues the reads a table view of relation `oid` opens with: its detail,
 * then its count, so the first page does not wait behind count(*). */
void page_open(catalog *c, uint32_t schema, uint32_t oid);

/* Asks again for what a table view of relation `oid`, with total `t`, still
 * needs after a restart forgot it: the detail when none was asked for, and,
 * when `count`, the count while `t` waits for one and none was asked for. */
void page_rewant(catalog *c, uint32_t schema, uint32_t oid, page_total t, bool count);

/* Whether relation `oid` is gone (dropped): the schema list loaded without
 * its schema, or its schema's relations loaded without it. Records not yet
 * read are not gone. */
bool page_gone(const catalog *c, uint32_t schema, uint32_t oid);

/* The pager's text: "Page k", "Page k of ~N" or "Page k of N", N being the
 * pages the rows fill (at least 1). */
sc_str page_label(sc_arena *a, uint64_t page, page_total t, bool *lost);

/* The bottom bar's row count for a page that holds `rows` rows: "Rows
 * 2,001–2,500 of ~2,500" by the same rule as page_label, " (memory limit)"
 * after it when the page was `capped`, and "No rows on this page" for none. */
sc_str page_rows_label(sc_arena *a, uint64_t page, uint64_t rows, page_total t, bool capped,
                       bool *lost);

/* ‹ is enabled past page 1; › once a page has `ended` with PAGE_SIZE rows
 * and was not `capped` by memory. */
bool page_can_prev(uint64_t page);
bool page_can_next(bool ended, uint64_t rows, bool capped);

/* ---- The Structure view's rows ----
 *
 * Three sections, each a header row and then its items: Columns (every
 * column, in attnum order), Indexes, and Constraints (check and foreign-key
 * constraints only; the key and unique ones show as column chips). */

typedef enum {
    PAGE_ROW_SECTION,   /* `name`: Columns, Indexes or Constraints */
    PAGE_ROW_COLUMN,    /* `name`, `type`, `detail` (the default) and `chips` */
    PAGE_ROW_INDEX,     /* `name` and `detail` (pg_get_indexdef) */
    PAGE_ROW_CONSTRAINT /* `name` and `detail` (pg_get_constraintdef) */
} page_row_kind;

enum {
    PAGE_CHIP_PRIMARY_KEY = 1u,
    PAGE_CHIP_NOT_NULL = 2u,
    PAGE_CHIP_UNIQUE = 4u,    /* a unique constraint on this column alone */
    PAGE_CHIP_FOREIGN = 8u,   /* `target` is the referenced table */
    PAGE_CHIP_IDENTITY = 16u,
    PAGE_CHIP_GENERATED = 32u
};

typedef struct {
    page_row_kind kind;
    sc_str name, type, detail, target; /* live in the catalog arena, or `a` for a section's name */
    uint32_t chips;
} page_row;

/* Rows relation `r`'s Structure shows. */
size_t page_structure_count(const catalog *c, const catalog_relation *r);

/* Row `i`; a section's name is written in `a`. False past the end. */
bool page_structure_row(const catalog *c, const catalog_relation *r, size_t i, sc_arena *a,
                        page_row *out);

#endif
