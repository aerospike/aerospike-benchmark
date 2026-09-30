#include <check.h>
#include <stdio.h>
#include <string.h>

#include <aerospike/as_msgpack.h>
#include <aerospike/as_string.h>

#include <benchmark.h>
#include <common.h>
#include <object_spec.h>
#include <synth_data.h>
#include <synth_gen.h>


#define N_SAMPLES 10000


static void
simple_setup(void)
{
	freopen("/dev/null", "w", stderr);
}

static void
simple_teardown(void)
{
}

static const synth_spec_t*
_gen_of(const struct obj_spec_s* o)
{
	const struct bin_spec_s* b = obj_spec_bin_spec(o, 0);
	ck_assert_uint_eq(b->type, BIN_SPEC_TYPE_GEN);
	return &b->gen;
}

static void
_check_samples(const char* spec_str)
{
	struct obj_spec_s o;
	as_random random;
	seed_as_random(&random, 0x5EEDLU, __LINE__);

	ck_assert_msg(obj_spec_parse(&o, spec_str) == 0, "failed to parse %s", spec_str);
	const synth_spec_t* spec = _gen_of(&o);

	for (uint32_t i = 0; i < N_SAMPLES; i++) {
		as_val* v = obj_spec_gen_bin_val(&o, 0, &random, 1.f);
		ck_assert_msg(synth_check_val(spec, v), "%s produced an invalid value "
				"on sample %u", spec_str, i);
		as_val_destroy(v);
	}
	obj_spec_free(&o);
}

#define DEFINE_SAMPLE_TEST(test_name, spec_str) \
START_TEST(test_name) \
{ \
	_check_samples(spec_str); \
} \
END_TEST

DEFINE_SAMPLE_TEST(samples_first_name, "@first_name");
DEFINE_SAMPLE_TEST(samples_last_name, "@last_name");
DEFINE_SAMPLE_TEST(samples_full_name, "@full_name");
DEFINE_SAMPLE_TEST(samples_username, "@username");
DEFINE_SAMPLE_TEST(samples_email, "@email");
DEFINE_SAMPLE_TEST(samples_phone, "@phone");
DEFINE_SAMPLE_TEST(samples_street, "@street");
DEFINE_SAMPLE_TEST(samples_city, "@city");
DEFINE_SAMPLE_TEST(samples_state, "@state");
DEFINE_SAMPLE_TEST(samples_state_abbr, "@state_abbr");
DEFINE_SAMPLE_TEST(samples_zip, "@zip");
DEFINE_SAMPLE_TEST(samples_country, "@country");
DEFINE_SAMPLE_TEST(samples_country_code, "@country_code");
DEFINE_SAMPLE_TEST(samples_lat, "@lat");
DEFINE_SAMPLE_TEST(samples_lat_range, "@lat(-10.5,10.5)");
DEFINE_SAMPLE_TEST(samples_lon, "@lon");
DEFINE_SAMPLE_TEST(samples_geojson, "@geojson");
DEFINE_SAMPLE_TEST(samples_geojson_bbox, "@geojson(-33.9,-33.8,151.1,151.3)");
DEFINE_SAMPLE_TEST(samples_geo_circle, "@geo_circle");
DEFINE_SAMPLE_TEST(samples_geo_circle_bbox, "@geo_circle(10,20,0,1,-1,0)");
DEFINE_SAMPLE_TEST(samples_company, "@company");
DEFINE_SAMPLE_TEST(samples_job_title, "@job_title");
DEFINE_SAMPLE_TEST(samples_ipv4, "@ipv4");
DEFINE_SAMPLE_TEST(samples_ipv6, "@ipv6");
DEFINE_SAMPLE_TEST(samples_mac, "@mac");
DEFINE_SAMPLE_TEST(samples_url, "@url");
DEFINE_SAMPLE_TEST(samples_domain, "@domain");
DEFINE_SAMPLE_TEST(samples_uuid, "@uuid");
DEFINE_SAMPLE_TEST(samples_word, "@word");
DEFINE_SAMPLE_TEST(samples_words, "@words(7)");
DEFINE_SAMPLE_TEST(samples_words_one, "@words(1)");
DEFINE_SAMPLE_TEST(samples_sentence, "@sentence");
DEFINE_SAMPLE_TEST(samples_lorem, "@lorem(37)");
DEFINE_SAMPLE_TEST(samples_lorem_one, "@lorem(1)");
DEFINE_SAMPLE_TEST(samples_product, "@product");
DEFINE_SAMPLE_TEST(samples_color, "@color");
DEFINE_SAMPLE_TEST(samples_credit_card, "@credit_card");
DEFINE_SAMPLE_TEST(samples_date, "@date");
DEFINE_SAMPLE_TEST(samples_date_old, "@date(\"1900-01-01\",\"1969-12-31\")");
DEFINE_SAMPLE_TEST(samples_date_fmt, "@date(1577836800,1735689599,\"%d/%m/%Y %H:%M\")");
DEFINE_SAMPLE_TEST(samples_timestamp, "@timestamp");
DEFINE_SAMPLE_TEST(samples_int, "@int(-1000,1000)");
DEFINE_SAMPLE_TEST(samples_int_full, "@int(-9223372036854775808,9223372036854775807)");
DEFINE_SAMPLE_TEST(samples_double, "@double(-2.5,7.25)");
DEFINE_SAMPLE_TEST(samples_pick, "@pick(\"a\",\"bb\",\"ccc\")");
DEFINE_SAMPLE_TEST(samples_pick_weighted, "@pick(\"x\":1,\"y\":1000000)");
DEFINE_SAMPLE_TEST(samples_fmt, "@fmt(\"#{first_name}.#{last_name}+#{int(1,99)}@#{domain}\")");
DEFINE_SAMPLE_TEST(samples_fmt_double, "@fmt(\"#{double(-5,5)} / #{lat} / #{now}\")");

START_TEST(samples_now)
{
	_check_samples("@now");
}
END_TEST


static void
_populate_seeded(const struct obj_spec_s* o, const as_bin_name* names,
		as_record* rec, uint64_t seed, uint64_t key)
{
	as_random random;
	seed_as_random(&random, 0x5EEDLU, __LINE__);
	uint64_t x = seed ^ (key * 0x9E3779B97F4A7C15LU);
	uint64_t record_seed = splitmix64(&x);
	as_record_init(rec, obj_spec_n_bins(o));
	ck_assert_int_eq(obj_spec_populate_bins_named(o, rec, &random, names, NULL,
				0, 1.f, &record_seed), 0);
}

static bool
_records_equal(const struct obj_spec_s* o, const as_bin_name* names,
		const as_record* a, const as_record* b)
{
	for (uint32_t i = 0; i < obj_spec_n_bins(o); i++) {
		as_val* va = (as_val*) as_record_get(a, names[i]);
		as_val* vb = (as_val*) as_record_get(b, names[i]);
		if (va == NULL || vb == NULL ||
				as_val_cmp(va, vb) != MSGPACK_COMPARE_EQUAL) {
			return false;
		}
	}
	return true;
}

#define DET_SPEC "first=@first_name, email=@email, tags=[3*@word], " \
	"age=@int(18,90), when=@date(\"2020-01-01\",\"2024-12-31\"), " \
	"loc=@geojson, attrs={3*@word:@pick(\"a\",\"b\")}"

START_TEST(determinism_same_seed_key)
{
	struct obj_spec_s o;
	as_bin_name names[7];
	ck_assert_int_eq(obj_spec_parse(&o, DET_SPEC), 0);
	ck_assert_int_eq(obj_spec_resolve_bin_names(&o, "b", names), 0);

	for (uint64_t key = 0; key < 200; key++) {
		as_record a, b;
		_populate_seeded(&o, (const as_bin_name*) names, &a, 42, key);
		_populate_seeded(&o, (const as_bin_name*) names, &b, 42, key);
		ck_assert(_records_equal(&o, (const as_bin_name*) names, &a, &b));
		as_record_destroy(&a);
		as_record_destroy(&b);
	}
	obj_spec_free(&o);
}
END_TEST

START_TEST(determinism_keys_and_seeds_differ)
{
	struct obj_spec_s o;
	as_bin_name names[7];
	ck_assert_int_eq(obj_spec_parse(&o, DET_SPEC), 0);
	ck_assert_int_eq(obj_spec_resolve_bin_names(&o, "b", names), 0);

	uint32_t same_key = 0, same_seed = 0;
	for (uint64_t key = 0; key < 100; key++) {
		as_record a, b, c;
		_populate_seeded(&o, (const as_bin_name*) names, &a, 42, key);
		_populate_seeded(&o, (const as_bin_name*) names, &b, 42, key + 1000);
		_populate_seeded(&o, (const as_bin_name*) names, &c, 43, key);
		same_key += _records_equal(&o, (const as_bin_name*) names, &a, &b);
		same_seed += _records_equal(&o, (const as_bin_name*) names, &a, &c);
		as_record_destroy(&a);
		as_record_destroy(&b);
		as_record_destroy(&c);
	}
	ck_assert_uint_eq(same_key, 0);
	ck_assert_uint_eq(same_seed, 0);
	obj_spec_free(&o);
}
END_TEST

START_TEST(distinct_emails)
{
	struct obj_spec_s o;
	as_bin_name names[1];
	char seen[100][128];
	uint32_t distinct = 0;
	ck_assert_int_eq(obj_spec_parse(&o, "e=@email"), 0);
	ck_assert_int_eq(obj_spec_resolve_bin_names(&o, "b", names), 0);

	for (uint64_t key = 0; key < 100; key++) {
		as_record r;
		_populate_seeded(&o, (const as_bin_name*) names, &r, 7, key);
		const char* e = as_record_get_str(&r, "e");
		bool dup = false;
		for (uint32_t i = 0; i < distinct; i++) {
			dup |= strcmp(seen[i], e) == 0;
		}
		if (!dup) {
			snprintf(seen[distinct++], sizeof(seen[0]), "%s", e);
		}
		as_record_destroy(&r);
	}
	ck_assert_uint_ge(distinct, 99);
	obj_spec_free(&o);
}
END_TEST

START_TEST(int_inclusive_bounds)
{
	struct obj_spec_s o;
	as_random random;
	uint32_t hist[3] = { 0, 0, 0 };
	seed_as_random(&random, 0x5EEDLU, __LINE__);
	ck_assert_int_eq(obj_spec_parse(&o, "@int(1,3)"), 0);
	for (uint32_t i = 0; i < N_SAMPLES; i++) {
		as_val* v = obj_spec_gen_bin_val(&o, 0, &random, 1.f);
		int64_t x = as_integer_get(as_integer_fromval(v));
		ck_assert(x >= 1 && x <= 3);
		hist[x - 1]++;
		as_val_destroy(v);
	}
	for (uint32_t i = 0; i < 3; i++) {
		ck_assert_uint_gt(hist[i], N_SAMPLES / 4);
	}
	obj_spec_free(&o);
}
END_TEST

START_TEST(weighted_pick_histogram)
{
	struct obj_spec_s o;
	as_random random;
	uint32_t a = 0;
	const uint32_t n = 100000;
	seed_as_random(&random, 0x5EEDLU, __LINE__);
	ck_assert_int_eq(obj_spec_parse(&o, "@pick(\"a\":90,\"b\":10)"), 0);
	for (uint32_t i = 0; i < n; i++) {
		as_val* v = obj_spec_gen_bin_val(&o, 0, &random, 1.f);
		a += strcmp(as_string_get(as_string_fromval(v)), "a") == 0;
		as_val_destroy(v);
	}
	double share = (double) a / n;
	ck_assert_msg(share > 0.88 && share < 0.92, "share of a was %f", share);
	obj_spec_free(&o);
}
END_TEST

START_TEST(uniform_pick_histogram)
{
	struct obj_spec_s o;
	as_random random;
	uint32_t hist[4] = { 0 };
	const uint32_t n = 100000;
	seed_as_random(&random, 0x5EEDLU, __LINE__);
	ck_assert_int_eq(obj_spec_parse(&o, "@pick(\"a\",\"b\",\"c\",\"d\")"), 0);
	for (uint32_t i = 0; i < n; i++) {
		as_val* v = obj_spec_gen_bin_val(&o, 0, &random, 1.f);
		hist[as_string_get(as_string_fromval(v))[0] - 'a']++;
		as_val_destroy(v);
	}
	for (uint32_t i = 0; i < 4; i++) {
		double share = (double) hist[i] / n;
		ck_assert_msg(share > 0.22 && share < 0.28, "share %u was %f", i, share);
	}
	obj_spec_free(&o);
}
END_TEST

START_TEST(date_fast_path_matches_strftime)
{
	struct obj_spec_s fast, slow;
	ck_assert_int_eq(obj_spec_parse(&fast, "@date(-2208988800,4102444799)"), 0);
	ck_assert_int_eq(obj_spec_parse(&slow, "@date(-2208988800,4102444799,\"%Y-%m-%d.\")"), 0);
	for (uint64_t i = 0; i < N_SAMPLES; i++) {
		as_random r1, r2;
		seed_as_random(&r1, 99, i);
		seed_as_random(&r2, 99, i);
		as_val* a = obj_spec_gen_bin_val(&fast, 0, &r1, 1.f);
		as_val* b = obj_spec_gen_bin_val(&slow, 0, &r2, 1.f);
		const char* sa = as_string_get(as_string_fromval(a));
		const char* sb = as_string_get(as_string_fromval(b));
		ck_assert_uint_eq(strlen(sb), 11);
		ck_assert_msg(strncmp(sa, sb, 10) == 0, "%s vs %s", sa, sb);
		as_val_destroy(a);
		as_val_destroy(b);
	}
	obj_spec_free(&fast);
	obj_spec_free(&slow);
}
END_TEST

START_TEST(dictionary_words_zero_copy)
{
	struct obj_spec_s o;
	as_random random;
	seed_as_random(&random, 0x5EEDLU, __LINE__);
	ck_assert_int_eq(obj_spec_parse(&o, "@city"), 0);
	const char* lo = SYNTH_DICT_CITY.pool;
	const char* hi = SYNTH_DICT_CITY.pool + SYNTH_DICT_CITY.off[SYNTH_DICT_CITY.n];
	for (uint32_t i = 0; i < 1000; i++) {
		as_val* v = obj_spec_gen_bin_val(&o, 0, &random, 1.f);
		const char* s = as_string_get(as_string_fromval(v));
		ck_assert(s >= lo && s < hi);
		as_val_destroy(v);
	}
	obj_spec_free(&o);
}
END_TEST

START_TEST(cardinality)
{
	struct obj_spec_s o;
	ck_assert_int_eq(obj_spec_parse(&o, "@state, @int(1,10), @now, "
				"@pick(\"a\",\"b\",\"c\"), @uuid, @date(0,863999), "
				"@fmt(\"#{state_abbr}-#{int(0,9)}\"), @date(0,863999,\"%H\")"), 0);
	ck_assert_uint_eq(synth_spec_cardinality(&obj_spec_bin_spec(&o, 0)->gen), 50);
	ck_assert_uint_eq(synth_spec_cardinality(&obj_spec_bin_spec(&o, 1)->gen), 10);
	ck_assert_uint_eq(synth_spec_cardinality(&obj_spec_bin_spec(&o, 2)->gen), 1);
	ck_assert_uint_eq(synth_spec_cardinality(&obj_spec_bin_spec(&o, 3)->gen), 3);
	ck_assert_uint_eq(synth_spec_cardinality(&obj_spec_bin_spec(&o, 4)->gen), UINT64_MAX);
	ck_assert_uint_eq(synth_spec_cardinality(&obj_spec_bin_spec(&o, 5)->gen), 10);
	ck_assert_uint_eq(synth_spec_cardinality(&obj_spec_bin_spec(&o, 6)->gen), 500);
	ck_assert_uint_eq(synth_spec_cardinality(&obj_spec_bin_spec(&o, 7)->gen), UINT64_MAX);
	obj_spec_free(&o);
}
END_TEST

START_TEST(fmt_placeholder_error_msg)
{
	synth_spec_t spec;
	const char* end;
	const char* msg;
	const char* loc;
	ck_assert_int_ne(synth_parse("@fmt(\"#{nope}\")", &end, &spec, &msg, &loc), 0);
	ck_assert_str_eq(msg, "@fmt placeholder: Unknown generator \"@nope\"");
}
END_TEST

START_TEST(map_keys_unique_near_cardinality)
{
	struct obj_spec_s o;
	as_random random;
	seed_as_random(&random, 0x5EEDLU, __LINE__);
	ck_assert_int_eq(obj_spec_parse(&o, "{50*@state_abbr:@int(1,9)}"), 0);
	for (uint32_t i = 0; i < 100; i++) {
		as_val* v = obj_spec_gen_bin_val(&o, 0, &random, 1.f);
		ck_assert_uint_eq(as_map_size(as_map_fromval(v)), 50);
		as_val_destroy(v);
	}
	obj_spec_free(&o);
}
END_TEST

START_TEST(geojson_is_geo_particle_in_collections)
{
	struct obj_spec_s o;
	as_random random;
	seed_as_random(&random, 0x5EEDLU, __LINE__);
	ck_assert_int_eq(obj_spec_parse(&o, "[2*@geojson], {@city:@geo_circle}"), 0);
	as_val* list = obj_spec_gen_bin_val(&o, 0, &random, 1.f);
	ck_assert_int_eq(as_list_get(as_list_fromval(list), 0)->type, AS_GEOJSON);
	ck_assert_int_eq(as_list_get(as_list_fromval(list), 1)->type, AS_GEOJSON);
	as_val_destroy(list);

	as_val* map = obj_spec_gen_bin_val(&o, 1, &random, 1.f);
	ck_assert_uint_eq(as_map_size(as_map_fromval(map)), 1);
	as_val_destroy(map);
	obj_spec_free(&o);
}
END_TEST

START_TEST(template_max_len_respected)
{
	struct obj_spec_s o;
	as_random random;
	seed_as_random(&random, 0x5EEDLU, __LINE__);
	ck_assert_int_eq(obj_spec_parse(&o, "@fmt(\"#{company} #{sentence} "
				"#{int(-9223372036854775808,9223372036854775807)} "
				"#{double(-1e15,1e15)} #{words(20)}\")"), 0);
	const synth_spec_t* spec = _gen_of(&o);
	for (uint32_t i = 0; i < N_SAMPLES; i++) {
		as_val* v = obj_spec_gen_bin_val(&o, 0, &random, 1.f);
		ck_assert_uint_le(as_string_len(as_string_fromval(v)), spec->max_len);
		as_val_destroy(v);
	}
	obj_spec_free(&o);
}
END_TEST


Suite*
synth_gen_suite(void)
{
	Suite* s;
	TCase* tc_samples;
	TCase* tc_behavior;

	s = suite_create("Synth Gen");

	tc_samples = tcase_create("Samples");
	tcase_add_checked_fixture(tc_samples, simple_setup, simple_teardown);
	tcase_set_timeout(tc_samples, 60);
	tcase_add_test(tc_samples, samples_first_name);
	tcase_add_test(tc_samples, samples_last_name);
	tcase_add_test(tc_samples, samples_full_name);
	tcase_add_test(tc_samples, samples_username);
	tcase_add_test(tc_samples, samples_email);
	tcase_add_test(tc_samples, samples_phone);
	tcase_add_test(tc_samples, samples_street);
	tcase_add_test(tc_samples, samples_city);
	tcase_add_test(tc_samples, samples_state);
	tcase_add_test(tc_samples, samples_state_abbr);
	tcase_add_test(tc_samples, samples_zip);
	tcase_add_test(tc_samples, samples_country);
	tcase_add_test(tc_samples, samples_country_code);
	tcase_add_test(tc_samples, samples_lat);
	tcase_add_test(tc_samples, samples_lat_range);
	tcase_add_test(tc_samples, samples_lon);
	tcase_add_test(tc_samples, samples_geojson);
	tcase_add_test(tc_samples, samples_geojson_bbox);
	tcase_add_test(tc_samples, samples_geo_circle);
	tcase_add_test(tc_samples, samples_geo_circle_bbox);
	tcase_add_test(tc_samples, samples_company);
	tcase_add_test(tc_samples, samples_job_title);
	tcase_add_test(tc_samples, samples_ipv4);
	tcase_add_test(tc_samples, samples_ipv6);
	tcase_add_test(tc_samples, samples_mac);
	tcase_add_test(tc_samples, samples_url);
	tcase_add_test(tc_samples, samples_domain);
	tcase_add_test(tc_samples, samples_uuid);
	tcase_add_test(tc_samples, samples_word);
	tcase_add_test(tc_samples, samples_words);
	tcase_add_test(tc_samples, samples_words_one);
	tcase_add_test(tc_samples, samples_sentence);
	tcase_add_test(tc_samples, samples_lorem);
	tcase_add_test(tc_samples, samples_lorem_one);
	tcase_add_test(tc_samples, samples_product);
	tcase_add_test(tc_samples, samples_color);
	tcase_add_test(tc_samples, samples_credit_card);
	tcase_add_test(tc_samples, samples_date);
	tcase_add_test(tc_samples, samples_date_old);
	tcase_add_test(tc_samples, samples_date_fmt);
	tcase_add_test(tc_samples, samples_timestamp);
	tcase_add_test(tc_samples, samples_now);
	tcase_add_test(tc_samples, samples_int);
	tcase_add_test(tc_samples, samples_int_full);
	tcase_add_test(tc_samples, samples_double);
	tcase_add_test(tc_samples, samples_pick);
	tcase_add_test(tc_samples, samples_pick_weighted);
	tcase_add_test(tc_samples, samples_fmt);
	tcase_add_test(tc_samples, samples_fmt_double);
	suite_add_tcase(s, tc_samples);

	tc_behavior = tcase_create("Behavior");
	tcase_add_checked_fixture(tc_behavior, simple_setup, simple_teardown);
	tcase_set_timeout(tc_behavior, 60);
	tcase_add_test(tc_behavior, determinism_same_seed_key);
	tcase_add_test(tc_behavior, determinism_keys_and_seeds_differ);
	tcase_add_test(tc_behavior, distinct_emails);
	tcase_add_test(tc_behavior, int_inclusive_bounds);
	tcase_add_test(tc_behavior, weighted_pick_histogram);
	tcase_add_test(tc_behavior, uniform_pick_histogram);
	tcase_add_test(tc_behavior, date_fast_path_matches_strftime);
	tcase_add_test(tc_behavior, dictionary_words_zero_copy);
	tcase_add_test(tc_behavior, cardinality);
	tcase_add_test(tc_behavior, fmt_placeholder_error_msg);
	tcase_add_test(tc_behavior, map_keys_unique_near_cardinality);
	tcase_add_test(tc_behavior, geojson_is_geo_particle_in_collections);
	tcase_add_test(tc_behavior, template_max_len_respected);
	suite_add_tcase(s, tc_behavior);

	return s;
}
