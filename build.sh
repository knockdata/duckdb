#!/bin/bash
# Builds our own duckdb, then either the N-API addon or the wasm module.
#
#   bash build.sh          the native addon  -> build/Release/duckdb_napi.node
#   bash build.sh wasm     the wasm module   -> wasm/duckdb.js + wasm/duckdb.wasm
#
# The official @duckdb/node-bindings ships a 117 MB libduckdb.dylib (a fat binary, so
# ~57 MB per arch) with every symbol exported. We build the same engine with the
# extensions we actually want linked in statically, extension loading turned off, the
# built-in httplib gone, and every symbol hidden except the C API.
#
# Which extensions those are is in extension/config-native.cmake and
# extension/config-wasm.cmake, not here. None of them reach the network on their own:
# httpfs, aws and azure are absent, and our own objectfs extension answers s3:// gs://
# az:// by asking the explorer's web server, which already holds the sign-in and the cache.
#
# Unlike sqlite there is no amalgamation — duckdb is ~1,600 C++17 files and cmake is
# mandatory. With this extension set expect well over an hour on a cold build.
#
# TARGET_ARCH cross-builds on macOS, where one Xcode targets both arm64 and x86_64.
set -e
cd "$(dirname "$0")"
here=$PWD

# One source of truth for which DuckDB this is: our package version is
# <upstream>-r.<revision>, so 1.5.5-r.1 builds upstream v1.5.5. Bump package.json, not this.
version=v$(node -p 'require("./package.json").version.split("-")[0]')
target=${1:-native}

# Windows refuses a path over 260 characters unless git is told to use the long-path API, and
# iceberg's test data ("data/persistent/partition_timestamptz/.../partition_col=2023-05-15T14%3A
# 30%3A45%2B00%3A00/00000-0-....parquet") is well past that once it sits under
# build/extension_configuration/_deps/iceberg_extension_fc-src. cmake does that clone, not us, so
# the setting has to reach a git we never call: GIT_CONFIG_* is inherited by every child process,
# unlike `git config --global`, which would write to the machine running the build.
#
# The test is $OS rather than `uname`, which names the bash flavour: Git Bash is MINGW64 on
# x64 and CLANGARM64 on arm, and the arm runner is one of the two that failed.
if [ "$OS" = "Windows_NT" ]; then
	export GIT_CONFIG_COUNT=1
	export GIT_CONFIG_KEY_0=core.longpaths
	export GIT_CONFIG_VALUE_0=true
fi

# A clone left over from an earlier version would silently build the wrong DuckDB, so the
# checkout has to be at the tag we want or it is replaced.
if [ "$(git -C duckdb describe --tags --exact-match HEAD 2>/dev/null)" = "$version" ]; then
	echo "duckdb $version source present"
else
	echo "cloning duckdb $version"
	rm -rf duckdb
	git clone --depth 1 --branch "$version" https://github.com/duckdb/duckdb.git duckdb
fi

# iceberg, avro and excel declare vcpkg dependencies. cmake can only install them when a
# vcpkg toolchain is on hand, so bring one if the machine has none. The tag is the one
# duckdb's own Makefile pins for this release.
setup_vcpkg() {
	if [ -z "$VCPKG_TOOLCHAIN_PATH" ]; then
		if [ ! -f "$here/vcpkg/scripts/buildsystems/vcpkg.cmake" ]; then
			echo "bootstrapping vcpkg"
			rm -rf "$here/vcpkg"
			# not a shallow clone: vcpkg checks a pinned port out of its own git history by
			# tree hash, and a shallow clone does not have the object to unpack
			git clone --branch 2025.12.12 https://github.com/microsoft/vcpkg "$here/vcpkg"
			"$here/vcpkg/bootstrap-vcpkg.sh"
		fi
		export VCPKG_TOOLCHAIN_PATH="$here/vcpkg/scripts/buildsystems/vcpkg.cmake"
	fi
	echo "vcpkg toolchain: $VCPKG_TOOLCHAIN_PATH"
}

# Several extensions carry their own vcpkg.json. Building more than one of them needs a
# single merged manifest, which duckdb generates from a throwaway configure pass with
# EXTENSION_CONFIG_BUILD=TRUE. That pass writes build/extension_configuration/vcpkg.json,
# and the real configure below points VCPKG_MANIFEST_DIR at it.
merge_vcpkg_manifest() {
	freshen duckdb/build/extension_configuration
	(cd duckdb/build/extension_configuration && cmake \
		-DDUCKDB_EXTENSION_CONFIGS="$config" \
		-DOBJECTFS_DIR="$here/extension/objectfs" \
		-DEXTENSION_CONFIG_BUILD=TRUE -DVCPKG_BUILD=1 -DCMAKE_BUILD_TYPE=Debug ../..)
	cmake --build duckdb/build/extension_configuration
}

common_flags=(
	-DCMAKE_BUILD_TYPE=Release
	-DOVERRIDE_GIT_DESCRIBE="$version"
	-DSMALLER_BINARY=1
	# a guard, not a selection: an out-of-tree extension's own config can pull one of
	# these in as a dependency, and none of them may end up in our engine
	-DSKIP_EXTENSIONS="autocomplete;aws;httpfs;azure;tpch;tpcds;fts;spatial"
	-DDISABLE_EXTENSION_LOAD=1 -DDISABLE_BUILTIN_HTTPLIB=1
	-DBUILD_SHELL=0 -DBUILD_UNITTESTS=0 -DBUILD_BENCHMARKS=0
	-DENABLE_JEMALLOC=0 -DENABLE_SANITIZER=0 -DENABLE_UBSAN=0
	-DOBJECTFS_DIR="$here/extension/objectfs"
)
jobs=$(getconf _NPROCESSORS_ONLN)

# Two things make an existing build directory unusable, and starting it over is the only
# fix for either.
#
# cmake bakes absolute paths into CMakeCache.txt and refuses to run when they no longer
# point here — which happens whenever the checkout is copied, moved, or restored from a
# CI cache.
#
# And a cache outlives a change to the extension set in a way that looks like it worked:
# vcpkg installs the merged manifest's dependencies on a build tree's FIRST configure and
# never again, so reusing a tree from before an extension was added reconfigures happily
# and then fails much later on a find_package for a library nothing ever fetched.
freshen() {
	local cache=$1/CMakeCache.txt
	if [ -f "$cache" ]; then
		if ! grep -qx "CMAKE_HOME_DIRECTORY:INTERNAL=$PWD/duckdb" "$cache"; then
			echo "cmake cache in $1 was configured somewhere else, starting it over"
			rm -rf "$1"
		elif [ -n "$config" ] && [ "$config" -nt "$cache" ]; then
			echo "the extension set changed since $1 was configured, starting it over"
			rm -rf "$1"
		fi
	fi
	mkdir -p "$1"
}

if [ "$target" = "wasm" ]; then
	config=$here/extension/config-wasm.cmake
	# DUCKDB_NO_THREADS is what upstream's own wasm_eh target uses: single threaded, so no
	# SharedArrayBuffer and no cross-origin isolation needed to run it.
	freshen duckdb/build/wasm
	(cd duckdb/build/wasm && emcmake cmake "${common_flags[@]}" \
		-DDUCKDB_EXTENSION_CONFIGS="$config" \
		-DDISABLE_THREADS=ON \
		-DDUCKDB_EXPLICIT_PLATFORM=wasm_eh \
		-DCMAKE_CXX_FLAGS="-fwasm-exceptions -DDUCKDB_NO_THREADS=1 -fvisibility=hidden -fvisibility-inlines-hidden" \
		../..)
	# every configured extension, not a hand-written target list — the default target
	# builds all of them along with duckdb_static and the loader.
	#
	# Building everything is also why DUCKDB_PLATFORM is set above: the default target
	# includes duckdb_platform, which normally works the platform string out by running a
	# small program it just compiled — and under emscripten that program is a .js file the
	# shell cannot execute. Naming the platform skips the detection entirely. wasm_eh is
	# what -fwasm-exceptions makes this, and it is upstream's own name for it.
	cmake --build duckdb/build/wasm --parallel "$jobs"
	make -C wasm
else
	config=$here/extension/config-native.cmake
	setup_vcpkg
	merge_vcpkg_manifest

	# OSX_BUILD_ARCH, not CMAKE_OSX_ARCHITECTURES: duckdb forces the cmake variable from this
	# one anyway, and it is the name the extensions read. delta picks its rust target from
	# OSX_BUILD_ARCH alone — set only the cmake variable and cargo builds delta-kernel-rs for
	# the runner's own arm64 while everything around it compiles x86_64, which links as
	# "symbol(s) not found for architecture x86_64" an hour in.
	osx_arch=()
	if [ -n "$TARGET_ARCH" ] && [ "$(uname)" = "Darwin" ]; then
		if [ "$TARGET_ARCH" = "x64" ]; then
			osx_arch=(-DOSX_BUILD_ARCH=x86_64)
		else
			osx_arch=(-DOSX_BUILD_ARCH="$TARGET_ARCH")
		fi
	fi
	vcpkg_flags=(
		-DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN_PATH"
		-DVCPKG_BUILD=1
		-DVCPKG_MANIFEST_DIR="$here/duckdb/build/extension_configuration"
	)
	if [ -n "$VCPKG_TARGET_TRIPLET" ]; then
		vcpkg_flags+=(-DVCPKG_TARGET_TRIPLET="$VCPKG_TARGET_TRIPLET")
	fi

	freshen duckdb/build/minimal
	(cd duckdb/build/minimal && cmake "${common_flags[@]}" \
		-DDUCKDB_EXTENSION_CONFIGS="$config" \
		"${vcpkg_flags[@]}" \
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
