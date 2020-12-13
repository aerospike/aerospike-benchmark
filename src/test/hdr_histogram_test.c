
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
	hdr_close(&hist);
}






/**
 * Tests that a simple initialization works
 */
START_TEST(initialization_test)
{
	ck_assert_msg(hdr_init(1, 1000000, 3, &h) == 0,
			"HDR Histogram basic initialization failed");
}
END_TEST


Suite*
hdr_histogram_suite(void)
{
	Suite* s;
	TCase* tc_core;

	s = suite_create(TEST_SUITE_NAME);

	/* Core test cases */
	tc_core = tcase_create("Core");
	tcase_add_test(tc_core, initialization_test);
	suite_add_tcase(s, tc_core);

	return s;
}

