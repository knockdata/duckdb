{
	"targets": [{
		"target_name": "duckdb_napi",
		"sources": ["napi/duckdb_napi.c"],
		"include_dirs": ["duckdb/src/include"],
		"defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"],
		"cflags": ["-O2"],
		# duckdb registers types and functions from static initializers spread across the
		# archives. Without whole-archive linking the linker keeps only the objects our handful
		# of C API calls reference, those initializers never run, and the first open segfaults.
		# A node addon is a bundle, so an unresolved symbol links fine and crashes later.
		#
		# The paths are relative to build/, where node-gyp runs the linker, and the three
		# archives are the ones cmake links into libduckdb itself, in the same order. They are
		# spelled out per platform rather than shared through a gyp variable: a >@() reference
		# inside conditions does not resolve, which is what broke the linux leg.
		"xcode_settings": {
			"OTHER_CFLAGS": ["-O2"],
			"OTHER_LDFLAGS": [
				"-lc++",
				"-Wl,-force_load,../duckdb/build/minimal/extension/libduckdb_generated_extension_loader.a",
				"-Wl,-force_load,../duckdb/build/minimal/extension/core_functions/libcore_functions_extension.a",
				"-Wl,-force_load,../duckdb/build/minimal/src/libduckdb_static.a"
			]
		},
		"conditions": [
			["OS=='linux'", {
				"libraries": [
					"-lstdc++",
					"-Wl,--whole-archive",
					"../duckdb/build/minimal/extension/libduckdb_generated_extension_loader.a",
					"../duckdb/build/minimal/extension/core_functions/libcore_functions_extension.a",
					"../duckdb/build/minimal/src/libduckdb_static.a",
					"-Wl,--no-whole-archive"
				]
			}],
			["OS=='win'", {
				"libraries": [
					"../duckdb/build/minimal/extension/Release/duckdb_generated_extension_loader.lib",
					"../duckdb/build/minimal/extension/core_functions/Release/core_functions_extension.lib",
					"../duckdb/build/minimal/src/Release/duckdb_static.lib",
					# duckdb asks the Restart Manager which process holds a locked file
					# (RmStartSession and friends), so the addon has to link it too
					"rstrtmgr.lib"
				],
				"msvs_settings": {
					"VCCLCompilerTool": { "Optimization": 2 },
					# the MSVC spelling of force_load / --whole-archive
					"VCLinkerTool": {
						"AdditionalOptions": [
							"/WHOLEARCHIVE:duckdb_generated_extension_loader.lib",
							"/WHOLEARCHIVE:core_functions_extension.lib",
							"/WHOLEARCHIVE:duckdb_static.lib"
						]
					}
				}
			}]
		]
	}]
}
