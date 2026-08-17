// Prints everything the addon (or the wasm module) has to link, in link order, one entry
// per line, already spelled the way the asking linker wants it.
//
//   node scripts/archives.mjs minimal mac      for xcode_settings.OTHER_LDFLAGS
//   node scripts/archives.mjs minimal linux    for libraries
//   node scripts/archives.mjs minimal win      for libraries
//   node scripts/archives.mjs minimal winwhole for VCLinkerTool.AdditionalOptions
//   node scripts/archives.mjs wasm   paths     bare paths, for wasm/Makefile
//   node scripts/archives.mjs minimal targets  the cmake targets that produce them, for build.sh
//
// This used to be three hardcoded lists inside binding.gyp — one per platform — holding
// three paths each. With the extensions we link now it is a dozen duckdb archives plus
// three dozen libraries vcpkg and cargo built underneath them, and the failure when one is
// missing is not a link error: duckdb registers its types and functions from static
// initializers, a node addon is a bundle and links fine with undefined symbols, so a
// forgotten library is `symbol not found in flat namespace` on the first require, or a
// segfault on the first query. Reading the build tree removes the chance to forget.
//
// Two groups, and the difference matters:
//
//   whole      duckdb and its extensions, every object linked in whether or not anything
//              references it, because their registrations live in static initializers
//   deps       what those extensions call into — openssl, curl, the aws sdk, avro,
//              minizip, delta's rust kernel. Linked normally: whole-archive here would
//              add tens of megabytes of code nothing calls.
//
// third_party is deliberately absent from both: duckdb folds those object files into
// libduckdb_static.a itself, so listing them again would double every symbol.
import { readdirSync, existsSync, statSync, readFileSync } from "node:fs"
import { join, basename } from "node:path"
import { fileURLToPath } from "node:url"

// fileURLToPath, never url.pathname: on Windows that returns "/D:/a/duckdb", the leading slash
// makes every join below "\D:\a\duckdb", nothing exists, and the script reports an engine that
// was in fact built fine
const here = fileURLToPath(new URL("..", import.meta.url))
const target = process.argv[2] === "wasm" ? "wasm" : "minimal"
const style = process.argv[3] || "paths"
const build = join(here, "duckdb", "build", target)

// MSVC writes <name>.lib into a per-configuration subdirectory; the unix generators write
// lib<name>.a beside the target. Look in both.
function archiveIn(directory, name) {
	const candidates = [
		join(directory, `lib${name}.a`),
		join(directory, `${name}.lib`),
		join(directory, "Release", `${name}.lib`),
	]
	return candidates.find(candidate => existsSync(candidate)) || null
}

function directoriesIn(parent) {
	if (existsSync(parent)) {
		return readdirSync(parent).sort().filter(name => statSync(join(parent, name)).isDirectory())
	}
	else {
		return []
	}
}

// Which extensions this engine actually has, straight from the loader cmake generated for
// it. Reading the extension directory instead would be a trap: dropping an extension from
// the config leaves its archive behind on disk, and linking that orphan back in is both
// enormous and a lie about what the engine contains. The generated loader only ever names
// what was configured this time round.
function linkedExtensions() {
	const loader = join(build, "codegen", "src", "generated_extension_loader.cpp")
	if (existsSync(loader)) {
		const body = readFileSync(loader, "utf8")
		const list = /LinkedExtensions\(\)\s*\{[^}]*?\{([^}]*)\}/s.exec(body)
		if (list) {
			return [...list[1].matchAll(/"([^"]+)"/g)].map(match => match[1])
		}
	}
	console.error(`cannot tell which extensions ${build} linked — run build.sh first`)
	process.exit(1)
}

function extensionArchives() {
	const extensionDir = join(build, "extension")
	return linkedExtensions()
		.map(name => archiveIn(join(extensionDir, name), `${name}_extension`))
		.filter(Boolean)
}

// The loader runs every extension's registration, so it goes first; duckdb_static holds
// everything they call into, so it goes last. dummy_static_extension_loader is duckdb's
// own placeholder for a build with no extensions and is never one of these, because it
// lives directly under extension/ rather than in a per-extension directory.
//
// Read when a style asks for it, never at import: `targets` runs BEFORE the build, when none
// of these archives exists yet, and the check below would call a tree that is about to be
// built an empty one.
function wholeArchives() {
	const found = [
		archiveIn(join(build, "extension"), "duckdb_generated_extension_loader"),
		...extensionArchives(),
		archiveIn(join(build, "src"), "duckdb_static"),
	].filter(Boolean)
	if (found.length < 2) {
		console.error(`no duckdb archives under ${build} — run build.sh first`)
		process.exit(1)
	}
	return found
}

// Whatever vcpkg was asked for by the extensions' merged manifest, whichever triplet it
// resolved to. There is exactly one triplet per build tree.
function vcpkgLibraries() {
	const installed = join(build, "vcpkg_installed")
	const found = []
	for (const triplet of directoriesIn(installed)) {
		const libDir = join(installed, triplet, "lib")
		if (triplet !== "vcpkg" && existsSync(libDir)) {
			for (const name of readdirSync(libDir).sort()) {
				if (name.endsWith(".a") || name.endsWith(".lib")) {
					found.push(join(libDir, name))
				}
			}
		}
	}
	return found
}

// delta is a C++ shell over delta-kernel-rs, and cargo puts its staticlib under a target
// triple directory when cross-compiling and directly under target/release when not. The
// deps/ and build/ subdirectories hold cargo's intermediates, which are not it.
function rustLibraries() {
	const root = join(build, "rust", "src", "delta_kernel", "target")
	const releases = [join(root, "release"), ...directoriesIn(root).map(triple => join(root, triple, "release"))]
	return releases
		.map(release => archiveIn(release, "delta_kernel_ffi"))
		.filter(Boolean)
		.slice(0, 1)
}

function dependencyLibraries() {
	return [...vcpkgLibraries(), ...rustLibraries()]
}

// The cmake targets behind the archives, so build.sh can build exactly these instead of the
// default target. Everything else in that target is something we never ship: a loadable
// .duckdb_extension per extension, and a shared libduckdb that compiles all of duckdb a
// second time. The loadables are also where a windows build dies — they link the vcpkg aws
// libraries, whose CRT imports (__imp__popen, __imp__aligned_malloc) go unresolved there.
function targets() {
	return [
		"duckdb_generated_extension_loader",
		...linkedExtensions().map(name => `${name}_extension`),
		"duckdb_static",
	]
}

function spell(style) {
	if (style === "targets") {
		return targets()
	}
	else if (style === "mac") {
		return [...wholeArchives().map(archive => `-Wl,-force_load,${archive}`), ...dependencyLibraries()]
	}
	else if (style === "linux") {
		// --start-group, because these libraries reference each other in both directions
		// and a single pass over them in any one order leaves something unresolved
		return [
			"-Wl,--whole-archive", ...wholeArchives(), "-Wl,--no-whole-archive",
			"-Wl,--start-group", ...dependencyLibraries(), "-Wl,--end-group",
		]
	}
	else if (style === "win") {
		return [...wholeArchives(), ...dependencyLibraries()]
	}
	else if (style === "winwhole") {
		return wholeArchives().map(archive => `/WHOLEARCHIVE:${basename(archive)}`)
	}
	else {
		return [...wholeArchives(), ...dependencyLibraries()]
	}
}

// Forward slashes, even on Windows. binding.gyp pulls this output in through <!@(...) and gyp
// reads what comes back as a string literal, where a backslash is an escape: D:\a\duckdb reached
// the linker as "D:aduckdb" and it opened nothing. MSVC, cmake and gyp all take forward slashes.
console.log(spell(style).map(line => line.replaceAll("\\", "/")).join("\n"))
