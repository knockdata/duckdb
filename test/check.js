// What both engines must answer for test/sample.duckdb. Shared so the addon and the wasm are
// held to exactly the same results — that equivalence is the whole design.
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

// open is (source) => a handle with query/exec/run/close, whichever engine backs it
export async function checkEngine(open, source) {
	const db = await open(source)
	try {
		const schemas = await db.query(
			'SELECT schema_name FROM duckdb_schemas() WHERE database_name = current_database() ORDER BY schema_name')
		check('schemas', schemas.map(row => row.schema_name), ['main'])

		const tables = await db.query(`SELECT table_name AS name, table_type AS type
			FROM information_schema.tables
			WHERE table_catalog = current_database() AND table_schema = 'main'
			ORDER BY name`)
		check('tables', tables.map(row => row.name), ['users'])

		const columns = await db.query(`SELECT column_name FROM information_schema.columns
			WHERE table_catalog = current_database()
				AND table_schema = 'main' AND table_name = 'users'
			ORDER BY ordinal_position`)
		check('columns', columns.map(row => row.column_name), ['id', 'name', 'email', 'age'])

		const total = Number((await db.query('SELECT COUNT(*) AS total FROM "main"."users"'))[0].total)
		check('total', total, 3)

		const page = await db.query('SELECT * FROM "main"."users" ORDER BY id LIMIT 2 OFFSET 0')
		check('rows', page.map(row => `${row.id}:${row.name}:${row.age}`), ['1:Alice:30', '2:Bob:25'])

		const offset = await db.query('SELECT * FROM "main"."users" ORDER BY id LIMIT 2 OFFSET 2')
		check('offset', offset.map(row => row.name), ['Carol'])

		// the types have to match between the engines, not just the values — comparing
		// stringified rows would let a number and a bigint pass for each other
		const types = (await db.query('SELECT id, name FROM "main"."users" LIMIT 1'))[0]
		check('column types', Object.values(types).map(value => typeof value), ['number', 'string'])
	}
	finally {
		await db.close()
	}
}
