# Which extensions the native engine is built with.
#
# build.sh passes this file as -DDUCKDB_EXTENSION_CONFIGS, which is the same variable
# duckdb's own Makefile sets; every extension named here is compiled and STATICALLY
# linked, because the build keeps DISABLE_EXTENSION_LOAD=1 and can never load one at
# runtime. Nothing here reaches the network on its own — httpfs, aws and azure are
# deliberately absent, and objectfs answers every cloud URI through our own web server.
#
# The GIT_TAGs are copied verbatim from duckdb/.github/config/extensions/<name>.cmake at
# v1.5.5, so each out-of-tree extension is the exact commit upstream tests this duckdb
# against. Bumping duckdb means recopying them.

# in-tree
duckdb_extension_load(core_functions)
duckdb_extension_load(parquet)
duckdb_extension_load(json)
duckdb_extension_load(icu)

# out-of-tree
duckdb_extension_load(inet
    GIT_URL https://github.com/duckdb/duckdb-inet
    GIT_TAG fe7f60bb60245197680fb07ecd1629a1dc3d91c8
    INCLUDE_DIR src/include
)

# encodings is deliberately absent. It reads CSVs written in legacy code pages, and it
# carries a conversion table for every one of them: a 452 MB static archive, against ~42 MB
# for all the other extensions put together. Whole-archive linking it roughly doubled the
# addon. Add it back only if a real file needs a code page utf-8 cannot express.

duckdb_extension_load(excel
    GIT_URL https://github.com/duckdb/duckdb-excel
    GIT_TAG f4c72b5ef04a03b3a78a95b5a2ee94ba93e3178d
    INCLUDE_DIR src/excel/include
)

# avro reads .avro files, and it is also how an Iceberg table gets read at all: its manifests
# are avro, so read_avro is what the explorer's own iceberg reader parses them with.
duckdb_extension_load(avro
    GIT_URL https://github.com/duckdb/duckdb-avro
    GIT_TAG f9d590297485f0318f480372c70bdd852826e258
)

# iceberg is deliberately absent, for the same reason as delta. find_package(AWSSDK REQUIRED) in
# duckdb-iceberg is unconditional, so the extension drags in the whole AWS SDK, curl and openssl —
# ~50 MB of archives, the longest pole in every CI build, and nine windows import libraries — all
# to support REST, Glue and S3Tables catalogs that this app never asks for. Our tables arrive as a
# path through objectfs.
#
# What is left after those catalogs is metadata.json -> manifest list -> manifests -> data files,
# and duckdb reads avro. So rock2/server/src/duckdb/iceberg-*.js walks that chain and rewrites the
# query into read_parquet(schema=...) over the live files, position deletes, equality deletes and
# v3 puffin deletion vectors included. rock2/server/test/iceberg-golden.json holds this engine's
# old answers, to check that it still reads them the same.

duckdb_extension_load(ducklake
    GIT_URL https://github.com/duckdb/ducklake
    GIT_TAG d8a1881e22516ea3d186d73e83c65fe5bd1a1dc4
)

# delta is deliberately absent. It is a C++ shell over delta-kernel-rs, so it needs a rust
# toolchain, a 4.3 GB build tree and a 103 MB static archive — and through rustls it pulls in
# aws-lc-sys, a BoringSSL fork built by cmake that never once compiled on windows arm64.
#
# All of that to answer one question: which parquet files in the folder does the table consist
# of? _delta_log answers it in json, so the explorer's own server replays it and rewrites the
# query into a read_parquet over the live files — see rock2/server/src/duckdb/delta-log.js.
# Partition values, column mapping and deletion vectors are handled there too, and
# rock2/server/test/delta.test.js holds this engine's old answers to check that against.

# ours: s3:// gs:// az:// resolved through the explorer's own connectors
duckdb_extension_load(objectfs SOURCE_DIR ${OBJECTFS_DIR})
