// DuckDB primitive calls over an initialized Emscripten Module, the sibling of
// server/sqlite/wasm/core.js. Core(Module) returns { load, open, close, query }.
//
// The file is written into emscripten's in-memory filesystem first, then reached by
// path — duckdb uses ordinary POSIX calls, so MEMFS is all it needs and there is no
// VFS to register. That is true of a db file opened directly, and equally of a parquet
// staged with load() and then named in a query against ':memory:'.

// duckdb_type values that map onto a JavaScript number or boolean. Everything
// duckdb has and JavaScript does not — dates, timestamps, decimals, hugeints,
// lists, structs — comes back as the string duckdb itself would print.
const TYPE_BOOLEAN = 1
const INTEGER_TYPES = new Set([2, 3, 4, 5, 6, 7, 8, 9])
const FLOAT_TYPES = new Set([10, 11])

export default function Core(Module) {
	return { load, open, close, query }

	function stringToPointer(text) {
		const length = Module.lengthBytesUTF8(text) + 1
		const pointer = Module._malloc(length)
		Module.stringToUTF8(text, pointer, length)
		return pointer
	}

	// put the db bytes in MEMFS under a name of our choosing and hand back that path
	function load(name, bytes) {
		Module.FS.writeFile(name, new Uint8Array(bytes))
		return name
	}

	// readOnly false is what ':memory:' needs — see the comment on shim_open
	function open(path, readOnly = true) {
		const pathPointer = stringToPointer(path)
		const handle = Module._shim_open(pathPointer, readOnly ? 1 : 0)
		Module._free(pathPointer)
		if (handle) {
			return handle
		}
		else {
			throw new Error(`duckdb open failed: ${path}`)
		}
	}

	function close(handle) {
		Module._shim_close(handle)
		return null
	}

	function query(handle, sql) {
		const sqlPointer = stringToPointer(sql)
		const result = Module._shim_query(handle, sqlPointer)
		Module._free(sqlPointer)
		const errorPointer = Module._shim_error(result)
		if (errorPointer) {
			const message = Module.UTF8ToString(errorPointer)
			Module._shim_destroy_result(result)
			throw new Error(`duckdb query: ${message}`)
		}
		else {
			try {
				return readRows(result)
			}
			finally {
				Module._shim_destroy_result(result)
			}
		}
	}

	function readRows(result) {
		const columnCount = Module._shim_columns(result)
		const rowCount = Module._shim_rows(result)
		const names = []
		const types = []
		for (let column = 0; column < columnCount; column++) {
			names.push(Module.UTF8ToString(Module._shim_column_name(result, column)))
			types.push(Module._shim_column_type(result, column))
		}
		const rows = []
		for (let row = 0; row < rowCount; row++) {
			const entry = {}
			for (let column = 0; column < columnCount; column++) {
				entry[names[column]] = readCell(result, column, row, types[column])
			}
			rows.push(entry)
		}
		return rows
	}

	function readCell(result, column, row, type) {
		if (Module._shim_is_null(result, column, row)) {
			return null
		}
		else if (type === TYPE_BOOLEAN) {
			return Module._shim_value_boolean(result, column, row) === 1
		}
		else if (INTEGER_TYPES.has(type)) {
			return Number(Module._shim_value_int64(result, column, row))
		}
		else if (FLOAT_TYPES.has(type)) {
			return Module._shim_value_double(result, column, row)
		}
		else {
			const pointer = Module._shim_value_varchar(result, column, row)
			const text = Module.UTF8ToString(pointer)
			Module._shim_free(pointer)
			return text
		}
	}
}
