// Universal duckdb entry. DuckDB(source) returns { query, exec, run, close }: SQL in, rows
// out. Node runs the native addon and falls back to the wasm; the browser always runs the
// wasm, and both answer the same SQL the same way.
//
// Listing what is in a database is deliberately not here. "A schema is a folder, a table looks
// like this, here is a page of rows" is an application's shape, not duckdb's, and baking one
// app's answer into the engine package only makes the next app fight it. duckdb_schemas() and
// information_schema.tables are a query away.
//
// Nothing here is Node only, so this entry bundles for the browser: node.js and browser.js
// are both dynamic imports. setEngineDir, addonPath and wasmPath live in engineDir.js and are
// imported from '@knockdata/duckdb/engineDir.js' — re-exporting them here would drag node:fs
// into every browser bundle.

const isNode = typeof process !== 'undefined' && process.versions != null && process.versions.node != null

// source: a file path (Node) or a File/bytes (browser). Opened read only.
export default async function DuckDB(source) {
	if (isNode) {
		const nodeDuckDB = (await import(/* @vite-ignore */ './node.js')).default
		return await nodeDuckDB(source)
	}
	else {
		const browserDuckDB = (await import(/* @vite-ignore */ './browser.js')).default
		return await browserDuckDB(source)
	}
}
