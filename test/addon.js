// The native addon, opened by path. Fails loudly if the addon is missing rather than
// silently falling back to the wasm — in CI this job exists to prove the addon works.
import fs from 'node:fs'
import { addonPath } from '../engineDir.js'
import { getEntries, getEntry } from '../index.js'
import { check, checkEngine, samplePath } from './check.js'

// A cross build produces an addon for another architecture, which this process cannot load
// at all — node is one architecture and dlopen will not mix. Nothing to check here; the
// platform's own release run is where that binary gets exercised.
const crossBuilt = process.env.TARGET_ARCH && process.env.TARGET_ARCH !== process.arch
const addon = addonPath()
if (crossBuilt) {
	console.log(`skipped: built for ${process.env.TARGET_ARCH}, running on ${process.arch}`)
	check('cross build still produced an addon', addon !== null, true)
	// the linker ignores archives of the wrong architecture instead of failing, so size is
	// the only signal that the engine actually made it in
	check('addon links the engine (>5 MB)', fs.statSync(addon).size > 5_000_000, true)
}
else if (addon) {
	console.log('addon:', addon, fs.statSync(addon).size, 'bytes')
	const before = fs.statSync(samplePath).mtimeMs
	await checkEngine(getEntries, getEntry, samplePath)
	// READ_ONLY is the promise this package makes; a write would show up here
	check('fixture untouched', fs.statSync(samplePath).mtimeMs, before)
	check('no wal left behind', fs.existsSync(`${samplePath}.wal`), false)
}
else {
	console.error('FAIL no addon built — run build.sh')
	process.exitCode = 1
}
