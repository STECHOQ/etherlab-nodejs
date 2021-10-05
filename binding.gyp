{
	"variables": {
		"buildtarget": "<!(node -p \"'$BUILD_TARGET'\")"
	},
	"targets": [{
		"target_name": "ecat",
		"include_dirs": [
			"<!@(node -p \"require('node-addon-api').include\")",
			"./src/include",
			"/usr/local/include",
			"/usr/local/lib/"
		],
		"sources": [
			"./src/ecat.cc",
			"./src/include/config_parser.cpp"
		],
		"link_settings": {
			"libraries": [
				"/usr/local/lib/libethercat.so"
			],
			"library_dirs": [
				"/usr/local/lib/"
			]
		},
		"conditions":[
			[
				"buildtarget!='DEBUG'", {
					"cflags": [
						"-fno-exceptions",
						"-lstdc++",
						"-Iinclude",
						"-g",
						"-lpthread"
					],
					"cflags_cc": [
						"-fno-exceptions",
						"-lstdc++",
						"-Iinclude",
						"-g",
						"-lpthread"
					]
				},
				"buildtarget=='DEBUG'", {
					"cflags": [
						"-DDEBUG",
						"-fno-exceptions",
						"-lstdc++",
						"-Iinclude",
						"-g",
						"-lpthread"
					],
					"cflags_cc": [
						"-DDEBUG",
						"-fno-exceptions",
						"-lstdc++",
						"-Iinclude",
						"-g",
						"-lpthread"
					]
				}
			]
		],
		"defines": [ 'NAPI_DISABLE_CPP_EXCEPTIONS' ]
	}]
}
