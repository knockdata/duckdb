// What both engines must produce from test/sample.duckdb. Shared so the addon and the
// wasm are held to exactly the same shapes — that equivalence is the whole design.
import path from 'node:path'
import { fileURLToPath } from 'node:url'

export const samplePath = path.join(path.dirname(fileURLToPath(import.meta.url)), 'sample.duckdb')

export function check(label, actual, wanted) {
	if (JSON.stringify(actual) === JSON.stringify(wanted)) {
		console.log('ok  ', label, JSON.stringify(actual))
	}
	else {
		console.error('FAIL', label, '\n  got  ', JSON.stringify(actual), '\n  want ', JSON.stringify(wanted))
		process.exitCode = 1
	}
}

// getEntries/getEntry against sample.duckdb, whichever engine backs them
export async function checkEngine(getEntries, getEntry, source) {
	const schemas = await getEntries(source, 'sample.duckdb', '')
	check('schemas', schemas.map(row => row.name), ['main'])
	check('schema is a folder', schemas.map(row => row.objectKind), ['folder'])

	const tables = await getEntries(source, 'sample.duckdb', 'main')
	check('tables', tables.map(row => row.name), ['users'])
	check('table path', tables.map(row => row.path), ['sample.duckdb/main/users'])

	const page = await getEntry(source, 'main/users', 0, 2)
	check('columns', page.columns, ['id', 'name', 'email', 'age'])
	check('total', page.total, 3)
	check('rows', page.rows, [
		{ id: 1, name: 'Alice', email: 'alice@example.com', age: 30 },
		{ id: 2, name: 'Bob', email: 'bob@example.com', age: 25 },
	])

	const empty = await getEntries(source, 'sample.duckdb', 'main/users/deeper')
	check('too deep is empty', empty, [])
}
