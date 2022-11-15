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

//==========================================================
// Includes.
//

#include <stdio.h>

#include <citrusleaf/alloc.h>
#include <stdatomic.h>

#include "histogram.h"
#include "common.h"


//==========================================================
// Typedefs & constants.
//

/*
 * internal struct to describe the layout of the histogram, with redundancy
 * for performance reasons
 */
typedef struct bucket_range_desc_s {
	delay_t lower_bound;
	delay_t bucket_width;
	uint32_t offset;
	uint32_t n_buckets;
} bucket_range_desc_t;

#define UNDERFLOW_IDX (-2)
#define OVERFLOW_IDX  (-1)


//==========================================================
// Forward declarations.
//

LOCAL_HELPER int32_t _histogram_get_index(histogram_t* h, delay_t elapsed_us);
LOCAL_HELPER void _print_header(const histogram_t* h, uint64_t period_duration_us,
		uint64_t total_cnt, FILE* out_file);


//==========================================================
// Inlines and macros.
//

STATIC_ASSERT(offsetof(histogram_t, underflow_cnt) + sizeof(uint32_t) ==
		offsetof(histogram_t, overflow_cnt));

inline _Atomic(uint32_t)*
__attribute__((always_inline))
__histogram_get_bucket(histogram_t* h, int64_t idx) {
	return (idx < 0) ? (((_Atomic(uint32_t)*) (((ptr_int_t) h) + offsetof(histogram_t, underflow_cnt)
				+ 2 * sizeof(uint32_t))) + idx) : &h->buckets[idx];
}


//==========================================================
// Public API.
//

int
histogram_init(histogram_t* h, size_t n_ranges, delay_t lowb, rangespec_t* ranges)
{
	bucket_range_desc_t* b =
		(bucket_range_desc_t*) cf_malloc(n_ranges * sizeof(bucket_range_desc_t));

	delay_t range_start = lowb;
	uint32_t total_buckets = 0;
	for (size_t i = 0; i < n_ranges; i++) {
		delay_t range_end = ranges[i].upper_bound;
		delay_t width = ranges[i].bucket_width;

		// validate the ranges provided by the user (in ascending order,
		// non-zero bucket width, and bucket width evenly dividing the range)
		if (range_end <= range_start ||
				width == 0 ||
				((range_end - range_start) % width) != 0) {
			cf_free(b);
			return -1;
		}

		uint32_t n_buckets = (range_end - range_start) / width;

		b[i].lower_bound = range_start;
		b[i].bucket_width = width;
		b[i].offset = total_buckets;
		b[i].n_buckets = n_buckets;

		total_buckets += n_buckets;
		range_start = range_end;
	}

	h->buckets = (_Atomic(uint32_t)*) cf_calloc(total_buckets, sizeof(_Atomic(uint32_t)));

	for (uint32_t i = 0; i < total_buckets; i++) {
		atomic_init(&h->buckets[i], 0);
	}

	h->bounds = b;
	h->name = NULL;
	h->range_min = lowb;
	h->range_max = range_start;
	atomic_init(&h->underflow_cnt, 0);
	atomic_init(&h->overflow_cnt, 0);
	h->n_bounds = n_ranges;
	h->n_buckets = total_buckets;
	return 0;
}

void
histogram_free(histogram_t* h)
{
	if (h->name != NULL) {
		cf_free(h->name);
	}
	cf_free(h->buckets);
	cf_free(h->bounds);
}

void
histogram_clear(histogram_t* h)
{
	for (uint32_t i = 0; i < h->n_buckets; i++) {
		h->buckets[i] = 0;
	}

	h->underflow_cnt = 0;
	h->overflow_cnt  = 0;
}


void
histogram_set_name(histogram_t* h, const char* name)
{
	if (h->name != NULL) {
		cf_free(h->name);
	}
	h->name = cf_strdup(name);
}

void
histogram_incr(histogram_t* h, delay_t elapsed_us)
{
	int32_t bucket_idx = _histogram_get_index(h, elapsed_us);
	_Atomic(uint32_t)* bucket = __histogram_get_bucket(h, bucket_idx);

	(*bucket)++;
}

delay_t
histogram_get_count(histogram_t* h, uint64_t bucket_idx)
{
	_Atomic(uint32_t)* bucket = __histogram_get_bucket(h, bucket_idx);

	return *bucket;
}

uint64_t
histogram_calc_total(const histogram_t* h)
{
	uint64_t total;

	total = h->underflow_cnt + h->overflow_cnt;

	for (uint32_t i = 0; i < h->n_buckets; i++) {
		total += h->buckets[i];
	}

	return total;
}

void
histogram_print(const histogram_t* h, uint64_t period_duration_us, FILE* out_file)
{
	uint64_t total_cnt = histogram_calc_total(h);
	_print_header(h, period_duration_us, total_cnt, out_file);

	if (h->underflow_cnt > 0) {
		fprintf(out_file, ", 0:%" PRIu32 , h->underflow_cnt);
	}

	uint32_t idx = 0;
	for (uint32_t i = 0; i < h->n_bounds; i++) {
		bucket_range_desc_t * r = &h->bounds[i];

		for (uint32_t j = 0; j < r->n_buckets; j++) {
			if (h->buckets[idx] > 0) {
				fprintf(out_file,
						", %"PRIu64 ":%" PRIu32,
						r->lower_bound + j * r->bucket_width,
						h->buckets[idx]);
			}
			idx++;
		}
	}

	if (h->overflow_cnt > 0) {
		fprintf(out_file, ", %" PRIu64 ":%" PRIu32, h->range_max,
				h->overflow_cnt);
	}

	fprintf(out_file, "\n");
}

void
histogram_print_clear(histogram_t* h, uint64_t period_duration_us, FILE* out_file)
{
	uint64_t total_cnt = 0;
	uint32_t* cnts = (uint32_t*) cf_malloc(h->n_buckets * sizeof(uint32_t));

	/*
	 * to avoid race conditions/inconsistencies in the total count of bucket
	 * values and the values in the buckets, we first go through the entire
	 * histogram and atomically swap out buckets with 0, accumulating the
	 * counts that were read in and storing the bucket values in an array
	 * of counts (to be read later when the individual buckets are printed)
	 */
	uint32_t underflow_cnt = atomic_exchange(&h->underflow_cnt, 0);
	total_cnt += underflow_cnt;

	for (uint32_t idx = 0; idx < h->n_buckets; idx++) {
		// atomic swap 0 in for the bucket value
		uint32_t cnt = atomic_exchange(&h->buckets[idx], 0);
		cnts[idx] = cnt;
		total_cnt += cnt;
	}
	uint32_t overflow_cnt = atomic_exchange(&h->overflow_cnt, 0);
	total_cnt += overflow_cnt;

	_print_header(h, period_duration_us, total_cnt, out_file);

	if (underflow_cnt > 0) {
		fprintf(out_file, ", 0:%" PRIu32, underflow_cnt);
	}

	uint32_t idx = 0;
	for (uint32_t i = 0; i < h->n_bounds; i++) {
		bucket_range_desc_t * r = &h->bounds[i];

		for (uint32_t j = 0; j < r->n_buckets; j++) {
			uint32_t cnt = cnts[idx];
			if (cnt > 0) {
				fprintf(out_file,
						", %" PRIu64 ":%" PRIu32,
						r->lower_bound + j * r->bucket_width, cnt);
			}
			idx++;
		}
	}

	if (overflow_cnt > 0) {
		fprintf(out_file, ", %" PRIu64 ":%" PRIu32, h->range_max, overflow_cnt);
	}

	fprintf(out_file, "\n");

	cf_free(cnts);
}

void
histogram_print_info(const histogram_t* h, FILE* out_file)
{

	fprintf(out_file,
			"%s:\n"
			"\tTotal num buckets: %" PRIu32 "\n"
			"\tRange min: %" PRIu64 "us\n"
			"\tRange max: %" PRIu64 "us\n",
			h->name != NULL ? h->name : "Histogram",
			h->n_buckets,
			h->range_min,
			h->range_max);

	for (uint32_t i = 0; i < h->n_bounds; i++) {
		bucket_range_desc_t* r = &h->bounds[i];

		fprintf(out_file,
				"\tBucket range %" PRId32 ":\n"
				"\t\tRange min: %" PRIu64 "us\n"
				"\t\tRange max: %" PRIu64 "us\n"
				"\t\tBucket width: %" PRIu64 "us\n"
				"\t\tNum buckets: %" PRIu32 "\n",
				i,
				r->lower_bound,
				r->lower_bound + r->bucket_width * r->n_buckets,
				r->bucket_width,
				r->n_buckets);
	}
}


//==========================================================
// Local helpers.
//

LOCAL_HELPER int32_t
_histogram_get_index(histogram_t* h, delay_t elapsed_us)
{
	int32_t bin_idx;
	delay_t lower_bound;
	int32_t bin_offset;

	// find which range index belongs in. Expecting a small number
	// of buckets-size ranges, so do a simple linear search

	if (elapsed_us < h->range_min) {
		return UNDERFLOW_IDX;
	}
	if (elapsed_us >= h->range_max) {
		return OVERFLOW_IDX;
	}

	bin_idx = h->n_bounds - 1;
	while (elapsed_us < (lower_bound = h->bounds[bin_idx].lower_bound)) {
		bin_idx--;
	}

	bin_offset = (elapsed_us - lower_bound) / h->bounds[bin_idx].bucket_width;
	return h->bounds[bin_idx].offset + bin_offset;
}

LOCAL_HELPER void
_print_header(const histogram_t* h, uint64_t period_duration_us, uint64_t total_cnt,
		FILE* out_file)
{
	if (h->name != NULL) {
		fprintf(out_file, "%s ", h->name);
	}
	fprintf(out_file, "%s, %gs, %" PRIu64, utc_time_str(time(NULL)),
			period_duration_us / 1000000.f, total_cnt);
}

