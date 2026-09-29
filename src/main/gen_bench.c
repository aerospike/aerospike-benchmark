/*******************************************************************************
 * Copyright 2026 by Aerospike.
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

//==========================================================
// Includes.
//

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>

#include <aerospike/as_msgpack.h>
#include <aerospike/as_record.h>
#include <aerospike/as_serializer.h>
#include <citrusleaf/cf_clock.h>

#include <benchmark.h>
#include <common.h>
#include <gen_bench.h>
#include <object_spec.h>
#include <synth_data.h>
#include <workload.h>


//==========================================================
// Typedefs & constants.
//

#define SIZE_SAMPLE_RECORDS 10000
#define DICT_PICKS_MIN 1000000LU
#define DICT_PICKS_MAX 100000000LU

typedef struct gen_bench_job_s {
	const args_t* args;
	const stage_t* stage;
	uint64_t n_records;
	uint32_t t_idx;
	uint64_t checksum;
} gen_bench_job_t;


//==========================================================
// Forward declarations.
//

LOCAL_HELPER uint64_t _gen_records(const args_t* args, const stage_t* stage,
		uint64_t n_records, uint32_t t_idx);
LOCAL_HELPER void* _gen_worker(void* udata);
LOCAL_HELPER double _avg_payload_bytes(const args_t* args, const stage_t* stage);


//==========================================================
// Public API.
//

int
run_gen_bench(const args_t* args)
{
	const stage_t* stage = &args->stages.stages[0];
	const obj_spec_t* spec = &stage->obj_spec;
	uint32_t n_bins = obj_spec_n_bins(spec);
	uint64_t n = args->gen_bench_iters;

	char spec_str[4096];
	snprint_obj_spec(spec, spec_str, sizeof(spec_str));
	printf("gen-bench object spec: %s\n", spec_str);
	printf("gen-bench seed: %s\n", args->seed_set ? "key-derived" : "per-thread");

	_gen_records(args, stage, n < 10000 ? n : 10000, 0);

	uint64_t start = cf_getns();
	_gen_records(args, stage, n, 0);
	uint64_t elapsed = cf_getns() - start;

	double ns_rec = (double) elapsed / (double) n;
	printf("gen-bench: 1 thread, %" PRIu64 " records, %u bins/record, "
			"%.1f ms\n", n, n_bins, elapsed / 1e6);
	printf("  records/s: %.0f   ns/record: %.1f   ns/bin: %.1f\n",
			1e9 / ns_rec, ns_rec, ns_rec / n_bins);
	printf("  avg payload: %.1f bytes/record (msgpack)\n",
			_avg_payload_bytes(args, stage));

	uint32_t n_threads = (uint32_t) args->transaction_worker_threads;
	if (n_threads > 1) {
		pthread_t* threads = cf_malloc(n_threads * sizeof(pthread_t));
		gen_bench_job_t* jobs = cf_malloc(n_threads * sizeof(gen_bench_job_t));

		start = cf_getns();
		for (uint32_t i = 0; i < n_threads; i++) {
			jobs[i] = (gen_bench_job_t) { args, stage, n, i, 0 };
			pthread_create(&threads[i], NULL, _gen_worker, &jobs[i]);
		}
		for (uint32_t i = 0; i < n_threads; i++) {
			pthread_join(threads[i], NULL);
		}
		elapsed = cf_getns() - start;

		double total = (double) n * n_threads;
		printf("gen-bench: %u threads, %.0f records, %.1f ms\n", n_threads,
				total, elapsed / 1e6);
		printf("  records/s: %.0f   bins/s: %.0f\n",
				total * 1e9 / elapsed, total * n_bins * 1e9 / elapsed);

		cf_free(jobs);
		cf_free(threads);
	}

	as_random random;
	as_random_init(&random);
	const synth_dict_t* d = &SYNTH_DICT_FIRST_NAME;
	uint64_t sink = 0;
	uint64_t picks = n * 20;
	picks = picks < DICT_PICKS_MIN ? DICT_PICKS_MIN :
		(picks > DICT_PICKS_MAX ? DICT_PICKS_MAX : picks);
	start = cf_getns();
	for (uint64_t i = 0; i < picks; i++) {
		uint32_t len;
		uint32_t r = (uint32_t) (as_random_next_uint64(&random) >> 32);
		const char* w = synth_dict_word(d, synth_dict_index(d, r), &len);
		sink += (uint8_t) w[0] + len;
	}
	elapsed = cf_getns() - start;
	printf("  dict pick: %" PRIu64 " picks, %.2f ns/pick (checksum %" PRIu64 ")\n",
			picks, (double) elapsed / picks, sink);

	return 0;
}


//==========================================================
// Local helpers.
//

LOCAL_HELPER uint64_t
_gen_records(const args_t* args, const stage_t* stage, uint64_t n_records,
		uint32_t t_idx)
{
	const obj_spec_t* spec = &stage->obj_spec;
	uint32_t n_bins = obj_spec_n_bins(spec);
	as_random random;
	uint64_t checksum = 0;

	if (args->seed_set) {
		seed_as_random(&random, ~args->seed, t_idx);
	}
	else {
		as_random_init(&random);
	}

	for (uint64_t i = 0; i < n_records; i++) {
		uint64_t record_seed;
		const uint64_t* seed_ptr = NULL;
		if (args->seed_set) {
			uint64_t key = i + ((uint64_t) t_idx << 40);
			uint64_t x = args->seed ^ (key * 0x9E3779B97F4A7C15LU);
			record_seed = splitmix64(&x);
			seed_ptr = &record_seed;
		}

		as_record* rec = as_record_new(n_bins);
		obj_spec_populate_bins_named(spec, rec, &random,
				(const as_bin_name*) stage->bin_names, NULL, 0,
				args->compression_ratio, seed_ptr);
		checksum += rec->bins.size;
		as_record_destroy(rec);
	}
	return checksum;
}

LOCAL_HELPER void*
_gen_worker(void* udata)
{
	gen_bench_job_t* job = (gen_bench_job_t*) udata;
	job->checksum = _gen_records(job->args, job->stage, job->n_records,
			job->t_idx + 1);
	return NULL;
}

LOCAL_HELPER double
_avg_payload_bytes(const args_t* args, const stage_t* stage)
{
	const obj_spec_t* spec = &stage->obj_spec;
	uint32_t n_bins = obj_spec_n_bins(spec);
	as_random random;
	as_serializer ser;
	uint64_t total = 0;

	as_random_init(&random);
	as_msgpack_init(&ser);

	for (uint32_t i = 0; i < SIZE_SAMPLE_RECORDS; i++) {
		as_record* rec = as_record_new(n_bins);
		obj_spec_populate_bins_named(spec, rec, &random,
				(const as_bin_name*) stage->bin_names, NULL, 0,
				args->compression_ratio, NULL);
		for (uint16_t b = 0; b < rec->bins.size; b++) {
			as_bin* bin = &rec->bins.entries[b];
			total += strlen(bin->name) + as_serializer_serialize_getsize(&ser,
					(as_val*) bin->valuep);
		}
		as_record_destroy(rec);
	}

	as_serializer_destroy(&ser);
	return (double) total / SIZE_SAMPLE_RECORDS;
}
