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
	uint32_t * buckets;
	struct bucket_range_desc * bounds;

	// name to be printed before each output line of this histogram
	char * name;

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
	// total number of buckets in the histogram;
	uint32_t n_buckets;
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
 *
 * returns 0 on success and -1 on error
 */
int histogram_init(histogram * h, size_t n_ranges, delay_t lowb, rangespec_t * ranges);

void histogram_free(histogram * h);

/*
 * resets all bucket counts to 0
 */
void histogram_clear(histogram * h);


/*
 * sets the name of the histogram, to be printed at the beginning of each
 * histogram_print call
 */
void histogram_set_name(histogram * h, const char * name);

/*
 * Calculates the totals of all buckets by traversing them and adding. This
 * information is not stored in the histogram because it would require two
 * atomic increments per insertion rather than one, and that atomic increment
 * would be on highly contentious memory
 */
uint64_t histogram_calc_total(const histogram * h);

/*
 * insert the delay into the histogram in a thread-safe manner
 */
void histogram_add(histogram * h, delay_t elapsed_us);

/*
 * returns the count in the bucket of the given index
 */
delay_t histogram_get_count(histogram * h, uint32_t bucket_idx);

/*
 * prints the histogram in a condensed format, requires period duration in
 * seconds (i.e. how long this histogram has been accumulating)
 */
void histogram_print(const histogram * h, uint32_t period_duration, FILE * out_file);

/*
 * prints the histogram, clearing the buckets as their values are read. This
 * method is thread-safe and is intended to be used in a context with
 * concurrent writers executing simultaneously. This guarantees that no writes
 * to the histogram will be missed
 */
void histogram_print_clear(histogram * h, uint32_t period_duration, FILE * out_file);

/*
 * print info about the histogram and how it is constructed
 */
void histogram_print_info(const histogram * h, FILE * out_file);

void histogram_print_dbg(const histogram * h);

