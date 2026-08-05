// The wasm module under Node, reading the db through emscripten's in-memory filesystem.
// node.js prefers the addon, so this drives the wasm directly to be sure it is what ran.
import fs from 'node:fs'
import DuckdbModule from '../wasm/duckdb.js'
import Core from '../wasm/core.js'
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

	// the same handle shape index.js returns, but wired straight to the wasm, so the shared
	// checks exercise this engine rather than whichever one node.js would have picked
	let next = 0
	async function open(source) {
		next = next + 1
		const handle = core.open(core.load(`db-${next}.duckdb`, fs.readFileSync(source)))
		return {
			query: async (sql) => core.query(handle, sql),
			exec: async (sql) => core.query(handle, sql),
			run: async (sql) => core.query(handle, sql),
			close: async () => core.close(handle),
		}
	}

	await checkEngine(open, samplePath)
	check('fixture untouched', fs.existsSync(`${samplePath}.wal`), false)
}
else {
	console.error('FAIL no wasm built — run build.sh wasm')
	process.exitCode = 1
}
