# @knockdata/duckdb

DuckDB built for **querying data files** — a database file, a parquet, a csv, an iceberg or
ducklake table, local or in a bucket. One source produces both a native N-API addon and a wasm
module, so the same `query(sql)` works in Node and in the browser.

```
native addon   87 MB   11 extensions linked in statically
wasm           26 MB   5 extensions, single threaded
glue           72 KB   wasm/duckdb.js, bundles with your app
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

`query(sql)` returns rows and `close()` releases the file. `exec` and `run` are the same
function under other names, for callers that already speak those. **A database file is opened
READ_ONLY on a single thread**, so it is never rewritten and no `.wal` is left beside it.

Integers and floats come back as JavaScript numbers, booleans as booleans, and everything
DuckDB has that JavaScript does not — dates, timestamps, decimals, hugeints, lists, structs —
as the string DuckDB itself would print.

### Querying files rather than opening one

A parquet or a csv is not a database, so there is nothing to open: start an in-memory database
and name the file in the SQL. That needs writing, because reading a file puts temporary state
somewhere, and READ_ONLY refuses to open an in-memory database at all:

```js
const db = await DuckDB(':memory:', { readOnly: false })
await db.query("SELECT * FROM 'sales.parquet' LIMIT 100")
```

`readOnly` defaults to true for a path and false for `':memory:'`.

In the browser there is no filesystem to read from, so hand the bytes over first and query them
by the name you gave them:

```js
await db.load('sales.parquet', bytes)
await db.query("SELECT * FROM 'sales.parquet' LIMIT 100")
```

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

The wasm binary is 26 MB, so this package does not decide how you get it — serve it, cache it,
however suits your app — you just hand over the bytes once:

```js
import { setWasmBinary } from '@knockdata/duckdb/browser.js'

setWasmBinary(await (await fetch('/duckdb/duckdb.wasm')).arrayBuffer())
```

The file to serve is `node_modules/@knockdata/duckdb/wasm/duckdb.wasm`. The 72 KB of glue
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

## What is linked in

Extension loading is compiled out (`DISABLE_EXTENSION_LOAD=1`), so the engine can never fetch or
`LOAD` anything at runtime: what is in the binary is all there will ever be. The two sets live in
`extension/config-native.cmake` and `extension/config-wasm.cmake`.

| | native | wasm |
|---------------------------------------------|--------|------|
| core_functions, parquet, json, icu, inet | yes | yes |
| excel, avro, iceberg, ducklake, delta | yes | — |
| objectfs (ours) | yes | — |

The wasm set stops there for three separate reasons: upstream excludes `delta` and `encodings`
from wasm builds outright, the table formats would push a browser download well past 26 MB, and
`objectfs` needs a socket that a wasm build does not have.

`encodings` is absent from both. It reads CSVs in legacy code pages and carries a conversion
table for each one — a 452 MB static archive against ~42 MB for every other extension put
together, which roughly doubled the addon.

**Nothing here reaches the network on its own.** `httpfs`, `aws` and `azure` are absent and the
built-in httplib is compiled out (`DISABLE_BUILTIN_HTTPLIB=1`). Instead our own `objectfs`
extension claims `s3://`, `gs://`, `gcs://`, `az://`, `abfs://` and `abfss://` and answers them
by asking the explorer's web server, which already holds the sign-in and the cache. It talks to
`127.0.0.1:8080` unless the `objectfs_server` setting or the `OBJECTFS_SERVER` environment
variable says otherwise.

## Why this exists

The official `@duckdb/node-bindings` ships a 117 MB `libduckdb.dylib` — a *fat* binary, so about
57 MB for one architecture — beside a 425 KB addon; effectively all of the weight is the engine,
and it comes with extension loading and an HTTP client compiled in. Our build is the same engine
rebuilt at the pinned upstream tag with the extension set above, DuckDB's own `SMALLER_BINARY`,
`-fvisibility=hidden`, no shell and no tests. The native artifact is a single self-contained
`.node` with the engine and all eleven extensions linked in — no `.dylib` to ship beside it,
nothing downloaded on first use, and no code path that could load an extension we did not build.

The API is the other half of the point. Reading a file through the official client means
instances, connections, readers and materialised results; here it is `query` and `close`,
returning plain objects, with the same shapes from both engines.

## Build it yourself

```bash
bash build.sh          # native addon → build/Release/duckdb_napi.node
bash build.sh wasm     # wasm module  → wasm/duckdb.js + wasm/duckdb.wasm
```

`build.sh` shallow-clones upstream DuckDB at the tag matching this package's version (there is
no `version=` to edit — it reads `package.json`), so nothing is vendored here. DuckDB has no
amalgamation: it is ~1,600 C++17 translation units, cmake is required, and with this extension
set a cold build takes well over an hour.

Three things have to be on the machine: **cmake**, a **rust toolchain** (`delta` builds
delta-kernel-rs), and **emcc** for the wasm target. vcpkg is bootstrapped by the script itself if
`VCPKG_TOOLCHAIN_PATH` is unset — `iceberg`, `avro` and `excel` declare vcpkg dependencies, and
the script runs DuckDB's own configure pass first to merge their manifests into one.
`TARGET_ARCH` cross-builds on macOS, where one Xcode targets both architectures.

What is actually ours is small: `napi/duckdb_napi.c` and `wasm/shim.c` are two thin C surfaces
over DuckDB's C API, `extension/objectfs` is the cloud filesystem, plus the JS that picks an
engine and hands back a handle.

## Releasing

`./release.sh` tags the version already in `package.json` and pushes the tag;
`.github/workflows/release.yml` does the rest — six native legs each publish their own platform
package, and a seventh builds the wasm and publishes the universal package last, because that
one names the platform versions in its `optionalDependencies`. A rerun skips whatever is already
on npm, so a half-published release can be finished without burning a revision.

## Versioning

`<upstream duckdb version>-r.<revision>` — `1.5.5-r.1` is upstream **v1.5.5**, our first build
of it. A new revision means we changed the build or the JS; a new base version means DuckDB
moved. Pin exactly.

## License

MIT for the code in this repository. DuckDB itself is MIT, © DuckDB Labs and contributors.
