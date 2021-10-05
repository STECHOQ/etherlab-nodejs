#include "ecrt.h"

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
