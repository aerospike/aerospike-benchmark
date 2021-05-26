/**
 * hdr_histogram_test.c
 * Written by Michael Barker and released to the public domain,
 * as explained at http://creativecommons.org/publicdomain/zero/1.0/
 */

#include <check.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

#include <stdio.h>
#include <hdr_histogram/hdr_histogram.h>

#include <citrusleaf/alloc.h>

#include "hdr_test_util.h"


#define TEST_SUITE_NAME "hdr_histogram"

static void compare_values(double a, double b, double variation)
{
    ck_assert_float_eq_tol(a, b, b * variation);
}

static void compare_percentile(int64_t a, double b, double variation)
{
    compare_values((double) a, b, variation);
}

static struct hdr_histogram* raw_histogram = NULL;
static struct hdr_histogram* cor_histogram = NULL;
static struct hdr_histogram* scaled_raw_histogram = NULL;
static struct hdr_histogram* scaled_cor_histogram = NULL;



START_TEST(test_create)
{
    struct hdr_histogram* h = NULL;
    int r = hdr_init(1, INT64_C(3600000000), 3, &h);

    ck_assert_msg(r == 0, "Failed to allocate hdr_histogram");
    ck_assert_msg(h != NULL, "Failed to allocate hdr_histogram");
    ck_assert_msg(h->counts_len == 23552, "Incorrect array length");

    hdr_close(h);
}
END_TEST


START_TEST(test_create_with_large_values)
{
    struct hdr_histogram* h = NULL;
    int r = hdr_init(20000000, 100000000, 5, &h);
    ck_assert_msg(r == 0, "Didn't create");

    hdr_record_value(h, 100000000);
    hdr_record_value(h, 20000000);
    hdr_record_value(h, 30000000);

    ck_assert(
        hdr_values_are_equivalent(h, 20000000, hdr_value_at_percentile(h, 50.0)));

    ck_assert(
        hdr_values_are_equivalent(h, 30000000, hdr_value_at_percentile(h, 83.33)));

    ck_assert(
        hdr_values_are_equivalent(h, 100000000, hdr_value_at_percentile(h, 83.34)));

    ck_assert(
        hdr_values_are_equivalent(h, 100000000, hdr_value_at_percentile(h, 99.0)));

	hdr_close(h);
}
END_TEST


START_TEST(test_invalid_significant_figures)
{
    struct hdr_histogram* h = NULL;

    int r = hdr_alloc(36000000, -1, &h);
    ck_assert(r == EINVAL);
    ck_assert(h == 0);

    r = hdr_alloc(36000000, 6, &h);
    ck_assert(r == EINVAL);
    ck_assert(h == 0);

}
END_TEST


START_TEST(test_invalid_init)
{
    struct hdr_histogram* h = NULL;

    ck_assert(EINVAL == hdr_init(0, 64*1024, 2, &h));
    ck_assert(EINVAL == hdr_init(80, 110, 5, &h));

}
END_TEST


START_TEST(test_out_of_range_values)
{
    struct hdr_histogram *h;
    hdr_init(1, 1000, 4, &h);
    ck_assert( hdr_record_value(h, 32767));
    ck_assert(!hdr_record_value(h, 32768));
	hdr_close(h);
}
END_TEST


START_TEST(test_linear_iter_buckets_correctly)
{
    int step_count = 0;
    int64_t total_count = 0;
    struct hdr_histogram *h;
    struct hdr_iter iter;

    hdr_init(1, 255, 2, &h);

    hdr_record_value(h, 193);
    hdr_record_value(h, 255);
    hdr_record_value(h, 0);
    hdr_record_value(h, 1);
    hdr_record_value(h, 64);
    hdr_record_value(h, 128);

    hdr_iter_linear_init(&iter, h, 64);

    while (hdr_iter_next(&iter))
    {
        total_count += iter.specifics.linear.count_added_in_this_iteration_step;
        /* start - changes to reproduce issue */
        if (step_count == 0)
        {
            hdr_record_value(h, 2);
        }
        /* end - changes to reproduce issue */
        step_count++;
    }

    ck_assert(step_count == 4);
    ck_assert(total_count == 6);

	hdr_close(h);
}
END_TEST


/**
 * Any setup code to run each test goes here
 *
 * @return  void  
 */
static void 
basic_setup(void) 
{
    const int64_t highest_trackable_value = INT64_C(3600) * 1000 * 1000;
    const int32_t significant_figures = 3;
    const int64_t interval = INT64_C(10000);

    int i;

    hdr_init(1, highest_trackable_value, significant_figures, &raw_histogram);
    hdr_init(1, highest_trackable_value, significant_figures, &cor_histogram);

    for (i = 0; i < 10000; i++)
    {
        hdr_record_value(raw_histogram, 1000);
        hdr_record_corrected_value(cor_histogram, 1000, interval);
    }

    hdr_record_value(raw_histogram, 100000000);
    hdr_record_corrected_value(cor_histogram, 100000000, 10000L);
}

/**
 * setup's friend. Does the opposite and cleans up the test
 *
 * @return  void  
 */
static void
basic_teardown(void)
{
	hdr_close(raw_histogram);
	hdr_close(cor_histogram);
	raw_histogram = NULL;
	cor_histogram = NULL;
}


START_TEST(test_total_count)
{
    ck_assert(raw_histogram->total_count == 10001);
    ck_assert(cor_histogram->total_count == 20000);
}
END_TEST


START_TEST(test_get_max_value)
{
    int64_t actual_raw_max, actual_cor_max;

    actual_raw_max = hdr_max(raw_histogram);
    ck_assert(hdr_values_are_equivalent(raw_histogram, actual_raw_max, 100000000));
    actual_cor_max = hdr_max(cor_histogram);
    ck_assert(hdr_values_are_equivalent(cor_histogram, actual_cor_max, 100000000));
}
END_TEST


START_TEST(test_get_min_value)
{
    ck_assert(hdr_min(raw_histogram) == 1000);
    ck_assert(hdr_min(cor_histogram) == 1000);
}
END_TEST


START_TEST(test_percentiles)
{
    compare_percentile(hdr_value_at_percentile(raw_histogram, 30.0), 1000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(raw_histogram, 99.0), 1000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(raw_histogram, 99.99), 1000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(raw_histogram, 99.999), 100000000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(raw_histogram, 100.0), 100000000.0, 0.001);

    compare_percentile(hdr_value_at_percentile(cor_histogram, 30.0), 1000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(cor_histogram, 50.0), 1000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(cor_histogram, 75.0), 50000000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(cor_histogram, 90.0), 80000000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(cor_histogram, 99.0), 98000000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(cor_histogram, 99.999), 100000000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(cor_histogram, 100.0), 100000000.0, 0.001);
}
END_TEST


START_TEST(test_recorded_values)
{
    struct hdr_iter iter;
    int index;
    int64_t total_added_count = 0;

    /* Raw Histogram */
    hdr_iter_recorded_init(&iter, raw_histogram);

    index = 0;
    while (hdr_iter_next(&iter))
    {
        int64_t count_added_in_this_bucket = iter.specifics.recorded.count_added_in_this_iteration_step;
        if (index == 0)
        {
            ck_assert_msg(count_added_in_this_bucket == 10000, "Value at 0 is not 10000");
        }
        else
        {
            ck_assert_msg(count_added_in_this_bucket == 1, "Value at 1 is not 1");
        }

        index++;
    }
    ck_assert_msg(index == 2, "Should have encountered 2 values");

    /* Corrected Histogram */
    hdr_iter_recorded_init(&iter, cor_histogram);

    index = 0;
    while (hdr_iter_next(&iter))
    {
        int64_t count_added_in_this_bucket = iter.specifics.recorded.count_added_in_this_iteration_step;
        if (index == 0)
        {
            ck_assert_msg(count_added_in_this_bucket == 10000, "Count at 0 is not 10000");
        }
        ck_assert_msg(iter.count != 0, "Count should not be 0");
        ck_assert_msg(iter.count == count_added_in_this_bucket,
				"Count at value iterated to should be count added in this step");
        total_added_count += count_added_in_this_bucket;
        index++;
    }
    ck_assert(total_added_count == 20000);
}
END_TEST


START_TEST(test_linear_values)
{
    struct hdr_iter iter;
    int index;
    int64_t total_added_count;

    /* Raw Histogram */
    hdr_iter_linear_init(&iter, raw_histogram, 100000);
    index = 0;
    while (hdr_iter_next(&iter))
    {
        int64_t count_added_in_this_bucket = iter.specifics.linear.count_added_in_this_iteration_step;

        if (index == 0)
        {
            ck_assert_msg(count_added_in_this_bucket == 10000, "Count at 0 is not 10000");
        }
        else if (index == 999)
        {
            ck_assert_msg(count_added_in_this_bucket == 1, "Count at 999 is not 1");
        }
        else
        {
            ck_assert_msg(count_added_in_this_bucket == 0, "Count should be 0");
        }

        index++;
    }
    ck_assert_msg(index == 1000, "Should have met 1000 values");

    /* Corrected Histogram */

    hdr_iter_linear_init(&iter, cor_histogram, 10000);
    index = 0;
    total_added_count = 0;
    while (hdr_iter_next(&iter))
    {
        int64_t count_added_in_this_bucket = iter.specifics.linear.count_added_in_this_iteration_step;

        if (index == 0)
        {
            ck_assert_msg(count_added_in_this_bucket == 10001, "Count at 0 is not 10001");
        }

        total_added_count += count_added_in_this_bucket;
        index++;
    }
    ck_assert_msg(index == 10000, "Should have met 10001 values");
    ck_assert_msg(total_added_count == 20000, "Should have met 20000 counts");
}
END_TEST


START_TEST(test_logarithmic_values)
{
    struct hdr_iter iter;
    int index;
    uint64_t total_added_count;

    hdr_iter_log_init(&iter, raw_histogram, 10000, 2.0);
    index = 0;

    while(hdr_iter_next(&iter))
    {
        uint64_t count_added_in_this_bucket = iter.specifics.log.count_added_in_this_iteration_step;
        if (index == 0)
        {
            ck_assert(10000 == count_added_in_this_bucket);
        }
        else if (index == 14)
        {
            ck_assert(1 == count_added_in_this_bucket);
        }
        else
        {
            ck_assert(0 == count_added_in_this_bucket);
        }

        index++;
    }

    ck_assert(index - 1 == 14);

    hdr_iter_log_init(&iter, cor_histogram, 10000, 2.0);
    index = 0;
    total_added_count = 0;
    while (hdr_iter_next(&iter))
    {
        uint64_t count_added_in_this_bucket = iter.specifics.log.count_added_in_this_iteration_step;

        if (index == 0)
        {
			ck_assert(10001 == count_added_in_this_bucket);
        }
        total_added_count += count_added_in_this_bucket;
        index++;
    }

    ck_assert(index - 1 == 14);
    ck_assert(total_added_count == 20000);
}
END_TEST


START_TEST(test_reset)
{
    ck_assert_msg(hdr_value_at_percentile(raw_histogram, 99.0) != 0,
			"Value at 99%% == 0.0");
    ck_assert_msg(hdr_value_at_percentile(cor_histogram, 99.0) != 0,
			"Value at 99%% == 0.0");

    hdr_reset(raw_histogram);
    hdr_reset(cor_histogram);

    ck_assert_msg(raw_histogram->total_count == 0, "Total raw count != 0");
    ck_assert_msg(cor_histogram->total_count == 0, "Total corrected count != 0");

    ck_assert_msg(hdr_value_at_percentile(raw_histogram, 99.0) == 0,
			"Value at 99%% not 0.0");
    ck_assert_msg(hdr_value_at_percentile(cor_histogram, 99.0) == 0,
			"Value at 99%% not 0.0");
}
END_TEST


/**
 * Any setup code to run each test goes here
 *
 * @return  void  
 */
static void 
scaled_setup(void) 
{
	const int64_t highest_trackable_value = INT64_C(3600) * 1000 * 1000;
    const int32_t significant_figures = 3;
    const int64_t interval = INT64_C(10000);
    const int64_t scale = 512;
    const int64_t scaled_interval = interval * scale;

    int i;

    hdr_init(1, highest_trackable_value, significant_figures, &raw_histogram);
    hdr_init(1, highest_trackable_value, significant_figures, &cor_histogram);

    hdr_init(1000, highest_trackable_value * 512, significant_figures, &scaled_raw_histogram);
    hdr_init(1000, highest_trackable_value * 512, significant_figures, &scaled_cor_histogram);

    for (i = 0; i < 10000; i++)
    {
        hdr_record_value(raw_histogram, 1000);
        hdr_record_corrected_value(cor_histogram, 1000, interval);

        hdr_record_value(scaled_raw_histogram, 1000 * scale);
        hdr_record_corrected_value(scaled_cor_histogram, 1000 * scale, scaled_interval);
    }

    hdr_record_value(raw_histogram, 100000000);
    hdr_record_corrected_value(cor_histogram, 100000000, 10000L);

    hdr_record_value(scaled_raw_histogram, 100000000 * scale);
    hdr_record_corrected_value(scaled_cor_histogram, 100000000 * scale, scaled_interval);
}

/**
 * setup's friend. Does the opposite and cleans up the test
 *
 * @return  void  
 */
static void
scaled_teardown(void)
{
	hdr_close(raw_histogram);
	hdr_close(cor_histogram);
	hdr_close(scaled_raw_histogram);
	hdr_close(scaled_cor_histogram);
	raw_histogram = NULL;
	cor_histogram = NULL;
	scaled_raw_histogram = NULL;
	scaled_cor_histogram = NULL;
}


START_TEST(test_scaling_equivalence)
{
    int64_t expected_99th, scaled_99th;

	compare_values(
			hdr_mean(cor_histogram) * 512,
			hdr_mean(scaled_cor_histogram),
			0.000001);

    ck_assert(cor_histogram->total_count == scaled_cor_histogram->total_count);

    expected_99th = hdr_value_at_percentile(cor_histogram, 99.0) * 512;
    scaled_99th = hdr_value_at_percentile(scaled_cor_histogram, 99.0);
	ck_assert(
			hdr_lowest_equivalent_value(cor_histogram, expected_99th) ==
			hdr_lowest_equivalent_value(scaled_cor_histogram, scaled_99th));
}
END_TEST


/**
 * Any setup code to run each test goes here
 *
 * @return  void  
 */
static void 
atomic_setup(void) 
{
	const int64_t highest_trackable_value = INT64_C(3600) * 1000 * 1000;
    const int32_t significant_figures = 3;
    const int64_t interval = INT64_C(10000);
    const int64_t scale = 512;
    const int64_t scaled_interval = interval * scale;

    int i;

    hdr_init(1, highest_trackable_value, significant_figures, &raw_histogram);
    hdr_init(1, highest_trackable_value, significant_figures, &cor_histogram);

    hdr_init(1000, highest_trackable_value * 512, significant_figures, &scaled_raw_histogram);
    hdr_init(1000, highest_trackable_value * 512, significant_figures, &scaled_cor_histogram);

    for (i = 0; i < 10000; i++)
    {
        hdr_record_value_atomic(raw_histogram, 1000);
        hdr_record_corrected_value_atomic(cor_histogram, 1000, interval);

        hdr_record_value_atomic(scaled_raw_histogram, 1000 * scale);
        hdr_record_corrected_value_atomic(scaled_cor_histogram, 1000 * scale, scaled_interval);
    }

    hdr_record_value_atomic(raw_histogram, 100000000);
    hdr_record_corrected_value_atomic(cor_histogram, 100000000, 10000L);

    hdr_record_value_atomic(scaled_raw_histogram, 100000000 * scale);
    hdr_record_corrected_value_atomic(scaled_cor_histogram, 100000000 * scale, scaled_interval);
}

/**
 * setup's friend. Does the opposite and cleans up the test
 *
 * @return  void  
 */
static void
atomic_teardown(void)
{
	hdr_close(raw_histogram);
	hdr_close(cor_histogram);
	hdr_close(scaled_raw_histogram);
	hdr_close(scaled_cor_histogram);
	raw_histogram = NULL;
	cor_histogram = NULL;
	scaled_raw_histogram = NULL;
	scaled_cor_histogram = NULL;
}


START_TEST(test_atomic_create_with_large_values)
{
    struct hdr_histogram* h = NULL;
    int r = hdr_init(20000000, 100000000, 5, &h);
    ck_assert_msg(r == 0, "Didn't create");

    hdr_record_value_atomic(h, 100000000);
    hdr_record_value_atomic(h, 20000000);
    hdr_record_value_atomic(h, 30000000);

    ck_assert(
        hdr_values_are_equivalent(h, 20000000, hdr_value_at_percentile(h, 50.0)));

    ck_assert(
        hdr_values_are_equivalent(h, 30000000, hdr_value_at_percentile(h, 83.33)));

    ck_assert(
        hdr_values_are_equivalent(h, 100000000, hdr_value_at_percentile(h, 83.34)));

    ck_assert(
        hdr_values_are_equivalent(h, 100000000, hdr_value_at_percentile(h, 99.0)));

	hdr_close(h);
}
END_TEST


START_TEST(test_atomic_out_of_range_values)
{
    struct hdr_histogram *h;
    hdr_init(1, 1000, 4, &h);
    ck_assert( hdr_record_value_atomic(h, 32767));
    ck_assert(!hdr_record_value_atomic(h, 32768));
	hdr_close(h);
}
END_TEST


START_TEST(test_atomic_linear_iter_buckets_correctly)
{
    int step_count = 0;
    int64_t total_count = 0;
    struct hdr_histogram *h;
    struct hdr_iter iter;

    hdr_init(1, 255, 2, &h);

    hdr_record_value_atomic(h, 193);
    hdr_record_value_atomic(h, 255);
    hdr_record_value_atomic(h, 0);
    hdr_record_value_atomic(h, 1);
    hdr_record_value_atomic(h, 64);
    hdr_record_value_atomic(h, 128);

    hdr_iter_linear_init(&iter, h, 64);

    while (hdr_iter_next(&iter))
    {
        total_count += iter.specifics.linear.count_added_in_this_iteration_step;
        /* start - changes to reproduce issue */
        if (step_count == 0)
        {
            hdr_record_value_atomic(h, 2);
        }
        /* end - changes to reproduce issue */
        step_count++;
    }

    ck_assert(step_count == 4);
    ck_assert(total_count == 6);

	hdr_close(h);
}
END_TEST


START_TEST(test_atomic_total_count)
{
    ck_assert(raw_histogram->total_count == 10001);
    ck_assert(cor_histogram->total_count == 20000);
}
END_TEST


START_TEST(test_atomic_get_max_value)
{
    int64_t actual_raw_max, actual_cor_max;

    actual_raw_max = hdr_max(raw_histogram);
    ck_assert(hdr_values_are_equivalent(raw_histogram, actual_raw_max, 100000000));
    actual_cor_max = hdr_max(cor_histogram);
    ck_assert(hdr_values_are_equivalent(cor_histogram, actual_cor_max, 100000000));
}
END_TEST


START_TEST(test_atomic_get_min_value)
{
    ck_assert(hdr_min(raw_histogram) == 1000);
    ck_assert(hdr_min(cor_histogram) == 1000);
}
END_TEST


START_TEST(test_atomic_percentiles)
{
    compare_percentile(hdr_value_at_percentile(raw_histogram, 30.0), 1000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(raw_histogram, 99.0), 1000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(raw_histogram, 99.99), 1000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(raw_histogram, 99.999), 100000000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(raw_histogram, 100.0), 100000000.0, 0.001);

    compare_percentile(hdr_value_at_percentile(cor_histogram, 30.0), 1000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(cor_histogram, 50.0), 1000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(cor_histogram, 75.0), 50000000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(cor_histogram, 90.0), 80000000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(cor_histogram, 99.0), 98000000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(cor_histogram, 99.999), 100000000.0, 0.001);
    compare_percentile(hdr_value_at_percentile(cor_histogram, 100.0), 100000000.0, 0.001);
}
END_TEST


START_TEST(test_atomic_recorded_values)
{
    struct hdr_iter iter;
    int index;
    int64_t total_added_count = 0;

    /* Raw Histogram */
    hdr_iter_recorded_init(&iter, raw_histogram);

    index = 0;
    while (hdr_iter_next(&iter))
    {
        int64_t count_added_in_this_bucket = iter.specifics.recorded.count_added_in_this_iteration_step;
        if (index == 0)
        {
            ck_assert_msg(count_added_in_this_bucket == 10000, "Value at 0 is not 10000");
        }
        else
        {
            ck_assert_msg(count_added_in_this_bucket == 1, "Value at 1 is not 1");
        }

        index++;
    }
    ck_assert_msg(index == 2, "Should have encountered 2 values");

    /* Corrected Histogram */
    hdr_iter_recorded_init(&iter, cor_histogram);

    index = 0;
    while (hdr_iter_next(&iter))
    {
        int64_t count_added_in_this_bucket = iter.specifics.recorded.count_added_in_this_iteration_step;
        if (index == 0)
        {
            ck_assert_msg(count_added_in_this_bucket == 10000, "Count at 0 is not 10000");
        }
        ck_assert_msg(iter.count != 0, "Count should not be 0");
        ck_assert_msg(iter.count == count_added_in_this_bucket,
				"Count at value iterated to should be count added in this step");
        total_added_count += count_added_in_this_bucket;
        index++;
    }
    ck_assert(total_added_count == 20000);
}
END_TEST


START_TEST(test_atomic_linear_values)
{
    struct hdr_iter iter;
    int index;
    int64_t total_added_count;

    /* Raw Histogram */
    hdr_iter_linear_init(&iter, raw_histogram, 100000);
    index = 0;
    while (hdr_iter_next(&iter))
    {
        int64_t count_added_in_this_bucket = iter.specifics.linear.count_added_in_this_iteration_step;

        if (index == 0)
        {
            ck_assert_msg(count_added_in_this_bucket == 10000, "Count at 0 is not 10000");
        }
        else if (index == 999)
        {
            ck_assert_msg(count_added_in_this_bucket == 1, "Count at 999 is not 1");
        }
        else
        {
            ck_assert_msg(count_added_in_this_bucket == 0, "Count should be 0");
        }

        index++;
    }
    ck_assert_msg(index == 1000, "Should have met 1000 values");

    /* Corrected Histogram */

    hdr_iter_linear_init(&iter, cor_histogram, 10000);
    index = 0;
    total_added_count = 0;
    while (hdr_iter_next(&iter))
    {
        int64_t count_added_in_this_bucket = iter.specifics.linear.count_added_in_this_iteration_step;

        if (index == 0)
        {
            ck_assert_msg(count_added_in_this_bucket == 10001, "Count at 0 is not 10001");
        }

        total_added_count += count_added_in_this_bucket;
        index++;
    }
    ck_assert_msg(index == 10000, "Should have met 10001 values");
    ck_assert_msg(total_added_count == 20000, "Should have met 20000 counts");
}
END_TEST


START_TEST(test_atomic_logarithmic_values)
{
    struct hdr_iter iter;
    int index;
    uint64_t total_added_count;

    hdr_iter_log_init(&iter, raw_histogram, 10000, 2.0);
    index = 0;

    while(hdr_iter_next(&iter))
    {
        uint64_t count_added_in_this_bucket = iter.specifics.log.count_added_in_this_iteration_step;
        if (index == 0)
        {
            ck_assert(10000 == count_added_in_this_bucket);
        }
        else if (index == 14)
        {
            ck_assert(1 == count_added_in_this_bucket);
        }
        else
        {
            ck_assert(0 == count_added_in_this_bucket);
        }

        index++;
    }

    ck_assert(index - 1 == 14);

    hdr_iter_log_init(&iter, cor_histogram, 10000, 2.0);
    index = 0;
    total_added_count = 0;
    while (hdr_iter_next(&iter))
    {
        uint64_t count_added_in_this_bucket = iter.specifics.log.count_added_in_this_iteration_step;

        if (index == 0)
        {
			ck_assert(10001 == count_added_in_this_bucket);
        }
        total_added_count += count_added_in_this_bucket;
        index++;
    }

    ck_assert(index - 1 == 14);
    ck_assert(total_added_count == 20000);
}
END_TEST


START_TEST(test_atomic_reset)
{
    ck_assert_msg(hdr_value_at_percentile(raw_histogram, 99.0) != 0,
			"Value at 99%% == 0.0");
    ck_assert_msg(hdr_value_at_percentile(cor_histogram, 99.0) != 0,
			"Value at 99%% == 0.0");

    hdr_reset(raw_histogram);
    hdr_reset(cor_histogram);

    ck_assert_msg(raw_histogram->total_count == 0, "Total raw count != 0");
    ck_assert_msg(cor_histogram->total_count == 0, "Total corrected count != 0");

    ck_assert_msg(hdr_value_at_percentile(raw_histogram, 99.0) == 0,
			"Value at 99%% not 0.0");
    ck_assert_msg(hdr_value_at_percentile(cor_histogram, 99.0) == 0,
			"Value at 99%% not 0.0");
}
END_TEST


START_TEST(test_atomic_scaling_equivalence)
{
    int64_t expected_99th, scaled_99th;

	compare_values(
			hdr_mean(cor_histogram) * 512,
			hdr_mean(scaled_cor_histogram),
			0.000001);

    ck_assert(cor_histogram->total_count == scaled_cor_histogram->total_count);

    expected_99th = hdr_value_at_percentile(cor_histogram, 99.0) * 512;
    scaled_99th = hdr_value_at_percentile(scaled_cor_histogram, 99.0);
	ck_assert(
			hdr_lowest_equivalent_value(cor_histogram, expected_99th) ==
			hdr_lowest_equivalent_value(scaled_cor_histogram, scaled_99th));
}
END_TEST



struct test_histogram_data
{
    struct hdr_histogram* histogram;
    int64_t* values;
    int values_len;
};

static void* record_values(void* thread_context)
{
    int i;
    struct test_histogram_data* thread_data = (struct test_histogram_data*) thread_context;


    for (i = 0; i < thread_data->values_len; i++)
    {
        hdr_record_value_atomic(thread_data->histogram, thread_data->values[i]);
    }

    pthread_exit(NULL);
}


START_TEST(test_recording_concurrently)
{
    const int value_count = 10000000;
    int64_t* values = cf_calloc(value_count, sizeof(int64_t));
    struct hdr_histogram* expected_histogram;
    struct hdr_histogram* actual_histogram;
    struct test_histogram_data thread_data[2];
    struct hdr_iter expected_iter;
    struct hdr_iter actual_iter;
    pthread_t threads[2];
    int i;

    ck_assert(hdr_init(1, 10000000, 2, &expected_histogram) == 0);
    ck_assert(hdr_init(1, 10000000, 2, &actual_histogram) == 0);


    for (i = 0; i < value_count; i++)
    {
        values[i] = rand() % 20000;
    }
    
    for (i = 0; i < value_count; i++)
    {
        hdr_record_value(expected_histogram, values[i]);
    }

    thread_data[0].histogram = actual_histogram;
    thread_data[0].values = values;
    thread_data[0].values_len = value_count / 2;
    pthread_create(&threads[0], NULL, record_values, &thread_data[0]);

    thread_data[1].histogram = actual_histogram;
    thread_data[1].values = &values[value_count / 2];
    thread_data[1].values_len = value_count / 2;
    pthread_create(&threads[1], NULL, record_values, &thread_data[1]);

    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);

    hdr_iter_init(&expected_iter, expected_histogram);
    hdr_iter_init(&actual_iter, actual_histogram);

    ck_assert(compare_histograms(expected_histogram, actual_histogram) == 0);
	cf_free(values);

	hdr_close(expected_histogram);
	hdr_close(actual_histogram);
}
END_TEST


Suite*
hdr_histogram_suite(void)
{
	Suite* s;
	TCase* tc_init;
	TCase* tc_iter;
	TCase* tc_regular;
	TCase* tc_scaling;
	TCase* tc_atomic;
	TCase* tc_concurrency;

	s = suite_create("HDR Histogram");

	/* initialization tests */
	tc_init = tcase_create("Initialization");
	tcase_add_test(tc_init, test_create);
	tcase_add_test(tc_init, test_create_with_large_values);
	tcase_add_test(tc_init, test_invalid_significant_figures);
	tcase_add_test(tc_init, test_invalid_init);
	tcase_add_test(tc_init, test_out_of_range_values);
	suite_add_tcase(s, tc_init);

	/* iteration tests */
	tc_iter = tcase_create("Iteration");
	tcase_add_test(tc_iter, test_linear_iter_buckets_correctly);
	suite_add_tcase(s, tc_iter);

	/* regular tests */
	tc_regular = tcase_create("Regular");
	tcase_add_checked_fixture(tc_regular, basic_setup, basic_teardown);
	tcase_add_test(tc_regular, test_total_count);
	tcase_add_test(tc_regular, test_get_max_value);
	tcase_add_test(tc_regular, test_get_min_value);
	tcase_add_test(tc_regular, test_percentiles);
	tcase_add_test(tc_regular, test_recorded_values);
	tcase_add_test(tc_regular, test_linear_values);
	tcase_add_test(tc_regular, test_logarithmic_values);
	tcase_add_test(tc_regular, test_reset);
	suite_add_tcase(s, tc_regular);

	/* scaling tests */
	tc_scaling = tcase_create("Scaling");
	tcase_add_checked_fixture(tc_scaling, scaled_setup, scaled_teardown);
	tcase_add_test(tc_scaling, test_scaling_equivalence);
	suite_add_tcase(s, tc_scaling);

	/* atomic tests */
	tc_atomic = tcase_create("Atomic");
	tcase_add_checked_fixture(tc_atomic, atomic_setup, atomic_teardown);
	tcase_add_test(tc_atomic, test_atomic_create_with_large_values);
	tcase_add_test(tc_atomic, test_atomic_out_of_range_values);
	tcase_add_test(tc_atomic, test_atomic_linear_iter_buckets_correctly);
	tcase_add_test(tc_atomic, test_atomic_total_count);
	tcase_add_test(tc_atomic, test_atomic_get_max_value);
	tcase_add_test(tc_atomic, test_atomic_get_min_value);
	tcase_add_test(tc_atomic, test_atomic_percentiles);
	tcase_add_test(tc_atomic, test_atomic_recorded_values);
	tcase_add_test(tc_atomic, test_atomic_linear_values);
	tcase_add_test(tc_atomic, test_atomic_logarithmic_values);
	tcase_add_test(tc_atomic, test_atomic_reset);
	tcase_add_test(tc_atomic, test_atomic_scaling_equivalence);
	suite_add_tcase(s, tc_atomic);

	/* concurrency tests */
	tc_concurrency = tcase_create("Concurrency");
	tcase_add_test(tc_concurrency, test_recording_concurrently);
	suite_add_tcase(s, tc_concurrency);

	return s;
}
