/*******************************************************************************
 * Copyright 2020 by Aerospike.
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

#include <stddef.h>
#include <stdint.h>

/*
 * delays are measured in microseconds
 */
typedef uint64_t delay_t;

typedef struct rangespec {
	delay_t upper_bound;
	delay_t bucket_width;
} rangespec_t;


typedef struct histogram {
	uint32_t * bins;
	struct bucket_range_desc * bounds;

	// inclusive lower bound on the histogram range
	delay_t range_min;
	// exclusive upper bound on the histogram range
	delay_t range_max;

	// a count of the number of data points below the minimum bucket
	uint32_t underflow_cnt;
	// a count of the number of data points above the maximum bucket
	uint32_t overflow_cnt;

	// the number of elements in the bounds array
	uint32_t n_bounds;
} histogram;


/*
 * initializes a histogram with "n_ranges" ranges with different bucket widths.
 *
 * i.e. if you want a histogram with 3 ranges
 *     100us - 4ms with 100us buckets
 *     4ms - 64ms with 1ms buckets
 *     64ms - 128ms with 4ms buckts
 * you would write
 *     histogram_init(h, 3, 100, (rangespec_t[]) {
 *         { .upper_bound = 4000,   .bucket_width = 100  },
 *         { .upper_bound = 64000,  .bucket_width = 1000 },
 *         { .upper_bound = 128000, .bucket_width = 4000 },
 *     });
 */
void histogram_init(histogram * h, size_t n_ranges, delay_t lowb, rangespec_t * ranges);

void histogram_free(histogram * h);

/*
 * insert the delay into the histogram in a thread-safe manner
 */
void histogram_add(histogram * h, delay_t elapsed_us);

void histogram_print(histogram * h);

