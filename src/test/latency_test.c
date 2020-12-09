
#include <check.h>
#include <stdio.h>
#include <time.h>

#include "latency.h"


#define TEST_SUITE_NAME "latency"


latency lat;

/**
 * Tests that a simple initialization works
 */
START_TEST(initialization_test)
{
	latency l;
	latency_init(&l, 4, 3);
	latency_free(&l);
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
	latency_init(&lat, 4, 3);
}

/**
 * setup's friend. Does the opposite and cleans up the test
 *
 * @return  void  
 */
static void
simple_teardown(void)
{
	latency_free(&lat);
}


/**
 * Checks that the latency table is cleared on initialization
 */
START_TEST(simple_cleared_on_init)
{
	latency * l = &lat;

	for (int i = 0; i < 4; i++) {
		ck_assert_int_eq(latency_get_count(l, i), 0);
	}
}
END_TEST


/**
 * Checks that inserting 0 gets placed in the first bucket
 */
START_TEST(simple_insert_0)
{
	latency * l = &lat;

	latency_add(l, 0);
	ck_assert_int_eq(latency_get_count(l, 0), 1);
}
END_TEST


/**
 * Checks that inserting 0 does not affect any other bins
 */
START_TEST(simple_insert_0_noclobber)
{
	latency * l = &lat;

	latency_add(l, 0);
	for (int i = 1; i < 4; i++) {
		ck_assert_int_eq(latency_get_count(l, i), 0);
	}
}
END_TEST


/**
 * Checks that inserting 1 gets placed in the first bucket
 */
START_TEST(simple_insert_1)
{
	latency * l = &lat;

	latency_add(l, 1);
	ck_assert_int_eq(latency_get_count(l, 0), 1);
}
END_TEST


/**
 * Checks that inserting 2 gets placed in the second bucket
 */
START_TEST(simple_insert_2)
{
	latency * l = &lat;

	latency_add(l, 2);
	ck_assert_int_eq(latency_get_count(l, 1), 1);
}
END_TEST


/**
 * Checks that inserting 8 gets placed in the second bucket
 */
START_TEST(simple_insert_8)
{
	latency * l = &lat;

	latency_add(l, 8);
	ck_assert_int_eq(latency_get_count(l, 1), 1);
}
END_TEST


/**
 * Checks that inserting 9 gets placed in the third bucket
 */
START_TEST(simple_insert_9)
{
	latency * l = &lat;

	latency_add(l, 9);
	ck_assert_int_eq(latency_get_count(l, 2), 1);
}
END_TEST


/**
 * Checks that inserting 64 gets placed in the third bucket
 */
START_TEST(simple_insert_64)
{
	latency * l = &lat;

	latency_add(l, 64);
	ck_assert_int_eq(latency_get_count(l, 2), 1);
}
END_TEST


/**
 * Checks that inserting 65 gets placed in the fourth bucket
 */
START_TEST(simple_insert_65)
{
	latency * l = &lat;

	latency_add(l, 65);
	ck_assert_int_eq(latency_get_count(l, 3), 1);
}
END_TEST


/**
 * Checks that inserting very large numbers all get placed in
 * the fourth bucket
 */
START_TEST(simple_insert_large)
{
	latency * l = &lat;

	latency_add(l, 128);
	ck_assert_int_eq(latency_get_count(l, 3), 1);
	latency_add(l, 129);
	ck_assert_int_eq(latency_get_count(l, 3), 2);
	latency_add(l, 500);
	ck_assert_int_eq(latency_get_count(l, 3), 3);
}
END_TEST


/**
 * Any setup code to run each test goes here
 *
 * @return  void  
 */
static void 
print_setup(void) 
{
	latency_init(&lat, 4, 3);
}

/**
 * setup's friend. Does the opposite and cleans up the test
 *
 * @return  void  
 */
static void
print_teardown(void)
{
	latency_free(&lat);
}


/**
 * Checks that printing an empty table only prints 0's
 */
START_TEST(print_empty)
{
	latency * l = &lat;

	char out[500];

	latency_print_results(l, "", out);

	int b1, b2, b3, b4;
	ck_assert_int_eq(sscanf(out, "%d%% %d%% %d%% %d%%", &b1, &b2, &b3, &b4), 4);

	ck_assert_int_eq(b1, 0);
	ck_assert_int_eq(b2, 0);
	ck_assert_int_eq(b3, 0);
	ck_assert_int_eq(b4, 0);
}
END_TEST


/**
 * Checks that printing a table with only an element in bucket 3 prints a 0
 * in bucket 0 and 100's in the rest
 */
START_TEST(print_name)
{
	latency * l = &lat;

	latency_add(l, 65);

	char out[500];

	latency_print_results(l, "test", out);

	char name[500];
	ck_assert_int_eq(sscanf(out, "%500s", name), 1);
	ck_assert_msg(strcmp(name, "test") == 0, "the name of the latency row was "
			"not printed");
}
END_TEST


/**
 * Checks that printing a table with only an element in bucket 0 prints a 100
 * in bucket 0 and 0's in the rest
 */
START_TEST(print_bucket_0)
{
	latency * l = &lat;

	latency_add(l, 0);

	char out[500];

	latency_print_results(l, "", out);

	int b1, b2, b3, b4;
	ck_assert_int_eq(sscanf(out, "%d%% %d%% %d%% %d%%", &b1, &b2, &b3, &b4), 4);

	ck_assert_int_eq(b1, 100);
	ck_assert_int_eq(b2, 0);
	ck_assert_int_eq(b3, 0);
	ck_assert_int_eq(b4, 0);
}
END_TEST


/**
 * Checks that printing a table with only an element in bucket 1 prints a 100
 * in bucket 1 and 0's in the rest
 */
START_TEST(print_bucket_1)
{
	latency * l = &lat;

	latency_add(l, 2);

	char out[500];

	latency_print_results(l, "", out);

	int b1, b2, b3, b4;
	ck_assert_int_eq(sscanf(out, "%d%% %d%% %d%% %d%%", &b1, &b2, &b3, &b4), 4);

	ck_assert_int_eq(b1, 0);
	ck_assert_int_eq(b2, 100);
	ck_assert_int_eq(b3, 0);
	ck_assert_int_eq(b4, 0);
}
END_TEST


/**
 * Checks that printing a table with only an element in bucket 2 prints 0's
 * in buckets 0 and 3 and 100's in buckets 1 and 2
 */
START_TEST(print_bucket_2)
{
	latency * l = &lat;

	latency_add(l, 9);

	char out[500];

	latency_print_results(l, "", out);

	int b1, b2, b3, b4;
	ck_assert_int_eq(sscanf(out, "%d%% %d%% %d%% %d%%", &b1, &b2, &b3, &b4), 4);

	ck_assert_int_eq(b1, 0);
	ck_assert_int_eq(b2, 100);
	ck_assert_int_eq(b3, 100);
	ck_assert_int_eq(b4, 0);
}
END_TEST


/**
 * Checks that printing a table with only an element in bucket 3 prints a 0
 * in bucket 0 and 100's in the rest
 */
START_TEST(print_bucket_3)
{
	latency * l = &lat;

	latency_add(l, 65);

	char out[500];

	latency_print_results(l, "", out);

	int b1, b2, b3, b4;
	ck_assert_int_eq(sscanf(out, "%d%% %d%% %d%% %d%%", &b1, &b2, &b3, &b4), 4);

	ck_assert_int_eq(b1, 0);
	ck_assert_int_eq(b2, 100);
	ck_assert_int_eq(b3, 100);
	ck_assert_int_eq(b4, 100);
}
END_TEST


/**
 * Checks the header of the latency table is printed correctly
 */
START_TEST(print_header)
{
	latency * l = &lat;

	char out[500];

	latency_set_header(l, out);

	int b1, b2, b3, b4;
	ck_assert_int_eq(sscanf(out, " <=%dms >%dms >%dms >%dms", &b1, &b2, &b3, &b4), 4);

	ck_assert_int_eq(b1, 1);
	ck_assert_int_eq(b2, 1);
	ck_assert_int_eq(b3, 8);
	ck_assert_int_eq(b4, 64);
}
END_TEST



Suite*
latency_suite(void)
{
	Suite* s;
	TCase* tc_core;
	TCase* tc_simple;
	TCase* tc_print;

	s = suite_create("Latency");

	/* Core test cases */
	tc_core = tcase_create("Core");
	tcase_add_test(tc_core, initialization_test);
	suite_add_tcase(s, tc_core);

	tc_simple = tcase_create("Simple");
	tcase_add_checked_fixture(tc_simple, simple_setup, simple_teardown);
	tcase_add_test(tc_simple, simple_cleared_on_init);
	tcase_add_test(tc_simple, simple_insert_0);
	tcase_add_test(tc_simple, simple_insert_0_noclobber);
	tcase_add_test(tc_simple, simple_insert_1);
	tcase_add_test(tc_simple, simple_insert_2);
	tcase_add_test(tc_simple, simple_insert_8);
	tcase_add_test(tc_simple, simple_insert_9);
	tcase_add_test(tc_simple, simple_insert_64);
	tcase_add_test(tc_simple, simple_insert_65);
	tcase_add_test(tc_simple, simple_insert_large);
	suite_add_tcase(s, tc_simple);

	tc_print = tcase_create("Print");
	tcase_add_checked_fixture(tc_print, print_setup, print_teardown);
	tcase_add_test(tc_print, print_empty);
	tcase_add_test(tc_print, print_name);
	tcase_add_test(tc_print, print_bucket_0);
	tcase_add_test(tc_print, print_bucket_1);
	tcase_add_test(tc_print, print_bucket_2);
	tcase_add_test(tc_print, print_bucket_3);
	tcase_add_test(tc_print, print_header);
	suite_add_tcase(s, tc_print);
	

	return s;
}
