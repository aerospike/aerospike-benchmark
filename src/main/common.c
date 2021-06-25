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

//==========================================================
// Includes.
//

#include <string.h>
#include <sys/time.h>

#include <aerospike/as_boolean.h>

#include "common.h"


//==========================================================
// Forward declarations.
//

LOCAL_HELPER int as_bytes_cmp(as_bytes* b1, as_bytes* b2);
LOCAL_HELPER int as_list_cmp_max(const as_list* list1, const as_list* list2,
		uint32_t max, uint32_t fin);
LOCAL_HELPER int as_list_cmp(const as_list* list1, const as_list* list2);
LOCAL_HELPER int as_vector_cmp(as_vector* list1, as_vector* list2);
LOCAL_HELPER bool key_append(const as_val* key, const as_val* val, void* udata);
LOCAL_HELPER int key_cmp(const void* v1, const void* v2);
LOCAL_HELPER bool map_to_sorted_keys(const as_map* map, uint32_t size,
		as_vector* list);
LOCAL_HELPER int as_map_cmp(const as_map* map1, const as_map* map2);


//==========================================================
// Public API.
//

/*
 * algorithm inspired by: https://stackoverflow.com/a/18027868
 */
int dec_display_len(size_t number)
{
	struct {
		uint64_t max;
		int digits;
	} lookup[] = {
		{ -1, 1 }, { -1, 1 }, { -1, 1 }, { 9, 1 },
		{ -1, 2 }, { -1, 2 }, { 99, 2 },
		{ -1, 3 }, { -1, 3 }, { 999, 3 },
		{ -1, 4 }, { -1, 4 }, { -1, 4 }, { 9999, 4},
		{ -1, 5 }, { -1, 5 }, { 99999, 5 },
		{ -1, 6 }, { -1, 6 }, { 999999, 6 },
		{ -1, 7 }, { -1, 7 }, { -1, 7 }, { 9999999, 7 },
		{ -1, 8 }, { -1, 8 }, { 99999999, 8 },
		{ -1, 9 }, { -1, 9 }, { 999999999, 9 },
		{ -1, 10 }, { -1, 10 }, { -1, 10 }, { 9999999999, 10 },
		{ -1, 11 }, { -1, 11 }, { 99999999999, 11 },
		{ -1, 12 }, { -1, 12 }, { 999999999999, 12 },
		{ -1, 13 }, { -1, 13 }, { -1, 13 }, { 9999999999999, 13 },
		{ -1, 14 }, { -1, 14 }, { 99999999999999, 14 },
		{ -1, 15 }, { -1, 15 }, { 999999999999999, 15 },
		{ -1, 16 }, { -1, 16 }, { -1, 16 }, { 9999999999999999, 16 },
		{ -1, 17 }, { -1, 17 }, { 99999999999999999, 17 },
		{ -1, 18 }, { -1, 18 }, { 999999999999999999, 18 },
		{ -1, 19 }, { -1, 19 }, { -1, 19 }, { 9999999999999999999U, 19 },
	};

	uint32_t digits = (8 * sizeof(number) - 1) - __builtin_clzl(number);
	return number == 0 ? 1 :
		lookup[digits].digits + (lookup[digits].max < number);
}

void
blog_detailv(as_log_level level, const char* fmt, va_list ap)
{
	// Write message all at once so messages generated from multiple threads
	// have less of a chance of getting garbled.
	char fmtbuf[1024];
	struct timeval now;
	gettimeofday(&now, NULL);

	struct tm* t = localtime(&now.tv_sec);
	uint64_t msecs = now.tv_usec / 1000;
	int len = sprintf(fmtbuf, "%d-%02d-%02d %02d:%02d:%02d.%03" PRIu64 " %s ",
		t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min,
		t->tm_sec, msecs, as_log_level_tostring(level));

	size_t len2 = strlen(fmt);
	char* p = fmtbuf + len;
	memcpy(p, fmt, len2);
	p += len2;
	*p = 0;
	
	vprintf(fmtbuf, ap);
}

void
blog_detail(as_log_level level, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	blog_detailv(level, fmt, ap);
	va_end(ap);
}


#ifdef __APPLE__

char* strchrnul(const char *s, int c_in)
{
	char* r = strchr(s, c_in);
	return (char*) (r == NULL ? s + strlen(s) : r);
}

void* memrchr(const void* s, int c, size_t n)
{
	const uint8_t *cp;

	if (n != 0) {
		cp = (uint8_t*) s + n;
		do {
			if (*(--cp) == (uint8_t) c) {
				return (void*) cp;
			}
		} while (--n != 0);
	}
    return NULL;
}

#endif /* __linux__ */

const char* utc_time_str(time_t t)
{
	static char buf[UTC_STR_LEN + 1];
	struct tm * utc = gmtime(&t);
	snprintf(buf, sizeof(buf),
			"%4d-%02d-%02dT%02d:%02d:%02dZ",
			1900 + utc->tm_year, utc->tm_mon + 1, utc->tm_mday,
			utc->tm_hour, utc->tm_min, utc->tm_sec);
	return buf;
}

uint32_t gen_rand_range(as_random* random, uint32_t max)
{
	/*
	 * To eliminate any statistical bias, we'd want to choose only from a range
	 * [0, n * max), for some integer n, before modding by max. Since max may
	 * not be a power of two (and therefore not divide 2^32), the natural range
	 * of as_random_next_uint32 of [0, 2^32) will be biased towards the smaller
	 * numbers. To accomodate this, we can choose from the range
	 * [0, 2^32 - (2^32 % max)), or, equivalently (and cheaper computationally),
	 * [2^32 % max, 2^32)
	 *
	 * calculate 0x100000000LU % max, which is equivalent to
	 * (0x100000000LU - max) % max = ((uint32_t) -max) % max
	 *
	 * doing this with 32-bit numbers is faster than with 64-bit
	 */
	uint32_t rem = (-max) % max;
	uint32_t r;

	do {
		r = as_random_next_uint32(random);
	} while (UNLIKELY(r < rem));

	return r % max;
}

uint64_t gen_rand_range_64(as_random* random, uint64_t max)
{
	uint64_t rem = (-max) % max;
	uint64_t r;

	do {
		r = as_random_next_uint64(random);
	} while (UNLIKELY(r < rem));

	return r % max;
}

int
as_val_cmp(const as_val* v1, const as_val* v2)
{
	if (v1->type == AS_CMP_WILDCARD || v2->type == AS_CMP_WILDCARD) {
		return 0;
	}

	if (v1->type != v2->type) {
		return v1->type - v2->type;
	}

	switch (v1->type) {
		case AS_BOOLEAN:
			return ((as_boolean*)v1)->value - ((as_boolean*)v2)->value;

		case AS_INTEGER: {
			int64_t cmp = ((as_integer*)v1)->value - ((as_integer*)v2)->value;
			return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
		}

		case AS_DOUBLE: {
			double cmp = ((as_double*)v1)->value - ((as_double*)v2)->value;
			return cmp < 0.0 ? -1 : cmp > 0.0 ? 1 : 0;
		}

		case AS_STRING:
			return strcmp(((as_string*)v1)->value, ((as_string*)v2)->value);

		case AS_GEOJSON:
			return strcmp(((as_geojson*)v1)->value, ((as_geojson*)v2)->value);

		case AS_BYTES:
			return as_bytes_cmp((as_bytes*)v1, (as_bytes*)v2);

		case AS_LIST:
			return as_list_cmp((as_list*)v1, (as_list*)v2);

		case AS_MAP:
			return as_map_cmp((as_map*)v1, (as_map*)v2);

		default:
			return 0;
	}
}

bool bin_name_too_large(size_t name_len, uint32_t n_bins)
{
	if (n_bins == 1) {
		return name_len >= sizeof(as_bin_name);
	}

	int max_display_len = dec_display_len(n_bins);
	// key format: <key_name>_<bin_num>
	return (name_len + 1 + max_display_len) >= sizeof(as_bin_name);
}

void gen_bin_name(as_bin_name name_buf, const char* bin_name, uint32_t bin_idx)
{
	if (bin_idx == 0) {
		strncpy(name_buf, bin_name, sizeof(as_bin_name) - 1);
		// in case bin_name exactly filled the buffer, add the
		// null-terminater, since strncpy doesn't do that in this
		// instance
		name_buf[sizeof(as_bin_name) - 1] = '\0';
	}
	else {
		snprintf(name_buf, sizeof(as_bin_name), "%s_%d", bin_name, bin_idx + 1);
	}
}

char* parse_string_literal(const char* restrict str,
		const char** restrict endptr)
{
	if (*str != '"') {
		fprintf(stderr, "Expected a '\"' at the beginning of the string\n");
		return NULL;
	}

	// calculate the string length
	uint64_t len = 0;
	const char* end = str + 1;
	while (*end != '"') {
		if (*end == '\0') {
			fprintf(stderr, "Unterminated '\"' in string literal\n");
			return NULL;
		}
		if (*end == '\\') {
			end++;
			if (*end == '\0') {
				fprintf(stderr, "Dangling escape character\n");
				return NULL;
			}
			if (*end == 'x') {
				if (*(end + 1) == '\0' || *(end + 2) == '\0') {
					fprintf(stderr, "Unterminated hexadecimal escape sequence\n");
					return NULL;
				}
				end += 2;
			}
			else if ('0' <= *end && *end <= '3') {
				if (*(end + 1) < '0' || *(end + 1) > '7' ||
						*(end + 2) < '0' || *(end + 2) > '7') {
					fprintf(stderr, "Invalid octal string \"%.4s\"\n", end - 1);
					return NULL;
				}
				end += 2;
			}
		}
		end++;
		len++;
	}

	// allocate a buffer for the parsed string
	char* res = (char*) cf_malloc(len + 1);
	uint64_t i = 0;
	for (const char* s = str + 1; s != end; s++, i++) {
		if (*s == '\\') {
			switch (*(s + 1)) {
				case 'a':
					res[i] = '\a';
					break;
				case 'b':
					res[i] = '\b';
					break;
				case 'e':
					res[i] = '\e';
					break;
				case 'f':
					res[i] = '\f';
					break;
				case 'n':
					res[i] = '\n';
					break;
				case 'r':
					res[i] = '\r';
					break;
				case 't':
					res[i] = '\t';
					break;
				case 'v':
					res[i] = '\v';
					break;
				case '\\':
					res[i] = '\\';
					break;
				case '\'':
					res[i] = '\'';
					break;
				case '"':
					res[i] = '\"';
					break;
				case '?':
					res[i] = '\?';
					break;

				case '0':
				case '1':
				case '2':
				case '3': {
					// parse as octal, which has already been error checked in the first pass
					int8_t val = (*(s + 1) - '0') * 64 +
						(*(s + 2) - '0') * 8 + (*(s + 3) - '0');
					res[i] = val;
					s += 2;
					break;
				}

				case 'x': {
					char* endptr;
					// move the next two characters into a local buffer that we null-terminate,
					// then try parsing as a 2-digit hex string
					char buf[3];
					memcpy(buf, s + 2, 2 * sizeof(char));
					buf[2] = '\0';
					uint64_t val = strtoul(buf, &endptr, 16);
					if (endptr != ((char*) buf) + 2) {
						fprintf(stderr, "Invalid hexadecimal escape sequence "
								"\"\\%.3s\"\n", s + 1);
						cf_free(res);
						return NULL;
					}

					res[i] = (int8_t) val;
					s += 2;
					break;
				}

				default:
					fprintf(stderr, "Unknown escape sequence \"\\%c\"\n",
							*(s + 1));
					cf_free(res);
					return NULL;
			}
			s++;
		}
		else {
			res[i] = *s;
		}
	}
	res[len] = '\0';

	if (endptr != NULL) {
		*endptr = end + 1;
	}
	return res;
}

void print_hdr_percentiles(struct hdr_histogram* h, const char* name,
		uint64_t elapsed_s, as_vector* percentiles, FILE *out_file)
{
	int64_t min, max;
	int64_t total_cnt;

	total_cnt = hdr_total_count(h);
	min = total_cnt == 0 ? 0 : hdr_min(h);
	max = hdr_max(h);
	fprintf(out_file, "hdr: %-5s %.24s %" PRIu64 ", %" PRIu64 ", %" PRId64
			", %" PRId64,
			name, utc_time_str(time(NULL)), elapsed_s, total_cnt, min, max);
	for (uint32_t i = 0; i < percentiles->size; i++) {
		double p = *(double *) as_vector_get(percentiles, i);
		uint64_t cnt = hdr_value_at_percentile(h, p);
		fprintf(out_file, ", %" PRIu64, cnt);
	}
	fprintf(out_file, "\n");
}


//==========================================================
// Local helpers.
//

LOCAL_HELPER int
as_bytes_cmp(as_bytes* b1, as_bytes* b2)
{
	if (b1->size == b2->size) {
		return memcmp(b1->value, b2->value, b1->size);
	}
	else if (b1->size < b2->size) {
		int cmp = memcmp(b1->value, b2->value, b1->size);
		return cmp != 0 ? cmp : -1;
	}
	else {
		int cmp = memcmp(b1->value, b2->value, b2->size);
		return cmp != 0 ? cmp : 1;
	}
}

LOCAL_HELPER int
as_list_cmp_max(const as_list* list1, const as_list* list2, uint32_t max, uint32_t fin)
{
	for (uint32_t i = 0; i < max; i++) {
		int cmp = as_val_cmp(as_list_get(list1, i), as_list_get(list2, i));

		if (cmp != 0) {
			return cmp;
		}
	}
	return fin;
}

LOCAL_HELPER int
as_list_cmp(const as_list* list1, const as_list* list2)
{
	uint32_t size1 = as_list_size(list1);
	uint32_t size2 = as_list_size(list2);

	if (size1 == size2) {
		return as_list_cmp_max(list1, list2, size1, 0);
	}
	else if (size1 < size2) {
		return as_list_cmp_max(list1, list2, size1, -1);
	}
	else {
		return as_list_cmp_max(list1, list2, size2, 1);
	}
}

LOCAL_HELPER int
as_vector_cmp(as_vector* list1, as_vector* list2)
{
	// Size of vectors should already be the same.
	for (uint32_t i = 0; i < list1->size; i++) {
		int cmp = as_val_cmp(as_vector_get_ptr(list1, i), as_vector_get_ptr(list2, i));

		if (cmp != 0) {
			return cmp;
		}
	}
	return 0;
}

LOCAL_HELPER bool
key_append(const as_val* key, const as_val* val, void* udata)
{
	as_vector_append(udata, (void*)&key);
	return true;
}

LOCAL_HELPER int
key_cmp(const void* v1, const void* v2)
{
	return as_val_cmp(*(as_val**)v1, *(as_val**)v2);
}

LOCAL_HELPER bool
map_to_sorted_keys(const as_map* map, uint32_t size, as_vector* list)
{
	as_vector_init(list, sizeof(as_val*), size);

	if (! as_map_foreach(map, key_append, list)) {
		return false;
	}

	// Sort list of map entries.
	qsort(list->list, list->size, sizeof(as_val*), key_cmp);
	return true;
}

LOCAL_HELPER int
as_map_cmp(const as_map* map1, const as_map* map2)
{
	// Map ordering documented at https://www.aerospike.com/docs/guide/cdt-ordering.html
	uint32_t size1 = as_map_size(map1);
	uint32_t size2 = as_map_size(map2);
	int cmp = size1 - size2;

	if (cmp != 0) {
		return cmp;
	}

	// Convert maps to lists of keys and sort before comparing.
	as_vector list1;

	if (map_to_sorted_keys(map1, size1, &list1)) {
		as_vector list2;

		if (map_to_sorted_keys(map2, size2, &list2)) {
			cmp = as_vector_cmp(&list1, &list2);
		}
		as_vector_destroy(&list2);
	}
	as_vector_destroy(&list1);
	return cmp;
}

