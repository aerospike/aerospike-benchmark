
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "histogram.h"


#define TEST_SUITE_NAME "histogram"


static histogram_t hist;

// format string to consume a UTC time in scanf
#define UTC_DATE_FMT "%*4d-%*02d-%*02dT%*02d:%*02d:%*02dZ"


/**
 * Tests that a simple initialization works
 */
START_TEST(initialization_test)
{
	histogram_t h;
	ck_assert_msg(histogram_init(&h, 1, 10, (rangespec_t[]) {
				{ .upper_bound = 100, .bucket_width = 10 }
				}) == 0,
			"Histogram basic initialization failed");
	histogram_free(&h);
}
END_TEST

/**
 * Tests for failure (expected) on out-of-order ranges
 */
START_TEST(range_out_of_order)
{
	histogram_t h;
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
	histogram_t h;
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
	histogram_t h;
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
	histogram_t h;
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
	histogram_t* h = &hist;
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
	histogram_t* h = &hist;
	histogram_add(h, 1);
}
END_TEST


/**
 * Tests insertion of a single element and querying of it's bin count
 */
START_TEST(simple_query_one)
{
	histogram_t* h = &hist;
	histogram_add(h, 1);
	ck_assert_int_eq(histogram_get_count(h, 0), 1);
}
END_TEST


/**
 * Tests insertion of a single element and querying of it's bin count
 */
START_TEST(simple_query_total)
{
	histogram_t* h = &hist;
	histogram_add(h, 1);
	ck_assert_int_eq(histogram_calc_total(h), 1);
}
END_TEST


/**
 * Tests insertion of a single element below the histogram range
 */
START_TEST(simple_query_below_range)
{
	histogram_t* h = &hist;
	histogram_add(h, 0);
	ck_assert_int_eq(h->underflow_cnt, 1);
}
END_TEST


/**
 * Tests insertion of a single element above the histogram range
 */
START_TEST(simple_query_above_range)
{
	histogram_t* h = &hist;
	histogram_add(h, 10);
	ck_assert_int_eq(h->overflow_cnt, 1);
}
END_TEST


/**
 * Tests clearing the histogram
 */
START_TEST(simple_clear)
{
	histogram_t* h = &hist;
	histogram_add(h, 2);
	ck_assert_int_eq(histogram_get_count(h, 1), 1);
	histogram_clear(h);
	ck_assert_int_eq(histogram_get_count(h, 1), 0);
}
END_TEST


/**
 * Tests that changing the name of the historam makes a duplicate of the string
 * passed
 */
START_TEST(simple_rename)
{
	histogram_t* h = &hist;
	const static char name[] = "name 1";

	histogram_set_name(h, name);
	char* hname = h->name;

	ck_assert_msg(hname != name, "Histogram renaming did not duplicate the string");
	ck_assert_int_eq(strcmp(hname, name), 0);
}
END_TEST


/**
 * Tests that changing the name twice replaces the first name
 */
START_TEST(simple_rename_twice)
{
	histogram_t* h = &hist;

	histogram_set_name(h, "name 1");
	histogram_set_name(h, "name 2");
	ck_assert_int_eq(strcmp(h->name, "name 2"), 0);
}
END_TEST


/**
 * Tests printing a histogram with only one element
 */
START_TEST(simple_print)
{
	histogram_t* h = &hist;
	histogram_add(h, 3);

	FILE* out_file = tmpfile();

	histogram_print(h, 1000000, out_file);
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
 * Tests printing a histogram with only one element below the minimum range of
 * the histogram
 */
START_TEST(simple_print_lowb)
{
	histogram_t* h = &hist;
	histogram_add(h, 0);

	FILE* out_file = tmpfile();

	histogram_print(h, 1000000, out_file);
	fseek(out_file, 0, SEEK_SET);

	int bucket, cnt;
	ck_assert_int_eq(fscanf(out_file, UTC_DATE_FMT ", 1s, 1, %d:%d\n", &bucket, &cnt), 2);
	ck_assert_int_eq(bucket, 0);
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
 * Tests printing a histogram with only one element above the maximum range of
 * the histogram
 */
START_TEST(simple_print_upb)
{
	histogram_t* h = &hist;
	histogram_add(h, 20);

	FILE* out_file = tmpfile();

	histogram_print(h, 1000000, out_file);
	fseek(out_file, 0, SEEK_SET);

	int bucket, cnt;
	ck_assert_int_eq(fscanf(out_file, UTC_DATE_FMT ", 1s, 1, %d:%d\n", &bucket, &cnt), 2);
	ck_assert_int_eq(bucket, 10);
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
	histogram_t* h = &hist;
	histogram_add(h, 3);

	FILE* out_file = tmpfile();

	histogram_print_clear(h, 1000000, out_file);
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
	histogram_t* h = &hist;

	histogram_set_name(h, name);

	FILE* out_file = tmpfile();

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
 * Tests that name is included in output of print_info
 */
START_TEST(simple_print_info_name)
{
	const static char name[] = "test_histogram_name";
	histogram_t* h = &hist;

	histogram_set_name(h, name);

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	char buf[sizeof(name)];
	ck_assert_int_eq(fread(buf, 1, sizeof(buf) - 1, out_file), sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	ck_assert_int_eq(strncmp(buf, name, sizeof(buf) - 1), 0);

	fclose(out_file);
}
END_TEST


/**
 * Tests that the total number of buckets is included in the info
 */
START_TEST(simple_print_info_size)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	// go through line-by-line and search for a line contaning the number of
	// buckets

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "buckets") != NULL) {
			ck_assert_msg(strstr(buf, "9") != NULL, "the number of "
					"buckets is not correct");
			free(buf);
			fclose(out_file);
			return;
		}
	}
	free(buf);

	ck_assert_msg(0, "the number of buckets was not found");
}
END_TEST


/**
 * Tests that the lower bound on the histogram range is included in the info
 */
START_TEST(simple_print_info_range_lowb)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	// go through line-by-line and search for a line contaning the number of
	// buckets

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "range") != NULL &&
				strcasestr(buf, "min") != NULL) {
			ck_assert_msg(strstr(buf, "1us") != NULL, "the lower bound on the "
					"histogram range is incorrect");
			free(buf);
			fclose(out_file);
			return;
		}
		else if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range") != NULL) {
			ck_assert_msg(0, "histogram range upper bound not found before the "
					"first bucket range descriptor");
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range lower bound not found");
}
END_TEST


/**
 * Tests that the upper bound on the histogram range is included in the info
 */
START_TEST(simple_print_info_range_upb)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	// go through line-by-line and search for a line contaning the number of
	// buckets

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "range") != NULL &&
				strcasestr(buf, "max") != NULL) {
			ck_assert_msg(strstr(buf, "10us") != NULL, "the upper bound on the "
					"histogram range is incorrect");
			free(buf);
			fclose(out_file);
			return;
		}
		else if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range") != NULL) {
			ck_assert_msg(0, "histogram range upper bound not found before the "
					"first bucket range descriptor");
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range upper bound not found");
}
END_TEST


/**
 * Tests that the lower bound on the first bucket range is included in the info
 */
START_TEST(simple_print_info_range_0_lowb)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	// go through line-by-line and search for a line contaning the number of
	// buckets

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range 0") != NULL) {

			while (getline(&buf, &size, out_file) != -1) {
				
				if (strcasestr(buf, "range") != NULL &&
						strcasestr(buf, "min") != NULL) {
					ck_assert_msg(strstr(buf, "1us") != NULL, "range 0 lower "
							"bound not correct");
					free(buf);
					fclose(out_file);
					return;
				}
				else if (strcasestr(buf, "bucket") != NULL &&
						strcasestr(buf, "range") != NULL) {
					ck_assert_msg(0, "a second bucket range found when not "
							"expected");
				}
			}
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range 0 lower bound not found");
}
END_TEST


/**
 * Tests that the upper bound on the first bucket range is included in the info
 */
START_TEST(simple_print_info_range_0_upb)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	// go through line-by-line and search for a line contaning the number of
	// buckets

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range 0") != NULL) {

			while (getline(&buf, &size, out_file) != -1) {
				
				if (strcasestr(buf, "range") != NULL &&
						strcasestr(buf, "max") != NULL) {
					ck_assert_msg(strstr(buf, "10us") != NULL, "range 0 upper "
							"bound not correct");
					free(buf);
					fclose(out_file);
					return;
				}
				else if (strcasestr(buf, "bucket") != NULL &&
						strcasestr(buf, "range") != NULL) {
					ck_assert_msg(0, "a second bucket range found when not "
							"expected");
				}
			}
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range 0 upper bound not found");
}
END_TEST


/**
 * Tests that the width of the first bucket range is included in the info
 */
START_TEST(simple_print_info_range_0_width)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	// go through line-by-line and search for a line contaning the number of
	// buckets

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range 0") != NULL) {

			while (getline(&buf, &size, out_file) != -1) {
				
				if (strcasestr(buf, "width") != NULL) {
					ck_assert_msg(strstr(buf, "1us") != NULL, "range 0 width "
							"not correct");
					free(buf);
					fclose(out_file);
					return;
				}
				else if (strcasestr(buf, "bucket") != NULL &&
						strcasestr(buf, "range") != NULL) {
					ck_assert_msg(0, "a second bucket range found when not "
							"expected");
				}
			}
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range 0 width not found");
}
END_TEST


/**
 * Tests that the number of buckets in the first bucket range is included in
 * the info
 */
START_TEST(simple_print_info_range_0_n_buckets)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	// go through line-by-line and search for a line contaning the number of
	// buckets

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range 0") != NULL) {

			while (getline(&buf, &size, out_file) != -1) {
				
				if (strcasestr(buf, "buckets") != NULL) {
					ck_assert_msg(strstr(buf, "9") != NULL, "range 0 num "
							"buckets not correct");
					free(buf);
					fclose(out_file);
					return;
				}
				else if (strcasestr(buf, "bucket") != NULL &&
						strcasestr(buf, "range") != NULL) {
					ck_assert_msg(0, "a second bucket range found when not "
							"expected");
				}
			}
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range 0 num buckets not found");
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
	histogram_t* h = &hist;
	ck_assert_int_eq(h->underflow_cnt, 99);
}

/*
 * verifies the overflow count in the default histgoram setup
 */
START_TEST(default_overflow_cnt)
{
	histogram_t* h = &hist;
	ck_assert_int_eq(h->overflow_cnt, 500);
}

/*
 * verifies bin counts in the first range of bins
 */
START_TEST(default_range_1_cnt)
{
	histogram_t* h = &hist;
	for (int i = 0; i < 39; i++) {
		ck_assert_int_eq(histogram_get_count(h, i), 100);
	}
}

/*
 * verifies bin counts in the second range of bins
 */
START_TEST(default_range_2_cnt)
{
	histogram_t* h = &hist;
	for (int i = 39; i < 99; i++) {
		ck_assert_int_eq(histogram_get_count(h, i), 1000);
	}
}

/*
 * verifies bin counts in the third range of bins
 */
START_TEST(default_range_3_cnt)
{
	histogram_t* h = &hist;
	for (int i = 99; i < 115; i++) {
		ck_assert_int_eq(histogram_get_count(h, i), 4000);
	}
}

/*
 * verifies clearing the histogram
 */
START_TEST(default_clear)
{
	histogram_t* h = &hist;
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
	histogram_t* h = &hist;

	FILE* tmp = fopen("/dev/null", "w");
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
	histogram_t* h = &hist;

	ck_assert_int_eq(histogram_calc_total(h), 128499);
}


/**
 * Tests that the total number of buckets is included in the info
 */
START_TEST(default_print_info_size)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	// go through line-by-line and search for a line contaning the number of
	// buckets

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "buckets") != NULL) {
			ck_assert_msg(strstr(buf, "115") != NULL, "the number of "
					"buckets is not correct");
			free(buf);
			fclose(out_file);
			return;
		}
	}
	free(buf);

	ck_assert_msg(0, "the number of buckets was not found");
}
END_TEST


/**
 * Tests that the lower bound on the histogram range is included in the info
 */
START_TEST(default_print_info_range_lowb)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "range") != NULL &&
				strcasestr(buf, "min") != NULL) {
			ck_assert_msg(strstr(buf, "100us") != NULL, "the lower bound on the "
					"histogram range is incorrect");
			free(buf);
			fclose(out_file);
			return;
		}
		else if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range") != NULL) {
			ck_assert_msg(0, "histogram range upper bound not found before the "
					"first bucket range descriptor");
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range lower bound not found");
}
END_TEST


/**
 * Tests that the upper bound on the histogram range is included in the info
 */
START_TEST(default_print_info_range_upb)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "range") != NULL &&
				strcasestr(buf, "max") != NULL) {
			ck_assert_msg(strstr(buf, "128000us") != NULL, "the upper bound on the "
					"histogram range is incorrect");
			free(buf);
			fclose(out_file);
			return;
		}
		else if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range") != NULL) {
			ck_assert_msg(0, "histogram range upper bound not found before the "
					"first bucket range descriptor");
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range upper bound not found");
}
END_TEST


/**
 * Tests that the lower bound on the first bucket range is included in the info
 */
START_TEST(default_print_info_range_0_lowb)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range 0") != NULL) {

			while (getline(&buf, &size, out_file) != -1) {
				
				if (strcasestr(buf, "range") != NULL &&
						strcasestr(buf, "min") != NULL) {
					ck_assert_msg(strstr(buf, "100us") != NULL, "range 0 lower "
							"bound not correct");
					free(buf);
					fclose(out_file);
					return;
				}
				else if (strcasestr(buf, "bucket") != NULL &&
						strcasestr(buf, "range") != NULL) {
					ck_assert_msg(0, "histogram range 0 lower bound not found");
				}
			}
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range 0 lower bound not found");
}
END_TEST


/**
 * Tests that the lower bound on the second bucket range is included in the info
 */
START_TEST(default_print_info_range_1_lowb)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range 1") != NULL) {

			while (getline(&buf, &size, out_file) != -1) {
				
				if (strcasestr(buf, "range") != NULL &&
						strcasestr(buf, "min") != NULL) {
					ck_assert_msg(strstr(buf, "4000us") != NULL, "range 1 lower "
							"bound not correct");
					free(buf);
					fclose(out_file);
					return;
				}
				else if (strcasestr(buf, "bucket") != NULL &&
						strcasestr(buf, "range") != NULL) {
					ck_assert_msg(0, "histogram range 1 lower bound not found");
				}
			}
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range 1 lower bound not found");
}
END_TEST


/**
 * Tests that the lower bound on the third bucket range is included in the info
 */
START_TEST(default_print_info_range_2_lowb)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range 2") != NULL) {

			while (getline(&buf, &size, out_file) != -1) {
				
				if (strcasestr(buf, "range") != NULL &&
						strcasestr(buf, "min") != NULL) {
					ck_assert_msg(strstr(buf, "64000us") != NULL, "range 2 lower "
							"bound not correct");
					free(buf);
					fclose(out_file);
					return;
				}
				else if (strcasestr(buf, "bucket") != NULL &&
						strcasestr(buf, "range") != NULL) {
					ck_assert_msg(0, "histogram range 2 lower bound not found");
				}
			}
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range 2 lower bound not found");
}
END_TEST


/**
 * Tests that the upper bound on the first bucket range is included in the info
 */
START_TEST(default_print_info_range_0_upb)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range 0") != NULL) {

			while (getline(&buf, &size, out_file) != -1) {
				
				if (strcasestr(buf, "range") != NULL &&
						strcasestr(buf, "max") != NULL) {
					ck_assert_msg(strstr(buf, "4000us") != NULL, "range 0 upper "
							"bound not correct");
					free(buf);
					fclose(out_file);
					return;
				}
				else if (strcasestr(buf, "bucket") != NULL &&
						strcasestr(buf, "range") != NULL) {
					ck_assert_msg(0, "histogram range 0 upper bound not found");
				}
			}
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range 0 upper bound not found");
}
END_TEST


/**
 * Tests that the upper bound on the second bucket range is included in the info
 */
START_TEST(default_print_info_range_1_upb)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range 1") != NULL) {

			while (getline(&buf, &size, out_file) != -1) {
				
				if (strcasestr(buf, "range") != NULL &&
						strcasestr(buf, "max") != NULL) {
					ck_assert_msg(strstr(buf, "64000us") != NULL, "range 1 upper "
							"bound not correct");
					free(buf);
					fclose(out_file);
					return;
				}
				else if (strcasestr(buf, "bucket") != NULL &&
						strcasestr(buf, "range") != NULL) {
					ck_assert_msg(0, "histogram range 1 upper bound not found");
				}
			}
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range 1 upper bound not found");
}
END_TEST


/**
 * Tests that the upper bound on the third bucket range is included in the info
 */
START_TEST(default_print_info_range_2_upb)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range 2") != NULL) {

			while (getline(&buf, &size, out_file) != -1) {
				
				if (strcasestr(buf, "range") != NULL &&
						strcasestr(buf, "max") != NULL) {
					ck_assert_msg(strstr(buf, "128000us") != NULL, "range 2 upper "
							"bound not correct");
					free(buf);
					fclose(out_file);
					return;
				}
				else if (strcasestr(buf, "bucket") != NULL &&
						strcasestr(buf, "range") != NULL) {
					ck_assert_msg(0, "histogram range 2 upper bound not found");
				}
			}
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range 2 upper bound not found");
}
END_TEST


/**
 * Tests that the width of the first bucket range is included in the info
 */
START_TEST(default_print_info_range_0_width)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range 0") != NULL) {

			while (getline(&buf, &size, out_file) != -1) {
				
				if (strcasestr(buf, "width") != NULL) {
					ck_assert_msg(strstr(buf, "100us") != NULL, "range 0 width "
							"not correct");
					free(buf);
					fclose(out_file);
					return;
				}
				else if (strcasestr(buf, "bucket") != NULL &&
						strcasestr(buf, "range") != NULL) {
					ck_assert_msg(0, "histogram range 0 width bound not found");
				}
			}
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range 0 width not found");
}
END_TEST


/**
 * Tests that the width of the second bucket range is included in the info
 */
START_TEST(default_print_info_range_1_width)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range 1") != NULL) {

			while (getline(&buf, &size, out_file) != -1) {
				
				if (strcasestr(buf, "width") != NULL) {
					ck_assert_msg(strstr(buf, "1000us") != NULL, "range 1 width "
							"not correct");
					free(buf);
					fclose(out_file);
					return;
				}
				else if (strcasestr(buf, "bucket") != NULL &&
						strcasestr(buf, "range") != NULL) {
					ck_assert_msg(0, "histogram range 1 width bound not found");
				}
			}
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range 1 width not found");
}
END_TEST


/**
 * Tests that the width of the third bucket range is included in the info
 */
START_TEST(default_print_info_range_2_width)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range 2") != NULL) {

			while (getline(&buf, &size, out_file) != -1) {
				
				if (strcasestr(buf, "width") != NULL) {
					ck_assert_msg(strstr(buf, "4000us") != NULL, "range 2 width "
							"not correct");
					free(buf);
					fclose(out_file);
					return;
				}
				else if (strcasestr(buf, "bucket") != NULL &&
						strcasestr(buf, "range") != NULL) {
					ck_assert_msg(0, "histogram range 2 width bound not found");
				}
			}
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range 2 width not found");
}
END_TEST


/**
 * Tests that the number of buckets in the first bucket range is included in
 * the info
 */
START_TEST(default_print_info_range_0_n_buckets)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range 0") != NULL) {

			while (getline(&buf, &size, out_file) != -1) {
				
				if (strcasestr(buf, "buckets") != NULL) {
					ck_assert_msg(strstr(buf, "39") != NULL, "range 0 num "
							"buckets not correct");
					free(buf);
					fclose(out_file);
					return;
				}
				else if (strcasestr(buf, "bucket") != NULL &&
						strcasestr(buf, "range") != NULL) {
					ck_assert_msg(0, "histogram range 0 num buckets bound not "
							"found");
				}
			}
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range 0 num buckets not found");
}
END_TEST


/**
 * Tests that the number of buckets in the second bucket range is included in
 * the info
 */
START_TEST(default_print_info_range_1_n_buckets)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range 1") != NULL) {

			while (getline(&buf, &size, out_file) != -1) {
				
				if (strcasestr(buf, "buckets") != NULL) {
					ck_assert_msg(strstr(buf, "60") != NULL, "range 1 num "
							"buckets not correct");
					free(buf);
					fclose(out_file);
					return;
				}
				else if (strcasestr(buf, "bucket") != NULL &&
						strcasestr(buf, "range") != NULL) {
					ck_assert_msg(0, "histogram range 1 num buckets bound not "
							"found");
				}
			}
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range 1 num buckets not found");
}
END_TEST


/**
 * Tests that the number of buckets in the third bucket range is included in
 * the info
 */
START_TEST(default_print_info_range_2_n_buckets)
{
	histogram_t* h = &hist;

	FILE* out_file = tmpfile();

	histogram_print_info(h, out_file);
	fseek(out_file, 0, SEEK_SET);

	char* buf = NULL;
	size_t size = 0;
	while (getline(&buf, &size, out_file) != -1) {
		if (strcasestr(buf, "bucket") != NULL &&
				strcasestr(buf, "range 2") != NULL) {

			while (getline(&buf, &size, out_file) != -1) {
				
				if (strcasestr(buf, "buckets") != NULL) {
					ck_assert_msg(strstr(buf, "16") != NULL, "range 2 num "
							"buckets not correct");
					free(buf);
					fclose(out_file);
					return;
				}
				else if (strcasestr(buf, "bucket") != NULL &&
						strcasestr(buf, "range") != NULL) {
					ck_assert_msg(0, "histogram range 2 num buckets bound not "
							"found");
				}
			}
		}
	}
	free(buf);

	ck_assert_msg(0, "histogram range 2 num buckets not found");
}
END_TEST


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
	tcase_add_test(tc_simple, simple_rename);
	tcase_add_test(tc_simple, simple_rename_twice);
	tcase_add_test(tc_simple, simple_print);
	tcase_add_test(tc_simple, simple_print_lowb);
	tcase_add_test(tc_simple, simple_print_upb);
	tcase_add_test(tc_simple, simple_print_clear);
	tcase_add_test(tc_simple, simple_print_name);
	tcase_add_test(tc_simple, simple_print_info_name);
	tcase_add_test(tc_simple, simple_print_info_size);
	tcase_add_test(tc_simple, simple_print_info_range_lowb);
	tcase_add_test(tc_simple, simple_print_info_range_upb);
	tcase_add_test(tc_simple, simple_print_info_range_0_lowb);
	tcase_add_test(tc_simple, simple_print_info_range_0_upb);
	tcase_add_test(tc_simple, simple_print_info_range_0_width);
	tcase_add_test(tc_simple, simple_print_info_range_0_n_buckets);
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
	tcase_add_test(tc_default, default_print_info_size);
	tcase_add_test(tc_default, default_print_info_range_lowb);
	tcase_add_test(tc_default, default_print_info_range_upb);
	tcase_add_test(tc_default, default_print_info_range_0_lowb);
	tcase_add_test(tc_default, default_print_info_range_1_lowb);
	tcase_add_test(tc_default, default_print_info_range_2_lowb);
	tcase_add_test(tc_default, default_print_info_range_0_upb);
	tcase_add_test(tc_default, default_print_info_range_1_upb);
	tcase_add_test(tc_default, default_print_info_range_2_upb);
	tcase_add_test(tc_default, default_print_info_range_0_width);
	tcase_add_test(tc_default, default_print_info_range_1_width);
	tcase_add_test(tc_default, default_print_info_range_2_width);
	tcase_add_test(tc_default, default_print_info_range_0_n_buckets);
	tcase_add_test(tc_default, default_print_info_range_1_n_buckets);
	tcase_add_test(tc_default, default_print_info_range_2_n_buckets);
	suite_add_tcase(s, tc_default);

	return s;
}

