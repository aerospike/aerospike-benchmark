
#include <check.h>
#include <stdio.h>
#include <time.h>

#include <hdr_histogram/hdr_histogram.h>


#define TEST_SUITE_NAME "histogram"


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

Suite*
hdr_histogram_suite(void)
{
	Suite* s;
	TCase* tc_core;

	s = suite_create("HDR Histogram");

	/* Core test cases */
	tc_core = tcase_create("Core");
	tcase_add_test(tc_core, initialization_test);
	suite_add_tcase(s, tc_core);

	return s;
}

