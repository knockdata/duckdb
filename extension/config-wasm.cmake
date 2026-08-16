# Which extensions the wasm engine is built with — a deliberately smaller set than
# config-native.cmake.
#
# The browser gets enough to read the files it is shown: parquet, json, icu collations,
# inet. It does not get the rest, for three separate reasons:
#
#   delta, encodings   upstream excludes both from wasm builds (NOT ${WASM_ENABLED})
#   iceberg, ducklake, excel, avro   they would push the 19 MB wasm past what is
#                      reasonable to fetch into a browser tab
#   objectfs           it talks to our server over a socket, and a wasm build has no
#                      sockets and no local filesystem to hand the bytes back to

duckdb_extension_load(core_functions)
duckdb_extension_load(parquet)
duckdb_extension_load(json)
duckdb_extension_load(icu)

duckdb_extension_load(inet
    GIT_URL https://github.com/duckdb/duckdb-inet
    GIT_TAG fe7f60bb60245197680fb07ecd1629a1dc3d91c8
    INCLUDE_DIR src/include
)
