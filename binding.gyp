{
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
		"cflags": [
			"-std=c++11",
			"-fexceptions",
			"-lstdc++",
			"-Iinclude",
			"-g",
			"-Ofast",
			"-lpthread"
		],
		"cflags_cc": [
			"-std=c++11",
			"-fexceptions",
			"-lstdc++",
			"-Iinclude",
			"-g",
			"-Ofast",
			"-lpthread"
		],
		"defines": [ 'NAPI_DISABLE_CPP_EXCEPTIONS' ]
	}]
}
