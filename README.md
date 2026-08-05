# @knockdata/duckdb

A minimal DuckDB built for **browsing database files** — list the schemas, list the tables,
read a page of rows. One source produces both a native N-API addon and a wasm module, so the
same three functions work in Node and in the browser.

```
native addon   24 MB   (official @duckdb/node-bindings: ~57 MB per arch)
wasm           19 MB   (official @duckdb/duckdb-wasm:    34 MB)
```

## Install

```bash
npm install @knockdata/duckdb
```

npm brings the addon for your machine along with it. Each platform's binary is its own
package (`@knockdata/duckdb-darwin-arm64`, `-linux-x64`, `-win32-x64`, …) listed under
`optionalDependencies` with `os` and `cpu` fields, so the resolver downloads exactly one and
skips the rest. Nothing is fetched by an install script, and everything is covered by your
lockfile. If no binary matches your platform, the wasm runs instead.

## Use

```js
import DuckDB from '@knockdata/duckdb'

const db = await DuckDB('sales.duckdb')            // a path in Node, a File or bytes in the browser

await db.query('SELECT id, name FROM main.users LIMIT 2')
// → [ { id: 1, name: 'Alice' }, { id: 2, name: 'Bob' } ]

await db.close()
```

`query(sql)` returns rows and `close()` releases the file. **The file is opened READ_ONLY on a
single thread**, so it is never rewritten and no `.wal` is left beside it — the point of a viewer.

Integers and floats come back as JavaScript numbers, booleans as booleans, and everything
DuckDB has that JavaScript does not — dates, timestamps, decimals, hugeints, lists, structs —
as the string DuckDB itself would print.

### Listing what is in a database

There is no `getEntries` here, on purpose. "A schema is a folder, a table looks like that, here
is a page of rows" is an application's shape, not DuckDB's — bake one app's answer into the
engine package and the next app spends its time fighting it. Ask DuckDB directly:

```js
await db.query(`SELECT schema_name FROM duckdb_schemas()
  WHERE database_name = current_database() ORDER BY schema_name`)

await db.query(`SELECT table_name, table_type FROM information_schema.tables
  WHERE table_catalog = current_database() AND table_schema = 'main' ORDER BY table_name`)

await db.query('SELECT * FROM "main"."users" LIMIT 100 OFFSET 0')
```

Identifiers cannot be bound as parameters, so a schema or table name is interpolated — double
any embedded quote (`name.replaceAll('"', '""')`) before you do.

### In the browser

The wasm binary is 19 MB, so this package does not decide how you get it — serve it, cache it,
however suits your app — you just hand over the bytes once:

```js
import { setWasmBinary } from '@knockdata/duckdb/browser.js'

setWasmBinary(await (await fetch('/duckdb/duckdb.wasm')).arrayBuffer())
```

The file to serve is `node_modules/@knockdata/duckdb/wasm/duckdb.wasm`. The ~70 KB of glue
next to it bundles with your app normally. The wasm build is single-threaded, so it needs no
`SharedArrayBuffer` and no COOP/COEP headers.

### In a single-file bundle

An SEA or an esbuild bundle has no `node_modules` to resolve against. Unpack the two files
wherever you like and point the package at them:

```js
import { setEngineDir } from '@knockdata/duckdb/engineDir.js'

setEngineDir('/somewhere/duckdb')   // holding duckdb_napi.node and wasm/duckdb.wasm
```

`engineDir.js` is a separate entry, not part of the main one, because it reads the filesystem —
importing it from the root would pull `node:fs` into every browser bundle. `wasmPath()` from the
same module answers where the wasm is, which is what a server serving `/duckdb/duckdb.wasm` needs.

## Why this exists

The official binding is built to be a complete analytical database. `libduckdb.dylib` is
117 MB — and that is a *fat* binary, so about 57 MB for one architecture — with icu, json and
parquet linked in, extension loading and an HTTP client compiled in, and **35,088 exported
symbols**. The addon on top of it is only 425 KB; effectively all of the weight is the engine.

For a file browser none of that is needed. Rebuilding upstream v1.5.5 with parquet skipped,
extension loading and the built-in httplib disabled, no shell or tests, DuckDB's own
`SMALLER_BINARY` (which trims template specialisation, the real cause of a 33 MB `__text`),
and `-fvisibility=hidden` gives:

| | official | this |
|---------------------|---------|--------|
| native, per arch | ~57 MB | **24 MB** |
| wasm | 34 MB | **19 MB** |
| exported symbols | 35,088 | **249** |

The native artifact is a single self-contained `.node` with the engine linked in — no `.dylib`
to ship beside it.

The API is the other half of the point. Browsing a db file through the official client means
instances, connections, readers and materialised results; here it is two functions that return
plain objects, with the same shapes from both engines.

## Build it yourself

```bash
bash build.sh          # native addon → build/Release/duckdb_napi.node
bash build.sh wasm     # wasm module  → wasm/duckdb.js + wasm/duckdb.wasm
```

`build.sh` shallow-clones upstream DuckDB at the pinned tag (one `version=` line at the top of
the script), so nothing is vendored here. A cold build is 15–40 minutes: DuckDB has no
amalgamation, it is ~1,600 C++17 translation units and cmake is required. The wasm build also
needs `emcc` on your `PATH`.

What is actually ours is small: `napi/duckdb_napi.c` and `wasm/shim.c` are two thin C surfaces
over DuckDB's C API, using about fifteen of its 548 functions, plus the JS that picks an engine
and hands back a handle.

## Versioning

`<upstream duckdb version>-r.<revision>` — `1.5.5-r.1` is upstream **v1.5.5**, our first build
of it. A new revision means we changed the build or the JS; a new base version means DuckDB
moved. Pin exactly.

## License

MIT for the code in this repository. DuckDB itself is MIT, © DuckDB Labs and contributors.
