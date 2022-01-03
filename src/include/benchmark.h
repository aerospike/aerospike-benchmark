/*******************************************************************************
 * Copyright 2008-2021 by Aerospike.
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

#include <aerospike/aerospike.h>
#include <aerospike/as_event.h>
#include <aerospike/as_password.h>
#include <aerospike/as_policy.h>
#include <aerospike/as_random.h>
#include <aerospike/as_record.h>
#include <aerospike/as_udf.h>

#include <hdr_histogram/hdr_histogram.h>
#include <dynamic_throttle.h>
#include <histogram.h>
#include <object_spec.h>
#include <workload.h>

// forward declare thr_coordinator for use in threaddata
struct thr_coordinator_s;

typedef struct args_s {
	char* hosts;
	int port;
	bool use_services_alternate;
	const char* user;
	char password[AS_PASSWORD_SIZE];
	const char* namespace;
	const char* set;
	char* bin_name;
	uint64_t start_key;
	uint64_t keys;

	struct stage_defs_s stage_defs;
	struct stages_s stages;
	char* workload_stages_file;

	// the default object spec, in the case that a workload stage isn't defined
	// with one
	struct obj_spec_s obj_spec;

	int transaction_worker_threads;
	bool enable_compression;
	float compression_ratio;

	int read_socket_timeout;
	int write_socket_timeout;
	int read_total_timeout;
	int write_total_timeout;
	int max_retries;
	bool debug;
	bool latency;
	as_vector latency_percentiles;
	bool latency_histogram;
	char* histogram_output;
	int histogram_period;
	char* hdr_output;
	bool use_shm;
	as_policy_key key;
	as_policy_replica replica;
	int rack_id;
	as_policy_read_mode_ap read_mode_ap;
	as_policy_read_mode_sc read_mode_sc;
	as_policy_commit_level write_commit_level;
	int conn_pools_per_node;
	bool durable_deletes;
	int async_max_commands;
	int event_loop_capacity;
	as_config_tls tls;
	char* tls_name;
	as_auth_mode auth_mode;
} args_t;

typedef struct clientdata_s {
	const char* namespace;
	const char* set;
	const char* bin_name;
	struct stages_s stages;

	uint64_t period_begin;

	aerospike client;

	// TODO make all these counts thread-local to reduce contention
	uint64_t read_hit_count;
	uint64_t read_miss_count;
	uint64_t read_timeout_count;
	uint64_t read_error_count;

	uint64_t write_count;
	uint64_t write_timeout_count;
	uint64_t write_error_count;

	uint64_t udf_count;
	uint64_t udf_timeout_count;
	uint64_t udf_error_count;

	FILE* hdr_comp_read_output;
	FILE* hdr_text_read_output;
	FILE* hdr_comp_write_output;
	FILE* hdr_text_write_output;
	FILE* hdr_comp_udf_output;
	FILE* hdr_text_udf_output;

	struct hdr_histogram* read_hdr;
	struct hdr_histogram* write_hdr;
	struct hdr_histogram* udf_hdr;
	as_vector latency_percentiles;

	FILE* histogram_output;
	int histogram_period;
	histogram_t read_histogram;
	histogram_t write_histogram;
	histogram_t udf_histogram;

	uint32_t tdata_count;

	int async_max_commands;
	int transaction_worker_threads;

	float compression_ratio;
	bool latency;
	bool debug;

} cdata_t;

typedef struct threaddata_s {
	cdata_t* cdata;
	struct thr_coordinator_s* coord;
	as_random* random;
	dyn_throttle_t dyn_throttle;

	// thread index: [0, n_threads)
	uint32_t t_idx;
	// which workload stage we're currrently on
	uint32_t stage_idx;

	/*
	 * note: to stop threads, tdata->finished must be set before tdata->do_work
	 * to prevent deadlocking
	 */
	// when true, things continue as normal, when set to false, worker
	// threads will stop doing what they're doing and await orders
	bool do_work;
	// when true, all threads will stop doing work and close (note that do_work
	// must also be set to false for this to work)
	bool finished;

	// the following arguments are initialized for each stage
	as_record fixed_value;
	as_list* fixed_udf_fn_args;

	as_policy_read read_policy;
	as_policy_write write_policy;
	as_policy_apply apply_policy;
	as_policy_batch batch_policy;
} tdata_t;


void load_defaults(args_t* args);
int load_defaults_post(args_t* args);
void free_args(args_t* args);

int run_benchmark(args_t* args);

