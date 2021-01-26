
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


/*
 * set stages struct to default values if they were not supplied
 */
static int stages_set_defaults_and_parse(struct stages* stages,
		const struct arguments_t* args)
{
	uint32_t n_stages = stages->n_stages;

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
	cyaml_free(&config, &top_schema, stages->stages, stages->n_stages);
}


void stages_print(const struct stages* stages)
{
	for (uint32_t i = 0; i < stages->n_stages; i++) {
		const struct stage* stage = &stages->stages[i];
		printf( "- duration: %lu\n"
				"  desc: %s\n"
				"  tps: %lu\n"
				"  key-start: %lu\n"
				"  key-end: %lu\n"
				"  pause: %lu\n"
				"  workload: %s\n"
				"  stage: %u\n"
				"  object-spec: %s\n"
				"  read-bins: %u\n",
				stage->duration, stage->desc, stage->tps, stage->key_start,
				stage->key_end, stage->pause, stage->workload_str,
				stage->stage_idx, stage->obj_spec_str, stage->read_bins.size);
	}
}

