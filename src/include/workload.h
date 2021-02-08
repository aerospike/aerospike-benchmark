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


#define WORKLOAD_TYPE_LINEAR 0x0
#define WORKLOAD_TYPE_RANDOM 0x1
#define WORKLOAD_TYPE_DELETE 0x2

#define WORKLOAD_LINEAR_DEFAULT_PCT 100.f
#define WORKLOAD_RANDOM_DEFAULT_PCT 50.f

// the default number of seconds an infinite workload will run if not specified
#define DEFAULT_RANDOM_DURATION 10

struct workload {
	/*
	 * one of:
	 *  WORKLOAD_TYPE_LINEAR: linear insert workload, initializing pct% of the
	 *  		keys (default 100%)
	 *  WORKLOAD_TYPE_RANDOM: random reads/writes, with pct% of the operations
	 *  		being reads, the rest being writes
	 *  WORKLOAD_TYPE_DELETE: bin delete workload, which just deletes all the
	 *  		bins that were created by prior stages
	 */
	uint8_t type;
	/*
	 * percent of keys to initialize, for LINEAR,
	 * percent of reads (rest are writes), for RANDOM
	 */
	float pct;
};


struct stage {
	// minimum stage duration in seconds
	uint64_t duration;

	// string desctriptor for the stage, printed when the stage begins
	const char* desc;

	// max transactions per second
	uint64_t tps;

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

	union {
		struct {
			char* workload_str;
			// only used temporarily to verify that stages are indexed properly
			uint16_t stage_idx;
		};
		struct workload workload;
	};

	union {
		char* obj_spec_str;
		struct obj_spec obj_spec;
	};

	union {
		char* read_bins_str;
		struct {
			char** read_bins;
			uint32_t n_read_bins;
		};
	};
};


struct stages {
	struct stage* stages;
	uint32_t n_stages;
	/*
     * when set to true, this is a valid stages struct, when set to false, this
	 * stages struct has already been freed/is owned by another stages struct
	 *
	 * note: this can still be used even if it's invalid, so long as the owner
	 * hasn't been freed yet
	 */
	bool valid;
};

// forward declare arguments struct from benchmark
struct arguments_t;


/*
 * given a workload string, populates the workload struct pointed to by the
 * first argument
 */
int parse_workload_type(struct workload*, const char* workload_str);

static inline bool workload_is_random(const struct workload* workload)
{
	return workload->type == WORKLOAD_TYPE_RANDOM;
}

static inline bool workload_contains_reads(const struct workload* workload)
{
	return workload->type == WORKLOAD_TYPE_RANDOM;
}

static inline bool stages_contain_async(const struct stages* stages)
{
	for (uint32_t i = 0; i < stages->n_stages; i++) {
		if (stages->stages[i].async) {
			return true;
		}
	}
	return false;
}

static inline bool stages_contain_random(const struct stages* stages)
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
static inline bool workload_is_infinite(const struct workload* workload)
{
	return workload->type == WORKLOAD_TYPE_RANDOM;
}

/*
 * reads and parses bins_str, a comma-separated list of bin numbers
 * (1-based indexed) and populates the read_bins field of stage
 *
 * note: this must be done after the obj_spec has already been parsed
 */
int parse_bins_selection(struct stage* stage, const char* bins_str,
		const char* bin_name);

/*
 * frees the bins selection array created from parse_bins_selection
 */
void free_bins_selection(struct stage* stage);

/*
 * set stages struct to default values if they were not supplied
 */
int stages_set_defaults_and_parse(struct stages* stages,
		const struct arguments_t* args);

/*
 * parses the given file into the stages struct
 */
int parse_workload_config_file(const char* file, struct stages* stages,
		const struct arguments_t* args);

void free_workload_config(struct stages* stages);

/*
 * transfers ownership of the stages struct from src to dst, so if free is
 * called on the previous owner it does not free the stages while the new
 * owner is still using it
 */
void stages_move(struct stages* dst, struct stages* src);

/*
 * copies the stages struct src into dst without transferring ownership
 * (meaning dst may become invalid once src is freed)
 */
void stages_shallow_copy(struct stages* dst, const struct stages* src);

/*
 * returns true if any of the stages will perform reads
 */
bool stages_contains_reads(const struct stages*);

/*
 * generates a random key for the stage
 */
uint64_t stage_gen_random_key(const struct stage*, as_random*);

/*
 * pauses for a random amount of time, to be called before the stage begins
 */
void stage_random_pause(as_random* random, const struct stage* stage);

void stages_print(const struct stages* stages);

