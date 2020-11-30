
#include <check.h>
#include <stdio.h>
#include <time.h>
#include <x86intrin.h>

#include "aerospike/as_random.h"

#include "histogram.h"


#define TEST_SUITE_NAME "histogram"


static histogram hist;



/**
 * Tests that a simple initialization works
 */
START_TEST(initialization_test)
{
	histogram h;
	ck_assert_msg(histogram_init(&h, 1, 10, (rangespec_t[]) {
				{ .upper_bound = 100, .bucket_width = 10 }
				}) == 0,
			"Histogram basic initialization failed");
}
END_TEST

/**
 * Tests for failure (expected) on out-of-order ranges
 */
START_TEST(range_out_of_order)
{
	histogram h;
	ck_assert_msg(histogram_init(&h, 1, 100, (rangespec_t[]) {
				{ .upper_bound = 10, .bucket_width = 10 }
				}) == -1,
			"Histogram initialization should have failed: "
			"ranges not in ascending order");
}
END_TEST

/**
 * Tests for failure (expected) on range difference of 0 (i.e. the difference
 * between two range bounds is 0). This could cause division-by-zero if the
 * parameters are not checked in the proper order
 */
START_TEST(range_difference_0)
{
	histogram h;
	ck_assert_msg(histogram_init(&h, 1, 100, (rangespec_t[]) {
				{ .upper_bound = 100, .bucket_width = 10 }
				}) == -1,
			"Histogram initialization should have failed: "
			"two ranges have the same value");
}
END_TEST

/**
 * Tests for failure (expected) on a bucket width of 0
 */
START_TEST(bucket_width_0)
{
	histogram h;
	ck_assert_msg(histogram_init(&h, 1, 10, (rangespec_t[]) {
				{ .upper_bound = 100, .bucket_width = 0 }
				}) == -1,
			"Histogram initialization should have failed: "
			"bucket width is 0");
}
END_TEST

/**
 * Tests for failure (expected) on a bucket not evenly dividing a range
 */
START_TEST(bucket_width_does_not_divide_range)
{
	histogram h;
	ck_assert_msg(histogram_init(&h, 1, 10, (rangespec_t[]) {
				{ .upper_bound = 100, .bucket_width = 20 }
				}) == -1,
			"Histogram initialization should have failed: "
			"bucket width does not evenly divide the range");
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
	histogram_init(&hist, 1, 1, (rangespec_t[]) {
			{ .upper_bound = 10,   .bucket_width = 1  }
			});
}

/**
 * setup's friend. Does the opposite and cleans up the test
 *
 * @return  void  
 */
static void
simple_teardown(void)
{
	histogram_free(&hist);
}


/**
 * Checks that the count of each bucket is 0 by default
 */
START_TEST(simple_cleared_on_init)
{
	histogram * h = &hist;
	for (uint32_t bucket_idx = 0; bucket_idx < 9; bucket_idx++) {
		ck_assert_int_eq(histogram_get_count(h, bucket_idx), 0);
	}
}
END_TEST

/**
 * Tests insertion of a single element (shouldn't segfault or anything)
 */
START_TEST(simple_insert_one)
{
	histogram * h = &hist;
	histogram_add(h, 1);
}
END_TEST


/**
 * Tests insertion of a single element and querying of it's bin count
 */
START_TEST(simple_query_one)
{
	histogram * h = &hist;
	histogram_add(h, 1);
	ck_assert_int_eq(histogram_get_count(h, 0), 1);
}
END_TEST


/**
 * Any setup code to run each test goes here
 *
 * @return  void  
 */
static void 
default_setup(void) 
{
	histogram_init(&hist, 3, 100, (rangespec_t[]) {
			{ .upper_bound = 4000,   .bucket_width = 100  },
			{ .upper_bound = 64000,  .bucket_width = 1000 },
			{ .upper_bound = 128000, .bucket_width = 4000 }
			});
}

/**
 * setup's friend. Does the opposite and cleans up the test
 *
 * @return  void  
 */
static void
default_teardown(void)
{
	histogram_free(&hist);
}


// sequential test
START_TEST(default_config_test)
{
	histogram * h = &hist;

	for (delay_t us = 1; us < 128500; us++) {
		histogram_add(h, us);
	}

	ck_assert_int_eq(h->underflow_cnt, 99);
	ck_assert_int_eq(h->overflow_cnt, 500);

	// 115 buckets in total
	for (size_t i = 0; i < 115; i++) {
		if (i < 39) {
			ck_assert_int_eq(h->buckets[i], 100);
		}
		else if (i < 99) {
			ck_assert_int_eq(h->buckets[i], 1000);
		}
		else {
			ck_assert_int_eq(h->buckets[i], 4000);
		}
	}

	//histogram_clear(&h);
}
END_TEST


/*
START_TEST(sequential_print_check)
{
	histogram * h = &hist;

	for (delay_t us = 1; us < 361; us++) {
		if (us < 180 || us >= 220) {
			histogram_add(h, us);
		}
	}

	ck_assert_int_eq(h->underflow_cnt, 19);
	ck_assert_int_eq(h->overflow_cnt,  1);

	// 14 buckets in total
	for (size_t i = 0; i < 14; i++) {
		if (i < 8) {
			ck_assert_int_eq(h->buckets[i], 20);
		}
		else if (i == 8) {
			ck_assert_int_eq(h->buckets[i], 0);
		}
		else if (i == 9) {
			ck_assert_int_eq(h->buckets[i], 20);
		}
		else {
			ck_assert_int_eq(h->buckets[i], 30);
		}
	}

	FILE * out_file = tmpfile();

	histogram_print(h, 1, out_file);
	fseek(out_file, 0, SEEK_SET);

	int delay;
	uint64_t total_cnt;
	ck_assert_int_eq(fscanf(out_file, "%*3s %*3s %*d %*2d:%*2d:%*2d %*4d, %ds, %lu",
				&delay, &total_cnt), 2);

	ck_assert_int_eq(total_cnt, 320);

	uint32_t bucket, cnt;
	// underflow bucket
	ck_assert_int_eq(fscanf(out_file, ", %u:%u", &bucket, &cnt), 2);
	ck_assert_int_eq(bucket, 0);
	ck_assert_int_eq(cnt, 19);

	// one of the buckets should be skipped
	for (size_t i = 0; i < 13; i++) {
		ck_assert_int_eq(fscanf(out_file, ", %u:%u", &bucket, &cnt), 2);
		if (i < 8) {
			ck_assert_int_eq(bucket, 20 * (i + 1));
		}
		else {
			ck_assert_int_eq(bucket, 30 * (i - 1));
		}
		ck_assert_int_eq(cnt, h->buckets[i + (i >= 8)]);
	}

	// overflow bucket
	ck_assert_int_eq(fscanf(out_file, ", %u:%u", &bucket, &cnt), 2);
	ck_assert_int_eq(bucket, 360);
	ck_assert_int_eq(cnt, 1);


	char buf[1];
	ck_assert_int_eq(fread(buf, 1, sizeof(buf), out_file), 1);
	ck_assert_int_eq(buf[0], '\n');

	fclose(out_file);

}
END_TEST*/


Suite*
histogram_suite(void)
{
	Suite* s;
	TCase* tc_core;
	TCase* tc_simple;
	TCase* tc_default;

	s = suite_create("Histogram");

	/* Core test cases */
	tc_core = tcase_create("Core");
	tcase_add_test(tc_core, initialization_test);
	tcase_add_test(tc_core, range_out_of_order);
	tcase_add_test(tc_core, range_difference_0);
	tcase_add_test(tc_core, bucket_width_0);
	tcase_add_test(tc_core, bucket_width_does_not_divide_range);
	suite_add_tcase(s, tc_core);

	tc_simple = tcase_create("Simple");
	tcase_add_checked_fixture(tc_simple, simple_setup, simple_teardown);
	tcase_add_test(tc_simple, simple_cleared_on_init);
	tcase_add_test(tc_simple, simple_insert_one);
	tcase_add_test(tc_simple, simple_query_one);
	suite_add_tcase(s, tc_simple);

	tc_default = tcase_create("Defualt Config");
	tcase_add_checked_fixture(tc_default, default_setup, default_teardown);
	tcase_add_test(tc_default, default_config_test);
	//tcase_add_test(tc_default, sequential_print_check);
	suite_add_tcase(s, tc_default);

	return s;
}

