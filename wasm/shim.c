// A flat C surface over duckdb's C API for the wasm build.
//
// Two reasons this exists rather than exporting duckdb.h directly: several duckdb
// functions return structs by value (duckdb_blob), which wasm turns into a hidden
// out-pointer argument that is painful to call from JS, and a handle is a pair
// (database + connection) that JS would otherwise have to track as two pointers.
#include "duckdb.h"
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>

typedef struct {
	duckdb_database database;
	duckdb_connection connection;
} Handle;

// Open on a single thread. Returns NULL on failure.
//
// read_only is what browsing a db file wants, and it is the default everywhere above this.
// Querying a parquet file wants the opposite: the file is staged into the emscripten
// filesystem and the database itself is ":memory:", which READ_ONLY refuses to open at all.
EMSCRIPTEN_KEEPALIVE Handle *shim_open(const char *path, int read_only) {
	duckdb_config config;
	duckdb_create_config(&config);
	if (read_only) {
		duckdb_set_config(config, "access_mode", "READ_ONLY");
	}
	duckdb_set_config(config, "threads", "1");

	Handle *handle = malloc(sizeof(Handle));
	char *error = NULL;
	duckdb_state state = duckdb_open_ext(path, &handle->database, config, &error);
	duckdb_destroy_config(&config);

	if (state == DuckDBSuccess) {
		duckdb_connect(handle->database, &handle->connection);
		return handle;
	}
	else {
		duckdb_free(error);
		free(handle);
		return NULL;
	}
}

EMSCRIPTEN_KEEPALIVE void shim_close(Handle *handle) {
	duckdb_disconnect(&handle->connection);
	duckdb_close(&handle->database);
	free(handle);
}

// run sql. The result is owned by the caller until shim_destroy_result; a failed
// query still returns a result, whose shim_error is the message.
EMSCRIPTEN_KEEPALIVE duckdb_result *shim_query(Handle *handle, const char *sql) {
	duckdb_result *result = malloc(sizeof(duckdb_result));
	memset(result, 0, sizeof(duckdb_result));
	duckdb_query(handle->connection, sql, result);
	return result;
}

// NULL while the query succeeded
EMSCRIPTEN_KEEPALIVE const char *shim_error(duckdb_result *result) {
	return duckdb_result_error(result);
}

EMSCRIPTEN_KEEPALIVE void shim_destroy_result(duckdb_result *result) {
	duckdb_destroy_result(result);
	free(result);
}

EMSCRIPTEN_KEEPALIVE int shim_columns(duckdb_result *result) {
	return (int)duckdb_column_count(result);
}

EMSCRIPTEN_KEEPALIVE int shim_rows(duckdb_result *result) {
	return (int)duckdb_row_count(result);
}

EMSCRIPTEN_KEEPALIVE const char *shim_column_name(duckdb_result *result, int column) {
	return duckdb_column_name(result, (idx_t)column);
}

EMSCRIPTEN_KEEPALIVE int shim_column_type(duckdb_result *result, int column) {
	return (int)duckdb_column_type(result, (idx_t)column);
}

EMSCRIPTEN_KEEPALIVE int shim_is_null(duckdb_result *result, int column, int row) {
	return duckdb_value_is_null(result, (idx_t)column, (idx_t)row) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE double shim_value_double(duckdb_result *result, int column, int row) {
	return duckdb_value_double(result, (idx_t)column, (idx_t)row);
}

EMSCRIPTEN_KEEPALIVE int64_t shim_value_int64(duckdb_result *result, int column, int row) {
	return duckdb_value_int64(result, (idx_t)column, (idx_t)row);
}

EMSCRIPTEN_KEEPALIVE int shim_value_boolean(duckdb_result *result, int column, int row) {
	return duckdb_value_boolean(result, (idx_t)column, (idx_t)row) ? 1 : 0;
}

// caller frees with shim_free
EMSCRIPTEN_KEEPALIVE char *shim_value_varchar(duckdb_result *result, int column, int row) {
	return duckdb_value_varchar(result, (idx_t)column, (idx_t)row);
}

EMSCRIPTEN_KEEPALIVE void shim_free(void *pointer) {
	duckdb_free(pointer);
}
