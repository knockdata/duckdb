// Universal duckdb entry. DuckDB(source) returns
// { query, exec, run, close, getEntries, getEntry }. Node runs the native addon and falls
// back to the wasm; the browser always runs the wasm. Both share the shapes in entries.js.
// A duckdb file has schemas, so listing it is two levels deep:
//   getEntries(source, dbPath, "")        → schema folders
//   getEntries(source, dbPath, "main")    → that schema's tables and views
//   getEntry(source, "main/users", 0, 20) → one page of rows
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

// schema folders of a db, or the tables and views of one schema. Opened fresh per call.
export async function getEntries(source, dbPath, entryPath = '') {
	const db = await DuckDB(source)
	try {
		return await db.getEntries(dbPath, entryPath)
	}
	finally {
		await db.close()
	}
}

// one page of rows from "<schema>/<table>"
export async function getEntry(source, entryPath, offset = 0, limit = 1000) {
	const db = await DuckDB(source)
	try {
		return await db.getEntry(entryPath, offset, limit)
	}
	finally {
		await db.close()
	}
}
