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


#ifdef _TEST

// when testing, make all functions global symbols
#define LOCAL_HELPER

#else

// normally local helper functions should be local symbols
#define LOCAL_HELPER static

#endif /* _TEST */


#define _STR(x) #x
#define STR(x) _STR(x)

#define STATIC_ASSERT(expr) \
	extern void static_assert_ ## __LINE__(int STATIC_ASSERTION_FAILED[(expr)?1:-1])

typedef uint64_t ptr_int_t;

#define as_assert(expr) \
	do { \
		if (UNLIKELY(!(expr))) { \
			fprintf(stderr, __FILE__ ":" STR(__LINE__) " assertion failed: " \
					"%s\n", STR(expr)); \
			abort(); \
		} \
	} while(0)


#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define MAX(a, b) ((a) < (b) ? (b) : (a))

#define LIKELY(expr) __builtin_expect((expr), 1)
#define UNLIKELY(expr) __builtin_expect((expr), 0)

/*
 * returns a if a is positive or 0 if a is negative
 */
static inline uint64_t ramp(uint64_t a)
{
	return (~(((int64_t) a) >> 63)) & a;
}


static inline uint64_t timespec_to_us(const struct timespec* ts)
{
	return (ts->tv_sec * 1000000LU) + (ts->tv_nsec / 1000);
}

static inline void timespec_add_us(struct timespec* ts, uint64_t us)
{
	uint64_t nsec = ts->tv_nsec;
	nsec += us * 1000;
	ts->tv_sec += nsec / 1000000000LU;
	ts->tv_nsec = nsec % 1000000000LU;
}


/*
 * returns the length of the given number were it to be printed in decimal
 */
int dec_display_len(size_t number);


void blog_detailv(as_log_level level, const char* fmt, va_list ap);
void blog_detail(as_log_level level, const char* fmt, ...);

#define blog_info(_fmt, ...) { blog_detail(AS_LOG_LEVEL_INFO, _fmt, ##__VA_ARGS__); }
#define blog_error(_fmt, ...) { blog_detail(AS_LOG_LEVEL_ERROR, _fmt, ##__VA_ARGS__); }

static inline const char*
boolstring(bool val)
{
	if (val) {
		return "true";
	}
	else {
		return "false";
	}
}


#ifndef __linux__

char* strchrnul(char* s, int c_in);

#endif /* __linux__ */


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
 * compares two as_val's, returning true if they are the same
 */
int
as_val_cmp(const as_val* v1, const as_val* v2);


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

/*
 * parses a literal string, which must be surrounded by double quotes,
 * returning the parsed string (or NULL on error). If endptr is not null, then
 * it will point to the character past the last character parsed
 */
char* parse_string_literal(const char* restrict str,
		const char** restrict endptr);

void print_hdr_percentiles(struct hdr_histogram* h, const char* name,
		uint64_t elapsed_s, as_vector* percentiles, FILE *out_file);

