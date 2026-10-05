
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
	ck_assert_ptr_eq(strstr(out_buf, "secret-agent: ERR: connect failed: "),
			out_buf);
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
		"300", "--sa-cafile", "/path/to/ca.pem" };
	ck_assert_int_eq(parse(&args, 7, argv), 0);
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
		"--sa-port", closed_port, "--sa-cafile", MISSING_CA };
	ck_assert_int_ne(parse(&args, 9, argv), 0);
	ck_assert_str_eq(out_buf, "--sa-cafile: cannot read file " MISSING_CA
			": No such file or directory\n");
	_free_args(&args);
}
END_TEST

START_TEST(cli_tls_keyfile_password_uses_later_sa_options)
{
	args_t args;
	char* argv[] = { "asbench", "--tls-keyfile-password=secrets:asbench:pw",
		"--tls-keyfile", "key.pem", "--sa-cafile", MISSING_CA };
	ck_assert_int_ne(parse(&args, 6, argv), 0);
	ck_assert_str_eq(out_buf, "--sa-cafile: cannot read file " MISSING_CA
			": No such file or directory\n");
	cf_free(args.tls.keyfile);
	cf_free(args.tls.keyfile_pw);
	_free_args(&args);
}
END_TEST

START_TEST(cli_sa_options_unused_without_secrets)
{
	args_t args;
	char* argv[] = { "asbench", "-U", "admin", "-P", SECRET, "--sa-cafile",
		MISSING_CA, "--sa-port", closed_port };
	ck_assert_int_eq(parse(&args, 9, argv), 0);
	ck_assert_str_eq(args.password, SECRET);
	ck_assert_str_eq(out_buf, "");
	_free_args(&args);
}
END_TEST

START_TEST(cli_bad_values)
{
	const char* cases[][3] = {
		{ "--sa-timeout", "0", "--sa-timeout: invalid value 0, expected an "
			"integer from 1 to 2147483647\n" },
		{ "--sa-timeout", "abc", "--sa-timeout: invalid value abc, expected an "
			"integer from 1 to 2147483647\n" },
		{ "--sa-timeout", "2147483648", "--sa-timeout: invalid value "
			"2147483648, expected an integer from 1 to 2147483647\n" },
		{ "--sa-port", "65536", "--sa-port: invalid value 65536\n" },
		{ "--sa-port", "http", "--sa-port: invalid value http\n" },
		{ "--sa-address", "", "--sa-address: invalid value \n" },
		{ "--sa-address", "[::1", "--sa-address: invalid value [::1\n" },
		{ "--sa-address", "1::2::3", "--sa-address: invalid value 1::2::3\n" },
		{ "--sa-address", "host:3005:x", "--sa-address: invalid value "
			"host:3005:x\n" },
		{ "--sa-address", "[host:3005:x]", "--sa-address: invalid value "
			"[host:3005:x]\n" },
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
	char expected[256];
	snprintf(expected, sizeof(expected), "secret-agent: ERR: connect failed: "
			"3, errno: %d (%s)\n", ECONNREFUSED, strerror(ECONNREFUSED));

	capture_start();
	errno = ECONNREFUSED;
	sa_log_cb("ERR: connect failed: %d, errno: %d", 3, ECONNREFUSED);
	capture_end();
	ck_assert_str_eq(out_buf, expected);
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
	suite_add_tcase(s, tc_cli);

	tc_log = tcase_create("Log filter");
	tcase_add_test(tc_log, log_allowed_line_prints_args);
	tcase_add_test(tc_log, log_connect_failed_adds_strerror);
	tcase_add_test(tc_log, log_unlisted_line_is_cut);
	tcase_add_test(tc_log, log_line_without_args);
	suite_add_tcase(s, tc_log);

	return s;
}
