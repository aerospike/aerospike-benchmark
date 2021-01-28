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

#include <aerospike/as_vector.h>

#include <object_spec.h>


#define WORKLOAD_TYPE_LINEAR 0x0
#define WORKLOAD_TYPE_RANDOM 0x1
#define WORKLOAD_TYPE_DELETE 0x2

#define WORKLOAD_LINEAR_DEFAULT_PCT 100.f
#define WORKLOAD_RANDOM_DEFAULT_PCT 50.f

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
	// stage duration in seconds
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
		char** read_bins;
	};
};


struct stages {
	struct stage* stages;
	uint32_t n_stages;
};

// forward declare arguments struct from benchmark
struct arguments_t;


/*
 * given a workload string, populates the workload struct pointed to by the
 * first argument
 */
int parse_workload_type(struct workload*, const char* workload_str);

inline bool workload_is_random(struct workload* workload)
{
	return workload->type == WORKLOAD_TYPE_RANDOM;
}

/*
 * parses the given file into the stages struct
 */
int parse_workload_config_file(const char* file, struct stages* stages,
		const struct arguments_t* args);

void free_workload_config(struct stages* stages);

void stages_print(const struct stages* stages);

