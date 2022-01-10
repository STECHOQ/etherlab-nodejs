#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H
#endif

#include <cstdio>
#include <cstdint>
#include <cctype>
#include <vector>

#include <sys/stat.h>

#include "rapidjson/document.h"
#include "ecrt.h"

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

/*****************************************************************************/

typedef struct slaveConfig{
	uint16_t alias; /**< Slave alias address. */
	uint16_t position; /**< Slave position. */
	uint32_t vendor_id; /**< Slave vendor ID. */
	uint32_t product_code; /**< Slave product code. */
	ec_slave_config_state_t state;
} slaveConfig;

typedef struct startupConfig{
	uint8_t size;
	uint8_t slavePosition;
	uint16_t index;
	uint8_t subindex;
	uint32_t value;
} startupConfig;

typedef struct slaveEntry{
	uint16_t alias; /**< Slave alias address. */
	uint16_t position; /**< Slave position. */
	uint32_t vendor_id; /**< Slave vendor ID. */
	uint32_t product_code; /**< Slave product code. */

	uint8_t sync_index; /**< SM index. */
	uint16_t pdo_index; /**< PDO entry index. */

	uint16_t index; /**< PDO entry index. */
	uint8_t subindex; /**< PDO entry subindex. */
	uint8_t size;

	uint8_t add_to_domain;

	uint32_t offset;
	uint32_t bit_position;

	uint32_t value;
	uint8_t direction;

	uint8_t SWAP_ENDIAN;
	uint8_t SIGNED;

	uint32_t writtenValue;

	uint8_t WATCHDOG_ENABLED;
} slaveEntry;

/*****************************************************************************/

static const uint8_t SyncMEthercatDirection[] = {
		1, // SM0 EC_DIR_OUTPUT
		2, // SM1 EC_DIR_INPUT
		1, // SM2 EC_DIR_OUTPUT
		2  // SM3 EC_DIR_INPUT
	};

/*****************************************************************************/

int8_t parse_json(const char *, std::vector<slaveEntry> &,
					uint8_t *, std::vector<startupConfig> &,
					uint8_t *, bool do_sort_slave);
