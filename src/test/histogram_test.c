
#include <check.h>
#include <stdio.h>
#include <time.h>
#include <x86intrin.h>

#include "aerospike/as_random.h"

#include "histogram.h"


#define TEST_SUITE_NAME "histogram"


static histogram hist;


// format string to consume a UTC time in scanf
#define UTC_DATE_FMT "%*3s %*3s %*2d %*02d:%*02d:%*02d %*4d"


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
	ck_assert_int_eq(h->underflow_cnt, 0);
	ck_assert_int_eq(h->overflow_cnt, 0);
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
 * Tests insertion of a single element and querying of it's bin count
 */
START_TEST(simple_query_total)
{
	histogram * h = &hist;
	histogram_add(h, 1);
	ck_assert_int_eq(histogram_calc_total(h), 1);
}
END_TEST


/**
 * Tests insertion of a single element below the histogram range
 */
START_TEST(simple_query_below_range)
{
	histogram * h = &hist;
	histogram_add(h, 0);
	ck_assert_int_eq(h->underflow_cnt, 1);
}
END_TEST


/**
 * Tests insertion of a single element above the histogram range
 */
START_TEST(simple_query_above_range)
{
	histogram * h = &hist;
	histogram_add(h, 10);
	ck_assert_int_eq(h->overflow_cnt, 1);
}
END_TEST


/**
 * Tests clearing the histogram
 */
START_TEST(simple_clear)
{
	histogram * h = &hist;
	histogram_add(h, 2);
	ck_assert_int_eq(histogram_get_count(h, 1), 1);
	histogram_clear(h);
	ck_assert_int_eq(histogram_get_count(h, 1), 0);
}
END_TEST


/**
 * Tests printing a histogram with only one element
 */
START_TEST(simple_print)
{
	histogram * h = &hist;
	histogram_add(h, 3);

	FILE * out_file = tmpfile();

	histogram_print(h, 1, out_file);
	fseek(out_file, 0, SEEK_SET);

	int bucket, cnt;
	ck_assert_int_eq(fscanf(out_file, UTC_DATE_FMT ", 1s, 1, %d:%d\n", &bucket, &cnt), 2);
	ck_assert_int_eq(bucket, 3);
	ck_assert_int_eq(cnt, 1);

	// make sure that there is nothing left that was printed
	long pos = ftell(out_file);
	fseek(out_file, 0L, SEEK_END);
	long end = ftell(out_file);
	ck_assert_int_eq(pos, end);

	fclose(out_file);
}
END_TEST


/**
 * Tests that the print clear method correctly prints the single bin and also
 * clears that bin
 */
START_TEST(simple_print_clear)
{
	histogram * h = &hist;
	histogram_add(h, 3);

	FILE * out_file = tmpfile();

	histogram_print_clear(h, 1, out_file);
	fseek(out_file, 0, SEEK_SET);

	// should get the exact same output as simple_print
	int bucket, cnt;
	ck_assert_int_eq(fscanf(out_file, UTC_DATE_FMT ", 1s, 1, %d:%d", &bucket, &cnt), 2);
	ck_assert_int_eq(bucket, 3);
	ck_assert_int_eq(cnt, 1);

	fclose(out_file);

	ck_assert_int_eq(histogram_get_count(h, 2), 0);
}
END_TEST


/**
 * Tests printing the name of a histogram
 */
START_TEST(simple_print_name)
{
	const static char name[] = "test_histogram_name";
	histogram * h = &hist;

	histogram_set_name(h, name);

	FILE * out_file = tmpfile();

	histogram_print(h, 1, out_file);
	fseek(out_file, 0, SEEK_SET);

	char buf[sizeof(name)];
	ck_assert_int_eq(fread(buf, 1, sizeof(buf) - 1, out_file), sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	ck_assert_int_eq(strncmp(buf, name, sizeof(buf) - 1), 0);

	fclose(out_file);
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

	// insert a bunch of elements
	for (delay_t us = 1; us < 128500; us++) {
		histogram_add(&hist, us);
	}
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


/*
 * verifies the underflow count in the default histgoram setup
 */
START_TEST(default_underflow_cnt)
{
	histogram * h = &hist;
	ck_assert_int_eq(h->underflow_cnt, 99);
}

/*
 * verifies the overflow count in the default histgoram setup
 */
START_TEST(default_overflow_cnt)
{
	histogram * h = &hist;
	ck_assert_int_eq(h->overflow_cnt, 500);
}

/*
 * verifies bin counts in the first range of bins
 */
START_TEST(default_range_1_cnt)
{
	histogram * h = &hist;
	for (int i = 0; i < 39; i++) {
		ck_assert_int_eq(histogram_get_count(h, i), 100);
	}
}

/*
 * verifies bin counts in the second range of bins
 */
START_TEST(default_range_2_cnt)
{
	histogram * h = &hist;
	for (int i = 39; i < 99; i++) {
		ck_assert_int_eq(histogram_get_count(h, i), 1000);
	}
}

/*
 * verifies bin counts in the third range of bins
 */
START_TEST(default_range_3_cnt)
{
	histogram * h = &hist;
	for (int i = 99; i < 115; i++) {
		ck_assert_int_eq(histogram_get_count(h, i), 4000);
	}
}

/*
 * verifies clearing the histogram
 */
START_TEST(default_clear)
{
	histogram * h = &hist;
	histogram_clear(h);
	for (int i = 0; i < 115; i++) {
		ck_assert_int_eq(histogram_get_count(h, i), 0);
	}
	ck_assert_int_eq(h->underflow_cnt, 0);
	ck_assert_int_eq(h->overflow_cnt, 0);
}

/*
 * verifies that print_clear actually clears
 */
START_TEST(default_print_clear_clears)
{
	histogram * h = &hist;

	FILE * tmp = fopen("/dev/null", "w");
	histogram_print_clear(h, 1, tmp);
	fclose(tmp);
	for (int i = 0; i < 115; i++) {
		ck_assert_int_eq(histogram_get_count(h, i), 0);
	}
	ck_assert_int_eq(h->underflow_cnt, 0);
	ck_assert_int_eq(h->overflow_cnt, 0);
}

/*
 * verifies calc_total
 */
START_TEST(default_calc_total)
{
	histogram * h = &hist;

	ck_assert_int_eq(histogram_calc_total(h), 128499);
}


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
	tcase_add_test(tc_simple, simple_query_total);
	tcase_add_test(tc_simple, simple_query_below_range);
	tcase_add_test(tc_simple, simple_query_above_range);
	tcase_add_test(tc_simple, simple_clear);
	tcase_add_test(tc_simple, simple_print);
	tcase_add_test(tc_simple, simple_print_clear);
	tcase_add_test(tc_simple, simple_print_name);
	suite_add_tcase(s, tc_simple);

	tc_default = tcase_create("Defualt Config");
	tcase_add_checked_fixture(tc_default, default_setup, default_teardown);
	tcase_add_test(tc_default, default_underflow_cnt);
	tcase_add_test(tc_default, default_overflow_cnt);
	tcase_add_test(tc_default, default_range_1_cnt);
	tcase_add_test(tc_default, default_range_2_cnt);
	tcase_add_test(tc_default, default_range_3_cnt);
	tcase_add_test(tc_default, default_clear);
	tcase_add_test(tc_default, default_print_clear_clears);
	tcase_add_test(tc_default, default_calc_total);
	//tcase_add_test(tc_default, sequential_print_check);
	suite_add_tcase(s, tc_default);

	return s;
}

