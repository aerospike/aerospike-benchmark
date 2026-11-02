
#include <arpa/inet.h>
#include <check.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "benchmark.h"
#include "common.h"


#define SECRET "s3cr3t-pw"
#define SECRET_B64 "czNjcjN0LXB3"
#define MISSING_CA "/nonexistent/asbench_ca.pem"
#define MISSING_OTHER_CA "/nonexistent/asbench_other_ca.pem"
#define DEPRECATED_CAFILE \
	"Argument --sa-cafile is deprecated. Use --sa-ca-file instead.\n"

extern int set_args(int argc, char * const* argv, args_t* args);
extern void _load_defaults(args_t* args);
extern void _free_args(args_t* args);
extern bool sa_parse_timeout(const char* str, int* timeout_ms);
extern bool sa_parse_port(const char* str, char** port);
extern bool sa_parse_address(const char* address, char** host, char** port);
extern const char* sa_apply(sa_args_t* sa, const char* address,
		const char* port);
extern void sa_log_cb(const char* fmt, ...);


static char out_buf[1024];
static int saved_stderr;
static FILE* captured;
static char closed_port[8];
static sa_args_t sa;


static void
capture_start(void)
{
	fflush(stderr);
	saved_stderr = dup(STDERR_FILENO);
	captured = tmpfile();
	ck_assert_ptr_ne(captured, NULL);
	dup2(fileno(captured), STDERR_FILENO);
}

static void
capture_end(void)
{
	fflush(stderr);
	dup2(saved_stderr, STDERR_FILENO);
	close(saved_stderr);

	rewind(captured);
	size_t n = fread(out_buf, 1, sizeof(out_buf) - 1, captured);
	out_buf[n] = '\0';
	fclose(captured);
}


static void
setup_agent(void)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr = { .sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
	socklen_t len = sizeof(addr);

	ck_assert_int_ge(fd, 0);
	ck_assert_int_eq(bind(fd, (struct sockaddr*) &addr, sizeof(addr)), 0);
	ck_assert_int_eq(getsockname(fd, (struct sockaddr*) &addr, &len), 0);
	close(fd);
	snprintf(closed_port, sizeof(closed_port), "%d", ntohs(addr.sin_port));

	sa.host = "127.0.0.1";
	sa.port = closed_port;
	sa.timeout_ms = 1000;
	sa.cafile = NULL;
	sa.cafile_opt = "--sa-cafile";
	out_buf[0] = '\0';
}

static bool
resolve(const char* option, const char* value, char** pw)
{
	capture_start();
	bool ok = read_password(&sa, option, value,
			TLS_KEYFILE_PASSWORD_MAX_LEN, false, pw);
	capture_end();
	return ok;
}


START_TEST(resolve_prefix_case_sensitive)
{
	char* pw = NULL;
	ck_assert(resolve("--password", "Secrets:asbench:pw", &pw));
	ck_assert_str_eq(pw, "Secrets:asbench:pw");
	ck_assert_str_eq(out_buf, "");
	cf_free(pw);
}
END_TEST

START_TEST(resolve_agent_unreachable)
{
	char* pw = NULL;
	ck_assert(!resolve("--tls-keyfile-password", "secrets:asbench:pw", &pw));
	ck_assert_ptr_eq(pw, NULL);
	ck_assert_ptr_eq(strstr(out_buf,
				"secret-agent: ERR: connect failed, errno: "), out_buf);
	ck_assert_ptr_ne(strstr(out_buf, "\n--tls-keyfile-password: secret agent "
				"request for secrets:asbench:pw failed: connection or protocol "
				"error\n"), NULL);
}
END_TEST

START_TEST(resolve_cafile_unreadable)
{
	char* pw = NULL;
	sa.cafile = MISSING_CA;
	ck_assert(!resolve("--password", "secrets:asbench:pw", &pw));
	ck_assert_ptr_eq(pw, NULL);
	ck_assert_str_eq(out_buf, "--sa-cafile: cannot read file " MISSING_CA
			": No such file or directory\n");
}
END_TEST

START_TEST(resolve_without_agent_settings)
{
	char* pw = NULL;

	capture_start();
	ck_assert(!read_password(NULL, "--password", "secrets:asbench:pw",
				AS_PASSWORD_SIZE - 1, false, &pw));
	capture_end();
	ck_assert_ptr_eq(pw, NULL);
	ck_assert_str_eq(out_buf, "--password: no secret agent settings for "
			"secrets:asbench:pw\n");

	capture_start();
	ck_assert(read_password(NULL, "--password", SECRET, AS_PASSWORD_SIZE - 1,
				false, &pw));
	capture_end();
	ck_assert_str_eq(pw, SECRET);
	cf_free(pw);
}
END_TEST

START_TEST(resolve_bad_port)
{
	char* pw = NULL;
	sa.port = "0";
	ck_assert(!resolve("--password", "secrets:asbench:pw", &pw));
	ck_assert_ptr_eq(pw, NULL);
	ck_assert_str_eq(out_buf, "secret-agent: ERR: port\n"
			"secret-agent: ERR: failed to create socket\n--password: secret "
			"agent request for secrets:asbench:pw failed: bad address or port\n");
}
END_TEST


START_TEST(timeout_valid)
{
	int timeout = 0;
	ck_assert(sa_parse_timeout("1", &timeout));
	ck_assert_int_eq(timeout, 1);
	ck_assert(sa_parse_timeout("300", &timeout));
	ck_assert_int_eq(timeout, 300);
	ck_assert(sa_parse_timeout("2147483647", &timeout));
	ck_assert_int_eq(timeout, INT_MAX);
}
END_TEST

START_TEST(timeout_invalid)
{
	const char* bad[] = { "0", "-1", "+1", " 1", "", "abc", "1x", "1.5",
		"2147483648", "99999999999999999999" };
	int timeout = 42;

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		ck_assert_msg(!sa_parse_timeout(bad[i], &timeout), "accepted \"%s\"",
				bad[i]);
	}

	ck_assert_int_eq(timeout, 42);
}
END_TEST

START_TEST(port_valid)
{
	const char* good[][2] = { { "1", "1" }, { "3005", "3005" },
		{ "65535", "65535" }, { "03005", "3005" } };

	for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
		char* port = NULL;
		ck_assert_msg(sa_parse_port(good[i][0], &port), "rejected \"%s\"",
				good[i][0]);
		ck_assert_str_eq(port, good[i][1]);
		cf_free(port);
	}
}
END_TEST

START_TEST(port_invalid)
{
	const char* bad[] = { "0", "65536", "99999", "-1", "+1", "", "http",
		"3005x", " 3005" };

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		char* port = NULL;
		ck_assert_msg(!sa_parse_port(bad[i], &port), "accepted \"%s\"", bad[i]);
		ck_assert_ptr_eq(port, NULL);
	}
}
END_TEST

START_TEST(address_valid)
{
	const char* good[][3] = {
		{ "agent", "agent", NULL },
		{ "agent.example.com:4000", "agent.example.com", "4000" },
		{ "10.0.0.1", "10.0.0.1", NULL },
		{ "10.0.0.1:3005", "10.0.0.1", "3005" },
		{ "[::1]", "::1", NULL },
		{ "[::1]:4000", "::1", "4000" },
		{ "::1", "::1", NULL },
		{ "fe80::1:2", "fe80::1:2", NULL },
		{ "2001:db8::10", "2001:db8::10", NULL },
		{ "fe80::1%lo0", "fe80::1%lo0", NULL },
		{ "[fe80::1%eth0]:4000", "fe80::1%eth0", "4000" },
		{ "[::ffff:10.0.0.1]", "::ffff:10.0.0.1", NULL },
	};

	for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
		char* host = NULL;
		char* port = NULL;
		ck_assert_msg(sa_parse_address(good[i][0], &host, &port),
				"rejected \"%s\"", good[i][0]);
		ck_assert_str_eq(host, good[i][1]);

		if (good[i][2] == NULL) {
			ck_assert_ptr_eq(port, NULL);
		}
		else {
			ck_assert_str_eq(port, good[i][2]);
		}

		cf_free(host);
		cf_free(port);
	}
}
END_TEST

START_TEST(address_invalid)
{
	const char* bad[] = { "", ":3005", "host:", "host:0", "host:99999",
		"host:http", "[::1", "[]", "[]:3005", "[::1]x", "[::1]:", "[::1]:0",
		"host:3005:x", "10.0.0.1:3005:1", "1::2::3", "::g", "fe80::1%",
		"1::2::3%lo0", "[host:3005:x]", "[host]", "[10.0.0.1]:3005",
		"[fe80::1%]" };

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		char* host = NULL;
		char* port = NULL;
		ck_assert_msg(!sa_parse_address(bad[i], &host, &port),
				"accepted \"%s\"", bad[i]);
		ck_assert_ptr_eq(host, NULL);
		cf_free(port);
	}
}
END_TEST


static void
sa_defaults(sa_args_t* sa)
{
	sa->host = strdup("127.0.0.1");
	sa->port = strdup("3005");
	sa->timeout_ms = 1000;
	sa->cafile = NULL;
}

static void
sa_free(sa_args_t* sa)
{
	cf_free(sa->host);
	cf_free(sa->port);
}

START_TEST(apply_nothing)
{
	sa_args_t sa;
	sa_defaults(&sa);
	ck_assert_ptr_eq(sa_apply(&sa, NULL, NULL), NULL);
	ck_assert_str_eq(sa.host, "127.0.0.1");
	ck_assert_str_eq(sa.port, "3005");
	sa_free(&sa);
}
END_TEST

START_TEST(apply_address_without_port_keeps_port)
{
	sa_args_t sa;
	sa_defaults(&sa);
	ck_assert_ptr_eq(sa_apply(&sa, "agent", NULL), NULL);
	ck_assert_str_eq(sa.host, "agent");
	ck_assert_str_eq(sa.port, "3005");
	sa_free(&sa);
}
END_TEST

START_TEST(apply_address_port)
{
	sa_args_t sa;
	sa_defaults(&sa);
	ck_assert_ptr_eq(sa_apply(&sa, "[::1]:4000", NULL), NULL);
	ck_assert_str_eq(sa.host, "::1");
	ck_assert_str_eq(sa.port, "4000");
	sa_free(&sa);
}
END_TEST

START_TEST(apply_explicit_port_wins)
{
	sa_args_t sa;
	sa_defaults(&sa);
	ck_assert_ptr_eq(sa_apply(&sa, "agent:4000", "4001"), NULL);
	ck_assert_str_eq(sa.host, "agent");
	ck_assert_str_eq(sa.port, "4001");
	sa_free(&sa);
}
END_TEST

START_TEST(apply_bad_address)
{
	sa_args_t sa;
	sa_defaults(&sa);
	ck_assert_str_eq(sa_apply(&sa, "host:3005:x", "4001"), "sa-address");
	ck_assert_str_eq(sa.host, "127.0.0.1");
	ck_assert_str_eq(sa.port, "3005");
	sa_free(&sa);
}
END_TEST

START_TEST(apply_bad_port)
{
	sa_args_t sa;
	sa_defaults(&sa);
	ck_assert_str_eq(sa_apply(&sa, "agent:4000", "0"), "sa-port");
	ck_assert_str_eq(sa.host, "127.0.0.1");
	ck_assert_str_eq(sa.port, "3005");
	sa_free(&sa);
}
END_TEST


static int
parse(args_t* args, int argc, char** argv)
{
	optind = 0;
	_load_defaults(args);
	capture_start();
	int rv = set_args(argc, argv, args);
	capture_end();
	return rv;
}

START_TEST(cli_defaults)
{
	args_t args;
	char* argv[] = { "asbench" };
	ck_assert_int_eq(parse(&args, 1, argv), 0);
	ck_assert_str_eq(args.sa.host, "127.0.0.1");
	ck_assert_str_eq(args.sa.port, "3005");
	ck_assert_int_eq(args.sa.timeout_ms, 1000);
	ck_assert_ptr_eq(args.sa.cafile, NULL);
	_free_args(&args);
}
END_TEST

START_TEST(cli_all_options)
{
	args_t args;
	char* argv[] = { "asbench", "--sa-address", "[::1]:4000", "--sa-timeout",
		"300", "--sa-ca-file", "/path/to/ca.pem" };
	int rv = parse(&args, 7, argv);
	ck_assert_msg(rv == 0, "set_args returned %d: %s", rv, out_buf);
	ck_assert_str_eq(args.sa.host, "::1");
	ck_assert_str_eq(args.sa.port, "4000");
	ck_assert_int_eq(args.sa.timeout_ms, 300);
	ck_assert_str_eq(args.sa.cafile, "/path/to/ca.pem");
	ck_assert_str_eq(out_buf, "");
	_free_args(&args);
}
END_TEST

START_TEST(cli_explicit_port_after_address)
{
	args_t args;
	char* argv[] = { "asbench", "--sa-address=agent:4000", "--sa-port=4001" };
	ck_assert_int_eq(parse(&args, 3, argv), 0);
	ck_assert_str_eq(args.sa.host, "agent");
	ck_assert_str_eq(args.sa.port, "4001");
	_free_args(&args);
}
END_TEST

START_TEST(cli_explicit_port_before_address)
{
	args_t args;
	char* argv[] = { "asbench", "--sa-port", "4001", "--sa-address",
		"agent:4000" };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.sa.host, "agent");
	ck_assert_str_eq(args.sa.port, "4001");
	_free_args(&args);
}
END_TEST

START_TEST(cli_last_value_wins)
{
	args_t args;
	char* argv[] = { "asbench", "--sa-address", "a:4000", "--sa-address", "b",
		"--sa-cafile", "x.pem", "--sa-cafile", "y.pem" };
	ck_assert_int_eq(parse(&args, 9, argv), 0);
	ck_assert_str_eq(args.sa.host, "b");
	ck_assert_str_eq(args.sa.port, "3005");
	ck_assert_str_eq(args.sa.cafile, "y.pem");
	_free_args(&args);
}
END_TEST

START_TEST(cli_password_uses_later_sa_options)
{
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P", "secrets:asbench:pw",
		"--sa-port", closed_port, "--sa-ca-file", MISSING_CA };
	ck_assert_int_ne(parse(&args, 9, argv), 0);
	ck_assert_str_eq(out_buf, "--sa-ca-file: cannot read file " MISSING_CA
			": No such file or directory\n");
	_free_args(&args);
}
END_TEST

START_TEST(cli_tls_keyfile_password_uses_later_sa_options)
{
	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile-password=secrets:asbench:pw",
		"--tls-keyfile", "key.pem", "--sa-ca-file", MISSING_CA, "--tls-enable" };
	ck_assert_int_ne(parse(&args, 7, argv), 0);
	ck_assert_str_eq(out_buf, "--sa-ca-file: cannot read file " MISSING_CA
			": No such file or directory\n");
	cf_free(args.tls.keyfile);
	cf_free(args.tls.keyfile_pw);
	_free_args(&args);
}
END_TEST

START_TEST(cli_sa_options_unused_without_secrets)
{
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P", SECRET, "--sa-ca-file",
		MISSING_CA, "--sa-port", closed_port };
	int rv = parse(&args, 9, argv);
	ck_assert_msg(rv == 0, "set_args returned %d: %s", rv, out_buf);
	ck_assert_str_eq(args.password, SECRET);
	ck_assert_str_eq(out_buf, "");
	_free_args(&args);
}
END_TEST

START_TEST(cli_bad_values)
{
#define TIMEOUT_EXPECTED ", expected an integer from 1 to 2147483647\n"
#define PORT_EXPECTED ", expected an integer from 1 to 65535\n"
#define ADDRESS_EXPECTED ", expected <host>[:<port>] or [<ipv6>][:<port>]\n"
	const char* cases[][3] = {
		{ "--sa-timeout", "0", "--sa-timeout: invalid value \"0\"" TIMEOUT_EXPECTED },
		{ "--sa-timeout", "", "--sa-timeout: invalid value \"\"" TIMEOUT_EXPECTED },
		{ "--sa-timeout", "abc", "--sa-timeout: invalid value \"abc\""
			TIMEOUT_EXPECTED },
		{ "--sa-timeout", "2147483648", "--sa-timeout: invalid value "
			"\"2147483648\"" TIMEOUT_EXPECTED },
		{ "--sa-timeout", "+5", "--sa-timeout: invalid value \"+5\""
			TIMEOUT_EXPECTED },
		{ "--sa-timeout", " 5", "--sa-timeout: invalid value \" 5\""
			TIMEOUT_EXPECTED },
		{ "--sa-timeout", "\xef\xbc\x95", "--sa-timeout: invalid value "
			"\"\xef\xbc\x95\"" TIMEOUT_EXPECTED },
		{ "--sa-port", "65536", "--sa-port: invalid value \"65536\"" PORT_EXPECTED },
		{ "--sa-port", "http", "--sa-port: invalid value \"http\"" PORT_EXPECTED },
		{ "--sa-address", "", "--sa-address: invalid value \"\"" ADDRESS_EXPECTED },
		{ "--sa-address", "[::1", "--sa-address: invalid value \"[::1\""
			ADDRESS_EXPECTED },
		{ "--sa-address", "1::2::3", "--sa-address: invalid value \"1::2::3\""
			ADDRESS_EXPECTED },
		{ "--sa-address", "host:3005:x", "--sa-address: invalid value "
			"\"host:3005:x\"" ADDRESS_EXPECTED },
		{ "--sa-address", "[host:3005:x]", "--sa-address: invalid value "
			"\"[host:3005:x]\"" ADDRESS_EXPECTED },
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		args_t args;
		char* argv[] = { "asbench", "-P", "secrets:asbench:pw",
			(char*) cases[i][0], (char*) cases[i][1] };
		ck_assert_int_ne(parse(&args, 5, argv), 0);
		ck_assert_str_eq(out_buf, cases[i][2]);
		_free_args(&args);
	}
}
END_TEST

START_TEST(cli_sa_ca_file)
{
	args_t args;
	char* argv[] = { "asbench", "--sa-ca-file", "/path/to/ca.pem" };
	int rv = parse(&args, 3, argv);
	ck_assert_msg(rv == 0, "set_args returned %d: %s", rv, out_buf);
	ck_assert_str_eq(args.sa.cafile, "/path/to/ca.pem");
	ck_assert_str_eq(out_buf, "");
	_free_args(&args);
}
END_TEST

START_TEST(cli_sa_cafile_deprecated)
{
	args_t args;
	char* argv[] = { "asbench", "--sa-cafile", "/path/to/ca.pem" };
	ck_assert_int_eq(parse(&args, 3, argv), 0);
	ck_assert_str_eq(args.sa.cafile, "/path/to/ca.pem");
	ck_assert_msg(strstr(out_buf, DEPRECATED_CAFILE) != NULL,
			"no deprecation warning in \"%s\"", out_buf);
	_free_args(&args);
}
END_TEST

START_TEST(cli_sa_ca_file_wins)
{
	char* argv[][5] = {
		{ "asbench", "--sa-cafile", "a.pem", "--sa-ca-file", "b.pem" },
		{ "asbench", "--sa-ca-file", "b.pem", "--sa-cafile", "a.pem" },
	};

	for (size_t i = 0; i < sizeof(argv) / sizeof(argv[0]); i++) {
		args_t args;
		int rv = parse(&args, 5, argv[i]);
		ck_assert_msg(rv == 0, "set_args returned %d: %s", rv, out_buf);
		ck_assert_str_eq(args.sa.cafile, "b.pem");
		_free_args(&args);
	}
}
END_TEST

START_TEST(cli_sa_cafile_not_read_with_sa_ca_file)
{
	char* argv[][11] = {
		{ "asbench", "-U", "admin", "-P", "secrets:asbench:pw", "--sa-port",
			closed_port, "--sa-cafile", MISSING_OTHER_CA, "--sa-ca-file",
			MISSING_CA },
		{ "asbench", "-U", "admin", "-P", "secrets:asbench:pw", "--sa-port",
			closed_port, "--sa-ca-file", MISSING_CA, "--sa-cafile",
			MISSING_OTHER_CA },
	};

	for (size_t i = 0; i < sizeof(argv) / sizeof(argv[0]); i++) {
		args_t args;
		ck_assert_int_ne(parse(&args, 11, argv[i]), 0);
		ck_assert_msg(strstr(out_buf, "--sa-ca-file: cannot read file "
					MISSING_CA ": No such file or directory\n") != NULL,
				"--sa-ca-file not read: %s", out_buf);
		ck_assert_ptr_eq(strstr(out_buf, MISSING_OTHER_CA), NULL);
		_free_args(&args);
	}
}
END_TEST

static void
assert_cafile_rejected(const char* contents)
{
	args_t args;
	char path[] = "/tmp/asbench_ca_XXXXXX";
	char expected[128];
	int fd = mkstemp(path);
	ck_assert_int_ge(fd, 0);

	ssize_t written = write(fd, contents, strlen(contents));
	close(fd);
	snprintf(expected, sizeof(expected),
			"--sa-cafile: cannot load TLS CA file %s: ", path);

	char* argv[] = { "asbench", "-U", "admin", "-P", "secrets:asbench:pw",
		"--sa-port", closed_port, "--sa-cafile", path };
	int rv = parse(&args, 9, argv);
	unlink(path);
	ck_assert_int_eq(written, (ssize_t) strlen(contents));
	ck_assert_int_ne(rv, 0);
	ck_assert_msg(strstr(out_buf, expected) != NULL,
			"no CA file error naming the option in \"%s\"", out_buf);
	ck_assert_ptr_eq(strstr(out_buf, "secret-agent: ERR: connect"), NULL);
	_free_args(&args);
}

START_TEST(cli_empty_cafile_names_option)
{
	assert_cafile_rejected("");
}
END_TEST

START_TEST(cli_cafile_not_pem_names_option)
{
	assert_cafile_rejected("not a certificate\n");
}
END_TEST

static void
assert_empty_ca_path_rejected(char* opt)
{
	args_t args;
	char expected[32];

	snprintf(expected, sizeof(expected), "%s: ", opt);

	char* argv[] = { "asbench", "-U", "u", "-P", "secrets:a:b", "--sa-port",
		closed_port, opt, "" };
	ck_assert_int_ne(parse(&args, 9, argv), 0);
	ck_assert_msg(strstr(out_buf, expected) != NULL,
			"no error naming %s in \"%s\"", opt, out_buf);
	ck_assert_ptr_eq(strstr(out_buf, "secret-agent: ERR: connect"), NULL);
	_free_args(&args);
}

START_TEST(cli_empty_sa_ca_file_path_names_option)
{
	assert_empty_ca_path_rejected("--sa-ca-file");
}
END_TEST

START_TEST(cli_empty_sa_cafile_path_names_option)
{
	assert_empty_ca_path_rejected("--sa-cafile");
}
END_TEST

START_TEST(cli_empty_ca_path_rejected_without_secrets)
{
	// As in asbackup, aql and asadm, on every run.
	static char* const options[] = { "--sa-ca-file", "--sa-cafile" };

	for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
		args_t args;
		char expected[128];
		snprintf(expected, sizeof(expected), "%s: invalid value \"\", expected "
				"a path to a CA certificate file\n", options[i]);

		char* argv[] = { "asbench", options[i], "" };
		ck_assert_int_ne(parse(&args, 3, argv), 0);
		ck_assert_msg(strstr(out_buf, expected) != NULL, "\"%s\" not in \"%s\"",
				expected, out_buf);
		_free_args(&args);
	}
}
END_TEST

START_TEST(cli_empty_cafile_ignored_next_to_ca_file)
{
	// Only the option that wins is checked.
	char* argv[][5] = {
		{ "asbench", "--sa-ca-file", "b.pem", "--sa-cafile", "" },
		{ "asbench", "--sa-cafile", "", "--sa-ca-file", "b.pem" },
	};

	for (size_t i = 0; i < sizeof(argv) / sizeof(argv[0]); i++) {
		args_t args;
		int rv = parse(&args, 5, argv[i]);
		ck_assert_msg(rv == 0, "set_args returned %d: %s", rv, out_buf);
		ck_assert_str_eq(args.sa.cafile, "b.pem");
		_free_args(&args);
	}
}
END_TEST

START_TEST(cli_tls_keyfile_password_not_fetched_without_tls)
{
	// The key is never used without --tls-enable, so an agent outage must not
	// fail the run.
	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password", "secrets:asbench:pw", "--sa-port", closed_port };
	int rv = parse(&args, 7, argv);
	ck_assert_msg(rv == 0, "set_args returned %d: %s", rv, out_buf);
	ck_assert_ptr_eq(strstr(out_buf, "secret-agent"), NULL);
	cf_free(args.tls.keyfile);
	cf_free(args.tls.keyfile_pw);
	_free_args(&args);
}
END_TEST

START_TEST(cli_leftover_argument_rejected)
{
	// getopt stops at the first operand under POSIXLY_CORRECT or after "--", so
	// any option after it, such as --sa-ca-file, would be dropped silently.
	char* argv[][4] = {
		{ "asbench", "stray", "--sa-ca-file", MISSING_CA },
		{ "asbench", "--", "--sa-ca-file", MISSING_CA },
	};
	const char* expected[] = { "stray", "--sa-ca-file" };

	for (size_t i = 0; i < sizeof(argv) / sizeof(argv[0]); i++) {
		args_t args;
		char message[128];
		snprintf(message, sizeof(message), "Unexpected argument '%s'\n",
				expected[i]);

		ck_assert_int_ne(parse(&args, 4, argv[i]), 0);
		ck_assert_msg(strstr(out_buf, message) != NULL, "\"%s\" not in \"%s\"",
				message, out_buf);
		_free_args(&args);
	}
}
END_TEST

START_TEST(cli_space_separated_password_is_consumed)
{
	args_t args;
	char* argv[][5] = {
		{ "asbench", "-U", "admin", "-P", SECRET },
		{ "asbench", "--tls-keyfile", "key.pem", "--tls-keyfile-password", SECRET },
	};

	int rv = parse(&args, 5, argv[0]);
	ck_assert_msg(rv == 0, "set_args returned %d: %s", rv, out_buf);
	ck_assert_str_eq(args.password, SECRET);
	_free_args(&args);

	rv = parse(&args, 5, argv[1]);
	ck_assert_msg(rv == 0, "set_args returned %d: %s", rv, out_buf);
	cf_free(args.tls.keyfile);
	cf_free(args.tls.keyfile_pw);
	_free_args(&args);
}
END_TEST

START_TEST(cli_password_secrets_without_user_warns)
{
	args_t args;
	char* argv[] = { "asbench", "-P", "secrets:asbench:pw", "--sa-port",
		closed_port };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.password, "");
	ck_assert_str_eq(out_buf,
			"Warning: --password: ignored because no user is set\n");
	_free_args(&args);
}
END_TEST

START_TEST(cli_password_secrets_not_fetched_without_user)
{
	args_t args;
	char* argv[] = { "asbench", "-P", "secrets:asbench:pw", "--sa-port",
		closed_port };
	ck_assert_int_eq(parse(&args, 5, argv), 0);
	ck_assert_str_eq(args.password, "");
	ck_assert_ptr_eq(strstr(out_buf, "secret"), NULL);
	_free_args(&args);
}
END_TEST

START_TEST(cli_user_not_fetched)
{
	args_t args;
	char* argv[] = { "asbench", "-U", "secrets:asbench:user", "-P", SECRET,
		"--sa-port", closed_port };
	ck_assert_int_eq(parse(&args, 7, argv), 0);
	ck_assert_str_eq(args.user, "secrets:asbench:user");
	ck_assert_str_eq(args.password, SECRET);
	ck_assert_str_eq(out_buf, "");
	_free_args(&args);
}
END_TEST

START_TEST(cli_password_pki_secrets_not_fetched)
{
	args_t args;
	char* with_user[] = { "asbench", "--auth", "PKI", "-U", "admin", "-P",
		"secrets:asbench:pw", "--sa-port", closed_port };
	ck_assert_int_eq(parse(&args, 9, with_user), 0);
	ck_assert_str_eq(args.password, "secrets:asbench:pw");
	ck_assert_str_eq(out_buf, "");
	_free_args(&args);

	char* without_user[] = { "asbench", "--auth", "PKI", "-P",
		"secrets:asbench:pw", "--sa-port", closed_port };
	ck_assert_int_eq(parse(&args, 7, without_user), 0);
	ck_assert_str_eq(out_buf, "");
	_free_args(&args);
}
END_TEST


START_TEST(log_allowed_line_prints_args)
{
	capture_start();
	sa_log_cb("ERR: failed to lookup address: %s", "agent.example.com");
	sa_log_cb("ERR: response: %.*s", 9, "not found in config");
	sa_log_cb("ERR: SSL_connect certificate verify failed: %s (%ld)",
			"IP address mismatch", 64L);
	sa_log_cb("ERR: unable to set TLS peer name: %s", "agent.example.com");
	capture_end();
	ck_assert_str_eq(out_buf, "secret-agent: ERR: failed to lookup address: "
			"agent.example.com\nsecret-agent: ERR: response: not found\n"
			"secret-agent: ERR: SSL_connect certificate verify failed: IP address "
			"mismatch (64)\nsecret-agent: ERR: unable to set TLS peer name: "
			"agent.example.com\n");
}
END_TEST

START_TEST(log_connect_failed_adds_strerror)
{
	char* pw = NULL;
	char expected[256];
	snprintf(expected, sizeof(expected), ", errno: %d (%s)\n", ECONNREFUSED,
			strerror(ECONNREFUSED));

	ck_assert(!resolve("--password", "secrets:asbench:pw", &pw));

	const char* line = strstr(out_buf, "secret-agent: ERR: connect failed");
	ck_assert_msg(line != NULL, "no connect failed line in \"%s\"", out_buf);

	const char* tail = strstr(line, expected);
	ck_assert_msg(tail != NULL && tail + strlen(expected) - 1 ==
			strchr(line, '\n'), "connect failed line without errno and its "
			"reason in \"%s\"", out_buf);
}
END_TEST

START_TEST(log_unlisted_line_is_cut)
{
	capture_start();
	sa_log_cb("ERR: failed to parse response JSON line %d (%s)", 1,
			"'[' or '{' expected near '" SECRET_B64 "'");
	sa_log_cb("ERR: failed to parse response JSON line %d, column %d, "
			"position %d", 1, 29, 29);
	sa_log_cb("ERR: failed asking for secret - %s",
			"{\"SecretKey\":\"" SECRET "\"}");
	sa_log_cb("ERR: response: %s", SECRET);
	sa_log_cb("ERR: failed reading secret header, errno: %d", 104);
	capture_end();
	ck_assert_str_eq(out_buf,
			"secret-agent: ERR: failed to parse response JSON line\n"
			"secret-agent: ERR: failed to parse response JSON line\n"
			"secret-agent: ERR: failed asking for secret\n"
			"secret-agent: ERR: response\n"
			"secret-agent: ERR: failed reading secret header, errno\n");
	ck_assert_ptr_eq(strstr(out_buf, SECRET), NULL);
	ck_assert_ptr_eq(strstr(out_buf, SECRET_B64), NULL);
}
END_TEST

START_TEST(log_line_without_args)
{
	capture_start();
	sa_log_cb("ERR: socket poll timed out");
	sa_log_cb("ERR: connect timed out");
	capture_end();
	ck_assert_str_eq(out_buf, "secret-agent: ERR: socket poll timed out\n"
			"secret-agent: ERR: connect timed out\n");
}
END_TEST


Suite*
secret_agent_suite(void)
{
	Suite* s;
	TCase* tc_resolve;
	TCase* tc_parse;
	TCase* tc_apply;
	TCase* tc_cli;
	TCase* tc_log;

	s = suite_create("Secret Agent");

	tc_resolve = tcase_create("Resolve");
	tcase_add_checked_fixture(tc_resolve, setup_agent, NULL);
	tcase_add_test(tc_resolve, resolve_prefix_case_sensitive);
	tcase_add_test(tc_resolve, resolve_agent_unreachable);
	tcase_add_test(tc_resolve, resolve_cafile_unreadable);
	tcase_add_test(tc_resolve, resolve_without_agent_settings);
	tcase_add_test(tc_resolve, resolve_bad_port);
	suite_add_tcase(s, tc_resolve);

	tc_parse = tcase_create("Parse");
	tcase_add_test(tc_parse, timeout_valid);
	tcase_add_test(tc_parse, timeout_invalid);
	tcase_add_test(tc_parse, port_valid);
	tcase_add_test(tc_parse, port_invalid);
	tcase_add_test(tc_parse, address_valid);
	tcase_add_test(tc_parse, address_invalid);
	suite_add_tcase(s, tc_parse);

	tc_apply = tcase_create("Apply");
	tcase_add_test(tc_apply, apply_nothing);
	tcase_add_test(tc_apply, apply_address_without_port_keeps_port);
	tcase_add_test(tc_apply, apply_address_port);
	tcase_add_test(tc_apply, apply_explicit_port_wins);
	tcase_add_test(tc_apply, apply_bad_address);
	tcase_add_test(tc_apply, apply_bad_port);
	suite_add_tcase(s, tc_apply);

	tc_cli = tcase_create("Command line");
	tcase_add_checked_fixture(tc_cli, setup_agent, NULL);
	tcase_add_test(tc_cli, cli_defaults);
	tcase_add_test(tc_cli, cli_all_options);
	tcase_add_test(tc_cli, cli_explicit_port_after_address);
	tcase_add_test(tc_cli, cli_explicit_port_before_address);
	tcase_add_test(tc_cli, cli_last_value_wins);
	tcase_add_test(tc_cli, cli_password_uses_later_sa_options);
	tcase_add_test(tc_cli, cli_tls_keyfile_password_uses_later_sa_options);
	tcase_add_test(tc_cli, cli_sa_options_unused_without_secrets);
	tcase_add_test(tc_cli, cli_bad_values);
	tcase_add_test(tc_cli, cli_sa_ca_file);
	tcase_add_test(tc_cli, cli_sa_cafile_deprecated);
	tcase_add_test(tc_cli, cli_sa_ca_file_wins);
	tcase_add_test(tc_cli, cli_sa_cafile_not_read_with_sa_ca_file);
	tcase_add_test(tc_cli, cli_empty_cafile_names_option);
	tcase_add_test(tc_cli, cli_cafile_not_pem_names_option);
	tcase_add_test(tc_cli, cli_empty_sa_ca_file_path_names_option);
	tcase_add_test(tc_cli, cli_empty_sa_cafile_path_names_option);
	tcase_add_test(tc_cli, cli_empty_ca_path_rejected_without_secrets);
	tcase_add_test(tc_cli, cli_empty_cafile_ignored_next_to_ca_file);
	tcase_add_test(tc_cli, cli_tls_keyfile_password_not_fetched_without_tls);
	tcase_add_test(tc_cli, cli_leftover_argument_rejected);
	tcase_add_test(tc_cli, cli_space_separated_password_is_consumed);
	tcase_add_test(tc_cli, cli_password_secrets_without_user_warns);
	tcase_add_test(tc_cli, cli_password_secrets_not_fetched_without_user);
	tcase_add_test(tc_cli, cli_user_not_fetched);
	tcase_add_test(tc_cli, cli_password_pki_secrets_not_fetched);
	suite_add_tcase(s, tc_cli);

	tc_log = tcase_create("Log filter");
	tcase_add_checked_fixture(tc_log, setup_agent, NULL);
	tcase_add_test(tc_log, log_allowed_line_prints_args);
	tcase_add_test(tc_log, log_connect_failed_adds_strerror);
	tcase_add_test(tc_log, log_unlisted_line_is_cut);
	tcase_add_test(tc_log, log_line_without_args);
	suite_add_tcase(s, tc_log);

	return s;
}
