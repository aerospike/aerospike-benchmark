
//==========================================================
// Includes.
//

#include <assert.h>
#include <stdio.h>

#include <aerospike/as_sleep.h>
#include <cyaml/cyaml.h>

#include <benchmark.h>
#include <common.h>
#include <workload.h>


//==========================================================
// Typedefs & constants.
//

/* CYAML mapping schema fields array for stages */
static const cyaml_schema_field_t stage_mapping_schema[] = {
	CYAML_FIELD_UINT("stage", 0,
			stage_def_t, stage_idx),
	CYAML_FIELD_STRING_PTR("desc",
			CYAML_FLAG_POINTER_NULL_STR | CYAML_FLAG_OPTIONAL,
			stage_def_t, desc,
			0, CYAML_UNLIMITED),
	CYAML_FIELD_UINT("duration", 0,
			stage_def_t, duration),
	CYAML_FIELD_STRING_PTR("workload", 0,
			stage_def_t, workload_str,
			0, CYAML_UNLIMITED),
	CYAML_FIELD_UINT("tps", CYAML_FLAG_OPTIONAL,
			stage_def_t, tps),
	CYAML_FIELD_STRING_PTR("object-spec",
			CYAML_FLAG_POINTER_NULL_STR | CYAML_FLAG_OPTIONAL,
			stage_def_t, obj_spec_str,
			0, CYAML_UNLIMITED),
	CYAML_FIELD_UINT("key-start", CYAML_FLAG_OPTIONAL | CYAML_FLAG_DEFAULT_ONES,
			stage_def_t, key_start),
	CYAML_FIELD_UINT("key-end", CYAML_FLAG_OPTIONAL | CYAML_FLAG_DEFAULT_ONES,
			stage_def_t, key_end),
	CYAML_FIELD_STRING_PTR("read-bins",
			CYAML_FLAG_POINTER_NULL_STR | CYAML_FLAG_OPTIONAL,
			stage_def_t, read_bins_str,
			0, CYAML_UNLIMITED),
	CYAML_FIELD_STRING_PTR("write-bins",
			CYAML_FLAG_POINTER_NULL_STR | CYAML_FLAG_OPTIONAL,
			stage_def_t, write_bins_str,
			0, CYAML_UNLIMITED),
	CYAML_FIELD_UINT("pause", CYAML_FLAG_OPTIONAL,
			stage_def_t, pause),
	CYAML_FIELD_UINT("batch-size", CYAML_FLAG_OPTIONAL,
			stage_def_t, batch_size),
	CYAML_FIELD_BOOL("async", CYAML_FLAG_OPTIONAL,
			stage_def_t, async),
	CYAML_FIELD_BOOL("random", CYAML_FLAG_OPTIONAL,
			stage_def_t, random),
	CYAML_FIELD_END
};

static const cyaml_schema_value_t stage_schema = {
	CYAML_VALUE_MAPPING(CYAML_FLAG_DEFAULT,
			stage_def_t, stage_mapping_schema),
};

/* CYAML value schema for the top level mapping. */
static const cyaml_schema_value_t top_schema = {
	CYAML_VALUE_SEQUENCE(CYAML_FLAG_POINTER,
			stage_def_t, &stage_schema,
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


//==========================================================
// Forward declarations.
//


/*
 * tells _parse_bins_selection to generate a list of bin names
 */
#define PARSE_BINS_STR 0x0
/*
 * tells _parse_bins_selection to generate a list of bin indices
 */
#define PARSE_BINS_INT 0x1

/*
 * reads and parses bins_str, a comma-separated list of bin numbers
 * (1-based indexed) and populates the read_bins field of stage
 *
 * the list of bin names will be returned, and n_bins will be populated with
 * the number of bins in the returned array
 */
static void* _parse_bins_selection(const char* bins_str,
		const obj_spec_t* obj_spec, const char* stage_bin_name,
		uint32_t* n_bins_ptr, uint8_t mode);
/*
 * cleanup helper used by _parse_bins_selection on failure
 */
static void _parse_bins_destroy(as_vector* read_bins, uint8_t mode);
/*
 * frees the bins selection array created from parse_bins_selection in STR mode
 */
static void _free_bins_selection(char** bins);


//==========================================================
// Public API.
//

int
parse_workload_type(workload_t* workload, const char* workload_str)
{
	char* endptr;

	switch (*workload_str) {
		case 'I': {
			workload->type = WORKLOAD_TYPE_LINEAR;
			break;
		}
		case 'R': {
			float pct;
			if (workload_str[1] == 'U' && workload_str[2] == '\0') {
				pct = WORKLOAD_RANDOM_DEFAULT_PCT;
			}
			else {
				if (workload_str[1] != 'U' || workload_str[2] != ',') {
					fprintf(stderr, "Unknown workload \"%s\"\n", workload_str);
					return -1;
				}
				pct = strtod(workload_str + 3, &endptr);
				if (workload_str[3] == '\0' || *endptr != '\0') {
					fprintf(stderr, "\"%s\" not a floating point number\n",
							workload_str + 3);
					return -1;
				}
				if (pct <= 0 || pct > 100) {
					fprintf(stderr, "%f not a percentage greater than 0\n",
							pct);
					return -1;
				}
			}
			workload->type = WORKLOAD_TYPE_RANDOM;
			workload->pct = pct;
			break;
		}
		case 'D': {
			if (workload_str[1] != 'B' || workload_str[2] != '\0') {
				fprintf(stderr, "Unknown workload \"%s\"\n", workload_str);
				return -1;
			}
			workload->type = WORKLOAD_TYPE_DELETE;
			break;
		}
		default:
			fprintf(stderr, "Unknown workload \"%s\"\n", workload_str);
			return -1;
	}
	return 0;
}

int
stages_set_defaults_and_parse(stages_t* stages, const stage_defs_t* stage_defs,
		const args_t* args)
{
	uint32_t n_stages = stage_defs->n_stages;

	// when obj_specs are omitted, they are inherited from the previous stage,
	// with the first stage inheriting from the global obj_spec
	const struct obj_spec_s* prev_obj_spec = &args->obj_spec;

	int ret = 0;

	stages->stages = (stage_t*) cf_malloc(stage_defs->n_stages * sizeof(stage_t));
	stages->n_stages = stage_defs->n_stages;

	for (uint32_t i = 0; i < n_stages; i++) {
		stage_def_t* stage_def = &stage_defs->stages[i];
		stage_t* stage = &stages->stages[i];

		stage->desc = strdup(stage_def->desc);
		stage->tps = stage_def->tps;
		stage->pause = stage_def->pause;
		stage->async = stage_def->async;
		stage->random = stage_def->random;

		if (stage_def->key_start == -1LU) {
			// if key_start wasn't specified, then inherit from the global context
			stage->key_start = args->start_key;
		}
		else {
			stage->key_start = stage_def->key_start;
		}

		if (stage_def->key_end == -1LU) {
			// if key_end wasn't specified, then set it to the default (keys
			// count from the global context + start_key)
			stage->key_end = stage->key_start + args->keys;
		}
		else {
			stage->key_end = stage_def->key_end;
		}

		if (stage->key_start >= stage->key_end) {
			fprintf(stderr,
					"key_start (%lu) must be less than key_end (%lu)\n",
					stage->key_start, stage->key_end);
			ret = -1;
		}

		// batch_size = 0 means it wasn't set, so set it to 1
		stage->batch_size = MAX(stage_def->batch_size, 1);

		if (stage_def->stage_idx != i + 1) {
			fprintf(stderr,
					"Stage %d is marked with index %d\n",
					i + 1, stage_def->stage_idx);
			ret = -1;
		}

		if (parse_workload_type(&stage->workload, stage_def->workload_str)
				!= 0) {
			ret = -1;
		}

		if (stage_def->duration == -1LU) {
			if (workload_is_infinite(&stage->workload)) {
				stage->duration = DEFAULT_RANDOM_DURATION;
			}
			else {
				stage->duration = 0;
			}
		}
		else {
			stage->duration = stage_def->duration;
		}

		if (stage_def->obj_spec_str == NULL) {
			// inherit obj_spec either from the previous stage or from the
			// global obj_spec
			obj_spec_shallow_copy(&stage->obj_spec, prev_obj_spec);
		}
		else {
			// parse the given obj_spec string
			if (ret == 0) {
				ret = obj_spec_parse(&stage->obj_spec, stage_def->obj_spec_str);
			}

			if (ret == 0) {
				prev_obj_spec = &stage->obj_spec;
			}
			else {
				// mark the obj_spec as invalid so it isn't freed
				stage->obj_spec.valid = false;
			}
		}

		char* bins_str = stage_def->read_bins_str;
		if (bins_str != NULL && !workload_contains_reads(&stage->workload)) {
			fprintf(stderr, "Stage %d: cannot specify read-bins on workload "
					"without reads\n",
					i + 1);
			ret = -1;
		}
		else if (bins_str != NULL) {
			stage->read_bins = _parse_bins_selection(bins_str, &stage->obj_spec,
					args->bin_name, &stage->n_read_bins, PARSE_BINS_STR);
			if (stage->read_bins == NULL) {
				ret = -1;
			}
		}
		else {
			stage->read_bins = NULL;
			stage->n_read_bins = 0;
		}

		// now parse write bins
		bins_str = stage_def->write_bins_str;
		if (bins_str != NULL && !workload_contains_writes(&stage->workload)) {
			fprintf(stderr, "Stage %d: cannot specify write-bins on workload "
					"without writes\n",
					i + 1);
			ret = -1;
		}
		else if (bins_str != NULL) {
			stage->write_bins = _parse_bins_selection(bins_str,
					&stage->obj_spec, args->bin_name, &stage->n_write_bins,
					PARSE_BINS_INT);
			if (stage->write_bins == NULL) {
				ret = -1;
			}
		}
		else {
			stage->write_bins = NULL;
			stage->n_write_bins = 0;
		}
	}

	if (ret != 0) {
		free_workload_config(stages);
		stages->valid = false;
	}

	return ret;
}

int parse_workload_config_file(const char* file, stages_t* stages,
		const args_t* args)
{
	cyaml_err_t err;
	stage_defs_t stage_defs;

	err = cyaml_load_file(file, &config,
			&top_schema, (cyaml_data_t **)&stage_defs.stages,
			&stage_defs.n_stages);
	if (err != CYAML_OK) {
		fprintf(stderr, "ERROR: %s\n", cyaml_strerror(err));
		return -1;
	}
	stages->valid = true;

	int ret = stages_set_defaults_and_parse(stages, &stage_defs, args);
	cyaml_free(&config, &top_schema, stage_defs.stages, stage_defs.n_stages);
	return ret;
}

void free_workload_config(stages_t* stages)
{
	if (stages->valid) {
		// first go through and free the parts that aren't part of the yaml struct
		for (uint32_t i = 0; i < stages->n_stages; i++) {
			stage_t* stage = &stages->stages[i];
			obj_spec_free(&stage->obj_spec);
			_free_bins_selection(stage->read_bins);
			cf_free(stage->write_bins);
		}
	}
}

void stages_move(stages_t* dst, stages_t* src)
{
	// you can only move from a valid src
	assert(src->valid);
	__builtin_memcpy(dst, src, offsetof(stages_t, valid));
	dst->valid = true;
	src->valid = false;
}

void stages_shallow_copy(stages_t* dst, const stages_t* src)
{
	__builtin_memcpy(dst, src, offsetof(stages_t, valid));
	dst->valid = false;
}

bool stages_contains_reads(const stages_t* stages)
{
	for (uint32_t i = 0; i < stages->n_stages; i++) {
		if (workload_contains_reads(&stages->stages[i].workload)) {
			return true;
		}
	}
	return false;
}

uint64_t stage_gen_random_key(const stage_t* stage, as_random* random)
{
	return gen_rand_range_64(random, stage->key_end - stage->key_start) +
		stage->key_start;
}

void stage_random_pause(as_random* random, const stage_t* stage)
{
	uint32_t pause = stage->pause;
	if (pause != 0) {
		// generate a random pause amount between 1 and the specified pause
		// amount
		pause = gen_rand_range(random, pause) + 1;
		blog_line("Pause for %u seconds", pause);
		as_sleep(pause * 1000LU);
	}
}

void stages_print(const stages_t* stages)
{
	const static char* workloads[] = {
		"I",
		"RU",
		"DB"
	};

	char obj_spec_buf[512];
	for (uint32_t i = 0; i < stages->n_stages; i++) {
		const stage_t* stage = &stages->stages[i];

		snprint_obj_spec(&stage->obj_spec, obj_spec_buf, sizeof(obj_spec_buf));
		printf( "- duration: %lu\n"
				"  desc: %s\n"
				"  tps: %lu\n"
				"  key-start: %lu\n"
				"  key-end: %lu\n"
				"  pause: %lu\n"
				"  batch-size: %u\n"
				"  async: %s\n"
				"  random: %s\n",
				stage->duration, stage->desc, stage->tps, stage->key_start,
				stage->key_end, stage->pause, stage->batch_size,
				boolstring(stage->async), boolstring(stage->random));

		printf( "  workload: %s",
				workloads[stage->workload.type]);
		if (stage->workload.type == WORKLOAD_TYPE_RANDOM) {
			printf(",%g%%\n", stage->workload.pct);
		}
		else {
			printf("\n");
		}

		printf( "  stage: %u\n"
				"  object-spec: %s\n",
				i + 1, obj_spec_buf);


		if (stage->read_bins) {
			printf( "  read-bins: ");
			for (uint32_t i = 0; i < stage->n_read_bins; i++) {
				printf("%s", stage->read_bins[i]);
				if (i < stage->n_read_bins - 1) {
					printf(", ");
				}
				else {
					printf("\n");
					break;
				}
			}
		}
		else {
			printf("  read-bins: (null)\n");
		}

		if (stage->write_bins) {
			printf( "  write-bins: ");
			for (uint32_t i = 0; i < stage->n_write_bins; i++) {
				printf("%d", stage->write_bins[i] + 1);
				if (i < stage->n_write_bins - 1) {
					printf(", ");
				}
				else {
					printf("\n");
					break;
				}
			}
		}
		else {
			printf("  write-bins: (null)\n");
		}
	}
}

void stages_print_defs(const stage_defs_t* stage_defs)
{
	for (uint32_t i = 0; i < stage_defs->n_stages; i++) {
		const stage_def_t* stage = &stage_defs->stages[i];

		printf( "- stage: %u\n"
				"  duration: %lu\n"
				"  desc: %s\n"
				"  tps: %lu\n"
				"  key-start: %lu\n"
				"  key-end: %lu\n"
				"  pause: %lu\n"
				"  batch-size: %u\n"
				"  async: %s\n"
				"  random: %s\n"
				"  workload: %s\n"
				"  object-spec: %s\n"
				"  read-bins: %s\n"
				"  write-bins: %s\n",
				i + 1, stage->duration, stage->desc, stage->tps,
				stage->key_start, stage->key_end, stage->pause,
				stage->batch_size, boolstring(stage->async),
				boolstring(stage->random), stage->workload_str,
				stage->obj_spec_str, stage->read_bins_str,
				stage->write_bins_str);
	}
}


//==========================================================
// Local helpers.
//

static void*
_parse_bins_selection(const char* bins_str, const obj_spec_t* obj_spec,
		const char* stage_bin_name, uint32_t* n_bins_ptr, uint8_t mode)
{
	if (bins_str == NULL) {
		return NULL;
	}

	uint32_t n_bins = obj_spec_n_bins(obj_spec);
	void* bin_list;
	uint64_t prev_bin_num = 0;
	as_vector bins;

	uint32_t element_size;
	if (mode == PARSE_BINS_STR) {
		element_size = sizeof(char*);
	}
	else {
		element_size = sizeof(uint32_t);
	}

	as_vector_init(&bins, element_size, 8);

	while (*bins_str != '\0') {
		char* endptr;
		uint64_t bin_num = strtoul(bins_str, &endptr, 10);
		if ((*endptr != '\0' && *endptr != ',') || (endptr == bins_str)) {
			fprintf(stderr, "Element %u of read-bins list not a positive "
					"number\n", bins.size + 1);
			_parse_bins_destroy(&bins, mode);
			return NULL;
		}

		if (bin_num == 0) {
			fprintf(stderr, "Invalid bin number: 0\n");
			_parse_bins_destroy(&bins, mode);
			return NULL;
		}
		if (bin_num > n_bins) {
			fprintf(stderr, "No such bin %lu (there are only %u bins)\n",
					bin_num, n_bins);
			_parse_bins_destroy(&bins, mode);
			return NULL;
		}
		if (bin_num <= prev_bin_num) {
			fprintf(stderr, "Bins must appear in ascending order "
					"(%lu <= %lu)\n",
					bin_num, prev_bin_num);
			_parse_bins_destroy(&bins, mode);
			return NULL;
		}

		if (mode == PARSE_BINS_STR) {
			// form bin name
			char** bin_name = (char**) as_vector_reserve(&bins);
			*bin_name = (char*) cf_malloc(sizeof(as_bin_name));
			gen_bin_name(*bin_name, stage_bin_name, bin_num - 1);
		}
		else {
			uint32_t* bin_idx = (uint32_t*) as_vector_reserve(&bins);
			*bin_idx = bin_num - 1;
		}
		prev_bin_num = bin_num;

		bins_str = endptr;
		if (*bins_str == ',') {
			bins_str++;
		}
	}

	// this will append one last slot to the vector and zero that slot out
	// (null-terminating the list)
	as_vector_reserve(&bins);

	// and now copy the vector to bins
	bin_list = as_vector_to_array(&bins, n_bins_ptr);
	// don't count the null-terminating pointer
	(*n_bins_ptr)--;

	as_vector_destroy(&bins);
	return bin_list;
}

static void
_parse_bins_destroy(as_vector* read_bins, uint8_t mode)
{

	if (mode == PARSE_BINS_STR) {
		// free all the as_bin_names reserved so far
		for (uint32_t d_idx = read_bins->size - 1; d_idx < read_bins->size;
				d_idx--) {
			cf_free(as_vector_get_ptr(read_bins, d_idx));
		}
	}
	// then free the vector itself
	as_vector_destroy(read_bins);
}

static void
_free_bins_selection(char** bins)
{
	if (bins != NULL) {
		for (uint64_t i = 0; bins[i] != NULL; i++) {
			cf_free(bins[i]);
		}
		cf_free(bins);
	}
}
