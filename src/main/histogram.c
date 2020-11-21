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
#include <aerospike/as_atomic.h>
#include <stdio.h>

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
				+ 2 * sizeof(uint32_t))) + idx) : &h->bins[idx];
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

	h->bins = (uint32_t *) safe_calloc(n_ranges, sizeof(uint32_t));
	h->bounds = b;
	h->range_min = lowb;
	h->range_max = range_start;
	h->underflow_cnt = 0;
	h->overflow_cnt  = 0;
	h->n_bounds = n_ranges;
}

void
histogram_free(histogram * h)
{
	free(h->bins);
	free(h->bounds);
}

static int32_t
_histogram_get_index(histogram * h, delay_t elapsed_us)
{
	int32_t bin_idx;
	delay_t lower_bound;
	int32_t bin_offset;

	// find which range index belongs in. Expecting a small number
	// of bins-size ranges, so do a simple linear search

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

