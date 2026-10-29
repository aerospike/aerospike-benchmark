/*******************************************************************************
 * Copyright 2008-2018 by Aerospike.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ******************************************************************************/

//==========================================================
// Includes.
//

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <aerospike/as_boolean.h>
#include <aerospike/as_msgpack.h>
#include <citrusleaf/cf_b64.h>

#include <openssl/err.h>
#include <openssl/pem.h>

#include <sa_client.h>

#include "common.h"


//==========================================================
// Typedefs & constants.
//

typedef struct sa_log_format_s {
	const char* fmt;
	bool errno_arg;
} sa_log_format_t;

// secret agent client log lines whose arguments are safe to print, checked
// against the library sources by src/test/integration/test_secret_agent.py.
// errno_arg marks a line whose only argument is an errno.
static const sa_log_format_t SA_LOG_ALLOW[] = {
	{"ERR: failed to lookup address: %s", false},
	{"ERR: connect failed, errno: %d", true},
	{"ERR: response: %.*s", false},
	{"ERR: SSL_connect failed: %s", false},
	{"ERR: SSL_connect I/O error: %s", false},
	{"ERR: SSL_connect certificate verify failed: %s (%ld)", false},
	{"ERR: unable to set TLS peer name: %s", false}
};


//==========================================================
// Forward declarations.
//

LOCAL_HELPER void sa_log_cb(const char* fmt, ...);

LOCAL_HELPER int as_bytes_cmp(as_bytes* b1, as_bytes* b2);
LOCAL_HELPER int as_list_cmp_max(const as_list* list1, const as_list* list2,
		uint32_t max, uint32_t fin);
LOCAL_HELPER int as_list_cmp(const as_list* list1, const as_list* list2);
LOCAL_HELPER int as_vector_cmp(as_vector* list1, as_vector* list2);
LOCAL_HELPER bool key_append(const as_val* key, const as_val* val, void* udata);
LOCAL_HELPER int key_cmp(const void* v1, const void* v2);
LOCAL_HELPER bool map_to_sorted_keys(const as_map* map, uint32_t size,
		as_vector* list);
LOCAL_HELPER int as_map_cmp(const as_map* map1, const as_map* map2);


//==========================================================
// Public API.
//

/*
 * algorithm inspired by: https://stackoverflow.com/a/18027868
 */
int dec_display_len(size_t number)
{
	struct {
		uint64_t max;
		int digits;
	} lookup[] = {
		{ -1, 1 }, { -1, 1 }, { -1, 1 }, { 9, 1 },
		{ -1, 2 }, { -1, 2 }, { 99, 2 },
		{ -1, 3 }, { -1, 3 }, { 999, 3 },
		{ -1, 4 }, { -1, 4 }, { -1, 4 }, { 9999, 4},
		{ -1, 5 }, { -1, 5 }, { 99999, 5 },
		{ -1, 6 }, { -1, 6 }, { 999999, 6 },
		{ -1, 7 }, { -1, 7 }, { -1, 7 }, { 9999999, 7 },
		{ -1, 8 }, { -1, 8 }, { 99999999, 8 },
		{ -1, 9 }, { -1, 9 }, { 999999999, 9 },
		{ -1, 10 }, { -1, 10 }, { -1, 10 }, { 9999999999, 10 },
		{ -1, 11 }, { -1, 11 }, { 99999999999, 11 },
		{ -1, 12 }, { -1, 12 }, { 999999999999, 12 },
		{ -1, 13 }, { -1, 13 }, { -1, 13 }, { 9999999999999, 13 },
		{ -1, 14 }, { -1, 14 }, { 99999999999999, 14 },
		{ -1, 15 }, { -1, 15 }, { 999999999999999, 15 },
		{ -1, 16 }, { -1, 16 }, { -1, 16 }, { 9999999999999999, 16 },
		{ -1, 17 }, { -1, 17 }, { 99999999999999999, 17 },
		{ -1, 18 }, { -1, 18 }, { 999999999999999999, 18 },
		{ -1, 19 }, { -1, 19 }, { -1, 19 }, { 9999999999999999999U, 19 },
	};

	uint32_t digits = (8 * sizeof(number) - 1) - __builtin_clzl(number);
	return number == 0 ? 1 :
		lookup[digits].digits + (lookup[digits].max < number);
}

void
blog_detailv(as_log_level level, const char* fmt, va_list ap)
{
	// Write message all at once so messages generated from multiple threads
	// have less of a chance of getting garbled.
	char fmtbuf[1024];
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	struct tm* t = localtime(&now.tv_sec);
	uint64_t msecs = now.tv_nsec / 1000000;
	int len = sprintf(fmtbuf, "%d-%02d-%02d %02d:%02d:%02d.%03" PRIu64 " %s ",
		t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min,
		t->tm_sec, msecs, as_log_level_tostring(level));

	size_t len2 = strlen(fmt);
	char* p = fmtbuf + len;
	memcpy(p, fmt, len2);
	p += len2;
	*p = 0;
	
	vprintf(fmtbuf, ap);
}

void
blog_detail(as_log_level level, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	blog_detailv(level, fmt, ap);
	va_end(ap);
}


#ifdef __APPLE__

char* strchrnul(const char *s, int c_in)
{
	char* r = strchr(s, c_in);
	return (char*) (r == NULL ? s + strlen(s) : r);
}

void* memrchr(const void* s, int c, size_t n)
{
	const uint8_t *cp;

	if (n != 0) {
		cp = (uint8_t*) s + n;
		do {
			if (*(--cp) == (uint8_t) c) {
				return (void*) cp;
			}
		} while (--n != 0);
	}
    return NULL;
}

#endif /* __linux__ */

const char* utc_time_str(time_t t)
{
	static char buf[UTC_STR_LEN + 1];
	struct tm * utc = gmtime(&t);
	snprintf(buf, sizeof(buf),
			"%4d-%02d-%02dT%02d:%02d:%02dZ",
			1900 + utc->tm_year, utc->tm_mon + 1, utc->tm_mday,
			utc->tm_hour, utc->tm_min, utc->tm_sec);
	return buf;
}

uint32_t gen_rand_range(as_random* random, uint32_t max)
{
	/*
	 * To eliminate any statistical bias, we'd want to choose only from a range
	 * [0, n * max), for some integer n, before modding by max. Since max may
	 * not be a power of two (and therefore not divide 2^32), the natural range
	 * of as_random_next_uint32 of [0, 2^32) will be biased towards the smaller
	 * numbers. To accomodate this, we can choose from the range
	 * [0, 2^32 - (2^32 % max)), or, equivalently (and cheaper computationally),
	 * [2^32 % max, 2^32)
	 *
	 * calculate 0x100000000LU % max, which is equivalent to
	 * (0x100000000LU - max) % max = ((uint32_t) -max) % max
	 *
	 * doing this with 32-bit numbers is faster than with 64-bit
	 */
	uint32_t rem = (-max) % max;
	uint32_t r;

	do {
		r = as_random_next_uint32(random);
	} while (UNLIKELY(r < rem));

	return r % max;
}

uint64_t gen_rand_range_64(as_random* random, uint64_t max)
{
	uint64_t rem = (-max) % max;
	uint64_t r;

	do {
		r = as_random_next_uint64(random);
	} while (UNLIKELY(r < rem));

	return r % max;
}

bool bin_name_too_large(size_t name_len, uint32_t n_bins)
{
	if (n_bins == 1) {
		return name_len >= sizeof(as_bin_name);
	}

	int max_display_len = dec_display_len(n_bins);
	// key format: <key_name>_<bin_num>
	return (name_len + 1 + max_display_len) >= sizeof(as_bin_name);
}

void gen_bin_name(as_bin_name name_buf, const char* bin_name, uint32_t bin_idx)
{
	if (bin_idx == 0) {
		strncpy(name_buf, bin_name, sizeof(as_bin_name) - 1);
		// in case bin_name exactly filled the buffer, add the
		// null-terminater, since strncpy doesn't do that in this
		// instance
		name_buf[sizeof(as_bin_name) - 1] = '\0';
	}
	else {
		snprintf(name_buf, sizeof(as_bin_name), "%s_%d", bin_name, bin_idx + 1);
	}
}

static bool
password_env(const char* option, const char* var, char** ptr)
{
	const char* pw = getenv(var);

	if (pw == NULL || pw[0] == '\0') {
		fprintf(stderr, "%s: environment variable %s is not set or empty\n",
				option, var);
		return false;
	}

	*ptr = strdup(pw);

	if (*ptr == NULL) {
		fprintf(stderr, "%s: out of memory reading environment variable %s\n",
				option, var);
		return false;
	}

	return true;
}

static bool
password_b64(const char* option, const char* encoded, const char* what,
		const char* name, char** ptr)
{
	size_t len = strlen(encoded);
	char* pw = cf_malloc(len + 1);

	if (pw == NULL) {
		fprintf(stderr, "%s: out of memory decoding %s%s\n", option, what, name);
		return false;
	}

	uint32_t n = 0;
	uint32_t size = 0;

	// line breaks are skipped, as Go's base64.StdEncoding does
	for (size_t i = 0; i < len; i++) {
		if (encoded[i] != '\r' && encoded[i] != '\n') {
			pw[n++] = encoded[i];
		}
	}

	if (n != 0 && !cf_b64_validate_and_decode_in_place((uint8_t*) pw, n,
				&size)) {
		fprintf(stderr, "%s: invalid base64 in %s%s\n", option, what, name);
		cf_free(pw);
		return false;
	}

	if (memchr(pw, '\0', size) != NULL) {
		fprintf(stderr, "%s: %s%s decodes to a value containing a NUL byte\n",
				option, what, name);
		cf_free(pw);
		return false;
	}

	if (size != 0 && pw[size - 1] == '\n') {
		size--;
	}

	pw[size] = '\0';

	if (size == 0) {
		fprintf(stderr, "%s: password from %s%s is empty\n", option, what, name);
		cf_free(pw);
		return false;
	}

	*ptr = pw;
	return true;
}

static bool
password_file(const char* option, const char* path, size_t max_len,
		bool first_line, char** ptr)
{
	FILE* fh = fopen(path, "r");

	if (fh == NULL) {
		fprintf(stderr, "%s: cannot read file %s: %s\n", option, path,
				strerror(errno));
		return false;
	}

	// room for a trailing \r\n and one byte more, so an over-long value is
	// caught without reading the rest of the file
	size_t cap = max_len + 3;
	char* pw = cf_malloc(cap + 1);

	if (pw == NULL) {
		fprintf(stderr, "%s: out of memory reading file %s\n", option, path);
		fclose(fh);
		return false;
	}

	size_t len = 0;
	int c = EOF;

	while (len < cap && (c = getc(fh)) != EOF && c != '\0') {
		if (first_line && (c == '\n' || c == '\r')) {
			break;
		}

		pw[len++] = (char) c;
	}

	int err = errno;

	if (ferror(fh)) {
		fclose(fh);
		cf_free(pw);
		fprintf(stderr, "%s: cannot read file %s: %s\n", option, path,
				strerror(err));
		return false;
	}

	fclose(fh);

	if (c == '\0') {
		fprintf(stderr, "%s: file %s contains a NUL byte\n", option, path);
		cf_free(pw);
		return false;
	}

	if (len != 0 && pw[len - 1] == '\n') {
		len--;

		if (len != 0 && pw[len - 1] == '\r') {
			len--;
		}
	}

	pw[len] = '\0';

	if (len == 0) {
		fprintf(stderr, "%s: password from %sfile %s is empty\n", option,
				first_line && c != EOF ? "first line of " : "", path);
		cf_free(pw);
		return false;
	}

	*ptr = pw;
	return true;
}

bool
password_has_source(const char* value)
{
	return strncmp(value, "env:", 4) == 0 ||
		strncmp(value, "env-b64:", 8) == 0 ||
		strncmp(value, "b64:", 4) == 0 ||
		strncmp(value, "file:", 5) == 0 ||
		strncmp(value, "secrets:", 8) == 0;
}

bool
password_len_ok(const char* option, const char* pw, size_t max_len)
{
	if (strlen(pw) > max_len) {
		fprintf(stderr, "%s: password is longer than %zu bytes\n", option,
				max_len);
		return false;
	}

	return true;
}

char*
password_trim(char* s)
{
	s += strspn(s, PASSWORD_WHITESPACE);

	size_t len = strlen(s);

	while (len != 0 && strchr(PASSWORD_WHITESPACE, s[len - 1]) != NULL) {
		len--;
	}

	s[len] = '\0';
	return s;
}

static volatile sig_atomic_t password_signal = 0;

static void
password_on_signal(int sig)
{
	password_signal = sig;
}

static bool
password_read_byte(char* c)
{
	while (true) {
		ssize_t n = read(STDIN_FILENO, c, 1);

		if (n == 1) {
			return true;
		}

		if (n < 0 && errno == EINTR && password_signal == 0) {
			continue;
		}

		return false;
	}
}

static char*
password_read_line(const char* option, const char* prompt, char* buf,
		size_t max_len, bool trim)
{
	static const int signals[] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM };
	size_t n_signals = sizeof(signals) / sizeof(signals[0]);
	struct sigaction saved_sa[sizeof(signals) / sizeof(signals[0])];
	struct sigaction saved_tstp;
	struct termios saved_term;
	bool guarded = prompt != NULL && tcgetattr(STDIN_FILENO, &saved_term) == 0;
	bool echo_off = false;

	password_signal = 0;

	if (prompt != NULL) {
		fputs(prompt, stderr);
	}

	// while echo is off, a signal restores the terminal before it takes effect
	// and Ctrl-Z cannot stop the read
	if (guarded) {
		struct sigaction sa = { .sa_handler = password_on_signal };
		struct sigaction ign = { .sa_handler = SIG_IGN };
		struct termios term = saved_term;

		sigemptyset(&sa.sa_mask);
		sigemptyset(&ign.sa_mask);

		for (size_t i = 0; i < n_signals; i++) {
			sigaction(signals[i], NULL, &saved_sa[i]);

			if (saved_sa[i].sa_handler != SIG_IGN) {
				sigaction(signals[i], &sa, NULL);
			}
		}

		sigaction(SIGTSTP, &ign, &saved_tstp);

		term.c_lflag &= ~(tcflag_t) (ECHO | ECHONL);
		echo_off = tcsetattr(STDIN_FILENO, TCSAFLUSH, &term) == 0;
	}

	size_t cap = max_len + 3;
	size_t len = 0;
	char c;

	// one byte at a time, so the rest of stdin stays unread
	while (len < cap && password_read_byte(&c)) {
		buf[len++] = c;

		if (c == '\n') {
			break;
		}
	}

	// a line that fills the bound is complete only if the input or line ends
	bool cut = len == cap && buf[len - 1] != '\n' && password_read_byte(&c) &&
		c != '\n';

	buf[len] = '\0';

	if (echo_off) {
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_term);
		fputc('\n', stderr);
	}

	if (guarded) {
		for (size_t i = 0; i < n_signals; i++) {
			sigaction(signals[i], &saved_sa[i], NULL);
		}

		sigaction(SIGTSTP, &saved_tstp, NULL);

		if (password_signal != 0) {
			raise(password_signal);
			return NULL;
		}
	}

	if (memchr(buf, '\0', len) != NULL) {
		fprintf(stderr, "%s: password from stdin contains a NUL byte\n", option);
		return NULL;
	}

	// a cut-off line stays as read so the caller's max_len check rejects it
	if (cut) {
		return buf;
	}

	if (trim) {
		return password_trim(buf);
	}

	if (len != 0 && buf[len - 1] == '\n') {
		buf[len - 1] = '\0';
	}

	return buf;
}

char*
read_piped_password(const char* option, char* buf, size_t max_len)
{
	return password_read_line(option, NULL, buf, max_len, true);
}

char*
read_typed_password(const char* option, const char* prompt, char* buf,
		size_t max_len, bool trim)
{
	return password_read_line(option, prompt, buf, max_len, trim);
}

LOCAL_HELPER void
sa_log_cb(const char* fmt, ...)
{
	for (size_t i = 0; i < sizeof(SA_LOG_ALLOW) / sizeof(SA_LOG_ALLOW[0]); i++) {
		if (strcmp(fmt, SA_LOG_ALLOW[i].fmt) == 0) {
			va_list ap;

			va_start(ap, fmt);
			fprintf(stderr, "secret-agent: ");
			vfprintf(stderr, fmt, ap);
			va_end(ap);

			if (SA_LOG_ALLOW[i].errno_arg) {
				va_start(ap, fmt);
				fprintf(stderr, " (%s)", strerror(va_arg(ap, int)));
				va_end(ap);
			}

			fprintf(stderr, "\n");
			return;
		}
	}

	// any other line could echo request or response bytes: print up to the first '%'
	size_t len = strcspn(fmt, "%");

	while (len != 0 && strchr(" :,-(", fmt[len - 1]) != NULL) {
		len--;
	}

	fprintf(stderr, "secret-agent: %.*s\n", (int) len, fmt);
}

static const char*
sa_err_str(sa_err err)
{
	switch (err.code) {
		case SA_FAILED_BAD_REQUEST:
			return "bad request";
		case SA_FAILED_BAD_CONFIG:
			return "bad address or port";
		case SA_FAILED_INTERNAL:
			return "connection or protocol error";
		case SA_FAILED_TIMEOUT:
			return "timed out";
		default:
			return "unknown error";
	}
}

static bool
sa_has_certificate(const char* pem)
{
	BIO* bio = BIO_new_mem_buf(pem, -1);
	X509* cert = bio != NULL ? PEM_read_bio_X509(bio, NULL, NULL, NULL) : NULL;
	bool found = cert != NULL;

	X509_free(cert);
	BIO_free(bio);
	ERR_clear_error();
	return found;
}

static char*
sa_read_ca_file(const char* option, const char* path)
{
	if (path[0] == '\0') {
		fprintf(stderr, "%s: requires a path\n", option);
		return NULL;
	}

	FILE* fh = fopen(path, "r");

	if (fh == NULL) {
		fprintf(stderr, "%s: cannot read file %s: %s\n", option, path,
				strerror(errno));
		return NULL;
	}

	char* buf = cf_malloc(SA_CA_FILE_MAX_SIZE + 2);
	size_t len = fread(buf, 1, SA_CA_FILE_MAX_SIZE + 1, fh);

	if (ferror(fh)) {
		int err = errno;

		fclose(fh);
		cf_free(buf);
		fprintf(stderr, "%s: cannot read file %s: %s\n", option, path,
				strerror(err));
		return NULL;
	}

	fclose(fh);
	buf[len] = '\0';

	if (len > SA_CA_FILE_MAX_SIZE) {
		fprintf(stderr, "%s: cannot load TLS CA file %s: file is larger than "
				"%d bytes\n", option, path, SA_CA_FILE_MAX_SIZE);
	}
	else if (!sa_has_certificate(buf)) {
		fprintf(stderr, "%s: cannot load TLS CA file %s: %s\n", option, path,
				len == 0 ? "file is empty" : "no PEM certificate found");
	}
	else {
		return buf;
	}

	cf_free(buf);
	return NULL;
}

static bool
password_secret(const sa_args_t* sa, const char* option, const char* path,
		char** ptr)
{
	if (sa == NULL) {
		fprintf(stderr, "%s: no secret agent settings for %s\n", option, path);
		return false;
	}

	sa_cfg cfg;

	sa_cfg_init(&cfg);
	cfg.addr = sa->host;
	cfg.port = sa->port;
	cfg.timeout = sa->timeout_ms;

	if (sa->cafile != NULL) {
		cfg.tls.ca_string = sa_read_ca_file(sa->cafile_opt, sa->cafile);

		if (cfg.tls.ca_string == NULL) {
			return false;
		}

		cfg.tls.enabled = true;
	}

	sa_client client;

	sa_client_init(&client, &cfg);
	sa_set_log_function(sa_log_cb);

	uint8_t* buf = NULL;
	size_t len = 0;
	sa_err err = sa_secret_get_bytes(&client, path, &buf, &len);

	cf_free(cfg.tls.ca_string);

	if (err.code != SA_OK) {
		fprintf(stderr, "%s: secret agent request for %s failed: %s\n", option,
				path, sa_err_str(err));
		return false;
	}

	if (len == 0 || memchr(buf, '\0', len) != NULL) {
		fprintf(stderr, "%s: secret agent returned %s for %s\n", option,
				len == 0 ? "an empty value" : "a value containing a NUL byte",
				path);
		free(buf);
		return false;
	}

	// the library leaves one spare byte past the value
	buf[len] = '\0';
	*ptr = (char*) buf;
	return true;
}

bool
read_password(const sa_args_t* sa, const char* option, const char* value,
		size_t max_len, bool first_line, char** ptr)
{
	const char* from;
	const char* name;
	char* pw;

	if (strncmp(value, "env:", 4) == 0) {
		from = "value from environment variable ";
		name = value + 4;

		if (name[0] == '\0') {
			fprintf(stderr, "%s: env: requires a variable name\n", option);
			return false;
		}

		if (!password_env(option, name, &pw)) {
			return false;
		}
	}
	else if (strncmp(value, "env-b64:", 8) == 0) {
		char* encoded;

		from = "decoded value from environment variable ";
		name = value + 8;

		if (name[0] == '\0') {
			fprintf(stderr, "%s: env-b64: requires a variable name\n", option);
			return false;
		}

		if (!password_env(option, name, &encoded)) {
			return false;
		}

		bool ok = password_b64(option, encoded, "environment variable ", name,
				&pw);

		cf_free(encoded);

		if (!ok) {
			return false;
		}
	}
	else if (strncmp(value, "b64:", 4) == 0) {
		from = "decoded b64: value";
		name = "";

		if (!password_b64(option, value + 4, "b64: value", "", &pw)) {
			return false;
		}
	}
	else if (strncmp(value, "file:", 5) == 0) {
		from = "value from file ";
		name = value + 5;

		if (name[0] == '\0') {
			fprintf(stderr, "%s: file: requires a path\n", option);
			return false;
		}

		if (!password_file(option, name, max_len, first_line, &pw)) {
			return false;
		}
	}
	else if (strncmp(value, "secrets:", 8) == 0) {
		from = "value from ";
		name = value;

		if (!password_secret(sa, option, value, &pw)) {
			return false;
		}
	}
	else {
		if (value[0] == '\0') {
			fprintf(stderr, "%s: password is empty\n", option);
			return false;
		}

		if (!password_len_ok(option, value, max_len)) {
			return false;
		}

		*ptr = strdup(value);
		return true;
	}

	if (strlen(pw) > max_len) {
		fprintf(stderr, "%s: %s%s is longer than %zu bytes\n", option, from, name,
				max_len);
		cf_free(pw);
		return false;
	}

	*ptr = pw;
	return true;
}

char* parse_string_literal(const char* restrict str,
		const char** restrict endptr)
{
	if (*str != '"') {
		fprintf(stderr, "Expected a '\"' at the beginning of the string\n");
		return NULL;
	}

	// calculate the string length
	uint64_t len = 0;
	const char* end = str + 1;
	while (*end != '"') {
		if (*end == '\0') {
			fprintf(stderr, "Unterminated '\"' in string literal\n");
			return NULL;
		}
		if (*end == '\\') {
			end++;
			if (*end == '\0') {
				fprintf(stderr, "Dangling escape character\n");
				return NULL;
			}
			if (*end == 'x') {
				if (*(end + 1) == '\0' || *(end + 2) == '\0') {
					fprintf(stderr, "Unterminated hexadecimal escape sequence\n");
					return NULL;
				}
				end += 2;
			}
			else if ('0' <= *end && *end <= '3') {
				if (*(end + 1) < '0' || *(end + 1) > '7' ||
						*(end + 2) < '0' || *(end + 2) > '7') {
					fprintf(stderr, "Invalid octal string \"%.4s\"\n", end - 1);
					return NULL;
				}
				end += 2;
			}
		}
		end++;
		len++;
	}

	// allocate a buffer for the parsed string
	char* res = (char*) cf_malloc(len + 1);
	uint64_t i = 0;
	for (const char* s = str + 1; s != end; s++, i++) {
		if (*s == '\\') {
			switch (*(s + 1)) {
				case 'a':
					res[i] = '\a';
					break;
				case 'b':
					res[i] = '\b';
					break;
				case 'e':
					res[i] = '\e';
					break;
				case 'f':
					res[i] = '\f';
					break;
				case 'n':
					res[i] = '\n';
					break;
				case 'r':
					res[i] = '\r';
					break;
				case 't':
					res[i] = '\t';
					break;
				case 'v':
					res[i] = '\v';
					break;
				case '\\':
					res[i] = '\\';
					break;
				case '\'':
					res[i] = '\'';
					break;
				case '"':
					res[i] = '\"';
					break;
				case '?':
					res[i] = '\?';
					break;

				case '0':
				case '1':
				case '2':
				case '3': {
					// parse as octal, which has already been error checked in the first pass
					int8_t val = (*(s + 1) - '0') * 64 +
						(*(s + 2) - '0') * 8 + (*(s + 3) - '0');
					res[i] = val;
					s += 2;
					break;
				}

				case 'x': {
					char* endptr;
					// move the next two characters into a local buffer that we null-terminate,
					// then try parsing as a 2-digit hex string
					char buf[3];
					memcpy(buf, s + 2, 2 * sizeof(char));
					buf[2] = '\0';
					uint64_t val = strtoul(buf, &endptr, 16);
					if (endptr != ((char*) buf) + 2) {
						fprintf(stderr, "Invalid hexadecimal escape sequence "
								"\"\\%.3s\"\n", s + 1);
						cf_free(res);
						return NULL;
					}

					res[i] = (int8_t) val;
					s += 2;
					break;
				}

				default:
					fprintf(stderr, "Unknown escape sequence \"\\%c\"\n",
							*(s + 1));
					cf_free(res);
					return NULL;
			}
			s++;
		}
		else {
			res[i] = *s;
		}
	}
	res[len] = '\0';

	if (endptr != NULL) {
		*endptr = end + 1;
	}
	return res;
}

void print_hdr_percentiles(struct hdr_histogram* h, const char* name,
		uint64_t elapsed_s, as_vector* percentiles, FILE *out_file)
{
	int64_t min, max;
	int64_t total_cnt;

	total_cnt = hdr_total_count(h);
	min = total_cnt == 0 ? 0 : hdr_min(h);
	max = hdr_max(h);
	fprintf(out_file, "hdr: %-5s %.24s %" PRIu64 ", %" PRIu64 ", %" PRId64
			", %" PRId64,
			name, utc_time_str(time(NULL)), elapsed_s, total_cnt, min, max);
	for (uint32_t i = 0; i < percentiles->size; i++) {
		double p = *(double *) as_vector_get(percentiles, i);
		uint64_t cnt = hdr_value_at_percentile(h, p);
		fprintf(out_file, ", %" PRIu64, cnt);
	}
	fprintf(out_file, "\n");
}


//==========================================================
// Local helpers.
//

LOCAL_HELPER int
as_bytes_cmp(as_bytes* b1, as_bytes* b2)
{
	if (b1->size == b2->size) {
		return memcmp(b1->value, b2->value, b1->size);
	}
	else if (b1->size < b2->size) {
		int cmp = memcmp(b1->value, b2->value, b1->size);
		return cmp != 0 ? cmp : -1;
	}
	else {
		int cmp = memcmp(b1->value, b2->value, b2->size);
		return cmp != 0 ? cmp : 1;
	}
}

LOCAL_HELPER int
as_list_cmp_max(const as_list* list1, const as_list* list2, uint32_t max, uint32_t fin)
{
	for (uint32_t i = 0; i < max; i++) {
		msgpack_compare_t cmp = as_val_cmp(as_list_get(list1, i), as_list_get(list2, i));

		if (cmp != MSGPACK_COMPARE_EQUAL) {
			return cmp;
		}
	}
	return fin;
}

LOCAL_HELPER int
as_list_cmp(const as_list* list1, const as_list* list2)
{
	uint32_t size1 = as_list_size(list1);
	uint32_t size2 = as_list_size(list2);

	if (size1 == size2) {
		return as_list_cmp_max(list1, list2, size1, 0);
	}
	else if (size1 < size2) {
		return as_list_cmp_max(list1, list2, size1, -1);
	}
	else {
		return as_list_cmp_max(list1, list2, size2, 1);
	}
}

LOCAL_HELPER int
as_vector_cmp(as_vector* list1, as_vector* list2)
{
	// Size of vectors should already be the same.
	for (uint32_t i = 0; i < list1->size; i++) {
		msgpack_compare_t cmp = as_val_cmp(as_vector_get_ptr(list1, i), as_vector_get_ptr(list2, i));

		if (cmp != MSGPACK_COMPARE_EQUAL) {
			return cmp;
		}
	}
	return 0;
}

LOCAL_HELPER bool
key_append(const as_val* key, const as_val* val, void* udata)
{
	as_vector_append(udata, (void*)&key);
	return true;
}

LOCAL_HELPER int
key_cmp(const void* v1, const void* v2)
{
	// we offset by 1 here because as_val_cmp considers 0 as less than, 1 as equal, and 2 as greater.
	// https://github.com/aerospike/aerospike-common/blob/master/src/include/aerospike/as_msgpack.h#L61
	int cmp = (int) as_val_cmp(*(as_val**)v1, *(as_val**)v2);
	switch (cmp)
	{
	case MSGPACK_COMPARE_LESS:
		return -1;
	case MSGPACK_COMPARE_EQUAL:
		return 0;
	case MSGPACK_COMPARE_GREATER:
		return 1;
	default:
		// shouldn't happen but
		// don't fail if it does
		return 0;
	}
}

LOCAL_HELPER bool
map_to_sorted_keys(const as_map* map, uint32_t size, as_vector* list)
{
	as_vector_init(list, sizeof(as_val*), size);

	if (! as_map_foreach(map, key_append, list)) {
		return false;
	}

	// Sort list of map entries.
	qsort(list->list, list->size, sizeof(as_val*), key_cmp);
	return true;
}

LOCAL_HELPER int
as_map_cmp(const as_map* map1, const as_map* map2)
{
	// Map ordering documented at https://www.aerospike.com/docs/guide/cdt-ordering.html
	uint32_t size1 = as_map_size(map1);
	uint32_t size2 = as_map_size(map2);
	int cmp = size1 - size2;

	if (cmp != 0) {
		return cmp;
	}

	// Convert maps to lists of keys and sort before comparing.
	as_vector list1;

	if (map_to_sorted_keys(map1, size1, &list1)) {
		as_vector list2;

		if (map_to_sorted_keys(map2, size2, &list2)) {
			cmp = as_vector_cmp(&list1, &list2);
		}
		as_vector_destroy(&list2);
	}
	as_vector_destroy(&list1);
	return cmp;
}

