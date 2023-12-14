#include <errno.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h> /* clock_gettime() */
#include <sched.h> /* sched_setscheduler() */
#include <stdint.h>
#include <unistd.h>

#include <cstdio>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <vector>
#include <map>

#include <napi.h>

#include "ecrt.h"
#include "include/config_parser.h"

/****************************************************************************/
#define MAX_SAFE_STACK (8 * 1024) /* The maximum stack size which is
									 guranteed safe to access without
									 faulting */

/** Task period in ns. */
#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC 1000000000
#endif

#define AL_BIT_INIT 0
#define AL_BIT_PREOP 1
#define AL_BIT_SAFEOP 2
#define AL_BIT_OP 3

#define MASTER_STATE_DETAIL(_BIT, _state) ((_state >> _BIT) & 0x01)

/****************************************************************************/

// EtherCAT
static ec_master_state_t master_state = {};
static ec_master_t *master = NULL;

static ec_domain_t *DomainN = NULL;
static ec_domain_state_t DomainN_state = {};
static ec_pdo_entry_reg_t *DomainN_regs = NULL;
static io_size_et DomainN_length;

// process data
static uint8_t *DomainN_pd = NULL;

struct timespec epoch;

#if DEBUG > 0
struct timespec start, end;
#endif

static uint32_t counter = 0;
static int8_t _running_state = -1;
static int8_t isMasterReady = -1;

// slave configurations
static std::vector<ec_slave_config_t*> sc_slaves;

static std::vector<slaveConfig> slaves;
static slave_size_et slaves_length = 0;

static std::vector<slaveEntry> IOs;
static io_size_et IOs_length = 0;

static std::vector<slaveEntry> slave_entries;
static slave_size_et slave_entries_length = 0;

static std::vector<startupConfig> startup_parameters;
static sparam_size_et startup_parameters_length = 0;

// SM startup config
inline static uint32_t _convert_index_sub_size(ecat_index_al index,
	ecat_sub_al subindex, ecat_size_al size);

// mapped domain
static std::map<uint32_t, io_size_et> mapped_domains;
inline static uint32_t _convert_pos_index_sub(const ecat_pos_al& s_position,
	const ecat_index_al& s_index, const ecat_sub_al& s_subindex);
static inline io_size_et get_domain_index(io_size_et* dmn_idx,
	const ecat_pos_al& s_position, const ecat_index_al& s_index,
	const ecat_sub_al& s_subindex);
static inline void assign_domain_identifier();

// Periodic task timing
static uint16_t FREQUENCY = 1000;
static uint32_t PERIOD_NS = NSEC_PER_SEC / FREQUENCY;

// configuration
static std::string json_path;
static bool do_sort_slave;

// Data structure representing our thread-safe function context.
struct TsfnContext {
	TsfnContext(Napi::Env env) : deferred(Napi::Promise::Deferred::New(env)) {};

	// Native Promise returned to JavaScript
	Napi::Promise::Deferred deferred;

	// Native thread
	std::thread nativeThread;

	Napi::ThreadSafeFunction tsfn;
};

/*****************************************************************************/

void check_domain_state(void)
{
	ec_domain_state_t ds;
	ecrt_domain_state(DomainN, &ds);

#if DEBUG > 1
	if (ds.working_counter != DomainN_state.working_counter) {
		timespec_get(&epoch, TIME_UTC);
		printf("%ld.%09ld | Domain1: WC %u.\n",
				epoch.tv_sec,
				epoch.tv_nsec,
				ds.working_counter
			);
	}

	if (ds.wc_state != DomainN_state.wc_state) {
		timespec_get(&epoch, TIME_UTC);
		printf("%ld.%09ld | Domain1: State %u.\n",
				epoch.tv_sec,
				epoch.tv_nsec,
				ds.wc_state
			);
	}
#endif

	DomainN_state = ds;
}

void check_master_state(ec_master_t *master)
{
	ec_master_state_t ms;
	ecrt_master_state(master, &ms);

#if DEBUG > 1
	if (ms.slaves_responding != master_state.slaves_responding) {
		timespec_get(&epoch, TIME_UTC);
		printf("%ld.%09ld | %u slave(s).\n",
				epoch.tv_sec,
				epoch.tv_nsec,
				ms.slaves_responding
			);
	}

	if (ms.al_states != master_state.al_states) {
		timespec_get(&epoch, TIME_UTC);
		printf("%ld.%09ld | AL states: 0x%02X.\n",
				epoch.tv_sec,
				epoch.tv_nsec,
				ms.al_states
			);
	}

	if (ms.link_up != master_state.link_up) {
		timespec_get(&epoch, TIME_UTC);
		printf("%ld.%09ld | Link is %s.\n",
				epoch.tv_sec,
				epoch.tv_nsec,
				ms.link_up ? "up" : "down"
			);
	}
#endif

	master_state = ms;
}

void check_slave_config_states(void)
{
	for(slave_size_et slNumber = 0; slNumber < slaves_length; slNumber++){
		ec_slave_config_state_t s;

		ecrt_slave_config_state(sc_slaves[slNumber], &s);

#if DEBUG > 1
		if (s.al_state != slaves[slNumber].state.al_state) {
			timespec_get(&epoch, TIME_UTC);
			printf("%ld.%09ld | Slaves %d : State 0x%02X.\n",
					epoch.tv_sec,
					epoch.tv_nsec,
					slNumber,
					s.al_state
				);
		}

		if (s.online != slaves[slNumber].state.online) {
			timespec_get(&epoch, TIME_UTC);
			printf("%ld.%09ld | Slaves %d : %s.\n",
					epoch.tv_sec,
					epoch.tv_nsec,
					slNumber,
					s.online ? "online" : "offline"
				);
		}

		if (s.operational != slaves[slNumber].state.operational) {
			timespec_get(&epoch, TIME_UTC);
			printf("%ld.%09ld | Slaves %d : %soperational.\n",
					epoch.tv_sec,
					epoch.tv_nsec,
					slNumber,
					s.operational ? "" : "Not "
				);
		}
#endif

		slaves[slNumber].state = s;
	}
}

uint8_t check_is_operational()
{
	for(slave_size_et slNumber = 0; slNumber < slaves_length; slNumber++){
		if(!slaves[slNumber].state.operational){
			return 0;
		}
	}

	return 1;
}

int8_t write_output_value(io_size_et dmn_idx, ecat_value_al value, uint8_t SIGNED)
{
	// selected index is not output, return immediately
	if(IOs[dmn_idx].direction != EC_DIR_OUTPUT){
		return -1;
	}

	switch(IOs[dmn_idx].size){
		case 1:
			EC_WRITE_BIT(
					DomainN_pd + IOs[dmn_idx].offset,
					IOs[dmn_idx].bit_position,
					(uint8_t) value & 0x1
				);
			return 1;
			break;

		case 8:
			if(SIGNED)
				EC_WRITE_S8(DomainN_pd + IOs[dmn_idx].offset, (int8_t) value);
			else
				EC_WRITE_U8(DomainN_pd + IOs[dmn_idx].offset, (uint8_t) value);
			break;

		case 16:
			if(SIGNED)
				EC_WRITE_S16(DomainN_pd + IOs[dmn_idx].offset, (int16_t) value);
			else
				EC_WRITE_U16(DomainN_pd + IOs[dmn_idx].offset, (uint16_t) value);
			break;

		case 32:
			if(SIGNED)
				EC_WRITE_S32(DomainN_pd + IOs[dmn_idx].offset, (int32_t) value);
			else
				EC_WRITE_U32(DomainN_pd + IOs[dmn_idx].offset, (uint32_t) value);

			break;

		default:
			return 0;
			break;
	}

	return 1;
}

void cyclic_task(ec_master_t *master, io_size_et dmn_size)
{
	// receive process data
	ecrt_master_receive(master);
	ecrt_domain_process(DomainN);

	// check process data state
	check_domain_state();

	if (counter) {
		counter--;
	} else {
		// reset counter
		counter = FREQUENCY;

		// check for master state (optional)
		check_master_state(master);

		// check for slave configuration state(s) (optional)
		check_slave_config_states();
	}

	// do nothing if master is not ready
	if(MASTER_STATE_DETAIL(AL_BIT_OP, master_state.al_states)) {
#if DEBUG > 1
		clock_gettime(CLOCK_MONOTONIC, &end);

		double time_spent = (end.tv_sec - start.tv_sec)
							+ (end.tv_nsec - start.tv_nsec);

		printf("The elapsed time is %f us\n", time_spent / 1000);
		clock_gettime(CLOCK_MONOTONIC, &start);
#endif

		uint16_t tmp16;
		uint32_t tmp32;

		for(io_size_et dmn_idx = 0; dmn_idx < dmn_size; dmn_idx++){
			if(IOs[dmn_idx].direction == EC_DIR_OUTPUT){
				write_output_value(
						dmn_idx,
						IOs[dmn_idx].writtenValue,
						IOs[dmn_idx].SIGNED
					);
			}

			switch(IOs[dmn_idx].size){
				// No endian difference for 1 bit variable
				case 1:
					IOs[dmn_idx].value = (uint32_t) EC_READ_BIT(
											DomainN_pd + IOs[dmn_idx].offset,
											IOs[dmn_idx].bit_position
										);
					break;

				// No endian difference for 1 byte variable
				case 8:
					IOs[dmn_idx].value =
						(uint32_t) EC_READ_U8(DomainN_pd + IOs[dmn_idx].offset);
					break;

				case 16:
					tmp16 = EC_READ_U16(DomainN_pd + IOs[dmn_idx].offset);
					IOs[dmn_idx].value = (uint32_t) IOs[dmn_idx].SWAP_ENDIAN
											? swap_endian16(tmp16)
											: tmp16;
					break;

				case 32:
					tmp32 = EC_READ_U32(DomainN_pd + IOs[dmn_idx].offset);
					IOs[dmn_idx].value = (uint32_t) IOs[dmn_idx].SWAP_ENDIAN
											? swap_endian32(tmp32)
											: tmp32;
					break;

				default:
					tmp32 = (*((uint32_t *)(DomainN_pd + IOs[dmn_idx].offset)))
							& mask(IOs[dmn_idx].size);

					IOs[dmn_idx].value = (uint32_t) IOs[dmn_idx].SWAP_ENDIAN
											? swap_endian32(tmp32)
											: tmp32;
					break;
			}

#if DEBUG > 2
			printf("Index %2d pos %d 0x%04x:%02x = %04x\n", dmn_idx,
				IOs[dmn_idx].position, IOs[dmn_idx].index, IOs[dmn_idx].subindex,
				IOs[dmn_idx].value);
#endif

		};

#if DEBUG > 2
		printf("=====================\n");
#endif

	}

	ecrt_domain_queue(DomainN);
	ecrt_master_send(master);
}

void domain_startup_config(ec_pdo_entry_reg_t **DomainN_regs, io_size_et *dmn_size)
{
#if DEBUG > 0
	fprintf(stdout, "\nConfiguring Domain...\n");
#endif

	slave_size_et length = slave_entries_length;
	slave_size_et slNumber;
	io_size_et dmn_idx;
	io_size_et alloc_size;

	if((*dmn_size) == 0){
		*DomainN_regs = (ec_pdo_entry_reg_t*) malloc(sizeof(ec_pdo_entry_reg_t));
	}

	// find length of valid domain inside slave_entries
	*dmn_size = 0;
	for(slNumber = 0; slNumber < length; slNumber++){

		// prevent bit-padding from being added into domains
		if(slave_entries[slNumber].index == 0x0000){
			slave_entries[slNumber].add_to_domain = 0;
			continue;
		}

		if(slave_entries[slNumber].add_to_domain){
			(*dmn_size)++;
		}
	}

	// new size to be allocated into DomainN_regs
	alloc_size = ((*dmn_size) + 1) * sizeof(ec_pdo_entry_reg_t);

	// reallocate domain_regs
	*DomainN_regs = (ec_pdo_entry_reg_t*) realloc(*DomainN_regs, alloc_size);

	// reserve vector memory allocation,
	// in order to avoid pointer address change everytime we push_back new value
	IOs.reserve(length);

	// add every valid slave process data into domain
	for(slNumber = 0, dmn_idx = 0; slNumber < length; slNumber++){
		if(slave_entries[slNumber].add_to_domain){
			// create IOs domain to access domain value
			IOs.push_back({
					slave_entries[slNumber].alias,
					slave_entries[slNumber].position,
					slave_entries[slNumber].vendor_id,
					slave_entries[slNumber].product_code,
					slave_entries[slNumber].sync_index,
					slave_entries[slNumber].pdo_index,
					slave_entries[slNumber].index,
					slave_entries[slNumber].subindex,
					slave_entries[slNumber].size,
					slave_entries[slNumber].add_to_domain,
					slave_entries[slNumber].offset,
					slave_entries[slNumber].bit_position,
					slave_entries[slNumber].value,
					slave_entries[slNumber].direction,
					slave_entries[slNumber].SWAP_ENDIAN,
					slave_entries[slNumber].SIGNED,
					slave_entries[slNumber].writtenValue,
					slave_entries[slNumber].WATCHDOG_ENABLED
				});

			// register domain with IOs
			(*DomainN_regs)[dmn_idx] = {
					IOs[dmn_idx].alias,
					IOs[dmn_idx].position,
					IOs[dmn_idx].vendor_id,
					IOs[dmn_idx].product_code,
					IOs[dmn_idx].index,
					IOs[dmn_idx].subindex,
					&IOs[dmn_idx].offset,
					&IOs[dmn_idx].bit_position
				};

#if DEBUG > 0
			printf("  > Domain %3d: Slave %3d 0x%04x:%02x offset %2d\n",
					dmn_idx,
					(*DomainN_regs)[dmn_idx].position,
					(*DomainN_regs)[dmn_idx].index,
					(*DomainN_regs)[dmn_idx].subindex,
					IOs[dmn_idx].offset
				);
#endif

			dmn_idx++;
		}
	}

	// terminate with an empty structure
	(*DomainN_regs)[(*dmn_size)] = {};
}

inline uint32_t _convert_index_sub_size(ecat_index_al index, ecat_sub_al subindex,
	ecat_size_al size)
{
	return ((index & 0xffff) << 16) | ((subindex & 0xff) << 8) | (size & 0xff);
}

void syncmanager_startup_config()
{
#if DEBUG > 0
	fprintf(stdout, "\nConfiguring SyncManager and Mapping PDOs...\n");
#endif

	slave_size_et length = slave_entries_length;

	ecat_pos_al last_position = -1;
	ecat_index_al last_pdo_index = -1;
	uint32_t last_index_sub_size = -1;

	uint8_t syncM_index = 0, last_syncM_index = -1;
	ec_direction_t direction;
	ec_watchdog_mode_t watchdog_mode;

	ecat_pos_al current_position = -1;
	ecat_index_al current_pdo_index = -1;
	ecat_index_al current_index = -1;
	ecat_sub_al current_subindex = -1;

	uint32_t processed_index_sub_size = -1;

	// add every valid slave and configure sync manager
	for(slave_size_et slNumber = 0; slNumber < length; slNumber++){
		processed_index_sub_size = _convert_index_sub_size(
										slave_entries[slNumber].index,
										slave_entries[slNumber].subindex,
										slave_entries[slNumber].size
									);
		syncM_index = slave_entries[slNumber].sync_index;

#if DEBUG > 0
		printf("\nSlave%2d 0x%04x 0x%08x %02d-bits\n",
				slave_entries[slNumber].position, slave_entries[slNumber].pdo_index,
				processed_index_sub_size, slave_entries[slNumber].size);
#endif

		// invalid pdo index use default configuration, continue to next index
		if(last_position != slave_entries[slNumber].position
			&& slave_entries[slNumber].pdo_index){

			// reset last SM index when encountering new slave
			last_syncM_index = -1;

			direction = (ec_direction_t) slave_entries[slNumber].direction;
			watchdog_mode = slave_entries[slNumber].WATCHDOG_ENABLED
						? EC_WD_ENABLE
						: EC_WD_DISABLE;
			current_position = slave_entries[slNumber].position;

#if DEBUG > 0
			printf("config SM                            : Slave%2d SM%d\n",
												current_position, syncM_index);
#endif

			if(ecrt_slave_config_sync_manager(
							sc_slaves[current_position],
							syncM_index,
							direction,
							watchdog_mode
			)){
				fprintf(stderr, "Failed to configure SM. Slave%2d SM%d\n",
												current_position, syncM_index);
				exit(EXIT_FAILURE);
			}

			last_position = current_position;
		}

		if(last_syncM_index != syncM_index && slave_entries[slNumber].pdo_index){
			ecrt_slave_config_pdo_assign_clear(sc_slaves[current_position],
				syncM_index);

			last_syncM_index = syncM_index;

#if DEBUG > 0
			printf("clear PDO assign                     : Slave%2d SM%d\n",
												current_position, syncM_index);
#endif
		}

		// invalid pdo index use default configuration, continue to next index
		if(last_pdo_index != slave_entries[slNumber].pdo_index && slave_entries[slNumber].pdo_index){

			current_pdo_index = slave_entries[slNumber].pdo_index;

#if DEBUG > 0
			printf("add PDO assign and clear PDO mapping : Slave%2d SM%d 0x%04x\n",
							current_position, syncM_index, current_pdo_index);
#endif

			if(ecrt_slave_config_pdo_assign_add(
									sc_slaves[current_position],
									syncM_index,
									current_pdo_index
			)){
				fprintf(stderr, "Failed to configure PDO assign. Slave%2d SM%d 0x%04x\n",
							current_position, syncM_index, current_pdo_index);
				exit(EXIT_FAILURE);
			}

			ecrt_slave_config_pdo_mapping_clear(
								sc_slaves[current_position],
								current_pdo_index
							);

			last_pdo_index = current_pdo_index;
		}

		// invalid pdo index use default configuration, continue to next index
		if(last_index_sub_size != processed_index_sub_size
				&& processed_index_sub_size
		){
			current_index = slave_entries[slNumber].index;
			current_subindex = slave_entries[slNumber].subindex;

#if DEBUG > 0
			printf("add PDO mapping                      : Slave%2d SM%d 0x%4x 0x%04x:%02x %2d\n",
				current_position, syncM_index, current_pdo_index, current_index,
									current_subindex, slave_entries[slNumber].size);
#endif

			int8_t mapping = ecrt_slave_config_pdo_mapping_add(
					sc_slaves[current_position],
					current_pdo_index,
					current_index,
					current_subindex,
					slave_entries[slNumber].size
				);

			if(mapping){
				fprintf(stderr,
					"Failed to add PDO mapping. Slave%2d SM%d 0x%4x 0x%04x:0x%02x %2d\n",
					current_position, syncM_index, current_pdo_index,
					current_index, current_subindex, slave_entries[slNumber].size);

				exit(EXIT_FAILURE);
			}

			last_index_sub_size = _convert_index_sub_size(
										slave_entries[slNumber].index,
										slave_entries[slNumber].subindex,
										slave_entries[slNumber].size
									);
		}
	}
}

static inline uint8_t _skip_current_slave_position(ecat_pos_al position,
									std::vector<ecat_pos_al>& last_positions)
{
	for(slave_size_et slNumber = 0; slNumber < last_positions.size(); slNumber++){
		if(position == last_positions[slNumber]){
			return 1;
		}
	}

	return 0;
}

void slave_startup_config(ec_master_t *master)
{
#if DEBUG > 0
	fprintf(stdout, "\nConfiguring Slaves...\n");
#endif

	slave_size_et length = slave_entries_length;
	std::vector<ecat_pos_al> last_positions;

	for(slave_size_et slNumber = 0; slNumber < length; slNumber++){
		// skip current slave, if it's already configured
		if(_skip_current_slave_position(
				slave_entries[slNumber].position, last_positions)
		){
			continue;
		}

		// current slave
		slaveConfig current = {
				slave_entries[slNumber].alias,
				slave_entries[slNumber].position,
				slave_entries[slNumber].vendor_id,
				slave_entries[slNumber].product_code,
				{}
			};

#if DEBUG > 0
		printf("Slave %2d: 0x%08x 0x%08x\n", current.position,
									current.vendor_id, current.product_code);
#endif

		// save current slave information in global slaves
		slaves.push_back(current);

		// configure slave
		sc_slaves.push_back(ecrt_master_slave_config(
				master,
				current.alias,
				current.position,
				current.vendor_id,
				current.product_code
			));

		// update number of slaves
		slaves_length++;

		// save last slave's position
		last_positions.push_back(current.position);

#if DEBUG > 0
		printf("Current: Slave %2d %04x %04x %02x\n",
			slave_entries[slNumber].position, slave_entries[slNumber].pdo_index,
			slave_entries[slNumber].index, slave_entries[slNumber].subindex);
#endif
	}
}

void startup_parameters_config()
{
#if DEBUG > 0
	fprintf(stdout, "\nConfiguring Startup Parameters...\n");
#endif

	static sparam_size_et length = startup_parameters_length;
	for(sparam_size_et par_idx = 0; par_idx < length; par_idx++){
		switch(startup_parameters[par_idx].size){
			case 8:
				ecrt_slave_config_sdo8(
						sc_slaves[startup_parameters[par_idx].slavePosition],
						startup_parameters[par_idx].index,
						startup_parameters[par_idx].subindex,
						(uint8_t) startup_parameters[par_idx].value
					);
				break;

			case 16:
				ecrt_slave_config_sdo16(
						sc_slaves[startup_parameters[par_idx].slavePosition],
						startup_parameters[par_idx].index,
						startup_parameters[par_idx].subindex,
						(uint16_t) startup_parameters[par_idx].value
					);
				break;

			default:
				ecrt_slave_config_sdo32(
						sc_slaves[startup_parameters[par_idx].slavePosition],
						startup_parameters[par_idx].index,
						startup_parameters[par_idx].subindex,
						(uint32_t) startup_parameters[par_idx].value
					);
				break;
		}

#if DEBUG > 0
		printf("Set Startup Parameter Slave%2d, 0x%04x:%02x = 0x%x (%d)\n",
				startup_parameters[par_idx].slavePosition,
				startup_parameters[par_idx].index,
				startup_parameters[par_idx].subindex,
				startup_parameters[par_idx].value,
				startup_parameters[par_idx].value
			);
#endif

	}
}

void reset_global_vars(void)
{
	IOs.clear();
	IOs_length = 0;

	slaves.clear();
	slaves_length = 0;

	slave_entries.clear();
	slave_entries_length = 0;

	startup_parameters.clear();
	startup_parameters_length = 0;

	sc_slaves.clear();
}

/****************************************************************************/

void stack_prefault(void)
{
	unsigned char dummy[MAX_SAFE_STACK];
	memset(dummy, 0, MAX_SAFE_STACK);
}

void set_next_wait_period(struct timespec* wakeup_time)
{
	wakeup_time->tv_nsec += PERIOD_NS;
	while (wakeup_time->tv_nsec >= NSEC_PER_SEC) {
		wakeup_time->tv_nsec -= NSEC_PER_SEC;
		wakeup_time->tv_sec++;
	}
}

void assign_domain_identifier()
{
#if DEBUG > 0
	printf("\nAssigning Domain identifier...\n");
#endif

	for (io_size_et dmn_idx = 0; dmn_idx < IOs_length; dmn_idx++){
		uint32_t identifier = _convert_pos_index_sub(
				IOs[dmn_idx].position,
				IOs[dmn_idx].index,
				IOs[dmn_idx].subindex
			);

		mapped_domains[identifier] = dmn_idx;
	}
}

inline static uint32_t _convert_pos_index_sub(const ecat_pos_al& s_position,
	const ecat_index_al& s_index, const ecat_sub_al& s_subindex)
{
	return (s_position << 24) | (s_index << 8) | (s_subindex << 0);
}

static io_size_et get_domain_index(io_size_et* dmn_idx,
	const ecat_pos_al& s_position, const ecat_index_al& s_index,
	const ecat_sub_al& s_subindex)
{
	uint32_t key = _convert_pos_index_sub(s_position, s_index, s_subindex);

	try {
		*dmn_idx = mapped_domains.at(key);
		return 0;
	} catch (const std::out_of_range& err) {
		fprintf(stderr, "Error %s: Index not found for pos %2d 0x%04x:%02x\n",
			err.what(),
			s_position,
			s_index,
			s_subindex);

		return -1;
	}
}

int8_t write_domain(const ecat_pos_al& s_position,
	const ecat_index_al& s_index, const ecat_sub_al& s_subindex,
	const ecat_value_al& value)
{
	if(!check_is_operational()){
		return -1;
	}

	io_size_et dmn_idx;
	if(get_domain_index(&dmn_idx, s_position, s_index, s_subindex) < 0){
		return -1;
	}

	IOs[dmn_idx].writtenValue = value;

	return 0;
}

int8_t read_domain(const ecat_pos_al& s_position, const ecat_index_al& s_index,
	const ecat_sub_al& s_subindex, ecat_value_al* value)
{
	if(!check_is_operational()){
		return -1;
	}

	io_size_et dmn_idx;
	if(get_domain_index(&dmn_idx, s_position, s_index, s_subindex) < 0){
		return -1;
	}

	*value = IOs[dmn_idx].value;

	return 0;
}

int8_t init_slave()
{
	return parse_json(
			&json_path[0],
			slave_entries,
			&slave_entries_length,
			startup_parameters,
			&startup_parameters_length,
			do_sort_slave
		);
}

void init_master_and_domain()
{
#if DEBUG > 0
	fprintf(stdout, "\nInitialize Master and Domains\n");
#endif

	DomainN_regs = NULL;
	master = NULL;
	DomainN_length = 0;

	if(slave_entries_length == 0){
		if(init_slave() != 0){
			Napi::Error::Fatal(
					"init_master_and_domain",
					"Slave(s) must be configured first!\n"
				);
			exit(EXIT_FAILURE);
		}
	}

	// request ethercat master
	master = ecrt_request_master(0);
	if (!master) {
		Napi::Error::Fatal(
				"init_master_and_domain",
				"Failed at requesting master!\n"
			);
		exit(EXIT_FAILURE);
	}

	/* configure Slaves at startup */
	slave_startup_config(master);

	/* Configure PDO at startup */
	syncmanager_startup_config();

	/* Startup parameters */
	startup_parameters_config();

	/* Configuring Domain */
	domain_startup_config(&DomainN_regs, &DomainN_length);
	IOs_length = DomainN_length;

	// Create a new process data domain
	if (!(DomainN = ecrt_master_create_domain(master))) {
		Napi::Error::Fatal(
				"init_master_and_domain",
				"Domain Creation failed!\n"
			);
		exit(EXIT_FAILURE);
	}

	if (ecrt_domain_reg_pdo_entry_list(DomainN, DomainN_regs)) {
		Napi::Error::Fatal(
				"init_master_and_domain",
				"PDO entry registration failed!\n"
			);
		exit(EXIT_FAILURE);
	}

	// free allocated memories from startup configurations
	free(DomainN_regs);

	// map domain indexes
	assign_domain_identifier();

#if DEBUG > 0
	fprintf(stdout, "\nMaster & Domain have been initialized.\n");
	fprintf(stdout, "Slaves Entries Length: %ld\n", sc_slaves.size());
	fprintf(stdout, "Number of Domains: %d \n", DomainN_length);
#endif

	isMasterReady = 1;

	check_slave_config_states();
}

void activate_master()
{

#if DEBUG > 0
	fprintf(stdout, "\nActivating master...\n");
#endif
	if (ecrt_master_activate(master)) {
		Napi::Error::Fatal(
				"init_master_and_domain",
				"Master Activation failed!\n"
			);
		exit(EXIT_FAILURE);
	}

#if DEBUG > 0
	fprintf(stdout, "\nInitializing Domain data...\n");
#endif
	if (!(DomainN_pd = ecrt_domain_data(DomainN))) {
		Napi::Error::Fatal(
				"init_master_and_domain",
				"Domain data initialization failed!\n"
			);
		exit(EXIT_FAILURE);
	}
}

void read_sdo_data(ec_sdo_request_t* req, const ecat_size_al& size, void* value)
{
	switch(size){
		case 1: {
			uint8_t read = EC_READ_U8(ecrt_sdo_request_data(req));
			memcpy(value, &read, size);
		} break;

		case 2: {
			uint16_t read = EC_READ_U16(ecrt_sdo_request_data(req));
			memcpy(value, &read, size);
		} break;

		case 4: {
			uint32_t read = EC_READ_U32(ecrt_sdo_request_data(req));
			memcpy(value, &read, size);
		} break;

		default: {
			memcpy(value, ecrt_sdo_request_data(req), size);
		} break;
	}
}

void write_sdo_data(ec_sdo_request_t* req, const ecat_size_al& size, void* value)
{
	Unit64b tmp;
	memcpy(&tmp, value, size);

	switch(size){
		case 1: {
			EC_WRITE_U8(ecrt_sdo_request_data(req), tmp.byte);
		} break;

		case 2: {
			EC_WRITE_U16(ecrt_sdo_request_data(req), tmp.word);
		} break;

		case 4: {
			EC_WRITE_U32(ecrt_sdo_request_data(req), tmp.dword);
		} break;

		default: {
			EC_WRITE_U32(ecrt_sdo_request_data(req), tmp.dword);
		} break;
	}
}

int8_t sdo_request(const sdo_req_type_al& rtype, void* result, const ecat_pos_al& pos,
	const ecat_index_al& s_index, const ecat_sub_al& s_subindex,
	const ecat_size_al& size, const uint32_t& timeout, const uint8_t& verbosity)
{
	struct timespec start, current;

	ec_slave_config_t* slave;
	ec_slave_config_state_t* state;

	// SDO Request
	ec_sdo_request_t *sdo_req;

	try {
		slave = sc_slaves.at(pos);
		state = &slaves.at(pos).state;

		ecrt_slave_config_state(slave, state);
	} catch (const std::out_of_range& err) {
		if(verbosity > 0){
			fprintf(stderr, "Slave pos %d doesn't exist! (max %ld)\n",
				pos, sc_slaves.size());
		}

		return 2;
	}

#if DEBUG > 1
	fprintf(stdout,
		"%d Slave %d 0x%04x:%02x - Online %02x | OP %02x | State %02x\n",
		rtype, pos, s_index, s_subindex, state->online, state->operational,
		state->al_state);
#endif

	// TODO: Find how Busy Request never ends once it happens.
	// this condition is to prevent infinite Busy Request, but tbh I'm not sure why it happens.
	// so for now, I simply prevent creating request when slave is in INIT state
	if(state->al_state == 0x01){
		if(verbosity > 0){
			fprintf(stderr, "Slave %d 0x%04x:%02x is in INIT state! (%02x)\n",
				pos, s_index, s_subindex, state->al_state);
		}

		return 3;
	}

	if (!(sdo_req = ecrt_slave_config_create_sdo_request(slave, s_index, s_subindex, size))) {
		if(verbosity > 0){
			fprintf(stderr, "Failed to create SDO request!\n");
		}

		return 1;
	}

	if(rtype == ECAT_SDO_READ){
		ecrt_sdo_request_read(sdo_req);
	} else {
		// data should be set before requesting sdo
		write_sdo_data(sdo_req, size, result);
		ecrt_sdo_request_write(sdo_req);
	}

	ecrt_sdo_request_timeout(sdo_req, timeout);

	clock_gettime(CLOCK_MONOTONIC, &start);

	while(1){
		switch (ecrt_sdo_request_state(sdo_req)) {
			case EC_REQUEST_UNUSED:
				if(verbosity > 0){
					fprintf(stderr, "Unused request!\n");
				}
				// request was not used yet, trigger read
				ecrt_sdo_request_read(sdo_req);
				break;

			case EC_REQUEST_BUSY:
				// there's possibility the loop stuck in busy state
				// limit the loop with the timeout
				clock_gettime(CLOCK_MONOTONIC, &current);
				if((NSEC_PER_SEC * (current.tv_sec - start.tv_sec))
					+ current.tv_nsec - start.tv_nsec > (timeout * 1000000)){

					if(verbosity > 0){
						fprintf(stderr, "Timeout waiting for Busy Request!\n");
					}

					return 4;
				}
				break;

			case EC_REQUEST_SUCCESS: {
				read_sdo_data(sdo_req, size, result);
				return 0;
			} break;

			case EC_REQUEST_ERROR:{
				return -1;
			} break;
		}
	}
}

/****************************************************************************/

// The thread-safe function finalizer callback. This callback executes
// at destruction of thread-safe function, taking as arguments the finalizer
// data and threadsafe-function context.
void finalizer_callback(Napi::Env env, void *finalizeData, TsfnContext *context)
{
	// Join the thread
	context->nativeThread.join();

	// Resolve the Promise previously returned to JS via the js_create_thread method.
	context->deferred.Resolve(Napi::Boolean::New(env, true));
	delete context;
}

// The thread entry point. This takes as its arguments the specific
// threadsafe-function context created inside the main thread.
void thread_entry(TsfnContext *context) {
	auto callback = [](Napi::Env env, Napi::Function jsCallback, int8_t* data) {
		Napi::Array values = Napi::Array::New(env, IOs_length);

		for(io_size_et dmn_idx = 0; dmn_idx < IOs_length; dmn_idx++){
			Napi::Object indexValue = Napi::Object::New(env);
			indexValue.Set("position", Napi::Value::From(env, IOs[dmn_idx].position));
			indexValue.Set("index", Napi::Value::From(env, IOs[dmn_idx].index));
			indexValue.Set("subindex", Napi::Value::From(env, IOs[dmn_idx].subindex));
			indexValue.Set("value", Napi::Value::From(env, IOs[dmn_idx].value));

			values[dmn_idx] = indexValue;
		}

		jsCallback.Call({
				values,
				Napi::Number::New(env, master_state.al_states)
			});
	};

	struct timespec wakeup_time;
	int8_t ret = 0;

#if DEBUG > 0
	timespec_get(&epoch, TIME_UTC);
	fprintf(stdout, "%ld.%09ld | Program Started\n", epoch.tv_sec, epoch.tv_nsec);
#endif

	if(isMasterReady != 1){
		init_master_and_domain();
	}

	// activate master and initialize domain data
	activate_master();

	/* Set priority */
	struct sched_param param = {};
	param.sched_priority = sched_get_priority_max(SCHED_FIFO);

	if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
		perror("sched_setscheduler failed");
	}

	stack_prefault();

#if DEBUG > 0
	fprintf(stdout, "\nUsing priority %i\n", param.sched_priority);
	fprintf(stdout, "\nStarting RT task with dt=%u ns.\n", PERIOD_NS);
#endif

	clock_gettime(CLOCK_MONOTONIC, &wakeup_time);
	wakeup_time.tv_sec += 1; /* start in future */
	wakeup_time.tv_nsec = 0;

	_running_state = 1;

	while (1) {
		ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wakeup_time, NULL);

		if (ret || !_running_state) {
			fprintf(stderr, "\nBreak Cyclic Process %d (running_state %d)\n",
				ret, _running_state);
			context->tsfn.Abort();
			break;
		}

		cyclic_task(master, DomainN_length);

		napi_status status = context->tsfn.BlockingCall(&ret, callback);

		if (status != napi_ok) {
			Napi::Error::Fatal(
					"thread_entry",
					"Napi::ThreadSafeNapi::Function.BlockingCall() failed"
				);
		}

		set_next_wait_period(&wakeup_time);
	}

	ecrt_master_deactivate(master);

	// wait until OP bit is reset after deactivation
	while(MASTER_STATE_DETAIL(AL_BIT_OP, master_state.al_states)){
		// update master state
		check_master_state(master);

		set_next_wait_period(&wakeup_time);
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wakeup_time, NULL);
	}

	reset_global_vars();
	ecrt_release_master(master);
	isMasterReady = 0;

	context->tsfn.Release();
}

/****************************************************************************
 * Node API
 ****************************************************************************/

Napi::Value js_init_slave(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();

	do_sort_slave = 0;

	if (info.Length() < 1 || !info[0].IsString()){
		Napi::TypeError::New(
				env,
				"Expected 1 Parameter(s) to be passed [ String ]"
			).ThrowAsJavaScriptException();

		return env.Null();
	}

	if (slave_entries_length > 0){
		Napi::TypeError::New(
				env,
				"Slaves are already initialized!"
			).ThrowAsJavaScriptException();

		return env.Null();
	}

	if (info.Length() == 2){
		do_sort_slave = info[1].As<Napi::Boolean>() ? true : false;
	}

	json_path = info[0].As<Napi::String>();

	int8_t parsing = init_slave();

	init_master_and_domain();

	return Napi::Number::New(env, parsing);
}

Napi::Value js_create_thread(const Napi::CallbackInfo &info)
{
	Napi::Env env = info.Env();

	// Construct context data
	auto _ctx = new TsfnContext(env);

	// Create a new ThreadSafeFunction.
	_ctx->tsfn = Napi::ThreadSafeFunction::New(
			env, // Environment
			info[0].As<Napi::Function>(), // JS function from caller
			"TSFN", // Resource name
			0, // Max queue size (0 = unlimited).
			2, // Initial thread count
			_ctx, // Context,
			finalizer_callback, // Finalizer
			(void *)nullptr	// Finalizer data
		);

	_ctx->nativeThread = std::thread(thread_entry, _ctx);

	return _ctx->deferred.Promise();
}

Napi::Value js_set_frequency(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();

	uint32_t frequency = info[0].As<Napi::Number>();

	FREQUENCY = frequency;
	PERIOD_NS = NSEC_PER_SEC / FREQUENCY;

	return Napi::Number::New(env, PERIOD_NS);
}

Napi::Value set_period_us(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();

	uint32_t period_us = info[0].As<Napi::Number>();

	PERIOD_NS = period_us * 1000;
	FREQUENCY = NSEC_PER_SEC / (float(period_us) * 1000);

	return Napi::Number::New(env, PERIOD_NS);
}

Napi::Value js_get_operational_status(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();

	return Napi::Number::New(env, check_is_operational());
}

Napi::Value js_stop_thread(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();
	_running_state = 0;

	return Napi::Number::New(env, _running_state);
}

Napi::Value js_write_index(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();

	// don't execute when main task is not running
	if(!MASTER_STATE_DETAIL(AL_BIT_OP, master_state.al_states)
		|| _running_state != 1
		|| IOs_length <= 0
	){
		return Napi::Number::New(env, -1);
	}

	io_size_et dmn_idx = info[0].As<Napi::Number>();
	ecat_value_al value = info[1].As<Napi::Number>();

	IOs[dmn_idx].writtenValue = value;

	return Napi::Number::New(env, IOs[dmn_idx].writtenValue);
}

Napi::Value js_write_by_key(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();

	// don't execute when main task is not running
	if(!MASTER_STATE_DETAIL(AL_BIT_OP, master_state.al_states)
		|| _running_state != 1
		|| IOs_length <= 0
	){
		return env.Null();
	}

	ecat_pos_al pos = info[0].As<Napi::Number>().Uint32Value();
	ecat_index_al index = info[1].As<Napi::Number>().Uint32Value();
	ecat_sub_al subindex = info[2].As<Napi::Number>().Uint32Value();
	ecat_value_al value = info[3].As<Napi::Number>().Uint32Value();

	if(write_domain(pos, index, subindex, value) < 0){
		return env.Undefined();
	}

	return Napi::Boolean::New(env, true);
}

Napi::Value js_read_by_key(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();

	// don't execute when main task is not running
	if(!MASTER_STATE_DETAIL(AL_BIT_OP, master_state.al_states)
		|| _running_state != 1
		|| IOs_length <= 0
	){
		return env.Null();
	}

	ecat_pos_al pos = info[0].As<Napi::Number>().Uint32Value();
	ecat_index_al index = info[1].As<Napi::Number>().Uint32Value();
	ecat_sub_al subindex = (ecat_sub_al) info[2].As<Napi::Number>().Uint32Value();
	ecat_value_al value;

	if(read_domain(pos, index, subindex, &value) < 0){
		return env.Undefined();
	}

	return Napi::Number::New(env, value);
}

Napi::Value js_get_mapped_domains(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();

	if(mapped_domains.empty()){
		return env.Undefined();
	}

	Napi::Object js_retval = Napi::Object::New(env);
	ecat_pos_al pos;
	ecat_index_al index;
	ecat_sub_al subindex;
	bool do_print = info[0].As<Napi::Boolean>();

	for(auto elem : mapped_domains){
		pos = (elem.first >> 24) & 0xff;
		index = (elem.first >> 8) & 0xffff;
		subindex = (elem.first) & 0xff;

		char tmpstr[30];
		sprintf(tmpstr, "%d:%04x:%02x", pos, index, subindex);
		js_retval.Set(tmpstr, Napi::Number::New(env, elem.second));

		if(do_print){
			fprintf(stdout, "Pos %d at 0x%04x:%02x = %d\n", pos, index,
				subindex, elem.second);
		}
	}

	return js_retval;
}

Napi::Promise js_get_allocated_domain(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();

	auto deferred = Napi::Promise::Deferred::New(env);

	// check if domain have been allocated or not
	if(IOs_length == 0){
		deferred.Reject(
			Napi::TypeError::New(
					env,
					"Slave(s) must be configured first"
				).Value()
		);

		return deferred.Promise();
	}

	Napi::Array _domains = Napi::Array::New(env, IOs_length);
	for(io_size_et dmn_idx = 0; dmn_idx < IOs_length; dmn_idx++){
		Napi::Object item = Napi::Object::New(env);
		item.Set("position", Napi::Value::From(env, IOs[dmn_idx].position));
		item.Set("vendorId", Napi::Value::From(env, IOs[dmn_idx].vendor_id));
		item.Set("productCode", Napi::Value::From(env, IOs[dmn_idx].product_code));
		item.Set("pdoIndex", Napi::Value::From(env, IOs[dmn_idx].pdo_index));
		item.Set("index", Napi::Value::From(env, IOs[dmn_idx].index));
		item.Set("subindex", Napi::Value::From(env, IOs[dmn_idx].subindex));
		item.Set("size", Napi::Value::From(env, IOs[dmn_idx].size));
		item.Set("isEndianSwapped", Napi::Value::From(env, IOs[dmn_idx].SWAP_ENDIAN));
		item.Set("isSigned", Napi::Value::From(env, IOs[dmn_idx].SIGNED));
		item.Set("direction", Napi::Value::From(env, IOs[dmn_idx].direction));
		item.Set("value", Napi::Value::From(env, IOs[dmn_idx].value));

		_domains[dmn_idx] = item;
	}

    deferred.Resolve(_domains);

	return deferred.Promise();
}

Napi::Promise js_get_domain_values(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();

	auto deferred = Napi::Promise::Deferred::New(env);

	// check if domain have been allocated or not
	if(IOs_length == 0){
		deferred.Reject(
			Napi::TypeError::New(
					env,
					"Slave(s) must be configured first"
				).Value()
		);

		return deferred.Promise();
	}

	Napi::Array values = Napi::Array::New(env, IOs_length);

	for(io_size_et dmn_idx = 0; dmn_idx < IOs_length; dmn_idx++){
		Napi::Object indexValue = Napi::Object::New(env);
		indexValue.Set("position", Napi::Value::From(env, IOs[dmn_idx].position));
		indexValue.Set("index", Napi::Value::From(env, IOs[dmn_idx].index));
		indexValue.Set("subindex", Napi::Value::From(env, IOs[dmn_idx].subindex));
		indexValue.Set("value", Napi::Value::From(env, IOs[dmn_idx].value));

		values[dmn_idx] = indexValue;
	}

    deferred.Resolve(values);

	return deferred.Promise();
}

Napi::Value js_get_master_state(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();
	return Napi::Number::New(env, master_state.al_states);
}

Napi::Value js_sdo_request_read(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();

	ecat_pos_al pos = info[0].As<Napi::Number>().Uint32Value();
	ecat_index_al index = info[1].As<Napi::Number>().Uint32Value();
	ecat_sub_al subindex = info[2].As<Napi::Number>().Uint32Value();
	ecat_size_al size = info[3].As<Napi::Number>().Uint32Value();

	sdo_req_type_al rtype = ECAT_SDO_READ;

	uint32_t timeout = 100;
	uint8_t verbosity = 0;
	Unit64b tmp;

	if(info.Length() > 4 && info[4].IsNumber()){
		timeout = info[4].As<Napi::Number>().Uint32Value();
	}

	if(info.Length() > 5 && info[5].IsNumber()){
		verbosity = info[5].As<Napi::Number>().Uint32Value();
	}

	if(sdo_request(rtype, &tmp, pos, index, subindex, size, timeout, verbosity)){
		return env.Undefined();
	}

	switch(size){
		case 1: return Napi::Number::New(env, tmp.byte);
		case 2: return Napi::Number::New(env, tmp.word);
		case 4: return Napi::Number::New(env, tmp.dword);
		default: return Napi::Buffer<uint8_t>::Copy(env, tmp.array, size);
	}
}

Napi::Value js_sdo_request_write(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();

	Unit64b tmp;
	tmp.dword = info[0].As<Napi::Number>().Uint32Value();

	ecat_pos_al pos = info[1].As<Napi::Number>().Uint32Value();
	ecat_index_al index = info[2].As<Napi::Number>().Uint32Value();
	ecat_sub_al subindex = info[3].As<Napi::Number>().Uint32Value();
	ecat_size_al size = info[4].As<Napi::Number>().Uint32Value();

	sdo_req_type_al rtype = ECAT_SDO_WRITE;

	uint32_t timeout = 100;
	uint8_t verbosity = 0;
	if(info.Length() > 5 && info[5].IsNumber()){
		timeout = info[5].As<Napi::Number>().Uint32Value();
	}

	if(info.Length() > 6 && info[6].IsNumber()){
		verbosity = info[6].As<Napi::Number>().Uint32Value();
	}

	if(sdo_request(rtype, &tmp, pos, index, subindex, size, timeout, verbosity)){
		return env.Undefined();
	}

	switch(size){
		case 1: return Napi::Number::New(env, tmp.byte);
		case 2: return Napi::Number::New(env, tmp.word);
		case 4: return Napi::Number::New(env, tmp.dword);
		default: return Napi::Buffer<uint8_t>::Copy(env, tmp.array, size);
	}
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
	exports.Set(Napi::String::New(env, "init"), Napi::Function::New(env, js_init_slave));
	exports.Set(Napi::String::New(env, "writeIndex"), Napi::Function::New(env, js_write_index));
	exports.Set(Napi::String::New(env, "isOperational"), Napi::Function::New(env, js_get_operational_status));
	exports.Set(Napi::String::New(env, "start"), Napi::Function::New(env, js_create_thread));
	exports.Set(Napi::String::New(env, "stop"), Napi::Function::New(env, js_stop_thread));
	exports.Set(Napi::String::New(env, "getAllocatedDomain"), Napi::Function::New(env, js_get_allocated_domain));
	exports.Set(Napi::String::New(env, "getMasterState"), Napi::Function::New(env, js_get_master_state));
	exports.Set(Napi::String::New(env, "getDomainValues"), Napi::Function::New(env, js_get_domain_values));
	exports.Set(Napi::String::New(env, "setFrequency"), Napi::Function::New(env, js_set_frequency));
	exports.Set(Napi::String::New(env, "writeDomain"), Napi::Function::New(env, js_write_by_key));
	exports.Set(Napi::String::New(env, "readDomain"), Napi::Function::New(env, js_read_by_key));
	exports.Set(Napi::String::New(env, "getMappedDomains"), Napi::Function::New(env, js_get_mapped_domains));
	exports.Set(Napi::String::New(env, "sdoRead"), Napi::Function::New(env, js_sdo_request_read));
	exports.Set(Napi::String::New(env, "sdoWrite"), Napi::Function::New(env, js_sdo_request_write));

	return exports;
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init);
