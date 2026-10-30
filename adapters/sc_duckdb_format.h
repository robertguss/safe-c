#ifndef SAFEC_ADAPTER_DUCKDB_FORMAT_H
#define SAFEC_ADAPTER_DUCKDB_FORMAT_H

#include "sc_local.h"
#include <duckdb.h>

char *sc_duckdb_type_label(duckdb_logical_type type);
bool sc_duckdb_type_numeric(duckdb_logical_type type);
bool sc_duckdb_format(sc_local_text *out, duckdb_vector vector, size_t row);

#endif
