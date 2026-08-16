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

# iceberg reads avro manifests, so avro comes along whether or not it is asked for
duckdb_extension_load(avro
    GIT_URL https://github.com/duckdb/duckdb-avro
    GIT_TAG f9d590297485f0318f480372c70bdd852826e258
)

duckdb_extension_load(iceberg
    GIT_URL https://github.com/duckdb/duckdb-iceberg
    GIT_TAG 45163a28e0ed6a2071a82a1bf1dd432d0216cf9c
)

duckdb_extension_load(ducklake
    GIT_URL https://github.com/duckdb/ducklake
    GIT_TAG d8a1881e22516ea3d186d73e83c65fe5bd1a1dc4
)

# delta builds delta-kernel-rs, so this one needs a rust toolchain on the machine
duckdb_extension_load(delta
    GIT_URL https://github.com/duckdb/duckdb-delta
    GIT_TAG 45c40878601b54b4188b09e08732fe0d576ad222
    SUBMODULES extension-ci-tools
)

# ours: s3:// gs:// az:// resolved through the explorer's own connectors
duckdb_extension_load(objectfs SOURCE_DIR ${OBJECTFS_DIR})
