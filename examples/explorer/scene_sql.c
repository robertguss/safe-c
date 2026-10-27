#include "scene_sql.h"

sc_str scene_sql(sc_arena *a, uint8_t run) {
    switch (run) {
    case RUN_ROWS:
        return SC_STR_LIT(a, "SELECT g AS id,\n"
                             "       CASE g WHEN 3 THEN 'Zo\xC3\xAB \xC3\x85ngstr\xC3\xB6m'\n"
                             "              WHEN 5 THEN 'stacked e' || repeat(U&'\\0301', 12)\n"
                             "              ELSE 'user ' || g END AS name,\n"
                             "       CASE WHEN g % 7 = 0 THEN NULL ELSE g * 1.25 END AS score,\n"
                             "       g % 2 = 0 AS even\n"
                             "FROM generate_series(1, 250) AS g;");
    case RUN_WIDE:
        /* LATIN1 makes the server send caf\xE9 as one byte, not UTF-8: the cell
         * shows U+FFFD for it. */
        return SC_STR_LIT(a, "SET client_encoding TO 'LATIN1';\n"
                             "SELECT g AS id, 'caf\xC3\xA9' AS latin1, repeat('W', 2000) AS wide,\n"
                             "       repeat('m', 2000) AS next\n"
                             "FROM generate_series(1, 3) AS g;");
    case RUN_TAG:
        return SC_STR_LIT(a, "CREATE TEMP TABLE notes (id serial PRIMARY KEY, body text);");
    case RUN_ERROR:
        return SC_STR_LIT(a, "SELECT relnam\nFROM pg_class;");
    case RUN_ERROR_ROWS:
        return SC_STR_LIT(a, "SELECT g, 100 / (60 - g) AS ratio\nFROM generate_series(1, 100) AS g;");
    case RUN_TERMINATE:
        return SC_STR_LIT(a, "SELECT pg_terminate_backend(pg_backend_pid());");
    case RUN_GRID:
        return SC_STR_LIT(a, "SELECT g AS id, 'customer ' || g AS name,\n"
                             "       round(g * 19.99, 2) AS total,\n"
                             "       CASE WHEN g % 5 = 0 THEN NULL ELSE g % 3 = 0 END AS active,\n"
                             "       date '2026-01-01' + g AS since\n"
                             "FROM generate_series(1, 5000) AS g;");
    case RUN_NULL_WRAP:
        return SC_STR_LIT(a, "SELECT g AS id,\n"
                             "       CASE WHEN g % 2 = 0 THEN NULL\n"
                             "            ELSE 'line one' || chr(10) || 'line two' || chr(9) || 'tab' END AS note,\n"
                             "       CASE WHEN g % 3 = 0 THEN NULL ELSE g END AS maybe\n"
                             "FROM generate_series(1, 40) AS g;");
    case RUN_CAPPED:
        return SC_STR_LIT(a, "SELECT g AS n, md5(g::text) AS hash\nFROM generate_series(1, 50000) AS g;");
    case RUN_DETAIL:
        return SC_STR_LIT(a, "SELECT 42 AS id, 'Zo\xC3\xAB \xC3\x85ngstr\xC3\xB6m' AS name,\n"
                             "       NULL::text AS nickname,\n"
                             "       'A note that is long enough to wrap across several lines of the pane, '\n"
                             "       || 'with an emoji \xF0\x9F\x98\x80 in it.' || chr(10) || 'And a second paragraph.' AS note,\n"
                             "       repeat('x', 10240) AS big, now()::date - date '2026-01-01' AS days;");
    case RUN_TABS:
        /* The first tab of the scene; the others are in play_tabs. */
        return SC_STR_LIT(a, "SELECT count(*) AS tables FROM pg_class WHERE relkind = 'r';");
    case RUN_MILLION:
        return SC_STR_LIT(a, "SELECT g AS n\nFROM generate_series(1, 1000000) AS g;");
    case RUN_CANCELLING:
    case RUN_DISCONNECT_OFFER:
        /* One row per 20 ms, as in tabs-streaming: always mid-stream. */
        return SC_STR_LIT(a, "SELECT g AS n, md5(g::text) || left(pg_sleep(0.02)::text, 0) AS hash\n"
                             "FROM generate_series(1, 3000) AS g;");
    case RUN_RECONNECT:
        /* The backend ends itself at row 300, after the rows before it. */
        return SC_STR_LIT(a, "SELECT g AS id, 'customer ' || g AS name,\n"
                             "       CASE WHEN g = 300 THEN pg_terminate_backend(pg_backend_pid()) END AS ended\n"
                             "FROM generate_series(1, 300) AS g;");
    case RUN_META_LOST:
        return SC_STR_LIT(a, "SELECT pg_terminate_backend(pid) AS ended\n"
                             "FROM pg_stat_activity\n"
                             "WHERE application_name = 'explorer-meta';");
    case RUN_SIDEBAR:
    case RUN_SIDEBAR_ERROR:
        return SC_STR_LIT(a, "DO $$ BEGIN\n"
            "IF NOT EXISTS (SELECT 1 FROM pg_catalog.pg_namespace WHERE nspname = 'shop') THEN\n"
            "CREATE SCHEMA shop;\n"
            "CREATE TABLE shop.customers (id int PRIMARY KEY, name text NOT NULL, email text);\n"
            "INSERT INTO shop.customers SELECT g, 'customer ' || g, 'c' || g || '@example.com'\n"
            "  FROM generate_series(1, 60) AS g;\n"
            "CREATE TABLE shop.orders (id int, customer_id int, placed date NOT NULL, total numeric(10,2))\n"
            "  PARTITION BY RANGE (placed);\n"
            "CREATE TABLE shop.orders_2026 PARTITION OF shop.orders\n"
            "  FOR VALUES FROM ('2026-01-01') TO ('2027-01-01');\n"
            "CREATE VIEW shop.recent_orders AS SELECT * FROM shop.orders WHERE placed > date '2026-10-01';\n"
            "CREATE MATERIALIZED VIEW shop.order_totals AS\n"
            "  SELECT customer_id, sum(total) AS total FROM shop.orders GROUP BY 1;\n"
            "CREATE FOREIGN DATA WRAPPER explorer_fdw;\n"
            "CREATE SERVER explorer_remote FOREIGN DATA WRAPPER explorer_fdw;\n"
            "CREATE FOREIGN TABLE shop.warehouse_stock (sku text, qty int) SERVER explorer_remote;\n"
            "CREATE FUNCTION shop.order_count(since date) RETURNS bigint LANGUAGE sql STABLE\n"
            "  AS $f$SELECT count(*) FROM shop.orders WHERE placed >= since$f$;\n"
            "CREATE FUNCTION shop.customer_label(c shop.customers) RETURNS text LANGUAGE sql IMMUTABLE\n"
            "  AS $f$SELECT c.name || ' <' || c.email || '>'$f$;\n"
            "CREATE PROCEDURE shop.tidy() LANGUAGE sql AS $f$SELECT 1$f$;\n"
            "CREATE SCHEMA analytics;\n"
            "CREATE TABLE analytics.events (id bigint, name text, at timestamptz);\n"
            "CREATE TABLE analytics.sessions (id bigint, started timestamptz);\n"
            "CREATE MATERIALIZED VIEW analytics.daily_events AS\n"
            "  SELECT date_trunc('day', at) AS day, count(*) AS n FROM analytics.events GROUP BY 1;\n"
            "CREATE FUNCTION analytics.event_count() RETURNS bigint LANGUAGE sql\n"
            "  AS $f$SELECT count(*) FROM analytics.events$f$;\n"
            "CREATE SCHEMA archive;\n"
            "CREATE TABLE archive.orders_2025 (id int, placed date);\n"
            "CREATE SCHEMA broken;\n"
            "CREATE TABLE broken.stats (n int);\n"
            "UPDATE pg_catalog.pg_class SET reltuples = 1e30\n"
            "  WHERE oid = 'broken.stats'::pg_catalog.regclass;\n"
            "END IF;\n"
            "END $$;");
    case RUN_TABLE_CONTENT:
    case RUN_TABLE_STRUCTURE:
    case RUN_TABLE_NOTE:
    case RUN_TABLE_BACK:
        return SC_STR_LIT(a, "DO $$ BEGIN\n"
            "IF NOT EXISTS (SELECT 1 FROM pg_catalog.pg_namespace WHERE nspname = 'store') THEN\n"
            "CREATE SCHEMA store;\n"
            "CREATE TABLE store.categories (id int PRIMARY KEY, name text NOT NULL);\n"
            "INSERT INTO store.categories SELECT g, 'category ' || g FROM generate_series(1, 12) AS g;\n"
            "CREATE TABLE store.products (id int GENERATED ALWAYS AS IDENTITY PRIMARY KEY,\n"
            "  name text NOT NULL UNIQUE, price numeric(10,2) NOT NULL CHECK (price > 0),\n"
            "  category_id int REFERENCES store.categories (id),\n"
            "  added date NOT NULL DEFAULT date '2026-01-01',\n"
            "  taxed numeric GENERATED ALWAYS AS (price * 1.2) STORED);\n"
            "INSERT INTO store.products (name, price, category_id, added)\n"
            "  SELECT 'product ' || g, round((g % 97) * 1.37 + 0.5, 2), 1 + g % 12, date '2026-01-01' + g % 300\n"
            "  FROM generate_series(1, 2500) AS g;\n"
            "CREATE INDEX products_category ON store.products (category_id);\n"
            "CREATE VIEW store.cheap_products AS SELECT id, name, price FROM store.products WHERE price < 20;\n"
            "ANALYZE store.products;\n"
            "END IF;\n"
            "END $$;");
    default:
        return SC_STR_LIT(a, "");
    }
}
