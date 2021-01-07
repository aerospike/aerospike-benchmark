/**
 * hdr_test_util.h
 * Written by Michael Barker and released to the public domain,
 * as explained at http://creativecommons.org/publicdomain/zero/1.0/
 */

#ifndef HDR_HISTOGRAM_HDR_TEST_UTIL_H
#define HDR_HISTOGRAM_HDR_TEST_UTIL_H

static int compare_histograms(struct hdr_histogram* expected, struct hdr_histogram* actual)
{
    struct hdr_iter expected_iter;
    struct hdr_iter actual_iter;

    hdr_iter_init(&expected_iter, expected);
    hdr_iter_init(&actual_iter, actual);

    while (hdr_iter_next(&expected_iter))
    {
		if (!hdr_iter_next(&actual_iter)) {
			return 1;
		}
        if (expected_iter.count != actual_iter.count) {
			return 2;
		}
    }

    if (expected->min_value != actual->min_value) {
		return 3;
	}
    if (expected->max_value != actual->max_value) {
		return 4;
	}
    if (expected->total_count != actual->total_count) {
		return 5;
	}

    return 0;
}


#endif
