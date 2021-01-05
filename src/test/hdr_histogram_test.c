
#include <check.h>
#include <stdio.h>
#include <time.h>

#include <hdr_histogram/hdr_histogram.h>
#include <hdr_histogram/hdr_histogram_log.h>


#define TEST_SUITE_NAME "HDR Histogram"


static struct hdr_histogram * h;
static struct hdr_log_writer writer;
static struct hdr_log_reader reader;
static FILE* tmp_file;


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


/**
 * Any setup code to run each test goes here
 *
 * @return  void  
 */
static void 
log_setup(void) 
{
	hdr_init(1, 1000000, 3, &h);
	hdr_log_writer_init(&writer);
	hdr_log_reader_init(&reader);
	tmp_file = tmpfile();
}

/**
 * setup's friend. Does the opposite and cleans up the test
 *
 * @return  void  
 */
static void
log_teardown(void)
{
	fclose(tmp_file);
	hdr_close(h);
}


/**
 * Tests that a simple initialization works
 */
START_TEST(log_restore_empty)
{
	hdr_timespec start_timespec, end_timespec;
	hdr_log_write(&writer, tmp_file, &start_timespec, &end_timespec, h);
	hdr_log_read(&reader, tmp_file, &h, &start_timespec, &end_timespec);

	ck_assert_int_eq(hdr_total_count(h), 0);
}
END_TEST


Suite*
hdr_histogram_suite(void)
{
	Suite* s;
	TCase* tc_core;
	TCase* tc_simple;
	TCase* tc_log;

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

	tc_log = tcase_create("Log");
	tcase_add_checked_fixture(tc_log, log_setup, log_teardown);
	tcase_add_test(tc_log, log_restore_empty);
	suite_add_tcase(s, tc_log);

	return s;
}

