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
		# The list is read out of the build tree by scripts/archives.mjs rather than written
		# here: it is one archive per linked extension, in three different spellings, and a
		# forgotten one does not fail the link — it fails the first query. The script also
		# knows the order (loader first, duckdb_static last) and skips third_party, whose
		# objects duckdb already folds into libduckdb_static.a.
		"xcode_settings": {
			"OTHER_CFLAGS": ["-O2"],
			"OTHER_LDFLAGS": [
				"-lc++",
				"<!@(node scripts/archives.mjs minimal mac)"
			]
		},
		"conditions": [
			["OS=='linux'", {
				"libraries": [
					"-lstdc++",
					"<!@(node scripts/archives.mjs minimal linux)"
				]
			}],
			["OS=='win'", {
				"libraries": [
					"<!@(node scripts/archives.mjs minimal win)",
					# duckdb asks the Restart Manager which process holds a locked file
					# (RmStartSession and friends), so the addon has to link it too
					"rstrtmgr.lib",
					# objectfs opens a socket to our web server
					"ws2_32.lib",
					# What the vcpkg libraries call into. cmake passes these on its own link
					# lines, but the addon links their archives directly and never sees one:
					# the aws sdk names Userenv, version, Wininet and winhttp as its platform
					# dependencies (it prints them at configure time), aws-c-cal and openssl
					# want bcrypt, crypt32, secur32 and ncrypt, and curl wants wldap32 and
					# normaliz. An import library nothing references costs nothing.
					"Userenv.lib",
					"version.lib",
					"Wininet.lib",
					"winhttp.lib",
					"bcrypt.lib",
					"crypt32.lib",
					"secur32.lib",
					"ncrypt.lib",
					"shlwapi.lib",
					"advapi32.lib",
					"wldap32.lib",
					"normaliz.lib"
				],
				"msvs_settings": {
					"VCCLCompilerTool": { "Optimization": 2 },
					# the MSVC spelling of force_load / --whole-archive
					"VCLinkerTool": {
						"AdditionalOptions": [
							"<!@(node scripts/archives.mjs minimal winwhole)"
						]
					}
				}
			}]
		]
	}]
}
