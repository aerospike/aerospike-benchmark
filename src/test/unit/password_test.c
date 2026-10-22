
#include <check.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "benchmark.h"
#include "common.h"


#define TEST_SUITE_NAME "password"

#define ENV_VAR "ASBENCH_TEST_PW"
#define ENV_UNSET "ASBENCH_TEST_PW_UNSET"
#define SECRET "s3cr3t-pw"
#define KEYFILE_SECRET "kp-s3cr3t"
#define NBSP "\xc2\xa0"

extern int set_args(int argc, char * const* argv, args_t* args);
extern void _load_defaults(args_t* args);
extern void _free_args(args_t* args);


static char tmp_path[64];
static char err_buf[1024];


static void
setup(void)
{
	unsetenv(ENV_VAR);
	unsetenv(ENV_UNSET);
	tmp_path[0] = '\0';
	err_buf[0] = '\0';
}

static void
teardown(void)
{
	unsetenv(ENV_VAR);

	if (tmp_path[0] != '\0') {
		unlink(tmp_path);
	}
}

static void
write_tmp_file(const char* contents, size_t len)
{
	strcpy(tmp_path, "/tmp/asbench_pw_XXXXXX");
	int fd = mkstemp(tmp_path);
	ck_assert_int_ge(fd, 0);
	ck_assert_int_eq(write(fd, contents, len), (ssize_t) len);
	close(fd);
}

static FILE* capture_tmp;
static int capture_saved;

static void
capture_start(void)
{
	fflush(stderr);
	capture_saved = dup(STDERR_FILENO);
	capture_tmp = tmpfile();
	ck_assert_ptr_ne(capture_tmp, NULL);
	dup2(fileno(capture_tmp), STDERR_FILENO);
}

static void
capture_end(void)
{
	fflush(stderr);
	dup2(capture_saved, STDERR_FILENO);
	close(capture_saved);

	rewind(capture_tmp);
	size_t n = fread(err_buf, 1, sizeof(err_buf) - 1, capture_tmp);
	err_buf[n] = '\0';
	fclose(capture_tmp);
}

static bool
resolve_opts(const char* option, const char* value, size_t max_len,
		bool first_line, char** pw)
{
	capture_start();
	bool ok = read_password(option, value, max_len, first_line, pw);
	capture_end();
	return ok;
}

static bool
resolve_max(const char* option, const char* value, size_t max_len, char** pw)
{
	return resolve_opts(option, value, max_len, false, pw);
}

static bool
resolve(const char* option, const char* value, char** pw)
{
	return resolve_max(option, value, TLS_KEYFILE_PASSWORD_MAX_LEN, pw);
}

static void
assert_resolves(const char* value, const char* expected)
{
	char* pw = NULL;
	ck_assert(resolve("--password", value, &pw));
	ck_assert_str_eq(pw, expected);
	ck_assert_str_eq(err_buf, "");
	cf_free(pw);
}

static void
assert_fails(const char* option, const char* value, const char* expected_err)
{
	char* pw = NULL;
	ck_assert(!resolve(option, value, &pw));
	ck_assert_ptr_eq(pw, NULL);
	ck_assert_str_eq(err_buf, expected_err);
}


START_TEST(literal)
{
	assert_resolves(SECRET, SECRET);
}
END_TEST

START_TEST(literal_unknown_prefix)
{
	assert_resolves("foo:bar", "foo:bar");
	assert_resolves("secrets:resource:key", "secrets:resource:key");
}
END_TEST

START_TEST(literal_prefix_case_sensitive)
{
	setenv(ENV_VAR, SECRET, 1);
	assert_resolves("ENV:" ENV_VAR, "ENV:" ENV_VAR);
	assert_resolves("File:/etc/hosts", "File:/etc/hosts");
	assert_resolves("B64:c2VjcmV0", "B64:c2VjcmV0");
}
END_TEST

START_TEST(literal_empty)
{
	assert_fails("--password", "", "--password: password is empty\n");
}
END_TEST

START_TEST(env)
{
	setenv(ENV_VAR, SECRET, 1);
	assert_resolves("env:" ENV_VAR, SECRET);
}
END_TEST

START_TEST(env_value_not_parsed_again)
{
	setenv(ENV_VAR, "b64:c2VjcmV0", 1);
	assert_resolves("env:" ENV_VAR, "b64:c2VjcmV0");
}
END_TEST

START_TEST(env_unset)
{
	assert_fails("--password", "env:" ENV_UNSET, "--password: environment "
			"variable " ENV_UNSET " is not set or empty\n");
}
END_TEST

START_TEST(env_no_name)
{
	assert_fails("--password", "env:",
			"--password: env: requires a variable name\n");
	assert_fails("--tls-keyfile-password", "env:",
			"--tls-keyfile-password: env: requires a variable name\n");
}
END_TEST

START_TEST(env_empty)
{
	setenv(ENV_VAR, "", 1);
	assert_fails("--tls-keyfile-password", "env:" ENV_VAR,
			"--tls-keyfile-password: environment variable " ENV_VAR
			" is not set or empty\n");
}
END_TEST

START_TEST(env_b64)
{
	setenv(ENV_VAR, "czNjcjN0LXB3", 1);
	assert_resolves("env-b64:" ENV_VAR, SECRET);
}
END_TEST

START_TEST(env_b64_trailing_newline)
{
	// echo s3cr3t-pw | base64
	setenv(ENV_VAR, "czNjcjN0LXB3Cg==", 1);
	assert_resolves("env-b64:" ENV_VAR, SECRET);
	setenv(ENV_VAR, "czNjcjN0LXB3\n", 1);
	assert_resolves("env-b64:" ENV_VAR, SECRET);
}
END_TEST

START_TEST(env_b64_unset)
{
	assert_fails("--password", "env-b64:" ENV_UNSET, "--password: environment "
			"variable " ENV_UNSET " is not set or empty\n");
}
END_TEST

START_TEST(env_b64_empty)
{
	setenv(ENV_VAR, "", 1);
	assert_fails("--password", "env-b64:" ENV_VAR, "--password: environment "
			"variable " ENV_VAR " is not set or empty\n");
}
END_TEST

START_TEST(env_b64_no_name)
{
	assert_fails("--password", "env-b64:",
			"--password: env-b64: requires a variable name\n");
	assert_fails("--tls-keyfile-password", "env-b64:",
			"--tls-keyfile-password: env-b64: requires a variable name\n");
}
END_TEST

START_TEST(env_b64_invalid)
{
	setenv(ENV_VAR, SECRET, 1);
	assert_fails("--tls-keyfile-password", "env-b64:" ENV_VAR,
			"--tls-keyfile-password: invalid base64 in environment variable "
			ENV_VAR "\n");
	ck_assert_ptr_eq(strstr(err_buf, SECRET), NULL);
}
END_TEST

START_TEST(env_b64_decodes_empty)
{
	setenv(ENV_VAR, "Cg==", 1);
	assert_fails("--password", "env-b64:" ENV_VAR, "--password: password from "
			"environment variable " ENV_VAR " is empty\n");
}
END_TEST

START_TEST(b64)
{
	assert_resolves("b64:czNjcjN0LXB3", SECRET);
	assert_resolves("b64:YQ==", "a");
	assert_resolves("b64:YWI=", "ab");
}
END_TEST

START_TEST(b64_trailing_newline)
{
	assert_resolves("b64:czNjcjN0LXB3Cg==", SECRET);
	assert_resolves("b64:czNjcjN0LXB3Cgo=", SECRET "\n");
}
END_TEST

START_TEST(b64_value_not_parsed_again)
{
	// b64:ZW52OkFTQkVOQ0hfVEVTVF9QVw== is "env:ASBENCH_TEST_PW"
	setenv(ENV_VAR, SECRET, 1);
	assert_resolves("b64:ZW52OkFTQkVOQ0hfVEVTVF9QVw==", "env:" ENV_VAR);
}
END_TEST

START_TEST(b64_invalid_chars)
{
	assert_fails("--password", "b64:" SECRET "!",
			"--password: invalid base64 in b64: value\n");
	ck_assert_ptr_eq(strstr(err_buf, SECRET), NULL);
}
END_TEST

START_TEST(b64_invalid_length)
{
	assert_fails("--tls-keyfile-password", "b64:czNjcjN0LXB",
			"--tls-keyfile-password: invalid base64 in b64: value\n");
	ck_assert_ptr_eq(strstr(err_buf, "czNjcjN0LXB"), NULL);
}
END_TEST

START_TEST(b64_invalid_padding)
{
	assert_fails("--password", "b64:YQ=a",
			"--password: invalid base64 in b64: value\n");
	assert_fails("--password", "b64:YQ",
			"--password: invalid base64 in b64: value\n");
}
END_TEST

START_TEST(b64_line_breaks_skipped)
{
	assert_resolves("b64:czNjcjN0\nLXB3", SECRET);
	assert_resolves("b64:czNjcjN0LXB3Cg==\r\n", SECRET);
}
END_TEST

START_TEST(b64_missing)
{
	assert_fails("--password", "b64:",
			"--password: password from b64: value is empty\n");
	assert_fails("--tls-keyfile-password", "b64:",
			"--tls-keyfile-password: password from b64: value is empty\n");
}
END_TEST

START_TEST(b64_only_line_breaks)
{
	assert_fails("--password", "b64:\r\n\n",
			"--password: password from b64: value is empty\n");
	assert_fails("--tls-keyfile-password", "b64:\n",
			"--tls-keyfile-password: password from b64: value is empty\n");
}
END_TEST

START_TEST(env_b64_only_line_breaks)
{
	setenv(ENV_VAR, "\r\n\n", 1);
	assert_fails("--password", "env-b64:" ENV_VAR, "--password: password from "
			"environment variable " ENV_VAR " is empty\n");
	setenv(ENV_VAR, "\n", 1);
	assert_fails("--tls-keyfile-password", "env-b64:" ENV_VAR,
			"--tls-keyfile-password: password from environment variable "
			ENV_VAR " is empty\n");
}
END_TEST

START_TEST(b64_decodes_empty)
{
	assert_fails("--password", "b64:Cg==",
			"--password: password from b64: value is empty\n");
	assert_fails("--tls-keyfile-password", "b64:Cg==",
			"--tls-keyfile-password: password from b64: value is empty\n");
}
END_TEST

START_TEST(file)
{
	write_tmp_file(SECRET "\n", strlen(SECRET "\n"));
	char value[128];
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	assert_resolves(value, SECRET);
}
END_TEST

START_TEST(file_no_line_ending)
{
	write_tmp_file(SECRET, strlen(SECRET));
	char value[128];
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	assert_resolves(value, SECRET);
}
END_TEST

START_TEST(file_crlf)
{
	write_tmp_file(SECRET "\r\n", strlen(SECRET "\r\n"));
	char value[128];
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	assert_resolves(value, SECRET);
}
END_TEST

START_TEST(file_one_line_ending_removed)
{
	write_tmp_file(SECRET "\n\n", strlen(SECRET "\n\n"));
	char value[128];
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	assert_resolves(value, SECRET "\n");
}
END_TEST

START_TEST(file_multi_line)
{
	write_tmp_file("line1\nline2\n", strlen("line1\nline2\n"));
	char value[128];
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	assert_resolves(value, "line1\nline2");
}
END_TEST

START_TEST(file_large)
{
	size_t len = TLS_KEYFILE_PASSWORD_MAX_LEN;
	char* contents = malloc(len + 2);
	memset(contents, 'x', len);
	contents[len] = '\n';
	contents[len + 1] = '\0';
	write_tmp_file(contents, len + 1);

	char value[128];
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	contents[len] = '\0';
	assert_resolves(value, contents);
	free(contents);
}
END_TEST

START_TEST(file_too_large)
{
	size_t len = TLS_KEYFILE_PASSWORD_MAX_LEN + 1;
	char* contents = malloc(len + 1);
	memset(contents, 'x', len);
	contents[len] = '\n';
	write_tmp_file(contents, len + 1);
	free(contents);

	char value[128];
	char expected[256];
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	snprintf(expected, sizeof(expected), "--password: value from file %s is "
			"longer than 1024 bytes\n", tmp_path);
	assert_fails("--password", value, expected);
}
END_TEST

START_TEST(file_no_path)
{
	assert_fails("--password", "file:", "--password: file: requires a path\n");
	assert_fails("--tls-keyfile-password", "file:",
			"--tls-keyfile-password: file: requires a path\n");
}
END_TEST

START_TEST(file_nul)
{
	write_tmp_file("\0" SECRET "\n", strlen(SECRET) + 2);
	char value[128];
	char expected[256];
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	snprintf(expected, sizeof(expected), "--password: file %s contains a NUL "
			"byte\n", tmp_path);
	assert_fails("--password", value, expected);
}
END_TEST

START_TEST(file_dev_zero)
{
	char* pw = NULL;
	ck_assert(!resolve_max("--password", "file:/dev/zero", 63, &pw));
	ck_assert_ptr_eq(pw, NULL);
	ck_assert_str_eq(err_buf, "--password: file /dev/zero contains a NUL "
			"byte\n");
}
END_TEST

START_TEST(b64_nul)
{
	// b64:YQBi is "a\0b"
	assert_fails("--password", "b64:YQBi", "--password: b64: value decodes to a "
			"value containing a NUL byte\n");
}
END_TEST

START_TEST(env_b64_nul)
{
	setenv(ENV_VAR, "YQBi", 1);
	assert_fails("--tls-keyfile-password", "env-b64:" ENV_VAR,
			"--tls-keyfile-password: environment variable " ENV_VAR " decodes "
			"to a value containing a NUL byte\n");
}
END_TEST

START_TEST(file_value_not_parsed_again)
{
	setenv(ENV_VAR, SECRET, 1);
	write_tmp_file("env:" ENV_VAR "\n", strlen("env:" ENV_VAR "\n"));
	char value[128];
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	assert_resolves(value, "env:" ENV_VAR);
}
END_TEST

START_TEST(file_missing)
{
	assert_fails("--password", "file:/nonexistent/asbench_pw",
			"--password: cannot read file /nonexistent/asbench_pw: No such "
			"file or directory\n");
}
END_TEST

START_TEST(file_is_directory)
{
	char* pw = NULL;
	ck_assert(!resolve("--tls-keyfile-password", "file:/", &pw));
	ck_assert_ptr_eq(pw, NULL);
	ck_assert_ptr_eq(strstr(err_buf, "--tls-keyfile-password: cannot read "
				"file /: "), err_buf);
}
END_TEST

START_TEST(file_empty)
{
	write_tmp_file("", 0);
	char value[128];
	char expected[256];
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	snprintf(expected, sizeof(expected), "--password: password from file %s is "
			"empty\n", tmp_path);
	assert_fails("--password", value, expected);
}
END_TEST

static char*
repeat(char c, size_t n)
{
	char* s = malloc(n + 1);
	memset(s, c, n);
	s[n] = '\0';
	return s;
}

START_TEST(max_len_env)
{
	char* ok = repeat('x', 63);
	char* pw = NULL;

	setenv(ENV_VAR, ok, 1);
	ck_assert(resolve_max("--password", "env:" ENV_VAR, 63, &pw));
	ck_assert_str_eq(pw, ok);
	cf_free(pw);
	pw = NULL;

	char* too_long = repeat('x', 64);
	setenv(ENV_VAR, too_long, 1);
	ck_assert(!resolve_max("--password", "env:" ENV_VAR, 63, &pw));
	ck_assert_ptr_eq(pw, NULL);
	ck_assert_str_eq(err_buf, "--password: value from environment variable "
			ENV_VAR " is longer than 63 bytes\n");

	free(ok);
	free(too_long);
}
END_TEST

START_TEST(max_len_env_b64)
{
	char* pw = NULL;

	// 64 bytes of 'x'
	setenv(ENV_VAR, "eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4"
			"eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eA==", 1);
	ck_assert(!resolve_max("--password", "env-b64:" ENV_VAR, 63, &pw));
	ck_assert_ptr_eq(pw, NULL);
	ck_assert_str_eq(err_buf, "--password: decoded value from environment "
			"variable " ENV_VAR " is longer than 63 bytes\n");
}
END_TEST

START_TEST(max_len_b64)
{
	char* pw = NULL;

	// 63 and 64 bytes of 'x'
	ck_assert(resolve_max("--password", "b64:eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4"
				"eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4", 63, &pw));
	ck_assert_uint_eq(strlen(pw), 63);
	cf_free(pw);
	pw = NULL;

	ck_assert(!resolve_max("--password", "b64:eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4"
				"eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eA==", 63, &pw));
	ck_assert_ptr_eq(pw, NULL);
	ck_assert_str_eq(err_buf, "--password: decoded b64: value is longer than "
			"63 bytes\n");
}
END_TEST

START_TEST(max_len_file_trailing_newline)
{
	char* contents = repeat('x', 64);
	char value[128];
	char* pw = NULL;

	contents[63] = '\n';
	write_tmp_file(contents, 64);
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	ck_assert(resolve_max("--password", value, 63, &pw));
	ck_assert_uint_eq(strlen(pw), 63);
	cf_free(pw);
	free(contents);
}
END_TEST

START_TEST(max_len_file_too_long)
{
	char* contents = repeat('x', 65);
	char value[128];
	char expected[256];
	char* pw = NULL;

	contents[64] = '\n';
	write_tmp_file(contents, 65);
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	snprintf(expected, sizeof(expected), "--password: value from file %s is "
			"longer than 63 bytes\n", tmp_path);
	ck_assert(!resolve_max("--password", value, 63, &pw));
	ck_assert_ptr_eq(pw, NULL);
	ck_assert_str_eq(err_buf, expected);
	free(contents);
}
END_TEST

START_TEST(max_len_literal)
{
	char* ok = repeat('x', 63);
	char* too_long = repeat('x', 64);
	char* pw = NULL;

	ck_assert(resolve_max("--password", ok, 63, &pw));
	ck_assert_str_eq(pw, ok);
	ck_assert_str_eq(err_buf, "");
	cf_free(pw);
	pw = NULL;

	ck_assert(!resolve_max("--password", too_long, 63, &pw));
	ck_assert_ptr_eq(pw, NULL);
	ck_assert_str_eq(err_buf, "--password: password is longer than 63 bytes\n");

	free(ok);
	free(too_long);
}
END_TEST

START_TEST(max_len_keyfile_literal)
{
	char* ok = repeat('x', TLS_KEYFILE_PASSWORD_MAX_LEN);
	char* too_long = repeat('x', TLS_KEYFILE_PASSWORD_MAX_LEN + 1);
	char* pw = NULL;

	ck_assert(resolve_opts("--tls-keyfile-password", ok,
				TLS_KEYFILE_PASSWORD_MAX_LEN, true, &pw));
	ck_assert_str_eq(pw, ok);
	cf_free(pw);
	pw = NULL;

	ck_assert(!resolve_opts("--tls-keyfile-password", too_long,
				TLS_KEYFILE_PASSWORD_MAX_LEN, true, &pw));
	ck_assert_ptr_eq(pw, NULL);
	ck_assert_str_eq(err_buf, "--tls-keyfile-password: password is longer "
			"than 1024 bytes\n");

	free(ok);
	free(too_long);
}
END_TEST

START_TEST(max_len_keyfile_env)
{
	char* ok = repeat('x', TLS_KEYFILE_PASSWORD_MAX_LEN);
	char* too_long = repeat('x', TLS_KEYFILE_PASSWORD_MAX_LEN + 1);
	char* pw = NULL;

	setenv(ENV_VAR, ok, 1);
	ck_assert(resolve_opts("--tls-keyfile-password", "env:" ENV_VAR,
				TLS_KEYFILE_PASSWORD_MAX_LEN, true, &pw));
	ck_assert_str_eq(pw, ok);
	cf_free(pw);
	pw = NULL;

	setenv(ENV_VAR, too_long, 1);
	ck_assert(!resolve_opts("--tls-keyfile-password", "env:" ENV_VAR,
				TLS_KEYFILE_PASSWORD_MAX_LEN, true, &pw));
	ck_assert_ptr_eq(pw, NULL);
	ck_assert_str_eq(err_buf, "--tls-keyfile-password: value from environment "
			"variable " ENV_VAR " is longer than 1024 bytes\n");

	free(ok);
	free(too_long);
}
END_TEST

START_TEST(has_source)
{
	ck_assert(password_has_source("env:X"));
	ck_assert(password_has_source("env-b64:X"));
	ck_assert(password_has_source("b64:X"));
	ck_assert(password_has_source("file:X"));
	ck_assert(password_has_source("env:"));
	ck_assert(!password_has_source(SECRET));
	ck_assert(!password_has_source("ENV:X"));
	ck_assert(!password_has_source("secrets:resource:key"));
	ck_assert(!password_has_source(""));
}
END_TEST

START_TEST(trim)
{
	char all[] = " \t\n\v\f\r" SECRET " \t\n\v\f\r";
	char inner[] = " a b\tc ";
	char blank[] = " \t\r\n";
	char empty[] = "";
	char nbsp[] = NBSP SECRET NBSP;
	char latin1_nbsp[] = "\xa0" SECRET "\xa0";

	ck_assert_str_eq(password_trim(all), SECRET);
	ck_assert_str_eq(password_trim(inner), "a b\tc");
	ck_assert_str_eq(password_trim(blank), "");
	ck_assert_str_eq(password_trim(empty), "");
	ck_assert_str_eq(password_trim(nbsp), NBSP SECRET NBSP);
	ck_assert_str_eq(password_trim(latin1_nbsp), "\xa0" SECRET "\xa0");
}
END_TEST

START_TEST(file_only_line_ending)
{
	write_tmp_file("\r\n", 2);
	char value[128];
	char expected[256];
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	snprintf(expected, sizeof(expected), "--password: password from file %s is "
			"empty\n", tmp_path);
	assert_fails("--password", value, expected);
}
END_TEST

static bool
resolve_first_line(const char* contents, size_t len, char** pw)
{
	char value[128];

	if (tmp_path[0] != '\0') {
		unlink(tmp_path);
	}

	write_tmp_file(contents, len);
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	return resolve_opts("--tls-keyfile-password", value,
			TLS_KEYFILE_PASSWORD_MAX_LEN, true, pw);
}

static void
assert_first_line(const char* contents, const char* expected)
{
	char* pw = NULL;
	ck_assert(resolve_first_line(contents, strlen(contents), &pw));
	ck_assert_str_eq(pw, expected);
	ck_assert_str_eq(err_buf, "");
	cf_free(pw);
}

static void
assert_first_line_empty(const char* contents)
{
	char expected[256];
	char* pw = NULL;
	ck_assert(!resolve_first_line(contents, strlen(contents), &pw));
	ck_assert_ptr_eq(pw, NULL);
	snprintf(expected, sizeof(expected), "--tls-keyfile-password: password "
			"from %sfile %s is empty\n",
			contents[0] == '\0' ? "" : "first line of ", tmp_path);
	ck_assert_str_eq(err_buf, expected);
}

START_TEST(file_first_line)
{
	assert_first_line(SECRET, SECRET);
	assert_first_line(SECRET "\n", SECRET);
	assert_first_line(SECRET "\r\n", SECRET);
	assert_first_line(SECRET "\n\n", SECRET);
	assert_first_line(SECRET "\r", SECRET);
	assert_first_line(SECRET "\nmore", SECRET);
	assert_first_line(SECRET "\rmore\n", SECRET);
	assert_first_line(SECRET "\r\n\r\n", SECRET);
	assert_first_line(SECRET "\r\r\n", SECRET);
}
END_TEST

START_TEST(file_first_line_empty)
{
	assert_first_line_empty("");
	assert_first_line_empty("\n" SECRET);
	assert_first_line_empty("\r\n" SECRET "\n");
	assert_first_line_empty("\n\n");
	assert_first_line_empty("\r");
}
END_TEST

START_TEST(file_first_line_large)
{
	size_t len = TLS_KEYFILE_PASSWORD_MAX_LEN;
	char* contents = repeat('x', len + strlen("\nmore"));
	char* pw = NULL;

	memcpy(contents + len, "\nmore", strlen("\nmore"));
	ck_assert(resolve_first_line(contents, strlen(contents), &pw));
	contents[len] = '\0';
	ck_assert_str_eq(pw, contents);
	cf_free(pw);
	free(contents);
}
END_TEST

START_TEST(file_first_line_too_large)
{
	size_t len = TLS_KEYFILE_PASSWORD_MAX_LEN + 1;
	char* contents = repeat('x', len + strlen("\nmore"));
	char expected[256];
	char* pw = NULL;

	memcpy(contents + len, "\nmore", strlen("\nmore"));
	ck_assert(!resolve_first_line(contents, strlen(contents), &pw));
	ck_assert_ptr_eq(pw, NULL);
	snprintf(expected, sizeof(expected), "--tls-keyfile-password: value from "
			"file %s is longer than 1024 bytes\n", tmp_path);
	ck_assert_str_eq(err_buf, expected);
	free(contents);
}
END_TEST

START_TEST(file_first_line_nul)
{
	char expected[256];
	char* pw = NULL;

	ck_assert(!resolve_first_line(SECRET "\0\nmore", strlen(SECRET) + 6, &pw));
	ck_assert_ptr_eq(pw, NULL);
	snprintf(expected, sizeof(expected), "--tls-keyfile-password: file %s "
			"contains a NUL byte\n", tmp_path);
	ck_assert_str_eq(err_buf, expected);
}
END_TEST


static int
parse(args_t* args, int argc, char** argv)
{
	optind = 0;
	_load_defaults(args);
	return set_args(argc, argv, args);
}

static int
parse_capture(args_t* args, int argc, char** argv)
{
	capture_start();
	int rc = parse(args, argc, argv);
	capture_end();
	return rc;
}

static int
parse_typed(args_t* args, int argc, char** argv, const char* typed)
{
	write_tmp_file(typed, strlen(typed));
	int saved = dup(STDIN_FILENO);
	ck_assert_ptr_ne(freopen(tmp_path, "r", stdin), NULL);

	int rc = parse_capture(args, argc, argv);

	dup2(saved, STDIN_FILENO);
	close(saved);
	return rc;
}

static void
free_parsed(args_t* args)
{
	cf_free(args->tls.keyfile);
	cf_free(args->tls.keyfile_pw);
	_free_args(args);
}

START_TEST(cli_password_env_space_separated)
{
	setenv(ENV_VAR, SECRET, 1);
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P", "env:" ENV_VAR };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.password, SECRET);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_password_b64_attached)
{
	args_t args;
	char* argv[] = { "asbench", "-Pb64:czNjcjN0LXB3", "-U", "admin" };
	ck_assert_int_eq(parse(&args, 4, argv), 0);
	ck_assert_str_eq(args.password, SECRET);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_password_long_form)
{
	setenv(ENV_VAR, "czNjcjN0LXB3", 1);
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "--password=env-b64:" ENV_VAR };
	ck_assert_int_eq(parse(&args, 4, argv), 0);
	ck_assert_str_eq(args.password, SECRET);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_password_file)
{
	write_tmp_file(SECRET "\n", strlen(SECRET "\n"));
	char value[128];
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "--password", value };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.password, SECRET);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_password_file_not_first_line)
{
	write_tmp_file(SECRET "\nmore\n", strlen(SECRET "\nmore\n"));
	char value[128];
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "--password", value };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.password, SECRET "\nmore");
	free_parsed(&args);
}
END_TEST

START_TEST(cli_password_literal)
{
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P", SECRET };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.password, SECRET);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_password_env_unset_fails)
{
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P", "env:" ENV_UNSET };
	ck_assert_int_ne(parse(&args, 5, argv), 0);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_password_resolved_once_last_wins)
{
	args_t args;
	char* argv[] = { "asbench", "-P", "env:" ENV_UNSET, "-U", "admin",
		"-P" SECRET };
	ck_assert_int_eq(parse(&args, 6, argv), 0);
	ck_assert_str_eq(args.password, SECRET);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_password_not_resolved_without_user)
{
	args_t args;
	char* argv[] = { "asbench", "-P", "env:" ENV_UNSET };
	ck_assert_int_eq(parse(&args, 3, argv), 0);
	ck_assert_str_eq(args.password, "");
	free_parsed(&args);
}
END_TEST

START_TEST(cli_password_too_long_fails)
{
	char* too_long = repeat('x', 64);
	setenv(ENV_VAR, too_long, 1);

	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P", "env:" ENV_VAR };
	ck_assert_int_ne(parse(&args, 5, argv), 0);
	free_parsed(&args);
	free(too_long);
}
END_TEST

START_TEST(cli_password_max_len)
{
	char* ok = repeat('x', 63);
	setenv(ENV_VAR, ok, 1);

	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P", "env:" ENV_VAR };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.password, ok);
	free_parsed(&args);
	free(ok);
}
END_TEST

START_TEST(cli_password_literal_max_len)
{
	char* literal = repeat('x', 63);

	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P", literal };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.password, literal);
	free_parsed(&args);
	free(literal);
}
END_TEST

START_TEST(cli_password_long_literal_fails)
{
	char* literal = repeat('x', 64);

	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P", literal };
	ck_assert_int_ne(parse_capture(&args, 5, argv), 0);
	ck_assert_str_eq(err_buf, "--password: password is longer than 63 bytes\n");
	free_parsed(&args);
	free(literal);
}
END_TEST

START_TEST(cli_password_no_name_fails)
{
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P", "env-b64:" };
	ck_assert_int_ne(parse_capture(&args, 5, argv), 0);
	ck_assert_str_eq(err_buf, "--password: env-b64: requires a variable name\n");
	free_parsed(&args);
}
END_TEST

START_TEST(cli_typed_password_max_len)
{
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P" };
	char* typed = repeat('x', 63);
	char line[128];

	snprintf(line, sizeof(line), "%s\n", typed);
	ck_assert_int_eq(parse_typed(&args, 4, argv, line), 0);
	ck_assert_str_eq(args.password, typed);
	ck_assert_str_eq(err_buf, "");
	free_parsed(&args);
	free(typed);
}
END_TEST

START_TEST(cli_typed_password_too_long_fails)
{
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P" };
	char* typed = repeat('x', 64);
	char line[128];

	snprintf(line, sizeof(line), "%s\n", typed);
	ck_assert_int_ne(parse_typed(&args, 4, argv, line), 0);
	ck_assert_str_eq(err_buf, "--password: password is longer than 63 bytes\n");
	free_parsed(&args);
	free(typed);
}
END_TEST

START_TEST(cli_typed_password_without_flag_too_long_fails)
{
	args_t args;
	char* argv[] = { "asbench", "-U", "admin" };
	char* typed = repeat('x', 100);
	char line[128];

	snprintf(line, sizeof(line), "%s\n", typed);
	ck_assert_int_ne(parse_typed(&args, 3, argv, line), 0);
	ck_assert_str_eq(err_buf, "--password: password is longer than 63 bytes\n");
	free_parsed(&args);
	free(typed);
}
END_TEST

START_TEST(cli_typed_password_is_literal)
{
	setenv(ENV_VAR, SECRET, 1);
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P" };
	ck_assert_int_eq(parse_typed(&args, 4, argv, "env:" ENV_VAR "\n"), 0);
	ck_assert_str_eq(args.password, "env:" ENV_VAR);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_password_literal_not_trimmed)
{
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P", " " SECRET " \t" };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.password, " " SECRET " \t");
	free_parsed(&args);
}
END_TEST

START_TEST(cli_piped_password_trimmed)
{
	const char* piped[] = { " s3cr3t \r\n", " s3cr3t \n", "\t s3cr3t ",
		"s3cr3t\r", "s3cr3t\nmore\n" };

	for (size_t i = 0; i < sizeof(piped) / sizeof(piped[0]); i++) {
		args_t args;
		char* argv[] = { "asbench", "-U", "admin", "-P" };
		ck_assert_int_eq(parse_typed(&args, 4, argv, piped[i]), 0);
		ck_assert_str_eq(args.password, "s3cr3t");
		ck_assert_str_eq(err_buf, "");
		free_parsed(&args);
		unlink(tmp_path);
		tmp_path[0] = '\0';
	}
}
END_TEST

START_TEST(cli_piped_password_padded_is_literal)
{
	setenv(ENV_VAR, SECRET, 1);
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P" };
	ck_assert_int_eq(parse_typed(&args, 4, argv, " env:" ENV_VAR " \n"), 0);
	ck_assert_str_eq(args.password, "env:" ENV_VAR);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_piped_password_eof_is_empty)
{
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P" };
	ck_assert_int_eq(parse_typed(&args, 4, argv, ""), 0);
	ck_assert_str_eq(args.password, "");
	ck_assert_str_eq(err_buf, "");
	free_parsed(&args);
}
END_TEST

START_TEST(cli_piped_password_cut_off_padded_fails)
{
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P" };
	char* typed = repeat('x', 110);

	memset(typed, ' ', 10);
	ck_assert_int_ne(parse_typed(&args, 4, argv, typed), 0);
	ck_assert_str_eq(err_buf, "--password: password is longer than 63 bytes\n");
	free_parsed(&args);
	free(typed);
}
END_TEST

START_TEST(cli_password_no_user_warns)
{
	const char* values[] = { "env:" ENV_UNSET, "env-b64:" ENV_UNSET,
		"b64:!", "file:/nonexistent/asbench_pw" };

	for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		args_t args;
		char* argv[] = { "asbench", "-P", (char*) values[i] };
		ck_assert_int_eq(parse_capture(&args, 3, argv), 0);
		ck_assert_str_eq(args.password, "");
		ck_assert_str_eq(err_buf,
				"Warning: --password: ignored because no user is set\n");
		free_parsed(&args);
	}
}
END_TEST

START_TEST(cli_password_no_user_literal_no_warning)
{
	args_t args;
	char* argv[] = { "asbench", "-P", SECRET };
	ck_assert_int_eq(parse_capture(&args, 3, argv), 0);
	ck_assert_str_eq(err_buf, "");
	free_parsed(&args);
}
END_TEST

START_TEST(cli_password_no_user_gen_bench_no_warning)
{
	args_t args;
	char* argv[] = { "asbench", "-P", "env:" ENV_UNSET, "--gen-bench", "1" };
	ck_assert_int_eq(parse_capture(&args, 5, argv), 0);
	ck_assert_str_eq(err_buf, "");
	free_parsed(&args);
}
END_TEST

START_TEST(cli_password_pki_not_resolved)
{
	char* too_long = repeat('x', 100);
	const char* values[] = { "env:" ENV_UNSET, "file:", too_long };

	for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		args_t args;
		char* argv[] = { "asbench", "--auth", "pki", "-U", "admin", "-P",
			(char*) values[i] };
		ck_assert_int_eq(parse_capture(&args, 7, argv), 0);
		ck_assert_int_eq(args.auth_mode, AS_AUTH_PKI);
		ck_assert_int_eq(strncmp(args.password, values[i],
					AS_PASSWORD_SIZE - 1), 0);
		ck_assert_str_eq(err_buf, "");
		free_parsed(&args);
	}

	free(too_long);
}
END_TEST

START_TEST(cli_password_pki_no_user_no_warning)
{
	args_t args;
	char* argv[] = { "asbench", "-P", "env:" ENV_UNSET, "--auth", "PKI" };
	ck_assert_int_eq(parse_capture(&args, 5, argv), 0);
	ck_assert_str_eq(err_buf, "");
	free_parsed(&args);
}
END_TEST

START_TEST(cli_typed_password_pki_not_checked)
{
	args_t args;
	char* argv[] = { "asbench", "--auth", "PKI", "-U", "admin", "-P" };
	char* typed = repeat('x', 64);
	char line[128];

	snprintf(line, sizeof(line), "%s\n", typed);
	ck_assert_int_eq(parse_typed(&args, 6, argv, line), 0);
	ck_assert_str_eq(err_buf, "");
	free_parsed(&args);
	free(typed);
}
END_TEST

START_TEST(cli_tls_keyfile_password_max_len)
{
	char* ok = repeat('x', TLS_KEYFILE_PASSWORD_MAX_LEN);
	setenv(ENV_VAR, ok, 1);

	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password", "env:" ENV_VAR };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.tls.keyfile_pw, ok);
	free_parsed(&args);

	char* argv_literal[] = { "asbench", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password", ok };
	ck_assert_int_eq(parse(&args, 5, argv_literal), 0);
	ck_assert_str_eq(args.tls.keyfile_pw, ok);
	free_parsed(&args);
	free(ok);
}
END_TEST

START_TEST(cli_tls_keyfile_password_too_long_fails)
{
	char* too_long = repeat('x', TLS_KEYFILE_PASSWORD_MAX_LEN + 1);
	setenv(ENV_VAR, too_long, 1);

	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password", "env:" ENV_VAR };
	ck_assert_int_ne(parse_capture(&args, 5, argv), 0);
	ck_assert_str_eq(err_buf, "--tls-keyfile-password: value from environment "
			"variable " ENV_VAR " is longer than 1024 bytes\n");
	free_parsed(&args);

	char* argv_literal[] = { "asbench", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password", too_long };
	ck_assert_int_ne(parse_capture(&args, 5, argv_literal), 0);
	ck_assert_str_eq(err_buf, "--tls-keyfile-password: password is longer "
			"than 1024 bytes\n");
	free_parsed(&args);
	free(too_long);
}
END_TEST

START_TEST(cli_tls_keyfile_password_not_resolved_without_keyfile)
{
	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile-password", "env:" ENV_UNSET };
	ck_assert_int_eq(parse_capture(&args, 3, argv), 0);
	ck_assert_str_eq(err_buf, "");
	free_parsed(&args);
}
END_TEST

START_TEST(cli_tls_keyfile_password_env)
{
	setenv(ENV_VAR, SECRET, 1);
	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile-password", "env:" ENV_VAR,
		"--tls-keyfile", "key.pem" };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.tls.keyfile_pw, SECRET);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_tls_keyfile_password_b64)
{
	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password=b64:czNjcjN0LXB3" };
	ck_assert_int_eq(parse(&args, 4, argv), 0);
	ck_assert_str_eq(args.tls.keyfile_pw, SECRET);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_tls_keyfile_password_literal)
{
	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password", SECRET };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.tls.keyfile_pw, SECRET);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_tls_keyfile_password_file_missing_fails)
{
	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password", "file:/nonexistent/asbench_pw" };
	ck_assert_int_ne(parse(&args, 5, argv), 0);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_tls_keyfile_password_file_first_line)
{
	write_tmp_file(SECRET "\r\n\r\nmore\n", strlen(SECRET "\r\n\r\nmore\n"));
	char value[128];
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password", value };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.tls.keyfile_pw, SECRET);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_tls_keyfile_password_last_wins)
{
	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password", "a", "--tls-keyfile-password",
		"b64:czNjcjN0LXB3" };
	ck_assert_int_eq(parse(&args, 7, argv), 0);
	ck_assert_str_eq(args.tls.keyfile_pw, SECRET);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_tls_keyfile_password_file_empty_first_line_fails)
{
	write_tmp_file("\n" SECRET "\n", strlen("\n" SECRET "\n"));
	char value[128];
	snprintf(value, sizeof(value), "file:%s", tmp_path);
	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password", value };
	ck_assert_int_ne(parse(&args, 5, argv), 0);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_tls_keyfile_password_padded_env)
{
	setenv(ENV_VAR, KEYFILE_SECRET, 1);
	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password", " env:" ENV_VAR " " };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.tls.keyfile_pw, KEYFILE_SECRET);
	free_parsed(&args);

	setenv(ENV_VAR, " " KEYFILE_SECRET " ", 1);
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.tls.keyfile_pw, " " KEYFILE_SECRET " ");
	free_parsed(&args);
}
END_TEST

START_TEST(cli_tls_keyfile_password_padded_literal)
{
	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password", "  " KEYFILE_SECRET " \t" };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.tls.keyfile_pw, KEYFILE_SECRET);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_tls_keyfile_password_nbsp_not_trimmed)
{
	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password", NBSP KEYFILE_SECRET };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.tls.keyfile_pw, NBSP KEYFILE_SECRET);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_piped_tls_keyfile_password_trimmed)
{
	const char* piped[] = { " " KEYFILE_SECRET " \r\n",
		" " KEYFILE_SECRET " \n", " " KEYFILE_SECRET " " };

	for (size_t i = 0; i < sizeof(piped) / sizeof(piped[0]); i++) {
		args_t args;
		char* argv[] = { "asbench", "--tls-keyfile", "key.pem",
			"--tls-keyfile-password" };
		ck_assert_int_eq(parse_typed(&args, 4, argv, piped[i]), 0);
		ck_assert_str_eq(args.tls.keyfile_pw, KEYFILE_SECRET);
		free_parsed(&args);
		unlink(tmp_path);
		tmp_path[0] = '\0';
	}
}
END_TEST

START_TEST(cli_piped_tls_keyfile_password_padded_env)
{
	setenv(ENV_VAR, KEYFILE_SECRET, 1);
	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password" };
	ck_assert_int_eq(parse_typed(&args, 4, argv, " env:" ENV_VAR " \n"), 0);
	ck_assert_str_eq(args.tls.keyfile_pw, KEYFILE_SECRET);
	free_parsed(&args);
}
END_TEST

START_TEST(cli_piped_tls_keyfile_password_cut_off_padded_fails)
{
	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password" };
	char* typed = repeat('x', 10 + TLS_KEYFILE_PASSWORD_MAX_LEN + 100);

	memset(typed, ' ', 10);
	ck_assert_int_ne(parse_typed(&args, 4, argv, typed), 0);
	ck_assert_str_eq(err_buf, "--tls-keyfile-password: password is longer "
			"than 1024 bytes\n");
	free_parsed(&args);
	free(typed);
}
END_TEST


Suite*
password_suite(void)
{
	Suite* s;
	TCase* tc_resolve;
	TCase* tc_cli;

	s = suite_create("Password");

	tc_resolve = tcase_create("Resolve");
	tcase_add_checked_fixture(tc_resolve, setup, teardown);
	tcase_add_test(tc_resolve, literal);
	tcase_add_test(tc_resolve, literal_unknown_prefix);
	tcase_add_test(tc_resolve, literal_prefix_case_sensitive);
	tcase_add_test(tc_resolve, literal_empty);
	tcase_add_test(tc_resolve, env);
	tcase_add_test(tc_resolve, env_value_not_parsed_again);
	tcase_add_test(tc_resolve, env_unset);
	tcase_add_test(tc_resolve, env_no_name);
	tcase_add_test(tc_resolve, env_empty);
	tcase_add_test(tc_resolve, env_b64);
	tcase_add_test(tc_resolve, env_b64_trailing_newline);
	tcase_add_test(tc_resolve, env_b64_unset);
	tcase_add_test(tc_resolve, env_b64_empty);
	tcase_add_test(tc_resolve, env_b64_no_name);
	tcase_add_test(tc_resolve, env_b64_invalid);
	tcase_add_test(tc_resolve, env_b64_decodes_empty);
	tcase_add_test(tc_resolve, env_b64_nul);
	tcase_add_test(tc_resolve, b64);
	tcase_add_test(tc_resolve, b64_trailing_newline);
	tcase_add_test(tc_resolve, b64_value_not_parsed_again);
	tcase_add_test(tc_resolve, b64_invalid_chars);
	tcase_add_test(tc_resolve, b64_invalid_length);
	tcase_add_test(tc_resolve, b64_invalid_padding);
	tcase_add_test(tc_resolve, b64_line_breaks_skipped);
	tcase_add_test(tc_resolve, b64_missing);
	tcase_add_test(tc_resolve, b64_only_line_breaks);
	tcase_add_test(tc_resolve, env_b64_only_line_breaks);
	tcase_add_test(tc_resolve, b64_decodes_empty);
	tcase_add_test(tc_resolve, b64_nul);
	tcase_add_test(tc_resolve, file);
	tcase_add_test(tc_resolve, file_no_line_ending);
	tcase_add_test(tc_resolve, file_crlf);
	tcase_add_test(tc_resolve, file_one_line_ending_removed);
	tcase_add_test(tc_resolve, file_multi_line);
	tcase_add_test(tc_resolve, file_large);
	tcase_add_test(tc_resolve, file_too_large);
	tcase_add_test(tc_resolve, file_no_path);
	tcase_add_test(tc_resolve, file_nul);
	tcase_add_test(tc_resolve, file_dev_zero);
	tcase_add_test(tc_resolve, file_value_not_parsed_again);
	tcase_add_test(tc_resolve, file_missing);
	tcase_add_test(tc_resolve, file_is_directory);
	tcase_add_test(tc_resolve, file_empty);
	tcase_add_test(tc_resolve, file_only_line_ending);
	tcase_add_test(tc_resolve, file_first_line);
	tcase_add_test(tc_resolve, file_first_line_empty);
	tcase_add_test(tc_resolve, file_first_line_large);
	tcase_add_test(tc_resolve, file_first_line_too_large);
	tcase_add_test(tc_resolve, file_first_line_nul);
	tcase_add_test(tc_resolve, max_len_env);
	tcase_add_test(tc_resolve, max_len_env_b64);
	tcase_add_test(tc_resolve, max_len_b64);
	tcase_add_test(tc_resolve, max_len_file_trailing_newline);
	tcase_add_test(tc_resolve, max_len_file_too_long);
	tcase_add_test(tc_resolve, max_len_literal);
	tcase_add_test(tc_resolve, max_len_keyfile_literal);
	tcase_add_test(tc_resolve, max_len_keyfile_env);
	tcase_add_test(tc_resolve, has_source);
	tcase_add_test(tc_resolve, trim);
	suite_add_tcase(s, tc_resolve);

	tc_cli = tcase_create("Command line");
	tcase_add_checked_fixture(tc_cli, setup, teardown);
	tcase_add_test(tc_cli, cli_password_env_space_separated);
	tcase_add_test(tc_cli, cli_password_b64_attached);
	tcase_add_test(tc_cli, cli_password_long_form);
	tcase_add_test(tc_cli, cli_password_file);
	tcase_add_test(tc_cli, cli_password_file_not_first_line);
	tcase_add_test(tc_cli, cli_password_literal);
	tcase_add_test(tc_cli, cli_password_env_unset_fails);
	tcase_add_test(tc_cli, cli_password_resolved_once_last_wins);
	tcase_add_test(tc_cli, cli_password_not_resolved_without_user);
	tcase_add_test(tc_cli, cli_password_too_long_fails);
	tcase_add_test(tc_cli, cli_password_max_len);
	tcase_add_test(tc_cli, cli_password_literal_max_len);
	tcase_add_test(tc_cli, cli_password_long_literal_fails);
	tcase_add_test(tc_cli, cli_password_no_name_fails);
	tcase_add_test(tc_cli, cli_typed_password_max_len);
	tcase_add_test(tc_cli, cli_typed_password_too_long_fails);
	tcase_add_test(tc_cli, cli_typed_password_without_flag_too_long_fails);
	tcase_add_test(tc_cli, cli_typed_password_is_literal);
	tcase_add_test(tc_cli, cli_password_literal_not_trimmed);
	tcase_add_test(tc_cli, cli_piped_password_trimmed);
	tcase_add_test(tc_cli, cli_piped_password_padded_is_literal);
	tcase_add_test(tc_cli, cli_piped_password_eof_is_empty);
	tcase_add_test(tc_cli, cli_piped_password_cut_off_padded_fails);
	tcase_add_test(tc_cli, cli_password_no_user_warns);
	tcase_add_test(tc_cli, cli_password_no_user_literal_no_warning);
	tcase_add_test(tc_cli, cli_password_no_user_gen_bench_no_warning);
	tcase_add_test(tc_cli, cli_password_pki_not_resolved);
	tcase_add_test(tc_cli, cli_password_pki_no_user_no_warning);
	tcase_add_test(tc_cli, cli_typed_password_pki_not_checked);
	tcase_add_test(tc_cli, cli_tls_keyfile_password_max_len);
	tcase_add_test(tc_cli, cli_tls_keyfile_password_too_long_fails);
	tcase_add_test(tc_cli, cli_tls_keyfile_password_not_resolved_without_keyfile);
	tcase_add_test(tc_cli, cli_tls_keyfile_password_env);
	tcase_add_test(tc_cli, cli_tls_keyfile_password_b64);
	tcase_add_test(tc_cli, cli_tls_keyfile_password_literal);
	tcase_add_test(tc_cli, cli_tls_keyfile_password_file_missing_fails);
	tcase_add_test(tc_cli, cli_tls_keyfile_password_file_first_line);
	tcase_add_test(tc_cli, cli_tls_keyfile_password_last_wins);
	tcase_add_test(tc_cli, cli_tls_keyfile_password_file_empty_first_line_fails);
	tcase_add_test(tc_cli, cli_tls_keyfile_password_padded_env);
	tcase_add_test(tc_cli, cli_tls_keyfile_password_padded_literal);
	tcase_add_test(tc_cli, cli_tls_keyfile_password_nbsp_not_trimmed);
	tcase_add_test(tc_cli, cli_piped_tls_keyfile_password_trimmed);
	tcase_add_test(tc_cli, cli_piped_tls_keyfile_password_padded_env);
	tcase_add_test(tc_cli, cli_piped_tls_keyfile_password_cut_off_padded_fails);
	suite_add_tcase(s, tc_cli);

	return s;
}
