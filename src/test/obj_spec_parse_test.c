
#include <check.h>
#include <stdio.h>

#include <cyaml/cyaml.h>

#include <benchmark.h>
#include <object_spec.h>


#define TEST_SUITE_NAME "obj_spec"


static void 
simple_setup(void) 
{
	// redirect stderr to /dev/null
	freopen("/dev/null", "w", stderr);
}

static void
simple_teardown(void)
{
}


/*
 * Memory test cases
 */
START_TEST(test_double_free)
{
	struct obj_spec o;
	obj_spec_parse(&o, "I");
	obj_spec_free(&o);
	obj_spec_free(&o);
}
END_TEST

START_TEST(test_free_after_failed_init)
{
	struct obj_spec o;
	// technically o can be anything, so let's set it to valid to make sure
	// that obj_spec_init unsets it if it fails to initialize
	o.valid = true;
	obj_spec_parse(&o, "K");
	obj_spec_free(&o);
}
END_TEST

START_TEST(test_free_after_move)
{
	struct obj_spec o;
	struct obj_spec p;
	as_record rec;

	obj_spec_parse(&o, "[I,D,{S10:B20}]");
	obj_spec_move(&p, &o);
	obj_spec_free(&o);

	as_record_init(&rec, obj_spec_n_bins(&p));
	obj_spec_populate_bins(&p, &rec, as_random_instance(), "test");
	_dbg_obj_spec_assert_valid(&p, &rec, "test");
	obj_spec_free(&p);
}
END_TEST

START_TEST(test_shallow_copy)
{
	struct obj_spec o;
	struct obj_spec p;
	as_record rec;

	obj_spec_parse(&o, "[I,D,{S10:B20}]");
	obj_spec_shallow_copy(&p, &o);

	as_record_init(&rec, obj_spec_n_bins(&p));
	obj_spec_populate_bins(&p, &rec, as_random_instance(), "test");
	_dbg_obj_spec_assert_valid(&p, &rec, "test");
	obj_spec_free(&p);
	obj_spec_free(&o);
}
END_TEST

START_TEST(test_free_after_shallow_copy)
{
	struct obj_spec o;
	struct obj_spec p;
	as_record rec;

	obj_spec_parse(&o, "[I,D,{S10:B20}]");
	obj_spec_shallow_copy(&p, &o);
	obj_spec_free(&p);

	as_record_init(&rec, obj_spec_n_bins(&o));
	obj_spec_populate_bins(&o, &rec, as_random_instance(), "test");
	_dbg_obj_spec_assert_valid(&o, &rec, "test");
	obj_spec_free(&o);
}
END_TEST


/*
 * test-case definining macros
 */
#define DEFINE_TCASE_DIFF(test_name, obj_spec_str, expected_out_str) \
START_TEST(test_name ## _str_cmp) \
{ \
	struct obj_spec o; \
	char buf[sizeof(expected_out_str) + 1]; \
	ck_assert_int_eq(obj_spec_parse(&o, obj_spec_str), 0); \
	_dbg_sprint_obj_spec(&o, buf, sizeof(buf)); \
	\
	ck_assert_str_eq(buf, expected_out_str); \
	obj_spec_free(&o); \
} \
END_TEST \
START_TEST(test_name ## _valid) \
{ \
	struct obj_spec o; \
	as_random random; \
	as_record rec; \
	as_random_init(&random); \
	ck_assert_int_eq(obj_spec_parse(&o, obj_spec_str), 0); \
	as_record_init(&rec, obj_spec_n_bins(&o)); \
	ck_assert_int_eq(obj_spec_populate_bins(&o, &rec, &random, \
				"test"), 0); \
	_dbg_obj_spec_assert_valid(&o, &rec, "test"); \
	as_record_destroy(&rec); \
	obj_spec_free(&o); \
}

#define DEFINE_TCASE(test_name, obj_spec_str) \
	DEFINE_TCASE_DIFF(test_name, obj_spec_str, obj_spec_str)

#define DEFINE_FAILING_TCASE(test_name, obj_spec_str, msg) \
START_TEST(test_name) \
{ \
	struct obj_spec o; \
	ck_assert_msg(obj_spec_parse(&o, obj_spec_str) != 0, \
			"test should have failed on string \"" obj_spec_str "\": " msg); \
} \
END_TEST


/*
 * Simple test cases
 */

DEFINE_TCASE(test_I1, "I1");
DEFINE_TCASE(test_I2, "I2");
DEFINE_TCASE(test_I3, "I3");
DEFINE_TCASE(test_I4, "I4");
DEFINE_TCASE(test_I5, "I5");
DEFINE_TCASE(test_I6, "I6");
DEFINE_TCASE(test_I7, "I7");
DEFINE_TCASE(test_I8, "I8");
DEFINE_TCASE_DIFF(test_I, "I", "I4");
DEFINE_FAILING_TCASE(test_I0, "I0", "I0 is an invalid integer specifier");
DEFINE_FAILING_TCASE(test_I9, "I9", "I9 is an invalid integer specifier");
DEFINE_FAILING_TCASE(test_Ia, "Ia", "Ia is an invalid integer specifier");
DEFINE_FAILING_TCASE(test_Ineg1, "I-1", "I-1 is an invalid integer specifier");

DEFINE_TCASE(test_D, "D");
DEFINE_FAILING_TCASE(test_D1, "D1", "Decimal numbers cannot have numeric quantifiers");
DEFINE_FAILING_TCASE(test_D0, "D0", "Decimal numbers cannot have numeric quantifiers");

DEFINE_TCASE(test_S1, "S1");
DEFINE_TCASE(test_S10, "S10");
DEFINE_TCASE(test_S100, "S100");
DEFINE_TCASE(test_S123, "S123");
DEFINE_FAILING_TCASE(test_S_, "S", "strings need a length specifier");
DEFINE_FAILING_TCASE(test_S0, "S0", "0-length strings are not allowed");
DEFINE_FAILING_TCASE(test_Sneg1, "S-1", "negative-length strings are not allowed");
DEFINE_FAILING_TCASE(test_S4294967296, "S4294967296", "this is beyong the max "
		"allowed string length (2^32 - 1)");

DEFINE_TCASE(test_B1, "B1");
DEFINE_TCASE(test_B10, "B10");
DEFINE_TCASE(test_B100, "B100");
DEFINE_TCASE(test_B123, "B123");
DEFINE_FAILING_TCASE(test_B_, "B", "binary data need a length specifier");
DEFINE_FAILING_TCASE(test_B0, "B0", "0-length binary data are not allowed");
DEFINE_FAILING_TCASE(test_Bneg1, "B-1", "negative-length binary data are not allowed");
DEFINE_FAILING_TCASE(test_B4294967296, "B4294967296", "this is beyond the max "
		"allowed binary data length (2^32 - 1)");


/*
 * List test cases
 */
DEFINE_TCASE(test_singleton_list, "[I3]");
DEFINE_TCASE(test_pair_list, "[I3,S5]");
DEFINE_TCASE(test_long_list, "[B10,D,S22,I7,I8,S30,B110,I2,I4]");
DEFINE_TCASE(test_repeated_list, "[D,D,D,D,D,B10,B10,B10,I4,I4,I4,I4]");
DEFINE_FAILING_TCASE(test_empty_list, "[]", "empty list not allowed");
DEFINE_FAILING_TCASE(test_unterminated_list, "[S10,I3", "unterminated list");
DEFINE_FAILING_TCASE(test_unterminated_list_v2, "[S10,I3,", "unterminated list");
DEFINE_FAILING_TCASE(test_unopened_list, "I3]", "unopened list");


/*
 * Map test cases
 */
DEFINE_TCASE(test_map_II, "{I1:I2}");
DEFINE_TCASE(test_map_ID, "{I1:D}");
DEFINE_TCASE(test_map_IS, "{I1:S4}");
DEFINE_TCASE(test_map_IB, "{I1:B5}");
DEFINE_TCASE(test_map_DI, "{D:I2}");
DEFINE_TCASE(test_map_DD, "{D:D}");
DEFINE_TCASE(test_map_DS, "{D:S4}");
DEFINE_TCASE(test_map_DB, "{D:B5}");
DEFINE_TCASE(test_map_SI, "{S2:I2}");
DEFINE_TCASE(test_map_SD, "{S2:D}");
DEFINE_TCASE(test_map_SS, "{S2:S4}");
DEFINE_TCASE(test_map_SB, "{S2:B5}");
DEFINE_TCASE(test_map_BI, "{B6:I2}");
DEFINE_TCASE(test_map_BD, "{B6:D}");
DEFINE_TCASE(test_map_BS, "{B6:S4}");
DEFINE_TCASE(test_map_BB, "{B6:B5}");

DEFINE_FAILING_TCASE(test_empty_map, "{}", "empty map not allowed");
DEFINE_FAILING_TCASE(test_map_with_no_value, "{I1}", "maps need key and value");
DEFINE_FAILING_TCASE(test_map_multiple_keys, "{I3:I5:I7}", "map cannot have multiple keys");
DEFINE_FAILING_TCASE(test_unterminated_map, "{", "unterminated map");
DEFINE_FAILING_TCASE(test_unterminated_map_v2, "{I3:", "unterminated map");
DEFINE_FAILING_TCASE(test_unterminated_map_v3, "{I3:S10", "unterminated map");
DEFINE_FAILING_TCASE(test_unopened_map, "I3:S10}", "unopened map");
DEFINE_FAILING_TCASE(test_unopened_map_v2, ":S10}", "unopened map");
DEFINE_FAILING_TCASE(test_unopened_map_v3, "S10}", "unopened map");
DEFINE_FAILING_TCASE(test_unopened_map_v4, "}", "unopened map");


/*
 * Multiple bins test cases
 */
DEFINE_TCASE(test_two_bins, "I1,I2");
DEFINE_TCASE(test_three_bins, "I1,I2,I3");
DEFINE_TCASE(test_mixed_bins, "S12,I6,B20");
DEFINE_TCASE(test_many_bins, "I1,I2,I3,I4,S1,S2,S3,S4,D,B1,B2,B3,B4");
DEFINE_TCASE(test_repeated_bins, "I1,I1,D,D,S10,S10,B5,B5");
DEFINE_FAILING_TCASE(test_no_commas, "I1D", "need commas separating tokens");
DEFINE_FAILING_TCASE(test_spaces, "I1 D", "need commas separating tokens");


/*
 * Nested lists/maps test cases
 */
DEFINE_TCASE(test_map_to_list, "{I5:[S10,B20,D]}");
DEFINE_TCASE(test_list_of_maps, "[{I5:I1},{S10:B20}]");
DEFINE_TCASE(test_mixed_list_of_maps, "[{D:I3},S10,{S20:B1}]");
DEFINE_TCASE(test_nested_lists,
		"[[S10,I4,[B10,B5,D],D],S20,[B100,I7,D],[[I3],[I4,I5,I6]]]");
DEFINE_TCASE(test_nested_maps, "{I5:{S30:{D:{B123:I1}}}}");
DEFINE_TCASE(test_nested_mix,
		"[{S10:D},[I3,{S10:B20},{I3:{D:[I1,I2]}},B10],{B32:[I3,I5,{I7:D}]}]");
DEFINE_FAILING_TCASE(test_map_key_list, "{[I3,I5]:I6}", "map key cannot be a list");
DEFINE_FAILING_TCASE(test_map_key_map, "{{S20:I4}:I1}", "map key cannot be a map");
DEFINE_FAILING_TCASE(test_map_to_undeclared_list, "{I4:I1,I2}", "map value must be single type");


/*
 * Multipliers test cases
 */
DEFINE_TCASE(test_mult_I, "2*I3");
DEFINE_TCASE(test_mult_D, "5*D");
DEFINE_TCASE(test_mult_S, "3*S10");
DEFINE_TCASE(test_mult_B, "10*B3");
DEFINE_TCASE(test_mult_list, "8*[I1,I2,S3]");
DEFINE_TCASE(test_mult_map, "20*{I6:S20}");
DEFINE_TCASE(test_mult_within_list, "2*[5*I1,3*I2,100*S3]");
DEFINE_TCASE(test_mult_within_map, "30*{I3:[5*S20]}");
DEFINE_TCASE(test_mult_map_key_I, "{3*I2:S3}");
DEFINE_TCASE(test_mult_map_key_D, "{5*D:S3}");
DEFINE_TCASE(test_mult_map_key_S, "{2*S2:S3}");
DEFINE_TCASE(test_mult_map_key_B, "{700*B5:S3}");
DEFINE_FAILING_TCASE(test_mult_no_star, "3I2", "multipliers must be followed with a '*'");
DEFINE_FAILING_TCASE(test_mult_map_val_I, "{I1:3*I2}",
		"no multipliers on map values allowed");
DEFINE_FAILING_TCASE(test_mult_map_val_D, "{I2:5*D}",
		"no multipliers on map values allowed");
DEFINE_FAILING_TCASE(test_mult_map_val_S, "{I3:2*S2}",
		"no multipliers on map values allowed");
DEFINE_FAILING_TCASE(test_mult_map_val_B, "{I4:7*B5}",
		"no multipliers on map values allowed");
DEFINE_FAILING_TCASE(test_mult_map_val_L, "{I4:7*[S3,B6,D]}",
		"no multipliers on map values allowed");
DEFINE_FAILING_TCASE(test_mult_map_val_M, "{I4:7*{B5:I4}}",
		"no multipliers on map values allowed");
DEFINE_FAILING_TCASE(test_mult_list_overflow, "[4294967296*I3]",
		"multiplier >= 2^32 overflows the mult field");
DEFINE_FAILING_TCASE(test_mult_list_overflow2, "[60000000000000000*I3]",
		"multiplier >= 2^32 overflows the mult field");
DEFINE_FAILING_TCASE(test_mult_list_too_many_elements, "[3000000000*I4,3000000000*S10]",
		"more than 2^32 elements in a single list is not allowed");


/*
 * Bin name test cases
 */
#define DEFINE_BIN_NAME_OK(test_name, n_bins, bin_name) \
START_TEST(test_name) \
{ \
	struct obj_spec o; \
	as_random random; \
	as_record rec; \
	as_random_init(&random); \
	ck_assert_int_eq(obj_spec_parse(&o, #n_bins "*I1"), 0); \
	as_record_init(&rec, obj_spec_n_bins(&o)); \
	ck_assert_int_eq(obj_spec_populate_bins(&o, &rec, &random, \
				bin_name), 0); \
	as_record_destroy(&rec); \
	obj_spec_free(&o); \
} \
END_TEST

#define DEFINE_BIN_NAME_TOO_LARGE(test_name, n_bins, bin_name) \
START_TEST(test_name) \
{ \
	struct obj_spec o; \
	as_random random; \
	as_record rec; \
	as_random_init(&random); \
	ck_assert_int_eq(obj_spec_parse(&o, #n_bins "*I1"), 0); \
	as_record_init(&rec, obj_spec_n_bins(&o)); \
	ck_assert_int_ne(obj_spec_populate_bins(&o, &rec, &random, \
				bin_name), 0); \
	as_record_destroy(&rec); \
} \
END_TEST


DEFINE_BIN_NAME_OK(bin_name_single_ok, 1, "abcdefghijklmno");
DEFINE_BIN_NAME_TOO_LARGE(bin_name_single_too_large, 1, "abcdefghijklmnop");
DEFINE_BIN_NAME_OK(bin_name_1_dig_ok, 3, "abcdefghijklm");
DEFINE_BIN_NAME_TOO_LARGE(bin_name_1_dig_too_large, 9, "abcdefghijklmn");
DEFINE_BIN_NAME_OK(bin_name_2_dig_ok, 10, "abcdefghijkl");
DEFINE_BIN_NAME_TOO_LARGE(bin_name_2_dig_too_large, 99, "abcdefghijklm");
DEFINE_BIN_NAME_OK(bin_name_3_dig_ok, 123, "abcdefghijl");
DEFINE_BIN_NAME_TOO_LARGE(bin_name_3_dig_too_large, 909, "abcdefghijkl");
DEFINE_BIN_NAME_OK(bin_name_4_dig_ok, 1024, "abcdefghij");
DEFINE_BIN_NAME_TOO_LARGE(bin_name_4_dig_too_large, 8192, "abcdefghijk");


Suite*
obj_spec_suite(void)
{
	Suite* s;
	TCase* tc_memory;
	TCase* tc_simple;
	TCase* tc_list;
	TCase* tc_map;
	TCase* tc_multi_bins;
	TCase* tc_nested;
	TCase* tc_multipliers;
	TCase* tc_bin_names;

	s = suite_create("Object Spec");

	tc_memory = tcase_create("Memory");
	tcase_add_checked_fixture(tc_memory, simple_setup, simple_teardown);
	tcase_add_test(tc_memory, test_double_free);
	tcase_add_test(tc_memory, test_free_after_failed_init);
	tcase_add_test(tc_memory, test_free_after_move);
	tcase_add_test(tc_memory, test_shallow_copy);
	tcase_add_test(tc_memory, test_free_after_shallow_copy);
	suite_add_tcase(s, tc_memory);

	tc_simple = tcase_create("Simple");
	tcase_add_checked_fixture(tc_simple, simple_setup, simple_teardown);
	tcase_add_test(tc_simple, test_I1_str_cmp);
	tcase_add_test(tc_simple, test_I2_str_cmp);
	tcase_add_test(tc_simple, test_I3_str_cmp);
	tcase_add_test(tc_simple, test_I4_str_cmp);
	tcase_add_test(tc_simple, test_I5_str_cmp);
	tcase_add_test(tc_simple, test_I6_str_cmp);
	tcase_add_test(tc_simple, test_I7_str_cmp);
	tcase_add_test(tc_simple, test_I8_str_cmp);
	tcase_add_test(tc_simple, test_I_str_cmp);
	tcase_add_test(tc_simple, test_I1_valid);
	tcase_add_test(tc_simple, test_I2_valid);
	tcase_add_test(tc_simple, test_I3_valid);
	tcase_add_test(tc_simple, test_I4_valid);
	tcase_add_test(tc_simple, test_I5_valid);
	tcase_add_test(tc_simple, test_I6_valid);
	tcase_add_test(tc_simple, test_I7_valid);
	tcase_add_test(tc_simple, test_I8_valid);
	tcase_add_test(tc_simple, test_I_valid);
	tcase_add_test(tc_simple, test_I0);
	tcase_add_test(tc_simple, test_I9);
	tcase_add_test(tc_simple, test_Ia);
	tcase_add_test(tc_simple, test_Ineg1);

	tcase_add_test(tc_simple, test_D_str_cmp);
	tcase_add_test(tc_simple, test_D_valid);
	tcase_add_test(tc_simple, test_D1);
	tcase_add_test(tc_simple, test_D0);

	tcase_add_test(tc_simple, test_S1_str_cmp);
	tcase_add_test(tc_simple, test_S10_str_cmp);
	tcase_add_test(tc_simple, test_S100_str_cmp);
	tcase_add_test(tc_simple, test_S123_str_cmp);
	tcase_add_test(tc_simple, test_S1_valid);
	tcase_add_test(tc_simple, test_S10_valid);
	tcase_add_test(tc_simple, test_S100_valid);
	tcase_add_test(tc_simple, test_S123_valid);
	tcase_add_test(tc_simple, test_S_);
	tcase_add_test(tc_simple, test_S0);
	tcase_add_test(tc_simple, test_Sneg1);
	tcase_add_test(tc_simple, test_S4294967296);

	tcase_add_test(tc_simple, test_B1_str_cmp);
	tcase_add_test(tc_simple, test_B10_str_cmp);
	tcase_add_test(tc_simple, test_B100_str_cmp);
	tcase_add_test(tc_simple, test_B123_str_cmp);
	tcase_add_test(tc_simple, test_B1_valid);
	tcase_add_test(tc_simple, test_B10_valid);
	tcase_add_test(tc_simple, test_B100_valid);
	tcase_add_test(tc_simple, test_B123_valid);
	tcase_add_test(tc_simple, test_B_);
	tcase_add_test(tc_simple, test_B0);
	tcase_add_test(tc_simple, test_Bneg1);
	tcase_add_test(tc_simple, test_B4294967296);
	suite_add_tcase(s, tc_simple);

	tc_list = tcase_create("List");
	tcase_add_checked_fixture(tc_list, simple_setup, simple_teardown);
	tcase_add_test(tc_list, test_singleton_list_str_cmp);
	tcase_add_test(tc_list, test_pair_list_str_cmp);
	tcase_add_test(tc_list, test_long_list_str_cmp);
	tcase_add_test(tc_list, test_repeated_list_str_cmp);
	tcase_add_test(tc_list, test_singleton_list_valid);
	tcase_add_test(tc_list, test_pair_list_valid);
	tcase_add_test(tc_list, test_long_list_valid);
	tcase_add_test(tc_list, test_repeated_list_valid);
	tcase_add_test(tc_list, test_empty_list);
	tcase_add_test(tc_list, test_unterminated_list);
	tcase_add_test(tc_list, test_unterminated_list_v2);
	tcase_add_test(tc_list, test_unopened_list);
	suite_add_tcase(s, tc_list);

	tc_map = tcase_create("Map");
	tcase_add_checked_fixture(tc_map, simple_setup, simple_teardown);
	tcase_add_test(tc_map, test_map_II_str_cmp);
	tcase_add_test(tc_map, test_map_ID_str_cmp);
	tcase_add_test(tc_map, test_map_IS_str_cmp);
	tcase_add_test(tc_map, test_map_IB_str_cmp);
	tcase_add_test(tc_map, test_map_DI_str_cmp);
	tcase_add_test(tc_map, test_map_DD_str_cmp);
	tcase_add_test(tc_map, test_map_DS_str_cmp);
	tcase_add_test(tc_map, test_map_DB_str_cmp);
	tcase_add_test(tc_map, test_map_SI_str_cmp);
	tcase_add_test(tc_map, test_map_SD_str_cmp);
	tcase_add_test(tc_map, test_map_SS_str_cmp);
	tcase_add_test(tc_map, test_map_SB_str_cmp);
	tcase_add_test(tc_map, test_map_BI_str_cmp);
	tcase_add_test(tc_map, test_map_BD_str_cmp);
	tcase_add_test(tc_map, test_map_BS_str_cmp);
	tcase_add_test(tc_map, test_map_BB_str_cmp);
	tcase_add_test(tc_map, test_map_II_valid);
	tcase_add_test(tc_map, test_map_ID_valid);
	tcase_add_test(tc_map, test_map_IS_valid);
	tcase_add_test(tc_map, test_map_IB_valid);
	tcase_add_test(tc_map, test_map_DI_valid);
	tcase_add_test(tc_map, test_map_DD_valid);
	tcase_add_test(tc_map, test_map_DS_valid);
	tcase_add_test(tc_map, test_map_DB_valid);
	tcase_add_test(tc_map, test_map_SI_valid);
	tcase_add_test(tc_map, test_map_SD_valid);
	tcase_add_test(tc_map, test_map_SS_valid);
	tcase_add_test(tc_map, test_map_SB_valid);
	tcase_add_test(tc_map, test_map_BI_valid);
	tcase_add_test(tc_map, test_map_BD_valid);
	tcase_add_test(tc_map, test_map_BS_valid);
	tcase_add_test(tc_map, test_map_BB_valid);
	tcase_add_test(tc_map, test_empty_map);
	tcase_add_test(tc_map, test_map_with_no_value);
	tcase_add_test(tc_map, test_map_multiple_keys);
	tcase_add_test(tc_map, test_unterminated_map);
	tcase_add_test(tc_map, test_unterminated_map_v2);
	tcase_add_test(tc_map, test_unterminated_map_v3);
	tcase_add_test(tc_map, test_unopened_map);
	tcase_add_test(tc_map, test_unopened_map_v2);
	tcase_add_test(tc_map, test_unopened_map_v3);
	tcase_add_test(tc_map, test_unopened_map_v4);
	suite_add_tcase(s, tc_map);

	tc_multi_bins = tcase_create("Multiple Bins");
	tcase_add_checked_fixture(tc_multi_bins, simple_setup, simple_teardown);
	tcase_add_test(tc_multi_bins, test_two_bins_str_cmp);
	tcase_add_test(tc_multi_bins, test_three_bins_str_cmp);
	tcase_add_test(tc_multi_bins, test_mixed_bins_str_cmp);
	tcase_add_test(tc_multi_bins, test_many_bins_str_cmp);
	tcase_add_test(tc_multi_bins, test_repeated_bins_str_cmp);
	tcase_add_test(tc_multi_bins, test_two_bins_valid);
	tcase_add_test(tc_multi_bins, test_three_bins_valid);
	tcase_add_test(tc_multi_bins, test_mixed_bins_valid);
	tcase_add_test(tc_multi_bins, test_many_bins_valid);
	tcase_add_test(tc_multi_bins, test_repeated_bins_valid);
	tcase_add_test(tc_multi_bins, test_no_commas);
	tcase_add_test(tc_multi_bins, test_spaces);
	suite_add_tcase(s, tc_multi_bins);

	tc_nested = tcase_create("Nested lists/maps");
	tcase_add_checked_fixture(tc_nested, simple_setup, simple_teardown);
	tcase_add_test(tc_nested, test_map_to_list_str_cmp);
	tcase_add_test(tc_nested, test_list_of_maps_str_cmp);
	tcase_add_test(tc_nested, test_mixed_list_of_maps_str_cmp);
	tcase_add_test(tc_nested, test_nested_lists_str_cmp);
	tcase_add_test(tc_nested, test_nested_maps_str_cmp);
	tcase_add_test(tc_nested, test_nested_mix_str_cmp);
	tcase_add_test(tc_nested, test_map_to_list_valid);
	tcase_add_test(tc_nested, test_list_of_maps_valid);
	tcase_add_test(tc_nested, test_mixed_list_of_maps_valid);
	tcase_add_test(tc_nested, test_nested_lists_valid);
	tcase_add_test(tc_nested, test_nested_maps_valid);
	tcase_add_test(tc_nested, test_nested_mix_valid);
	tcase_add_test(tc_nested, test_map_key_list);
	tcase_add_test(tc_nested, test_map_key_map);
	tcase_add_test(tc_nested, test_map_to_undeclared_list);
	suite_add_tcase(s, tc_nested);

	tc_multipliers = tcase_create("Multipliers");
	tcase_add_checked_fixture(tc_multipliers, simple_setup, simple_teardown);
	tcase_add_test(tc_multipliers, test_mult_I_str_cmp);
	tcase_add_test(tc_multipliers, test_mult_D_str_cmp);
	tcase_add_test(tc_multipliers, test_mult_S_str_cmp);
	tcase_add_test(tc_multipliers, test_mult_B_str_cmp);
	tcase_add_test(tc_multipliers, test_mult_list_str_cmp);
	tcase_add_test(tc_multipliers, test_mult_map_str_cmp);
	tcase_add_test(tc_multipliers, test_mult_within_list_str_cmp);
	tcase_add_test(tc_multipliers, test_mult_within_map_str_cmp);
	tcase_add_test(tc_multipliers, test_mult_map_key_I_str_cmp);
	tcase_add_test(tc_multipliers, test_mult_map_key_D_str_cmp);
	tcase_add_test(tc_multipliers, test_mult_map_key_S_str_cmp);
	tcase_add_test(tc_multipliers, test_mult_map_key_B_str_cmp);
	tcase_add_test(tc_multipliers, test_mult_I_valid);
	tcase_add_test(tc_multipliers, test_mult_D_valid);
	tcase_add_test(tc_multipliers, test_mult_S_valid);
	tcase_add_test(tc_multipliers, test_mult_B_valid);
	tcase_add_test(tc_multipliers, test_mult_list_valid);
	tcase_add_test(tc_multipliers, test_mult_map_valid);
	tcase_add_test(tc_multipliers, test_mult_within_list_valid);
	tcase_add_test(tc_multipliers, test_mult_within_map_valid);
	tcase_add_test(tc_multipliers, test_mult_map_key_I_valid);
	tcase_add_test(tc_multipliers, test_mult_map_key_D_valid);
	tcase_add_test(tc_multipliers, test_mult_map_key_S_valid);
	tcase_add_test(tc_multipliers, test_mult_map_key_B_valid);
	tcase_add_test(tc_multipliers, test_mult_no_star);
	tcase_add_test(tc_multipliers, test_mult_map_val_I);
	tcase_add_test(tc_multipliers, test_mult_map_val_D);
	tcase_add_test(tc_multipliers, test_mult_map_val_S);
	tcase_add_test(tc_multipliers, test_mult_map_val_B);
	tcase_add_test(tc_multipliers, test_mult_map_val_L);
	tcase_add_test(tc_multipliers, test_mult_map_val_M);
	tcase_add_test(tc_multipliers, test_mult_list_overflow);
	tcase_add_test(tc_multipliers, test_mult_list_overflow2);
	tcase_add_test(tc_multipliers, test_mult_list_too_many_elements);
	suite_add_tcase(s, tc_multipliers);

	tc_bin_names = tcase_create("Bin names");
	tcase_add_checked_fixture(tc_bin_names, simple_setup, simple_teardown);
	tcase_add_test(tc_bin_names, bin_name_single_ok);
	tcase_add_test(tc_bin_names, bin_name_single_too_large);
	tcase_add_test(tc_bin_names, bin_name_1_dig_ok);
	tcase_add_test(tc_bin_names, bin_name_1_dig_too_large);
	tcase_add_test(tc_bin_names, bin_name_2_dig_ok);
	tcase_add_test(tc_bin_names, bin_name_2_dig_too_large);
	tcase_add_test(tc_bin_names, bin_name_3_dig_ok);
	tcase_add_test(tc_bin_names, bin_name_3_dig_too_large);
	tcase_add_test(tc_bin_names, bin_name_4_dig_ok);
	tcase_add_test(tc_bin_names, bin_name_4_dig_too_large);
	suite_add_tcase(s, tc_bin_names);

	return s;
}

