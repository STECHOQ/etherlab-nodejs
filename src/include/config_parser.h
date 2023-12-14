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

typedef int32_t io_size_et;
typedef uint16_t slave_size_et;
typedef uint16_t sparam_size_et;

typedef uint16_t ecat_pos_al;
typedef uint16_t ecat_index_al;
typedef uint8_t ecat_sub_al;
typedef uint8_t ecat_size_al;
typedef uint32_t ecat_value_al;

typedef struct slaveConfig_s{
	uint16_t alias; /**< Slave alias address. */
	ecat_pos_al position; /**< Slave position. */
	uint32_t vendor_id; /**< Slave vendor ID. */
	uint32_t product_code; /**< Slave product code. */
	ec_slave_config_state_t state;
} slaveConfig;

typedef struct startupConfig_s{
	ecat_size_al size;
	ecat_pos_al slavePosition;
	ecat_index_al index;
	ecat_sub_al subindex;
	ecat_value_al value;
} startupConfig;

typedef struct slaveEntry_s{
	uint16_t alias; /**< Slave alias address. */
	ecat_pos_al position; /**< Slave position. */
	uint32_t vendor_id; /**< Slave vendor ID. */
	uint32_t product_code; /**< Slave product code. */

	uint8_t sync_index; /**< SM index. */
	ecat_index_al pdo_index; /**< PDO entry index. */

	ecat_index_al index; /**< PDO entry index. */
	ecat_sub_al subindex; /**< PDO entry subindex. */
	ecat_size_al size;

	uint8_t add_to_domain;

	uint32_t offset;
	uint32_t bit_position;

	ecat_value_al value;
	uint8_t direction;

	uint8_t SWAP_ENDIAN;
	uint8_t SIGNED;

	ecat_value_al writtenValue;

	uint8_t WATCHDOG_ENABLED;
} slaveEntry;

union Unit32b {
	uint8_t byte;
	uint16_t word;
	uint32_t dword;
	uint8_t array[4];
};

union Unit64b {
	uint8_t byte;
	uint16_t word;
	uint32_t dword;
	uint64_t qword;
	uint8_t array[8];
};

typedef enum sdo_req_type_en{
    ECAT_SDO_READ = 0,
    ECAT_SDO_WRITE = 1
} sdo_req_type_al;

/*****************************************************************************/

static const uint8_t SyncMEthercatDirection[] = {
		1, // SM0 EC_DIR_OUTPUT
		2, // SM1 EC_DIR_INPUT
		1, // SM2 EC_DIR_OUTPUT
		2  // SM3 EC_DIR_INPUT
	};

/*****************************************************************************/

extern int8_t parse_json(const char *json_string,
	std::vector<slaveEntry> &slave_entries, slave_size_et *slave_length,
	std::vector<startupConfig> &slave_parameters, sparam_size_et *parameters_length,
	bool do_sort_slave);
