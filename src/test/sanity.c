/*
 * Check: a unit test framework for C
 * Copyright (C) 2001, 2002 Arien Malec
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <stdlib.h>
#include <check.h>
#include "benchmark.h"

#define TEST_SUITE_NAME "sanity"

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

/**
 * Really make sure five is represented accurately
 */
START_TEST(test_sanity)
{
	uint16_t five = 5;
	char *five_str = "five";
	ck_assert_int_eq(five, 5);
	ck_assert_str_eq("five", five_str);
}
END_TEST

/**
 * The thing about 5 is that it is always trying to be tricky. Here
 * we double check that five is for sure 5 and "five"
 */
START_TEST(test_still_sane) 
{
	uint16_t five = 5;
	char* five_str = "five";

	ck_assert_msg(five == 5 && strcmp(five_str, "five") == 0,
					"five should be 5 and five_str should be \"five\"");
}
END_TEST

Suite*
sanity_suite(void) {
	Suite* s;
	TCase* tc_core;

	s = suite_create("Sanity");

	/* Core test case */
	tc_core = tcase_create("Core");
	tcase_add_checked_fixture(tc_core, setup, teardown);
	tcase_add_test(tc_core, test_sanity);
	tcase_add_test(tc_core, test_still_sane);
	suite_add_tcase(s, tc_core);

	return s;
}
