/*******************************************************************************
 * Copyright 2008-2018 by Aerospike.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ******************************************************************************/
#pragma once

#include <aerospike/as_random.h>
#include <aerospike/as_vector.h>

#include <object_spec.h>


typedef enum {
	// linear insertion workload
	WORKLOAD_TYPE_I,
	// random read/update workload
	WORKLOAD_TYPE_RU,
	// random read/replace workload
	WORKLOAD_TYPE_RR,
	// linear deletion workload
	WORKLOAD_TYPE_D,
	// random read/update/function (udf) workload
	WORKLOAD_TYPE_RUF,
	// random read/update/delete workload
	WORKLOAD_TYPE_RUD
} workload_type_t;

#define WORKLOAD_RU_DEFAULT_PCT 50.f

#define WORKLOAD_RUF_DEFAULT_READ_PCT 40.f
#define WORKLOAD_RUF_DEFAULT_WRITE_PCT 40.f

#define WORKLOAD_RUD_DEFAULT_READ_PCT 40.f
#define WORKLOAD_RUD_DEFAULT_WRITE_PCT 40.f

#define WORKLOAD_DEFAULT_READ_ALL_PCT 100.f
#define WORKLOAD_DEFAULT_WRITE_ALL_PCT 100.f

// the default number of seconds an infinite workload will run if not specified
#define DEFAULT_RANDOM_DURATION 10


// forward declare arguments since benchmark.h includes this file
struct args_s;


typedef struct workload_s {
	workload_type_t type;

	/*
	 * read_pct = percent of reads (rest are writes), for RU
	 * 100 - read_pct - write_pct = percent of UDF ops, for RUF
	 * 100 - read_pct - write_pct = percent of delete ops, for RUD
	 */
	float read_pct;
	float write_pct;

	/*
	 * these are the percent read all bins/percent all write bins values. The
	 * remaining reads/writes will only read/write the first bin of the record
	 * (or the bins specified with read-bins/write-bins, if those are given)
	 */
	float read_all_pct;
	float write_all_pct;
} workload_t;


typedef struct udf_spec_s {
	char* udf_package_name;
	char* udf_fn_name;
	char* udf_fn_args;
} udf_spec_t;

typedef struct stage_def_s {
	// minimum stage duration in seconds
	uint64_t duration;

	// string desctriptor for the stage, printed when the stage begins
	char* desc;

	// max transactions per second
	uint64_t tps;
	// record TTL used in write transactions
	uint64_t ttl;

	uint64_t key_start;
	uint64_t key_end;

	// max number of seconds to pause between stage starts, randomly selected
	// between 1 and pause
	uint64_t pause;

	// batch size of reads to use
	uint32_t batch_size;
	// whether or not this stage should be run in async mode
	bool async;
	// whether or not random objects should be created for each write op (as
	// opposed to using a single fixed object over and over)
	bool random;

	uint16_t stage_idx;

	char* workload_str;

	char* obj_spec_str;

	char* read_bins_str;

	char* write_bins_str;

	udf_spec_t udf_spec;
} stage_def_t;


typedef struct stage_defs_s {
	stage_def_t* stages;
	uint32_t n_stages;
} stage_defs_t;


typedef struct stage_s {
	// minimum stage duration in seconds
	uint64_t duration;

	// string desctriptor for the stage, printed when the stage begins
	char* desc;

	// max transactions per second
	uint64_t tps;
	// record TTL used in write transactions
	uint64_t ttl;

	uint64_t key_start;
	uint64_t key_end;

	// max number of seconds to pause between stage starts, randomly selected
	// between 1 and pause
	uint64_t pause;

	// batch size of reads to use
	uint32_t batch_size;
	// whether or not this stage should be run in async mode
	bool async;
	// whether or not random objects should be created for each write op (as
	// opposed to using a single fixed object over and over)
	bool random;

	workload_t workload;

	obj_spec_t obj_spec;

	char** read_bins;
	uint32_t n_read_bins;

	uint32_t* write_bins;
	uint32_t n_write_bins;

	as_udf_module_name udf_package_name;
	as_udf_function_name udf_fn_name;
	obj_spec_t udf_fn_args;
} stage_t;


typedef struct stages_s {
	stage_t* stages;
	uint32_t n_stages;
	/*
     * when set to true, this is a valid stages struct, when set to false, this
	 * stages struct has already been freed/is owned by another stages struct
	 *
	 * note: this can still be used even if it's invalid, so long as the owner
	 * hasn't been freed yet
	 */
	bool valid;
} stages_t;


static inline bool workload_is_random(const workload_t* workload)
{
	return workload->type == WORKLOAD_TYPE_RU ||
		workload->type == WORKLOAD_TYPE_RR ||
		workload->type == WORKLOAD_TYPE_RUF ||
		workload->type == WORKLOAD_TYPE_RUD;
}

static inline bool workload_contains_reads(const workload_t* workload)
{
	return (workload->type == WORKLOAD_TYPE_RU && workload->read_pct != 0) ||
		(workload->type == WORKLOAD_TYPE_RR && workload->read_pct != 0) ||
		(workload->type == WORKLOAD_TYPE_RUF && workload->read_pct != 0) ||
		(workload->type == WORKLOAD_TYPE_RUD && workload->read_pct != 0);
}

static inline bool workload_contains_writes(const workload_t* workload)
{
	return (workload->type != WORKLOAD_TYPE_RU || workload->read_pct != 100) &&
		(workload->type != WORKLOAD_TYPE_RR || workload->read_pct != 100) &&
		(workload->type != WORKLOAD_TYPE_RUF || workload->write_pct != 0) &&
		(workload->type != WORKLOAD_TYPE_RUD || workload->write_pct != 0);
}

static inline bool workload_contains_deletes(const workload_t* workload)
{
	return workload->type == WORKLOAD_TYPE_D || workload->type == WORKLOAD_TYPE_RUD;
}

static inline bool workload_contains_udfs(const workload_t* workload)
{
	return workload->type == WORKLOAD_TYPE_RUF;
}

static inline bool stages_contain_async(const stages_t* stages)
{
	for (uint32_t i = 0; i < stages->n_stages; i++) {
		if (stages->stages[i].async) {
			return true;
		}
	}
	return false;
}

static inline bool stages_contain_random(const stages_t* stages)
{
	for (uint32_t i = 0; i < stages->n_stages; i++) {
		if (stages->stages[i].random) {
			return true;
		}
	}
	return false;
}

/*
 * returns true if the workload has no fixed amount of work to do (i.e. could
 * run forever, and will not run at all if duration is set to 0)
 */
static inline bool workload_is_infinite(const workload_t* workload)
{
	return workload->type == WORKLOAD_TYPE_RU ||
		workload->type == WORKLOAD_TYPE_RR ||
		workload->type == WORKLOAD_TYPE_RUF ||
		workload->type == WORKLOAD_TYPE_RUD;
}

static inline void fprint_stage(FILE* out_file, const stages_t* stages,
		uint32_t stage_idx)
{
	const char* desc = stages->stages[stage_idx].desc;
	fprintf(out_file, "Stage %d: %s\n", stage_idx + 1, desc ? desc : "");
}

/*
 * given a workload string, populates the workload struct pointed to by the
 * first argument
 */
int parse_workload_type(workload_t*, const char* workload_str);

/*
 * set stages struct to default values if they were not supplied
 */
int stages_set_defaults_and_parse(stages_t* stages,
		const stage_defs_t* stage_defs, const struct args_s* args);

/*
 * parses the given file into the stages struct
 */
int parse_workload_config_file(const char* file, stages_t* stages,
		const struct args_s* args);

void free_stage_defs(stage_defs_t* stage_defs);

void free_workload_config(stages_t* stages);

/*
 * transfers ownership of the stages struct from src to dst, so if free is
 * called on the previous owner it does not free the stages while the new
 * owner is still using it
 */
void stages_move(stages_t* dst, stages_t* src);

/*
 * copies the stages struct src into dst without transferring ownership
 * (meaning dst may become invalid once src is freed)
 */
void stages_shallow_copy(stages_t* dst, const stages_t* src);

/*
 * returns true if any of the stages will perform writes
 */
bool stages_contain_writes(const stages_t*);

/*
 * returns true if any of the stages will perform reads
 */
bool stages_contain_reads(const stages_t*);

/*
 * returns true if any of the stages will perform UDF ops
 */
bool stages_contain_udfs(const stages_t*);

/*
 * generates a random key for the stage
 */
uint64_t stage_gen_random_key(const stage_t*, as_random*);

/*
 * pauses for a random amount of time, to be called before the stage begins
 */
void stage_random_pause(as_random* random, const stage_t* stage);

void stages_print(const stages_t* stages);

