// The wasm module under Node, reading the db through emscripten's in-memory filesystem.
// node.js prefers the addon, so this drives the wasm directly to be sure it is what ran.
import fs from 'node:fs'
import DuckdbModule from '../wasm/duckdb.js'
import Core from '../wasm/core.js'
import entriesApi from '../entries.js'
import { wasmPath } from '../engineDir.js'
import { check, checkEngine, samplePath } from './check.js'

const binary = wasmPath()
if (binary) {
	console.log('wasm:', binary, fs.statSync(binary).size, 'bytes')
	// locateFile as well as wasmBinary: the Makefile rewrites the glue's own
	// new URL("duckdb.wasm", import.meta.url) lookup into a bare relative name so bundlers do
	// not inline 19 MB, which leaves emscripten resolving it against the working directory if
	// it ever falls back to reading from disk. locateFile is the hook it checks first.
	const core = Core(await DuckdbModule({
		wasmBinary: fs.readFileSync(binary),
		locateFile: () => binary,
	}))

	// the shared checks take (source) and open per call, so give them one that reuses the
	// module and just reloads the bytes
	let next = 0
	function open(source) {
		next = next + 1
		const handle = core.open(core.load(`db-${next}.duckdb`, fs.readFileSync(source)))
		const query = async (sql) => core.query(handle, sql)
		return { api: entriesApi(query), close: () => core.close(handle) }
	}
	async function getEntries(source, dbPath, entryPath) {
		const db = open(source)
		try { return await db.api.getEntries(dbPath, entryPath) } finally { db.close() }
	}
	async function getEntry(source, entryPath, offset, limit) {
		const db = open(source)
		try { return await db.api.getEntry(entryPath, offset, limit) } finally { db.close() }
	}

	await checkEngine(getEntries, getEntry, samplePath)
	check('fixture untouched', fs.existsSync(`${samplePath}.wal`), false)
}
else {
	console.error('FAIL no wasm built — run build.sh wasm')
	process.exitCode = 1
}
