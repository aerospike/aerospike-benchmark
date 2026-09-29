#include <check.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <synth_data.h>


#define COVERAGE_MAX_N 64
#define COVERAGE_PICKS 100000
#define INDEX_SAMPLES 1000

#define X(n) &SYNTH_DICT_##n,
static const synth_dict_t* const g_dicts[] = {
	SYNTH_DICT_LIST(X)
};
#undef X

#define X(n) #n,
static const char* const g_names[] = {
	SYNTH_DICT_LIST(X)
};
#undef X

#define N_DICTS (sizeof(g_dicts) / sizeof(g_dicts[0]))


static uint64_t
synth_test_splitmix64(uint64_t* state)
{
	uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

static const char*
word_at(const synth_dict_t* d, uint32_t i)
{
	uint32_t len;
	return synth_dict_word(d, i, &len);
}

static uint32_t
len_at(const synth_dict_t* d, uint32_t i)
{
	uint32_t len;
	synth_dict_word(d, i, &len);
	return len;
}

static int
is_partner_table(const synth_dict_t* d)
{
	return d == &SYNTH_DICT_STATE_ABBR || d == &SYNTH_DICT_COUNTRY_CODE;
}

static void
assert_codes(const char* name, const synth_dict_t* d)
{
	for (uint32_t i = 0; i < d->n; i++) {
		const char* w = word_at(d, i);
		if (len_at(d, i) != 2 || !isupper((unsigned char) w[0]) ||
				!isupper((unsigned char) w[1])) {
			ck_abort_msg("%s[%u] \"%s\" is not two uppercase letters", name, i, w);
		}
		for (uint32_t j = 0; j < i; j++) {
			if (strcmp(word_at(d, j), w) == 0) {
				ck_abort_msg("%s[%u] \"%s\" duplicates entry %u", name, i, w, j);
			}
		}
	}
}

static void
assert_partner(const synth_dict_t* names, const synth_dict_t* codes,
		const char* name, const char* code)
{
	for (uint32_t i = 0; i < names->n; i++) {
		if (strcmp(word_at(names, i), name) == 0) {
			ck_assert_str_eq(word_at(codes, i), code);
			return;
		}
	}
	ck_abort_msg("\"%s\" not found", name);
}


START_TEST(dicts_nonempty)
{
	for (uint32_t k = 0; k < N_DICTS; k++) {
		ck_assert_msg(g_dicts[k]->n > 0, "%s is empty", g_names[k]);
		ck_assert_msg(g_dicts[k]->off[0] == 0, "%s off[0] != 0", g_names[k]);
	}
}
END_TEST

START_TEST(offsets_increasing)
{
	for (uint32_t k = 0; k < N_DICTS; k++) {
		const synth_dict_t* d = g_dicts[k];
		for (uint32_t i = 0; i < d->n; i++) {
			if (d->off[i + 1] <= d->off[i]) {
				ck_abort_msg("%s off[%u] not increasing", g_names[k], i + 1);
			}
			if (d->pool[d->off[i + 1] - 1] != '\0') {
				ck_abort_msg("%s word %u not NUL terminated", g_names[k], i);
			}
			if (strlen(word_at(d, i)) != len_at(d, i)) {
				ck_abort_msg("%s word %u has an embedded NUL", g_names[k], i);
			}
		}
	}
}
END_TEST

START_TEST(words_printable)
{
	for (uint32_t k = 0; k < N_DICTS; k++) {
		const synth_dict_t* d = g_dicts[k];
		for (uint32_t i = 0; i < d->n; i++) {
			uint32_t len;
			const char* w = synth_dict_word(d, i, &len);
			for (uint32_t j = 0; j < len; j++) {
				unsigned char c = (unsigned char) w[j];
				if (!isprint(c) || c == '"' || c == '\\' || c == '?') {
					ck_abort_msg("%s word %u has invalid byte 0x%02x", g_names[k], i, c);
				}
			}
		}
	}
}
END_TEST

START_TEST(no_empty_words)
{
	for (uint32_t k = 0; k < N_DICTS; k++) {
		const synth_dict_t* d = g_dicts[k];
		for (uint32_t i = 0; i < d->n; i++) {
			if (len_at(d, i) == 0) {
				ck_abort_msg("%s word %u is empty", g_names[k], i);
			}
		}
	}
}
END_TEST

START_TEST(max_len_exact)
{
	for (uint32_t k = 0; k < N_DICTS; k++) {
		const synth_dict_t* d = g_dicts[k];
		uint32_t longest = 0;
		for (uint32_t i = 0; i < d->n; i++) {
			uint32_t len = len_at(d, i);
			if (len > d->max_len) {
				ck_abort_msg("%s word %u longer than max_len %u", g_names[k], i, d->max_len);
			}
			if (len > longest) {
				longest = len;
			}
		}
		ck_assert_msg(longest == d->max_len, "%s max_len %u, longest word %u",
				g_names[k], d->max_len, longest);
	}
}
END_TEST

START_TEST(sorted_unique)
{
	for (uint32_t k = 0; k < N_DICTS; k++) {
		const synth_dict_t* d = g_dicts[k];
		if (is_partner_table(d)) {
			continue;
		}
		for (uint32_t i = 1; i < d->n; i++) {
			const char* prev = word_at(d, i - 1);
			const char* cur = word_at(d, i);
			if (strcasecmp(prev, cur) >= 0) {
				ck_abort_msg("%s not sorted unique at %u: \"%s\" >= \"%s\"", g_names[k], i,
						prev, cur);
			}
		}
	}
}
END_TEST

START_TEST(state_pairs)
{
	ck_assert_uint_eq(SYNTH_DICT_STATE.n, 50);
	ck_assert_uint_eq(SYNTH_DICT_STATE_ABBR.n, 50);
	assert_codes("STATE_ABBR", &SYNTH_DICT_STATE_ABBR);
	assert_partner(&SYNTH_DICT_STATE, &SYNTH_DICT_STATE_ABBR, "Alaska", "AK");
	assert_partner(&SYNTH_DICT_STATE, &SYNTH_DICT_STATE_ABBR, "California", "CA");
	assert_partner(&SYNTH_DICT_STATE, &SYNTH_DICT_STATE_ABBR, "Wyoming", "WY");
}
END_TEST

START_TEST(country_pairs)
{
	ck_assert_uint_eq(SYNTH_DICT_COUNTRY.n, SYNTH_DICT_COUNTRY_CODE.n);
	assert_codes("COUNTRY_CODE", &SYNTH_DICT_COUNTRY_CODE);
	assert_partner(&SYNTH_DICT_COUNTRY, &SYNTH_DICT_COUNTRY_CODE, "Andorra", "AD");
	assert_partner(&SYNTH_DICT_COUNTRY, &SYNTH_DICT_COUNTRY_CODE, "Germany", "DE");
	assert_partner(&SYNTH_DICT_COUNTRY, &SYNTH_DICT_COUNTRY_CODE, "Zimbabwe", "ZW");
}
END_TEST

START_TEST(tld_no_dot)
{
	for (uint32_t i = 0; i < SYNTH_DICT_TLD.n; i++) {
		ck_assert_msg(strchr(word_at(&SYNTH_DICT_TLD, i), '.') == NULL,
				"TLD \"%s\" contains a dot", word_at(&SYNTH_DICT_TLD, i));
	}
}
END_TEST

START_TEST(email_provider_dot)
{
	for (uint32_t i = 0; i < SYNTH_DICT_EMAIL_PROVIDER.n; i++) {
		ck_assert_msg(strchr(word_at(&SYNTH_DICT_EMAIL_PROVIDER, i), '.') != NULL,
				"EMAIL_PROVIDER \"%s\" has no dot", word_at(&SYNTH_DICT_EMAIL_PROVIDER, i));
	}
}
END_TEST

START_TEST(index_bounds)
{
	uint64_t state = 0x5eed;
	for (uint32_t k = 0; k < N_DICTS; k++) {
		const synth_dict_t* d = g_dicts[k];
		ck_assert_uint_eq(synth_dict_index(d, 0), 0);
		ck_assert_uint_lt(synth_dict_index(d, 1), d->n);
		ck_assert_uint_eq(synth_dict_index(d, 0xffffffff), d->n - 1);
		for (uint32_t i = 0; i < INDEX_SAMPLES; i++) {
			uint32_t r = (uint32_t) (synth_test_splitmix64(&state) >> 32);
			if (synth_dict_index(d, r) >= d->n) {
				ck_abort_msg("%s index(%u) out of range", g_names[k], r);
			}
		}
	}
}
END_TEST

START_TEST(index_coverage)
{
	uint32_t checked = 0;
	for (uint32_t k = 0; k < N_DICTS; k++) {
		const synth_dict_t* d = g_dicts[k];
		uint32_t counts[COVERAGE_MAX_N] = { 0 };
		uint64_t state = 0xc0ffee + k;
		uint32_t lo = UINT32_MAX;
		uint32_t hi = 0;
		if (d->n > COVERAGE_MAX_N) {
			continue;
		}
		for (uint32_t i = 0; i < COVERAGE_PICKS; i++) {
			uint32_t idx = synth_dict_index(d, (uint32_t) (synth_test_splitmix64(&state) >> 32));
			if (idx >= d->n) {
				ck_abort_msg("%s index %u out of range", g_names[k], idx);
			}
			counts[idx]++;
		}
		for (uint32_t i = 0; i < d->n; i++) {
			lo = counts[i] < lo ? counts[i] : lo;
			hi = counts[i] > hi ? counts[i] : hi;
		}
		ck_assert_msg(lo > 0, "%s has entries never picked", g_names[k]);
		ck_assert_msg(hi < 2 * lo, "%s bucket ratio %u/%u too skewed", g_names[k], hi, lo);
		checked++;
	}
	ck_assert_uint_gt(checked, 0);
}
END_TEST


Suite*
synth_data_suite(void)
{
	Suite* s;
	TCase* tc_layout;
	TCase* tc_content;
	TCase* tc_index;

	s = suite_create("Synth Data");

	tc_layout = tcase_create("Layout");
	tcase_add_test(tc_layout, dicts_nonempty);
	tcase_add_test(tc_layout, offsets_increasing);
	tcase_add_test(tc_layout, words_printable);
	tcase_add_test(tc_layout, no_empty_words);
	tcase_add_test(tc_layout, max_len_exact);
	tcase_add_test(tc_layout, sorted_unique);
	suite_add_tcase(s, tc_layout);

	tc_content = tcase_create("Content");
	tcase_add_test(tc_content, state_pairs);
	tcase_add_test(tc_content, country_pairs);
	tcase_add_test(tc_content, tld_no_dot);
	tcase_add_test(tc_content, email_provider_dot);
	suite_add_tcase(s, tc_content);

	tc_index = tcase_create("Index");
	tcase_add_test(tc_index, index_bounds);
	tcase_add_test(tc_index, index_coverage);
	suite_add_tcase(s, tc_index);

	return s;
}
