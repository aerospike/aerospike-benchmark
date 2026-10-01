#include <check.h>
#include <stdio.h>

#include <benchmark.h>
#include <workload.h>


static void
simple_setup(void)
{
	freopen("/dev/null", "w", stderr);
}

static void
simple_teardown(void)
{
}

#define DEFINE_CDT_PARSE(test_name, str, mode, pct, cap, k) \
START_TEST(test_name) \
{ \
	workload_t w; \
	ck_assert_int_eq(parse_workload_type(&w, str), 0); \
	ck_assert_int_eq(w.type, WORKLOAD_TYPE_CDT); \
	ck_assert_int_eq(w.cdt_mode, mode); \
	ck_assert_float_eq(w.read_pct, pct); \
	ck_assert_uint_eq(w.cdt_cap, cap); \
	ck_assert_uint_eq(w.cdt_read_k, k); \
	ck_assert(workload_is_random(&w)); \
	ck_assert(workload_is_infinite(&w)); \
	ck_assert(!workload_contains_udfs(&w)); \
	ck_assert(!workload_contains_deletes(&w)); \
} \
END_TEST

#define DEFINE_CDT_FAIL(test_name, str) \
START_TEST(test_name) \
{ \
	workload_t w; \
	ck_assert_int_ne(parse_workload_type(&w, str), 0); \
} \
END_TEST

DEFINE_CDT_PARSE(cdt_default, "C", CDT_MODE_PUT, 50, 0, 10);
DEFINE_CDT_PARSE(cdt_pct, "C,80", CDT_MODE_PUT, 80, 0, 10);
DEFINE_CDT_PARSE(cdt_pct_cap, "C,80,100", CDT_MODE_PUT, 80, 100, 10);
DEFINE_CDT_PARSE(cdt_all, "C,80,100,25", CDT_MODE_PUT, 80, 100, 25);
DEFINE_CDT_PARSE(cdt_float_pct, "C,12.5,7,3", CDT_MODE_PUT, 12.5, 7, 3);
DEFINE_CDT_PARSE(cdt_incr_default, "CI", CDT_MODE_INCR, 50, 0, 10);
DEFINE_CDT_PARSE(cdt_incr_all, "CI,10,1000,10", CDT_MODE_INCR, 10, 1000, 10);
DEFINE_CDT_PARSE(cdt_key, "CK,90,50", CDT_MODE_KEY, 90, 50, 10);
DEFINE_CDT_PARSE(cdt_write_only, "C,0", CDT_MODE_PUT, 0, 0, 10);
DEFINE_CDT_PARSE(cdt_read_only, "C,100", CDT_MODE_PUT, 100, 0, 10);
DEFINE_CDT_PARSE(cdt_zero_cap, "C,50,0,1", CDT_MODE_PUT, 50, 0, 1);

DEFINE_CDT_FAIL(cdt_too_many, "C,80,100,25,1");
DEFINE_CDT_FAIL(cdt_not_number, "C,abc");
DEFINE_CDT_FAIL(cdt_pct_too_high, "C,101");
DEFINE_CDT_FAIL(cdt_pct_negative, "C,-1");
DEFINE_CDT_FAIL(cdt_cap_negative, "C,50,-1");
DEFINE_CDT_FAIL(cdt_cap_float, "C,50,1.5");
DEFINE_CDT_FAIL(cdt_k_zero, "C,50,10,0");
DEFINE_CDT_FAIL(cdt_bad_mode, "CX");
DEFINE_CDT_FAIL(cdt_trailing_comma, "C,");
DEFINE_CDT_FAIL(cdt_trailing_comma2, "C,50,");
DEFINE_CDT_FAIL(cdt_cap_overflow, "C,50,4294967296");
DEFINE_CDT_FAIL(cdt_garbage_after, "CI,50x");

START_TEST(cdt_reads_writes_flags)
{
	workload_t w;
	ck_assert_int_eq(parse_workload_type(&w, "C,0"), 0);
	ck_assert(!workload_contains_reads(&w));
	ck_assert(workload_contains_writes(&w));

	ck_assert_int_eq(parse_workload_type(&w, "C,100"), 0);
	ck_assert(workload_contains_reads(&w));
	ck_assert(!workload_contains_writes(&w));

	ck_assert_int_eq(parse_workload_type(&w, "CK,40"), 0);
	ck_assert(workload_contains_reads(&w));
	ck_assert(workload_contains_writes(&w));
}
END_TEST

START_TEST(existing_workloads_unchanged)
{
	workload_t w;
	ck_assert_int_eq(parse_workload_type(&w, "RU,80"), 0);
	ck_assert_int_eq(w.type, WORKLOAD_TYPE_RU);
	ck_assert_int_eq(parse_workload_type(&w, "RUD,20,40"), 0);
	ck_assert_int_eq(w.type, WORKLOAD_TYPE_RUD);
	ck_assert_int_eq(parse_workload_type(&w, "I"), 0);
	ck_assert_int_eq(w.type, WORKLOAD_TYPE_I);
	ck_assert_int_eq(parse_workload_type(&w, "DB"), 0);
	ck_assert_int_eq(w.type, WORKLOAD_TYPE_D);
	ck_assert_int_ne(parse_workload_type(&w, "X"), 0);
}
END_TEST


Suite*
workload_parse_suite(void)
{
	Suite* s;
	TCase* tc_cdt;

	s = suite_create("Workload Parse");

	tc_cdt = tcase_create("CDT");
	tcase_add_checked_fixture(tc_cdt, simple_setup, simple_teardown);
	tcase_add_test(tc_cdt, cdt_default);
	tcase_add_test(tc_cdt, cdt_pct);
	tcase_add_test(tc_cdt, cdt_pct_cap);
	tcase_add_test(tc_cdt, cdt_all);
	tcase_add_test(tc_cdt, cdt_float_pct);
	tcase_add_test(tc_cdt, cdt_incr_default);
	tcase_add_test(tc_cdt, cdt_incr_all);
	tcase_add_test(tc_cdt, cdt_key);
	tcase_add_test(tc_cdt, cdt_write_only);
	tcase_add_test(tc_cdt, cdt_read_only);
	tcase_add_test(tc_cdt, cdt_zero_cap);
	tcase_add_test(tc_cdt, cdt_too_many);
	tcase_add_test(tc_cdt, cdt_not_number);
	tcase_add_test(tc_cdt, cdt_pct_too_high);
	tcase_add_test(tc_cdt, cdt_pct_negative);
	tcase_add_test(tc_cdt, cdt_cap_negative);
	tcase_add_test(tc_cdt, cdt_cap_float);
	tcase_add_test(tc_cdt, cdt_k_zero);
	tcase_add_test(tc_cdt, cdt_bad_mode);
	tcase_add_test(tc_cdt, cdt_trailing_comma);
	tcase_add_test(tc_cdt, cdt_trailing_comma2);
	tcase_add_test(tc_cdt, cdt_cap_overflow);
	tcase_add_test(tc_cdt, cdt_garbage_after);
	tcase_add_test(tc_cdt, cdt_reads_writes_flags);
	tcase_add_test(tc_cdt, existing_workloads_unchanged);
	suite_add_tcase(s, tc_cdt);

	return s;
}
