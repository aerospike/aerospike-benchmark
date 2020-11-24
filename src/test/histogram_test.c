
#include <check.h>
#include <stdio.h>
#include <time.h>
#include <x86intrin.h>

#include "aerospike/as_random.h"

#include "histogram.h"


#define TEST_SUITE_NAME "histogram"

#define N_INSERTIONS 10000000


/**
 * Any setup code to run each test goes here
 *
 * @return  void  
 */
static void 
setup(void) 
{
}

/**
 * setup's friend. Does the opposite and cleans up the test
 *
 * @return  void  
 */
static void
teardown(void)
{
//do some tearing
}


START_TEST(default_config_test)
{
	histogram h;

	// sequential test

	histogram_init(&h, 3, 100, (rangespec_t[]) {
			{ .upper_bound = 4000,   .bucket_width = 100  },
			{ .upper_bound = 64000,  .bucket_width = 1000 },
			{ .upper_bound = 128000, .bucket_width = 4000 }
			});

	for (delay_t us = 1; us < 128500; us++) {
		histogram_add(&h, us);
	}


	ck_assert_int_eq(h.underflow_cnt, 99);
	ck_assert_int_eq(h.overflow_cnt, 500);

	// 115 buckets in total
	for (size_t i = 0; i < 115; i++) {
		if (i < 39) {
			ck_assert_int_eq(h.buckets[i], 100);
		}
		else if (i < 99) {
			ck_assert_int_eq(h.buckets[i], 1000);
		}
		else {
			ck_assert_int_eq(h.buckets[i], 4000);
		}
	}


	histogram_clear(&h);
}
END_TEST


Suite*
histogram_suite(void)
{
	Suite* s;
	TCase* tc_core;

	s = suite_create("Histogram");

	/* Core test cases */
	tc_core = tcase_create("Core");
	tcase_add_checked_fixture(tc_core, setup, teardown);
	tcase_add_test(tc_core, default_config_test);
	suite_add_tcase(s, tc_core);

	return s;
}

