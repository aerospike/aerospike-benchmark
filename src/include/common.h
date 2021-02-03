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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "aerospike/as_record.h"
#include "aerospike/as_log.h"
#include "aerospike/as_random.h"
#include "aerospike/as_vector.h"
#include <citrusleaf/alloc.h>

#include <hdr_histogram/hdr_histogram.h>

#define _STR(x) #x
#define STR(x) _STR(x)

#define STATIC_ASSERT(expr) \
	extern void static_assert_ ## __LINE__(int STATIC_ASSERTION_FAILED[(expr)?1:-1])

typedef uint64_t ptr_int_t;

#define as_assert(expr) \
	do { \
		if (!__builtin_expect((expr), 1)) { \
			fprintf(stderr, __FILE__ ":" STR(__LINE__) " assertion failed: " \
					"%s\n", STR(expr)); \
			abort(); \
		} \
	} while(0)


#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define MAX(a, b) ((a) < (b) ? (b) : (a))

/*
 * returns a if a is positive and 0 if a is negative
 */
static inline uint64_t ramp(uint64_t a)
{
	return (~(((int64_t) a) >> 63)) & a;
}


/*
 * returns the length of the given number were it to be printed in decimal
 */
int dec_display_len(size_t number);


void blog_line(const char* fmt, ...);
void blog_detailv(as_log_level leve, const char* fmt, va_list ap);
void blog_detail(as_log_level level, const char* fmt, ...);

#define blog(_fmt, ...) { printf(_fmt, ##__VA_ARGS__); }
#define fblog(_file, _fmt, ...) { fprintf(_file, _fmt, ##__VA_ARGS__); }
#define blog_info(_fmt, ...) { blog_detail(AS_LOG_LEVEL_INFO, _fmt, ##__VA_ARGS__); }
#define blog_error(_fmt, ...) { blog_detail(AS_LOG_LEVEL_ERROR, _fmt, ##__VA_ARGS__); }


#define UTC_STR_LEN 72
const char* utc_time_str(time_t t);

/*
 * generate a random number within the range [0, max)
 */
uint32_t gen_rand_range(as_random*, uint32_t max);

/*
 * same as gen_rand_range, but for 64-bit numbers
 */
uint64_t gen_rand_range_64(as_random*, uint64_t max);


/*
 * given the length of the bin base name in characters and the number of bins,
 * determines whether any key will be too large to fit in an as_bin_name buffer
 */
bool bin_name_too_large(size_t name_len, uint32_t n_bins);

/*
 * given the base name of bins (bin_name) and the bin number (starting from 1),
 * populate name buf with the name of that bin
 *
 * bin name format:
 * 	1: <bin_name>
 * 	2: <bin_name>_2
 * 	3: <bin_name>_3
 * 	...
 */
void gen_bin_name(as_bin_name name_buf, const char* bin_name, uint32_t bin_num);

void print_hdr_percentiles(struct hdr_histogram* h, const char* name,
		uint64_t elapsed_s, as_vector* percentiles, FILE *out_file);

