#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H
#endif

#include "rapidjson/document.h"

#include "structs_declaration.h"

#include <cstdio>
#include <cstdint>
#include <cctype>

#include <sys/stat.h>

#define swap_endian16(x) \
		((uint16_t)( \
		(((uint16_t)(x) & 0x00ffU) << 8) | \
		(((uint16_t)(x) & 0xff00U) >> 8) ))
#define swap_endian32(x) \
		((uint32_t)( \
		(((uint32_t)(x) & 0x000000ffUL) << 24) | \
		(((uint32_t)(x) & 0x0000ff00UL) <<  8) | \
		(((uint32_t)(x) & 0x00ff0000UL) >>  8) | \
		(((uint32_t)(x) & 0xff000000UL) >> 24) ))
#define swap_endian64(x) \
		((uint64_t)( \
		(((uint64_t)(x) & 0x00000000000000ffULL) << 56) | \
		(((uint64_t)(x) & 0x000000000000ff00ULL) << 40) | \
		(((uint64_t)(x) & 0x0000000000ff0000ULL) << 24) | \
		(((uint64_t)(x) & 0x00000000ff000000ULL) <<  8) | \
		(((uint64_t)(x) & 0x000000ff00000000ULL) >>  8) | \
		(((uint64_t)(x) & 0x0000ff0000000000ULL) >> 24) | \
		(((uint64_t)(x) & 0x00ff000000000000ULL) >> 40) | \
		(((uint64_t)(x) & 0xff00000000000000ULL) >> 56) ))

#define mask(n) ((1<<n)-1)

static const uint8_t SyncMEthercatDirection[] = {
		1, // SM0 EC_DIR_OUTPUT
		2, // SM1 EC_DIR_INPUT
		1, // SM2 EC_DIR_OUTPUT
		2  // SM3 EC_DIR_INPUT
	};

off_t get_filesize(const char *);
std::string normalize_hex_string(std::string);
uint32_t _to_uint(const rapidjson::Value&);
uint8_t member_is_valid_array(const rapidjson::Value&, const char *);
int8_t parse_json(const char *, slaveEntry **, uint8_t *, startupConfig **, uint8_t *, uint8_t );
int32_t _slave_entries_sort_asc(const void *, const void *);
