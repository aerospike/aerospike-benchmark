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

#include "aerospike/as_log.h"
#include <citrusleaf/alloc.h>

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



void blog_line(const char* fmt, ...);
void blog_detailv(as_log_level leve, const char* fmt, va_list ap);
void blog_detail(as_log_level level, const char* fmt, ...);

#define blog(_fmt, ...) { printf(_fmt, ##__VA_ARGS__); }
#define blog_info(_fmt, ...) { blog_detail(AS_LOG_LEVEL_INFO, _fmt, ##__VA_ARGS__); }
#define blog_error(_fmt, ...) { blog_detail(AS_LOG_LEVEL_ERROR, _fmt, ##__VA_ARGS__); }



inline void *
__attribute__((always_inline))
safe_malloc(size_t sz) {
	void * ptr = cf_malloc(sz);
	if (ptr == NULL) {
		fprintf(stderr, "Unable to malloc %zu bytes\n", sz);
		abort();
	}
	return ptr;
}

inline void *
__attribute__((always_inline))
safe_calloc(size_t nmemb, size_t sz) {
	void * ptr = cf_calloc(nmemb, sz);
	if (ptr == NULL) {
		fprintf(stderr, "Unable to calloc %zu bytes\n", sz);
		abort();
	}
	return ptr;
}

