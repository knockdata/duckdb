// Node runs whichever of our two engines is on disk.
//
// The N-API addon (napi/duckdb_napi.c) is the fast path: it opens the file in place and
// the trimmed engine is linked straight into it. But it is per-platform and therefore
// built, not published — the npm tarball and the desktop bundle carry only the wasm. So
// a missing addon falls back to the same wasm the browser uses, which keeps duckdb
// working everywhere from one artifact.
//
// Both open READ_ONLY on a single thread, so a user's file is never rewritten.
import { createRequire } from 'node:module'
import { addonPath, wasmPath } from './engineDir.js'

export default async function nodeDuckDB(source, options = {}) {
	const addon = loadAddon()
	if (addon) {
		return await addonDuckDB(addon, source, options)
	}
	else {
		return await wasmDuckDB(source)
	}
}

function loadAddon() {
	const path = addonPath()
	if (path) {
		return createRequire(import.meta.url)(path)
	}
	else {
		return null
	}
}

async function addonDuckDB(addon, source, options = {}) {
	const { path, cleanup } = await sourcePath(source)
	const handle = addon.open(path, options)
	const query = async (sql) => addon.query(handle, sql)

	return {
		query,
		exec: (sql) => addon.query(handle, sql),
		run: (sql) => addon.query(handle, sql),
		close: async () => {
			addon.close(handle)
			await cleanup()
		},
	}
}

// the wasm module reads through emscripten's in-memory filesystem, so the db is copied
// in whole — fine for browsing, and the only option without a platform binary
async function wasmDuckDB(source) {
	const DuckdbModule = (await import(/* @vite-ignore */ './wasm/duckdb.js')).default
	const Core = (await import(/* @vite-ignore */ './wasm/core.js')).default
	const { readFile } = await import(/* @vite-ignore */ 'node:fs/promises')

	const binary = wasmPath()
	if (binary) {
		// locateFile as well as wasmBinary — build.sh rewrites the glue's own wasm lookup into a
		// bare relative name (so bundlers do not inline 19 MB), which resolves against the
		// working directory if emscripten ever falls back to reading from disk
		const core = Core(await DuckdbModule({
			wasmBinary: await readFile(binary),
			locateFile: () => binary,
		}))
		const bytes = typeof source === 'string' ? await readFile(source) : await toArrayBuffer(source)
		const handle = core.open(core.load(`db-${Date.now()}.duckdb`, bytes))
		const query = async (sql) => core.query(handle, sql)

		return {
			query,
			exec: (sql) => core.query(handle, sql),
			run: (sql) => core.query(handle, sql),
			close: async () => core.close(handle),
			}
	}
	else {
		throw new Error('DuckDB engine missing: run @knockdata/duckdb/build.sh')
	}
}

// a real path opens in place; bytes (a db nested inside an archive) go to a temp file
async function sourcePath(source) {
	if (typeof source === 'string') {
		return { path: source, cleanup: async () => { } }
	}
	else {
		const { join } = await import(/* @vite-ignore */ 'node:path')
		const { tmpdir } = await import(/* @vite-ignore */ 'node:os')
		const { writeFile, unlink } = await import(/* @vite-ignore */ 'node:fs/promises')
		const path = join(tmpdir(), `duckdb-${Date.now()}-${Math.random().toString(36).slice(2)}.duckdb`)
		await writeFile(path, new Uint8Array(await toArrayBuffer(source)))
		return { path, cleanup: async () => await unlink(path) }
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
