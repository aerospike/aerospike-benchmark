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

/*
 * delays are measured in microseconds
 */
typedef uint64_t delay_t;

typedef struct histogram_t {
	
} histogram;

typedef struct rangespec_t {
	delay_t upper_bound;
	delay_t bucket_width;
} rangespec;


/*
 * initializes a histogram with "n_ranges" ranges with different bucket widths.
 *
 * i.e. if you want a histogram with 3 ranges
 *     100us - 4ms with 100us buckets
 *     4ms - 64ms with 1ms buckets
 *     64ms - 128ms with 4ms buckts
 * you would write
 *     histogram_init(h, 100, 3, {
 *         { .upper_bound = 4000,   .bucket_width = 100  },
 *         { .upper_bound = 64000,  .bucket_width = 1000 },
 *         { .upper_bound = 128000, .bucket_width = 4000 },
 *     });
 */
void histogram_init(histogram * h, delay_t lowb, size_t n_ranges, rangespec * ranges);

void histogram_free(histogram * h);

/*
 * insert the delay into the histogram in a thread-safe manner
 */
void histogram_add(histogram * h, delay_t elapsed_us);

void histogram_print(histogram * h);

