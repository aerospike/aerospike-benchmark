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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <aerospike/as_random.h>
#include <aerospike/as_val.h>


typedef enum {
	SYNTH_FIRST_NAME,
	SYNTH_LAST_NAME,
	SYNTH_FULL_NAME,
	SYNTH_USERNAME,
	SYNTH_EMAIL,
	SYNTH_PHONE,
	SYNTH_STREET,
	SYNTH_CITY,
	SYNTH_STATE,
	SYNTH_STATE_ABBR,
	SYNTH_ZIP,
	SYNTH_COUNTRY,
	SYNTH_COUNTRY_CODE,
	SYNTH_LAT,
	SYNTH_LON,
	SYNTH_GEOJSON,
	SYNTH_GEO_CIRCLE,
	SYNTH_COMPANY,
	SYNTH_JOB_TITLE,
	SYNTH_IPV4,
	SYNTH_IPV6,
	SYNTH_MAC,
	SYNTH_URL,
	SYNTH_DOMAIN,
	SYNTH_UUID,
	SYNTH_WORD,
	SYNTH_WORDS,
	SYNTH_SENTENCE,
	SYNTH_LOREM,
	SYNTH_PRODUCT,
	SYNTH_COLOR,
	SYNTH_CREDIT_CARD,
	SYNTH_DATE,
	SYNTH_TIMESTAMP,
	SYNTH_NOW,
	SYNTH_INT,
	SYNTH_DOUBLE,
	SYNTH_PICK,
	SYNTH_FMT,
	SYNTH_N_KINDS
} synth_kind_t;

typedef enum {
	SYNTH_OUT_STR,
	SYNTH_OUT_INT,
	SYNTH_OUT_DOUBLE,
	SYNTH_OUT_GEO
} synth_out_t;

struct synth_spec_s;

struct synth_tpl_seg_s {
	char* lit;
	uint32_t lit_len;
	struct synth_spec_s* ref;
};

typedef struct synth_spec_s {
	uint8_t kind;
	uint8_t out_type;
	bool has_args;
	uint32_t max_len;
	union {
		uint32_t n;
		struct {
			int64_t min;
			int64_t max;
		} irange;
		struct {
			double min;
			double max;
		} drange;
		struct {
			int32_t lat_min;
			int32_t lat_max;
			int32_t lon_min;
			int32_t lon_max;
			uint32_t r_min;
			uint32_t r_max;
		} geo;
		struct {
			int64_t min;
			int64_t max;
			char* fmt;
		} date;
		struct {
			uint32_t n;
			bool weighted;
			char** items;
			uint32_t* lens;
			uint32_t* cum;
			uint32_t total;
		} pick;
		struct {
			char* src;
			uint32_t n_segs;
			struct synth_tpl_seg_s* segs;
		} tpl;
	};
} synth_spec_t;


/*
 * parses a generator token starting at str, which must point at '@'. On
 * success returns 0 and sets *endptr past the token. On failure returns -1 and
 * sets *err_msg and *err_loc for the caller to report.
 */
int synth_parse(const char* str, const char** endptr, synth_spec_t* out,
		const char** err_msg, const char** err_loc);

as_val* synth_gen_val(const synth_spec_t* spec, as_random* random);

void synth_spec_free(synth_spec_t* spec);

/*
 * prints the canonical form of spec, returning the remaining buffer size
 */
size_t synth_spec_sprint(const synth_spec_t* spec, char** out_str,
		size_t str_size);

/*
 * number of distinct values the generator can produce, UINT64_MAX when
 * effectively unbounded
 */
uint64_t synth_spec_cardinality(const synth_spec_t* spec);

static inline uint8_t
synth_spec_out_type(const synth_spec_t* spec)
{
	return spec->out_type;
}

const char* synth_kind_name(uint8_t kind);

#ifdef _TEST
bool synth_check_val(const synth_spec_t* spec, const as_val* val);
#endif /* _TEST */
