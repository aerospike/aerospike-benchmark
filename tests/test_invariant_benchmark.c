#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * The vulnerability is that sprintf(filter, "namespace/%s", namespace) can
 * overflow a stack buffer. The security invariant is: the formatted output
 * must never exceed the destination buffer size. We test that snprintf
 * would be safe by verifying the required length against a typical buffer size.
 *
 * Since we cannot safely call the vulnerable sprintf path with oversized input
 * without risking actual stack corruption, we validate the invariant that
 * "namespace/" + namespace must fit within the filter buffer (256 bytes typical).
 */

#define FILTER_BUF_SIZE 256

START_TEST(test_namespace_filter_no_overflow)
{
    /* Invariant: formatted "namespace/<input>" must fit in FILTER_BUF_SIZE */
    const char *payloads[] = {
        /* Exact exploit: namespace longer than buffer minus prefix */
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        /* Boundary: exactly FILTER_BUF_SIZE - strlen("namespace/") - 1 = 245 chars */
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
        /* Valid short input */
        "test-ns",
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    for (int i = 0; i < num_payloads; i++) {
        char filter[FILTER_BUF_SIZE];
        int needed = snprintf(filter, FILTER_BUF_SIZE, "namespace/%s", payloads[i]);
        /* Security invariant: output must not exceed buffer */
        ck_assert_msg(needed < FILTER_BUF_SIZE,
            "Payload %d would overflow filter buffer: needs %d, have %d",
            i, needed, FILTER_BUF_SIZE);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_namespace_filter_no_overflow);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}