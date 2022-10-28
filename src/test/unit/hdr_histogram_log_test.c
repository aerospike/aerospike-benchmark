/**
 * hdr_histogram_log_test.c
 * Written by Michael Barker and released to the public domain,
 * as explained at http://creativecommons.org/publicdomain/zero/1.0/
 */

#include <check.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>

#include <stdio.h>
#include <hdr_histogram/hdr_time.h>
#include <hdr_histogram/hdr_histogram.h>
#include <hdr_histogram/hdr_histogram_log.h>
#include <hdr_histogram/hdr_encoding.h>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

static long ns_to_ms(long ns)
{
	return (ns / 1000000) * 1000000;
}

static bool compare_timespec(hdr_timespec* a, hdr_timespec* b)
{
	char a_str[128];
	char b_str[128];

	long a_tv_msec = ns_to_ms(a->tv_nsec);
	long b_tv_msec = ns_to_ms(b->tv_nsec);

	/* Allow off by 1 millisecond due to parsing and rounding. */
	if (a->tv_sec == b->tv_sec && labs(a_tv_msec - b_tv_msec) <= 1000000)
	{
		return true;
	}

	if (a->tv_sec != b->tv_sec)
	{
#if defined(_MSC_VER)
		_ctime32_s(a_str, sizeof(a_str), &a->tv_sec);
		_ctime32_s(b_str, sizeof(b_str), &b->tv_sec);
		printf("tv_sec: %s != %s\n", a_str, b_str);
#else
		printf(
				"tv_sec: %s != %s\n",
				ctime_r(&a->tv_sec, a_str),
				ctime_r(&b->tv_sec, b_str));
#endif
	}

	if (a_tv_msec == b_tv_msec)
	{
		printf("%ld != %ld\n", a->tv_nsec, b->tv_nsec);
	}

	return false;
}

static void compare_histogram(struct hdr_histogram* a, struct hdr_histogram* b)
{
	int64_t a_max, b_max, a_min, b_min;
	size_t a_size, b_size, counts_size;
	struct hdr_iter iter_a, iter_b;

	ck_assert_int_eq(a->counts_len, b->counts_len);

	a_max = hdr_max(a);
	b_max = hdr_max(b);
	ck_assert_int_eq(a_max, b_max);

	a_min = hdr_min(a);
	b_min = hdr_min(b);
	ck_assert_int_eq(a_min, b_min);

	a_size = hdr_get_memory_size(a);
	b_size = hdr_get_memory_size(b);
	ck_assert_int_eq(a_size, b_size);

	counts_size = a->counts_len * sizeof(int64_t);

	if (memcmp(a->counts, b->counts, counts_size) != 0) {

		printf("%s\n", "Counts incorrect");

		hdr_iter_init(&iter_a, a);
		hdr_iter_init(&iter_b, b);

		while (hdr_iter_next(&iter_a) && hdr_iter_next(&iter_b)) {
			if (iter_a.count != iter_b.count ||
					iter_a.value != iter_b.value) {
				printf(
						"A - value: %"PRIu64", count: %"PRIu64", "
						"B - value: %"PRIu64", count: %"PRIu64"\n",
						iter_a.value, iter_a.count,
						iter_b.value, iter_b.count);
			}
		}

		ck_assert(0);
	}
}

static bool validate_return_code(int rc)
{
	if (rc == 0)
	{
		return true;
	}

	printf("%s\n", hdr_strerror(rc));
	return false;
}


static struct hdr_histogram* raw_histogram = NULL;
static struct hdr_histogram* cor_histogram = NULL;


/* Prototypes to avoid exporting in header file. */
void hdr_base64_encode_block(const uint8_t* input, char* output);

void hdr_base64_decode_block(const char* input, uint8_t* output);
int hdr_encode_compressed(struct hdr_histogram* h, uint8_t** buffer, size_t* length);
int hdr_decode_compressed(
		uint8_t* buffer, size_t length, struct hdr_histogram** histogram);
void hex_dump (char *desc, void *addr, int len);


/**
 * Any setup code to run each test goes here
 *
 * @return  void  
 */
static void 
encode_setup(void) 
{
	int i;

	hdr_alloc(INT64_C(3600) * 1000 * 1000, 3, &raw_histogram);
	hdr_alloc(INT64_C(3600) * 1000 * 1000, 3, &cor_histogram);

	for (i = 0; i < 10000; i++)
	{
		hdr_record_value(raw_histogram, 1000);
		hdr_record_corrected_value(cor_histogram, 1000, 10000);
	}

	hdr_record_value(raw_histogram, 100000000);
	hdr_record_corrected_value(cor_histogram, 100000000, 10000);
}

/**
 * setup's friend. Does the opposite and cleans up the test
 *
 * @return  void  
 */
static void
encode_teardown(void)
{
	hdr_close(raw_histogram);
	hdr_close(cor_histogram);
}


START_TEST(test_encode_and_decode_empty)
{
	uint8_t* buffer;
	uint8_t* decoded;
	char* encoded;
	size_t len = 0;
	int rc = 0;
	size_t encoded_len;
	size_t decoded_len;

	ck_assert_msg(hdr_init(1, 1000000, 1, &raw_histogram) == 0,
			"allocation should be valid");

	rc = hdr_encode_compressed(raw_histogram, &buffer, &len);
	ck_assert_msg(validate_return_code(rc), "Did not encode");

	encoded_len = hdr_base64_encoded_len(len);
	decoded_len = hdr_base64_decoded_len(encoded_len);
	encoded = calloc(encoded_len + 1, sizeof(char));
	decoded = calloc(decoded_len, sizeof(uint8_t));

	hdr_base64_encode(buffer, len, encoded, encoded_len);
	hdr_base64_decode(encoded, encoded_len, decoded, decoded_len);

	ck_assert(memcmp(buffer, decoded, len) == 0);

	free(decoded);
	free(encoded);
}
END_TEST


START_TEST(test_encode_and_decode_compressed)
{
	uint8_t* buffer = NULL;
	size_t len = 0;
	int rc = 0;
	struct hdr_histogram* actual = NULL;
	struct hdr_histogram* expected;

	expected = raw_histogram;

	rc = hdr_encode_compressed(expected, &buffer, &len);
	ck_assert_msg(validate_return_code(rc), "Did not encode");

	rc = hdr_decode_compressed(buffer, len, &actual);
	ck_assert_msg(validate_return_code(rc), "Did not decode");

	ck_assert_msg(actual != NULL, "Loaded histogram is null");

	compare_histogram(expected, actual);

	free(actual);
}
END_TEST


START_TEST(test_encode_and_decode_compressed2)
{
	uint8_t* buffer = NULL;
	size_t len = 0;
	int rc = 0;
	struct hdr_histogram* actual = NULL;
	struct hdr_histogram* expected;

	expected = cor_histogram;

	rc = hdr_encode_compressed(expected, &buffer, &len);
	ck_assert_msg(validate_return_code(rc), "Did not encode");

	rc = hdr_decode_compressed(buffer, len, &actual);
	ck_assert_msg(validate_return_code(rc), "Did not decode");

	ck_assert_msg(actual != NULL, "Loaded histogram is null");

	compare_histogram(expected, actual);

	free(actual);
}
END_TEST


START_TEST(test_bounds_check_on_decode)
{
	uint8_t* buffer = NULL;
	size_t len = 0;
	int rc = 0;
	struct hdr_histogram* actual = NULL;
	struct hdr_histogram* expected;

	expected = cor_histogram;

	rc = hdr_encode_compressed(expected, &buffer, &len);
	ck_assert_msg(validate_return_code(rc), "Did not encode");

	rc = hdr_decode_compressed(buffer, len - 1, &actual);
	ck_assert_msg(rc == EINVAL, "Should have be invalid");
	ck_assert_msg(actual == NULL, "Should not have built histogram");
}
END_TEST


START_TEST(test_encode_and_decode_base64)
{
	uint8_t* buffer = NULL;
	uint8_t* decoded = NULL;
	char* encoded = NULL;
	size_t encoded_len, decoded_len;
	size_t len = 0;
	int rc = 0;

	rc = hdr_encode_compressed(cor_histogram, &buffer, &len);
	ck_assert_msg(validate_return_code(rc), "Did not encode");

	encoded_len = hdr_base64_encoded_len(len);
	decoded_len = hdr_base64_decoded_len(encoded_len);
	encoded = calloc(encoded_len + 1, sizeof(char));
	decoded = calloc(decoded_len, sizeof(uint8_t));

	hdr_base64_encode(buffer, len, encoded, encoded_len);
	hdr_base64_decode(encoded, encoded_len, decoded, decoded_len);

	ck_assert_msg(memcmp(buffer, decoded, len) == 0, "Should be same");

	free(decoded);
	free(encoded);
}
END_TEST


START_TEST(test_encode_and_decode_compressed_large)
{
	const int64_t limit = INT64_C(3600) * 1000 * 1000;
	struct hdr_histogram* actual = NULL;
	struct hdr_histogram* expected = NULL;
	uint8_t* buffer = NULL;
	size_t len = 0;
	int i;
	int rc = 0;

	hdr_init(1, limit, 4, &expected);
	srand(5);

	for (i = 0; i < 8070; i++)
	{
		hdr_record_value(expected, rand() % limit);
	}

	rc = hdr_encode_compressed(expected, &buffer, &len);
	ck_assert_msg(validate_return_code(rc), "Did not encode");

	rc = hdr_decode_compressed(buffer, len, &actual);
	ck_assert_msg(validate_return_code(rc), "Did not decode");

	ck_assert(actual != NULL);

	compare_histogram(expected, actual);

	free(expected);
	free(actual);
}
END_TEST


static void assert_base64_encode(const char* input, const char* expected)
{
	size_t input_len = strlen(input);
	int output_len = (int) (ceil(input_len / 3.0) * 4.0);

	char* output = calloc(sizeof(char), output_len);

	int r = hdr_base64_encode((uint8_t*)input, input_len, output, output_len);
	ck_assert(r == 0);
	ck_assert_mem_eq(expected, output, output_len);

	free(output);
}


START_TEST(base64_encode_encodes_without_padding)
{
	assert_base64_encode(
			"any carnal pleasur",
			"YW55IGNhcm5hbCBwbGVhc3Vy");
}
END_TEST


START_TEST(base64_encode_encodes_with_padding)
{
	assert_base64_encode(
			"any carnal pleasure.",
			"YW55IGNhcm5hbCBwbGVhc3VyZS4=");
	assert_base64_encode(
			"any carnal pleasure",
			"YW55IGNhcm5hbCBwbGVhc3VyZQ==");
}
END_TEST


START_TEST(base64_encode_fails_with_invalid_lengths)
{
	ck_assert_msg(
			hdr_base64_encode(NULL, 9, NULL, 11),
			"Output length not 4/3 of input length");
}
END_TEST


START_TEST(base64_encode_block_encodes_3_bytes)
{
	char output[5] = { 0 };

	hdr_base64_encode_block((uint8_t*)"Man", output);
	ck_assert_str_eq("TWFu", output);
}
END_TEST


START_TEST(base64_decode_block_decodes_4_chars)
{
	uint8_t output[4] = { 0 };

	hdr_base64_decode_block("TWFu", output);
	ck_assert_str_eq("Man", (char*) output);
}
END_TEST


static void assert_base64_decode(const char* base64_encoded, const char* expected)
{
	size_t encoded_len = strlen(base64_encoded);
	size_t output_len = (encoded_len / 4) * 3;

	uint8_t* output = calloc(sizeof(uint8_t), output_len);

	int result = hdr_base64_decode(base64_encoded, encoded_len, output, output_len);

	ck_assert(result == 0);
	ck_assert_str_eq(expected, (char*)output);
}

START_TEST(base64_decode_decodes_strings_without_padding)
{
	assert_base64_decode(
			"YW55IGNhcm5hbCBwbGVhc3Vy",
			"any carnal pleasur");
}
END_TEST


START_TEST(base64_decode_decodes_strings_with_padding)
{
	assert_base64_decode(
			"YW55IGNhcm5hbCBwbGVhc3VyZS4=",
			"any carnal pleasure.");

	assert_base64_decode(
			"YW55IGNhcm5hbCBwbGVhc3VyZQ==",
			"any carnal pleasure");
}
END_TEST


START_TEST(base64_decode_fails_with_invalid_lengths)
{
	ck_assert_msg(hdr_base64_decode(NULL, 5, NULL, 3) != 0, "Input length %% 4 != 0");
	ck_assert_msg(hdr_base64_decode(NULL, 3, NULL, 3) != 0, "Input length < 4");
	ck_assert_msg(
			hdr_base64_decode(NULL, 8, NULL, 7) != 0,
			"Output length not 3/4 of input length");
}
END_TEST


/*
 * same setup as encode
 */
#define rw_setup encode_setup
#define rw_teardown encode_teardown


START_TEST(writes_and_reads_log)
{
	struct hdr_log_writer writer;
	struct hdr_log_reader reader;
	struct hdr_histogram* read_cor_histogram;
	struct hdr_histogram* read_raw_histogram;
	const char* file_name = "src/test/unit/histogram.log";
	int rc = 0;
	FILE* log_file;
	const char* tag = "tag_value";
	char read_tag[32];
	struct hdr_log_entry write_entry;
	struct hdr_log_entry read_entry;

	hdr_gettime(&write_entry.start_timestamp);

	write_entry.interval.tv_sec = 5;
	write_entry.interval.tv_nsec = 2000000;

	hdr_log_writer_init(&writer);
	hdr_log_reader_init(&reader);

	log_file = fopen(file_name, "w+");
	ck_assert_msg(log_file, "Could not open %s", file_name);

	rc = hdr_log_write_header(&writer, log_file, "Test log", &write_entry.start_timestamp);
	ck_assert_msg(validate_return_code(rc), "Failed header write");

	write_entry.tag = (char *) tag;
	write_entry.tag_len = strlen(tag);
	hdr_log_write_entry(&writer, log_file, &write_entry, cor_histogram);
	ck_assert_msg(validate_return_code(rc), "Failed corrected write");

	write_entry.tag = NULL;
	hdr_log_write_entry(&writer, log_file, &write_entry, raw_histogram);
	ck_assert_msg(validate_return_code(rc), "Failed raw write");

	fprintf(log_file, "\n");

	fflush(log_file);
	fclose(log_file);

	log_file = fopen(file_name, "r");

	read_cor_histogram = NULL;
	read_raw_histogram = NULL;

	rc = hdr_log_read_header(&reader, log_file);
	ck_assert_msg(validate_return_code(rc), "Failed header read");
	ck_assert_msg(reader.major_version == 1, "Incorrect major version");
	ck_assert_msg(reader.minor_version == 2, "Incorrect minor version");
	ck_assert_msg(
			compare_timespec(&reader.start_timestamp, &write_entry.start_timestamp),
			"Incorrect start timestamp");

	read_entry.tag = read_tag;
	read_entry.tag_len = sizeof(read_tag);
	read_entry.tag[0] = '\0';
	read_entry.tag[read_entry.tag_len - 1] = '\0';

	rc = hdr_log_read_entry(&reader, log_file, &read_entry, &read_cor_histogram);
	ck_assert_msg(validate_return_code(rc), "Failed corrected read");
	ck_assert_msg(
			compare_timespec(&read_entry.start_timestamp, &write_entry.start_timestamp),
			"Incorrect first timestamp");
	ck_assert_msg(
			compare_timespec(&read_entry.interval, &write_entry.interval),
			"Incorrect first interval");
	ck_assert_str_eq(read_entry.tag, tag);

	read_entry.tag[0] = '\0';
	read_entry.tag[read_entry.tag_len - 1] = '\0';
	rc = hdr_log_read_entry(&reader, log_file, &read_entry, &read_raw_histogram);
	ck_assert_msg(read_entry.tag[0] == '\0', "Should not find tag");
	ck_assert_msg(validate_return_code(rc), "Failed raw read");

	compare_histogram(cor_histogram, read_cor_histogram);

	compare_histogram(raw_histogram, read_raw_histogram);

	rc = hdr_log_read(&reader, log_file, &read_cor_histogram, NULL, NULL);
	ck_assert_msg(rc == EOF, "No EOF at end of file");

	fclose(log_file);
	remove(file_name);
}
END_TEST


START_TEST(log_reader_aggregates_into_single_histogram)
{
	const char* file_name = "histogram.log";
	hdr_timespec timestamp;
	hdr_timespec interval;
	struct hdr_log_writer writer;
	struct hdr_log_reader reader;
	int rc = 0;
	FILE* log_file;
	struct hdr_histogram* histogram;
	struct hdr_iter iter;
	int64_t expected_total_count;

	hdr_gettime(&timestamp);
	interval.tv_sec = 5;
	interval.tv_nsec = 2000000;

	hdr_log_writer_init(&writer);
	hdr_log_reader_init(&reader);

	log_file = fopen(file_name, "w+");

	hdr_log_write_header(&writer, log_file, "Test log", &timestamp);
	hdr_log_write(&writer, log_file, &timestamp, &interval, cor_histogram);
	hdr_log_write(&writer, log_file, &timestamp, &interval, raw_histogram);
	fflush(log_file);
	fclose(log_file);

	log_file = fopen(file_name, "r");

	hdr_alloc(INT64_C(3600) * 1000 * 1000, 3, &histogram);

	rc = hdr_log_read_header(&reader, log_file);
	ck_assert_msg(validate_return_code(rc), "Failed header read");
	rc = hdr_log_read(&reader, log_file, &histogram, NULL, NULL);
	ck_assert_msg(validate_return_code(rc), "Failed corrected read");
	rc = hdr_log_read(&reader, log_file, &histogram, NULL, NULL);
	ck_assert_msg(validate_return_code(rc), "Failed raw read");

	hdr_iter_recorded_init(&iter, histogram);
	expected_total_count = raw_histogram->total_count + cor_histogram->total_count;

	ck_assert_int_eq(histogram->total_count, expected_total_count);

	while (hdr_iter_next(&iter))
	{
		int64_t count = iter.count;
		int64_t value = iter.value;

		int64_t expected_count =
			hdr_count_at_value(raw_histogram, value) +
			hdr_count_at_value(cor_histogram, value);

		ck_assert_int_eq(count, expected_count);
	}

	fclose(log_file);
	remove(file_name);
	hdr_close(histogram);
}
END_TEST


START_TEST(log_reader_fails_with_incorrect_version)
{
	const char* log_with_invalid_version =
		"#[Test log]\n"
		"#[Histogram log format version 1.04]\n"
		"#[StartTime: 1404700005.222 (seconds since epoch), Mon Jul 02:26:45 GMT 2014]\n"
		"StartTimestamp\",\"EndTimestamp\",\"Interval_Max\",\"Interval_Compressed_Histogram\"\n";
	const char* file_name = "histogram_with_invalid_version.log";
	struct hdr_log_reader reader;
	FILE* log_file;
	int r;

	log_file = fopen(file_name, "w+");
	fprintf(log_file, "%s", log_with_invalid_version);
	fflush(log_file);
	fclose(log_file);

	log_file = fopen(file_name, "r");
	hdr_log_reader_init(&reader);
	r = hdr_log_read_header(&reader, log_file);

	ck_assert_msg(r == HDR_LOG_INVALID_VERSION, "Should error with incorrect version");

	fclose(log_file);
	remove(file_name);
}
END_TEST


START_TEST(test_encode_decode_empty)
{
	char *data;
	struct hdr_histogram *histogram, *hdr_new = NULL;

	hdr_alloc(INT64_C(3600) * 1000 * 1000, 3, &histogram);

	ck_assert_msg(hdr_log_encode(histogram, &data) == 0,
			"Failed to encode histogram data");
	ck_assert_msg(hdr_log_decode(&hdr_new, data, strlen(data)) == 0,
			"Failed to decode histogram data");
	compare_histogram(histogram, hdr_new);
	hdr_close(histogram);
	hdr_close(hdr_new);
	free(data);
}
END_TEST


START_TEST(test_string_encode_decode)
{
	int i;
	char *data;
	struct hdr_histogram *histogram, *hdr_new = NULL;

	hdr_alloc(INT64_C(3600) * 1000 * 1000, 3, &histogram);

	for (i = 1; i < 100; i++)
	{
		hdr_record_value(histogram, i*i);
	}

	ck_assert_msg(hdr_log_encode(histogram, &data) == 0,
			"Failed to encode histogram data");
	ck_assert_msg(hdr_log_decode(&hdr_new, data, strlen(data)) == 0,
			"Failed to decode histogram data");
	compare_histogram(histogram, hdr_new);
	ck_assert_float_eq_tol(hdr_mean(histogram), hdr_mean(hdr_new), 0.001);
}
END_TEST


START_TEST(test_string_encode_decode_2)
{
	int i;
	char *data;

	struct hdr_histogram *histogram, *hdr_new = NULL;

	hdr_alloc(1000, 3, &histogram);

	for (i = 1; i < histogram->highest_trackable_value; i++)
	{
		hdr_record_value(histogram, i);
	}

	ck_assert_msg(
			validate_return_code(hdr_log_encode(histogram, &data)),
			"Failed to encode histogram data");
	ck_assert_msg(
			validate_return_code(hdr_log_decode(&hdr_new, data, strlen(data))),
			"Failed to decode histogram data");
	compare_histogram(histogram, hdr_new);
	ck_assert_float_eq_tol(hdr_mean(histogram), hdr_mean(hdr_new), 0.001);
}
END_TEST


START_TEST(decode_v1_log)
{
	const char* v1_log = "src/test/unit/jHiccup-2.0.6.logV1.hlog";
	struct hdr_histogram* accum;
	struct hdr_histogram* h = NULL;
	struct hdr_log_reader reader;
	hdr_timespec timestamp, interval;
	int rc;
	int64_t total_count = 0;
	int histogram_count = 0;

	FILE* f = fopen(v1_log, "r");
	ck_assert_msg(f != NULL, "Can not open v1 log file");

	hdr_init(1, INT64_C(3600000000000), 3, &accum);


	hdr_log_reader_init(&reader);

	rc = hdr_log_read_header(&reader, f);
	ck_assert_msg(rc == 0, "Failed to read header");

	while ((rc = hdr_log_read(&reader, f, &h, &timestamp, &interval)) != EOF)
	{
		int64_t dropped;

		ck_assert_msg(rc == 0, "Failed to read histogram");
		histogram_count++;
		total_count += h->total_count;
		dropped = hdr_add(accum, h);
		ck_assert_msg(dropped == 0, "Dropped events");

		hdr_close(h);
		h = NULL;
	}

	ck_assert_msg(histogram_count == 88, "Wrong number of histograms");
	ck_assert_msg(total_count == 65964, "Wrong total count");
	ck_assert_msg(
			1829765119 == hdr_value_at_percentile(accum, 99.9),
			"99.9 percentile wrong");
	ck_assert_msg(1888485375 == hdr_max(accum), "max value wrong");
	ck_assert_msg(1438867590 == reader.start_timestamp.tv_sec,
			"Seconds wrong");
	ck_assert_msg(285000000 == reader.start_timestamp.tv_nsec,
			"Nanoseconds wrong");
}
END_TEST


START_TEST(decode_v2_log)
{
	struct hdr_histogram* accum;
	struct hdr_histogram* h = NULL;
	struct hdr_log_reader reader;
	hdr_timespec timestamp, interval;
	int histogram_count = 0;
	int64_t total_count = 0;
	int rc;

	const char* v2_log = "src/test/unit/jHiccup-2.0.7S.logV2.hlog";

	FILE* f = fopen(v2_log, "r");
	ck_assert_msg(f != NULL, "Can not open v2 log file");

	hdr_init(1, INT64_C(3600000000000), 3, &accum);

	hdr_log_reader_init(&reader);

	rc = hdr_log_read_header(&reader, f);
	ck_assert_msg(validate_return_code(rc), "Failed to read header");

	while ((rc = hdr_log_read(&reader, f, &h, &timestamp, &interval)) != EOF)
	{
		int64_t dropped;

		ck_assert_msg(validate_return_code(rc), "Failed to read histogram");
		histogram_count++;
		total_count += h->total_count;
		dropped = hdr_add(accum, h);
		ck_assert_msg(dropped == 0, "Dropped events");

		hdr_close(h);
		h = NULL;
	}

	ck_assert_msg(histogram_count == 62, "Wrong number of histograms");
	ck_assert_msg(total_count == 48761, "Wrong total count");
	ck_assert_msg(1745879039 == hdr_value_at_percentile(accum, 99.9),
			"99.9 percentile wrong");
	ck_assert_msg(1796210687 == hdr_max(accum), "max value wrong");
	ck_assert_msg(1441812279 == reader.start_timestamp.tv_sec,
			"Seconds wrong");
	ck_assert_msg(474000000 == reader.start_timestamp.tv_nsec,
			"Nanoseconds wrong");
}
END_TEST


START_TEST(decode_v3_log)
{
	struct hdr_histogram* accum;
	struct hdr_histogram* h = NULL;
	struct hdr_log_reader reader;
	hdr_timespec timestamp;
	hdr_timespec interval;
	int rc;
	int histogram_count = 0;
	int64_t total_count = 0;

	const char* v3_log = "src/test/unit/jHiccup-2.0.7S.logV3.hlog";

	FILE* f = fopen(v3_log, "r");
	if (NULL == f)
	{
		fprintf(stderr, "Open file (%s): [%d] %s", v3_log, errno, strerror(errno));
	}
	ck_assert_msg(f != NULL, "Can not open v3 log file");

	hdr_init(1, INT64_C(3600000000000), 3, &accum);

	hdr_log_reader_init(&reader);

	rc = hdr_log_read_header(&reader, f);
	ck_assert_msg(validate_return_code(rc), "Failed to read header");

	while ((rc = hdr_log_read(&reader, f, &h, &timestamp, &interval)) != EOF)
	{
		int64_t dropped;
		ck_assert_msg(validate_return_code(rc), "Failed to read histogram");
		histogram_count++;
		total_count += h->total_count;
		dropped = hdr_add(accum, h);
		ck_assert_msg(dropped == 0, "Dropped events");

		hdr_close(h);
		h = NULL;
	}

	ck_assert_msg(histogram_count == 62, "Wrong number of histograms");
	ck_assert_msg(total_count == 48761, "Wrong total count");
	ck_assert_msg(1745879039 == hdr_value_at_percentile(accum, 99.9),
			"99.9 percentile wrong");
	ck_assert_msg(1796210687 == hdr_max(accum), "max value wrong");
	ck_assert_msg(1441812279 == reader.start_timestamp.tv_sec,
			"Seconds wrong");
	ck_assert_msg(474000000 == reader.start_timestamp.tv_nsec,
			"Nanoseconds wrong");
}
END_TEST


static int parse_line_from_file(const char* filename)
{
	struct hdr_histogram *h = NULL;
	hdr_timespec timestamp;
	hdr_timespec interval;
	int result;

	FILE* f = fopen(filename, "r");
	if (NULL == f)
	{
		fprintf(stderr, "Open file (%s): [%d] %s", filename, errno, strerror(errno));
		return -EIO;
	}

	result = hdr_log_read(NULL, f, &h, &timestamp, &interval);
	fclose(f);

	return result;
}


START_TEST(handle_invalid_log_lines)
{
	ck_assert_msg(
			-EINVAL == parse_line_from_file("src/test/unit/test_tagged_invalid_histogram.txt"),
			"Should have invalid histogram");
	ck_assert_msg(
			-EINVAL == parse_line_from_file("src/test/unit/test_tagged_invalid_tag_key.txt"),
			"Should have invalid tag key");
	ck_assert_msg(
			-EINVAL == parse_line_from_file("src/test/unit/test_tagged_invalid_timestamp.txt"),
			"Should have invalid timestamp");
	ck_assert_msg(
			-EINVAL == parse_line_from_file("src/test/unit/test_tagged_missing_histogram.txt"),
			"Should have missing histogram");
}
END_TEST


START_TEST(decode_v0_log)
{
	struct hdr_histogram* accum;
	const char* v1_log = "src/test/unit/jHiccup-2.0.1.logV0.hlog";
	struct hdr_histogram* h = NULL;
	struct hdr_log_reader reader;
	hdr_timespec timestamp;
	hdr_timespec interval;
	int rc;
	int histogram_count = 0;
	int64_t total_count = 0;

	FILE* f = fopen(v1_log, "r");
	ck_assert_msg(f != NULL, "Can not open v1 log file");

	hdr_init(1, INT64_C(3600000000000), 3, &accum);

	hdr_log_reader_init(&reader);

	rc = hdr_log_read_header(&reader, f);
	ck_assert_msg(rc == 0, "Failed to read header");

	while ((rc = hdr_log_read(&reader, f, &h, &timestamp, &interval)) != EOF)
	{
		int64_t dropped;
		ck_assert_msg(rc == 0, "Failed to read histogram");
		histogram_count++;
		total_count += h->total_count;
		dropped = hdr_add(accum, h);
		ck_assert_msg(dropped == 0, "Dropped events");

		hdr_close(h);
		h = NULL;
	}

	ck_assert_msg(histogram_count == 81, "Wrong number of histograms");
	ck_assert_msg(total_count == 61256, "Wrong total count");
	ck_assert_msg(1510998015 == hdr_value_at_percentile(accum, 99.9),
			"99.9 percentile wrong");
	ck_assert_msg(1569718271 == hdr_max(accum), "max value wrong");
	ck_assert_msg(1438869961 == reader.start_timestamp.tv_sec,
			"Seconds wrong");
	ck_assert_msg(225000000 == reader.start_timestamp.tv_nsec,
			"Nanoseconds wrong");
}
END_TEST


START_TEST(test_zz_encode_1_byte)
{
	int64_t val, dval;
	uint8_t buf[9];

	// go through every value that encodes to just 1 byte
	for (val = ~0x3f; val <= 0x3f; val++) {
		ck_assert_int_eq(zig_zag_encode_i64(buf, val), 1);

		ck_assert_int_eq(zig_zag_decode_i64(buf, &dval), 1);
		ck_assert_int_eq(val, dval);
	}
}
END_TEST


START_TEST(test_zz_encode_2_bytes)
{
	int64_t val, dval;
	uint8_t buf[9];

	// cycle through a bunch of values which encode to 2 bytes, incrementing
	// by phi*(2^6) to hit many different combinations of first and second byte
	for (val = ~0x1fff; val <= 0x1fff; val += 0x27) {
		if (((val ^ (val >> 63)) & 0x1fc0) == 0) {
			continue;
		}
		ck_assert_int_eq(zig_zag_encode_i64(buf, val), 2);

		ck_assert_int_eq(zig_zag_decode_i64(buf, &dval), 2);
		ck_assert_int_eq(val, dval);
	}
}
END_TEST


START_TEST(test_zz_encode_3_bytes)
{
	int64_t val, dval;
	uint8_t buf[9];

	// cycle through a bunch of values which encode to 3 bytes, incrementing
	// by phi*(2^13) to hit many different combinations of first and second byte
	for (val = ~0xfffff; val <= 0xfffff; val += 0x13c7) {
		if (((val ^ (val >> 63)) & 0xfe000) == 0) {
			continue;
		}
		ck_assert_int_eq(zig_zag_encode_i64(buf, val), 3);

		ck_assert_int_eq(zig_zag_decode_i64(buf, &dval), 3);
		ck_assert_int_eq(val, dval);
	}
}
END_TEST


START_TEST(test_zz_encode_4_bytes)
{
	int64_t val, dval;
	uint8_t buf[9];

	// cycle through a bunch of values which encode to 4 bytes, incrementing
	// by phi*(2^20) to hit many different combinations of first and second byte
	for (val = ~0x7ffffff; val <= 0x7ffffff; val += 0x9e377) {
		if (((val ^ (val >> 63)) & 0x7f00000) == 0) {
			continue;
		}
		ck_assert_int_eq(zig_zag_encode_i64(buf, val), 4);

		ck_assert_int_eq(zig_zag_decode_i64(buf, &dval), 4);
		ck_assert_int_eq(val, dval);
	}
}
END_TEST


START_TEST(test_zz_encode_5_bytes)
{
	int64_t val, dval;
	uint8_t buf[9];

	// cycle through a bunch of values which encode to 4 bytes, incrementing
	// by phi*(2^27) to hit many different combinations of first and second byte
	for (val = ~0x3ffffffffl; val <= 0x3ffffffffl; val += 0x4f1bbcd) {
		if (((val ^ (val >> 63)) & 0x3f8000000l) == 0) {
			continue;
		}
		ck_assert_int_eq(zig_zag_encode_i64(buf, val), 5);

		ck_assert_int_eq(zig_zag_decode_i64(buf, &dval), 5);
		ck_assert_int_eq(val, dval);
	}
}
END_TEST


START_TEST(test_zz_encode_6_bytes)
{
	int64_t val, dval;
	uint8_t buf[9];

	// cycle through a bunch of values which encode to 4 bytes, incrementing
	// by phi*(2^34) to hit many different combinations of first and second byte
	for (val = ~0x1ffffffffffl; val <= 0x1ffffffffffl; val += 0x278dde6e5l) {
		if (((val ^ (val >> 63)) & 0x1fc00000000l) == 0) {
			continue;
		}
		ck_assert_int_eq(zig_zag_encode_i64(buf, val), 6);

		ck_assert_int_eq(zig_zag_decode_i64(buf, &dval), 6);
		ck_assert_int_eq(val, dval);
	}
}
END_TEST


START_TEST(test_zz_encode_7_bytes)
{
	int64_t val, dval;
	uint8_t buf[9];

	// cycle through a bunch of values which encode to 4 bytes, incrementing
	// by phi*(2^41) to hit many different combinations of first and second byte
	for (val = ~0xffffffffffffl; val <= 0xffffffffffffl; val += 0x13c6ef372ffl) {
		if (((val ^ (val >> 63)) & 0xfe0000000000l) == 0) {
			continue;
		}
		ck_assert_int_eq(zig_zag_encode_i64(buf, val), 7);

		ck_assert_int_eq(zig_zag_decode_i64(buf, &dval), 7);
		ck_assert_int_eq(val, dval);
	}
}
END_TEST


START_TEST(test_zz_encode_8_bytes)
{
	int64_t val, dval;
	uint8_t buf[9];

	// cycle through a bunch of values which encode to 4 bytes, incrementing
	// by phi*(2^48) to hit many different combinations of first and second byte
	for (val = ~0x7fffffffffffffl; val <= 0x7fffffffffffffl; val += 0x9e3779b97f4bl) {
		if (((val ^ (val >> 63)) & 0x7f000000000000l) == 0) {
			continue;
		}
		ck_assert_int_eq(zig_zag_encode_i64(buf, val), 8);

		ck_assert_int_eq(zig_zag_decode_i64(buf, &dval), 8);
		ck_assert_int_eq(val, dval);
	}
}
END_TEST


START_TEST(test_zz_encode_9_bytes)
{
	int64_t val, dval;
	uint8_t buf[9];

	// cycle through a bunch of values which encode to 4 bytes, incrementing
	// by phi*(2^55) to hit many different combinations of first and second byte
	for (val = ~0x7fffffffffffffffl; val <= 0x7fffffffffffffffl;) {
		if (((val ^ (val >> 63)) & 0x7f80000000000000l) != 0) {
			ck_assert_int_eq(zig_zag_encode_i64(buf, val), 9);

			ck_assert_int_eq(zig_zag_decode_i64(buf, &dval), 9);
			ck_assert_int_eq(val, dval);
		}

		int64_t next_val = val + 0x4f1bbcdcbfa53fl;
		if ((next_val & (~val)) < 0) {
			// when next_val overflows (i.e. goes negative), then we are done
			break;
		}
		val = next_val;
	}
}
END_TEST


Suite*
hdr_histogram_log_suite(void)
{
	Suite* s;
	TCase* tc_encode;
	TCase* tc_b64_decode;
	TCase* tc_b64_encode;
	TCase* tc_read_write;
	TCase* tc_string_encode;
	TCase* tc_decode_log;
	TCase* tc_encode_empty;
	TCase* tc_zz_encode;

	s = suite_create("HDR Histogram Log");

	/* encoding tests */
	tc_encode = tcase_create("Encode");
	tcase_add_checked_fixture(tc_encode, encode_setup, encode_teardown);
	tcase_add_test(tc_encode, test_encode_decode_empty);
	tcase_add_test(tc_encode, test_encode_and_decode_compressed);
	tcase_add_test(tc_encode, test_encode_and_decode_compressed2);
	tcase_add_test(tc_encode, test_encode_and_decode_compressed_large);
	tcase_add_test(tc_encode, test_encode_and_decode_base64);
	tcase_add_test(tc_encode, test_bounds_check_on_decode);
	suite_add_tcase(s, tc_encode);

	/* base 64 decoding tests */
	tc_b64_decode = tcase_create("B64 decode");
	tcase_add_test(tc_b64_decode, base64_decode_block_decodes_4_chars);
	tcase_add_test(tc_b64_decode, base64_decode_fails_with_invalid_lengths);
	tcase_add_test(tc_b64_decode, base64_decode_decodes_strings_without_padding);
	tcase_add_test(tc_b64_decode, base64_decode_decodes_strings_with_padding);
	suite_add_tcase(s, tc_b64_decode);

	/* base 64 decoding tests */
	tc_b64_encode = tcase_create("B64 decode");
	tcase_add_test(tc_b64_encode, base64_encode_block_encodes_3_bytes);
	tcase_add_test(tc_b64_encode, base64_encode_fails_with_invalid_lengths);
	tcase_add_test(tc_b64_encode, base64_encode_encodes_without_padding);
	tcase_add_test(tc_b64_encode, base64_encode_encodes_with_padding);
	suite_add_tcase(s, tc_b64_encode);

	/* read/write log tests */
	tc_read_write = tcase_create("Read/write log");
	tcase_add_checked_fixture(tc_read_write, rw_setup, rw_teardown);
	tcase_add_test(tc_read_write, writes_and_reads_log);
	tcase_add_test(tc_read_write, log_reader_aggregates_into_single_histogram);
	tcase_add_test(tc_read_write, log_reader_fails_with_incorrect_version);
	suite_add_tcase(s, tc_read_write);

	/* string encoding tests */
	tc_string_encode = tcase_create("String encode");
	tcase_add_test(tc_string_encode, test_string_encode_decode);
	tcase_add_test(tc_string_encode, test_string_encode_decode_2);
	suite_add_tcase(s, tc_string_encode);

	/* decode log tests */
	tc_decode_log = tcase_create("Decode log");
	tcase_add_test(tc_decode_log, decode_v3_log);
	tcase_add_test(tc_decode_log, decode_v2_log);
	tcase_add_test(tc_decode_log, decode_v1_log);
	tcase_add_test(tc_decode_log, decode_v0_log);
	tcase_add_test(tc_decode_log, handle_invalid_log_lines);
	suite_add_tcase(s, tc_decode_log);

	/* encode empty test */
	tc_encode_empty = tcase_create("Encode/decode empty");
	tcase_add_test(tc_encode_empty, test_encode_and_decode_empty);
	suite_add_tcase(s, tc_encode_empty);

	/* zig-zag encode/decode test */
	tc_zz_encode = tcase_create("Zig-Zag encode/decode");
	tcase_add_test(tc_zz_encode, test_zz_encode_1_byte);
	tcase_add_test(tc_zz_encode, test_zz_encode_2_bytes);
	tcase_add_test(tc_zz_encode, test_zz_encode_3_bytes);
	tcase_add_test(tc_zz_encode, test_zz_encode_4_bytes);
	tcase_add_test(tc_zz_encode, test_zz_encode_5_bytes);
	tcase_add_test(tc_zz_encode, test_zz_encode_6_bytes);
	tcase_add_test(tc_zz_encode, test_zz_encode_7_bytes);
	tcase_add_test(tc_zz_encode, test_zz_encode_8_bytes);
	tcase_add_test(tc_zz_encode, test_zz_encode_9_bytes);
	suite_add_tcase(s, tc_zz_encode);

	return s;
}


#if defined(_MSC_VER)
#pragma warning(pop)
#endif
