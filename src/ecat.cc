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
static int8_t DomainN_length;

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
static uint8_t slaves_length = 0;

static std::vector<slaveEntry> IOs;
static uint8_t IOs_length = 0;

static std::vector<slaveEntry> slave_entries;
static uint8_t slave_entries_length = 0;

static std::vector<startupConfig> startup_parameters;
static uint8_t startup_parameters_length = 0;

// SM startup config
inline static uint32_t _convert_index_sub_size(uint16_t index, uint8_t subindex, uint8_t size);

// mapped domain
static std::map<uint32_t, uint32_t> mapped_domains;
inline static uint32_t _convert_pos_index_sub(const uint8_t& s_position, const uint16_t& s_index, const uint8_t& s_subindex);
static int8_t get_domain_index(uint32_t* index, const uint8_t& s_position, const uint16_t& s_index, const uint8_t& s_subindex);
void assign_domain_identifier();

// Periodic task timing
static uint16_t FREQUENCY = 1000;
static uint32_t PERIOD_NS = NSEC_PER_SEC / FREQUENCY;

// SDO Request
static ec_sdo_request_t *sdo_req;

// configuration
static std::string json_path;
static bool do_sort_slave;

/*****************************************************************************/

void check_domain_state(void){
	ec_domain_state_t ds;

	ecrt_domain_state(DomainN, &ds);

#if DEBUG > 0
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

/*****************************************************************************/

void check_master_state(ec_master_t *master){
	ec_master_state_t ms;

	ecrt_master_state(master, &ms);

#if DEBUG > 0
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

void check_slave_config_states(void){
	for(uint8_t slNumber = 0; slNumber < slaves_length; slNumber++){
		ec_slave_config_state_t s;

		ecrt_slave_config_state(sc_slaves[slNumber], &s);

#if DEBUG > 0
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

/*****************************************************************************/

uint8_t check_is_operational(){
	for(uint8_t slNumber = 0; slNumber < slaves_length; slNumber++){
		if(!slaves[slNumber].state.operational){
			return 0;
		}
	}

	return 1;
}

int8_t write_output_value(uint8_t index, uint32_t value, uint8_t SIGNED){
	// selected index is not output, return immediately
	if(IOs[index].direction != EC_DIR_OUTPUT){
		return -1;
	}

	switch(IOs[index].size){
		case 1:
			EC_WRITE_BIT(
					DomainN_pd + IOs[index].offset,
					IOs[index].bit_position,
					(uint8_t) value & 0x1
				);
			return 1;
			break;

		case 8:
			EC_WRITE_S8(
					DomainN_pd + IOs[index].offset,
					SIGNED ? (int8_t) value : (uint8_t) value
				);
			break;

		case 16:
			EC_WRITE_S16(
					DomainN_pd + IOs[index].offset,
					SIGNED ? (int16_t) value : (int16_t) value
				);
			break;

		case 32:
			EC_WRITE_U32(
					DomainN_pd + IOs[index].offset,
					SIGNED ? (int32_t) value : (uint32_t) value
				);
			break;

		default:
			return 0;
			break;
	}

	return 1;
}

/*****************************************************************************/

void cyclic_task(ec_master_t *master, uint8_t length){
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

		for(uint8_t index = 0; index < length; index++){
			if(IOs[index].direction == EC_DIR_OUTPUT){
				write_output_value(
						index,
						IOs[index].writtenValue,
						IOs[index].SIGNED
					);
			}

			switch(IOs[index].size){
				// No endian difference for 1 bit variable
				case 1:
					IOs[index].value = (uint32_t) EC_READ_BIT(
											DomainN_pd + IOs[index].offset,
											IOs[index].bit_position
										);
					break;

				// No endian difference for 1 byte variable
				case 8:
					IOs[index].value = (uint32_t) EC_READ_U8(DomainN_pd + IOs[index].offset);
					break;

				case 16:
					tmp16 = EC_READ_U16(DomainN_pd + IOs[index].offset);
					IOs[index].value = (uint32_t) IOs[index].SWAP_ENDIAN
											? swap_endian16(tmp16)
											: tmp16;
					break;

				case 32:
					tmp32 = EC_READ_U32(DomainN_pd + IOs[index].offset);
					IOs[index].value = (uint32_t) IOs[index].SWAP_ENDIAN
											? swap_endian32(tmp32)
											: tmp32;
					break;

				default:
					tmp32 = (*((uint32_t *)(DomainN_pd + IOs[index].offset)))
														& mask(IOs[index].size);
					IOs[index].value = (uint32_t) IOs[index].SWAP_ENDIAN
											? swap_endian32(tmp32)
											: tmp32;
					break;
			}

#if DEBUG > 1
			printf("Index %2d 0x%04x:%02x = %04x\n", index, IOs[index].index,
										IOs[index].subindex, IOs[index].value);
#endif

		};

#if DEBUG > 1
		printf("=====================\n");
#endif

	}

	ecrt_domain_queue(DomainN);
	ecrt_master_send(master);
}

int8_t domain_startup_config(
				ec_pdo_entry_reg_t **DomainN_regs, int8_t *DomainN_length){
#if DEBUG > 0
	fprintf(stdout, "\nConfiguring Domain...\n");
#endif

	uint8_t length = slave_entries_length;
	uint8_t i, index;
	size_t size;

	if((*DomainN_length) == 0){
		*DomainN_regs = (ec_pdo_entry_reg_t*) malloc(sizeof(ec_pdo_entry_reg_t));
	}

	// find length of valid domain inside slave_entries
	*DomainN_length = 0;
	for(i = 0; i < length; i++){
		if(slave_entries[i].add_to_domain){
			(*DomainN_length)++;
		}
	}

	// new size to be allocated into DomainN_regs
	size = ((*DomainN_length) + 1) * sizeof(ec_pdo_entry_reg_t);

	// reallocate domain_regs
	*DomainN_regs = (ec_pdo_entry_reg_t*) realloc(*DomainN_regs, size);

	// reserve vector memory allocation,
	// in order to avoid pointer address change everytime we push_back new value
	IOs.reserve(length);

	// add every valid slave process data into domain
	for(i = 0, index = 0; i < length; i++){
		if(slave_entries[i].add_to_domain){
			// create IOs domain to access domain value
			IOs.push_back({
					slave_entries[i].alias,
					slave_entries[i].position,
					slave_entries[i].vendor_id,
					slave_entries[i].product_code,
					slave_entries[i].sync_index,
					slave_entries[i].pdo_index,
					slave_entries[i].index,
					slave_entries[i].subindex,
					slave_entries[i].size,
					slave_entries[i].add_to_domain,
					slave_entries[i].offset,
					slave_entries[i].bit_position,
					slave_entries[i].value,
					slave_entries[i].direction,
					slave_entries[i].SWAP_ENDIAN,
					slave_entries[i].SIGNED,
					slave_entries[i].writtenValue,
					slave_entries[i].WATCHDOG_ENABLED
				});

			// register domain with IOs
			(*DomainN_regs)[index] = {
					IOs[index].alias,
					IOs[index].position,
					IOs[index].vendor_id,
					IOs[index].product_code,
					IOs[index].index,
					IOs[index].subindex,
					&IOs[index].offset,
					&IOs[index].bit_position
				};

#if DEBUG > 0
			printf("Domain %3d : Slave%2d 0x%04x:%02x\n",
					index,
					(*DomainN_regs)[index].position,
					(*DomainN_regs)[index].index,
					(*DomainN_regs)[index].subindex
				);
#endif

			index++;
		}
	}

	// terminate with an empty structure
	(*DomainN_regs)[(*DomainN_length)] = {};

	return (*DomainN_length);
}

uint32_t _convert_index_sub_size(uint16_t index, uint8_t subindex, uint8_t size){
	return ((index & 0xffff) << 16) | ((subindex & 0xff) << 8) | (size & 0xff);
}

void syncmanager_startup_config(){
#if DEBUG > 0
	fprintf(stdout, "\nConfiguring SyncManager and Mapping PDOs...\n");
#endif

	uint8_t length = slave_entries_length;
	uint8_t i;

	uint16_t last_position = -1,
		last_pdo_index = -1;
	uint32_t last_index_sub_size = -1;

	uint8_t syncM_index = 0, last_syncM_index = -1;
	ec_direction_t direction;
	ec_watchdog_mode_t watchdog_mode;

	uint16_t current_position = -1,
		current_pdo_index = -1,
		current_index = -1;
	uint8_t current_subindex = -1;

	uint32_t processed_index_sub_size = -1;

	// add every valid slave and configure sync manager
	for(i = 0; i < length; i++){
		processed_index_sub_size = _convert_index_sub_size(
										slave_entries[i].index,
										slave_entries[i].subindex,
										slave_entries[i].size
									);
		syncM_index = slave_entries[i].sync_index;

#if DEBUG > 0
		printf("\nSlave%2d 0x%04x 0x%08x %02d-bits\n",
				slave_entries[i].position, slave_entries[i].pdo_index,
				processed_index_sub_size, slave_entries[i].size);
#endif

		// invalid pdo index use default configuration, continue to next index
		if(last_position != slave_entries[i].position
												&& slave_entries[i].pdo_index){
			// reset last SM index when encountering new slave
			last_syncM_index = -1;

			direction = (ec_direction_t) slave_entries[i].direction;
			watchdog_mode = slave_entries[i].WATCHDOG_ENABLED
						? EC_WD_ENABLE
						: EC_WD_DISABLE;
			current_position = slave_entries[i].position;

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

		if(last_syncM_index != syncM_index && slave_entries[i].pdo_index){
			ecrt_slave_config_pdo_assign_clear(
							sc_slaves[current_position],
							syncM_index
						);

#if DEBUG > 0
			printf("clear PDO assign                     : Slave%2d SM%d\n",
												current_position, syncM_index);
#endif

			last_syncM_index = syncM_index;
		}

		// invalid pdo index use default configuration, continue to next index
		if(last_pdo_index != slave_entries[i].pdo_index && slave_entries[i].pdo_index){

			current_pdo_index = slave_entries[i].pdo_index;

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
			current_index = slave_entries[i].index;
			current_subindex = slave_entries[i].subindex;

#if DEBUG > 0
			printf("add PDO mapping                      : Slave%2d SM%d 0x%4x 0x%04x:%02x %2d\n",
				current_position, syncM_index, current_pdo_index, current_index,
									current_subindex, slave_entries[i].size);
#endif

			int8_t mapping = ecrt_slave_config_pdo_mapping_add(
					sc_slaves[current_position],
					current_pdo_index,
					current_index,
					current_subindex,
					slave_entries[i].size
				);

			if(mapping){
				fprintf(stderr,
					"Failed to add PDO mapping. Slave%2d SM%d 0x%4x 0x%04x:0x%02x %2d\n",
					current_position, syncM_index, current_pdo_index,
					current_index, current_subindex, slave_entries[i].size);

				exit(EXIT_FAILURE);
			}

			last_index_sub_size = _convert_index_sub_size(
										slave_entries[i].index,
										slave_entries[i].subindex,
										slave_entries[i].size
									);
		}
	}
}

uint8_t _skip_current_slave_position(uint16_t position,
										std::vector<uint8_t>& last_positions){

	for(uint8_t sl_index = 0; sl_index < last_positions.size(); sl_index++){
		if(position == last_positions[sl_index]){
			return 1;
			break;
		}
	}

	return 0;
}

void slave_startup_config(ec_master_t *master){
#if DEBUG > 0
	fprintf(stdout, "\nConfiguring Slaves...\n");
#endif

	uint8_t length = slave_entries_length;
	std::vector<uint8_t> last_positions;

	for(uint8_t i = 0; i < length; i++){
		// skip current slave, if it's already configured
		if(_skip_current_slave_position(
								slave_entries[i].position,
								last_positions
		)){
			continue;
		}

		// current slave
		slaveConfig current = {
				slave_entries[i].alias,
				slave_entries[i].position,
				slave_entries[i].vendor_id,
				slave_entries[i].product_code,
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
			slave_entries[i].position, slave_entries[i].pdo_index,
			slave_entries[i].index, slave_entries[i].subindex);
#endif
	}
}

void startup_parameters_config(){
#if DEBUG > 0
	fprintf(stdout, "\nConfiguring Startup Parameters...\n");
#endif

	static uint8_t length = startup_parameters_length;
	for(int idx = 0; idx < length; idx++){
		switch(startup_parameters[idx].size){
			case 8:
				ecrt_slave_config_sdo8(
						sc_slaves[startup_parameters[idx].slavePosition],
						startup_parameters[idx].index,
						startup_parameters[idx].subindex,
						(uint8_t) startup_parameters[idx].value
					);
				break;

			case 16:
				ecrt_slave_config_sdo16(
						sc_slaves[startup_parameters[idx].slavePosition],
						startup_parameters[idx].index,
						startup_parameters[idx].subindex,
						(uint16_t) startup_parameters[idx].value
					);
				break;

			default:
				ecrt_slave_config_sdo32(
						sc_slaves[startup_parameters[idx].slavePosition],
						startup_parameters[idx].index,
						startup_parameters[idx].subindex,
						(uint32_t) startup_parameters[idx].value
					);
				break;
		}

#if DEBUG > 0
		printf("Set Startup Parameter Slave%2d, 0x%04x:%02x = 0x%x (%d)\n",
				startup_parameters[idx].slavePosition,
				startup_parameters[idx].index,
				startup_parameters[idx].subindex,
				startup_parameters[idx].value,
				startup_parameters[idx].value
			);
#endif

	}
}

void reset_global_vars(void){
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

void stack_prefault(void){
	unsigned char dummy[MAX_SAFE_STACK];
	memset(dummy, 0, MAX_SAFE_STACK);
}

void _wait_period(struct timespec * wakeup_time){
	wakeup_time->tv_nsec += PERIOD_NS;
	while (wakeup_time->tv_nsec >= NSEC_PER_SEC) {
		wakeup_time->tv_nsec -= NSEC_PER_SEC;
		wakeup_time->tv_sec++;
	}
}

void assign_domain_identifier(){
#if DEBUG > 0
	printf("\nAssigning Domain identifier...\n");
#endif

	for (uint8_t domain_index = 0; domain_index < IOs_length; domain_index++){
		uint32_t identifier = _convert_pos_index_sub(
				IOs[domain_index].position,
				IOs[domain_index].index,
				IOs[domain_index].subindex
			);

		mapped_domains[identifier] = domain_index;

#if DEBUG > 0
		printf("DOMAIN Pos %2d 0x%04x:%02x : %x index %d\n", IOs[domain_index].position,
				IOs[domain_index].index,
				IOs[domain_index].subindex,
				identifier,
				mapped_domains[identifier]);
#endif
	}
}

inline static uint32_t _convert_pos_index_sub(const uint8_t& s_position, const uint16_t& s_index, const uint8_t& s_subindex){
	return (s_position << 24) | (s_index << 8) | (s_subindex << 0);
}

static int8_t get_domain_index(uint32_t* index, const uint8_t& s_position, const uint16_t& s_index, const uint8_t& s_subindex){
	uint32_t key = _convert_pos_index_sub(s_position, s_index, s_subindex);

	try {
		*index = mapped_domains.at(key);
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

int8_t write_domain(const uint8_t& s_position, const uint16_t& s_index, const uint8_t& s_subindex, const uint32_t& value){
	if(!check_is_operational()){
		return -1;
	}

	uint32_t index;
	if(get_domain_index(&index, s_position, s_index, s_subindex) < 0){
		return -1;
	}

	IOs[index].writtenValue = value;

	return 0;
}

int8_t read_domain(const uint8_t& s_position, const uint16_t& s_index, const uint8_t& s_subindex, uint32_t* value){
	if(!check_is_operational()){
		return -1;
	}

	uint32_t index;
	if(get_domain_index(&index, s_position, s_index, s_subindex) < 0){
		return -1;
	}

	*value = IOs[index].value;

	return 0;
}

/****************************************************************************/
// Data structure representing our thread-safe function context.
struct TsfnContext {
	TsfnContext(Napi::Env env) : deferred(Napi::Promise::Deferred::New(env)) {};

	// Native Promise returned to JavaScript
	Napi::Promise::Deferred deferred;

	// Native thread
	std::thread nativeThread;

	Napi::ThreadSafeFunction tsfn;
};

// The thread-safe function finalizer callback. This callback executes
// at destruction of thread-safe function, taking as arguments the finalizer
// data and threadsafe-function context.
void finalizer_callback(Napi::Env env, void *finalizeData, TsfnContext *context) {
	// Join the thread
	context->nativeThread.join();

	// Resolve the Promise previously returned to JS via the create_tsfn method.
	context->deferred.Resolve(Napi::Boolean::New(env, true));
	delete context;
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
	DomainN_regs = NULL;
	master = NULL;
	DomainN_length = 0;
	printf("slave length %s, %d\n", "init_master_and_domain", slave_entries_length);

#if DEBUG > 0
	timespec_get(&epoch, TIME_UTC);
	fprintf(stdout, "%ld.%09ld | Program Started\n", epoch.tv_sec, epoch.tv_nsec);
#endif

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
	IOs_length = domain_startup_config(&DomainN_regs, &DomainN_length);

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

#if DEBUG > 0
	for(uint8_t _idx = 0; _idx < IOs_length; _idx++){
		printf("\t > Domain %3d : Slave%2d 0x%04x:%02x offset %2d\n",
				_idx,
				IOs[_idx].position,
				IOs[_idx].index,
				IOs[_idx].subindex,
				IOs[_idx].offset
			);
	}
#endif

	// free allocated memories from startup configurations
	free(DomainN_regs);

	// map domain indexes
	assign_domain_identifier();

#if DEBUG > 0
	fprintf(stdout, "\nMaster & Domain have been initialized.\n");
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

// The thread entry point. This takes as its arguments the specific
// threadsafe-function context created inside the main thread.
void thread_entry(TsfnContext *context) {
	auto callback = [](Napi::Env env, Napi::Function jsCallback, int8_t* data) {
		Napi::Array values = Napi::Array::New(env, IOs_length);

		for(uint8_t index = 0; index < IOs_length; index++){
			Napi::Object indexValue = Napi::Object::New(env);
			indexValue.Set("position", Napi::Value::From(env, IOs[index].position));
			indexValue.Set("index", Napi::Value::From(env, IOs[index].index));
			indexValue.Set("subindex", Napi::Value::From(env, IOs[index].subindex));
			indexValue.Set("value", Napi::Value::From(env, IOs[index].value));

			values[index] = indexValue;
		}

		jsCallback.Call({
				values,
				Napi::Number::New(env, master_state.al_states)
			});
	};

	struct timespec wakeup_time;
	int8_t ret = 0;

	if(isMasterReady != 1){
#if DEBUG > 0
	fprintf(stdout, "\nInitialize Master and Domains\n");
#endif
		init_master_and_domain();
	}

	// activate master and initialize domain data
	activate_master();

	/* Set priority */
	struct sched_param param = {};
	param.sched_priority = sched_get_priority_max(SCHED_FIFO);

	fprintf(stdout, "\nUsing priority %i\n", param.sched_priority);
	if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
		perror("sched_setscheduler failed");
	}

	stack_prefault();

#if DEBUG > 0
	fprintf(stdout, "\nStarting RT task with dt=%u ns.\n", PERIOD_NS);
#endif

	clock_gettime(CLOCK_MONOTONIC, &wakeup_time);
	wakeup_time.tv_sec += 1; /* start in future */
	wakeup_time.tv_nsec = 0;

	_running_state = 1;

	while (1) {
		ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wakeup_time, NULL);

		if (ret || !_running_state) {
			fprintf(stderr, "\nBreak Cyclic Process %d (running_state %d)\n", ret, _running_state);
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

		_wait_period(&wakeup_time);
	}

	ecrt_master_deactivate(master);

	// wait until OP bit is reset after deactivation
	while(MASTER_STATE_DETAIL(AL_BIT_OP, master_state.al_states)){
		// update master state
		check_master_state(master);
		_wait_period(&wakeup_time);
	}

	reset_global_vars();
	ecrt_release_master(master);
	isMasterReady = 0;

	context->tsfn.Release();
}

/****************************************************************************/

Napi::Value js_init_slave(const Napi::CallbackInfo& info) {
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

	printf("slave length %s %d\n", "init_slave", slave_entries_length);

	init_master_and_domain();

	return Napi::Number::New(env, parsing);
}

Napi::Value create_tsfn(const Napi::CallbackInfo &info) {
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

Napi::Value set_frequency(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();

	uint32_t frequency = info[0].As<Napi::Number>();

	FREQUENCY = frequency;
	PERIOD_NS = NSEC_PER_SEC / FREQUENCY;

	return Napi::Number::New(env, PERIOD_NS);
}

Napi::Value set_period_us(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();

	uint32_t period_us = info[0].As<Napi::Number>();

	PERIOD_NS = period_us * 1000;
	FREQUENCY = NSEC_PER_SEC / (float(period_us) * 1000);

	return Napi::Number::New(env, PERIOD_NS);
}

Napi::Value get_operational_status(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();

	return Napi::Number::New(env, check_is_operational());
}

Napi::Value stop(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();
	_running_state = 0;

	return Napi::Number::New(env, _running_state);
}

Napi::Value write_index(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();

	// don't execute when main task is not running
	if(!MASTER_STATE_DETAIL(AL_BIT_OP, master_state.al_states)
		|| _running_state != 1
		|| IOs_length <= 0
	){
		return Napi::Number::New(env, -1);
	}

	uint32_t index = info[0].As<Napi::Number>();
	uint32_t value = info[1].As<Napi::Number>();

	IOs[index].writtenValue = value;

	return Napi::Number::New(env, IOs[index].writtenValue);
}

Napi::Value js_write_by_key(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();

	// don't execute when main task is not running
	if(!MASTER_STATE_DETAIL(AL_BIT_OP, master_state.al_states)
		|| _running_state != 1
		|| IOs_length <= 0
	){
		return env.Null();
	}

	uint8_t pos = info[0].As<Napi::Number>().Uint32Value();
	uint16_t index = info[1].As<Napi::Number>().Uint32Value();
	uint8_t subindex = info[2].As<Napi::Number>().Uint32Value();
	uint32_t value = info[3].As<Napi::Number>().Uint32Value();

	if(write_domain(pos, index, subindex, value) < 0){
		return env.Undefined();
	}

	return Napi::Boolean::New(env, true);
}

Napi::Value js_read_by_key(const Napi::CallbackInfo& info) {
	Napi::Env env = info.Env();

	// don't execute when main task is not running
	if(!MASTER_STATE_DETAIL(AL_BIT_OP, master_state.al_states)
		|| _running_state != 1
		|| IOs_length <= 0
	){
		return env.Null();
	}

	uint8_t pos = info[0].As<Napi::Number>().Uint32Value();
	uint16_t index = info[1].As<Napi::Number>().Uint32Value();
	uint8_t subindex = (uint8_t) info[2].As<Napi::Number>().Uint32Value();
	uint32_t value;

	if(read_domain(pos, index, subindex, &value) < 0){
		return env.Undefined();
	}

	return Napi::Number::New(env, value);
}

Napi::Value js_get_mapped_domains(const Napi::CallbackInfo& info){
	Napi::Env env = info.Env();

	if(mapped_domains.empty()){
		return env.Undefined();
	}

	Napi::Object js_retval = Napi::Object::New(env);
	uint8_t pos, subindex;
	uint16_t index;
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

Napi::Promise get_allocated_domain(const Napi::CallbackInfo& info) {
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
	for(uint8_t index = 0; index < IOs_length; index++){
		Napi::Object item = Napi::Object::New(env);
		item.Set("position", Napi::Value::From(env, IOs[index].position));
		item.Set("vendorId", Napi::Value::From(env, IOs[index].vendor_id));
		item.Set("productCode", Napi::Value::From(env, IOs[index].product_code));
		item.Set("pdoIndex", Napi::Value::From(env, IOs[index].pdo_index));
		item.Set("index", Napi::Value::From(env, IOs[index].index));
		item.Set("subindex", Napi::Value::From(env, IOs[index].subindex));
		item.Set("size", Napi::Value::From(env, IOs[index].size));
		item.Set("isEndianSwapped", Napi::Value::From(env, IOs[index].SWAP_ENDIAN));
		item.Set("isSigned", Napi::Value::From(env, IOs[index].SIGNED));
		item.Set("direction", Napi::Value::From(env, IOs[index].direction));
		item.Set("value", Napi::Value::From(env, IOs[index].value));

		_domains[index] = item;
	}

    deferred.Resolve(_domains);

	return deferred.Promise();
}

Napi::Promise get_domain_values(const Napi::CallbackInfo& info){
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

	for(uint8_t index = 0; index < IOs_length; index++){
		Napi::Object indexValue = Napi::Object::New(env);
		indexValue.Set("position", Napi::Value::From(env, IOs[index].position));
		indexValue.Set("index", Napi::Value::From(env, IOs[index].index));
		indexValue.Set("subindex", Napi::Value::From(env, IOs[index].subindex));
		indexValue.Set("value", Napi::Value::From(env, IOs[index].value));

		values[index] = indexValue;
	}

    deferred.Resolve(values);

	return deferred.Promise();
}

Napi::Value get_master_state(const Napi::CallbackInfo& info){
	Napi::Env env = info.Env();

	return Napi::Number::New(env, master_state.al_states);
}

void set_sdo_data(const Napi::Env& env, const size_t& size, Napi::Value* value)
{
	switch(size){
		case 1: {
			*value = Napi::Number::New(env,
				EC_READ_U8(ecrt_sdo_request_data(sdo_req)));
		} break;

		case 2: {
			*value = Napi::Number::New(env,
				EC_READ_U16(ecrt_sdo_request_data(sdo_req)));
		} break;

		case 4: {
			*value = Napi::Number::New(env,
				EC_READ_U32(ecrt_sdo_request_data(sdo_req)));
		} break;

		default: {
			*value = Napi::Number::New(env,
				EC_READ_U32(ecrt_sdo_request_data(sdo_req)));
		} break;
	}
}

Napi::Value js_sdo_request_read(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();

	uint32_t pos = info[0].As<Napi::Number>().Uint32Value();
	uint16_t index = info[1].As<Napi::Number>().Uint32Value();
	uint8_t subindex = info[2].As<Napi::Number>().Uint32Value();
	size_t size = info[3].As<Napi::Number>().Uint32Value();
	uint32_t timeout = 100;
	uint8_t verbosity = 0;
	Napi::Value result;

	struct timespec start, current;

	ec_slave_config_t* slave;
	ec_slave_config_state_t state;

	if(info.Length() > 4 && info[4].IsNumber()){
		timeout = info[4].As<Napi::Number>().Uint32Value();
	}

	if(info.Length() > 5 && info[5].IsNumber()){
		verbosity = info[5].As<Napi::Number>().Uint32Value();
	}

	try {

		slave = sc_slaves.at(pos);
		ecrt_slave_config_state(slave, &state);

	} catch (const std::out_of_range& err) {
		if(verbosity > 0){
			fprintf(stderr, "Slave pos %d doesn't exist! (max %ld)\n",
				pos, sc_slaves.size());
		}

		return env.Undefined();
	}

#if DEBUG > 1
	fprintf(stdout,
		"Slave %d 0x%04x:%02x - Online %02x | OP %02x | State %02x\n",
		pos, index, subindex, state.online, state.operational, state.al_state);
#endif

	// if slave is not operational, then sdo request state will be in busy
	// until it's operational.
	if(state.operational != 0x01){
		if(verbosity > 0){
			fprintf(stderr, "Slave %d 0x%04x:%02x is not OP! (%02x)\n",
				pos, index, subindex, state.online);
		}

		return env.Undefined();
	}

	if (!(sdo_req = ecrt_slave_config_create_sdo_request(slave, index, subindex, size))) {
		if(verbosity > 0){
			fprintf(stderr, "Failed to create SDO request!\n");
		}

		return env.Undefined();
	}

	// trigger read
	ecrt_sdo_request_read(sdo_req);
	ecrt_sdo_request_timeout(sdo_req, timeout);

	clock_gettime(CLOCK_MONOTONIC, &start);

	while(1){
		switch (ecrt_sdo_request_state(sdo_req)) {
			case EC_REQUEST_UNUSED:
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

					return env.Undefined();
				}
				break;

			case EC_REQUEST_SUCCESS: {
				set_sdo_data(env, size, &result);
				return result;
			} break;

			case EC_REQUEST_ERROR:{
				return env.Undefined();
			} break;
		}
	}
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
	exports.Set(Napi::String::New(env, "init"), Napi::Function::New(env, js_init_slave));
	exports.Set(Napi::String::New(env, "writeIndex"), Napi::Function::New(env, write_index));
	exports.Set(Napi::String::New(env, "isOperational"), Napi::Function::New(env, get_operational_status));
	exports.Set(Napi::String::New(env, "start"), Napi::Function::New(env, create_tsfn));
	exports.Set(Napi::String::New(env, "stop"), Napi::Function::New(env, stop));
	exports.Set(Napi::String::New(env, "getAllocatedDomain"), Napi::Function::New(env, get_allocated_domain));
	exports.Set(Napi::String::New(env, "getMasterState"), Napi::Function::New(env, get_master_state));
	exports.Set(Napi::String::New(env, "getDomainValues"), Napi::Function::New(env, get_domain_values));
	exports.Set(Napi::String::New(env, "setFrequency"), Napi::Function::New(env, set_frequency));
	exports.Set(Napi::String::New(env, "writeDomain"), Napi::Function::New(env, js_write_by_key));
	exports.Set(Napi::String::New(env, "readDomain"), Napi::Function::New(env, js_read_by_key));
	exports.Set(Napi::String::New(env, "getMappedDomains"), Napi::Function::New(env, js_get_mapped_domains));
	exports.Set(Napi::String::New(env, "sdoRead"), Napi::Function::New(env, js_sdo_request_read));

	return exports;
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init);
