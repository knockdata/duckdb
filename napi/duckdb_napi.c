// N-API wrapper over duckdb's C API — the duckdb sibling of napi-sqlite.c.
// Pure C, no dependencies beyond node_api.h and duckdb.h.
//
// A handle bundles the database and its one connection, because everything above
// this opens a file, reads from it and closes it again.
//   open(path)        -> handle, opened READ_ONLY with a single thread
//   close(handle)
//   query(handle, sql)-> [{column: value, ...}, ...]
#include <node_api.h>
#include "duckdb.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct {
	duckdb_database database;
	duckdb_connection connection;
} Handle;

static char *get_string_arg(napi_env env, napi_value value) {
	size_t length;
	napi_get_value_string_utf8(env, value, NULL, 0, &length);
	char *text = malloc(length + 1);
	napi_get_value_string_utf8(env, value, text, length + 1, &length);
	return text;
}

// open(path) -> external handle. Read only, so a user's db is never rewritten.
static napi_value napi_open(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	char *path = get_string_arg(env, args[0]);

	duckdb_config config;
	duckdb_create_config(&config);
	duckdb_set_config(config, "access_mode", "READ_ONLY");
	duckdb_set_config(config, "threads", "1");

	Handle *handle = malloc(sizeof(Handle));
	char *error = NULL;
	duckdb_state state = duckdb_open_ext(path, &handle->database, config, &error);
	duckdb_destroy_config(&config);
	free(path);

	if (state == DuckDBSuccess) {
		duckdb_connect(handle->database, &handle->connection);
		napi_value result;
		napi_create_external(env, handle, NULL, NULL, &result);
		return result;
	}
	else {
		char message[512];
		snprintf(message, sizeof(message), "duckdb open: %s", error ? error : "unknown error");
		duckdb_free(error);
		free(handle);
		napi_throw_error(env, NULL, message);
		return NULL;
	}
}

static napi_value napi_close(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);

	Handle *handle;
	napi_get_value_external(env, args[0], (void **)&handle);
	duckdb_disconnect(&handle->connection);
	duckdb_close(&handle->database);
	free(handle);

	napi_value undefined;
	napi_get_undefined(env, &undefined);
	return undefined;
}

// one cell. Integers and floats keep their type; everything duckdb has and
// JavaScript does not — dates, timestamps, decimals, hugeints, lists, structs —
// comes back as the string duckdb itself would print.
static napi_value cell_to_napi(napi_env env, duckdb_result *result, idx_t column, idx_t row) {
	napi_value value;
	if (duckdb_value_is_null(result, column, row)) {
		napi_get_null(env, &value);
	}
	else {
		duckdb_type type = duckdb_column_type(result, column);
		if (type == DUCKDB_TYPE_BOOLEAN) {
			napi_get_boolean(env, duckdb_value_boolean(result, column, row), &value);
		}
		else if (type == DUCKDB_TYPE_TINYINT || type == DUCKDB_TYPE_SMALLINT
			|| type == DUCKDB_TYPE_INTEGER || type == DUCKDB_TYPE_BIGINT
			|| type == DUCKDB_TYPE_UTINYINT || type == DUCKDB_TYPE_USMALLINT
			|| type == DUCKDB_TYPE_UINTEGER || type == DUCKDB_TYPE_UBIGINT) {
			napi_create_int64(env, duckdb_value_int64(result, column, row), &value);
		}
		else if (type == DUCKDB_TYPE_FLOAT || type == DUCKDB_TYPE_DOUBLE) {
			napi_create_double(env, duckdb_value_double(result, column, row), &value);
		}
		else if (type == DUCKDB_TYPE_BLOB) {
			duckdb_blob blob = duckdb_value_blob(result, column, row);
			void *bytes;
			napi_create_arraybuffer(env, blob.size, &bytes, &value);
			if (blob.data && blob.size > 0) {
				memcpy(bytes, blob.data, blob.size);
			}
			duckdb_free(blob.data);
		}
		else {
			char *text = duckdb_value_varchar(result, column, row);
			napi_create_string_utf8(env, text ? text : "", NAPI_AUTO_LENGTH, &value);
			duckdb_free(text);
		}
	}
	return value;
}

// query(handle, sql) -> [{column: value, ...}, ...]
static napi_value napi_query(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);

	Handle *handle;
	napi_get_value_external(env, args[0], (void **)&handle);
	char *sql = get_string_arg(env, args[1]);

	duckdb_result result;
	duckdb_state state = duckdb_query(handle->connection, sql, &result);
	free(sql);

	if (state == DuckDBSuccess) {
		idx_t columns = duckdb_column_count(&result);
		idx_t rows = duckdb_row_count(&result);
		napi_value list;
		napi_create_array_with_length(env, rows, &list);
		for (idx_t row = 0; row < rows; row++) {
			napi_value entry;
			napi_create_object(env, &entry);
			for (idx_t column = 0; column < columns; column++) {
				napi_set_named_property(env, entry, duckdb_column_name(&result, column),
					cell_to_napi(env, &result, column, row));
			}
			napi_set_element(env, list, row, entry);
		}
		duckdb_destroy_result(&result);
		return list;
	}
	else {
		char message[1024];
		snprintf(message, sizeof(message), "duckdb query: %s", duckdb_result_error(&result));
		duckdb_destroy_result(&result);
		napi_throw_error(env, NULL, message);
		return NULL;
	}
}

static napi_value init(napi_env env, napi_value exports) {
	napi_value open_fn, close_fn, query_fn;
	napi_create_function(env, NULL, 0, napi_open, NULL, &open_fn);
	napi_create_function(env, NULL, 0, napi_close, NULL, &close_fn);
	napi_create_function(env, NULL, 0, napi_query, NULL, &query_fn);
	napi_set_named_property(env, exports, "open", open_fn);
	napi_set_named_property(env, exports, "close", close_fn);
	napi_set_named_property(env, exports, "query", query_fn);
	return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
