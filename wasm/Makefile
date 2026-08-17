BUILD = ../duckdb/build/wasm

# One archive per linked extension, in link order, read out of the build tree — see the
# comment in scripts/archives.mjs for why this is generated rather than written out.
#
# build.sh passes it in; this Makefile does not run node itself. $(shell node ...) died on
# the CI runner with "make: node: Permission denied": emsdk puts its own root on PATH, that
# root holds a *directory* called node/, and make execs a simple command itself rather than
# through a shell — its PATH search doesn't skip directories, so it tried to execute one. A
# shell skips it and finds the real node further along PATH. The link then succeeded with an
# empty --whole-archive and every duckdb symbol undefined, hence the check below: an empty
# LIBS is always a broken build, never a build with nothing to link.
LIBS ?=

ifeq ($(strip $(LIBS)),)
$(error no archives to link — run `bash build.sh wasm`, which passes LIBS in)
endif

# duckdb registers types and functions from static initializers spread across the
# archives, so every object has to be linked in, not just the referenced ones.
WHOLE = -Wl,--whole-archive $(LIBS) -Wl,--no-whole-archive

CFLAGS = -O2 -fwasm-exceptions -I ../duckdb/src/include

EXPORTS = _shim_open,_shim_close,_shim_query,_shim_error,_shim_destroy_result,_shim_columns,_shim_rows,_shim_column_name,_shim_column_type,_shim_is_null,_shim_value_double,_shim_value_int64,_shim_value_boolean,_shim_value_varchar,_shim_free,_malloc,_free

RUNTIME = FS,UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPU8

# no SINGLE_FILE: base64-inlining a 20 MB wasm would make the JS unparseable. The
# .wasm is fetched beside the .js, which is what the /duckdb/ route serves.
# shim.c is C, but everything it links against — duckdb and its extensions — is C++.
# emscripten 5 worked that out on its own; 6 links a .c entry point as C by default and
# fails on every C++ symbol in the archives, so the C++ runtime is asked for explicitly.
# emcc rather than em++ still compiles shim.c as C, which is what it is.
EM_FLAGS = \
    -O2 \
    -sDEFAULT_TO_CXX \
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
