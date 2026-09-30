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

//==========================================================
// Includes.
//

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <aerospike/as_double.h>
#include <aerospike/as_geojson.h>
#include <aerospike/as_integer.h>
#include <aerospike/as_string.h>
#include <aerospike/as_vector.h>
#include <citrusleaf/alloc.h>
#include <citrusleaf/cf_clock.h>

#include <common.h>
#include <synth_data.h>
#include <synth_gen.h>


//==========================================================
// Typedefs & constants.
//

#define ARGS_VARIADIC 0xff

#define ARG_INT    0x0
#define ARG_DOUBLE 0x1
#define ARG_STR    0x2

#define US_LAT_MIN 24000000
#define US_LAT_MAX 49000000
#define US_LON_MIN (-125000000)
#define US_LON_MAX (-66000000)

#define DEFAULT_TS_MIN 1577836800LL
#define DEFAULT_TS_MAX 1893455999LL

#define DEFAULT_WORDS 3
#define DEFAULT_LOREM 100
#define MAX_WORDS 1000
#define MAX_LOREM 1000000

#define DEFAULT_R_MIN 100
#define DEFAULT_R_MAX 5000
#define MAX_RADIUS 20000000

#define DATE_BUF_LEN 64

#define INT_TEXT_MAX 20
#define DOUBLE_TEXT_MAX 24
#define FMT_DOUBLE_ABS_MAX 9e16

#define GEOJSON_POINT_MAX 64
#define GEOJSON_CIRCLE_MAX 96

typedef struct synth_kind_def_s {
	const char* name;
	uint8_t out_type;
	uint8_t min_args;
	uint8_t max_args;
} synth_kind_def_t;

static const synth_kind_def_t synth_kinds[SYNTH_N_KINDS] = {
	[SYNTH_FIRST_NAME]   = { "first_name",   SYNTH_OUT_STR,    0, 0 },
	[SYNTH_LAST_NAME]    = { "last_name",    SYNTH_OUT_STR,    0, 0 },
	[SYNTH_FULL_NAME]    = { "full_name",    SYNTH_OUT_STR,    0, 0 },
	[SYNTH_USERNAME]     = { "username",     SYNTH_OUT_STR,    0, 0 },
	[SYNTH_EMAIL]        = { "email",        SYNTH_OUT_STR,    0, 0 },
	[SYNTH_PHONE]        = { "phone",        SYNTH_OUT_STR,    0, 0 },
	[SYNTH_STREET]       = { "street",       SYNTH_OUT_STR,    0, 0 },
	[SYNTH_CITY]         = { "city",         SYNTH_OUT_STR,    0, 0 },
	[SYNTH_STATE]        = { "state",        SYNTH_OUT_STR,    0, 0 },
	[SYNTH_STATE_ABBR]   = { "state_abbr",   SYNTH_OUT_STR,    0, 0 },
	[SYNTH_ZIP]          = { "zip",          SYNTH_OUT_STR,    0, 0 },
	[SYNTH_COUNTRY]      = { "country",      SYNTH_OUT_STR,    0, 0 },
	[SYNTH_COUNTRY_CODE] = { "country_code", SYNTH_OUT_STR,    0, 0 },
	[SYNTH_LAT]          = { "lat",          SYNTH_OUT_DOUBLE, 0, 2 },
	[SYNTH_LON]          = { "lon",          SYNTH_OUT_DOUBLE, 0, 2 },
	[SYNTH_GEOJSON]      = { "geojson",      SYNTH_OUT_GEO,    0, 4 },
	[SYNTH_GEO_CIRCLE]   = { "geo_circle",   SYNTH_OUT_GEO,    0, 6 },
	[SYNTH_COMPANY]      = { "company",      SYNTH_OUT_STR,    0, 0 },
	[SYNTH_JOB_TITLE]    = { "job_title",    SYNTH_OUT_STR,    0, 0 },
	[SYNTH_IPV4]         = { "ipv4",         SYNTH_OUT_STR,    0, 0 },
	[SYNTH_IPV6]         = { "ipv6",         SYNTH_OUT_STR,    0, 0 },
	[SYNTH_MAC]          = { "mac",          SYNTH_OUT_STR,    0, 0 },
	[SYNTH_URL]          = { "url",          SYNTH_OUT_STR,    0, 0 },
	[SYNTH_DOMAIN]       = { "domain",       SYNTH_OUT_STR,    0, 0 },
	[SYNTH_UUID]         = { "uuid",         SYNTH_OUT_STR,    0, 0 },
	[SYNTH_WORD]         = { "word",         SYNTH_OUT_STR,    0, 0 },
	[SYNTH_WORDS]        = { "words",        SYNTH_OUT_STR,    0, 1 },
	[SYNTH_SENTENCE]     = { "sentence",     SYNTH_OUT_STR,    0, 0 },
	[SYNTH_LOREM]        = { "lorem",        SYNTH_OUT_STR,    0, 1 },
	[SYNTH_PRODUCT]      = { "product",      SYNTH_OUT_STR,    0, 0 },
	[SYNTH_COLOR]        = { "color",        SYNTH_OUT_STR,    0, 0 },
	[SYNTH_CREDIT_CARD]  = { "credit_card",  SYNTH_OUT_STR,    0, 0 },
	[SYNTH_DATE]         = { "date",         SYNTH_OUT_STR,    0, 3 },
	[SYNTH_TIMESTAMP]    = { "timestamp",    SYNTH_OUT_INT,    0, 2 },
	[SYNTH_NOW]          = { "now",          SYNTH_OUT_INT,    0, 0 },
	[SYNTH_INT]          = { "int",          SYNTH_OUT_INT,    2, 2 },
	[SYNTH_DOUBLE]       = { "double",       SYNTH_OUT_DOUBLE, 2, 2 },
	[SYNTH_PICK]         = { "pick",         SYNTH_OUT_STR,    1, ARGS_VARIADIC },
	[SYNTH_FMT]          = { "fmt",          SYNTH_OUT_STR,    1, 1 },
};

typedef struct synth_arg_s {
	uint8_t type;
	bool has_weight;
	int64_t i;
	double d;
	char* s;
	uint32_t weight;
	const char* loc;
} synth_arg_t;


//==========================================================
// Forward declarations.
//

LOCAL_HELPER int _sg_synth_parse_body(const char* str, const char** endptr,
		synth_spec_t* out, bool in_tpl, const char** err_msg,
		const char** err_loc);
LOCAL_HELPER uint32_t _sg_gen_into(const synth_spec_t* spec, as_random* random,
		char* dst);


//==========================================================
// Inlines and macros.
//

static char g_err_buf[256];

static const char HEX[] = "0123456789abcdef";

static inline uint32_t
mulhi32(uint32_t r, uint32_t n)
{
	return (uint32_t) (((uint64_t) r * n) >> 32);
}

static inline uint64_t
mulhi64(uint64_t r, uint64_t n)
{
	return (uint64_t) (((unsigned __int128) r * n) >> 64);
}

static inline uint32_t
hi32(uint64_t r)
{
	return (uint32_t) (r >> 32);
}

static inline uint32_t
lo32(uint64_t r)
{
	return (uint32_t) r;
}

static inline uint64_t
sat_mul(uint64_t a, uint64_t b)
{
	unsigned __int128 p = (unsigned __int128) a * b;
	return p > UINT64_MAX ? UINT64_MAX : (uint64_t) p;
}

static inline uint32_t
put_word(char* dst, const synth_dict_t* d, uint32_t r32)
{
	uint32_t len;
	const char* w = synth_dict_word(d, synth_dict_index(d, r32), &len);
	memcpy(dst, w, len);
	return len;
}

static inline uint32_t
put_word_slug(char* dst, const synth_dict_t* d, uint32_t r32)
{
	uint32_t len;
	const char* w = synth_dict_word(d, synth_dict_index(d, r32), &len);
	uint32_t n = 0;
	for (uint32_t i = 0; i < len; i++) {
		char c = w[i];
		if (c >= 'A' && c <= 'Z') {
			dst[n++] = (char) (c | 0x20);
		}
		else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
			dst[n++] = c;
		}
	}
	return n;
}

static inline void
put_fixed(char* dst, uint64_t v, uint32_t width)
{
	for (uint32_t i = width; i > 0; i--) {
		dst[i - 1] = (char) ('0' + (v % 10));
		v /= 10;
	}
}

static inline uint32_t
put_u64(char* dst, uint64_t v)
{
	char tmp[INT_TEXT_MAX];
	uint32_t n = 0;
	do {
		tmp[n++] = (char) ('0' + (v % 10));
		v /= 10;
	} while (v != 0);
	for (uint32_t i = 0; i < n; i++) {
		dst[i] = tmp[n - 1 - i];
	}
	return n;
}

static inline uint32_t
put_i64(char* dst, int64_t v)
{
	if (v < 0) {
		dst[0] = '-';
		return 1 + put_u64(dst + 1, (uint64_t) 0 - (uint64_t) v);
	}
	return put_u64(dst, (uint64_t) v);
}

static inline uint32_t
put_micro(char* dst, int32_t micro)
{
	uint32_t n = 0;
	uint32_t mag;
	if (micro < 0) {
		dst[n++] = '-';
		mag = (uint32_t) (-(int64_t) micro);
	}
	else {
		mag = (uint32_t) micro;
	}
	n += put_u64(dst + n, mag / 1000000);
	dst[n++] = '.';
	put_fixed(dst + n, mag % 1000000, 6);
	return n + 6;
}

static inline uint32_t
put_cents(char* dst, double v)
{
	int64_t c = (int64_t) llround(v * 100.0);
	uint32_t n = 0;
	uint64_t mag;
	if (c < 0) {
		dst[n++] = '-';
		mag = (uint64_t) 0 - (uint64_t) c;
	}
	else {
		mag = (uint64_t) c;
	}
	n += put_u64(dst + n, mag / 100);
	dst[n++] = '.';
	put_fixed(dst + n, mag % 100, 2);
	return n + 2;
}

static inline int32_t
rand_range_i32(uint32_t r32, int32_t min, int32_t max)
{
	uint64_t span = (uint64_t) ((int64_t) max - (int64_t) min) + 1;
	return (int32_t) ((int64_t) min + (int64_t) (((uint64_t) r32 * span) >> 32));
}

static inline int64_t
rand_range_i64(uint64_t r, int64_t min, int64_t max)
{
	uint64_t span = (uint64_t) max - (uint64_t) min;
	if (span == UINT64_MAX) {
		return (int64_t) r;
	}
	return (int64_t) ((uint64_t) min + mulhi64(r, span + 1));
}

static inline void
civil_from_days(int64_t z, int64_t* y_out, uint32_t* m_out, uint32_t* d_out)
{
	z += 719468;
	int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	uint32_t doe = (uint32_t) (z - era * 146097);
	uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t y = (int64_t) yoe + era * 400;
	uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	uint32_t mp = (5 * doy + 2) / 153;
	uint32_t d = doy - (153 * mp + 2) / 5 + 1;
	uint32_t m = mp < 10 ? mp + 3 : mp - 9;
	*y_out = y + (m <= 2);
	*m_out = m;
	*d_out = d;
}

static inline int64_t
days_from_civil(int64_t y, uint32_t m, uint32_t d)
{
	y -= m <= 2;
	int64_t era = (y >= 0 ? y : y - 399) / 400;
	uint32_t yoe = (uint32_t) (y - era * 400);
	uint32_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
	uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int64_t) doe - 719468;
}

static inline int64_t
floor_div(int64_t a, int64_t b)
{
	int64_t q = a / b;
	if ((a % b != 0) && ((a < 0) != (b < 0))) {
		q--;
	}
	return q;
}

static inline as_string*
str_block(uint32_t cap, char** buf)
{
	as_string* s = (as_string*) cf_malloc(sizeof(as_string) + cap + 1);
	*buf = (char*) (s + 1);
	return s;
}

static inline as_val*
str_finish(as_string* s, char* buf, uint32_t len)
{
	buf[len] = '\0';
	as_string_init_wlen(s, buf, len, false);
	s->_.free = true;
	return (as_val*) s;
}

static inline as_val*
str_wrap(const char* w, uint32_t len)
{
	return (as_val*) as_string_new_wlen((char*) w, len, false);
}

static inline as_val*
dict_val(const synth_dict_t* d, uint32_t r32)
{
	uint32_t len;
	const char* w = synth_dict_word(d, synth_dict_index(d, r32), &len);
	return str_wrap(w, len);
}

static inline const synth_dict_t*
single_dict(uint8_t kind)
{
	switch (kind) {
		case SYNTH_FIRST_NAME:
			return &SYNTH_DICT_FIRST_NAME;
		case SYNTH_LAST_NAME:
			return &SYNTH_DICT_LAST_NAME;
		case SYNTH_CITY:
			return &SYNTH_DICT_CITY;
		case SYNTH_STATE:
			return &SYNTH_DICT_STATE;
		case SYNTH_STATE_ABBR:
			return &SYNTH_DICT_STATE_ABBR;
		case SYNTH_COUNTRY:
			return &SYNTH_DICT_COUNTRY;
		case SYNTH_COUNTRY_CODE:
			return &SYNTH_DICT_COUNTRY_CODE;
		case SYNTH_WORD:
			return &SYNTH_DICT_WORD;
		case SYNTH_COLOR:
			return &SYNTH_DICT_COLOR;
		case SYNTH_JOB_TITLE:
			return &SYNTH_DICT_JOB_TITLE;
		default:
			return NULL;
	}
}

static inline uint32_t
max_u32(uint32_t a, uint32_t b)
{
	return a > b ? a : b;
}

static inline uint32_t
pick_index(const synth_spec_t* spec, as_random* random)
{
	uint32_t r = hi32(as_random_next_uint64(random));
	if (!spec->pick.weighted) {
		return mulhi32(r, spec->pick.n);
	}
	uint32_t t = mulhi32(r, spec->pick.total);
	uint32_t lo = 0, hi = spec->pick.n - 1;
	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;
		if (spec->pick.cum[mid] > t) {
			hi = mid;
		}
		else {
			lo = mid + 1;
		}
	}
	return lo;
}


//==========================================================
// Public API.
//

const char*
synth_kind_name(uint8_t kind)
{
	return kind < SYNTH_N_KINDS ? synth_kinds[kind].name : "?";
}

int
synth_parse(const char* str, const char** endptr, synth_spec_t* out,
		const char** err_msg, const char** err_loc)
{
	if (*str != '@') {
		*err_msg = "Expected '@' to start a generator";
		*err_loc = str;
		return -1;
	}
	return _sg_synth_parse_body(str + 1, endptr, out, false, err_msg, err_loc);
}

void
synth_spec_free(synth_spec_t* spec)
{
	switch (spec->kind) {
		case SYNTH_DATE:
			cf_free(spec->date.fmt);
			break;
		case SYNTH_PICK:
			for (uint32_t i = 0; i < spec->pick.n; i++) {
				cf_free(spec->pick.items[i]);
			}
			cf_free(spec->pick.items);
			cf_free(spec->pick.lens);
			cf_free(spec->pick.cum);
			break;
		case SYNTH_FMT:
			for (uint32_t i = 0; i < spec->tpl.n_segs; i++) {
				if (spec->tpl.segs[i].ref != NULL) {
					synth_spec_free(spec->tpl.segs[i].ref);
					cf_free(spec->tpl.segs[i].ref);
				}
			}
			cf_free(spec->tpl.segs);
			cf_free(spec->tpl.src);
			break;
		default:
			break;
	}
}

as_val*
synth_gen_val(const synth_spec_t* spec, as_random* random)
{
	switch (spec->out_type) {
		case SYNTH_OUT_INT: {
			int64_t v;
			if (spec->kind == SYNTH_NOW) {
				v = (int64_t) cf_clock_getabsolute();
			}
			else {
				v = rand_range_i64(as_random_next_uint64(random),
						spec->irange.min, spec->irange.max);
			}
			return (as_val*) as_integer_new(v);
		}

		case SYNTH_OUT_DOUBLE: {
			uint64_t r = as_random_next_uint64(random);
			double v;
			if (spec->kind == SYNTH_DOUBLE) {
				v = spec->drange.min + (double) (r >> 11) * 0x1.0p-53 *
					(spec->drange.max - spec->drange.min);
			}
			else if (spec->kind == SYNTH_LAT) {
				v = rand_range_i32(hi32(r), spec->geo.lat_min,
						spec->geo.lat_max) / 1e6;
			}
			else {
				v = rand_range_i32(hi32(r), spec->geo.lon_min,
						spec->geo.lon_max) / 1e6;
			}
			return (as_val*) as_double_new(v);
		}

		case SYNTH_OUT_GEO: {
			as_geojson* g = (as_geojson*) cf_malloc(sizeof(as_geojson) +
					spec->max_len + 1);
			char* buf = (char*) (g + 1);
			uint32_t len = _sg_gen_into(spec, random, buf);
			buf[len] = '\0';
			as_geojson_init_wlen(g, buf, len, false);
			g->_.free = true;
			return (as_val*) g;
		}

		default: {
			const synth_dict_t* d = single_dict(spec->kind);
			if (d != NULL) {
				return dict_val(d, hi32(as_random_next_uint64(random)));
			}

			if (spec->kind == SYNTH_PICK) {
				uint32_t idx = pick_index(spec, random);
				return str_wrap(spec->pick.items[idx], spec->pick.lens[idx]);
			}

			char* buf;
			as_string* s = str_block(spec->max_len, &buf);
			uint32_t len = _sg_gen_into(spec, random, buf);
			return str_finish(s, buf, len);
		}
	}
}

LOCAL_HELPER size_t
_sg_sprint_escaped(const char* s, char** out_str, size_t str_size)
{
	for (; *s != '\0'; s++) {
		switch (*s) {
			case '"':
				sprint(out_str, str_size, "\\\"");
				break;
			case '\\':
				sprint(out_str, str_size, "\\\\");
				break;
			case '\n':
				sprint(out_str, str_size, "\\n");
				break;
			case '\t':
				sprint(out_str, str_size, "\\t");
				break;
			default:
				sprint(out_str, str_size, "%c", *s);
				break;
		}
	}
	return str_size;
}

LOCAL_HELPER size_t
_sg_sprint_double(double v, char** out_str, size_t str_size)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%.15g", v);
	if (strtod(buf, NULL) != v) {
		snprintf(buf, sizeof(buf), "%.17g", v);
	}
	sprint(out_str, str_size, "%s", buf);
	return str_size;
}

LOCAL_HELPER size_t
_sg_sprint_micro(int32_t micro, char** out_str, size_t str_size)
{
	char buf[24];
	uint32_t n = put_micro(buf, micro);
	while (n > 0 && buf[n - 1] == '0') {
		n--;
	}
	if (n > 0 && buf[n - 1] == '.') {
		n--;
	}
	buf[n] = '\0';
	sprint(out_str, str_size, "%s", buf);
	return str_size;
}

size_t
synth_spec_sprint(const synth_spec_t* spec, char** out_str, size_t str_size)
{
	sprint(out_str, str_size, "@%s", synth_kinds[spec->kind].name);

	switch (spec->kind) {
		case SYNTH_LAT:
			if (spec->has_args) {
				sprint(out_str, str_size, "(");
				str_size = _sg_sprint_micro(spec->geo.lat_min, out_str, str_size);
				sprint(out_str, str_size, ",");
				str_size = _sg_sprint_micro(spec->geo.lat_max, out_str, str_size);
				sprint(out_str, str_size, ")");
			}
			break;

		case SYNTH_LON:
			if (spec->has_args) {
				sprint(out_str, str_size, "(");
				str_size = _sg_sprint_micro(spec->geo.lon_min, out_str, str_size);
				sprint(out_str, str_size, ",");
				str_size = _sg_sprint_micro(spec->geo.lon_max, out_str, str_size);
				sprint(out_str, str_size, ")");
			}
			break;

		case SYNTH_GEOJSON:
			if (spec->has_args) {
				sprint(out_str, str_size, "(");
				str_size = _sg_sprint_micro(spec->geo.lat_min, out_str, str_size);
				sprint(out_str, str_size, ",");
				str_size = _sg_sprint_micro(spec->geo.lat_max, out_str, str_size);
				sprint(out_str, str_size, ",");
				str_size = _sg_sprint_micro(spec->geo.lon_min, out_str, str_size);
				sprint(out_str, str_size, ",");
				str_size = _sg_sprint_micro(spec->geo.lon_max, out_str, str_size);
				sprint(out_str, str_size, ")");
			}
			break;

		case SYNTH_GEO_CIRCLE:
			if (spec->has_args) {
				sprint(out_str, str_size, "(%u,%u", spec->geo.r_min,
						spec->geo.r_max);
				if (spec->geo.lat_min != US_LAT_MIN ||
						spec->geo.lat_max != US_LAT_MAX ||
						spec->geo.lon_min != US_LON_MIN ||
						spec->geo.lon_max != US_LON_MAX) {
					sprint(out_str, str_size, ",");
					str_size = _sg_sprint_micro(spec->geo.lat_min, out_str, str_size);
					sprint(out_str, str_size, ",");
					str_size = _sg_sprint_micro(spec->geo.lat_max, out_str, str_size);
					sprint(out_str, str_size, ",");
					str_size = _sg_sprint_micro(spec->geo.lon_min, out_str, str_size);
					sprint(out_str, str_size, ",");
					str_size = _sg_sprint_micro(spec->geo.lon_max, out_str, str_size);
				}
				sprint(out_str, str_size, ")");
			}
			break;

		case SYNTH_WORDS:
		case SYNTH_LOREM:
			if (spec->has_args) {
				sprint(out_str, str_size, "(%u)", spec->n);
			}
			break;

		case SYNTH_DATE:
			if (spec->has_args) {
				sprint(out_str, str_size, "(%" PRId64 ",%" PRId64,
						spec->date.min, spec->date.max);
				if (spec->date.fmt != NULL) {
					sprint(out_str, str_size, ",\"");
					str_size = _sg_sprint_escaped(spec->date.fmt, out_str, str_size);
					sprint(out_str, str_size, "\"");
				}
				sprint(out_str, str_size, ")");
			}
			break;

		case SYNTH_TIMESTAMP:
		case SYNTH_INT:
			if (spec->has_args) {
				sprint(out_str, str_size, "(%" PRId64 ",%" PRId64 ")",
						spec->irange.min, spec->irange.max);
			}
			break;

		case SYNTH_DOUBLE:
			sprint(out_str, str_size, "(");
			str_size = _sg_sprint_double(spec->drange.min, out_str, str_size);
			sprint(out_str, str_size, ",");
			str_size = _sg_sprint_double(spec->drange.max, out_str, str_size);
			sprint(out_str, str_size, ")");
			break;

		case SYNTH_PICK:
			sprint(out_str, str_size, "(");
			for (uint32_t i = 0; i < spec->pick.n; i++) {
				if (i != 0) {
					sprint(out_str, str_size, ",");
				}
				sprint(out_str, str_size, "\"");
				str_size = _sg_sprint_escaped(spec->pick.items[i], out_str, str_size);
				sprint(out_str, str_size, "\"");
				if (spec->pick.weighted) {
					uint32_t w = spec->pick.cum[i] -
						(i == 0 ? 0 : spec->pick.cum[i - 1]);
					sprint(out_str, str_size, ":%u", w);
				}
			}
			sprint(out_str, str_size, ")");
			break;

		case SYNTH_FMT:
			sprint(out_str, str_size, "(\"");
			str_size = _sg_sprint_escaped(spec->tpl.src, out_str, str_size);
			sprint(out_str, str_size, "\")");
			break;

		default:
			break;
	}

	return str_size;
}

uint64_t
synth_spec_cardinality(const synth_spec_t* spec)
{
	const synth_dict_t* d = single_dict(spec->kind);
	if (d != NULL) {
		return d->n;
	}

	switch (spec->kind) {
		case SYNTH_FULL_NAME:
			return sat_mul(SYNTH_DICT_FIRST_NAME.n, SYNTH_DICT_LAST_NAME.n);
		case SYNTH_ZIP:
			return 99950 - 501 + 1;
		case SYNTH_PRODUCT:
			return sat_mul(sat_mul(SYNTH_DICT_PRODUCT_ADJECTIVE.n,
						SYNTH_DICT_PRODUCT_MATERIAL.n), SYNTH_DICT_PRODUCT_NAME.n);
		case SYNTH_DOMAIN:
			return sat_mul((uint64_t) SYNTH_DICT_LAST_NAME.n + SYNTH_DICT_NOUN.n,
					SYNTH_DICT_TLD.n);
		case SYNTH_DATE: {
			if (spec->date.fmt != NULL) {
				return UINT64_MAX;
			}
			int64_t d0 = floor_div(spec->date.min, 86400);
			int64_t d1 = floor_div(spec->date.max, 86400);
			return (uint64_t) (d1 - d0) + 1;
		}
		case SYNTH_TIMESTAMP:
		case SYNTH_INT: {
			uint64_t span = (uint64_t) spec->irange.max - (uint64_t) spec->irange.min;
			return span == UINT64_MAX ? UINT64_MAX : span + 1;
		}
		case SYNTH_NOW:
			return 1;
		case SYNTH_PICK:
			return spec->pick.n;
		case SYNTH_FMT: {
			uint64_t c = 1;
			for (uint32_t i = 0; i < spec->tpl.n_segs; i++) {
				if (spec->tpl.segs[i].ref != NULL) {
					c = sat_mul(c, synth_spec_cardinality(spec->tpl.segs[i].ref));
				}
			}
			return c;
		}
		default:
			return UINT64_MAX;
	}
}


//==========================================================
// Local helpers: generation.
//

LOCAL_HELPER uint32_t
_sg_gen_username(char* dst, uint64_t r)
{
	uint32_t n = 0;
	uint32_t style = (uint32_t) (r & 3);
	uint32_t digits = (uint32_t) ((r >> 2) & 0x3fff);

	if (style == 3) {
		uint32_t first_len = put_word_slug(dst, &SYNTH_DICT_FIRST_NAME, hi32(r));
		n = first_len > 0 ? 1 : 0;
	}
	else {
		n = put_word_slug(dst, &SYNTH_DICT_FIRST_NAME, hi32(r));
		if (style != 2) {
			dst[n++] = style == 0 ? '.' : '_';
		}
	}
	n += put_word_slug(dst + n, &SYNTH_DICT_LAST_NAME,
			(uint32_t) (r >> 16) ^ (uint32_t) (r << 7));

	switch (style) {
		case 1:
		case 3:
			put_fixed(dst + n, digits % 100, 2);
			n += 2;
			break;
		case 2:
			put_fixed(dst + n, digits % 10000, 4);
			n += 4;
			break;
		default:
			break;
	}
	return n;
}

LOCAL_HELPER uint32_t
_sg_gen_domain(char* dst, uint64_t r)
{
	uint32_t n;
	if (r & 1) {
		n = put_word_slug(dst, &SYNTH_DICT_LAST_NAME, hi32(r));
	}
	else {
		n = put_word_slug(dst, &SYNTH_DICT_NOUN, hi32(r));
	}
	if (n == 0) {
		dst[n++] = 'x';
	}
	dst[n++] = '.';
	n += put_word(dst + n, &SYNTH_DICT_TLD, (uint32_t) (r << 8));
	return n;
}

LOCAL_HELPER uint32_t
_sg_gen_date(const synth_spec_t* spec, uint64_t r, char* dst)
{
	int64_t t = rand_range_i64(r, spec->date.min, spec->date.max);

	if (spec->date.fmt == NULL) {
		int64_t y;
		uint32_t m, d;
		civil_from_days(floor_div(t, 86400), &y, &m, &d);
		put_fixed(dst, (uint64_t) (y < 0 ? 0 : y), 4);
		dst[4] = '-';
		put_fixed(dst + 5, m, 2);
		dst[7] = '-';
		put_fixed(dst + 8, d, 2);
		return 10;
	}

	time_t tt = (time_t) t;
	struct tm tm;
	gmtime_r(&tt, &tm);
	char buf[DATE_BUF_LEN];
	size_t len = strftime(buf, sizeof(buf), spec->date.fmt, &tm);
	memcpy(dst, buf, len);
	return (uint32_t) len;
}

LOCAL_HELPER uint32_t
_sg_gen_credit_card(char* dst, as_random* random)
{
	uint64_t r = as_random_next_uint64(random);
	uint64_t digits = as_random_next_uint64(random);
	uint32_t len;
	uint32_t n;

	switch (r & 3) {
		case 0:
			dst[0] = '4';
			n = 1;
			len = 16;
			break;
		case 1:
			dst[0] = '5';
			dst[1] = (char) ('1' + mulhi32(hi32(r), 5));
			n = 2;
			len = 16;
			break;
		case 2:
			dst[0] = '3';
			dst[1] = (r & 4) ? '4' : '7';
			n = 2;
			len = 15;
			break;
		default:
			memcpy(dst, "6011", 4);
			n = 4;
			len = 16;
			break;
	}

	for (; n < len - 1; n++) {
		unsigned __int128 p = (unsigned __int128) digits * 10;
		dst[n] = (char) ('0' + (uint32_t) (p >> 64));
		digits = (uint64_t) p;
	}

	uint32_t sum = 0;
	for (uint32_t i = 0; i < len - 1; i++) {
		uint32_t dgt = (uint32_t) (dst[len - 2 - i] - '0');
		if ((i & 1) == 0) {
			dgt *= 2;
			if (dgt > 9) {
				dgt -= 9;
			}
		}
		sum += dgt;
	}
	dst[len - 1] = (char) ('0' + (10 - (sum % 10)) % 10);
	return len;
}

LOCAL_HELPER uint32_t
_sg_gen_into(const synth_spec_t* spec, as_random* random, char* dst)
{
	const synth_dict_t* d = single_dict(spec->kind);
	if (d != NULL) {
		return put_word(dst, d, hi32(as_random_next_uint64(random)));
	}

	switch (spec->kind) {
		case SYNTH_FULL_NAME: {
			uint64_t r = as_random_next_uint64(random);
			uint32_t n = put_word(dst, &SYNTH_DICT_FIRST_NAME, hi32(r));
			dst[n++] = ' ';
			return n + put_word(dst + n, &SYNTH_DICT_LAST_NAME, lo32(r));
		}

		case SYNTH_USERNAME:
			return _sg_gen_username(dst, as_random_next_uint64(random));

		case SYNTH_EMAIL: {
			uint32_t n = _sg_gen_username(dst, as_random_next_uint64(random));
			dst[n++] = '@';
			return n + put_word(dst + n, &SYNTH_DICT_EMAIL_PROVIDER,
					hi32(as_random_next_uint64(random)));
		}

		case SYNTH_PHONE: {
			uint64_t r = as_random_next_uint64(random);
			put_fixed(dst, 200 + mulhi32(hi32(r), 800), 3);
			dst[3] = '-';
			put_fixed(dst + 4, 200 + mulhi32(lo32(r), 800), 3);
			dst[7] = '-';
			put_fixed(dst + 8, (r >> 20) % 10000, 4);
			return 12;
		}

		case SYNTH_STREET: {
			uint64_t r = as_random_next_uint64(random);
			uint32_t n = put_u64(dst, 1 + mulhi32(lo32(r) & 0xffff0000, 9999));
			dst[n++] = ' ';
			n += put_word(dst + n, &SYNTH_DICT_STREET_NAME, hi32(r));
			dst[n++] = ' ';
			return n + put_word(dst + n, &SYNTH_DICT_STREET_SUFFIX,
					(uint32_t) (r << 16));
		}

		case SYNTH_ZIP: {
			uint64_t r = as_random_next_uint64(random);
			put_fixed(dst, 501 + mulhi32(hi32(r), 99950 - 501 + 1), 5);
			return 5;
		}

		case SYNTH_COMPANY: {
			uint64_t r = as_random_next_uint64(random);
			uint32_t n;
			switch (r & 3) {
				case 0:
					n = put_word(dst, &SYNTH_DICT_LAST_NAME, hi32(r));
					dst[n++] = ' ';
					return n + put_word(dst + n, &SYNTH_DICT_COMPANY_SUFFIX,
							lo32(r) & ~3U);
				case 1:
					n = put_word(dst, &SYNTH_DICT_LAST_NAME, hi32(r));
					dst[n++] = '-';
					return n + put_word(dst + n, &SYNTH_DICT_LAST_NAME,
							lo32(r) & ~3U);
				case 2: {
					uint64_t r2 = as_random_next_uint64(random);
					n = put_word(dst, &SYNTH_DICT_LAST_NAME, hi32(r));
					dst[n++] = ',';
					dst[n++] = ' ';
					n += put_word(dst + n, &SYNTH_DICT_LAST_NAME, lo32(r) & ~3U);
					memcpy(dst + n, " and ", 5);
					n += 5;
					return n + put_word(dst + n, &SYNTH_DICT_LAST_NAME, hi32(r2));
				}
				default:
					return put_word(dst, &SYNTH_DICT_COMPANY_NAME, hi32(r));
			}
		}

		case SYNTH_PRODUCT: {
			uint64_t r = as_random_next_uint64(random);
			uint32_t n = put_word(dst, &SYNTH_DICT_PRODUCT_ADJECTIVE, hi32(r));
			dst[n++] = ' ';
			n += put_word(dst + n, &SYNTH_DICT_PRODUCT_MATERIAL, lo32(r));
			dst[n++] = ' ';
			return n + put_word(dst + n, &SYNTH_DICT_PRODUCT_NAME,
					(uint32_t) (r >> 16) ^ (uint32_t) (r << 11));
		}

		case SYNTH_IPV4: {
			uint64_t r = as_random_next_uint64(random);
			uint32_t n = put_u64(dst, 1 + mulhi32(hi32(r), 223));
			for (uint32_t i = 0; i < 3; i++) {
				dst[n++] = '.';
				n += put_u64(dst + n, (r >> (8 * i)) & 0xff);
			}
			return n;
		}

		case SYNTH_IPV6: {
			uint64_t a = as_random_next_uint64(random);
			uint64_t b = as_random_next_uint64(random);
			uint32_t n = 0;
			for (uint32_t g = 0; g < 8; g++) {
				uint64_t src = g < 4 ? a : b;
				uint32_t v = (uint32_t) ((src >> (16 * (g & 3))) & 0xffff);
				if (g != 0) {
					dst[n++] = ':';
				}
				dst[n++] = HEX[(v >> 12) & 0xf];
				dst[n++] = HEX[(v >> 8) & 0xf];
				dst[n++] = HEX[(v >> 4) & 0xf];
				dst[n++] = HEX[v & 0xf];
			}
			return n;
		}

		case SYNTH_MAC: {
			uint64_t r = as_random_next_uint64(random);
			uint32_t n = 0;
			for (uint32_t i = 0; i < 6; i++) {
				uint32_t byte = (uint32_t) ((r >> (8 * i)) & 0xff);
				if (i == 0) {
					byte &= 0xfe;
				}
				else {
					dst[n++] = ':';
				}
				dst[n++] = HEX[byte >> 4];
				dst[n++] = HEX[byte & 0xf];
			}
			return n;
		}

		case SYNTH_UUID: {
			uint8_t bytes[16];
			uint64_t a = as_random_next_uint64(random);
			uint64_t b = as_random_next_uint64(random);
			memcpy(bytes, &a, 8);
			memcpy(bytes + 8, &b, 8);
			bytes[6] = (uint8_t) ((bytes[6] & 0x0f) | 0x40);
			bytes[8] = (uint8_t) ((bytes[8] & 0x3f) | 0x80);
			uint32_t n = 0;
			for (uint32_t i = 0; i < 16; i++) {
				if (i == 4 || i == 6 || i == 8 || i == 10) {
					dst[n++] = '-';
				}
				dst[n++] = HEX[bytes[i] >> 4];
				dst[n++] = HEX[bytes[i] & 0xf];
			}
			return n;
		}

		case SYNTH_DOMAIN:
			return _sg_gen_domain(dst, as_random_next_uint64(random));

		case SYNTH_URL: {
			uint64_t r = as_random_next_uint64(random);
			memcpy(dst, "https://www.", 12);
			uint32_t n = 12 + _sg_gen_domain(dst + 12, r);
			dst[n++] = '/';
			return n + put_word(dst + n, &SYNTH_DICT_NOUN,
					hi32(as_random_next_uint64(random)));
		}

		case SYNTH_WORDS: {
			uint32_t n = 0;
			uint64_t r = 0;
			for (uint32_t i = 0; i < spec->n; i++) {
				if ((i & 1) == 0) {
					r = as_random_next_uint64(random);
				}
				if (i != 0) {
					dst[n++] = ' ';
				}
				n += put_word(dst + n, &SYNTH_DICT_WORD,
						(i & 1) == 0 ? hi32(r) : lo32(r));
			}
			return n;
		}

		case SYNTH_SENTENCE: {
			uint64_t r = as_random_next_uint64(random);
			uint32_t count = 5 + mulhi32(hi32(r), 8);
			uint32_t n = 0;
			for (uint32_t i = 0; i < count; i++) {
				if ((i & 1) == 0) {
					r = as_random_next_uint64(random);
				}
				if (i != 0) {
					dst[n++] = ' ';
				}
				n += put_word(dst + n, &SYNTH_DICT_WORD,
						(i & 1) == 0 ? hi32(r) : lo32(r));
			}
			if (dst[0] >= 'a' && dst[0] <= 'z') {
				dst[0] = (char) (dst[0] - ('a' - 'A'));
			}
			dst[n++] = '.';
			return n;
		}

		case SYNTH_LOREM: {
			uint32_t n = 0;
			uint64_t r = 0;
			uint32_t i = 0;
			while (n < spec->n) {
				if ((i & 1) == 0) {
					r = as_random_next_uint64(random);
				}
				if (n != 0) {
					dst[n++] = ' ';
				}
				n += put_word(dst + n, &SYNTH_DICT_LOREM,
						(i & 1) == 0 ? hi32(r) : lo32(r));
				i++;
			}
			if (dst[spec->n - 1] == ' ') {
				dst[spec->n - 1] = '.';
			}
			return spec->n;
		}

		case SYNTH_CREDIT_CARD:
			return _sg_gen_credit_card(dst, random);

		case SYNTH_DATE:
			return _sg_gen_date(spec, as_random_next_uint64(random), dst);

		case SYNTH_PICK: {
			uint32_t idx = pick_index(spec, random);
			memcpy(dst, spec->pick.items[idx], spec->pick.lens[idx]);
			return spec->pick.lens[idx];
		}

		case SYNTH_GEOJSON: {
			uint64_t r = as_random_next_uint64(random);
			int32_t lat = rand_range_i32(hi32(r), spec->geo.lat_min,
					spec->geo.lat_max);
			int32_t lon = rand_range_i32(lo32(r), spec->geo.lon_min,
					spec->geo.lon_max);
			uint32_t n = 0;
			memcpy(dst, "{\"type\":\"Point\",\"coordinates\":[", 31);
			n = 31;
			n += put_micro(dst + n, lon);
			dst[n++] = ',';
			n += put_micro(dst + n, lat);
			dst[n++] = ']';
			dst[n++] = '}';
			return n;
		}

		case SYNTH_GEO_CIRCLE: {
			uint64_t r = as_random_next_uint64(random);
			uint64_t r2 = as_random_next_uint64(random);
			int32_t lat = rand_range_i32(hi32(r), spec->geo.lat_min,
					spec->geo.lat_max);
			int32_t lon = rand_range_i32(lo32(r), spec->geo.lon_min,
					spec->geo.lon_max);
			uint32_t radius = spec->geo.r_min +
				mulhi32(hi32(r2), spec->geo.r_max - spec->geo.r_min + 1);
			uint32_t n = 0;
			memcpy(dst, "{\"type\":\"AeroCircle\",\"coordinates\":[[", 37);
			n = 37;
			n += put_micro(dst + n, lon);
			dst[n++] = ',';
			n += put_micro(dst + n, lat);
			dst[n++] = ']';
			dst[n++] = ',';
			n += put_u64(dst + n, radius);
			dst[n++] = ']';
			dst[n++] = '}';
			return n;
		}

		case SYNTH_FMT: {
			uint32_t n = 0;
			for (uint32_t i = 0; i < spec->tpl.n_segs; i++) {
				const struct synth_tpl_seg_s* seg = &spec->tpl.segs[i];
				if (seg->ref == NULL) {
					memcpy(dst + n, seg->lit, seg->lit_len);
					n += seg->lit_len;
				}
				else if (seg->ref->out_type == SYNTH_OUT_STR) {
					n += _sg_gen_into(seg->ref, random, dst + n);
				}
				else {
					as_val* v = synth_gen_val(seg->ref, random);
					if (seg->ref->out_type == SYNTH_OUT_INT) {
						n += put_i64(dst + n, as_integer_get(as_integer_fromval(v)));
					}
					else {
						n += put_cents(dst + n, as_double_get(as_double_fromval(v)));
					}
					as_val_destroy(v);
				}
			}
			return n;
		}

		default:
			return 0;
	}
}


//==========================================================
// Local helpers: parsing.
//

LOCAL_HELPER void
_sg_free_args(as_vector* args)
{
	for (uint32_t i = 0; i < args->size; i++) {
		synth_arg_t* a = (synth_arg_t*) as_vector_get(args, i);
		cf_free(a->s);
	}
	as_vector_destroy(args);
}

LOCAL_HELPER const char*
_sg_skip_spaces(const char* s)
{
	while (*s == ' ') {
		s++;
	}
	return s;
}

LOCAL_HELPER int
_sg_parse_args(const char* str, const char** endptr, as_vector* args,
		const char** err_msg, const char** err_loc)
{
	const char* s = _sg_skip_spaces(str + 1);

	if (*s == ')') {
		*endptr = s + 1;
		return 0;
	}

	for (;;) {
		synth_arg_t a;
		memset(&a, 0, sizeof(a));
		s = _sg_skip_spaces(s);
		a.loc = s;

		if (*s == '"') {
			const char* end;
			char* lit = parse_string_literal(s, &end);
			if (lit == NULL) {
				*err_msg = "Invalid string literal in generator arguments";
				*err_loc = s;
				return -1;
			}
			a.type = ARG_STR;
			a.s = lit;
			s = _sg_skip_spaces(end);

			if (*s == ':') {
				s = _sg_skip_spaces(s + 1);
				char* wend;
				errno = 0;
				long long w = strtoll(s, &wend, 10);
				if (wend == s || errno != 0 || w <= 0 || w > UINT32_MAX) {
					cf_free(lit);
					*err_msg = "Pick weights must be positive integers";
					*err_loc = s;
					return -1;
				}
				a.has_weight = true;
				a.weight = (uint32_t) w;
				s = wend;
			}
		}
		else {
			char* iend;
			errno = 0;
			long long iv = strtoll(s, &iend, 10);
			if (iend != s && *iend != '.' && *iend != 'e' && *iend != 'E') {
				if (errno != 0) {
					*err_msg = "Integer argument out of range";
					*err_loc = s;
					return -1;
				}
				a.type = ARG_INT;
				a.i = iv;
				a.d = (double) iv;
				s = iend;
			}
			else {
				char* dend;
				errno = 0;
				double dv = strtod(s, &dend);
				if (dend == s || errno != 0 || !isfinite(dv)) {
					*err_msg = "Expected a number or a string literal as a "
						"generator argument";
					*err_loc = s;
					return -1;
				}
				a.type = ARG_DOUBLE;
				a.d = dv;
				s = dend;
				if (*s == 'f') {
					s++;
				}
			}
		}

		as_vector_append(args, &a);
		s = _sg_skip_spaces(s);

		if (*s == ',') {
			s++;
			continue;
		}
		if (*s == ')') {
			*endptr = s + 1;
			return 0;
		}
		*err_msg = "Expected ',' or ')' in generator arguments";
		*err_loc = s;
		return -1;
	}
}

LOCAL_HELPER bool
_sg_parse_date_str(const char* s, int64_t* days)
{
	if (strlen(s) != 10 || s[4] != '-' || s[7] != '-') {
		return false;
	}
	for (uint32_t i = 0; i < 10; i++) {
		if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9')) {
			return false;
		}
	}
	int64_t y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 +
		(s[3] - '0');
	uint32_t m = (uint32_t) ((s[5] - '0') * 10 + (s[6] - '0'));
	uint32_t d = (uint32_t) ((s[8] - '0') * 10 + (s[9] - '0'));
	if (m < 1 || m > 12 || d < 1 || d > 31) {
		return false;
	}
	int64_t z = days_from_civil(y, m, d);
	int64_t y2;
	uint32_t m2, d2;
	civil_from_days(z, &y2, &m2, &d2);
	if (y2 != y || m2 != m || d2 != d) {
		return false;
	}
	*days = z;
	return true;
}

LOCAL_HELPER int
_sg_arg_epoch(const synth_arg_t* a, bool is_max, int64_t* out,
		const char** err_msg, const char** err_loc)
{
	if (a->type == ARG_INT) {
		*out = a->i;
		return 0;
	}
	if (a->type == ARG_STR) {
		int64_t days;
		if (_sg_parse_date_str(a->s, &days)) {
			*out = days * 86400 + (is_max ? 86399 : 0);
			return 0;
		}
	}
	*err_msg = "Expected epoch seconds or a \"YYYY-MM-DD\" date";
	*err_loc = a->loc;
	return -1;
}

LOCAL_HELPER int
_sg_arg_micro(const synth_arg_t* a, int32_t limit, int32_t* out,
		const char** err_msg, const char** err_loc)
{
	if (a->type == ARG_STR) {
		*err_msg = "Expected a coordinate in degrees";
		*err_loc = a->loc;
		return -1;
	}
	double v = a->d * 1e6;
	if (v < -(double) limit || v > (double) limit) {
		snprintf(g_err_buf, sizeof(g_err_buf),
				"Coordinate out of range (must be within +/-%d degrees)",
				limit / 1000000);
		*err_msg = g_err_buf;
		*err_loc = a->loc;
		return -1;
	}
	*out = (int32_t) llround(v);
	return 0;
}

LOCAL_HELPER int
_sg_arg_uint(const synth_arg_t* a, uint64_t min, uint64_t max, uint64_t* out,
		const char* what, const char** err_msg, const char** err_loc)
{
	if (a->type != ARG_INT || a->i < 0 || (uint64_t) a->i < min ||
			(uint64_t) a->i > max) {
		snprintf(g_err_buf, sizeof(g_err_buf),
				"%s must be an integer between %" PRIu64 " and %" PRIu64,
				what, min, max);
		*err_msg = g_err_buf;
		*err_loc = a->loc;
		return -1;
	}
	*out = (uint64_t) a->i;
	return 0;
}

LOCAL_HELPER int
_sg_parse_bbox(synth_arg_t* args, synth_spec_t* out, const char** err_msg,
		const char** err_loc)
{
	if (_sg_arg_micro(&args[0], 90000000, &out->geo.lat_min, err_msg, err_loc) ||
			_sg_arg_micro(&args[1], 90000000, &out->geo.lat_max, err_msg, err_loc) ||
			_sg_arg_micro(&args[2], 180000000, &out->geo.lon_min, err_msg, err_loc) ||
			_sg_arg_micro(&args[3], 180000000, &out->geo.lon_max, err_msg, err_loc)) {
		return -1;
	}
	if (out->geo.lat_min > out->geo.lat_max ||
			out->geo.lon_min > out->geo.lon_max) {
		*err_msg = "Bounding box minimums must be <= maximums "
			"(lat_min,lat_max,lon_min,lon_max)";
		*err_loc = args[0].loc;
		return -1;
	}
	return 0;
}

LOCAL_HELPER int
_sg_parse_template(synth_spec_t* out, char* src, const char* tok_loc,
		const char** err_msg, const char** err_loc)
{
	as_vector segs;
	as_vector_inita(&segs, sizeof(struct synth_tpl_seg_s), 8);
	uint32_t max_len = 0;
	const char* p = src;
	const char* lit = src;

	while (*p != '\0') {
		if (p[0] != '#' || p[1] != '{') {
			p++;
			continue;
		}

		if (p > lit) {
			struct synth_tpl_seg_s seg = { (char*) lit, (uint32_t) (p - lit), NULL };
			as_vector_append(&segs, &seg);
			max_len += seg.lit_len;
		}

		synth_spec_t* ref = (synth_spec_t*) cf_malloc(sizeof(synth_spec_t));
		const char* end;
		const char* sub_msg;
		const char* sub_loc;
		if (_sg_synth_parse_body(p + 2, &end, ref, true, &sub_msg, &sub_loc) != 0) {
			cf_free(ref);
			char sub_buf[sizeof(g_err_buf)];
			snprintf(sub_buf, sizeof(sub_buf), "%s", sub_msg);
			snprintf(g_err_buf, sizeof(g_err_buf), "@fmt placeholder: %s",
					sub_buf);
			*err_msg = g_err_buf;
			*err_loc = tok_loc;
			goto fail;
		}
		if (*end != '}') {
			synth_spec_free(ref);
			cf_free(ref);
			*err_msg = "@fmt: unterminated #{ placeholder";
			*err_loc = tok_loc;
			goto fail;
		}
		if (ref->out_type == SYNTH_OUT_GEO || ref->kind == SYNTH_FMT) {
			synth_spec_free(ref);
			cf_free(ref);
			*err_msg = "@fmt placeholders cannot be geo or nested @fmt generators";
			*err_loc = tok_loc;
			goto fail;
		}
		if (ref->kind == SYNTH_DOUBLE &&
				(fabs(ref->drange.min) >= FMT_DOUBLE_ABS_MAX ||
				 fabs(ref->drange.max) >= FMT_DOUBLE_ABS_MAX)) {
			synth_spec_free(ref);
			cf_free(ref);
			*err_msg = "@fmt: #{double} bounds must be within +/-9e16";
			*err_loc = tok_loc;
			goto fail;
		}

		struct synth_tpl_seg_s seg = { NULL, 0, ref };
		as_vector_append(&segs, &seg);
		switch (ref->out_type) {
			case SYNTH_OUT_INT:
				max_len += INT_TEXT_MAX + 1;
				break;
			case SYNTH_OUT_DOUBLE:
				max_len += DOUBLE_TEXT_MAX;
				break;
			default:
				max_len += ref->max_len;
				break;
		}

		p = end + 1;
		lit = p;
	}

	if (p > lit) {
		struct synth_tpl_seg_s seg = { (char*) lit, (uint32_t) (p - lit), NULL };
		as_vector_append(&segs, &seg);
		max_len += seg.lit_len;
	}

	out->tpl.src = src;
	out->tpl.segs = (struct synth_tpl_seg_s*) as_vector_to_array(&segs,
			&out->tpl.n_segs);
	as_vector_destroy(&segs);
	out->max_len = max_len;
	return 0;

fail:
	for (uint32_t i = 0; i < segs.size; i++) {
		struct synth_tpl_seg_s* seg =
			(struct synth_tpl_seg_s*) as_vector_get(&segs, i);
		if (seg->ref != NULL) {
			synth_spec_free(seg->ref);
			cf_free(seg->ref);
		}
	}
	as_vector_destroy(&segs);
	return -1;
}

LOCAL_HELPER int
_sg_init_kind(synth_spec_t* out, as_vector* argv, const char* tok_loc,
		const char** err_msg, const char** err_loc)
{
	synth_arg_t* args = (synth_arg_t*) argv->list;
	uint32_t n_args = argv->size;
	const char* name = synth_kinds[out->kind].name;
	const synth_dict_t* d = single_dict(out->kind);

	if (d != NULL) {
		out->max_len = d->max_len;
		return 0;
	}

	switch (out->kind) {
		case SYNTH_FULL_NAME:
			out->max_len = SYNTH_DICT_FIRST_NAME.max_len + 1 +
				SYNTH_DICT_LAST_NAME.max_len;
			return 0;

		case SYNTH_USERNAME:
			out->max_len = SYNTH_DICT_FIRST_NAME.max_len + 1 +
				SYNTH_DICT_LAST_NAME.max_len + 4;
			return 0;

		case SYNTH_EMAIL:
			out->max_len = SYNTH_DICT_FIRST_NAME.max_len + 1 +
				SYNTH_DICT_LAST_NAME.max_len + 4 + 1 +
				SYNTH_DICT_EMAIL_PROVIDER.max_len;
			return 0;

		case SYNTH_PHONE:
			out->max_len = 12;
			return 0;

		case SYNTH_STREET:
			out->max_len = 4 + 1 + SYNTH_DICT_STREET_NAME.max_len + 1 +
				SYNTH_DICT_STREET_SUFFIX.max_len;
			return 0;

		case SYNTH_ZIP:
			out->max_len = 5;
			return 0;

		case SYNTH_COMPANY: {
			uint32_t last = SYNTH_DICT_LAST_NAME.max_len;
			out->max_len = max_u32(max_u32(last + 1 + SYNTH_DICT_COMPANY_SUFFIX.max_len,
						3 * last + 7), SYNTH_DICT_COMPANY_NAME.max_len);
			return 0;
		}

		case SYNTH_PRODUCT:
			out->max_len = SYNTH_DICT_PRODUCT_ADJECTIVE.max_len + 1 +
				SYNTH_DICT_PRODUCT_MATERIAL.max_len + 1 +
				SYNTH_DICT_PRODUCT_NAME.max_len;
			return 0;

		case SYNTH_IPV4:
			out->max_len = 15;
			return 0;

		case SYNTH_IPV6:
			out->max_len = 39;
			return 0;

		case SYNTH_MAC:
			out->max_len = 17;
			return 0;

		case SYNTH_UUID:
			out->max_len = 36;
			return 0;

		case SYNTH_DOMAIN:
			out->max_len = max_u32(SYNTH_DICT_LAST_NAME.max_len,
					SYNTH_DICT_NOUN.max_len) + 2 + SYNTH_DICT_TLD.max_len;
			return 0;

		case SYNTH_URL:
			out->max_len = 12 + max_u32(SYNTH_DICT_LAST_NAME.max_len,
					SYNTH_DICT_NOUN.max_len) + 2 + SYNTH_DICT_TLD.max_len + 1 +
				SYNTH_DICT_NOUN.max_len;
			return 0;

		case SYNTH_CREDIT_CARD:
			out->max_len = 16;
			return 0;

		case SYNTH_SENTENCE:
			out->max_len = 12 * (SYNTH_DICT_WORD.max_len + 1) + 1;
			return 0;

		case SYNTH_WORDS: {
			uint64_t n = DEFAULT_WORDS;
			if (n_args == 1 && _sg_arg_uint(&args[0], 1, MAX_WORDS, &n,
						"@words count", err_msg, err_loc) != 0) {
				return -1;
			}
			out->n = (uint32_t) n;
			out->max_len = out->n * (SYNTH_DICT_WORD.max_len + 1);
			return 0;
		}

		case SYNTH_LOREM: {
			uint64_t n = DEFAULT_LOREM;
			if (n_args == 1 && _sg_arg_uint(&args[0], 1, MAX_LOREM, &n,
						"@lorem length", err_msg, err_loc) != 0) {
				return -1;
			}
			out->n = (uint32_t) n;
			out->max_len = out->n + SYNTH_DICT_LOREM.max_len + 1;
			return 0;
		}

		case SYNTH_LAT:
		case SYNTH_LON: {
			bool is_lat = out->kind == SYNTH_LAT;
			int32_t limit = is_lat ? 90000000 : 180000000;
			int32_t* min = is_lat ? &out->geo.lat_min : &out->geo.lon_min;
			int32_t* max = is_lat ? &out->geo.lat_max : &out->geo.lon_max;
			out->geo.lat_min = US_LAT_MIN;
			out->geo.lat_max = US_LAT_MAX;
			out->geo.lon_min = US_LON_MIN;
			out->geo.lon_max = US_LON_MAX;
			if (n_args == 1) {
				snprintf(g_err_buf, sizeof(g_err_buf),
						"@%s expects 0 or 2 arguments (min,max)", name);
				*err_msg = g_err_buf;
				*err_loc = tok_loc;
				return -1;
			}
			if (n_args == 2) {
				if (_sg_arg_micro(&args[0], limit, min, err_msg, err_loc) ||
						_sg_arg_micro(&args[1], limit, max, err_msg, err_loc)) {
					return -1;
				}
				if (*min > *max) {
					snprintf(g_err_buf, sizeof(g_err_buf),
							"@%s: min must be <= max", name);
					*err_msg = g_err_buf;
					*err_loc = args[0].loc;
					return -1;
				}
			}
			return 0;
		}

		case SYNTH_GEOJSON:
			out->geo.lat_min = US_LAT_MIN;
			out->geo.lat_max = US_LAT_MAX;
			out->geo.lon_min = US_LON_MIN;
			out->geo.lon_max = US_LON_MAX;
			out->max_len = GEOJSON_POINT_MAX;
			if (n_args != 0 && n_args != 4) {
				*err_msg = "@geojson expects 0 or 4 arguments "
					"(lat_min,lat_max,lon_min,lon_max)";
				*err_loc = tok_loc;
				return -1;
			}
			return n_args == 4 ? _sg_parse_bbox(args, out, err_msg, err_loc) : 0;

		case SYNTH_GEO_CIRCLE: {
			out->geo.lat_min = US_LAT_MIN;
			out->geo.lat_max = US_LAT_MAX;
			out->geo.lon_min = US_LON_MIN;
			out->geo.lon_max = US_LON_MAX;
			out->geo.r_min = DEFAULT_R_MIN;
			out->geo.r_max = DEFAULT_R_MAX;
			out->max_len = GEOJSON_CIRCLE_MAX;
			if (n_args != 0 && n_args != 2 && n_args != 6) {
				*err_msg = "@geo_circle expects 0, 2 or 6 arguments "
					"(r_min_m,r_max_m[,lat_min,lat_max,lon_min,lon_max])";
				*err_loc = tok_loc;
				return -1;
			}
			if (n_args >= 2) {
				uint64_t r_min, r_max;
				if (_sg_arg_uint(&args[0], 1, MAX_RADIUS, &r_min, "Radius",
							err_msg, err_loc) ||
						_sg_arg_uint(&args[1], 1, MAX_RADIUS, &r_max, "Radius",
							err_msg, err_loc)) {
					return -1;
				}
				if (r_min > r_max) {
					*err_msg = "@geo_circle: r_min must be <= r_max";
					*err_loc = args[0].loc;
					return -1;
				}
				out->geo.r_min = (uint32_t) r_min;
				out->geo.r_max = (uint32_t) r_max;
			}
			return n_args == 6 ? _sg_parse_bbox(args + 2, out, err_msg, err_loc) : 0;
		}

		case SYNTH_DATE: {
			out->date.min = DEFAULT_TS_MIN;
			out->date.max = DEFAULT_TS_MAX;
			out->date.fmt = NULL;
			out->max_len = 10;
			if (n_args == 1) {
				*err_msg = "@date expects 0, 2 or 3 arguments (min,max[,\"format\"])";
				*err_loc = tok_loc;
				return -1;
			}
			if (n_args >= 2) {
				if (_sg_arg_epoch(&args[0], false, &out->date.min, err_msg, err_loc) ||
						_sg_arg_epoch(&args[1], true, &out->date.max, err_msg, err_loc)) {
					return -1;
				}
				if (out->date.min > out->date.max) {
					*err_msg = "@date: min must be <= max";
					*err_loc = args[0].loc;
					return -1;
				}
				// 0000-01-01T00:00:00Z .. 9999-12-31T23:59:59Z
				if (out->date.min < -62167219200LL ||
						out->date.max > 253402300799LL) {
					*err_msg = "@date: range must be within years 0000-9999";
					*err_loc = args[0].loc;
					return -1;
				}
			}
			if (n_args == 3) {
				if (args[2].type != ARG_STR || args[2].s[0] == '\0') {
					*err_msg = "@date format must be a non-empty strftime string";
					*err_loc = args[2].loc;
					return -1;
				}
				char buf[DATE_BUF_LEN];
				struct tm tm;
				time_t t = 1758758399;
				gmtime_r(&t, &tm);
				if (strftime(buf, sizeof(buf), args[2].s, &tm) == 0) {
					*err_msg = "@date format produces empty or too long "
						"(> 63 chars) output";
					*err_loc = args[2].loc;
					return -1;
				}
				if (strcmp(args[2].s, "%Y-%m-%d") != 0) {
					out->date.fmt = args[2].s;
					args[2].s = NULL;
					out->max_len = DATE_BUF_LEN - 1;
				}
			}
			return 0;
		}

		case SYNTH_TIMESTAMP:
			out->irange.min = DEFAULT_TS_MIN;
			out->irange.max = DEFAULT_TS_MAX;
			if (n_args == 1) {
				*err_msg = "@timestamp expects 0 or 2 arguments (min,max)";
				*err_loc = tok_loc;
				return -1;
			}
			if (n_args == 2) {
				if (_sg_arg_epoch(&args[0], false, &out->irange.min, err_msg, err_loc) ||
						_sg_arg_epoch(&args[1], true, &out->irange.max, err_msg, err_loc)) {
					return -1;
				}
				if (out->irange.min > out->irange.max) {
					*err_msg = "@timestamp: min must be <= max";
					*err_loc = args[0].loc;
					return -1;
				}
			}
			return 0;

		case SYNTH_NOW:
			return 0;

		case SYNTH_INT:
			if (args[0].type != ARG_INT || args[1].type != ARG_INT) {
				*err_msg = "@int expects 2 integer arguments (min,max)";
				*err_loc = tok_loc;
				return -1;
			}
			if (args[0].i > args[1].i) {
				*err_msg = "@int: min must be <= max";
				*err_loc = args[0].loc;
				return -1;
			}
			out->irange.min = args[0].i;
			out->irange.max = args[1].i;
			return 0;

		case SYNTH_DOUBLE:
			if (args[0].type == ARG_STR || args[1].type == ARG_STR) {
				*err_msg = "@double expects 2 numeric arguments (min,max)";
				*err_loc = tok_loc;
				return -1;
			}
			if (args[0].d > args[1].d) {
				*err_msg = "@double: min must be <= max";
				*err_loc = args[0].loc;
				return -1;
			}
			out->drange.min = args[0].d;
			out->drange.max = args[1].d;
			return 0;

		case SYNTH_PICK: {
			bool weighted = args[0].has_weight;
			uint64_t total = 0;
			for (uint32_t i = 0; i < n_args; i++) {
				if (args[i].type != ARG_STR) {
					*err_msg = "@pick arguments must be string literals";
					*err_loc = args[i].loc;
					return -1;
				}
				if (args[i].has_weight != weighted) {
					*err_msg = "@pick: mix of weighted and unweighted items";
					*err_loc = args[i].loc;
					return -1;
				}
				total += weighted ? args[i].weight : 1;
				if (total > UINT32_MAX) {
					*err_msg = "@pick: total weight exceeds 2^32-1";
					*err_loc = args[i].loc;
					return -1;
				}
			}
			out->pick.n = n_args;
			out->pick.weighted = weighted;
			out->pick.total = (uint32_t) total;
			out->pick.items = (char**) cf_malloc(n_args * sizeof(char*));
			out->pick.lens = (uint32_t*) cf_malloc(n_args * sizeof(uint32_t));
			out->pick.cum = (uint32_t*) cf_malloc(n_args * sizeof(uint32_t));
			uint32_t cum = 0;
			out->max_len = 0;
			for (uint32_t i = 0; i < n_args; i++) {
				out->pick.items[i] = args[i].s;
				out->pick.lens[i] = (uint32_t) strlen(args[i].s);
				args[i].s = NULL;
				cum += weighted ? args[i].weight : 1;
				out->pick.cum[i] = cum;
				out->max_len = max_u32(out->max_len, out->pick.lens[i]);
			}
			return 0;
		}

		case SYNTH_FMT: {
			if (args[0].type != ARG_STR) {
				*err_msg = "@fmt expects a string template, e.g. "
					"@fmt(\"#{first_name}.#{last_name}@example.com\")";
				*err_loc = args[0].loc;
				return -1;
			}
			char* src = args[0].s;
			args[0].s = NULL;
			if (_sg_parse_template(out, src, tok_loc, err_msg, err_loc) != 0) {
				cf_free(src);
				return -1;
			}
			return 0;
		}

		default:
			*err_msg = "Unknown generator";
			*err_loc = tok_loc;
			return -1;
	}
}

LOCAL_HELPER int
_sg_synth_parse_body(const char* str, const char** endptr, synth_spec_t* out,
		bool in_tpl, const char** err_msg, const char** err_loc)
{
	const char* tok_loc = str - (in_tpl ? 0 : 1);
	const char* p = str;

	while ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_') {
		p++;
	}
	size_t name_len = (size_t) (p - str);

	if (name_len == 0) {
		*err_msg = "Expected a generator name after '@' (see --help for the list)";
		*err_loc = tok_loc;
		return -1;
	}

	int kind = -1;
	for (int i = 0; i < SYNTH_N_KINDS; i++) {
		if (strncmp(synth_kinds[i].name, str, name_len) == 0 &&
				synth_kinds[i].name[name_len] == '\0') {
			kind = i;
			break;
		}
	}
	if (kind < 0) {
		snprintf(g_err_buf, sizeof(g_err_buf), "Unknown generator \"@%.*s\"",
				(int) name_len, str);
		*err_msg = g_err_buf;
		*err_loc = tok_loc;
		return -1;
	}

	memset(out, 0, sizeof(*out));
	out->kind = (uint8_t) kind;
	out->out_type = synth_kinds[kind].out_type;

	as_vector args;
	as_vector_inita(&args, sizeof(synth_arg_t), 8);

	if (*p == '(') {
		if (_sg_parse_args(p, &p, &args, err_msg, err_loc) != 0) {
			_sg_free_args(&args);
			return -1;
		}
	}

	const synth_kind_def_t* def = &synth_kinds[kind];
	if (args.size < def->min_args ||
			(def->max_args != ARGS_VARIADIC && args.size > def->max_args)) {
		if (def->max_args == 0) {
			snprintf(g_err_buf, sizeof(g_err_buf), "@%s takes no arguments",
					def->name);
		}
		else if (def->max_args == ARGS_VARIADIC) {
			snprintf(g_err_buf, sizeof(g_err_buf),
					"@%s expects at least %u argument(s)", def->name,
					def->min_args);
		}
		else if (def->min_args == def->max_args) {
			snprintf(g_err_buf, sizeof(g_err_buf),
					"@%s expects %u argument(s)", def->name, def->min_args);
		}
		else {
			snprintf(g_err_buf, sizeof(g_err_buf),
					"@%s expects between %u and %u arguments", def->name,
					def->min_args, def->max_args);
		}
		*err_msg = g_err_buf;
		*err_loc = tok_loc;
		_sg_free_args(&args);
		return -1;
	}

	out->has_args = args.size != 0;

	if (_sg_init_kind(out, &args, tok_loc, err_msg, err_loc) != 0) {
		_sg_free_args(&args);
		return -1;
	}

	_sg_free_args(&args);
	*endptr = p;
	return 0;
}


//==========================================================
// Local helpers: validation (tests only).
//

#ifdef _TEST

LOCAL_HELPER bool
_sg_fail(const synth_spec_t* spec, const char* why, const char* got)
{
	fprintf(stderr, "@%s value check failed: %s (got \"%s\")\n",
			synth_kinds[spec->kind].name, why, got ? got : "");
	return false;
}

LOCAL_HELPER bool
_sg_dict_has(const synth_dict_t* d, const char* s, size_t len)
{
	uint32_t lo = 0, hi = d->n;
	char buf[1024];
	if (len >= sizeof(buf)) {
		return false;
	}
	memcpy(buf, s, len);
	buf[len] = '\0';
	while (lo < hi) {
		uint32_t mid = lo + (hi - lo) / 2;
		uint32_t wlen;
		const char* w = synth_dict_word(d, mid, &wlen);
		int c = strcasecmp(w, buf);
		if (c == 0) {
			return strcmp(w, buf) == 0;
		}
		if (c < 0) {
			lo = mid + 1;
		}
		else {
			hi = mid;
		}
	}
	for (uint32_t i = 0; i < d->n; i++) {
		uint32_t wlen;
		const char* w = synth_dict_word(d, i, &wlen);
		if (wlen == len && memcmp(w, buf, len) == 0) {
			return true;
		}
	}
	return false;
}

LOCAL_HELPER bool
_sg_all_digits(const char* s, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (s[i] < '0' || s[i] > '9') {
			return false;
		}
	}
	return true;
}

LOCAL_HELPER bool
_sg_is_hex_lower(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

LOCAL_HELPER bool
_sg_check_username(const char* s, size_t n)
{
	if (n == 0) {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
					c == '_')) {
			return false;
		}
	}
	return true;
}

LOCAL_HELPER bool
_sg_check_words(const synth_dict_t* d, const char* s, size_t n, uint32_t* count)
{
	uint32_t c = 0;
	size_t start = 0;
	for (size_t i = 0; i <= n; i++) {
		if (i == n || s[i] == ' ') {
			if (i == start || !_sg_dict_has(d, s + start, i - start)) {
				return false;
			}
			c++;
			start = i + 1;
		}
	}
	*count = c;
	return true;
}

LOCAL_HELPER bool
_sg_check_domain(const char* s, size_t n)
{
	const char* dot = memchr(s, '.', n);
	if (dot == NULL || dot == s) {
		return false;
	}
	for (const char* p = s; p < dot; p++) {
		if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9'))) {
			return false;
		}
	}
	return _sg_dict_has(&SYNTH_DICT_TLD, dot + 1, n - (size_t) (dot + 1 - s));
}

LOCAL_HELPER bool
_sg_check_luhn(const char* s, size_t n)
{
	uint32_t sum = 0;
	for (size_t i = 0; i < n; i++) {
		uint32_t dgt = (uint32_t) (s[n - 1 - i] - '0');
		if (i & 1) {
			dgt *= 2;
			if (dgt > 9) {
				dgt -= 9;
			}
		}
		sum += dgt;
	}
	return sum % 10 == 0;
}

LOCAL_HELPER bool
_sg_check_coord(const char** p, int32_t min, int32_t max)
{
	char* end;
	double v = strtod(*p, &end);
	if (end == *p) {
		return false;
	}
	*p = end;
	int64_t micro = llround(v * 1e6);
	return micro >= min && micro <= max;
}

bool
synth_check_val(const synth_spec_t* spec, const as_val* val)
{
	if (val == NULL) {
		return _sg_fail(spec, "value is NULL", NULL);
	}

	switch (spec->out_type) {
		case SYNTH_OUT_INT: {
			as_integer* i = as_integer_fromval(val);
			if (i == NULL) {
				return _sg_fail(spec, "expected an integer", NULL);
			}
			int64_t v = as_integer_get(i);
			if (spec->kind == SYNTH_NOW) {
				int64_t now = (int64_t) cf_clock_getabsolute();
				if (v < now - 86400000 || v > now + 1000) {
					return _sg_fail(spec, "timestamp not near now", NULL);
				}
				return true;
			}
			if (v < spec->irange.min || v > spec->irange.max) {
				return _sg_fail(spec, "integer out of range", NULL);
			}
			return true;
		}

		case SYNTH_OUT_DOUBLE: {
			as_double* dv = as_double_fromval(val);
			if (dv == NULL) {
				return _sg_fail(spec, "expected a double", NULL);
			}
			double v = as_double_get(dv);
			if (spec->kind == SYNTH_DOUBLE) {
				if (!(v >= spec->drange.min && v <= spec->drange.max)) {
					return _sg_fail(spec, "double out of range", NULL);
				}
				return true;
			}
			int64_t micro = llround(v * 1e6);
			int32_t min = spec->kind == SYNTH_LAT ? spec->geo.lat_min : spec->geo.lon_min;
			int32_t max = spec->kind == SYNTH_LAT ? spec->geo.lat_max : spec->geo.lon_max;
			if (micro < min || micro > max) {
				return _sg_fail(spec, "coordinate out of range", NULL);
			}
			return true;
		}

		case SYNTH_OUT_GEO: {
			if (val->type != AS_GEOJSON) {
				return _sg_fail(spec, "expected a GeoJSON value", NULL);
			}
			const char* s = as_geojson_get((as_geojson*) val);
			const char* p;
			if (spec->kind == SYNTH_GEOJSON) {
				const char* prefix = "{\"type\":\"Point\",\"coordinates\":[";
				if (strncmp(s, prefix, strlen(prefix)) != 0) {
					return _sg_fail(spec, "bad GeoJSON point prefix", s);
				}
				p = s + strlen(prefix);
			}
			else {
				const char* prefix = "{\"type\":\"AeroCircle\",\"coordinates\":[[";
				if (strncmp(s, prefix, strlen(prefix)) != 0) {
					return _sg_fail(spec, "bad AeroCircle prefix", s);
				}
				p = s + strlen(prefix);
			}
			if (!_sg_check_coord(&p, spec->geo.lon_min, spec->geo.lon_max) || *p++ != ',' ||
					!_sg_check_coord(&p, spec->geo.lat_min, spec->geo.lat_max)) {
				return _sg_fail(spec, "coordinates out of range", s);
			}
			if (spec->kind == SYNTH_GEOJSON) {
				if (strcmp(p, "]}") != 0) {
					return _sg_fail(spec, "bad GeoJSON point suffix", s);
				}
				return true;
			}
			if (p[0] != ']' || p[1] != ',') {
				return _sg_fail(spec, "bad AeroCircle body", s);
			}
			char* end;
			unsigned long r = strtoul(p + 2, &end, 10);
			if (strcmp(end, "]}") != 0 || r < spec->geo.r_min || r > spec->geo.r_max) {
				return _sg_fail(spec, "bad AeroCircle radius", s);
			}
			return true;
		}

		default:
			break;
	}

	as_string* str = as_string_fromval(val);
	if (str == NULL) {
		return _sg_fail(spec, "expected a string", NULL);
	}
	const char* s = as_string_get(str);
	size_t n = as_string_len(str);

	if (n > spec->max_len && spec->kind != SYNTH_LOREM) {
		return _sg_fail(spec, "longer than max_len", s);
	}
	bool user_text = spec->kind == SYNTH_PICK || spec->kind == SYNTH_FMT;
	for (size_t i = 0; i < n && !user_text; i++) {
		if ((unsigned char) s[i] < 0x20 || (unsigned char) s[i] > 0x7e) {
			return _sg_fail(spec, "non printable ASCII character", s);
		}
	}

	const synth_dict_t* d = single_dict(spec->kind);
	if (d != NULL) {
		return _sg_dict_has(d, s, n) ? true : _sg_fail(spec, "not in dictionary", s);
	}

	switch (spec->kind) {
		case SYNTH_FULL_NAME:
			for (size_t i = 0; i < n; i++) {
				if (s[i] == ' ' && _sg_dict_has(&SYNTH_DICT_FIRST_NAME, s, i) &&
						_sg_dict_has(&SYNTH_DICT_LAST_NAME, s + i + 1, n - i - 1)) {
					return true;
				}
			}
			return _sg_fail(spec, "not \"<first> <last>\"", s);

		case SYNTH_USERNAME:
			return _sg_check_username(s, n) ? true : _sg_fail(spec, "bad username", s);

		case SYNTH_EMAIL: {
			const char* at = memchr(s, '@', n);
			if (at == NULL || memchr(at + 1, '@', n - (size_t) (at + 1 - s)) != NULL ||
					!_sg_check_username(s, (size_t) (at - s)) ||
					!_sg_dict_has(&SYNTH_DICT_EMAIL_PROVIDER, at + 1,
						n - (size_t) (at + 1 - s))) {
				return _sg_fail(spec, "bad email", s);
			}
			return true;
		}

		case SYNTH_PHONE:
			if (n != 12 || s[3] != '-' || s[7] != '-' || !_sg_all_digits(s, 3) ||
					!_sg_all_digits(s + 4, 3) || !_sg_all_digits(s + 8, 4) ||
					s[0] < '2' || s[4] < '2') {
				return _sg_fail(spec, "bad phone", s);
			}
			return true;

		case SYNTH_STREET: {
			const char* sp = memchr(s, ' ', n);
			const char* last_sp = memrchr(s, ' ', n);
			if (sp == NULL || sp == s || last_sp == sp ||
					!_sg_all_digits(s, (size_t) (sp - s)) || s[0] == '0' ||
					!_sg_dict_has(&SYNTH_DICT_STREET_SUFFIX, last_sp + 1,
						n - (size_t) (last_sp + 1 - s))) {
				return _sg_fail(spec, "bad street", s);
			}
			return true;
		}

		case SYNTH_ZIP: {
			if (n != 5 || !_sg_all_digits(s, 5)) {
				return _sg_fail(spec, "bad zip", s);
			}
			long z = strtol(s, NULL, 10);
			return (z >= 501 && z <= 99950) ? true : _sg_fail(spec, "zip out of range", s);
		}

		case SYNTH_COMPANY:
		case SYNTH_PRODUCT:
			return n > 0 ? true : _sg_fail(spec, "empty", s);

		case SYNTH_IPV4: {
			const char* p = s;
			for (uint32_t i = 0; i < 4; i++) {
				char* end;
				long o = strtol(p, &end, 10);
				if (end == p || o < 0 || o > 255 || (i == 0 && (o < 1 || o > 223))) {
					return _sg_fail(spec, "bad octet", s);
				}
				if (i < 3 && *end != '.') {
					return _sg_fail(spec, "missing dot", s);
				}
				p = end + (i < 3 ? 1 : 0);
			}
			return *p == '\0' ? true : _sg_fail(spec, "trailing characters", s);
		}

		case SYNTH_IPV6:
			if (n != 39) {
				return _sg_fail(spec, "bad length", s);
			}
			for (size_t i = 0; i < n; i++) {
				if ((i % 5 == 4) ? s[i] != ':' : !_sg_is_hex_lower(s[i])) {
					return _sg_fail(spec, "bad ipv6", s);
				}
			}
			return true;

		case SYNTH_MAC:
			if (n != 17) {
				return _sg_fail(spec, "bad length", s);
			}
			for (size_t i = 0; i < n; i++) {
				if ((i % 3 == 2) ? s[i] != ':' : !_sg_is_hex_lower(s[i])) {
					return _sg_fail(spec, "bad mac", s);
				}
			}
			return true;

		case SYNTH_UUID:
			if (n != 36 || s[8] != '-' || s[13] != '-' || s[18] != '-' ||
					s[23] != '-' || s[14] != '4' || strchr("89ab", s[19]) == NULL) {
				return _sg_fail(spec, "bad uuid v4", s);
			}
			for (size_t i = 0; i < n; i++) {
				if (i != 8 && i != 13 && i != 18 && i != 23 && !_sg_is_hex_lower(s[i])) {
					return _sg_fail(spec, "bad uuid hex", s);
				}
			}
			return true;

		case SYNTH_DOMAIN:
			return _sg_check_domain(s, n) ? true : _sg_fail(spec, "bad domain", s);

		case SYNTH_URL: {
			if (n < 13 || strncmp(s, "https://www.", 12) != 0) {
				return _sg_fail(spec, "bad url scheme", s);
			}
			const char* slash = memchr(s + 12, '/', n - 12);
			if (slash == NULL || !_sg_check_domain(s + 12, (size_t) (slash - s - 12)) ||
					!_sg_dict_has(&SYNTH_DICT_NOUN, slash + 1, n - (size_t) (slash + 1 - s))) {
				return _sg_fail(spec, "bad url", s);
			}
			return true;
		}

		case SYNTH_WORDS: {
			uint32_t count;
			if (!_sg_check_words(&SYNTH_DICT_WORD, s, n, &count) || count != spec->n) {
				return _sg_fail(spec, "bad word list", s);
			}
			return true;
		}

		case SYNTH_SENTENCE: {
			if (n < 2 || s[n - 1] != '.' || s[0] < 'A' || s[0] > 'Z') {
				return _sg_fail(spec, "bad sentence shape", s);
			}
			char buf[2048];
			if (n >= sizeof(buf)) {
				return _sg_fail(spec, "sentence too long", s);
			}
			memcpy(buf, s, n - 1);
			buf[0] = (char) (buf[0] | 0x20);
			uint32_t count;
			bool ok = _sg_check_words(&SYNTH_DICT_WORD, buf, n - 1, &count);
			if (!ok) {
				buf[0] = s[0];
				ok = _sg_check_words(&SYNTH_DICT_WORD, buf, n - 1, &count);
			}
			if (!ok || count < 5 || count > 12) {
				return _sg_fail(spec, "bad sentence words", s);
			}
			return true;
		}

		case SYNTH_LOREM:
			if (n != spec->n || s[0] == ' ' || s[n - 1] == ' ') {
				return _sg_fail(spec, "bad lorem length or padding", s);
			}
			return true;

		case SYNTH_CREDIT_CARD: {
			bool prefix_ok = (n == 16 && (s[0] == '4' ||
						(s[0] == '5' && s[1] >= '1' && s[1] <= '5') ||
						strncmp(s, "6011", 4) == 0)) ||
				(n == 15 && s[0] == '3' && (s[1] == '4' || s[1] == '7'));
			if (!prefix_ok || !_sg_all_digits(s, n) || !_sg_check_luhn(s, n)) {
				return _sg_fail(spec, "bad card number", s);
			}
			return true;
		}

		case SYNTH_DATE: {
			if (spec->date.fmt != NULL) {
				return n > 0 ? true : _sg_fail(spec, "empty date", s);
			}
			int64_t days;
			if (!_sg_parse_date_str(s, &days)) {
				return _sg_fail(spec, "not YYYY-MM-DD", s);
			}
			if (days < floor_div(spec->date.min, 86400) ||
					days > floor_div(spec->date.max, 86400)) {
				return _sg_fail(spec, "date out of range", s);
			}
			return true;
		}

		case SYNTH_PICK:
			for (uint32_t i = 0; i < spec->pick.n; i++) {
				if (spec->pick.lens[i] == n && memcmp(spec->pick.items[i], s, n) == 0) {
					return true;
				}
			}
			return _sg_fail(spec, "not one of the options", s);

		case SYNTH_FMT:
			return strstr(s, "#{") == NULL ? true : _sg_fail(spec, "unexpanded placeholder", s);

		default:
			return _sg_fail(spec, "unknown kind", s);
	}
}

#endif /* _TEST */
