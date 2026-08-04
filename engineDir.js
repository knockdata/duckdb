// Where the native addon and the wasm binary are.
//
// Normally npm answers this: the platform package named in optionalDependencies is only
// installed on the machine it matches, and the wasm sits inside this package. But a
// single-file bundle (an SEA, an esbuild bundle) has no node_modules to resolve against,
// so an embedder can call setEngineDir with a folder holding both, and that wins.
import fs from 'node:fs'
import path from 'node:path'
import { createRequire } from 'node:module'
import { fileURLToPath } from 'node:url'

const platformPackage = `@knockdata/duckdb-${process.platform}-${process.arch}`

let engineDir = null

export function setEngineDir(dir) {
	engineDir = dir
}

export function addonPath() {
	return firstExisting([
		engineDir && path.join(engineDir, 'duckdb_napi.node'),
		resolveRequire(`${platformPackage}/duckdb_napi.node`),
		// a local build.sh run, before anything is published
		path.join(here(), 'build', 'Release', 'duckdb_napi.node'),
	])
}

export function wasmPath() {
	return firstExisting([
		engineDir && path.join(engineDir, 'wasm', 'duckdb.wasm'),
		path.join(here(), 'wasm', 'duckdb.wasm'),
		// a bundler may have inlined this file far away from the package it came from, so
		// fall back to asking the resolver where the package itself is
		resolveRequire('@knockdata/duckdb/wasm/duckdb.wasm'),
	])
}

// not resolving is normal, and never fatal: npm skips an optional dependency whose os/cpu
// does not match, and the caller falls back to the wasm
function resolveRequire(specifier) {
	try {
		return createRequire(import.meta.url).resolve(specifier)
	}
	catch (error) {
		return null
	}
}

function here() {
	return path.dirname(fileURLToPath(import.meta.url))
}

function firstExisting(candidates) {
	return candidates.find(candidate => candidate && fs.existsSync(candidate)) ?? null
}
