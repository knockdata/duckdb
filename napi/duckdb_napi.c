// N-API wrapper over duckdb's C API — the duckdb sibling of napi-sqlite.c.
// Pure C, no dependencies beyond node_api.h and duckdb.h.
//
// A handle bundles the database and its one connection, because everything above
// this opens a file, reads from it and closes it again.
//   open(path, opts)  -> handle, single threaded; READ_ONLY unless opts.readOnly is false
//   close(handle)
//   query(handle, sql)-> Promise<[{column: value, ...}, ...]>
//
// query runs on a worker thread. One connection is not safe for two queries at once, so a
// caller awaits one before asking the next — which every caller here already does.
//
// Browsing a db file wants READ_ONLY, which is the default and never rewrites a user's
// file. Analytical work wants open(":memory:", { readOnly: false }) instead: reading a
// parquet file or a bucket needs somewhere to put temporary state, and READ_ONLY refuses
// to open an in-memory database at all.
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

// opts.readOnly, defaulting to true when there is no options object and when the property
// is absent, so every existing open(path) call keeps opening read only.
static bool read_only_option(napi_env env, size_t argc, napi_value *args) {
	bool read_only = true;
	if (argc >= 2) {
		napi_valuetype type;
		napi_typeof(env, args[1], &type);
		if (type == napi_object) {
			napi_value value;
			napi_get_named_property(env, args[1], "readOnly", &value);
			napi_valuetype value_type;
			napi_typeof(env, value, &value_type);
			if (value_type == napi_boolean) {
				napi_get_value_bool(env, value, &read_only);
			}
		}
	}
	return read_only;
}

// open(path, options) -> external handle.
static napi_value napi_open(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);
	char *path = get_string_arg(env, args[0]);

	duckdb_config config;
	duckdb_create_config(&config);
	if (read_only_option(env, argc, args)) {
		duckdb_set_config(config, "access_mode", "READ_ONLY");
	}
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
// comes back as the string duckdb itself would print. See cast_complex_columns for how
// the nested and extension types get there.
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

// Does this column hold something the row API refuses to turn into text?
//
// duckdb_value_varchar is the deprecated row-based accessor, and it never materialises a
// LIST, a STRUCT, a MAP or an extension type like INET — it answers NULL for them, which
// used to reach JavaScript as an empty string. Rather than enumerate the type ids that
// behave this way (and get it wrong again when duckdb adds one), ask the value itself:
// the first cell that is not NULL and does not convert is a column that needs casting.
static bool needs_cast(duckdb_result *result, idx_t column, idx_t rows) {
	bool needed = false;
	bool answered = false;
	for (idx_t row = 0; row < rows && !answered; row++) {
		if (!duckdb_value_is_null(result, column, row)) {
			char *text = duckdb_value_varchar(result, column, row);
			needed = text == NULL;
			answered = true;
			duckdb_free(text);
		}
	}
	return needed;
}

// A column name as SQL, with any embedded quote doubled.
static void append_quoted(char *sql, size_t size, const char *name) {
	size_t at = strlen(sql);
	if (at + 2 < size) {
		sql[at++] = '"';
		for (const char *c = name; *c && at + 3 < size; c++) {
			if (*c == '"') {
				sql[at++] = '"';
			}
			sql[at++] = *c;
		}
		sql[at++] = '"';
		sql[at] = '\0';
	}
}

// Wrap the query so duckdb prints the columns the row API would not.
//
//   SELECT * REPLACE (CAST("items" AS VARCHAR) AS "items") FROM (<original>) AS _t
//
// REPLACE rather than a full column list, so every other column keeps its own type, its
// name and its position, and the SQL stays the same length whatever the result is wide.
// Returns NULL when nothing needs it, which is every ordinary query.
static char *cast_complex_columns(duckdb_result *result, const char *sql) {
	idx_t columns = duckdb_column_count(result);
	idx_t rows = duckdb_row_count(result);

	char replacements[4096];
	replacements[0] = '\0';
	bool any = false;
	for (idx_t column = 0; column < columns; column++) {
		if (needs_cast(result, column, rows)) {
			const char *name = duckdb_column_name(result, column);
			if (any) {
				strncat(replacements, ", ", sizeof(replacements) - strlen(replacements) - 1);
			}
			strncat(replacements, "CAST(", sizeof(replacements) - strlen(replacements) - 1);
			append_quoted(replacements, sizeof(replacements), name);
			strncat(replacements, " AS VARCHAR) AS ", sizeof(replacements) - strlen(replacements) - 1);
			append_quoted(replacements, sizeof(replacements), name);
			any = true;
		}
	}

	if (any) {
		size_t size = strlen(sql) + strlen(replacements) + 64;
		char *wrapped = malloc(size);
		snprintf(wrapped, size, "SELECT * REPLACE (%s) FROM (%s) AS _t", replacements, sql);
		return wrapped;
	}
	else {
		return NULL;
	}
}

// One query in flight: the SQL going in, the result coming back, and the promise waiting.
typedef struct {
	duckdb_connection connection;
	char *sql;
	duckdb_result result;
	duckdb_state state;
	napi_deferred deferred;
	napi_async_work work;
} Query;

// Runs on a libuv worker thread, with no env and therefore no touching JavaScript.
//
// Off the main thread is not an optimisation here, it is the only thing that works. Our
// objectfs extension answers a cloud path by asking the explorer's own web server for it —
// and that server is this very Node process. Running duckdb on the main thread would block
// the event loop inside duckdb_query, the HTTP request would never be served, and the two
// would wait for each other forever.
static void run_query(napi_env env, void *data) {
	Query *query = data;
	query->state = duckdb_query(query->connection, query->sql, &query->result);

	// A result holding a list, a struct or an extension type is run once more with those
	// columns cast to text, because that is the only way to get duckdb's own printed form
	// of them through the row API. Ordinary results never take this branch — cast_complex_
	// columns answers NULL for them and nothing else happens here.
	if (query->state == DuckDBSuccess) {
		char *wrapped = cast_complex_columns(&query->result, query->sql);
		if (wrapped) {
			duckdb_result printable;
			if (duckdb_query(query->connection, wrapped, &printable) == DuckDBSuccess) {
				duckdb_destroy_result(&query->result);
				query->result = printable;
			}
			else {
				// the wrap did not take — a query that is not a subquery, most likely.
				// The first result is still a real answer, so it stands.
				duckdb_destroy_result(&printable);
			}
			free(wrapped);
		}
	}
}

// Back on the main thread, where the result becomes JavaScript.
static void finish_query(napi_env env, napi_status status, void *data) {
	Query *query = data;

	if (query->state == DuckDBSuccess) {
		idx_t columns = duckdb_column_count(&query->result);
		idx_t rows = duckdb_row_count(&query->result);
		napi_value list;
		napi_create_array_with_length(env, rows, &list);
		for (idx_t row = 0; row < rows; row++) {
			napi_value entry;
			napi_create_object(env, &entry);
			for (idx_t column = 0; column < columns; column++) {
				napi_set_named_property(env, entry, duckdb_column_name(&query->result, column),
					cell_to_napi(env, &query->result, column, row));
			}
			napi_set_element(env, list, row, entry);
		}
		napi_resolve_deferred(env, query->deferred, list);
	}
	else {
		char message[1024];
		snprintf(message, sizeof(message), "duckdb query: %s", duckdb_result_error(&query->result));
		napi_value error, text;
		napi_create_string_utf8(env, message, NAPI_AUTO_LENGTH, &text);
		napi_create_error(env, NULL, text, &error);
		napi_reject_deferred(env, query->deferred, error);
	}

	duckdb_destroy_result(&query->result);
	napi_delete_async_work(env, query->work);
	free(query->sql);
	free(query);
}

// query(handle, sql) -> Promise<[{column: value, ...}, ...]>
static napi_value napi_query(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2];
	napi_get_cb_info(env, info, &argc, args, NULL, NULL);

	Handle *handle;
	napi_get_value_external(env, args[0], (void **)&handle);

	Query *query = malloc(sizeof(Query));
	query->connection = handle->connection;
	query->sql = get_string_arg(env, args[1]);

	napi_value promise;
	napi_create_promise(env, &query->deferred, &promise);

	napi_value name;
	napi_create_string_utf8(env, "duckdb query", NAPI_AUTO_LENGTH, &name);
	napi_create_async_work(env, NULL, name, run_query, finish_query, query, &query->work);
	napi_queue_async_work(env, query->work);

	return promise;
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
