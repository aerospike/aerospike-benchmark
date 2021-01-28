
#include <stdio.h>

#include <cyaml/cyaml.h>

#include <benchmark.h>
#include <common.h>
#include <workload.h>


/* CYAML mapping schema fields array for stages */
static const cyaml_schema_field_t stage_mapping_schema[] = {
	CYAML_FIELD_UINT("stage", 0,
			struct stage, stage_idx),
	CYAML_FIELD_STRING_PTR("desc",
			CYAML_FLAG_POINTER_NULL_STR | CYAML_FLAG_OPTIONAL,
			struct stage, desc,
			0, CYAML_UNLIMITED),
	CYAML_FIELD_UINT("duration", 0,
			struct stage, stage_idx),
	CYAML_FIELD_STRING_PTR("workload", 0,
			struct stage, workload_str,
			0, CYAML_UNLIMITED),
	CYAML_FIELD_UINT("tps", CYAML_FLAG_OPTIONAL,
			struct stage, tps),
	CYAML_FIELD_STRING_PTR("object-spec",
			CYAML_FLAG_POINTER_NULL_STR | CYAML_FLAG_OPTIONAL,
			struct stage, obj_spec_str,
			0, CYAML_UNLIMITED),
	CYAML_FIELD_UINT("key-start", CYAML_FLAG_OPTIONAL | CYAML_FLAG_DEFAULT_ONES,
			struct stage, key_start),
	CYAML_FIELD_UINT("key-end", CYAML_FLAG_OPTIONAL | CYAML_FLAG_DEFAULT_ONES,
			struct stage, key_end),
	CYAML_FIELD_STRING_PTR("read-bins",
			CYAML_FLAG_POINTER_NULL_STR | CYAML_FLAG_OPTIONAL,
			struct stage, read_bins_str,
			0, CYAML_UNLIMITED),
	CYAML_FIELD_UINT("pause", CYAML_FLAG_OPTIONAL,
			struct stage, pause),
	CYAML_FIELD_END
};

static const cyaml_schema_value_t stage_schema = {
	CYAML_VALUE_MAPPING(CYAML_FLAG_DEFAULT,
			struct stage, stage_mapping_schema),
};

/* CYAML value schema for the top level mapping. */
static const cyaml_schema_value_t top_schema = {
	CYAML_VALUE_SEQUENCE(CYAML_FLAG_POINTER,
			struct stage, &stage_schema,
			1, CYAML_UNLIMITED),
};


/*
 * basic cyaml config
 */
static const cyaml_config_t config = {
	.log_level = CYAML_LOG_WARNING,
	.log_fn = cyaml_log,
	.mem_fn = cyaml_mem,
};



static void __parse_bins_destroy(as_vector* read_bins)
{

	// free all the as_bin_names reserved so far
	for (uint32_t d_idx = read_bins->size - 1; d_idx < read_bins->size;
			d_idx--) {
		cf_free(as_vector_get_ptr(read_bins, d_idx));
	}
	// then free the vector itself
	as_vector_destroy(read_bins);
}

/*
 * note: this must be done after the obj_spec has already been parsed
 */
static int _parse_bins_selection(struct stage* stage,
		const struct arguments_t* args)
{
	if (stage->read_bins_str == NULL) {
		return 0;
	}

	char* read_bins_str = stage->read_bins_str;
	as_vector read_bins;
	as_vector_init(&read_bins, sizeof(char*), 8);

	while (*read_bins_str != '\0') {
		char* endptr;
		uint64_t bin_num = strtoul(read_bins_str, &endptr, 10);
		if ((*endptr != '\0' && *endptr != ',') || (endptr == read_bins_str)) {
			fprintf(stderr, "Element %u of read-bins list not a positive "
					"number\n", read_bins.size + 1);
			__parse_bins_destroy(&read_bins);
			return -1;
		}

		if (bin_num == 0) {
			fprintf(stderr, "Invalid bin number: 0\n");
			__parse_bins_destroy(&read_bins);
			return -1;
		}

		// form bin name
		char** bin_name = (char**) as_vector_reserve(&read_bins);
		*bin_name = (char*) malloc(sizeof(as_bin_name));
		gen_bin_name(*bin_name, args->bin_name, bin_num - 1);

		read_bins_str = endptr;
		if (*read_bins_str == ',') {
			read_bins_str++;
		}
	}

	cf_free(stage->read_bins_str);
	
	// this will append one last slot to the vector and zero that slot out
	// (null-terminating the list)
	as_vector_reserve(&read_bins);

	// and now copy the vector to read_bins
	// since we don't actually need the size, make a temporary variable for it
	uint32_t size;
	stage->read_bins = (char**) as_vector_to_array(&read_bins, &size);

	as_vector_destroy(&read_bins);
	return 0;
}


/*
 * set stages struct to default values if they were not supplied
 */
static int stages_set_defaults_and_parse(struct stages* stages,
		const struct arguments_t* args)
{
	uint32_t n_stages = stages->n_stages;

	// when obj_specs are omitted, they are inherited from the previous stage,
	// with the first stage inheriting from the global obj_spec
	const struct obj_spec* prev_obj_spec = &args->obj_spec;

	for (uint32_t i = 0; i < n_stages; i++) {
		struct stage* stage = &stages->stages[i];

		if (stage->desc == NULL) {
			static const char default_msg[] = "stage %d";
			uint64_t buf_len = sizeof(default_msg) - 1 + dec_display_len(i + 1);
			char* buf = malloc(buf_len);
			snprintf(buf, buf_len, default_msg, i + 1);
			stage->desc = buf;
		}

		if (stage->key_start == -1LU) {
			// if key_start wasn't specified, then inherit from the global context
			stage->key_start = args->start_key;
		}

		if (stage->key_end == -1LU) {
			// if key_end wasn't specified, then set it to the default (keys
			// count from the global context + start_key)
			stage->key_end = stage->key_start + args->keys;
		}

		if (stage->key_start >= stage->key_end) {
			fprintf(stderr,
					"key_start (%lu) must be less than key_end (%lu)\n",
					stage->key_start, stage->key_end);
			return -1;
		}

		if (stage->stage_idx != i + 1) {
			fprintf(stderr,
					"Stage %d is marked with index %d\n",
					i + 1, stage->stage_idx);
			return -1;
		}

		if (stage->obj_spec_str == NULL) {
			// inherit obj_spec either from the previous stage or from the
			// global obj_spec
			obj_spec_shallow_copy(&stage->obj_spec, prev_obj_spec);
		}
		else {
			// parse the given obj_spec string
			char* obj_spec_str = stage->obj_spec_str;
			obj_spec_parse(&stage->obj_spec, obj_spec_str);

			cf_free(obj_spec_str);
			prev_obj_spec = &stage->obj_spec;
		}

		if (_parse_bins_selection(stage, args) != 0) {
			return -1;
		}
	}

	return 0;
}


int parse_workload_config_file(const char* file, struct stages* stages,
		const struct arguments_t* args)
{
	cyaml_err_t err;

	err = cyaml_load_file(file, &config,
			&top_schema, (cyaml_data_t **)&stages->stages,
			&stages->n_stages);
	if (err != CYAML_OK) {
		fprintf(stderr, "ERROR: %s\n", cyaml_strerror(err));
		return -1;
	}

	return stages_set_defaults_and_parse(stages, args);
}

void free_workload_config(struct stages* stages)
{
	// first go through and free the parts that aren't part of the yaml struct
	for (uint32_t i = 0; i < stages->n_stages; i++) {
		struct stage* stage = &stages->stages[i];

		obj_spec_free(&stage->obj_spec);
		// so cyaml_free doesn't try freeing whatever garbage is left here
		stage->obj_spec_str = NULL;
	}
	cyaml_free(&config, &top_schema, stages->stages, stages->n_stages);
}


void stages_print(const struct stages* stages)
{
	char obj_spec_buf[512];
	for (uint32_t i = 0; i < stages->n_stages; i++) {
		const struct stage* stage = &stages->stages[i];

		snprint_obj_spec(&stage->obj_spec, obj_spec_buf, sizeof(obj_spec_buf));
		printf( "- duration: %lu\n"
				"  desc: %s\n"
				"  tps: %lu\n"
				"  key-start: %lu\n"
				"  key-end: %lu\n"
				"  pause: %lu\n"
				"  workload: %s\n"
				"  stage: %u\n"
				"  object-spec: %s\n",
				stage->duration, stage->desc, stage->tps, stage->key_start,
				stage->key_end, stage->pause, stage->workload_str,
				stage->stage_idx, obj_spec_buf);

		if (stage->read_bins) {
			printf( "  read_bins: ");
			for (uint32_t i = 0; i < 10; i++) {
				printf("%s", stage->read_bins[i]);
				if (stage->read_bins[i + 1] != NULL) {
					printf(", ");
				}
				else {
					printf("\n");
					break;
				}
			}
		}
		else {
			printf("  read_bins: (null)\n");
		}
	}
}

