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


void
histogram_init(histogram * h, size_t n_ranges, delay_t lowb, rangespec_t * ranges)
{
	bucket_range_desc_t * b =
		(bucket_range_desc_t *) safe_malloc(n_ranges * sizeof(bucket_range_desc_t));

	delay_t range_start = lowb;
	uint32_t total_buckets = 0;
	for (size_t i = 0; i < n_ranges; i++) {
		delay_t range_end = ranges[i].upper_bound;
		delay_t width = ranges[i].bucket_width;

		as_assert(range_end > range_start);
		as_assert(width > 0);
		as_assert(((range_end - range_start) % width) == 0);

		uint32_t n_buckets = (range_end - range_start) / width;

		b[i].lower_bound = range_start;
		b[i].bucket_width = width;
		b[i].offset = total_buckets;
		b[i].n_buckets = n_buckets;

		total_buckets += n_buckets;
		range_start = range_end;
	}

	h->buckets = (uint32_t *) safe_calloc(total_buckets, sizeof(uint32_t));
	h->bounds = b;
	h->range_min = lowb;
	h->range_max = range_start;
	h->underflow_cnt = 0;
	h->overflow_cnt  = 0;
	h->n_bounds = n_ranges;
	h->n_buckets = total_buckets;
}

void
histogram_free(histogram * h)
{
	free(h->buckets);
	free(h->bounds);
}

void
histogram_clear(histogram * h)
{
	memset(h->buckets, 0, h->n_buckets * sizeof(uint32_t));
	h->underflow_cnt = 0;
	h->overflow_cnt  = 0;
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


void
histogram_print(const histogram * h, uint32_t period_duration)
{
	struct tm * utc;
	time_t t;
	uint64_t total_cnt;

	t = time(NULL);
	utc = gmtime(&t);
	
	total_cnt = histogram_calc_total(h);
	printf("%.24s, %us, %lu", asctime(utc), period_duration, total_cnt);

	if (h->underflow_cnt > 0) {
		printf(", 0:%u", h->underflow_cnt);
	}

	uint32_t idx = 0;
	for (uint32_t i = 0; i < h->n_bounds; i++) {
		bucket_range_desc_t * r = &h->bounds[i];

		for (uint32_t j = 0; j < r->n_buckets; j++) {
			if (h->buckets[idx] > 0) {
				printf(", %lu:%u",
						r->lower_bound + j * r->bucket_width,
						h->buckets[idx]);
			}
			idx++;
		}
	}

	if (h->overflow_cnt > 0) {
		printf(", %lu:%u", h->range_max, h->overflow_cnt);
	}

	printf("\n");
}

#define BUCKETS_PER_LINE 16

void
histogram_print_dbg(const histogram * h)
{
	printf(
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
		printf(
				"%luus <= x < %luus (width=%luus):\n"
				" [ ",
				r->lower_bound, r->lower_bound + r->bucket_width * r->n_buckets,
				r->bucket_width);

		for (size_t j = 0; j < r->n_buckets; j++) {
			printf("%6u",
					h->buckets[r->offset + j]);
			if (j != r->n_buckets - 1) {
				printf(", ");
				if (j % BUCKETS_PER_LINE == BUCKETS_PER_LINE - 1) {
					printf("\n   ");
				}
			}
		}
		printf(" ]\n");
	}
}

