
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


// sequential test
START_TEST(default_config_test)
{
	histogram h;
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


START_TEST(sequential_print_check)
{
	histogram h;
	histogram_init(&h, 2, 20, (rangespec_t[]) {
			{ .upper_bound = 180, .bucket_width = 20 },
			{ .upper_bound = 360, .bucket_width = 30 }
			});

	for (delay_t us = 1; us < 361; us++) {
		if (us < 180 || us >= 220) {
			histogram_add(&h, us);
		}
	}

	ck_assert_int_eq(h.underflow_cnt, 19);
	ck_assert_int_eq(h.overflow_cnt,  1);

	// 14 buckets in total
	for (size_t i = 0; i < 14; i++) {
		if (i < 8) {
			ck_assert_int_eq(h.buckets[i], 20);
		}
		else if (i == 8) {
			ck_assert_int_eq(h.buckets[i], 0);
		}
		else if (i == 9) {
			ck_assert_int_eq(h.buckets[i], 20);
		}
		else {
			ck_assert_int_eq(h.buckets[i], 30);
		}
	}

	FILE * out_file = tmpfile();

	histogram_print(&h, 1, out_file);
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
		ck_assert_int_eq(cnt, h.buckets[i + (i >= 8)]);
	}

	// overflow bucket
	ck_assert_int_eq(fscanf(out_file, ", %u:%u", &bucket, &cnt), 2);
	ck_assert_int_eq(bucket, 360);
	ck_assert_int_eq(cnt, 1);


	char buf[1];
	ck_assert_int_eq(fread(buf, 1, sizeof(buf), out_file), 1);
	ck_assert_int_eq(buf[0], '\n');

	fclose(out_file);

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
	tcase_add_test(tc_core, sequential_print_check);
	suite_add_tcase(s, tc_core);

	return s;
}

