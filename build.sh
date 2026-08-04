#!/bin/bash
# Builds our own trimmed duckdb, then either the N-API addon or the wasm module.
#
#   bash build.sh          the native addon  -> build/Release/duckdb_napi.node
#   bash build.sh wasm     the wasm module   -> wasm/duckdb.js + wasm/duckdb.wasm
#
# The official @duckdb/node-bindings ships a 117 MB libduckdb.dylib (a fat binary, so
# ~57 MB per arch) with icu+json+parquet linked and every symbol exported. We only ever
# browse a db file, so this build drops parquet, disables extension loading and the
# built-in httplib, turns on duckdb's own SMALLER_BINARY, and hides every symbol except
# the C API: 24 MB native, 19 MB wasm, both self-contained.
#
# Unlike sqlite there is no amalgamation — duckdb is ~1,600 C++17 files and cmake is
# mandatory. Expect 15-40 minutes on a cold build.
#
# TARGET_ARCH cross-builds on macOS, where one Xcode targets both arm64 and x86_64.
set -e
cd "$(dirname "$0")"

# One source of truth for which DuckDB this is: our package version is
# <upstream>-r.<revision>, so 1.5.5-r.1 builds upstream v1.5.5. Bump package.json, not this.
version=v$(node -p 'require("./package.json").version.split("-")[0]')
target=${1:-native}

# A clone left over from an earlier version would silently build the wrong DuckDB, so the
# checkout has to be at the tag we want or it is replaced.
if [ "$(git -C duckdb describe --tags --exact-match HEAD 2>/dev/null)" = "$version" ]; then
	echo "duckdb $version source present"
else
	echo "cloning duckdb $version"
	rm -rf duckdb
	git clone --depth 1 --branch "$version" https://github.com/duckdb/duckdb.git duckdb
fi

# OVERRIDE_GIT_DESCRIBE is needed because a shallow clone has no tag history and duckdb
# would otherwise stamp itself v0.0.1.
common_flags=(
	-DCMAKE_BUILD_TYPE=Release
	-DOVERRIDE_GIT_DESCRIBE="$version"
	-DSMALLER_BINARY=1
	-DSKIP_EXTENSIONS=parquet
	-DDISABLE_EXTENSION_LOAD=1 -DDISABLE_BUILTIN_HTTPLIB=1
	-DBUILD_SHELL=0 -DBUILD_UNITTESTS=0 -DBUILD_BENCHMARKS=0
	-DENABLE_JEMALLOC=0 -DENABLE_SANITIZER=0 -DENABLE_UBSAN=0
)
jobs=$(getconf _NPROCESSORS_ONLN)

# cmake bakes absolute paths into CMakeCache.txt and refuses to run when they no longer point
# here — which happens whenever the checkout is copied, moved, or restored from a CI cache.
# Starting that build directory over is the only fix, and costs nothing when it is not needed.
freshen() {
	if [ -f "$1/CMakeCache.txt" ] && ! grep -qx "CMAKE_HOME_DIRECTORY:INTERNAL=$PWD/duckdb" "$1/CMakeCache.txt"; then
		echo "stale cmake cache in $1, starting it over"
		rm -rf "$1"
	fi
	mkdir -p "$1"
}

if [ "$target" = "wasm" ]; then
	# DUCKDB_NO_THREADS is what upstream's own wasm_eh target uses: single threaded, so no
	# SharedArrayBuffer and no cross-origin isolation needed to run it.
	freshen duckdb/build/wasm
	(cd duckdb/build/wasm && emcmake cmake "${common_flags[@]}" \
		-DDISABLE_THREADS=ON \
		-DCMAKE_CXX_FLAGS="-fwasm-exceptions -DDUCKDB_NO_THREADS=1 -fvisibility=hidden -fvisibility-inlines-hidden" \
		../..)
	cmake --build duckdb/build/wasm --parallel "$jobs" \
		--target duckdb_static core_functions_extension duckdb_generated_extension_loader
	make -C wasm
else
	osx_arch=()
	if [ -n "$TARGET_ARCH" ] && [ "$(uname)" = "Darwin" ]; then
		if [ "$TARGET_ARCH" = "x64" ]; then
			osx_arch=(-DCMAKE_OSX_ARCHITECTURES=x86_64)
		else
			osx_arch=(-DCMAKE_OSX_ARCHITECTURES="$TARGET_ARCH")
		fi
	fi
	freshen duckdb/build/minimal
	(cd duckdb/build/minimal && cmake "${common_flags[@]}" \
		"${osx_arch[@]}" \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
		-DCMAKE_CXX_FLAGS_RELEASE="-DNDEBUG -fvisibility=hidden -fvisibility-inlines-hidden" \
		../..)
	# not "make": on Windows cmake writes a Visual Studio solution, not a Makefile
	cmake --build duckdb/build/minimal --config Release --parallel "$jobs"

	# The addon has to target the same architecture the engine was just built for. Without
	# this, a mac x64 cross build links arm64 object code against x86_64 archives, the linker
	# quietly ignores every archive it cannot use, and the result is a 53 KB .node that links
	# clean (a bundle tolerates undefined symbols) and segfaults on the first call.
	arch_flag=()
	if [ -n "$TARGET_ARCH" ]; then
		arch_flag=(--arch="$TARGET_ARCH")
	fi

	# Stripped, because the archives carry symbol tables we never need.
	npx node-gyp configure build "${arch_flag[@]}"
	if [ "$(uname)" = "Darwin" ] || [ "$(uname)" = "Linux" ]; then
		strip -x build/Release/duckdb_napi.node
	fi
	ls -l build/Release/duckdb_napi.node
fi
