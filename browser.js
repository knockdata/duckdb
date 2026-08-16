// Browser: the wasm build (wasm/duckdb.js + wasm/duckdb.wasm). The ~70 KB glue bundles
// with the app; the wasm binary is 19 MB, so the embedder decides how to get hold of it —
// fetch it, cache it, read it from disk — and hands the bytes to setWasmBinary once. That
// keeps the route and the cache, which are application concerns, out of this package.
//
// The module is a singleton: callers open and close a db per query, but closing only drops
// the db handle, never the 19 MB module.
import DuckdbModule from './wasm/duckdb.js'
import Core from './wasm/core.js'

let wasmBinary = null
let core = null
let nextName = 0
const loadedNames = new WeakMap()

// call once, before the first open, with the contents of wasm/duckdb.wasm
export function setWasmBinary(bytes) {
	wasmBinary = bytes
}

// source is a File or bytes to open as a database, or the string ':memory:' for a database
// with nothing in it — which is what querying a parquet or a csv wants. Those are not
// databases, so there is nothing to open: stage the bytes with load() and name them in the
// SQL. Opened read only unless options.readOnly is false, and ':memory:' requires that.
export default async function browserDuckDB(source, options = {}) {
	const duckdb = await getCore()
	const memory = source === ':memory:'
	const path = memory ? ':memory:' : await loadSource(duckdb, source)
	const handle = duckdb.open(path, options.readOnly ?? !memory)
	const query = async (sql) => duckdb.query(handle, sql)

	return {
		query,
		exec: (sql) => duckdb.query(handle, sql),
		run: (sql) => duckdb.query(handle, sql),
		// put a file where the SQL can name it: load('sales.parquet', bytes) makes
		// SELECT * FROM 'sales.parquet' work against this handle
		load: async (name, bytes) => duckdb.load(name, await toArrayBuffer(bytes)),
		close: () => duckdb.close(handle),
	}
}

async function getCore() {
	if (core === null) {
		if (wasmBinary) {
			// instantiateWasm, not the `wasmBinary` module option: emscripten 6 stopped
			// reading Module["wasmBinary"], and without this the glue quietly fetches
			// duckdb.wasm beside itself — downloading 27 MB a second time, or failing when
			// that URL is not served. This hook hands it the bytes the embedder already has.
			core = Core(await DuckdbModule({
				instantiateWasm(imports, done) {
					WebAssembly.instantiate(wasmBinary, imports)
						.then(result => done(result.instance, result.module))
					return {}
				},
			}))
		}
		else {
			throw new Error('duckdb wasm not loaded: call setWasmBinary(bytes) with wasm/duckdb.wasm')
		}
	}
	return core
}

// a File is written into the wasm filesystem once and reused, so paging a table
// never re-uploads the db
async function loadSource(duckdb, source) {
	if (loadedNames.has(source)) {
		return loadedNames.get(source)
	}
	else {
		nextName = nextName + 1
		const name = `db-${nextName}.duckdb`
		const path = duckdb.load(name, await toArrayBuffer(source))
		loadedNames.set(source, path)
		return path
	}
}

async function toArrayBuffer(source) {
	if (source instanceof ArrayBuffer) {
		return source
	}
	else if (source.arrayBuffer) {
		return await source.arrayBuffer()
	}
	else {
		return source.buffer.slice(source.byteOffset, source.byteOffset + source.byteLength)
	}
}
