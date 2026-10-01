
#include <check.h>
#include <stdio.h>

#include <aerospike/as_msgpack.h>
#include <cyaml/cyaml.h>

#include <citrusleaf/cf_clock.h>

#include <benchmark.h>
#include <common.h>
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
	struct obj_spec_s o;
	obj_spec_parse(&o, "I");
	obj_spec_free(&o);
	obj_spec_free(&o);
}
END_TEST

START_TEST(test_free_after_failed_init)
{
	struct obj_spec_s o;
	// technically o can be anything, so let's set it to valid to make sure
	// that obj_spec_init unsets it if it fails to initialize
	o.valid = true;
	obj_spec_parse(&o, "K");
	obj_spec_free(&o);
}
END_TEST

START_TEST(test_free_after_move)
{
	struct obj_spec_s o;
	struct obj_spec_s p;
	as_record rec;

	obj_spec_parse(&o, "[I,D,{S10:B20}]");
	obj_spec_move(&p, &o);
	obj_spec_free(&o);

	as_record_init(&rec, obj_spec_n_bins(&p));
	obj_spec_populate_bins(&p, &rec, as_random_instance(), "test", NULL, 0,
			1.f);
	_dbg_obj_spec_assert_valid(&p, &rec, NULL, 0, "test");
	as_record_destroy(&rec);
	obj_spec_free(&p);
}
END_TEST

START_TEST(test_shallow_copy)
{
	struct obj_spec_s o;
	struct obj_spec_s p;
	as_record rec;

	obj_spec_parse(&o, "[I,D,{S10:B20}]");
	obj_spec_shallow_copy(&p, &o);

	as_record_init(&rec, obj_spec_n_bins(&p));
	obj_spec_populate_bins(&p, &rec, as_random_instance(), "test", NULL, 0,
			1.f);
	_dbg_obj_spec_assert_valid(&p, &rec, NULL, 0, "test");
	as_record_destroy(&rec);
	obj_spec_free(&p);
	obj_spec_free(&o);
}
END_TEST

START_TEST(test_free_after_shallow_copy)
{
	struct obj_spec_s o;
	struct obj_spec_s p;
	as_record rec;

	obj_spec_parse(&o, "[I,D,{S10:B20}]");
	obj_spec_shallow_copy(&p, &o);
	obj_spec_free(&p);

	as_record_init(&rec, obj_spec_n_bins(&o));
	obj_spec_populate_bins(&o, &rec, as_random_instance(), "test", NULL, 0,
			1.f);
	_dbg_obj_spec_assert_valid(&o, &rec, NULL, 0, "test");
	as_record_destroy(&rec);
	obj_spec_free(&o);
}
END_TEST

START_TEST(test_not_enough_bins)
{
	struct obj_spec_s o;
	as_record rec;

	obj_spec_parse(&o, "I,D,{S10:B20}");

	as_record_init(&rec, 2);
	ck_assert_int_ne(0, obj_spec_populate_bins(&o, &rec, as_random_instance(),
				"test", NULL, 0, 1.f));
	as_record_destroy(&rec);
	obj_spec_free(&o);
}
END_TEST

START_TEST(test_not_enough_bins_write_bins)
{
	struct obj_spec_s o;
	as_record rec;

	obj_spec_parse(&o, "I,D,{S10:B20}");
	uint32_t bins[] = { 0, 2 };

	as_record_init(&rec, 1);
	ck_assert_int_ne(0, obj_spec_populate_bins(&o, &rec, as_random_instance(),
				"test", bins, 2, 1.f));
	as_record_destroy(&rec);
	obj_spec_free(&o);
}
END_TEST

START_TEST(test_bins_already_occupied)
{
	struct obj_spec_s o;
	as_record rec;

	obj_spec_parse(&o, "I,D,{S10:B20}");

	as_record_init(&rec, 3);
	as_integer i;
	as_integer_init(&i, 0);
	as_bin_name bin_name = "extra_bin";
	as_record_set(&rec, bin_name, (as_bin_value*) &i);
	ck_assert_int_ne(0, obj_spec_populate_bins(&o, &rec, as_random_instance(),
				"test", NULL, 0, 1.f));
	as_record_destroy(&rec);
	obj_spec_free(&o);
}
END_TEST

START_TEST(test_bins_already_occupied_write_bins)
{
	struct obj_spec_s o;
	as_record rec;

	obj_spec_parse(&o, "I,D,{S10:B20}");
	uint32_t bins[] = { 0, 2 };

	as_record_init(&rec, 2);
	as_bin_name bin_name = "extra_bin";
	as_record_set_int64(&rec, bin_name, 1);
	ck_assert_int_ne(0, obj_spec_populate_bins(&o, &rec, as_random_instance(),
				"test", bins, 2, 1.f));
	as_record_destroy(&rec);
	obj_spec_free(&o);
}
END_TEST


static void
_test_str_cmp(const char* obj_spec_str,
		const char* expected_out_str, uint64_t expected_out_str_len,
		uint32_t* write_bins, uint32_t n_write_bins)
{
	struct obj_spec_s o;
	char buf[expected_out_str_len + 1];
	ck_assert_int_eq(obj_spec_parse(&o, obj_spec_str), 0);
	_dbg_sprint_obj_spec(&o, buf, sizeof(buf));

	ck_assert_str_eq(buf, expected_out_str);
	obj_spec_free(&o);
}

static void
_test_valid(const char* obj_spec_str,
		const char* expected_out_str, uint32_t* write_bins,
		uint32_t n_write_bins)
{
	struct obj_spec_s o;
	as_random random, random2;
	as_record rec;
	as_val* val;
	as_list* list;

	as_random_init(&random);
	memcpy(&random2, &random, sizeof(as_random));

	ck_assert_int_eq(obj_spec_parse(&o, obj_spec_str), 0);
	as_record_init(&rec, obj_spec_n_bins(&o));
	ck_assert_int_eq(obj_spec_populate_bins(&o, &rec, &random,
				"test", write_bins, n_write_bins, 1.f), 0);
	_dbg_obj_spec_assert_valid(&o, &rec, write_bins, n_write_bins, "test");

	val = obj_spec_gen_value(&o, &random2, write_bins, n_write_bins);
	ck_assert_ptr_ne(val, NULL);
	list = as_list_fromval(val);
	ck_assert_ptr_ne(list, NULL);
	if (write_bins != NULL) {
		ck_assert_int_eq(as_list_size(list), n_write_bins);
	}

	for (uint32_t i = 0; i < as_list_size(list); i++) {
		as_bin_name bin;
		obj_spec_bin_name(&o, (write_bins ? ((uint32_t*) write_bins)[i] : i),
				"test", bin);
		ck_assert(as_val_cmp(as_list_get(list, i),
					(as_val*) as_record_get(&rec, bin)) == MSGPACK_COMPARE_EQUAL);
	}
	as_val_destroy(val);
	as_record_destroy(&rec);
	obj_spec_free(&o);
}

/*
 * test-case definining macros
 */
#define DEFINE_TCASE_DIFF_WRITE_BINS(test_name, obj_spec_str, \
		expected_out_str, write_bins, n_write_bins) \
START_TEST(test_name ## _str_cmp) \
{ \
	_test_str_cmp(obj_spec_str, expected_out_str, sizeof(expected_out_str), \
			write_bins, n_write_bins); \
} \
END_TEST \
START_TEST(test_name ## _valid) \
{ \
	_test_valid(obj_spec_str, expected_out_str, write_bins, n_write_bins); \
}\
END_TEST

#define DEFINE_TCASE_DIFF(test_name, obj_spec_str, expected_out_str) \
	DEFINE_TCASE_DIFF_WRITE_BINS(test_name, obj_spec_str, expected_out_str, \
			NULL, 0)

#define DEFINE_TCASE_WRITE_BINS(test_name, obj_spec_str, write_bins, \
		n_write_bins) \
	DEFINE_TCASE_DIFF_WRITE_BINS(test_name, obj_spec_str, obj_spec_str, \
			write_bins, n_write_bins)

#define DEFINE_TCASE(test_name, obj_spec_str) \
	DEFINE_TCASE_DIFF(test_name, obj_spec_str, obj_spec_str)

#define DEFINE_FAILING_TCASE(test_name, obj_spec_str, msg) \
START_TEST(test_name) \
{ \
	struct obj_spec_s o; \
	ck_assert_msg(obj_spec_parse(&o, obj_spec_str) != 0, \
			"test should have failed on string \"" obj_spec_str "\": " msg); \
} \
END_TEST


/*
 * Simple test cases
 */

DEFINE_TCASE(test_b, "b");
DEFINE_FAILING_TCASE(test_b1, "b1", "Booleans cannot have numeric quantifiers");
DEFINE_FAILING_TCASE(test_b0, "b0", "Booleans cannot have numeric quantifiers");

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
DEFINE_FAILING_TCASE(test_D1, "D1", "Floating point numbers cannot have numeric quantifiers");
DEFINE_FAILING_TCASE(test_D0, "D0", "Floating point numbers cannot have numeric quantifiers");

DEFINE_TCASE(test_S1, "S1");
DEFINE_TCASE(test_S10, "S10");
DEFINE_TCASE(test_S100, "S100");
DEFINE_TCASE(test_S123, "S123");
DEFINE_TCASE(test_S0, "S0");
DEFINE_FAILING_TCASE(test_S_, "S", "strings need a length specifier");
DEFINE_FAILING_TCASE(test_Sneg1, "S-1", "negative-length strings are not allowed");
DEFINE_FAILING_TCASE(test_S4294967296, "S4294967296", "this is beyong the max "
		"allowed string length (2^32 - 1)");

DEFINE_TCASE(test_B1, "B1");
DEFINE_TCASE(test_B10, "B10");
DEFINE_TCASE(test_B100, "B100");
DEFINE_TCASE(test_B123, "B123");
DEFINE_TCASE(test_B0, "B0");
DEFINE_FAILING_TCASE(test_B_, "B", "binary data need a length specifier");
DEFINE_FAILING_TCASE(test_Bneg1, "B-1", "negative-length binary data are not allowed");
DEFINE_FAILING_TCASE(test_B4294967296, "B4294967296", "this is beyond the max "
		"allowed binary data length (2^32 - 1)");

/*
 * Constants test cases
 */

DEFINE_TCASE(test_const_b_true,  "true");
DEFINE_TCASE(test_const_b_false, "false");
DEFINE_TCASE_DIFF(test_const_b_True,  "True", "true");
DEFINE_TCASE_DIFF(test_const_b_TRUE,  "TRUE", "true");
DEFINE_TCASE_DIFF(test_const_b_T,     "T", "true");
DEFINE_TCASE_DIFF(test_const_b_False, "False", "false");
DEFINE_TCASE_DIFF(test_const_b_FALSE, "FALSE", "false");
DEFINE_TCASE_DIFF(test_const_b_F,     "F", "false");
DEFINE_FAILING_TCASE(test_const_b_t, "t", "Single-character booleans must be capitalized");
DEFINE_FAILING_TCASE(test_const_b_f, "f", "Single-character booleans must be capitalized");
DEFINE_FAILING_TCASE(test_const_b_true1, "true1", "Booleans cannot have numeric quantifiers");
DEFINE_FAILING_TCASE(test_const_b_false1, "false1", "Booleans cannot have numeric quantifiers");

DEFINE_TCASE(test_const_I_0, "0");
DEFINE_TCASE(test_const_I_1, "1");
DEFINE_TCASE(test_const_I_123, "123");
DEFINE_TCASE_DIFF(test_const_I_0x40, "0x40", "64");
DEFINE_TCASE(test_const_I_int64_max, "9223372036854775807");
DEFINE_FAILING_TCASE(test_const_I_int64_max_1, "9223372036854775808",
		"9223372036854775808 = INT64_MAX + 1, and therefore should be out of range");
DEFINE_TCASE_DIFF(test_const_I_hex_int64_max, "0x7fffffffffffffff", "9223372036854775807");

DEFINE_TCASE(test_const_I_n1, "-1");
DEFINE_TCASE(test_const_I_n123, "-123");
DEFINE_FAILING_TCASE(test_const_I_n0x40, "-0x40", "hex numbers can't be negative");
DEFINE_TCASE(test_const_I_int64_min, "-9223372036854775808");
DEFINE_FAILING_TCASE(test_const_I_int64_min_1, "-9223372036854775809",
		"-9223372036854775809 = INT64_MIN - 1, and therefore should be out of range");
DEFINE_TCASE_DIFF(test_const_I_hex_int64_min, "0x8000000000000000", "-9223372036854775808");
DEFINE_FAILING_TCASE(test_const_I_hex_int64_oob, "0x10000000000000000",
		"0x10000000000000000 should be out of range");

DEFINE_TCASE(test_const_S_empty, "\"\"");
DEFINE_TCASE(test_const_S_a, "\"a\"");
DEFINE_TCASE(test_const_S_clayton, "\"clayton\"");
DEFINE_TCASE(test_const_S_long,
		"\"aaaaaaaaaaaaaaabbbbbbbbbbbbbbbbbcccccccccccccccccccdddddddddddddddd"
		"dddddddddeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeefffffffffffffffffffffffggg"
		"ggggggggggggggg\"");
DEFINE_TCASE_DIFF(test_const_S_ba, "\"\\a\"", "\"\a\"");
DEFINE_TCASE_DIFF(test_const_S_bb, "\"\\b\"", "\"\b\"");
DEFINE_TCASE_DIFF(test_const_S_be, "\"\\e\"", "\"\e\"");
DEFINE_TCASE_DIFF(test_const_S_bf, "\"\\f\"", "\"\f\"");
DEFINE_TCASE_DIFF(test_const_S_bn, "\"\\n\"", "\"\n\"");
DEFINE_TCASE_DIFF(test_const_S_br, "\"\\r\"", "\"\r\"");
DEFINE_TCASE_DIFF(test_const_S_bt, "\"\\t\"", "\"\t\"");
DEFINE_TCASE_DIFF(test_const_S_bv, "\"\\v\"", "\"\v\"");
DEFINE_TCASE_DIFF(test_const_S_bs, "\"\\\\\"", "\"\\\"");
DEFINE_TCASE_DIFF(test_const_S_bq, "\"\\'\"", "\"\'\"");
DEFINE_TCASE_DIFF(test_const_S_bqq, "\"\\\"\"", "\"\"\"");
DEFINE_TCASE_DIFF(test_const_S_bqm, "\"\\?\"", "\"\?\"");
DEFINE_TCASE_DIFF(test_const_S_hex, "\"\\x61\"", "\"\x61\"");
DEFINE_TCASE_DIFF(test_const_S_hex_v2, "\"\\x61f\"", "\"af\"");
DEFINE_TCASE_DIFF(test_const_S_oct, "\"\\141\"", "\"\141\"");
DEFINE_TCASE_DIFF(test_const_S_oct_v2, "\"\\377\"", "\"\377\"");
DEFINE_TCASE_DIFF(test_const_S_mixed,
		"\"this is \\147onna be a \\x6cong message\\n"
		"\\150\\145\\154\\154\\157\\041\\011"
		"\\x67\\x6f\\x6F\\x64\\x62\\x79\\x65\\x3f\"",
		"\"this is gonna be a long message\nhello!\tgoodbye?\"");
DEFINE_FAILING_TCASE(test_const_S_unterminated, "\"test string", "unterminated string");
DEFINE_FAILING_TCASE(test_const_S_single_0x, "\"\\x0x\"", "\"0x\" is not a valid hex code");

DEFINE_TCASE_DIFF(test_const_D_0, "0.", "0f");
DEFINE_TCASE_DIFF(test_const_D_0f, "0.f", "0f");
DEFINE_TCASE_DIFF(test_const_D_0_0, "0.0", "0f");
DEFINE_TCASE_DIFF(test_const_D_0_0f, "0.0f", "0f");
DEFINE_TCASE_DIFF(test_const_D_1, "1.", "1f");
DEFINE_TCASE_DIFF(test_const_D_1f, "1.f", "1f");
DEFINE_TCASE_DIFF(test_const_D_1_0, "1.0", "1f");
DEFINE_TCASE_DIFF(test_const_D_1_0f, "1.0f", "1f");
DEFINE_TCASE_DIFF(test_const_D_123, "123.", "123f");
DEFINE_TCASE_DIFF(test_const_D_123_456, "123.456", "123.456f");
DEFINE_FAILING_TCASE(test_const_D_f, "f", "a single 'f' is not a valid float");
DEFINE_FAILING_TCASE(test_const_D_0f, "0f", "floats must always contain a '.'");
DEFINE_FAILING_TCASE(test_const_D_1f, "1f", "floats must always contain a '.'");

DEFINE_FAILING_TCASE(test_const_map_key_bool, "{true:S5}", "maps cannot have boolean keys");
DEFINE_TCASE(test_const_map_key_int, "{10:S5}");
DEFINE_TCASE(test_const_map_key_str, "{\"test_key\":S5}");
DEFINE_FAILING_TCASE(test_const_map_key_double, "{3.14f:S5}", "maps cannot have double keys");
DEFINE_FAILING_TCASE(test_const_map_keys, "{5*10:S5}", "constant map keys "
		"cannot have multipliers");
DEFINE_FAILING_TCASE(test_const_map_key_bool_rep, "{true:S5,I:I,true:B10}",
		"cannot repeat constant boolean key");
DEFINE_FAILING_TCASE(test_const_map_key_int_rep, "{10:S5,I:I,10:B10}",
		"cannot repeat constant integer key");
DEFINE_FAILING_TCASE(test_const_map_key_str_rep,
		"{\"test_key\":S5,\"key2\":I,\"test_key\":B10}",
		"cannot repeat constant string key");
DEFINE_FAILING_TCASE(test_const_map_key_double_rep, "{3.14f:S5,I:I,3.14f:B10}",
		"cannot repeat constant double key");


/*
 * List test cases
 */
DEFINE_TCASE(test_singleton_list, "[I3]");
DEFINE_TCASE(test_pair_list, "[I3,S5]");
DEFINE_TCASE(test_long_list, "[B10,D,S22,I7,I8,S30,B110,I2,I4]");
DEFINE_TCASE(test_repeated_list, "[D,D,D,D,D,b,B10,B10,B10,I4,I4,I4,I4]");
DEFINE_TCASE(test_empty_list, "[]");
DEFINE_FAILING_TCASE(test_unterminated_list, "[S10,I3", "unterminated list");
DEFINE_FAILING_TCASE(test_unterminated_list_v2, "[S10,I3,", "unterminated list");
DEFINE_FAILING_TCASE(test_unopened_list, "I3]", "unopened list");


/*
 * Map test cases
 */
DEFINE_FAILING_TCASE(test_map_bb, "{b:b}", "maps cannot have boolean keys");
DEFINE_FAILING_TCASE(test_map_bI, "{b:I2}", "maps cannot have boolean keys");
DEFINE_FAILING_TCASE(test_map_bD, "{b:D}", "maps cannot have boolean keys");
DEFINE_FAILING_TCASE(test_map_bS, "{b:S4}", "maps cannot have boolean keys");
DEFINE_FAILING_TCASE(test_map_bB, "{b:B5}", "maps cannot have boolean keys");
DEFINE_TCASE(test_map_Ib, "{I1:b}");
DEFINE_TCASE(test_map_II, "{I1:I2}");
DEFINE_TCASE(test_map_ID, "{I1:D}");
DEFINE_TCASE(test_map_IS, "{I1:S4}");
DEFINE_TCASE(test_map_IB, "{I1:B5}");
DEFINE_FAILING_TCASE(test_map_Db, "{D:b}", "maps cannot have double keys");
DEFINE_FAILING_TCASE(test_map_DI, "{D:I2}", "maps cannot have double keys");
DEFINE_FAILING_TCASE(test_map_DD, "{D:D}", "maps cannot have double keys");
DEFINE_FAILING_TCASE(test_map_DS, "{D:S4}", "maps cannot have double keys");
DEFINE_FAILING_TCASE(test_map_DB, "{D:B5}", "maps cannot have double keys");
DEFINE_TCASE(test_map_Sb, "{S2:b}");
DEFINE_TCASE(test_map_SI, "{S2:I2}");
DEFINE_TCASE(test_map_SD, "{S2:D}");
DEFINE_TCASE(test_map_SS, "{S2:S4}");
DEFINE_TCASE(test_map_SB, "{S2:B5}");
DEFINE_TCASE(test_map_Bb, "{B6:b}");
DEFINE_TCASE(test_map_BI, "{B6:I2}");
DEFINE_TCASE(test_map_BD, "{B6:D}");
DEFINE_TCASE(test_map_BS, "{B6:S4}");
DEFINE_TCASE(test_map_BB, "{B6:B5}");
DEFINE_TCASE(test_empty_map, "{}");

DEFINE_FAILING_TCASE(test_empty_map_v2, "{0*S10:B20}", "cannot have 0 multiplier on map key");
DEFINE_FAILING_TCASE(test_map_with_no_value, "{I1}", "maps need key and value");
DEFINE_FAILING_TCASE(test_map_with_no_value_v2, "{I1:}", "maps need key and value");
DEFINE_FAILING_TCASE(test_map_multiple_keys, "{I3:I5:I7}", "map cannot have multiple keys");
DEFINE_FAILING_TCASE(test_unterminated_map, "{", "unterminated map");
DEFINE_FAILING_TCASE(test_unterminated_map_v2, "{I3:", "unterminated map");
DEFINE_FAILING_TCASE(test_unterminated_map_v3, "{I3:S10", "unterminated map");
DEFINE_FAILING_TCASE(test_unopened_map, "I3:S10}", "unopened map");
DEFINE_FAILING_TCASE(test_unopened_map_v2, ":S10}", "unopened map");
DEFINE_FAILING_TCASE(test_unopened_map_v3, "S10}", "unopened map");
DEFINE_FAILING_TCASE(test_unopened_map_v4, "}", "unopened map");

/*
 * maps with multiple entries test cases
 */
DEFINE_TCASE(test_multi_map_simple, "{I1:b,I2:D}");
DEFINE_TCASE(test_multi_map_long, "{I1:b,I2:D,I3:B10,I4:S20,I5:[D,B11,S12]}");
DEFINE_TCASE(test_multi_map_repeat_keys, "{I1:S5,I1:I5}");
DEFINE_TCASE(test_multi_map_const_keys, "{\"test_key1\":S5,\"test_key2\":I5}");
DEFINE_FAILING_TCASE(test_multi_map_repeat_const_key,
		"{\"test_key\":b,I3:D,\"test_key\":S10}",
		"should not be allowed to repeat a const key");

/*
 * Multiple bins test cases
 */
DEFINE_TCASE(test_two_bins, "I1, I2");
DEFINE_TCASE(test_three_bins, "I1, I2, I3");
DEFINE_TCASE(test_mixed_bins, "S12, I6, B20");
DEFINE_TCASE(test_many_bins, "I1, I2, I3, I4, S1, S2, S3, S4, D, b, B1, B2, B3, B4");
DEFINE_TCASE(test_repeated_bins, "I1, I1, D, D, S10, S10, B5, B5, b, b");
DEFINE_FAILING_TCASE(test_no_commas, "I1D", "need commas separating tokens");
DEFINE_FAILING_TCASE(test_spaces, "I1 D", "need commas separating tokens");


/*
 * Nested lists/maps test cases
 */
DEFINE_TCASE(test_map_to_list, "{I5:[S10,B20,D,b]}");
DEFINE_TCASE(test_list_of_maps, "[{I5:I1},{S10:B20}]");
DEFINE_FAILING_TCASE(test_mixed_list_of_maps, "[{D:I3},S10,{S20:B1},b]", "map cannot have double keys");
DEFINE_TCASE(test_nested_lists,
		"[[S10,I4,[B10,B5,D],D],S20,[B100,I7,D,b],[[I3],[I4,I5,I6]]]");
DEFINE_FAILING_TCASE(test_nested_maps, "{I5:{S30:{D:{B123:b}}}}", "map cannot have double keys");
DEFINE_FAILING_TCASE(test_nested_mix,
		"[{S10:D},[I3,{S10:B20},{I3:{D:[I1,I2,b]}},B10,b],{B32:[b,I3,I5,{I7:D}]}]", "map cannot have double keys");
DEFINE_FAILING_TCASE(test_map_key_list, "{[I3,I5]:I6}", "map key cannot be a list");
DEFINE_FAILING_TCASE(test_map_key_map, "{{S20:I4}:I1}", "map key cannot be a map");
DEFINE_FAILING_TCASE(test_map_to_undeclared_list, "{I4:I1,I2}", "map value must be single type");


/*
 * Const collection data types
 */
DEFINE_TCASE(test_const_list, "[123, \"abc\", 3.14]");
DEFINE_TCASE(test_const_map, "{\"test_key\":123}");
DEFINE_TCASE(test_const_nested_list, "[123, \"abc\", [456, [\"string\", false], \"def\"], true]");
DEFINE_TCASE(test_const_nested_map, "{1:{\"name\":\"clayton\", \"species\":\"human\"}, "
		"2:{123:true, 456:false}}");
DEFINE_TCASE(test_const_nested_mixed, "{1:{\"names\":[\"clayton\", \"clay\", 1234]}, "
		"2:[456, false, 123, true], "
		"\"species\":\"human\"}, [{456:\"hi\"}, {\"hello\":789}]");


/*
 * Multipliers test cases
 */
DEFINE_TCASE(test_mult_b, "8*b");
DEFINE_TCASE(test_mult_I, "2*I3");
DEFINE_TCASE(test_mult_D, "5*D");
DEFINE_TCASE(test_mult_S, "3*S10");
DEFINE_TCASE(test_mult_B, "10*B3");
DEFINE_TCASE(test_mult_list, "8*[I1,I2,S3]");
DEFINE_TCASE(test_mult_map, "20*{I6:S20}");
DEFINE_TCASE(test_mult_within_list, "2*[5*I1,3*I2,100*S3]");
DEFINE_TCASE(test_mult_within_map, "30*{I3:[5*S20]}");
DEFINE_TCASE(test_mult_map_key_I, "{3*I2:S3}");
DEFINE_FAILING_TCASE(test_mult_map_key_D, "{5*D:S3}", "map cannot have double keys");
DEFINE_TCASE(test_mult_map_key_S, "{2*S2:S3}");
DEFINE_TCASE(test_mult_map_key_B, "{700*B5:S3}");
DEFINE_TCASE(test_mult_map_key_I_high_coverage, "{200*I1:S3}");
DEFINE_TCASE(test_mult_multi_map, "{3*I1:b,5*I2:D}");
DEFINE_TCASE(test_mult_multi_map_repeat_keys, "{3*I1:b,5*I1:D}");
DEFINE_FAILING_TCASE(test_mult_zero, "0*I2", "cannot have zero multiplier except on map keys");
DEFINE_FAILING_TCASE(test_mult_no_star, "3I2", "multipliers must be followed with a '*'");
DEFINE_FAILING_TCASE(test_mult_map_val_b, "{I1:4*b}",
		"no multipliers on map values allowed");
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
 * write-bins test cases
 */
DEFINE_TCASE_WRITE_BINS(test_wb_simple, "I4, D", ((uint32_t[]) { 0, 1 }), 2);
DEFINE_TCASE_WRITE_BINS(test_wb_odds, "I1, I2, I3, I4, I5, I6, I7, I8",
		((uint32_t[]) { 0, 2, 4, 6 }), 4);
DEFINE_TCASE_WRITE_BINS(test_wb_evens, "I1, I2, I3, I4, I5, I6, I7, I8",
		((uint32_t[]) { 1, 3, 5, 7 }), 4);
DEFINE_TCASE_WRITE_BINS(test_wb_lists, "I4, [I6,B10], B20, [[S20,I8],S10]",
		((uint32_t[]) { 1, 3 }), 2);
DEFINE_TCASE_WRITE_BINS(test_wb_maps, "{5*S10:B20}, [3*{I4:S5},D], B10, {I4:I8}",
		((uint32_t[]) { 0, 1, 3 }), 3);
DEFINE_TCASE_WRITE_BINS(test_wb_repeats, "30*I3, 27*S10, D, I1, I2, b, I5, 10*[5*I6]",
		((uint32_t[]) { 14, 27, 30, 56, 57, 59, 63, 66, 71 }), 9);


/*
 * Bin name test cases
 */
#define DEFINE_BIN_NAME_OK(test_name, n_bins, bin_name) \
START_TEST(test_name) \
{ \
	struct obj_spec_s o; \
	ck_assert_int_eq(obj_spec_parse(&o, #n_bins "*I1"), 0); \
	ck_assert(obj_spec_bin_name_compatible(&o, bin_name)); \
	obj_spec_free(&o); \
} \
END_TEST

#define DEFINE_BIN_NAME_TOO_LARGE(test_name, n_bins, bin_name) \
START_TEST(test_name) \
{ \
	struct obj_spec_s o; \
	ck_assert_int_eq(obj_spec_parse(&o, #n_bins "*I1"), 0); \
	ck_assert(!obj_spec_bin_name_compatible(&o, bin_name)); \
	obj_spec_free(&o); \
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


/*
 * Spacing test cases
 */
DEFINE_TCASE_DIFF(test_space, "I, D", "I4, D");
DEFINE_TCASE_DIFF(test_space_in_list, "I, [B12, S15]", "I4, [B12,S15]");
DEFINE_TCASE_DIFF(test_space_map_after_key, "{S12 :I7}", "{S12:I7}");
DEFINE_TCASE_DIFF(test_space_map_before_value, "{B8: b}", "{B8:b}");
DEFINE_TCASE_DIFF(test_space_map_both, "{I2 : S1}", "{I2:S1}");
DEFINE_TCASE_DIFF(test_space_mult_before, "4 *I,[ 3 *b, 2 *{3 *S10:B20}]", "4*I4, [3*b,2*{3*S10:B20}]");
DEFINE_TCASE_DIFF(test_space_mult_after, "4* I,[3* b, 2* {3* S10:B20}]", "4*I4, [3*b,2*{3*S10:B20}]");
DEFINE_TCASE_DIFF(test_space_mult_both, "4 * I,[3 * b, 2 * {3 * S10:B20}]", "4*I4, [3*b,2*{3*S10:B20}]");
DEFINE_TCASE_DIFF(test_space_list, "[ I, D, S20 ]", "[I4,D,S20]");
DEFINE_TCASE_DIFF(test_space_map, "{ S20 : B10 }", "{S20:B10}");


/*
 * Generator test cases
 */
DEFINE_TCASE(test_gen_first_name, "@first_name");
DEFINE_TCASE(test_gen_last_name, "@last_name");
DEFINE_TCASE(test_gen_full_name, "@full_name");
DEFINE_TCASE(test_gen_username, "@username");
DEFINE_TCASE(test_gen_email, "@email");
DEFINE_TCASE(test_gen_phone, "@phone");
DEFINE_TCASE(test_gen_street, "@street");
DEFINE_TCASE(test_gen_city, "@city");
DEFINE_TCASE(test_gen_state, "@state");
DEFINE_TCASE(test_gen_state_abbr, "@state_abbr");
DEFINE_TCASE(test_gen_zip, "@zip");
DEFINE_TCASE(test_gen_country, "@country");
DEFINE_TCASE(test_gen_country_code, "@country_code");
DEFINE_TCASE(test_gen_lat, "@lat");
DEFINE_TCASE(test_gen_lat_range, "@lat(30,40.5)");
DEFINE_TCASE(test_gen_lon, "@lon");
DEFINE_TCASE(test_gen_lon_range, "@lon(-100.25,-90)");
DEFINE_TCASE(test_gen_geojson, "@geojson");
DEFINE_TCASE(test_gen_geojson_bbox, "@geojson(37.7,37.8,-122.5,-122.4)");
DEFINE_TCASE(test_gen_geo_circle, "@geo_circle");
DEFINE_TCASE(test_gen_geo_circle_r, "@geo_circle(50,500)");
DEFINE_TCASE(test_gen_geo_circle_bbox, "@geo_circle(50,500,51.4,51.6,-0.3,0.1)");
DEFINE_TCASE(test_gen_company, "@company");
DEFINE_TCASE(test_gen_job_title, "@job_title");
DEFINE_TCASE(test_gen_ipv4, "@ipv4");
DEFINE_TCASE(test_gen_ipv6, "@ipv6");
DEFINE_TCASE(test_gen_mac, "@mac");
DEFINE_TCASE(test_gen_url, "@url");
DEFINE_TCASE(test_gen_domain, "@domain");
DEFINE_TCASE(test_gen_uuid, "@uuid");
DEFINE_TCASE(test_gen_word, "@word");
DEFINE_TCASE(test_gen_words, "@words");
DEFINE_TCASE(test_gen_words_n, "@words(5)");
DEFINE_TCASE(test_gen_sentence, "@sentence");
DEFINE_TCASE(test_gen_lorem, "@lorem");
DEFINE_TCASE(test_gen_lorem_n, "@lorem(20)");
DEFINE_TCASE(test_gen_product, "@product");
DEFINE_TCASE(test_gen_color, "@color");
DEFINE_TCASE(test_gen_credit_card, "@credit_card");
DEFINE_TCASE(test_gen_date, "@date");
DEFINE_TCASE(test_gen_date_range, "@date(1577836800,1735689599)");
DEFINE_TCASE(test_gen_date_fmt, "@date(1577836800,1735689599,\"%d/%m/%Y\")");
DEFINE_TCASE(test_gen_timestamp, "@timestamp");
DEFINE_TCASE(test_gen_timestamp_range, "@timestamp(1700000000,1800000000)");
DEFINE_TCASE(test_gen_int, "@int(18,90)");
DEFINE_TCASE(test_gen_int_neg, "@int(-5,5)");
DEFINE_TCASE(test_gen_int_single, "@int(7,7)");
DEFINE_TCASE(test_gen_double, "@double(0.5,2.5)");
DEFINE_TCASE(test_gen_double_ints, "@double(-1,1)");
DEFINE_TCASE(test_gen_pick, "@pick(\"a\",\"b\",\"c\")");
DEFINE_TCASE(test_gen_pick_weighted, "@pick(\"a\":90,\"b\":10)");
DEFINE_TCASE(test_gen_pick_escaped, "@pick(\"say \\\"hi\\\"\",\"tab\\there\")");
DEFINE_TCASE(test_gen_fmt, "@fmt(\"#{first_name}.#{last_name}@example.com\")");
DEFINE_TCASE(test_gen_fmt_int, "@fmt(\"SKU-#{int(1000,9999)}\")");
DEFINE_TCASE(test_gen_fmt_double, "@fmt(\"$#{double(1,100)}\")");
DEFINE_TCASE(test_gen_fmt_pick, "@fmt(\"#{pick(\\\"x\\\",\\\"y\\\")}-#{uuid}\")");
DEFINE_TCASE(test_gen_fmt_literal, "@fmt(\"constant\")");

DEFINE_TCASE_DIFF(test_gen_spacing, "@int( 18 , 90 )", "@int(18,90)");
DEFINE_TCASE_DIFF(test_gen_empty_parens, "@first_name()", "@first_name");
DEFINE_TCASE_DIFF(test_gen_date_strings, "@date(\"2020-01-01\",\"2024-12-31\")",
		"@date(1577836800,1735689599)");
DEFINE_TCASE_DIFF(test_gen_date_default_fmt,
		"@date(1577836800,1735689599,\"%Y-%m-%d\")",
		"@date(1577836800,1735689599)");
DEFINE_TCASE_DIFF(test_gen_timestamp_strings,
		"@timestamp(\"2024-01-01\",\"2024-01-31\")",
		"@timestamp(1704067200,1706745599)");
DEFINE_TCASE_DIFF(test_gen_double_suffix, "@double(0.1f,0.2)", "@double(0.1,0.2)");
DEFINE_TCASE_DIFF(test_gen_circle_default_bbox,
		"@geo_circle(50,500,24,49,-125,-66)", "@geo_circle(50,500)");
DEFINE_TCASE_DIFF(test_gen_name_space, "first= @first_name", "first=@first_name");

DEFINE_TCASE(test_gen_named, "first=@first_name, age=@int(18,90)");
DEFINE_TCASE(test_gen_list, "[3*@word]");
DEFINE_TCASE(test_gen_map, "{5*@word:@int(1,5)}");
DEFINE_TCASE(test_gen_map_str_key, "{@state_abbr:@city}");
DEFINE_TCASE(test_gen_map_int_key, "{10*@int(1,1000):@email}");
DEFINE_TCASE(test_gen_mixed, "I4, @email, [3*@word]");
DEFINE_TCASE(test_gen_mult, "2*@word");
DEFINE_TCASE(test_gen_named_mult, "tags=3*@word");
DEFINE_TCASE(test_gen_named_mixed, "a=I4, S8, c=D");
DEFINE_TCASE(test_gen_named_max_len, "abcdefghijklmno=I4");
DEFINE_TCASE(test_gen_customer,
		"id=@uuid, first=@first_name, last=@last_name, email=@email, "
		"age=@int(18,90), city=@city, state=@state_abbr, zip=@zip, "
		"joined=@date(1420070400,1767225599)");
DEFINE_TCASE(test_gen_orders,
		"orders=[3*{\"sku\":@product,\"qty\":@int(1,9),\"price\":@double(1,500)}]");
DEFINE_TCASE(test_gen_feed,
		"events=[2*{\"ts\":@timestamp,\"type\":@pick(\"view\",\"click\",\"buy\"),\"loc\":@geojson}]");
DEFINE_TCASE(test_gen_nested_geo, "{3*@city:[2*@geo_circle]}");
DEFINE_TCASE_WRITE_BINS(test_gen_wb, "a=@first_name, I4, c=@email, @uuid",
		((uint32_t[]) { 0, 2, 3 }), 3);
DEFINE_TCASE_WRITE_BINS(test_gen_wb_repeat, "tags=4*@word, n=@int(1,9)",
		((uint32_t[]) { 1, 3, 4 }), 3);

DEFINE_FAILING_TCASE(test_gen_unknown, "@nope", "unknown generator");
DEFINE_FAILING_TCASE(test_gen_bare_at, "@", "missing generator name");
DEFINE_FAILING_TCASE(test_gen_int_no_args, "@int", "@int needs 2 args");
DEFINE_FAILING_TCASE(test_gen_int_one_arg, "@int(1)", "@int needs 2 args");
DEFINE_FAILING_TCASE(test_gen_int_reversed, "@int(90,18)", "min > max");
DEFINE_FAILING_TCASE(test_gen_int_double, "@int(1.5,2)", "@int needs integers");
DEFINE_FAILING_TCASE(test_gen_int_unclosed, "@int(1,2", "unclosed args");
DEFINE_FAILING_TCASE(test_gen_int_bad_sep, "@int(1;2)", "bad separator");
DEFINE_FAILING_TCASE(test_gen_words_zero, "@words(0)", "count must be >= 1");
DEFINE_FAILING_TCASE(test_gen_words_neg, "@words(-1)", "count must be >= 1");
DEFINE_FAILING_TCASE(test_gen_lorem_zero, "@lorem(0)", "length must be >= 1");
DEFINE_FAILING_TCASE(test_gen_date_reversed,
		"@date(\"2024-12-31\",\"2020-01-01\")", "min > max");
DEFINE_FAILING_TCASE(test_gen_date_bad_month,
		"@date(\"2020-13-01\",\"2021-01-01\")", "invalid month");
DEFINE_FAILING_TCASE(test_gen_date_bad_day,
		"@date(\"2021-02-29\",\"2021-03-01\")", "invalid day");
DEFINE_FAILING_TCASE(test_gen_date_one_arg, "@date(1)", "@date needs 0, 2 or 3 args");
DEFINE_FAILING_TCASE(test_gen_date_empty_fmt, "@date(1,2,\"\")", "empty format");
DEFINE_FAILING_TCASE(test_gen_date_year_range, "@date(0,999999999999)",
		"year > 9999");
DEFINE_FAILING_TCASE(test_gen_date_fmt_longest_names, "@date(0,1,\""
		"123456789012345678901234567890123456789012345%A %B\")",
		"64 chars on a Wednesday in September");
DEFINE_FAILING_TCASE(test_gen_pick_empty, "@pick()", "@pick needs an option");
DEFINE_FAILING_TCASE(test_gen_pick_zero_weight, "@pick(\"a\":0)", "weight must be > 0");
DEFINE_FAILING_TCASE(test_gen_pick_mixed, "@pick(\"a\":5,\"b\")", "mixed weights");
DEFINE_FAILING_TCASE(test_gen_pick_ints, "@pick(1,2)", "@pick needs strings");
DEFINE_FAILING_TCASE(test_gen_fmt_unknown, "@fmt(\"#{unknown}\")", "unknown placeholder");
DEFINE_FAILING_TCASE(test_gen_fmt_geo, "@fmt(\"#{geojson}\")", "geo placeholder");
DEFINE_FAILING_TCASE(test_gen_fmt_nested, "@fmt(\"#{fmt(\\\"x\\\")}\")", "nested @fmt");
DEFINE_FAILING_TCASE(test_gen_fmt_unterminated, "@fmt(\"#{first_name\")", "unterminated");
DEFINE_FAILING_TCASE(test_gen_fmt_not_string, "@fmt(1)", "template must be a string");
DEFINE_FAILING_TCASE(test_gen_no_args_allowed, "@first_name(1)", "takes no arguments");
DEFINE_FAILING_TCASE(test_gen_geojson_two_args, "@geojson(1,2)", "0 or 4 args");
DEFINE_FAILING_TCASE(test_gen_geojson_reversed, "@geojson(50,40,-1,1)", "min > max");
DEFINE_FAILING_TCASE(test_gen_geojson_out_of_range, "@geojson(95,96,0,1)", "lat > 90");
DEFINE_FAILING_TCASE(test_gen_circle_reversed, "@geo_circle(500,50)", "r_min > r_max");
DEFINE_FAILING_TCASE(test_gen_lat_one_arg, "@lat(1)", "0 or 2 args");
DEFINE_FAILING_TCASE(test_gen_double_string, "@double(\"a\",1)", "numeric args");
DEFINE_FAILING_TCASE(test_gen_map_key_geo, "{@geojson:I}", "geo map key");
DEFINE_FAILING_TCASE(test_gen_map_key_double, "{@lat:I}", "double map key");
DEFINE_FAILING_TCASE(test_gen_map_key_double2, "{@double(1,2):I}", "double map key");
DEFINE_FAILING_TCASE(test_gen_map_key_cardinality, "{60*@state:I}", "only 50 states");
DEFINE_FAILING_TCASE(test_gen_map_key_now, "{2*@now:I}", "@now has 1 value");
DEFINE_FAILING_TCASE(test_gen_name_too_long, "abcdefghijklmnop=@int(1,2)", "16 chars");
DEFINE_FAILING_TCASE(test_gen_name_repeat_too_long, "abcdefghijklmn=2*I", "name_2 is 16 chars");
DEFINE_FAILING_TCASE(test_gen_name_duplicate, "a=@int(1,2), a=@word", "duplicate name");
DEFINE_FAILING_TCASE(test_gen_name_empty, "=@int(1,2)", "empty name");
DEFINE_FAILING_TCASE(test_gen_name_in_list, "[a=I]", "nested name");
DEFINE_FAILING_TCASE(test_gen_name_in_map, "{S4:a=I}", "nested name");

START_TEST(test_gen_now)
{
	struct obj_spec_s o;
	as_random random;
	as_record rec;
	char buf[16];

	as_random_init(&random);
	ck_assert_int_eq(obj_spec_parse(&o, "@now"), 0);
	_dbg_sprint_obj_spec(&o, buf, sizeof(buf));
	ck_assert_str_eq(buf, "@now");

	int64_t t0 = (int64_t) cf_clock_getabsolute();
	as_record_init(&rec, 1);
	ck_assert_int_eq(obj_spec_populate_bins(&o, &rec, &random, "test", NULL, 0,
				1.f), 0);
	int64_t t1 = (int64_t) cf_clock_getabsolute();
	int64_t v = as_record_get_int64(&rec, "test", -1);
	ck_assert_int_ge(v, t0);
	ck_assert_int_le(v, t1);
	as_record_destroy(&rec);
	obj_spec_free(&o);
}
END_TEST

START_TEST(test_gen_has_generators)
{
	struct obj_spec_s o;
	ck_assert_int_eq(obj_spec_parse(&o, "I4, [S3, {S2:@word}]"), 0);
	ck_assert(obj_spec_has_generators(&o));
	ck_assert(!obj_spec_has_bin_names(&o));
	obj_spec_free(&o);

	ck_assert_int_eq(obj_spec_parse(&o, "I4, [S3, {S2:B4}], \"x\""), 0);
	ck_assert(!obj_spec_has_generators(&o));
	obj_spec_free(&o);
}
END_TEST

START_TEST(test_gen_resolve_names)
{
	struct obj_spec_s o;
	as_bin_name names[6];
	ck_assert_int_eq(obj_spec_parse(&o, "tags=3*@word, I, geo=@geojson, S2"), 0);
	ck_assert(obj_spec_has_bin_names(&o));
	ck_assert_int_eq(obj_spec_resolve_bin_names(&o, "x", names), 0);
	ck_assert_str_eq(names[0], "tags");
	ck_assert_str_eq(names[1], "tags_2");
	ck_assert_str_eq(names[2], "tags_3");
	ck_assert_str_eq(names[3], "x_4");
	ck_assert_str_eq(names[4], "geo");
	ck_assert_str_eq(names[5], "x_6");
	obj_spec_free(&o);
}
END_TEST

START_TEST(test_gen_resolve_clash)
{
	struct obj_spec_s o;
	as_bin_name names[3];
	ck_assert_int_eq(obj_spec_parse(&o, "I, testbin=S4"), 0);
	ck_assert_int_ne(obj_spec_resolve_bin_names(&o, "testbin", names), 0);
	obj_spec_free(&o);

	ck_assert_int_eq(obj_spec_parse(&o, "I, S4, testbin_2=I"), 0);
	ck_assert_int_ne(obj_spec_resolve_bin_names(&o, "testbin", names), 0);
	obj_spec_free(&o);
}
END_TEST

START_TEST(test_gen_bin_accessors)
{
	struct obj_spec_s o;
	as_random random;
	as_random_init(&random);
	ck_assert_int_eq(obj_spec_parse(&o, "2*I1, {3*@word:@int(1,5)}, [@city]"), 0);
	ck_assert_uint_eq(obj_spec_bin_spec(&o, 0)->type, BIN_SPEC_TYPE_INT);
	ck_assert_uint_eq(obj_spec_bin_spec(&o, 1)->type, BIN_SPEC_TYPE_INT);
	ck_assert_uint_eq(obj_spec_bin_spec(&o, 2)->type, BIN_SPEC_TYPE_MAP);
	ck_assert_uint_eq(obj_spec_bin_spec(&o, 3)->type, BIN_SPEC_TYPE_LIST);

	as_val* key = obj_spec_gen_map_key(&o, 2, &random);
	ck_assert_ptr_ne(key, NULL);
	ck_assert_int_eq(key->type, AS_STRING);
	ck_assert(synth_check_val(&obj_spec_bin_spec(&o, 2)->map.kv_pairs[0].key.gen, key));
	as_val_destroy(key);
	ck_assert_ptr_eq(obj_spec_gen_map_key(&o, 3, &random), NULL);

	as_val* map = obj_spec_gen_bin_val(&o, 2, &random, 1.f);
	ck_assert_int_eq(map->type, AS_MAP);
	ck_assert_uint_eq(as_map_size(as_map_fromval(map)), 3);
	as_val_destroy(map);
	obj_spec_free(&o);
}
END_TEST

START_TEST(test_gen_print_truncation)
{
	struct obj_spec_s o;
	char buf[24];
	memset(buf, 'Z', sizeof(buf));
	ck_assert_int_eq(obj_spec_parse(&o,
				"orders=[3*{\"sku\":@product,\"qty\":@int(1,9)}]"), 0);
	snprint_obj_spec(&o, buf, 16);
	ck_assert_uint_eq(strlen(buf), 15);
	ck_assert_int_eq(buf[16], 'Z');
	obj_spec_free(&o);
}
END_TEST

START_TEST(test_gen_seeded_bins_independent)
{
	struct obj_spec_s o;
	as_bin_name names[4];
	as_random random;
	as_record full, partial;
	uint64_t seed = 12345;
	uint32_t wb[] = { 1, 3 };

	as_random_init(&random);
	ck_assert_int_eq(obj_spec_parse(&o, "a=@email, b=@uuid, c=@int(1,1000000), d=[3*@word]"), 0);
	ck_assert_int_eq(obj_spec_resolve_bin_names(&o, "x", names), 0);

	as_record_init(&full, 4);
	as_record_init(&partial, 2);
	ck_assert_int_eq(obj_spec_populate_bins_named(&o, &full, &random,
				(const as_bin_name*) names, NULL, 0, 1.f, &seed), 0);
	ck_assert_int_eq(obj_spec_populate_bins_named(&o, &partial, &random,
				(const as_bin_name*) names, wb, 2, 1.f, &seed), 0);

	ck_assert(as_val_cmp((as_val*) as_record_get(&full, "b"),
				(as_val*) as_record_get(&partial, "b")) == MSGPACK_COMPARE_EQUAL);
	ck_assert(as_val_cmp((as_val*) as_record_get(&full, "d"),
				(as_val*) as_record_get(&partial, "d")) == MSGPACK_COMPARE_EQUAL);

	as_record_destroy(&full);
	as_record_destroy(&partial);
	obj_spec_free(&o);
}
END_TEST


#define tcase_add_ptest(tcase, test_name) \
	do { \
		tcase_add_test(tcase, test_name ## _str_cmp); \
		tcase_add_test(tcase, test_name ## _valid); \
	} while (0)

#define tcase_add_ftest(tcase, test_name) \
	tcase_add_test(tcase, test_name)

Suite*
obj_spec_suite(void)
{
	Suite* s;
	TCase* tc_memory;
	TCase* tc_simple;
	TCase* tc_constants;
	TCase* tc_list;
	TCase* tc_map;
	TCase* tc_multi_map_entries;
	TCase* tc_multi_bins;
	TCase* tc_nested;
	TCase* tc_const_colx;
	TCase* tc_multipliers;
	TCase* tc_write_bins;
	TCase* tc_bin_names;
	TCase* tc_spacing;
	TCase* tc_generators;

	s = suite_create("Object Spec");

	tc_memory = tcase_create("Memory");
	tcase_add_checked_fixture(tc_memory, simple_setup, simple_teardown);
	tcase_add_test(tc_memory, test_double_free);
	tcase_add_test(tc_memory, test_free_after_failed_init);
	tcase_add_test(tc_memory, test_free_after_move);
	tcase_add_test(tc_memory, test_shallow_copy);
	tcase_add_test(tc_memory, test_free_after_shallow_copy);
	tcase_add_test(tc_memory, test_not_enough_bins);
	tcase_add_test(tc_memory, test_not_enough_bins_write_bins);
	tcase_add_test(tc_memory, test_bins_already_occupied);
	tcase_add_test(tc_memory, test_bins_already_occupied_write_bins);
	suite_add_tcase(s, tc_memory);

	tc_simple = tcase_create("Simple");
	tcase_add_checked_fixture(tc_simple, simple_setup, simple_teardown);
	tcase_add_ptest(tc_simple, test_b);
	tcase_add_ftest(tc_simple, test_b0);
	tcase_add_ftest(tc_simple, test_b1);

	tcase_add_ptest(tc_simple, test_I1);
	tcase_add_ptest(tc_simple, test_I2);
	tcase_add_ptest(tc_simple, test_I3);
	tcase_add_ptest(tc_simple, test_I4);
	tcase_add_ptest(tc_simple, test_I5);
	tcase_add_ptest(tc_simple, test_I6);
	tcase_add_ptest(tc_simple, test_I7);
	tcase_add_ptest(tc_simple, test_I8);
	tcase_add_ptest(tc_simple, test_I);
	tcase_add_ftest(tc_simple, test_I0);
	tcase_add_ftest(tc_simple, test_I9);
	tcase_add_ftest(tc_simple, test_Ia);
	tcase_add_ftest(tc_simple, test_Ineg1);

	tcase_add_ptest(tc_simple, test_D);
	tcase_add_ftest(tc_simple, test_D1);
	tcase_add_ftest(tc_simple, test_D0);

	tcase_add_ptest(tc_simple, test_S1);
	tcase_add_ptest(tc_simple, test_S10);
	tcase_add_ptest(tc_simple, test_S100);
	tcase_add_ptest(tc_simple, test_S123);
	tcase_add_ptest(tc_simple, test_S0);
	tcase_add_ftest(tc_simple, test_S_);
	tcase_add_ftest(tc_simple, test_Sneg1);
	tcase_add_ftest(tc_simple, test_S4294967296);

	tcase_add_ptest(tc_simple, test_B1);
	tcase_add_ptest(tc_simple, test_B10);
	tcase_add_ptest(tc_simple, test_B100);
	tcase_add_ptest(tc_simple, test_B123);
	tcase_add_ptest(tc_simple, test_B0);
	tcase_add_ftest(tc_simple, test_B_);
	tcase_add_ftest(tc_simple, test_Bneg1);
	tcase_add_ftest(tc_simple, test_B4294967296);
	suite_add_tcase(s, tc_simple);

	tc_constants = tcase_create("Constants");
	tcase_add_checked_fixture(tc_constants, simple_setup, simple_teardown);
	tcase_add_ptest(tc_constants, test_const_b_true);
	tcase_add_ptest(tc_constants, test_const_b_false);
	tcase_add_ptest(tc_constants, test_const_b_True);
	tcase_add_ptest(tc_constants, test_const_b_TRUE);
	tcase_add_ptest(tc_constants, test_const_b_T);
	tcase_add_ptest(tc_constants, test_const_b_False);
	tcase_add_ptest(tc_constants, test_const_b_FALSE);
	tcase_add_ptest(tc_constants, test_const_b_F);

	tcase_add_ftest(tc_constants, test_const_b_t);
	tcase_add_ftest(tc_constants, test_const_b_f);
	tcase_add_ftest(tc_constants, test_const_b_true1);
	tcase_add_ftest(tc_constants, test_const_b_false1);

	tcase_add_ptest(tc_constants, test_const_I_0);
	tcase_add_ptest(tc_constants, test_const_I_1);
	tcase_add_ptest(tc_constants, test_const_I_123);
	tcase_add_ptest(tc_constants, test_const_I_0x40);
	tcase_add_ptest(tc_constants, test_const_I_int64_max);
	tcase_add_ftest(tc_constants, test_const_I_int64_max_1);
	tcase_add_ptest(tc_constants, test_const_I_hex_int64_max);

	tcase_add_ptest(tc_constants, test_const_I_n1);
	tcase_add_ptest(tc_constants, test_const_I_n123);
	tcase_add_ftest(tc_constants, test_const_I_n0x40);
	tcase_add_ptest(tc_constants, test_const_I_int64_min);
	tcase_add_ftest(tc_constants, test_const_I_int64_min_1);
	tcase_add_ptest(tc_constants, test_const_I_hex_int64_min);
	tcase_add_ftest(tc_constants, test_const_I_hex_int64_oob);

	tcase_add_ptest(tc_constants, test_const_S_empty);
	tcase_add_ptest(tc_constants, test_const_S_a);
	tcase_add_ptest(tc_constants, test_const_S_clayton);
	tcase_add_ptest(tc_constants, test_const_S_long);
	tcase_add_ptest(tc_constants, test_const_S_ba);
	tcase_add_ptest(tc_constants, test_const_S_bb);
	tcase_add_ptest(tc_constants, test_const_S_be);
	tcase_add_ptest(tc_constants, test_const_S_bf);
	tcase_add_ptest(tc_constants, test_const_S_bn);
	tcase_add_ptest(tc_constants, test_const_S_br);
	tcase_add_ptest(tc_constants, test_const_S_bt);
	tcase_add_ptest(tc_constants, test_const_S_bv);
	tcase_add_ptest(tc_constants, test_const_S_bs);
	tcase_add_ptest(tc_constants, test_const_S_bq);
	tcase_add_ptest(tc_constants, test_const_S_bqq);
	tcase_add_ptest(tc_constants, test_const_S_bqm);
	tcase_add_ptest(tc_constants, test_const_S_hex);
	tcase_add_ptest(tc_constants, test_const_S_hex_v2);
	tcase_add_ptest(tc_constants, test_const_S_oct);
	tcase_add_ptest(tc_constants, test_const_S_oct_v2);
	tcase_add_ptest(tc_constants, test_const_S_mixed);
	tcase_add_ftest(tc_constants, test_const_S_unterminated);
	tcase_add_ftest(tc_constants, test_const_S_single_0x);

	tcase_add_ptest(tc_constants, test_const_D_0);
	tcase_add_ptest(tc_constants, test_const_D_0f);
	tcase_add_ptest(tc_constants, test_const_D_0_0);
	tcase_add_ptest(tc_constants, test_const_D_0_0f);
	tcase_add_ptest(tc_constants, test_const_D_1);
	tcase_add_ptest(tc_constants, test_const_D_1f);
	tcase_add_ptest(tc_constants, test_const_D_1_0);
	tcase_add_ptest(tc_constants, test_const_D_1_0f);
	tcase_add_ptest(tc_constants, test_const_D_123);
	tcase_add_ptest(tc_constants, test_const_D_123_456);
	tcase_add_ftest(tc_constants, test_const_D_f);
	tcase_add_ftest(tc_constants, test_const_D_0f);
	tcase_add_ftest(tc_constants, test_const_D_1f);

	tcase_add_ftest(tc_constants, test_const_map_key_bool);
	tcase_add_ptest(tc_constants, test_const_map_key_int);
	tcase_add_ptest(tc_constants, test_const_map_key_str);
	tcase_add_ftest(tc_constants, test_const_map_key_double);
	tcase_add_ftest(tc_constants, test_const_map_keys);
	tcase_add_ftest(tc_constants, test_const_map_key_bool_rep);
	tcase_add_ftest(tc_constants, test_const_map_key_int_rep);
	tcase_add_ftest(tc_constants, test_const_map_key_str_rep);
	tcase_add_ftest(tc_constants, test_const_map_key_double_rep);
	suite_add_tcase(s, tc_constants);

	tc_list = tcase_create("List");
	tcase_add_checked_fixture(tc_list, simple_setup, simple_teardown);
	tcase_add_ptest(tc_list, test_singleton_list);
	tcase_add_ptest(tc_list, test_pair_list);
	tcase_add_ptest(tc_list, test_long_list);
	tcase_add_ptest(tc_list, test_repeated_list);
	tcase_add_ptest(tc_list, test_empty_list);
	tcase_add_ftest(tc_list, test_unterminated_list);
	tcase_add_ftest(tc_list, test_unterminated_list_v2);
	tcase_add_ftest(tc_list, test_unopened_list);
	suite_add_tcase(s, tc_list);

	tc_map = tcase_create("Map");
	tcase_add_checked_fixture(tc_map, simple_setup, simple_teardown);
	tcase_add_ftest(tc_map, test_map_bb);
	tcase_add_ftest(tc_map, test_map_bI);
	tcase_add_ftest(tc_map, test_map_bD);
	tcase_add_ftest(tc_map, test_map_bS);
	tcase_add_ftest(tc_map, test_map_bB);
	tcase_add_ptest(tc_map, test_map_Ib);
	tcase_add_ptest(tc_map, test_map_II);
	tcase_add_ptest(tc_map, test_map_ID);
	tcase_add_ptest(tc_map, test_map_IS);
	tcase_add_ptest(tc_map, test_map_IB);
	tcase_add_ftest(tc_map, test_map_Db);
	tcase_add_ftest(tc_map, test_map_DI);
	tcase_add_ftest(tc_map, test_map_DD);
	tcase_add_ftest(tc_map, test_map_DS);
	tcase_add_ftest(tc_map, test_map_DB);
	tcase_add_ptest(tc_map, test_map_Sb);
	tcase_add_ptest(tc_map, test_map_SI);
	tcase_add_ptest(tc_map, test_map_SD);
	tcase_add_ptest(tc_map, test_map_SS);
	tcase_add_ptest(tc_map, test_map_SB);
	tcase_add_ptest(tc_map, test_map_Bb);
	tcase_add_ptest(tc_map, test_map_BI);
	tcase_add_ptest(tc_map, test_map_BD);
	tcase_add_ptest(tc_map, test_map_BS);
	tcase_add_ptest(tc_map, test_map_BB);
	tcase_add_ptest(tc_map, test_empty_map);
	tcase_add_ftest(tc_map, test_empty_map_v2);
	tcase_add_ftest(tc_map, test_map_with_no_value);
	tcase_add_ftest(tc_map, test_map_with_no_value_v2);
	tcase_add_ftest(tc_map, test_map_multiple_keys);
	tcase_add_ftest(tc_map, test_unterminated_map);
	tcase_add_ftest(tc_map, test_unterminated_map_v2);
	tcase_add_ftest(tc_map, test_unterminated_map_v3);
	tcase_add_ftest(tc_map, test_unopened_map);
	tcase_add_ftest(tc_map, test_unopened_map_v2);
	tcase_add_ftest(tc_map, test_unopened_map_v3);
	tcase_add_ftest(tc_map, test_unopened_map_v4);
	suite_add_tcase(s, tc_map);

	tc_multi_map_entries = tcase_create("Multiple Map Entries");
	tcase_add_checked_fixture(tc_multi_map_entries, simple_setup, simple_teardown);
	tcase_add_ptest(tc_multi_map_entries, test_multi_map_simple);
	tcase_add_ptest(tc_multi_map_entries, test_multi_map_long);
	tcase_add_ptest(tc_multi_map_entries, test_multi_map_repeat_keys);
	tcase_add_ptest(tc_multi_map_entries, test_multi_map_const_keys);
	tcase_add_ftest(tc_multi_map_entries, test_multi_map_repeat_const_key);
	suite_add_tcase(s, tc_multi_map_entries);

	tc_multi_bins = tcase_create("Multiple Bins");
	tcase_add_checked_fixture(tc_multi_bins, simple_setup, simple_teardown);
	tcase_add_ptest(tc_multi_bins, test_two_bins);
	tcase_add_ptest(tc_multi_bins, test_three_bins);
	tcase_add_ptest(tc_multi_bins, test_mixed_bins);
	tcase_add_ptest(tc_multi_bins, test_many_bins);
	tcase_add_ptest(tc_multi_bins, test_repeated_bins);
	tcase_add_ftest(tc_multi_bins, test_no_commas);
	tcase_add_ftest(tc_multi_bins, test_spaces);
	suite_add_tcase(s, tc_multi_bins);

	tc_nested = tcase_create("Nested lists/maps");
	tcase_add_checked_fixture(tc_nested, simple_setup, simple_teardown);
	tcase_add_ptest(tc_nested, test_map_to_list);
	tcase_add_ptest(tc_nested, test_list_of_maps);
	tcase_add_ftest(tc_nested, test_mixed_list_of_maps);
	tcase_add_ptest(tc_nested, test_nested_lists);
	tcase_add_ftest(tc_nested, test_nested_maps);
	tcase_add_ftest(tc_nested, test_nested_mix);
	tcase_add_ftest(tc_nested, test_map_key_list);
	tcase_add_ftest(tc_nested, test_map_key_map);
	tcase_add_ftest(tc_nested, test_map_to_undeclared_list);
	suite_add_tcase(s, tc_nested);

	tc_const_colx = tcase_create("Constant collection data types");
	tcase_add_checked_fixture(tc_const_colx, simple_setup, simple_teardown);
	tcase_add_ptest(tc_const_colx, test_const_list);
	tcase_add_ptest(tc_const_colx, test_const_map);
	tcase_add_ptest(tc_const_colx, test_const_nested_list);
	tcase_add_ptest(tc_const_colx, test_const_nested_map);
	tcase_add_ptest(tc_const_colx, test_const_nested_mixed);
	suite_add_tcase(s, tc_const_colx);

	tc_multipliers = tcase_create("Multipliers");
	tcase_add_checked_fixture(tc_multipliers, simple_setup, simple_teardown);
	tcase_add_ptest(tc_multipliers, test_mult_b);
	tcase_add_ptest(tc_multipliers, test_mult_I);
	tcase_add_ptest(tc_multipliers, test_mult_D);
	tcase_add_ptest(tc_multipliers, test_mult_S);
	tcase_add_ptest(tc_multipliers, test_mult_B);
	tcase_add_ptest(tc_multipliers, test_mult_list);
	tcase_add_ptest(tc_multipliers, test_mult_map);
	tcase_add_ptest(tc_multipliers, test_mult_within_list);
	tcase_add_ptest(tc_multipliers, test_mult_within_map);
	tcase_add_ptest(tc_multipliers, test_mult_map_key_I);
	tcase_add_ftest(tc_multipliers, test_mult_map_key_D);
	tcase_add_ptest(tc_multipliers, test_mult_map_key_S);
	tcase_add_ptest(tc_multipliers, test_mult_map_key_B);
	tcase_add_ptest(tc_multipliers, test_mult_map_key_I_high_coverage);
	tcase_add_ptest(tc_multipliers, test_mult_multi_map);
	tcase_add_ptest(tc_multipliers, test_mult_multi_map_repeat_keys);
	tcase_add_ftest(tc_multipliers, test_mult_zero);
	tcase_add_ftest(tc_multipliers, test_mult_no_star);
	tcase_add_ftest(tc_multipliers, test_mult_map_val_b);
	tcase_add_ftest(tc_multipliers, test_mult_map_val_I);
	tcase_add_ftest(tc_multipliers, test_mult_map_val_D);
	tcase_add_ftest(tc_multipliers, test_mult_map_val_S);
	tcase_add_ftest(tc_multipliers, test_mult_map_val_B);
	tcase_add_ftest(tc_multipliers, test_mult_map_val_L);
	tcase_add_ftest(tc_multipliers, test_mult_map_val_M);
	tcase_add_ftest(tc_multipliers, test_mult_list_overflow);
	tcase_add_ftest(tc_multipliers, test_mult_list_overflow2);
	tcase_add_ftest(tc_multipliers, test_mult_list_too_many_elements);
	suite_add_tcase(s, tc_multipliers);

	tc_write_bins = tcase_create("Write bins");
	tcase_add_checked_fixture(tc_write_bins, simple_setup, simple_teardown);
	tcase_add_ptest(tc_write_bins, test_wb_simple);
	tcase_add_ptest(tc_write_bins, test_wb_odds);
	tcase_add_ptest(tc_write_bins, test_wb_evens);
	tcase_add_ptest(tc_write_bins, test_wb_lists);
	tcase_add_ptest(tc_write_bins, test_wb_maps);
	tcase_add_ptest(tc_write_bins, test_wb_repeats);
	suite_add_tcase(s, tc_write_bins);

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

	tc_spacing = tcase_create("Spacing");
	tcase_add_ptest(tc_spacing, test_space);
	tcase_add_ptest(tc_spacing, test_space_in_list);
	tcase_add_ptest(tc_spacing, test_space_map_after_key);
	tcase_add_ptest(tc_spacing, test_space_map_before_value);
	tcase_add_ptest(tc_spacing, test_space_map_both);
	tcase_add_ptest(tc_spacing, test_space_mult_before);
	tcase_add_ptest(tc_spacing, test_space_mult_after);
	tcase_add_ptest(tc_spacing, test_space_mult_both);
	tcase_add_ptest(tc_spacing, test_space_list);
	tcase_add_ptest(tc_spacing, test_space_map);
	suite_add_tcase(s, tc_spacing);

	tc_generators = tcase_create("Generators");
	tcase_add_checked_fixture(tc_generators, simple_setup, simple_teardown);
	tcase_add_ptest(tc_generators, test_gen_first_name);
	tcase_add_ptest(tc_generators, test_gen_last_name);
	tcase_add_ptest(tc_generators, test_gen_full_name);
	tcase_add_ptest(tc_generators, test_gen_username);
	tcase_add_ptest(tc_generators, test_gen_email);
	tcase_add_ptest(tc_generators, test_gen_phone);
	tcase_add_ptest(tc_generators, test_gen_street);
	tcase_add_ptest(tc_generators, test_gen_city);
	tcase_add_ptest(tc_generators, test_gen_state);
	tcase_add_ptest(tc_generators, test_gen_state_abbr);
	tcase_add_ptest(tc_generators, test_gen_zip);
	tcase_add_ptest(tc_generators, test_gen_country);
	tcase_add_ptest(tc_generators, test_gen_country_code);
	tcase_add_ptest(tc_generators, test_gen_lat);
	tcase_add_ptest(tc_generators, test_gen_lat_range);
	tcase_add_ptest(tc_generators, test_gen_lon);
	tcase_add_ptest(tc_generators, test_gen_lon_range);
	tcase_add_ptest(tc_generators, test_gen_geojson);
	tcase_add_ptest(tc_generators, test_gen_geojson_bbox);
	tcase_add_ptest(tc_generators, test_gen_geo_circle);
	tcase_add_ptest(tc_generators, test_gen_geo_circle_r);
	tcase_add_ptest(tc_generators, test_gen_geo_circle_bbox);
	tcase_add_ptest(tc_generators, test_gen_company);
	tcase_add_ptest(tc_generators, test_gen_job_title);
	tcase_add_ptest(tc_generators, test_gen_ipv4);
	tcase_add_ptest(tc_generators, test_gen_ipv6);
	tcase_add_ptest(tc_generators, test_gen_mac);
	tcase_add_ptest(tc_generators, test_gen_url);
	tcase_add_ptest(tc_generators, test_gen_domain);
	tcase_add_ptest(tc_generators, test_gen_uuid);
	tcase_add_ptest(tc_generators, test_gen_word);
	tcase_add_ptest(tc_generators, test_gen_words);
	tcase_add_ptest(tc_generators, test_gen_words_n);
	tcase_add_ptest(tc_generators, test_gen_sentence);
	tcase_add_ptest(tc_generators, test_gen_lorem);
	tcase_add_ptest(tc_generators, test_gen_lorem_n);
	tcase_add_ptest(tc_generators, test_gen_product);
	tcase_add_ptest(tc_generators, test_gen_color);
	tcase_add_ptest(tc_generators, test_gen_credit_card);
	tcase_add_ptest(tc_generators, test_gen_date);
	tcase_add_ptest(tc_generators, test_gen_date_range);
	tcase_add_ptest(tc_generators, test_gen_date_fmt);
	tcase_add_ptest(tc_generators, test_gen_timestamp);
	tcase_add_ptest(tc_generators, test_gen_timestamp_range);
	tcase_add_ptest(tc_generators, test_gen_int);
	tcase_add_ptest(tc_generators, test_gen_int_neg);
	tcase_add_ptest(tc_generators, test_gen_int_single);
	tcase_add_ptest(tc_generators, test_gen_double);
	tcase_add_ptest(tc_generators, test_gen_double_ints);
	tcase_add_ptest(tc_generators, test_gen_pick);
	tcase_add_ptest(tc_generators, test_gen_pick_weighted);
	tcase_add_ptest(tc_generators, test_gen_pick_escaped);
	tcase_add_ptest(tc_generators, test_gen_fmt);
	tcase_add_ptest(tc_generators, test_gen_fmt_int);
	tcase_add_ptest(tc_generators, test_gen_fmt_double);
	tcase_add_ptest(tc_generators, test_gen_fmt_pick);
	tcase_add_ptest(tc_generators, test_gen_fmt_literal);
	tcase_add_ptest(tc_generators, test_gen_spacing);
	tcase_add_ptest(tc_generators, test_gen_empty_parens);
	tcase_add_ptest(tc_generators, test_gen_date_strings);
	tcase_add_ptest(tc_generators, test_gen_date_default_fmt);
	tcase_add_ptest(tc_generators, test_gen_timestamp_strings);
	tcase_add_ptest(tc_generators, test_gen_double_suffix);
	tcase_add_ptest(tc_generators, test_gen_circle_default_bbox);
	tcase_add_ptest(tc_generators, test_gen_name_space);
	tcase_add_ptest(tc_generators, test_gen_named);
	tcase_add_ptest(tc_generators, test_gen_list);
	tcase_add_ptest(tc_generators, test_gen_map);
	tcase_add_ptest(tc_generators, test_gen_map_str_key);
	tcase_add_ptest(tc_generators, test_gen_map_int_key);
	tcase_add_ptest(tc_generators, test_gen_mixed);
	tcase_add_ptest(tc_generators, test_gen_mult);
	tcase_add_ptest(tc_generators, test_gen_named_mult);
	tcase_add_ptest(tc_generators, test_gen_named_mixed);
	tcase_add_ptest(tc_generators, test_gen_named_max_len);
	tcase_add_ptest(tc_generators, test_gen_customer);
	tcase_add_ptest(tc_generators, test_gen_orders);
	tcase_add_ptest(tc_generators, test_gen_feed);
	tcase_add_ptest(tc_generators, test_gen_nested_geo);
	tcase_add_ptest(tc_generators, test_gen_wb);
	tcase_add_ptest(tc_generators, test_gen_wb_repeat);
	tcase_add_ftest(tc_generators, test_gen_unknown);
	tcase_add_ftest(tc_generators, test_gen_bare_at);
	tcase_add_ftest(tc_generators, test_gen_int_no_args);
	tcase_add_ftest(tc_generators, test_gen_int_one_arg);
	tcase_add_ftest(tc_generators, test_gen_int_reversed);
	tcase_add_ftest(tc_generators, test_gen_int_double);
	tcase_add_ftest(tc_generators, test_gen_int_unclosed);
	tcase_add_ftest(tc_generators, test_gen_int_bad_sep);
	tcase_add_ftest(tc_generators, test_gen_words_zero);
	tcase_add_ftest(tc_generators, test_gen_words_neg);
	tcase_add_ftest(tc_generators, test_gen_lorem_zero);
	tcase_add_ftest(tc_generators, test_gen_date_reversed);
	tcase_add_ftest(tc_generators, test_gen_date_bad_month);
	tcase_add_ftest(tc_generators, test_gen_date_bad_day);
	tcase_add_ftest(tc_generators, test_gen_date_one_arg);
	tcase_add_ftest(tc_generators, test_gen_date_empty_fmt);
	tcase_add_ftest(tc_generators, test_gen_date_year_range);
	tcase_add_ftest(tc_generators, test_gen_date_fmt_longest_names);
	tcase_add_ftest(tc_generators, test_gen_pick_empty);
	tcase_add_ftest(tc_generators, test_gen_pick_zero_weight);
	tcase_add_ftest(tc_generators, test_gen_pick_mixed);
	tcase_add_ftest(tc_generators, test_gen_pick_ints);
	tcase_add_ftest(tc_generators, test_gen_fmt_unknown);
	tcase_add_ftest(tc_generators, test_gen_fmt_geo);
	tcase_add_ftest(tc_generators, test_gen_fmt_nested);
	tcase_add_ftest(tc_generators, test_gen_fmt_unterminated);
	tcase_add_ftest(tc_generators, test_gen_fmt_not_string);
	tcase_add_ftest(tc_generators, test_gen_no_args_allowed);
	tcase_add_ftest(tc_generators, test_gen_geojson_two_args);
	tcase_add_ftest(tc_generators, test_gen_geojson_reversed);
	tcase_add_ftest(tc_generators, test_gen_geojson_out_of_range);
	tcase_add_ftest(tc_generators, test_gen_circle_reversed);
	tcase_add_ftest(tc_generators, test_gen_lat_one_arg);
	tcase_add_ftest(tc_generators, test_gen_double_string);
	tcase_add_ftest(tc_generators, test_gen_map_key_geo);
	tcase_add_ftest(tc_generators, test_gen_map_key_double);
	tcase_add_ftest(tc_generators, test_gen_map_key_double2);
	tcase_add_ftest(tc_generators, test_gen_map_key_cardinality);
	tcase_add_ftest(tc_generators, test_gen_map_key_now);
	tcase_add_ftest(tc_generators, test_gen_name_too_long);
	tcase_add_ftest(tc_generators, test_gen_name_repeat_too_long);
	tcase_add_ftest(tc_generators, test_gen_name_duplicate);
	tcase_add_ftest(tc_generators, test_gen_name_empty);
	tcase_add_ftest(tc_generators, test_gen_name_in_list);
	tcase_add_ftest(tc_generators, test_gen_name_in_map);
	tcase_add_test(tc_generators, test_gen_now);
	tcase_add_test(tc_generators, test_gen_has_generators);
	tcase_add_test(tc_generators, test_gen_resolve_names);
	tcase_add_test(tc_generators, test_gen_resolve_clash);
	tcase_add_test(tc_generators, test_gen_bin_accessors);
	tcase_add_test(tc_generators, test_gen_print_truncation);
	tcase_add_test(tc_generators, test_gen_seeded_bins_independent);
	suite_add_tcase(s, tc_generators);

	return s;
}

