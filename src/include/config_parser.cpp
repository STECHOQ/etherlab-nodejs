#include "config_parser.h"

off_t get_filesize(const char *filename) {
    struct stat st;

    if (stat(filename, &st) == 0){
        return st.st_size;
	}

    return -1;
}

std::string normalize_hex_string(std::string str){
	std::string output;
	output.reserve(str.size()); // optional, avoids buffer reallocations in the loop
	for(size_t i = 0; i < str.size(); ++i){
		if(isxdigit(str[i])){
			output += str[i];
		}
	}

	return output;
}

uint32_t _to_uint(const rapidjson::Value& val){
	if(val.IsString()){
		return std::stoi(normalize_hex_string(val.GetString()), 0, 16);
	} else {
		return val.GetUint();
	}
}

uint8_t member_is_valid_array(const rapidjson::Value& doc, const char *name){
	if(!doc.HasMember(name)){
		return 0;
	}

	if(!doc[name].IsArray()){
		return 0;
	}

	return doc[name].Size() > 0 ? 1 : 0;
}

int8_t parse_json_file(const char *filename, slaveEntry **slave_entries, uint8_t *slave_length,
					startupConfig **slave_parameters, uint8_t *parameters_length,
					uint8_t do_sort_slave){

	const off_t filesize = get_filesize(filename);

	if(filesize == -1){
		fprintf(stderr, "filesize error : %s\n", filename);
		return filesize;
	}

	FILE *fp = fopen(filename, "r");

	if(fp == NULL){
		fprintf(stderr, "File is not accessible : %s", filename);
		return -1;
	}

	char readBuffer[filesize];
	rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));

	rapidjson::Document document;
	document.ParseStream(is);

	fclose(fp);
	assert(document.IsArray());

	/* allocate memory if length is still 0 */
	if(*slave_length == 0){
		*slave_entries = (slaveEntry*) malloc(sizeof(slaveEntry));
	}
	*slave_length = 0;

	if(*parameters_length == 0){
		*slave_parameters = (startupConfig*) malloc(sizeof(startupConfig));
	}
	*parameters_length = 0;
	/* ************************************ */

	for (uint8_t i_slaves = 0, crnt_idx_slaves = -1; i_slaves < document.Size(); i_slaves++){
		rapidjson::Value m_slaves = document[i_slaves].GetObject();

		assert(m_slaves.HasMember("alias"));
		assert(m_slaves.HasMember("position"));
		assert(m_slaves.HasMember("vendor_id"));
		assert(m_slaves.HasMember("product_code"));

		uint16_t alias = _to_uint(m_slaves["alias"]);
		uint16_t position = _to_uint(m_slaves["position"]);
		uint32_t vendor_id = _to_uint(m_slaves["vendor_id"]);
		uint32_t product_code = _to_uint(m_slaves["product_code"]);

		// add new slave entry if slave doesnt have syncs
		if(!member_is_valid_array(m_slaves, "syncs")){
			crnt_idx_slaves = (*slave_length)++;

			*slave_entries = (slaveEntry*) realloc(*slave_entries, (*slave_length) * sizeof(slaveEntry));

			(*slave_entries)[crnt_idx_slaves] = {
					alias,
					position,
					vendor_id,
					product_code,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0
				};

			continue;
		}

		assert(m_slaves["syncs"].IsArray());

		for(uint8_t i_syncs = 0; i_syncs < m_slaves["syncs"].Size(); i_syncs++){
			rapidjson::Value m_syncs = m_slaves["syncs"][i_syncs].GetObject();
			assert(m_syncs.HasMember("index"));
			assert(m_syncs.HasMember("pdos"));
			assert(m_syncs["pdos"].IsArray());

			uint16_t sync_index = _to_uint(m_syncs["index"]);
			uint8_t watchdog_enabled = 0;
			uint8_t direction = SyncMEthercatDirection[sync_index];

			if(m_syncs.HasMember("watchdog_enabled")){
				assert(m_syncs["watchdog_enabled"].IsBool());
				watchdog_enabled = (uint8_t) m_syncs["watchdog_enabled"].GetBool();
			}

			// override default 'direction' value if it's defined
			if(m_syncs.HasMember("direction")){
				assert(m_syncs["direction"].IsString());

				std::string sync_direction = m_syncs["direction"].GetString();

				if(sync_direction == "input"){
					direction = EC_DIR_INPUT;
				} else if(sync_direction == "output"){
					direction = EC_DIR_OUTPUT;
				} else {
					throw std::invalid_argument(
						"\"" + sync_direction + "\" is invalid value. "
						+ "'direction' value must be \"input\" or \"output\""
					);
				}
			}

			rapidjson::Value arr_pdos = m_syncs["pdos"].GetArray();
			uint8_t size_arr_pdos = arr_pdos.Size();

			for(uint8_t i_pdos = 0; i_pdos < size_arr_pdos; i_pdos++){
				rapidjson::Value m_pdos = arr_pdos[i_pdos].GetObject();
				assert(m_pdos.HasMember("index"));

				uint16_t pdo_index = _to_uint(m_pdos["index"]);

				// add new slave entry if sync doesnt have pdo entries
				if(!member_is_valid_array(m_pdos, "entries")){
					crnt_idx_slaves = (*slave_length)++;

					*slave_entries = (slaveEntry*) realloc(*slave_entries, (*slave_length) * sizeof(slaveEntry));

					(*slave_entries)[crnt_idx_slaves] = {
							alias,
							position,
							vendor_id,
							product_code,
							pdo_index,
							0,
							0,
							0,
							0,
							0,
							0,
							0,
							direction,
							0,
							0,
							0,
							0
						};

					continue;
				}

				rapidjson::Value arr_entries = m_pdos["entries"].GetArray();
				uint8_t size_arr_entries = arr_entries.Size();

				for(uint8_t i_entries = 0; i_entries < size_arr_entries; i_entries++){
					rapidjson::Value m_entries = arr_entries[i_entries].GetObject();

					assert(m_entries.HasMember("index"));
					assert(m_entries.HasMember("subindex"));
					assert(m_entries.HasMember("size"));

					uint16_t entry_index = -1;
					uint8_t entry_subindex = -1;
					uint8_t entry_size = -1;

					entry_index = _to_uint(m_entries["index"]);
					entry_subindex = _to_uint(m_entries["subindex"]);
					entry_size = _to_uint(m_entries["size"]);

					uint8_t entry_add_to_domain = 0;
					uint8_t entry_swap_endian = 0;
					uint8_t entry_signed = 0;

					if(m_entries.HasMember("swap_endian")){
						assert(m_entries["swap_endian"].IsBool());
						entry_swap_endian = (uint8_t) m_entries["swap_endian"].GetBool();
					}

					if(m_entries.HasMember("add_to_domain")){
						assert(m_entries["add_to_domain"].IsBool());
						entry_add_to_domain = (uint8_t) m_entries["add_to_domain"].GetBool();
					}

					if(m_entries.HasMember("signed")){
						assert(m_entries["signed"].IsBool());
						entry_signed = (uint8_t) m_entries["signed"].GetBool();
					}

					// add new slave entry
					crnt_idx_slaves = (*slave_length)++;

					*slave_entries = (slaveEntry*) realloc(*slave_entries, (*slave_length) * sizeof(slaveEntry));

					(*slave_entries)[crnt_idx_slaves] = {
							alias,
							position,
							vendor_id,
							product_code,
							pdo_index,
							entry_index,
							entry_subindex,
							entry_size,
							entry_add_to_domain,
							0,
							0,
							0,
							direction,
							entry_swap_endian,
							entry_signed,
							0,
							watchdog_enabled
						};
				}

			}

		}

		// start adding startup parameters if there is one
		if(member_is_valid_array(m_slaves, "parameters")){
			assert(m_slaves["parameters"].IsArray());

			for(uint8_t i_parameters = 0; i_parameters < m_slaves["parameters"].Size(); i_parameters++){
				rapidjson::Value m_parameters = m_slaves["parameters"][i_parameters].GetObject();
				assert(m_parameters.HasMember("index"));
				assert(m_parameters.HasMember("subindex"));
				assert(m_parameters.HasMember("size"));
				assert(m_parameters.HasMember("value"));

				uint16_t p_index = _to_uint(m_parameters["index"]);
				uint8_t p_subindex = _to_uint(m_parameters["subindex"]);
				uint8_t p_size = _to_uint(m_parameters["size"]);
				uint32_t p_value = _to_uint(m_parameters["value"]);

				uint8_t crnt_idx_parameters = (*parameters_length)++;
				*slave_parameters = (startupConfig*) realloc(*slave_parameters, (*parameters_length) * sizeof(startupConfig));

				(*slave_parameters)[crnt_idx_parameters] = {
						p_size,
						(uint8_t) position,
						p_index,
						p_subindex,
						p_value
					};
			}
		}

	}

	// sort slave entries asc
	if(do_sort_slave == 1){
		qsort(*slave_entries, *slave_length, sizeof(slaveEntry), _slave_entries_sort_asc);
	}

#ifdef DEBUG
	printf("slave_length = %d\n", *slave_length);
#endif

	return 0;
}

int32_t _slave_entries_sort_asc(const void *aptr, const void *bptr){
	const slaveEntry *p1 = (slaveEntry *)aptr;
	const slaveEntry *p2 = (slaveEntry *)bptr;

	// sort by position, pdo_index, index, then subindex
	if(p1->position > p2->position){
		return 1;
	} else if(p1->position < p2->position){
		return -1;
	} else if(p1->pdo_index > p2->pdo_index){
		return 1;
	} else if(p1->pdo_index < p2->pdo_index){
		return -1;
	} else if(p1->index > p2->index && (p1->index && p2->index)){
		return 1;
	} else if(p1->index < p2->index && (p1->index && p2->index)){
		return -1;
	} else if(p1->subindex > p2->subindex && (p1->subindex && p2->subindex)){
		return 1;
	} else if(p1->subindex < p2->subindex && (p1->subindex && p2->subindex)){
		return -1;
	} else if(p1->direction > p2->direction){
		return 1;
	} else if(p1->direction < p2->direction){
		return -1;
	} else {
		return 0;
	}
}
