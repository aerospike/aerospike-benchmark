
#include <check.h>
#include <stdio.h>
#include <time.h>

#include <hdr_histogram/hdr_histogram.h>


#define TEST_SUITE_NAME "HDR Histogram"


static struct hdr_histogram * h;


/**
 * Tests that a simple initialization works
 */
START_TEST(initialization_test)
{
	ck_assert_msg(hdr_init(1, 1000000, 3, &h) == 0,
			"HDR Histogram basic initialization failed");
}
END_TEST


/**
 * Any setup code to run each test goes here
 *
 * @return  void  
 */
static void 
simple_setup(void) 
{
	hdr_init(1, 1000000, 3, &h);
}

/**
 * setup's friend. Does the opposite and cleans up the test
 *
 * @return  void  
 */
static void
simple_teardown(void)
{
	hdr_close(h);
}


/**
 * Tests that a simple initialization works
 */
START_TEST(simple_insert_one)
{
	hdr_record_value(h, 1);
	ck_assert_int_eq(hdr_value_at_percentile(h, 0), 1);
	ck_assert_int_eq(hdr_value_at_percentile(h, 99.9), 1);
}
END_TEST


/**
 * Tests that a simple initialization works
 */
START_TEST(simple_insert_two)
{
	hdr_record_value(h, 1);
	hdr_record_value(h, 2);
	ck_assert_int_eq(hdr_value_at_percentile(h, 0), 1);
	ck_assert_int_eq(hdr_value_at_percentile(h, 74), 1);
	ck_assert_int_eq(hdr_value_at_percentile(h, 75), 2);
}
END_TEST


Suite*
hdr_histogram_suite(void)
{
	Suite* s;
	TCase* tc_core;
	TCase* tc_simple;

	s = suite_create(TEST_SUITE_NAME);

	/* Core test cases */
	tc_core = tcase_create("Core");
	tcase_add_test(tc_core, initialization_test);
	suite_add_tcase(s, tc_core);

	tc_simple = tcase_create("Simple");
	tcase_add_checked_fixture(tc_simple, simple_setup, simple_teardown);
	tcase_add_test(tc_simple, simple_insert_one);
	tcase_add_test(tc_simple, simple_insert_two);
	suite_add_tcase(s, tc_simple);

	return s;
}

