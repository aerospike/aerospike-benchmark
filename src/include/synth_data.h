/*******************************************************************************
 * Copyright 2026 by Aerospike.
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

#include <stdint.h>


typedef struct synth_dict_s {
	const char* pool;
	const uint32_t* off;
	uint32_t n;
	uint32_t max_len;
} synth_dict_t;


static inline const char*
synth_dict_word(const synth_dict_t* d, uint32_t i, uint32_t* len)
{
	*len = d->off[i + 1] - d->off[i] - 1;
	return d->pool + d->off[i];
}

static inline uint32_t
synth_dict_index(const synth_dict_t* d, uint32_t r)
{
	return (uint32_t) (((uint64_t) r * d->n) >> 32);
}

#include <synth_data_tables.h>
