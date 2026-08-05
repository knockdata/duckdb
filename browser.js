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

export default async function browserDuckDB(source) {
	const duckdb = await getCore()
	const path = await loadSource(duckdb, source)
	const handle = duckdb.open(path)
	const query = async (sql) => duckdb.query(handle, sql)

	return {
		query,
		exec: (sql) => duckdb.query(handle, sql),
		run: (sql) => duckdb.query(handle, sql),
		close: () => duckdb.close(handle),
	}
}

async function getCore() {
	if (core === null) {
		if (wasmBinary) {
			core = Core(await DuckdbModule({ wasmBinary }))
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
