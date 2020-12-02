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
#include <stdio.h>
#include <time.h>

#include <citrusleaf/alloc.h>
#include <aerospike/as_atomic.h>

#include "histogram.h"
#include "common.h"

/*
 * internal struct to describe the layout of the histogram, with redundancy
 * for performance reasons
 */
typedef struct bucket_range_desc {
	delay_t lower_bound;
	delay_t bucket_width;
	uint32_t offset;
	uint32_t n_buckets;
} bucket_range_desc_t;

#define UNDERFLOW_IDX (-2)
#define OVERFLOW_IDX  (-1)


STATIC_ASSERT(offsetof(histogram, underflow_cnt) + sizeof(uint32_t) ==
		offsetof(histogram, overflow_cnt));

inline uint32_t *
__attribute__((always_inline))
__histogram_get_bucket(histogram * h, int32_t idx) {
	return (idx < 0) ? (((uint32_t *) (((ptr_int_t) h) + offsetof(histogram, underflow_cnt)
				+ 2 * sizeof(uint32_t))) + idx) : &h->buckets[idx];
}


int
histogram_init(histogram * h, size_t n_ranges, delay_t lowb, rangespec_t * ranges)
{
	bucket_range_desc_t * b =
		(bucket_range_desc_t *) cf_malloc(n_ranges * sizeof(bucket_range_desc_t));

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

	h->buckets = (uint32_t *) cf_calloc(total_buckets, sizeof(uint32_t));
	h->bounds = b;
	h->name = NULL;
	h->range_min = lowb;
	h->range_max = range_start;
	h->underflow_cnt = 0;
	h->overflow_cnt  = 0;
	h->n_bounds = n_ranges;
	h->n_buckets = total_buckets;
	return 0;
}

void
histogram_free(histogram * h)
{
	if (h->name != NULL) {
		cf_free(h->name);
	}
	cf_free(h->buckets);
	cf_free(h->bounds);
}

void
histogram_clear(histogram * h)
{
	memset(h->buckets, 0, h->n_buckets * sizeof(uint32_t));
	h->underflow_cnt = 0;
	h->overflow_cnt  = 0;
}


void
histogram_set_name(histogram * h, const char * name)
{
	if (h->name != NULL) {
		cf_free(h->name);
	}
	h->name = cf_strdup(name);
}

static int32_t
_histogram_get_index(histogram * h, delay_t elapsed_us)
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

void
histogram_add(histogram * h, delay_t elapsed_us)
{
	int32_t bucket_idx = _histogram_get_index(h, elapsed_us);
	uint32_t * bucket = __histogram_get_bucket(h, bucket_idx);

	as_incr_uint32(bucket);
}

delay_t
histogram_get_count(histogram * h, uint32_t bucket_idx)
{
	uint32_t * bucket = __histogram_get_bucket(h, bucket_idx);

	return as_load_uint32(bucket);
}

uint64_t
histogram_calc_total(const histogram * h)
{
	uint64_t total;

	total = h->underflow_cnt + h->overflow_cnt;
	
	for (uint32_t i = 0; i < h->n_buckets; i++) {
		total += h->buckets[i];
	}

	return total;
}


static void
_print_header(const histogram * h, uint32_t period_duration, uint64_t total_cnt,
		FILE * out_file)
{
	struct tm * utc;
	time_t t;

	t = time(NULL);
	utc = gmtime(&t);

	if (h->name != NULL) {
		fblog(out_file, "%s ", h->name);
	}
	fblog(out_file, "%.24s, %us, %lu", asctime(utc), period_duration,
			total_cnt);
}


void
histogram_print(const histogram * h, uint32_t period_duration, FILE * out_file)
{
	uint64_t total_cnt = histogram_calc_total(h);
	_print_header(h, period_duration, total_cnt, out_file);

	if (h->underflow_cnt > 0) {
		fblog(out_file, ", 0:%u", h->underflow_cnt);
	}

	uint32_t idx = 0;
	for (uint32_t i = 0; i < h->n_bounds; i++) {
		bucket_range_desc_t * r = &h->bounds[i];

		for (uint32_t j = 0; j < r->n_buckets; j++) {
			if (h->buckets[idx] > 0) {
				fblog(out_file,
						", %lu:%u",
						r->lower_bound + j * r->bucket_width,
						h->buckets[idx]);
			}
			idx++;
		}
	}

	if (h->overflow_cnt > 0) {
		fblog(out_file, ", %lu:%u", h->range_max, h->overflow_cnt);
	}

	fblog(out_file, "\n");
}

void
histogram_print_clear(histogram * h, uint32_t period_duration, FILE * out_file)
{
	uint64_t total_cnt = 0;
	uint32_t * cnts = (uint32_t *) cf_malloc(h->n_buckets * sizeof(uint32_t));

	/*
	 * to avoid race conditions/inconsistencies in the total count of bucket
	 * values and the values in the buckets, we first go through the entire
	 * histogram and atomically swap out buckets with 0, accumulating the
	 * counts that were read in and storing the bucket values in an array
	 * of counts (to be read later when the individual buckets are printed)
	 */
	uint32_t underflow_cnt = as_fas_uint32(&h->underflow_cnt, 0);
	total_cnt += underflow_cnt;

	for (uint32_t idx = 0; idx < h->n_buckets; idx++) {
		// atomic swap 0 in for the bucket value
		uint32_t cnt = as_fas_uint32(&h->buckets[idx], 0);
		cnts[idx] = cnt;
		total_cnt += cnt;
	}
	uint32_t overflow_cnt = as_fas_uint32(&h->overflow_cnt, 0);
	total_cnt += overflow_cnt;

	_print_header(h, period_duration, total_cnt, out_file);

	if (underflow_cnt > 0) {
		fblog(out_file, ", 0:%u", underflow_cnt);
	}

	uint32_t idx = 0;
	for (uint32_t i = 0; i < h->n_bounds; i++) {
		bucket_range_desc_t * r = &h->bounds[i];

		for (uint32_t j = 0; j < r->n_buckets; j++) {
			uint32_t cnt = cnts[idx];
			if (cnt > 0) {
				fblog(out_file,
						", %lu:%u",
						r->lower_bound + j * r->bucket_width, cnt);
			}
			idx++;
		}
	}

	if (overflow_cnt > 0) {
		fblog(out_file, ", %lu:%u", h->range_max, overflow_cnt);
	}

	fblog(out_file, "\n");

	cf_free(cnts);
}

void
histogram_print_info(const histogram * h, FILE * out_file)
{

	fblog(out_file,
			"%s:\n"
			"\tTotal num buckets: %u\n"
			"\tRange min: %luus\n"
			"\tRange max: %luus\n",
			h->name != NULL ? h->name : "Histogram",
			h->n_buckets,
			h->range_min,
			h->range_max);

	for (uint32_t i = 0; i < h->n_bounds; i++) {
		bucket_range_desc_t * r = &h->bounds[i];

		fblog(out_file,
				"\tBucket range %d:\n"
				"\t\tRange min: %luus\n"
				"\t\tRange max: %luus\n"
				"\t\tBucket width: %luus\n"
				"\t\tNum buckets: %u\n",
				i,
				r->lower_bound,
				r->lower_bound + r->bucket_width * r->n_buckets,
				r->bucket_width,
				r->n_buckets);
	}
}

#define BUCKETS_PER_LINE 16

void
histogram_print_dbg(const histogram * h)
{
	blog(
			"Histogram:\n"
			"Range: %luus - %luus\n"
			"\n"
			" < %luus:\n"
			" [ %6u ]\n"
			" >= %luus:\n"
			" [ %6u ]\n",
			h->range_min, h->range_max,
			h->range_min,
			h->underflow_cnt,
			h->range_max,
			h->overflow_cnt);

	for (size_t i = 0; i < h->n_bounds; i++) {
		bucket_range_desc_t * r = &h->bounds[i];
		blog(
				"%luus <= x < %luus (width=%luus):\n"
				" [ ",
				r->lower_bound, r->lower_bound + r->bucket_width * r->n_buckets,
				r->bucket_width);

		for (size_t j = 0; j < r->n_buckets; j++) {
			blog("%6u",
					h->buckets[r->offset + j]);
			if (j != r->n_buckets - 1) {
				blog(", ");
				if (j % BUCKETS_PER_LINE == BUCKETS_PER_LINE - 1) {
					blog("\n   ");
				}
			}
		}
		blog(" ]\n");
	}
}

