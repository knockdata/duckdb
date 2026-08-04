BUILD = ../duckdb/build/wasm
LIBS = \
    $(BUILD)/extension/libduckdb_generated_extension_loader.a \
    $(BUILD)/extension/core_functions/libcore_functions_extension.a \
    $(BUILD)/src/libduckdb_static.a

# duckdb registers types and functions from static initializers spread across the
# archives, so every object has to be linked in, not just the referenced ones.
WHOLE = -Wl,--whole-archive $(LIBS) -Wl,--no-whole-archive

CFLAGS = -O2 -fwasm-exceptions -I ../duckdb/src/include

EXPORTS = _shim_open,_shim_close,_shim_query,_shim_error,_shim_destroy_result,_shim_columns,_shim_rows,_shim_column_name,_shim_column_type,_shim_is_null,_shim_value_double,_shim_value_int64,_shim_value_boolean,_shim_value_varchar,_shim_free,_malloc,_free

RUNTIME = FS,UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPU8

# no SINGLE_FILE: base64-inlining a 20 MB wasm would make the JS unparseable. The
# .wasm is fetched beside the .js, which is what the /duckdb/ route serves.
EM_FLAGS = \
    -O2 \
    -fwasm-exceptions \
    -sWASM=1 \
    -sWASM_BIGINT=1 \
    -sALLOW_MEMORY_GROWTH=1 \
    -sMAXIMUM_MEMORY=4GB \
    -sMODULARIZE=1 \
    -sEXPORT_ES6=1 \
    -sEXPORT_NAME=DuckdbModule \
    -sNO_EXIT_RUNTIME=1 \
    -sENVIRONMENT=web,worker,node \
    -sEXPORTED_FUNCTIONS=[$(EXPORTS)] \
    -sEXPORTED_RUNTIME_METHODS=[$(RUNTIME)]

.PHONY: all clean

all: duckdb.js

duckdb.js: shim.c $(LIBS)
	emcc $(CFLAGS) $(EM_FLAGS) shim.c $(WHOLE) -o duckdb.js
	@sed -i.bak -E 's|import\((["'\''][^"'\'']+["'\''])\)|import(/* @vite-ignore */ (\1))|g' duckdb.js
	@sed -i.bak -E 's|require\((["'\''][^"'\'']+["'\''])\)|require(/* @vite-ignore */ (\1))|g' duckdb.js
	# the glue locates duckdb.wasm beside itself, which makes a bundler inline all 19 MB
	# into the app. Every caller passes wasmBinary, so the reference is dead — drop it.
	@sed -i.bak -E 's|new URL\("duckdb.wasm",import.meta.url\).href|"duckdb.wasm"|g' duckdb.js
	@rm -f duckdb.js.bak
	@echo "built duckdb.js ($$(du -h duckdb.js | cut -f1)) + duckdb.wasm ($$(du -h duckdb.wasm | cut -f1))"

clean:
	rm -f duckdb.js duckdb.js.bak duckdb.wasm
