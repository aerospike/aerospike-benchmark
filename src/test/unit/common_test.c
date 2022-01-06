
#include <check.h>
#include <stdio.h>
#include <time.h>

#include "common.h"


#define TEST_SUITE_NAME "common"


/*
 * make sure 0 works
 */
START_TEST(ddl_0)
{
	ck_assert_int_eq(dec_display_len(0), 1);
}
END_TEST

START_TEST(ddl_1_digit)
{
	// test powers of two
	ck_assert_int_eq(dec_display_len(1), 1);
	ck_assert_int_eq(dec_display_len(2), 1);
	ck_assert_int_eq(dec_display_len(4), 1);
	ck_assert_int_eq(dec_display_len(8), 1);

	// test highest number
	ck_assert_int_eq(dec_display_len(9), 1);
}
END_TEST

START_TEST(ddl_2_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(10), 2);

	// test powers of two
	ck_assert_int_eq(dec_display_len(16), 2);
	ck_assert_int_eq(dec_display_len(32), 2);
	ck_assert_int_eq(dec_display_len(64), 2);

	// test highest number
	ck_assert_int_eq(dec_display_len(99), 2);
}
END_TEST

START_TEST(ddl_3_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(100), 3);

	// test powers of two
	ck_assert_int_eq(dec_display_len(128), 3);
	ck_assert_int_eq(dec_display_len(256), 3);
	ck_assert_int_eq(dec_display_len(512), 3);

	// test highest number
	ck_assert_int_eq(dec_display_len(999), 3);
}
END_TEST

START_TEST(ddl_4_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(1000), 4);

	// test powers of two
	ck_assert_int_eq(dec_display_len(1024), 4);
	ck_assert_int_eq(dec_display_len(2048), 4);
	ck_assert_int_eq(dec_display_len(4096), 4);
	ck_assert_int_eq(dec_display_len(8192), 4);

	// test highest number
	ck_assert_int_eq(dec_display_len(9999), 4);
}
END_TEST

START_TEST(ddl_5_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(10000), 5);

	// test powers of two
	ck_assert_int_eq(dec_display_len(16384), 5);
	ck_assert_int_eq(dec_display_len(32768), 5);
	ck_assert_int_eq(dec_display_len(65536), 5);

	// test highest number
	ck_assert_int_eq(dec_display_len(99999), 5);
}
END_TEST

START_TEST(ddl_6_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(100000), 6);

	// test powers of two
	ck_assert_int_eq(dec_display_len(131072), 6);
	ck_assert_int_eq(dec_display_len(262144), 6);
	ck_assert_int_eq(dec_display_len(524288), 6);

	// test highest number
	ck_assert_int_eq(dec_display_len(999999), 6);
}
END_TEST

START_TEST(ddl_7_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(1000000), 7);

	// test powers of two
	ck_assert_int_eq(dec_display_len(1048576), 7);
	ck_assert_int_eq(dec_display_len(2097152), 7);
	ck_assert_int_eq(dec_display_len(4194304), 7);
	ck_assert_int_eq(dec_display_len(8388608), 7);

	// test highest number
	ck_assert_int_eq(dec_display_len(9999999), 7);
}
END_TEST

START_TEST(ddl_8_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(10000000), 8);

	// test powers of two
	ck_assert_int_eq(dec_display_len(16777216), 8);
	ck_assert_int_eq(dec_display_len(33554432), 8);
	ck_assert_int_eq(dec_display_len(67108864), 8);

	// test highest number
	ck_assert_int_eq(dec_display_len(99999999), 8);
}
END_TEST

START_TEST(ddl_9_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(100000000), 9);

	// test powers of two
	ck_assert_int_eq(dec_display_len(134217728), 9);
	ck_assert_int_eq(dec_display_len(268435456), 9);
	ck_assert_int_eq(dec_display_len(536870912), 9);

	// test highest number
	ck_assert_int_eq(dec_display_len(999999999), 9);
}
END_TEST

START_TEST(ddl_10_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(1000000000), 10);

	// test powers of two
	ck_assert_int_eq(dec_display_len(1073741824), 10);
	ck_assert_int_eq(dec_display_len(2147483648L), 10);
	ck_assert_int_eq(dec_display_len(4294967296L), 10);
	ck_assert_int_eq(dec_display_len(8589934592L), 10);

	// test highest number
	ck_assert_int_eq(dec_display_len(9999999999L), 10);
}
END_TEST

START_TEST(ddl_11_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(10000000000L), 11);

	// test powers of two
	ck_assert_int_eq(dec_display_len(17179869184L), 11);
	ck_assert_int_eq(dec_display_len(34359738368L), 11);
	ck_assert_int_eq(dec_display_len(68719476736L), 11);

	// test highest number
	ck_assert_int_eq(dec_display_len(99999999999L), 11);
}
END_TEST

START_TEST(ddl_12_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(100000000000L), 12);

	// test powers of two
	ck_assert_int_eq(dec_display_len(137438953472L), 12);
	ck_assert_int_eq(dec_display_len(274877906944L), 12);
	ck_assert_int_eq(dec_display_len(549755813888L), 12);

	// test highest number
	ck_assert_int_eq(dec_display_len(999999999999L), 12);
}
END_TEST

START_TEST(ddl_13_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(1000000000000L), 13);

	// test powers of two
	ck_assert_int_eq(dec_display_len(1099511627776L), 13);
	ck_assert_int_eq(dec_display_len(2199023255552L), 13);
	ck_assert_int_eq(dec_display_len(4398046511104L), 13);
	ck_assert_int_eq(dec_display_len(8796093022208L), 13);

	// test highest number
	ck_assert_int_eq(dec_display_len(9999999999999L), 13);
}
END_TEST

START_TEST(ddl_14_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(10000000000000L), 14);

	// test powers of two
	ck_assert_int_eq(dec_display_len(17592186044416L), 14);
	ck_assert_int_eq(dec_display_len(35184372088832L), 14);
	ck_assert_int_eq(dec_display_len(70368744177664L), 14);

	// test highest number
	ck_assert_int_eq(dec_display_len(99999999999999L), 14);
}
END_TEST

START_TEST(ddl_15_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(100000000000000L), 15);

	// test powers of two
	ck_assert_int_eq(dec_display_len(140737488355328L), 15);
	ck_assert_int_eq(dec_display_len(281474976710656L), 15);
	ck_assert_int_eq(dec_display_len(562949953421312L), 15);

	// test highest number
	ck_assert_int_eq(dec_display_len(999999999999999L), 15);
}
END_TEST

START_TEST(ddl_16_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(1000000000000000L), 16);

	// test powers of two
	ck_assert_int_eq(dec_display_len(1125899906842624L), 16);
	ck_assert_int_eq(dec_display_len(2251799813685248L), 16);
	ck_assert_int_eq(dec_display_len(4503599627370496L), 16);
	ck_assert_int_eq(dec_display_len(9007199254740992L), 16);

	// test highest number
	ck_assert_int_eq(dec_display_len(9999999999999999L), 16);
}
END_TEST

START_TEST(ddl_17_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(10000000000000000L), 17);

	// test powers of two
	ck_assert_int_eq(dec_display_len(18014398509481984L), 17);
	ck_assert_int_eq(dec_display_len(36028797018963968L), 17);
	ck_assert_int_eq(dec_display_len(72057594037927936L), 17);

	// test highest number
	ck_assert_int_eq(dec_display_len(99999999999999999L), 17);
}
END_TEST

START_TEST(ddl_18_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(100000000000000000L), 18);

	// test powers of two
	ck_assert_int_eq(dec_display_len(144115188075855872L), 18);
	ck_assert_int_eq(dec_display_len(288230376151711744L), 18);
	ck_assert_int_eq(dec_display_len(576460752303423488L), 18);

	// test highest number
	ck_assert_int_eq(dec_display_len(999999999999999999L), 18);
}
END_TEST

START_TEST(ddl_19_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(1000000000000000000L), 19);

	// test powers of two
	ck_assert_int_eq(dec_display_len(1152921504606846976L), 19);
	ck_assert_int_eq(dec_display_len(2305843009213693952L), 19);
	ck_assert_int_eq(dec_display_len(4611686018427387904L), 19);
	ck_assert_int_eq(dec_display_len(9223372036854775808UL), 19);

	// test highest number
	ck_assert_int_eq(dec_display_len(9999999999999999999UL), 19);
}
END_TEST

START_TEST(ddl_20_digits)
{
	// test lowest number
	ck_assert_int_eq(dec_display_len(10000000000000000000UL), 20);

	// test highest number
	ck_assert_int_eq(dec_display_len(18446744073709551615UL), 20);
}
END_TEST


Suite*
common_suite(void)
{
	Suite* s;
	TCase* tc_dec_display_len;

	s = suite_create("Common");

	/* decimal display length test cases */
	tc_dec_display_len = tcase_create("Decimal display length");
	tcase_add_test(tc_dec_display_len, ddl_0);
	tcase_add_test(tc_dec_display_len, ddl_1_digit);
	tcase_add_test(tc_dec_display_len, ddl_2_digits);
	tcase_add_test(tc_dec_display_len, ddl_3_digits);
	tcase_add_test(tc_dec_display_len, ddl_4_digits);
	tcase_add_test(tc_dec_display_len, ddl_5_digits);
	tcase_add_test(tc_dec_display_len, ddl_6_digits);
	tcase_add_test(tc_dec_display_len, ddl_7_digits);
	tcase_add_test(tc_dec_display_len, ddl_8_digits);
	tcase_add_test(tc_dec_display_len, ddl_9_digits);
	tcase_add_test(tc_dec_display_len, ddl_10_digits);
	tcase_add_test(tc_dec_display_len, ddl_11_digits);
	tcase_add_test(tc_dec_display_len, ddl_12_digits);
	tcase_add_test(tc_dec_display_len, ddl_13_digits);
	tcase_add_test(tc_dec_display_len, ddl_14_digits);
	tcase_add_test(tc_dec_display_len, ddl_15_digits);
	tcase_add_test(tc_dec_display_len, ddl_16_digits);
	tcase_add_test(tc_dec_display_len, ddl_17_digits);
	tcase_add_test(tc_dec_display_len, ddl_18_digits);
	tcase_add_test(tc_dec_display_len, ddl_19_digits);
	tcase_add_test(tc_dec_display_len, ddl_20_digits);
	suite_add_tcase(s, tc_dec_display_len);

	return s;
}

