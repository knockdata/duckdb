// Schema/table/row shapes built on one query function — the same SQL runs on the
// native binding (Node) and on duckdb-wasm (browser), so both forks share this.
// duckdb has schemas, so a db lists schema folders and a schema lists its tables.

// "name" with any embedded quote doubled, for an identifier
function quote(name) {
	return `"${String(name).replaceAll('"', '""')}"`
}

// 'text' with any embedded quote doubled, for a string literal
function literal(text) {
	return `'${String(text).replaceAll("'", "''")}'`
}

// "<schema>/<table>", or a bare table name that defaults to the main schema
function splitTable(entryPath) {
	const segments = String(entryPath || '').split('/').filter(Boolean)
	if (segments.length > 1) {
		return { schema: segments[0], table: segments[1] }
	}
	else {
		return { schema: 'main', table: segments[0] }
	}
}

// bigints aren't JSON-friendly; downcast to Number
function normalizeRow(row) {
	const normalized = {}
	for (const key of Object.keys(row)) {
		const value = row[key]
		if (typeof value === 'bigint') {
			normalized[key] = Number(value)
		}
		else {
			normalized[key] = value
		}
	}
	return normalized
}

export default function entriesApi(queryFn) {
	return { getEntries, getEntry }

	// entryPath '' lists the schema folders, a schema name lists its tables and views
	async function getEntries(dbPath, entryPath = '') {
		const segments = String(entryPath || '').split('/').filter(Boolean)
		if (segments.length === 0) {
			return await getSchemas(dbPath)
		}
		else if (segments.length === 1) {
			return await getTables(dbPath, segments[0])
		}
		else {
			return []
		}
	}

	// every schema of the opened db. "internal" is true even for the user's own main
	// schema, so the only filter that works is the database name.
	async function getSchemas(dbPath) {
		const rows = await queryFn(
			'SELECT schema_name FROM duckdb_schemas() WHERE database_name = current_database() ORDER BY schema_name')
		return rows.map(row => ({
			name: row.schema_name,
			label: row.schema_name,
			type: 'schema',
			objectKind: 'folder',
			path: `${dbPath}/${row.schema_name}`,
		}))
	}

	// tables and views of one schema, in a single round trip
	async function getTables(dbPath, schema) {
		const rows = await queryFn(`SELECT table_name AS name,
				CASE WHEN table_type = 'VIEW' THEN 'view' ELSE 'table' END AS type
			FROM information_schema.tables
			WHERE table_catalog = current_database() AND table_schema = ${literal(schema)}
			ORDER BY type, name`)
		return rows.map(row => ({
			name: row.name,
			label: row.name,
			type: row.type,
			objectKind: 'table',
			path: `${dbPath}/${schema}/${row.name}`,
		}))
	}

	// one page of rows from "<schema>/<table>"
	async function getEntry(entryPath, offset = 0, limit = 1000) {
		const { schema, table } = splitTable(entryPath)
		const fullName = `${quote(schema)}.${quote(table)}`
		const columnRows = await queryFn(`SELECT column_name FROM information_schema.columns
			WHERE table_catalog = current_database()
				AND table_schema = ${literal(schema)} AND table_name = ${literal(table)}
			ORDER BY ordinal_position`)
		const columns = columnRows.map(row => row.column_name)
		const total = Number((await queryFn(`SELECT COUNT(*) AS total FROM ${fullName}`))[0].total)
		const raw = await queryFn(`SELECT * FROM ${fullName} LIMIT ${limit} OFFSET ${offset}`)
		return { rows: raw.map(normalizeRow), columns, total, offset, limit }
	}
}
