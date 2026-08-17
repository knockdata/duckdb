// The platforms we build for, and the runner each one needs.
//
// This lives here rather than in the workflow's matrix because scripts/plan.mjs has to reason
// about the same list — it drops the platforms already published before any runner starts.
//
// macos-latest is Apple Silicon and covers x64 too, because one Xcode targets both; windows
// and linux each need their own arm runner. Same runner set the desktop release already uses.
//
// vcpkgTriplet is what the extensions with C dependencies (iceberg, avro, excel) are built
// for. It has to name the platform being built for rather than the runner doing the
// building, which is why the two macos legs differ while the runner does not.
//
// On Windows the triplet also picks a C runtime, and all three sides have to agree: duckdb
// forces /MT (CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded) and node-gyp builds addons /MT too, so
// vcpkg has to be `-static`, not `-static-md`. With the -md triplets the aws libraries want the
// DLL CRT and the link ends on unresolved __imp__popen, __imp__aligned_malloc, __imp_modf.
//
// rustTarget is the same story for delta, which builds delta-kernel-rs with cargo: rustup
// installs the runner's own target only, so the macos x64 leg has to ask for x86_64-apple-darwin
// or cargo produces an arm64 library that cannot link into an x86_64 extension.
export default [
	{ os: "macos-latest", platform: "darwin", arch: "arm64", vcpkgTriplet: "arm64-osx", rustTarget: "aarch64-apple-darwin" },
	{ os: "macos-latest", platform: "darwin", arch: "x64", vcpkgTriplet: "x64-osx", rustTarget: "x86_64-apple-darwin" },
	{ os: "ubuntu-latest", platform: "linux", arch: "x64", vcpkgTriplet: "x64-linux", rustTarget: "x86_64-unknown-linux-gnu" },
	{ os: "ubuntu-24.04-arm", platform: "linux", arch: "arm64", vcpkgTriplet: "arm64-linux", rustTarget: "aarch64-unknown-linux-gnu" },
	{ os: "windows-latest", platform: "win32", arch: "x64", vcpkgTriplet: "x64-windows-static", rustTarget: "x86_64-pc-windows-msvc" },
	{ os: "windows-11-arm", platform: "win32", arch: "arm64", vcpkgTriplet: "arm64-windows-static", rustTarget: "aarch64-pc-windows-msvc" },
]
