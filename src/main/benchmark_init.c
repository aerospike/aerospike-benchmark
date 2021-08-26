/*******************************************************************************
 * Copyright 2008-2020 by Aerospike.
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
// Forward declarations.
//

#include <benchmark.h>
#include <common.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(_MSC_VER)
#undef _UNICODE  // Use ASCII getopt version on windows.
#endif
#include <getopt.h>


//==========================================================
// Typedefs & constants.
//

static const char* short_options = "h:p:U:P:n:s:b:K:k:o:Rt:w:z:g:T:dL:SC:N:B:M:Y:Dac:W:";

#define WARN_MSG 0x40000000

/*
 * Identifies the TLS client command line options.
 */
typedef enum {
	TLS_OPT_ENABLE = 1000,
	TLS_OPT_NAME,
	TLS_OPT_CA_FILE,
	TLS_OPT_CA_PATH,
	TLS_OPT_PROTOCOLS,
	TLS_OPT_CIPHER_SUITE,
	TLS_OPT_CRL_CHECK,
	TLS_OPT_CRL_CHECK_ALL,
	TLS_OPT_CERT_BLACK_LIST,
	TLS_OPT_LOG_SESSION_INFO,
	TLS_OPT_KEY_FILE,
	TLS_OPT_KEY_FILE_PASSWORD,
	TLS_OPT_CERT_FILE,
	TLS_OPT_LOGIN_ONLY
} tls_opt;

static struct option long_options[] = {
	{"help",                  no_argument,       0, '9'},
	{"hosts",                 required_argument, 0, 'h'},
	{"port",                  required_argument, 0, 'p'},
	{"user",                  required_argument, 0, 'U'},
	{"password",              optional_argument, 0, 'P'},
	{"services-alternate",    no_argument,       0, '*'},
	{"namespace",             required_argument, 0, 'n'},
	{"set",                   required_argument, 0, 's'},
	{"bin",                   required_argument, 0, 'b'},
	{"start-key",             required_argument, 0, 'K'},
	{"keys",                  required_argument, 0, 'k'},
	{"udf-package-name",      required_argument, 0, ':'},
	{"upn",                   required_argument, 0, ':'},
	{"udf-function-name",     required_argument, 0, ';'},
	{"ufn",                   required_argument, 0, ';'},
	{"udf-function-values",   required_argument, 0, '"'},
	{"ufv",                   required_argument, 0, '"'},
	{"object-spec",           required_argument, 0, 'o'},
	{"random",                no_argument,       0, 'R'},
	{"duration",              required_argument, 0, 't'},
	{"workload",              required_argument, 0, 'w'},
	{"workload-stages",       required_argument, 0, '.'},
	{"read-bins",             required_argument, 0, '+'},
	{"write-bins",            required_argument, 0, '-'},
	{"threads",               required_argument, 0, 'z'},
	{"throughput",            required_argument, 0, 'g'},
	{"batch-size",            required_argument, 0, '0'},
	{"compress",              no_argument,       0, '4'},
	{"compression-ratio",     required_argument, 0, '5'},
	{"socket-timeout",        required_argument, 0, '1'},
	{"read-socket-timeout",   required_argument, 0, '2'},
	{"write-socket-timeout",  required_argument, 0, '3'},
	{"timeout",               required_argument, 0, 'T'},
	{"read-timeout",          required_argument, 0, 'X'},
	{"write-timeout",         required_argument, 0, 'V'},
	{"max-retries",           required_argument, 0, 'r'},
	{"debug",                 no_argument,       0, 'd'},
	{"latency",               no_argument,       0, 'L'},
	{"percentiles",           required_argument, 0, '8'},
	{"output-file",           required_argument, 0, '6'},
	{"output-period",         required_argument, 0, '7'},
	{"hdr-hist",              required_argument, 0, '/'},
	{"shared",                no_argument,       0, 'S'},
	{"replica",               required_argument, 0, 'C'},
	{"read-mode-ap",          required_argument, 0, 'N'},
	{"read-mode-sc",          required_argument, 0, 'B'},
	{"commit-level",          required_argument, 0, 'M'},
	{"conn-pools-per-node",   required_argument, 0, 'Y'},
	{"durable-delete",        no_argument,       0, 'D'},
	{"async",                 no_argument,       0, 'a'},
	{"async-max-commands",    required_argument, 0, 'c'},
	{"event-loops",           required_argument, 0, 'W'},
	{"tls-enable",            no_argument,       0, TLS_OPT_ENABLE},
	{"tls-name",              no_argument,       0, TLS_OPT_NAME},
	{"tls-cafile",            required_argument, 0, TLS_OPT_CA_FILE},
	{"tls-capath",            required_argument, 0, TLS_OPT_CA_PATH},
	{"tls-protocols",         required_argument, 0, TLS_OPT_PROTOCOLS},
	{"tls-cipher-suite",      required_argument, 0, TLS_OPT_CIPHER_SUITE},
	{"tls-crl-check",         no_argument,       0, TLS_OPT_CRL_CHECK},
	{"tls-crl-check-all",     no_argument,       0, TLS_OPT_CRL_CHECK_ALL},
	{"tls-cert-blacklist",   required_argument, 0, TLS_OPT_CERT_BLACK_LIST},
	{"tls-log-session-info",  no_argument,       0, TLS_OPT_LOG_SESSION_INFO},
	{"tls-keyfile",          required_argument, 0, TLS_OPT_KEY_FILE},
	{"tls-keyfile-password", optional_argument, 0, TLS_OPT_KEY_FILE_PASSWORD},
	{"tls-certfile",         required_argument, 0, TLS_OPT_CERT_FILE},
	{"tls-login-only",        no_argument,       0, TLS_OPT_LOGIN_ONLY},
	{"auth",                  required_argument, 0, 'e'},

	{"servicesAlternate",     no_argument,       0, WARN_MSG | '*'},
	{"startKey",              required_argument, 0, WARN_MSG | 'K'},
	{"udfPackageName",        required_argument, 0, WARN_MSG | ':'},
	{"udfFunctionName",       required_argument, 0, WARN_MSG | ';'},
	{"udfFunctionValues",     required_argument, 0, WARN_MSG | '"'},
	{"objectSpec",            required_argument, 0, WARN_MSG | 'o'},
	{"workloadStages",        required_argument, 0, WARN_MSG | '.'},
	{"readBins",              required_argument, 0, WARN_MSG | '+'},
	{"writeBins",             required_argument, 0, WARN_MSG | '-'},
	{"batchSize",             required_argument, 0, WARN_MSG | '0'},
	{"compressionRatio",      required_argument, 0, WARN_MSG | '5'},
	{"socketTimeout",         required_argument, 0, WARN_MSG | '1'},
	{"readSocketTimeout",     required_argument, 0, WARN_MSG | '2'},
	{"writeSocketTimeout",    required_argument, 0, WARN_MSG | '3'},
	{"readTimeout",           required_argument, 0, WARN_MSG | 'X'},
	{"writeTimeout",          required_argument, 0, WARN_MSG | 'V'},
	{"maxRetries",            required_argument, 0, WARN_MSG | 'r'},
	{"outputFile",            required_argument, 0, WARN_MSG | '6'},
	{"outputPeriod",          required_argument, 0, WARN_MSG | '7'},
	{"hdrHist",               required_argument, 0, WARN_MSG | '/'},
	{"readModeAP",            required_argument, 0, WARN_MSG | 'N'},
	{"readModeSC",            required_argument, 0, WARN_MSG | 'B'},
	{"commitLevel",           required_argument, 0, WARN_MSG | 'M'},
	{"connPoolsPerNode",      required_argument, 0, WARN_MSG | 'Y'},
	{"durableDelete",         no_argument,       0, WARN_MSG | 'D'},
	{"asyncMaxCommands",      required_argument, 0, WARN_MSG | 'c'},
	{"eventLoops",            required_argument, 0, WARN_MSG | 'W'},
	{"tlsEnable",             no_argument,       0, WARN_MSG | TLS_OPT_ENABLE},
	{"tlsCaFile",             required_argument, 0, WARN_MSG | TLS_OPT_CA_FILE},
	{"tlsCaPath",             required_argument, 0, WARN_MSG | TLS_OPT_CA_PATH},
	{"tlsProtocols",          required_argument, 0, WARN_MSG | TLS_OPT_PROTOCOLS},
	{"tlsCipherSuite",        required_argument, 0, WARN_MSG | TLS_OPT_CIPHER_SUITE},
	{"tlsCrlCheck",           no_argument,       0, WARN_MSG | TLS_OPT_CRL_CHECK},
	{"tlsCrlCheckAll",        no_argument,       0, WARN_MSG | TLS_OPT_CRL_CHECK_ALL},
	{"tlsCertBlackList",      required_argument, 0, WARN_MSG | TLS_OPT_CERT_BLACK_LIST},
	{"tlsLogSessionInfo",     no_argument,       0, WARN_MSG | TLS_OPT_LOG_SESSION_INFO},
	{"tlsKeyFile",            required_argument, 0, WARN_MSG | TLS_OPT_KEY_FILE},
	{"tlsKeyFilePassword",    optional_argument, 0, WARN_MSG | TLS_OPT_KEY_FILE_PASSWORD},
	{"tlsCertFile",           required_argument, 0, WARN_MSG | TLS_OPT_CERT_FILE},
	{"tlsLoginOnly",          no_argument,       0, WARN_MSG | TLS_OPT_LOGIN_ONLY},

	{0, 0, 0, 0}
};


//==========================================================
// Forward declarations.
//

LOCAL_HELPER void print_usage(const char* program);
LOCAL_HELPER void print_args(args_t* args);
LOCAL_HELPER int validate_args(args_t* args);
LOCAL_HELPER stage_def_t* get_or_init_stage(args_t* args);
LOCAL_HELPER const char* camelcase_to_dash(const char* camel_str);
LOCAL_HELPER int set_args(int argc, char * const* argv, args_t* args);
LOCAL_HELPER void _load_defaults(args_t* args);
LOCAL_HELPER int _load_defaults_post(args_t* args);
LOCAL_HELPER void _free_args(args_t* args);


//==========================================================
// Public API.
//

int
benchmark_init(int argc, char* argv[])
{
	args_t args;
	_load_defaults(&args);

	int ret = set_args(argc, argv, &args);

	if (ret == 0) {
		ret = _load_defaults_post(&args);
	}

	if (ret == 0) {
		print_args(&args);
		ret = run_benchmark(&args);
	}
	else if (ret != -1) {
		printf("Run with --help for usage information and flag options.\n");
	}
	_free_args(&args);
	return ret;
}


//==========================================================
// Local helpers.
//

LOCAL_HELPER void
print_usage(const char* program)
{
	printf("Usage: %s <options>\n", program);
	printf("options:\n");
	printf("\n");

	printf("   --help\n");
	printf("   Prints this message\n");
	printf("\n");

	printf("-h --hosts <host1>[:<tlsname1>][:<port1>],...  # Default: localhost\n");
	printf("   Server seed hostnames or IP addresses.\n");
	printf("   The tlsname is only used when connecting with a secure TLS enabled server.\n");
	printf("   If the port is not specified, the default port is used. Examples:\n");
	printf("\n");
	printf("   host1\n");
	printf("   host1:3000,host2:3000\n");
	printf("   192.168.1.10:cert1:3000,192.168.1.20:cert2:3000\n");
	printf("\n");

	printf("-p --port <port> # Default: 3000\n");
	printf("   Server default port.\n");
	printf("\n");

	printf("-U --user <user name> # Default: empty\n");
	printf("   User name for Aerospike servers that require authentication.\n");
	printf("\n");

	printf("-P[<password>]  # Default: empty\n");
	printf("   User's password for Aerospike servers that require authentication.\n");
	printf("   If -P is set, the actual password if optional. If the password is not given,\n");
	printf("   the user will be prompted on the command line.\n");
	printf("   If the password is given, it must be provided directly after -P with no\n");
	printf("   intervening space (ie. -Pmypass).\n");
	printf("\n");

	printf("   --services-alternate\n");
	printf("   Enables \"services-alternate\" instead of \"services\" when connecting to the server\n");
	printf("\n");

	printf("-n --namespace <ns>   # Default: test\n");
	printf("   Aerospike namespace.\n");
	printf("\n");

	printf("-s --set <set name>   # Default: testset\n");
	printf("   Aerospike set name.\n");
	printf("\n");

	printf("-b --bin <bin name>   # Default: testbin\n");
	printf("   The base name to use for bins. The first bin will be <bin_name>, the second will be\n");
	printf("   <bin_name>_2, and so on.\n");
	printf("\n");

	printf("   --workload-stages <path/to/workload_stages.yml>\n");
	printf("   Accepts a path to a workload stages yml file, which should contain a list of\n");
	printf("       workload stages to run through.\n");
	printf("   Each stage must include:\n");
	printf("     duration: in seconds\n");
	printf("     workload: Workload type\n");
	printf("   Optionally each stage should include:\n");
	printf("     tps : max possible with 0 (default), or specified transactions per second\n");
	printf("     object-spec: Object spec for the stage. Otherwise, inherits from the previous\n");
	printf("         stage, with the first stage inheriting the global object spec.\n");
	printf("     key-start: Key start, otherwise inheriting from the global context\n");
	printf("     key-end: Key end, otherwise inheriting from the global context\n");
	printf("     read-bins: Which bins to read if the workload includes reads\n");
	printf("     write-bins: Which bins to write to if the workload includes reads\n");
	printf("     pause: max number of seconds to pause before the stage starts. Waits a random\n");
	printf("         number of seconds between 1 and the pause.\n");
	printf("     async: when true/yes, uses asynchronous commands for this stage. Default is false\n");
	printf("     random: when true/yes, randomly generates new objects for each write. Default is false\n");
	printf("     batch-size: specifies the batch size of reads for this stage. Default is 1\n");
	printf("\n");

	printf("-K --start-key <start> # Default: 0\n");
	printf("   Set the starting value of the working set of keys. If using an\n");
	printf("   'insert' workload, the start_value indicates the first value to\n");
	printf("   write. Otherwise, the start_value indicates the smallest value in\n");
	printf("   the working set of keys.\n");
	printf("\n");

	printf("-k --keys <count>     # Default: 1000000\n");
	printf("   Set the number of keys the client is dealing with. If using an\n");
	printf("   'insert' workload (detailed below), the client will write this\n");
	printf("   number of keys, starting from value = start-key. Otherwise, the\n");
	printf("   client will read and update randomly across the values between\n");
	printf("   start-key and start-key + num_keys.  start-key can be set using\n");
	printf("   '-K' or '--start-key'.\n");
	printf("\n");

	printf("-upn --udf-package-name <package_name>\n");
	printf("   The package name for the udf to be called\n");
	printf("\n");

	printf("-ufn --udf-function-name <function_name>\n");
	printf("   The name of the UDF function in the package to be called\n");
	printf("\n");

	printf("-ufv --udf-function-values <fn_vals>\n");
	printf("   The arguments to be passed to the udf when called, which are given\n");
	printf("   as an objet spec (see --object-spec).\n");
	printf("\n");

	printf("-o --object-spec describes a comma-separated bin specification\n");
	printf("   Scalar bins:\n");
	printf("      b | I<bytes> | B<size> | S<length> | D | <const> # Default: I\n");
	printf("\n");
	printf("      b) Generate a random boolean bin or value\n");
	printf("      I) Generate an integer bin or value in a specific byte range\n");
	printf("            (treat I as I4)\n");
	printf("         I1 for 0 - 255\n");
	printf("         I2 for 256 - 65535\n");
	printf("         I3 for 65536 - 2**24-1\n");
	printf("         I4 for 2**24 - 2**32-1\n");
	printf("         I5 for 2**32 - 2**40-1\n");
	printf("         I6 for 2**40 - 2**48-1\n");
	printf("         I7 for 2**48 - 2**56-1\n");
	printf("         I8 for 2**56 - 2**64-1\n");
	printf("      B) Generate a bytes bin or value with an bytearray of random bytes\n");
	printf("         B12 - generates a bytearray of 12 random bytes\n");
	printf("      S) Generate a string bin or value made of a-z{1,9} characers\n");
	printf("         S16 - a string with a 16 character length. ex: \"uir9a2mskd4poiur\"\n");
	printf("      D) Generate a Double bin or value (8 byte)\n");
	printf("      <const>) A constant value of any of the above types (besides bytes):\n");
	printf("         Const boolean: either T, true (case insensitive), F, or\n");
	printf("            false (case insensitive)\n");
	printf("         Const integer: i.e. 123, 1024, 0x3ff, -10, etc.\n");
	printf("         Const string: i.e. \"test string\", \"string\\twith\\ttabs\\n\", etc.\n");
	printf("         Const double: i.e. 123.456, 3.14f, -10.20\n");
	printf("            Note: doubles must contain a '.', but the trailing 'f' is optional.\n");
	printf("\n"); 
	printf("   Collection bins:\n");
	printf("      [] - a list\n");
	printf("         [3*I2] - ex: [312, 1651, 756]\n");
	printf("         [I2,S4,I2] - ex: [892, \"sf8h\", 8712]\n");
	printf("         [2*S12,3*I1] - ex: [\"bf90wek1a0cv\", \"pl3k2dkfi0sn\", 18, 109, 212]\n");
	printf("         [3*[I1,I1]] - ex: [[1,11],[123,221],[78,241]]\n");
	printf("\n");
	printf("      {} - a map\n");
	printf("         {5*S1:I1} - ex {\"a\":1, \"b\":2, \"d\":4, \"z\":26, \"e\":5}\n");
	printf("         {2*S1:[3*I,1],1*I1:S1} - ex {\"a\": [1,2,3], \"b\": [6,7,8], 10: \"x\"}\n");
	printf("\n");
	printf("   Example:\n");
	printf("      -o \"I2, S12, [3*I1]\" => b1: 478; b2: \"a09dfwu3ji2r\"; b3: [12, 45, 209])\n");
	printf("      -o \"123, \\\"test string\\\", [true, 3.14]\" => const, always same value\n");
	printf("\n");

	printf("   --read-bins        # Default: all bins\n");
	printf("   Specifies which bins from the object-spec to load from the database on read\n");
	printf("   transactions. Must be given as a comma-separated list of bin numbers,\n");
	printf("   starting from 1 (i.e. \"1,3,4,6\").\n");
	printf("\n");

	printf("   --write-bins       # Default: all bins\n");
	printf("   Specifies which bins from the object-spec to generate and store in the\n");
	printf("   database on write transactions. Must be given as a comma-separated list\n");
	printf("   of bin numbers, starting from 1 (i.e. \"1,3,4,6\").\n");
	printf("\n");

	printf("-R --random          # Default: static fixed bin values\n");
	printf("   Use dynamically generated random bin values instead of default static fixed bin values.\n");
	printf("\n");

	printf("-t --duration <seconds> # Default: 10 for infinite workload (RU, RUF), 0 for finite (I, DB)\n");
	printf("    Specifies the minimum amount of time the benchmark will run for.\n");
	printf("\n");

	printf("-w --workload I,<percent> | RU,<read percent> | RUF,<read percent>,<write percent> | DB  # Default: RU,50\n");
	printf("   Desired workload.\n");
	printf("   -w I,60      : Linear 'insert' workload initializing 60%% of the keys.\n");
	printf("   -w RU,80     : Random read/update workload with 80%% reads and 20%% writes.\n");
	printf("   -w RUF,20,40 : Random read/update/udf workload with 20%% reads, 40%% writes, and 60%% UDF calls.\n");
	printf("                  Note: -ufn and -upn are required in this mode.\n");
	printf("   -w DB        : Bin delete workload.\n");
	printf("\n");

	printf("-z --threads <count> # Default: 16\n");
	printf("   Load generating thread count.\n");
	printf("\n");

	printf("-g --throughput <tps> # Default: 0\n");
	printf("   Throttle transactions per second to a maximum value.\n");
	printf("   If tps is zero, do not throttle throughput.\n");
	printf("\n");

	printf("   --batch-size <size> # Default: 1\n");
	printf("   Enable batch mode with number of records to process in each batch get call.\n");
	printf("   Batch mode is valid only for RU and RUF workloads. Batch mode is disabled by default.\n");
	printf("\n");

	printf("   --compress\n");
	printf("   Enable binary data compression through the aerospike client.\n");
	printf("   Internally, this sets the compression policy to true.\n");
	printf("\n");

	printf("   --compression-ratio <ratio> # Default: 1\n");
	printf("   Sets the desired compression ratio for binary data.\n");
	printf("   Causes the benchmark tool to generate data which will roughly compress by this proportion.\n");
	printf("\n");

	printf("   --socket-timeout <ms> # Default: 30000\n");
	printf("   Read/Write socket timeout in milliseconds.\n");
	printf("\n");

	printf("   --read-socket-timeout <ms> # Default: 30000\n");
	printf("   Read socket timeout in milliseconds.\n");
	printf("\n");

	printf("   --write-socket-timeout <ms> # Default: 30000\n");
	printf("   Write socket timeout in milliseconds.\n");
	printf("\n");

	printf("-T --timeout <ms>    # Default: 0\n");
	printf("   Read/Write total timeout in milliseconds.\n");
	printf("\n");

	printf("   --read-timeout <ms> # Default: 0\n");
	printf("   Read total timeout in milliseconds.\n");
	printf("\n");

	printf("   --write-timeout <ms> # Default: 0\n");
	printf("   Write total timeout in milliseconds.\n");
	printf("\n");

	printf("   --max-retries <number> # Default: 1\n");
	printf("   Maximum number of retries before aborting the current transaction.\n");
	printf("\n");

	printf("-d --debug           # Default: debug mode is false.\n");
	printf("   Run benchmarks in debug mode.\n");
	printf("\n");

	printf("-L --latency\n");
	printf("   Enables the periodic HDR histogram summary of latency data.\n");
	printf("\n");

	printf("   --percentiles <p1>[,<p2>[,<p3>...]] # Default: \"50,90,99,99.9,99.99\".\n");
	printf("   Specifies the latency percentiles to display in the periodic latency\n");
	printf("   histogram.\n");
	printf("\n");

	printf("   --output-file  # Default: stdout\n");
	printf("   Specifies an output file to write periodic latency data, which enables\n");
	printf("   the tracking of transaction latencies in microseconds in a histogram.\n");
	printf("   Currently uses the default layout.\n");
	printf("   The file is opened in append mode.\n");
	printf("\n");

	printf("   --output-period <seconds>  # Default: 1s\n");
	printf("   Specifies the period between successive snapshots of the periodic\n");
	printf("   latency histogram.\n");
	printf("\n");

	printf("   --hdr-hist <path/to/output>  # Default: off\n");
	printf("   Enables the cumulative HDR histogram and specifies the directory to\n");
	printf("   dump the cumulative HDR histogram summary.\n");
	printf("\n");

	printf("-S --shared          # Default: false\n");
	printf("   Use shared memory cluster tending.\n");
	printf("\n");

	printf("-C --replica {master,any,sequence} # Default: master\n");
	printf("   Which replica to use for reads.\n");
	printf("\n");

	printf("-N --read-mode-ap {one,all} # Default: one\n");
	printf("   Read mode for AP (availability) namespaces.\n");
	printf("\n");

	printf("-B --read-mode-sc {session,linearize,allowReplica,allowUnavailable} # Default: session\n");
	printf("   Read mode for SC (strong consistency) namespaces.\n");
	printf("\n");

	printf("-M --commit-level {all,master} # Default: all\n");
	printf("   Write commit guarantee level.\n");
	printf("\n");

	printf("-Y --conn-pools-per-node <num>  # Default: 1\n");
	printf("   Number of connection pools per node.\n");
	printf("\n");

	printf("-D --durable-delete  # Default: durableDelete mode is false.\n");
	printf("   All transactions will set the durable-delete flag which indicates\n");
	printf("   to the server that if the transaction results in a delete, to generate\n");
	printf("   a tombstone for the deleted record.\n");
	printf("\n");

	printf("-a --async # Default: synchronous mode\n");
	printf("   Enable asynchronous mode.\n");
	printf("\n");

	printf("-c --async-max-commands <command count> # Default: 50\n");
	printf("   Maximum number of concurrent asynchronous commands that are active at any point\n");
	printf("   in time.\n");
	printf("\n");

	printf("-W --event-loops <thread count> # Default: 1\n");
	printf("   Number of event loops (or selector threads) when running in asynchronous mode.\n");
	printf("\n");

	printf("   --tls-enable         # Default: TLS disabled\n");
	printf("   Enable TLS.\n");
	printf("\n");

	printf("   --tls-name <name>    # Default: none\n");
	printf("   Set the default TLS name to use when connecting to the seed nodes.\n");
	printf("\n");

	printf("   --tls-cafile <path>\n");
	printf("   Set the TLS certificate authority file.\n");
	printf("\n");

	printf("   --tls-capath <path>\n");
	printf("   Set the TLS certificate authority directory.\n");
	printf("\n");

	printf("   --tls-protocols <protocols>\n");
	printf("   Set the TLS protocol selection criteria.\n");
	printf("\n");

	printf("   --tls-cipher-suite <suite>\n");
	printf("   Set the TLS cipher selection criteria.\n");
	printf("\n");

	printf("   --tls-crl-check\n");
	printf("   Enable CRL checking for leaf certs.\n");
	printf("\n");

	printf("   --tls-crl-check-all\n");
	printf("   Enable CRL checking for all certs.\n");
	printf("\n");

	printf("   --tls-cert-blacklist <path>\n");
	printf("   Path to a certificate blacklist file.\n");
	printf("\n");

	printf("   --tls-log-session-info\n");
	printf("   Log TLS connected session info.\n");
	printf("\n");

	printf("   --tls-keyfile <path>\n");
	printf("   Set the TLS client key file for mutual authentication.\n");
	printf("\n");

	printf("   --tls-keyfile-password=TLS_KEYFILE_PASSWORD\n");
	printf("   Password to load protected tls-keyfile.\n");
	printf("   It can be one of the following:\n");
	printf("     1) Environment varaible: 'env:<VAR>'\n");
	printf("     2) File: 'file:<PATH>'\n");
	printf("     3) String: 'PASSWORD'\n");
	printf("   Default: none\n");
	printf("   User will be prompted on command line if --tls-keyfile-password\n");
	printf("   specified and no password is given.\n");
	printf("\n");

	printf("   --tls-certfile <path>\n");
	printf("   Set the TLS client certificate chain file for mutual authentication.\n");
	printf("\n");

	printf("   --tls-login-only\n");
	printf("   Use TLS for node login only.\n");
	printf("\n");

	printf("   --auth {INTERNAL,EXTERNAL,EXTERNAL_SECURE} # Default: INTERNAL\n");
	printf("   Set authentication mode when user/password is defined.\n");
	printf("\n");
}

LOCAL_HELPER void
print_args(args_t* args)
{
	printf("hosts:                  %s\n", args->hosts);
	printf("port:                   %d\n", args->port);
	printf("user:                   %s\n", args->user);
	printf("services-alternate:     %s\n", boolstring(args->use_services_alternate));
	printf("namespace:              %s\n", args->namespace);
	printf("set:                    %s\n", args->set);
	printf("start-key:              %" PRIu64 "\n", args->start_key);
	printf("keys/records:           %" PRIu64 "\n", args->keys);

	char buf[1024];
	snprint_obj_spec(&args->obj_spec, buf, sizeof(buf));
	printf("object spec:            %s\n", buf);

	stages_print(&args->stages);

	printf("threads:                %d\n", args->transaction_worker_threads);

	printf("enable compression:     %s\n", boolstring(args->enable_compression));
	printf("compression ratio:      %f\n", args->compression_ratio);
	printf("read socket timeout:    %d ms\n", args->read_socket_timeout);
	printf("write socket timeout:   %d ms\n", args->write_socket_timeout);
	printf("read total timeout:     %d ms\n", args->read_total_timeout);
	printf("write total timeout:    %d ms\n", args->write_total_timeout);
	printf("max retries:            %d\n", args->max_retries);
	printf("debug:                  %s\n", boolstring(args->debug));

	if (args->latency) {
		printf("latency:                %d columns, shift exponent %d\n",
				args->latency_columns, args->latency_shift);

		printf("hdr histogram format:   UTC-time, seconds-running, total, "
				"min-latency, max-latency, ");
		for (uint32_t i = 0; i < args->latency_percentiles.size; i++) {
			if (i == 0) {
				printf("%g%%",
						*(double*) as_vector_get(&args->latency_percentiles, i));
			}
			else {
				printf(",%g%%",
						*(double*) as_vector_get(&args->latency_percentiles, i));
			}
		}
		printf("\n");
		printf("latency period:         %ds\n", args->histogram_period);
	}
	else {
		printf("latency:                false\n");
	}

	if (args->latency_histogram) {
		printf("latency histogram:      true\n");
		printf("histogram output file:  %s\n",
				(args->histogram_output ? args->histogram_output : "stdout"));
		printf("histogram period:       %ds\n", args->histogram_period);
	}
	else {
		printf("latency histogram:      false\n");
	}

	if (args->hdr_output) {
		printf("cumulative HDR hist:    true\n");
		printf("cumulative HDR output:  %s\n", args->hdr_output);
	}
	else {
		printf("cumulative HDR hist:    false\n");
	}

	printf("shared memory:          %s\n", boolstring(args->use_shm));

	const char* str;
	switch (args->replica) {
		case AS_POLICY_REPLICA_MASTER:
			str = "master";
			break;
		case AS_POLICY_REPLICA_ANY:
			str = "any";
			break;
		case AS_POLICY_REPLICA_SEQUENCE:
			str = "sequence";
			break;
		default:
			str = "unknown";
			break;
	}

	printf("read replica:           %s\n", str);
	printf("read mode AP:           %s\n",
			(AS_POLICY_READ_MODE_AP_ONE == args->read_mode_ap ? "one" : "all"));

	switch (args->read_mode_sc) {
		case AS_POLICY_READ_MODE_SC_SESSION:
			str = "session";
			break;
		case AS_POLICY_READ_MODE_SC_LINEARIZE:
			str = "linearize";
			break;
		case AS_POLICY_READ_MODE_SC_ALLOW_REPLICA:
			str = "allowReplica";
			break;
		case AS_POLICY_READ_MODE_SC_ALLOW_UNAVAILABLE:
			str = "allowUnavailable";
			break;
		default:
			str = "unknown";
			break;
	}

	printf("read mode SC:           %s\n", str);
	printf("write commit level:     %s\n",
			(AS_POLICY_COMMIT_LEVEL_ALL == args->write_commit_level ?
			 "all" : "master"));
	printf("conn pools per node:    %d\n", args->conn_pools_per_node);

	printf("async max commands:     %d\n", args->async_max_commands);
	printf("event loops:            %d\n", args->event_loop_capacity);

	if (args->tls.enable) {
		printf("TLS:                    enabled\n");
		printf("TLS cafile:             %s\n", args->tls.cafile);
		printf("TLS capath:             %s\n", args->tls.capath);
		printf("TLS protocols:          %s\n", args->tls.protocols);
		printf("TLS cipher suite:       %s\n", args->tls.cipher_suite);
		printf("TLS crl check:          %s\n", boolstring(args->tls.crl_check));
		printf("TLS crl check all:      %s\n", boolstring(args->tls.crl_check_all));
		printf("TLS cert blacklist:     %s\n", args->tls.cert_blacklist);
		printf("TLS log session info:   %s\n", boolstring(args->tls.log_session_info));
		printf("TLS keyfile:            %s\n", args->tls.keyfile);
		printf("TLS certfile:           %s\n", args->tls.certfile);
		printf("TLS login only:         %s\n", boolstring(args->tls.for_login_only));
	}

	char* s;
	switch (args->auth_mode) {
		case AS_AUTH_INTERNAL:
			s = "INTERNAL";
			break;
		case AS_AUTH_EXTERNAL:
			s = "EXTERNAL";
			break;
		case AS_AUTH_EXTERNAL_INSECURE:
			s = "EXTERNAL_INSECURE";
			break;
		default:
			s = "unknown";
			break;
	}
	printf("auth mode:              %s\n", s);
}

LOCAL_HELPER int
validate_args(args_t* args)
{
	if (args->start_key == ULLONG_MAX) {
		printf("Invalid start key: %" PRIu64 "\n", args->start_key);
		return 1;
	}

	if (args->keys == ULLONG_MAX) {
		printf("Invalid number of keys: %" PRIu64 "\n", args->keys);
		return 1;
	}

	if (args->transaction_worker_threads <= 0 ||
			args->transaction_worker_threads > 10000) {
		printf("Invalid number of threads: %d  Valid values: [1-10000]\n",
				args->transaction_worker_threads);
		return 1;
	}

	if (args->compression_ratio < 0.001 || args->compression_ratio > 1) {
		printf("Compression ratio must be in the range [0.001, 1]\n\n");
		return 1;
	}

	if (args->read_socket_timeout < 0) {
		printf("Invalid read socket timeout: %d  Valid values: [>= 0]\n",
				args->read_socket_timeout);
		return 1;
	}

	if (args->write_socket_timeout < 0) {
		printf("Invalid write socket timeout: %d  Valid values: [>= 0]\n",
				args->write_socket_timeout);
		return 1;
	}

	if (args->read_total_timeout < 0) {
		printf("Invalid read total timeout: %d  Valid values: [>= 0]\n",
				args->read_total_timeout);
		return 1;
	}

	if (args->write_total_timeout < 0) {
		printf("Invalid write total timeout: %d  Valid values: [>= 0]\n",
				args->write_total_timeout);
		return 1;
	}

	if (args->latency_columns < 0 || args->latency_columns > 16) {
		printf("Invalid latency columns: %d  Valid values: [1-16]\n",
				args->latency_columns);
		return 1;
	}

	if (args->latency_shift < 0 || args->latency_shift > 5) {
		printf("Invalid latency shift: %d  Valid values: [1-5]\n",
				args->latency_shift);
		return 1;
	}

	if (args->latency) {
		as_vector * perc = &args->latency_percentiles;
		if (perc->size == 0) {
			// silently fail, this can only happen if the user typed in
			// something invalid as the argument to --percentiles, which would
			// have already printed an error message
			return 1;
		}
		for (uint32_t i = 0; i < perc->size; i++) {
			double itm = *(double*) as_vector_get(perc, i);
			if (itm < 0 || itm >= 100) {
				printf("Invalid percentile \"%f\"\n", itm);
				return 1;
			}
		}
		for (uint32_t i = 1; i < perc->size; i++) {
			double l = *(double*) as_vector_get(perc, i - 1);
			double r = *(double*) as_vector_get(perc, i);
			if (l >= r) {
				printf("%f >= %f, out of order in percentile list\n", l, r);
				return 1;
			}
		}
	}

	if ((args->latency_histogram || args->latency) &&
			args->histogram_period <= 0) {
		printf("Invalid histogram period: %ds\n", args->histogram_period);
		return 1;
	}

	if (args->conn_pools_per_node <= 0 || args->conn_pools_per_node > 1000) {
		printf("Invalid conn-pools-per-node: %d  Valid values: [1-1000]\n",
				args->conn_pools_per_node);
		return 1;
	}

	if (args->async_max_commands <= 0 || args->async_max_commands > 5000) {
		printf("Invalid async-max-commands: %d  Valid values: [1-5000]\n",
				args->async_max_commands);
		return 1;
	}

	if (args->event_loop_capacity <= 0 || args->event_loop_capacity > 1000) {
		printf("Invalid event-loops: %d  Valid values: [1-1000]\n",
				args->event_loop_capacity);
		return 1;
	}
	return 0;
}

LOCAL_HELPER stage_def_t*
get_or_init_stage(args_t* args)
{
	if (args->stage_defs.stages == NULL) {
		args->stage_defs.stages =
			(struct stage_def_s*) cf_calloc(1, sizeof(struct stage_def_s));
		args->stage_defs.n_stages = 1;

		args->stage_defs.stages[0].stage_idx = 1;
		args->stage_defs.stages[0].duration = -1LU;
		args->stage_defs.stages[0].key_start = -1LU;
		args->stage_defs.stages[0].key_end = -1LU;
	}
	return &args->stage_defs.stages[0];
}

LOCAL_HELPER const char*
camelcase_to_dash(const char* camel_str)
{
	static char tmp[1024];
	uint32_t j = 0;
	for (uint32_t i = 0; camel_str[i] != '\0' && j < sizeof(tmp)-2; i++) {
		if ('A' <= camel_str[i] && camel_str[i] <= 'Z') {
			tmp[j] = '-';
			tmp[j + 1] = camel_str[i] + ('a' - 'A');
			j += 2;
		}
		else {
			tmp[j] = camel_str[i];
			j++;
		}
	}
	tmp[j] = '\0';
	return tmp;
}

LOCAL_HELPER int
set_args(int argc, char * const* argv, args_t* args)
{
	int option_index = 0;
	int c;

	while ((c = getopt_long_only(argc, argv, short_options, long_options,
					&option_index)) != -1) {

		if (c & WARN_MSG) {
			const char* opt_name = long_options[option_index].name;
			fprintf(stderr, "Warning: camelcase argument \"--%s\" is now "
					"deprecated. Use \"--%s\" instead\n",
					opt_name, camelcase_to_dash(opt_name));
		}

		switch (c & ~WARN_MSG) {
			case '9':
				print_usage(argv[0]);
				return -1;
			case 'h': {
				free(args->hosts);
				args->hosts = strdup(optarg);
				break;
			}

			case 'p':
				args->port = atoi(optarg);
				break;

			case 'U':
				args->user = optarg;
				break;

			case 'P':
				as_password_acquire(args->password, optarg, AS_PASSWORD_SIZE);
				break;

			case '*':
				args->use_services_alternate = true;
				break;

			case 'n':
				args->namespace = optarg;
				break;

			case 's':
				args->set = optarg;
				break;

			case 'b':
				args->bin_name = strdup(optarg);
				break;

			case 'K':
				args->start_key = strtoull(optarg, NULL, 10);
				break;

			case 'k':
				args->keys = strtoull(optarg, NULL, 10);
				break;

			case ':': {
				if (args->workload_stages_file != NULL) {
					fprintf(stderr, "Cannot specify both a workload stages "
							"file and the udf package name flag\n");
					return -1;
				}

				struct stage_def_s* stage = get_or_init_stage(args);

				if (strlen(optarg) > sizeof(as_udf_module_name)) {
					fprintf(stderr, "UDF package name \"%s\" too long (max "
							"length is %lu characters)\n",
							optarg, sizeof(as_udf_module_name));
					return -1;
				}
				stage->udf_spec.udf_package_name = strdup(optarg);
				break;
			}

			case ';': {
				if (args->workload_stages_file != NULL) {
					fprintf(stderr, "Cannot specify both a workload stages "
							"file and the udf function name flag\n");
					return -1;
				}

				struct stage_def_s* stage = get_or_init_stage(args);

				if (strlen(optarg) > sizeof(as_udf_function_name)) {
					fprintf(stderr, "UDF function name \"%s\" too long (max "
							"length is %lu characters)\n",
							optarg, sizeof(as_udf_function_name));
					return -1;
				}
				stage->udf_spec.udf_fn_name = strdup(optarg);
				break;
			}

			case '"': {
				if (args->workload_stages_file != NULL) {
					fprintf(stderr, "Cannot specify both a workload stages "
							"file and the udf function args flag\n");
					return -1;
				}
				struct stage_def_s* stage = get_or_init_stage(args);
				stage->udf_spec.udf_fn_args = strdup(optarg);
				break;
			}

			case 'o': {
				// free the default obj_spec before making a new one
				obj_spec_free(&args->obj_spec);
				int ret = obj_spec_parse(&args->obj_spec, optarg);
				if (ret != 0) {
					return ret;
				}
				break;
			}

			case 'R':
				if (args->workload_stages_file != NULL) {
					fprintf(stderr, "Cannot specify both a workload stages "
							"file and the random flag\n");
					return -1;
				}
				struct stage_def_s* stage = get_or_init_stage(args);
				stage->random = true;
				break;

			case 't': {
				if (args->workload_stages_file != NULL) {
					fprintf(stderr, "Cannot specify both a workload stages "
							"file and the duration flag\n");
					return -1;
				}
				struct stage_def_s* stage = get_or_init_stage(args);
				char* endptr;
				stage->duration = strtoull(optarg, &endptr, 10);
				if (*optarg == '\0' || *endptr != '\0') {
					printf("string \"%s\" is not a decimal point number\n",
							optarg);
					return -1;
				}
				break;
			}

			case 'w': {
				if (args->workload_stages_file != NULL) {
					fprintf(stderr, "Cannot specify both a workload stages "
							"file and the workload flag\n");
					return -1;
				}
				struct stage_def_s* stage = get_or_init_stage(args);
				stage->workload_str = strdup(optarg);
				break;
			}

			case '.': {
				if (args->stage_defs.stages != NULL) {
					fprintf(stderr, "Cannot specify both a workload stages "
							"file and the workload flag\n");
					return -1;
				}
				args->workload_stages_file = strdup(optarg);
				break;
			}

			case '+': {
				if (args->workload_stages_file != NULL) {
					fprintf(stderr, "Cannot specify both a workload stages "
							"file and the read-bins flag\n");
					return -1;
				}
				struct stage_def_s* stage = get_or_init_stage(args);
				stage->read_bins_str = strdup(optarg);
				break;
			}

			case '-': {
				if (args->workload_stages_file != NULL) {
					fprintf(stderr, "Cannot specify both a workload stages "
							"file and the write-bins flag\n");
					return -1;
				}
				struct stage_def_s* stage = get_or_init_stage(args);
				stage->write_bins_str = strdup(optarg);
				break;
			}

			case 'z':
				args->transaction_worker_threads = atoi(optarg);
				break;

			case 'g': {
				if (args->workload_stages_file != NULL) {
					fprintf(stderr, "Cannot specify both a workload stages "
							"file and the throughput flag\n");
					return -1;
				}
				struct stage_def_s* stage = get_or_init_stage(args);
				stage->tps = atoi(optarg);
				break;
			}

			case '0': {
				if (args->workload_stages_file != NULL) {
					fprintf(stderr, "Cannot specify both a workload stages "
							"file and the workload flag\n");
					return -1;
				}
				struct stage_def_s* stage = get_or_init_stage(args);
				stage->batch_size = atoi(optarg);
				break;
			}

			case '4':
				args->enable_compression = true;
				break;

			case '5':
				args->compression_ratio = (float) atof(optarg);
				break;

			case '1':
				args->read_socket_timeout = atoi(optarg);
				args->write_socket_timeout = args->read_socket_timeout;
				break;

			case '2':
				args->read_socket_timeout = atoi(optarg);
				break;

			case '3':
				args->write_socket_timeout = atoi(optarg);
				break;

			case 'T':
				args->read_total_timeout = atoi(optarg);
				args->write_total_timeout = args->read_total_timeout;
				break;

			case 'X':
				args->read_total_timeout = atoi(optarg);
				break;

			case 'V':
				args->write_total_timeout = atoi(optarg);
				break;

			case 'r':
				args->max_retries = atoi(optarg);
				break;

			case 'd':
				args->debug = true;
				break;

			case 'L':
				args->latency = true;
				break;

			case '8':
				; // parse percentiles as a comma-separated list
				as_vector * perc = &args->latency_percentiles;
				as_vector_clear(perc);
				char* _tmp = strdup(optarg);
				char* tmp = _tmp;

				char prior;
				char* next_comma;
				do {
					next_comma = strchrnul(tmp, ',');
					prior = *next_comma;
					*next_comma = '\0';
					char* endptr;
					double val = strtod(tmp, &endptr);
					if (*tmp == '\0' || *endptr != '\0') {
						printf("string \"%s\" is not a floating point number\n",
								tmp);
						// so that when validate_args is called, it will fail
						as_vector_clear(perc);
						break;
					}
					as_vector_append(perc, (void*) &val);

					tmp = next_comma + 1;
				} while (prior != '\0');
				free(_tmp);
				break;

			case '6':
				args->latency_histogram = true;
				if (strcmp(optarg, "stdout") != 0) {
					args->histogram_output = strdup(optarg);
				}
				break;

			case '7':
				args->histogram_period = atoi(optarg);
				break;

			case '/':
				args->hdr_output = strdup(optarg);
				break;

			case 'S':
				args->use_shm = true;
				break;

			case 'C':
				if (strcmp(optarg, "master") == 0) {
					args->replica = AS_POLICY_REPLICA_MASTER;
				}
				else if (strcmp(optarg, "any") == 0) {
					args->replica = AS_POLICY_REPLICA_ANY;
				}
				else if (strcmp(optarg, "sequence") == 0) {
					args->replica = AS_POLICY_REPLICA_SEQUENCE;
				}
				else {
					printf("replica must be master | any | sequence\n");
					return 1;
				}
				break;

			case 'N':
				if (strcmp(optarg, "one") == 0) {
					args->read_mode_ap = AS_POLICY_READ_MODE_AP_ONE;
				}
				else if (strcmp(optarg, "all") == 0) {
					args->read_mode_ap = AS_POLICY_READ_MODE_AP_ALL;
				}
				else {
					printf("read-mode-ap must be one or all\n");
					return 1;
				}
				break;

			case 'B':
				if (strcmp(optarg, "session") == 0) {
					args->read_mode_sc = AS_POLICY_READ_MODE_SC_SESSION;
				}
				else if (strcmp(optarg, "linearize") == 0) {
					args->read_mode_sc = AS_POLICY_READ_MODE_SC_LINEARIZE;
				}
				else if (strcmp(optarg, "allowReplica") == 0) {
					args->read_mode_sc = AS_POLICY_READ_MODE_SC_ALLOW_REPLICA;
				}
				else if (strcmp(optarg, "allowUnavailable") == 0) {
					args->read_mode_sc = AS_POLICY_READ_MODE_SC_ALLOW_UNAVAILABLE;
				}
				else {
					printf("read-mode-sc must be session | linearize | "
							"allowReplica | allowUnavailable\n");
					return 1;
				}
				break;

			case 'M':
				if (strcmp(optarg, "all") == 0) {
					args->write_commit_level = AS_POLICY_COMMIT_LEVEL_ALL;
				}
				else if (strcmp(optarg, "master") == 0) {
					args->write_commit_level = AS_POLICY_COMMIT_LEVEL_MASTER;
				}
				else {
					printf("commitLevel be all or master\n");
					return 1;
				}
				break;

			case 'Y':
				args->conn_pools_per_node = atoi(optarg);
				break;

			case 'D':
				args->durable_deletes = true;
				break;

			case 'a': {
				if (args->workload_stages_file != NULL) {
					fprintf(stderr, "Cannot specify both a workload stages "
							"file and the async flag\n");
					return -1;
				}
				struct stage_def_s* stage = get_or_init_stage(args);
				stage->async = true;
				break;
			}

			case 'c':
				args->async_max_commands = atoi(optarg);
				break;

			case 'W':
				args->event_loop_capacity = atoi(optarg);
				break;

			case TLS_OPT_ENABLE:
				args->tls.enable = true;
				break;

			case TLS_OPT_NAME:
				args->tls_name = strdup(optarg);
				break;

			case TLS_OPT_CA_FILE:
				args->tls.cafile = strdup(optarg);
				break;

			case TLS_OPT_CA_PATH:
				args->tls.capath = strdup(optarg);
				break;

			case TLS_OPT_PROTOCOLS:
				args->tls.protocols = strdup(optarg);
				break;

			case TLS_OPT_CIPHER_SUITE:
				args->tls.cipher_suite = strdup(optarg);
				break;

			case TLS_OPT_CRL_CHECK:
				args->tls.crl_check = true;
				break;

			case TLS_OPT_CRL_CHECK_ALL:
				args->tls.crl_check_all = true;
				break;

			case TLS_OPT_CERT_BLACK_LIST:
				args->tls.cert_blacklist = strdup(optarg);
				break;

			case TLS_OPT_LOG_SESSION_INFO:
				args->tls.log_session_info = true;
				break;

			case TLS_OPT_KEY_FILE:
				args->tls.keyfile = strdup(optarg);
				break;

			case TLS_OPT_KEY_FILE_PASSWORD:
				if (optarg == NULL) {
					// no password given
					args->tls.keyfile_pw = strdup("");
				}
				else {
					args->tls.keyfile_pw = strdup(optarg);
				}
				break;

			case TLS_OPT_CERT_FILE:
				args->tls.certfile = strdup(optarg);
				break;

			case TLS_OPT_LOGIN_ONLY:
				args->tls.for_login_only = true;
				break;

			case 'e':
				if (!as_auth_mode_from_string(&args->auth_mode, optarg)) {
					printf("invalid authentication mode: %s\n", optarg);
					return 1;
				}
				break;

			default:
				fprintf(stderr, "Unknown parameter '%c'\n", c);
			case '?':
				return 1;
		}
	}

	if (args->tls.keyfile && args->tls.keyfile_pw) {
		char* old_keyfile_pw = args->tls.keyfile_pw;

		if (strcmp(args->tls.keyfile_pw, "") == 0) {
			args->tls.keyfile_pw = getpass("Enter TLS-Keyfile Password: ");
		}

		if (!tls_read_password(args->tls.keyfile_pw, &args->tls.keyfile_pw)) {
			return 1;
		}
		cf_free(old_keyfile_pw);
	}

	return validate_args(args);
}

LOCAL_HELPER void
_load_defaults(args_t* args)
{
	args->hosts = strdup("127.0.0.1");
	args->port = 3000;
	args->user = 0;
	args->password[0] = 0;
	args->use_services_alternate = false;
	args->namespace = "test";
	args->set = "testset";
	args->bin_name = strdup("testbin");
	args->start_key = 1;
	args->keys = 1000000;
	memset(&args->stage_defs, 0, sizeof(struct stage_defs_s));
	args->workload_stages_file = NULL;
	obj_spec_parse(&args->obj_spec, "I");
	args->transaction_worker_threads = 16;
	args->enable_compression = false;
	args->compression_ratio = 1.f;
	args->read_socket_timeout = AS_POLICY_SOCKET_TIMEOUT_DEFAULT;
	args->write_socket_timeout = AS_POLICY_SOCKET_TIMEOUT_DEFAULT;
	args->read_total_timeout = AS_POLICY_TOTAL_TIMEOUT_DEFAULT;
	args->write_total_timeout = AS_POLICY_TOTAL_TIMEOUT_DEFAULT;
	args->max_retries = 1;
	args->debug = false;
	args->latency = false;
	args->latency_columns = 4;
	args->latency_shift = 3;
	as_vector_init(&args->latency_percentiles, sizeof(double), 5);
	args->latency_histogram = false;
	args->histogram_output = NULL;
	args->histogram_period = 1;
	args->hdr_output = NULL;
	args->use_shm = false;
	args->replica = AS_POLICY_REPLICA_SEQUENCE;
	args->read_mode_ap = AS_POLICY_READ_MODE_AP_ONE;
	args->read_mode_sc = AS_POLICY_READ_MODE_SC_SESSION;
	args->write_commit_level = AS_POLICY_COMMIT_LEVEL_ALL;
	args->durable_deletes = false;
	args->conn_pools_per_node = 1;
	args->async_max_commands = 50;
	args->event_loop_capacity = 1;
	memset(&args->tls, 0, sizeof(as_config_tls));
	args->tls_name = NULL;
	args->auth_mode = AS_AUTH_INTERNAL;

	double p1 = 50.,
		   p2 = 90.,
		   p3 = 99.,
		   p4 = 99.9,
		   p5 = 99.99;
	as_vector_append(&args->latency_percentiles, &p1);
	as_vector_append(&args->latency_percentiles, &p2);
	as_vector_append(&args->latency_percentiles, &p3);
	as_vector_append(&args->latency_percentiles, &p4);
	as_vector_append(&args->latency_percentiles, &p5);
}

LOCAL_HELPER int
_load_defaults_post(args_t* args)
{
	int res = 0;

	if (args->workload_stages_file != NULL) {
		res = parse_workload_config_file(args->workload_stages_file,
				&args->stages, args);
	}
	else {
		struct stage_def_s* stage = get_or_init_stage(args);

		stage->desc = strdup("default config (specify your own with "
				"--workload-stages)");

		if (stage->workload_str == NULL) {
			stage->workload_str = strdup("RU");
		}

		res = stages_set_defaults_and_parse(&args->stages, &args->stage_defs,
				args);
		args->stages.valid = true;
		free_stage_defs(&args->stage_defs);
	}

	return res;
}

LOCAL_HELPER void
_free_args(args_t* args)
{
	obj_spec_free(&args->obj_spec);
	if (args->workload_stages_file) {
		cf_free(args->workload_stages_file);
		free_workload_config(&args->stages);
	}
	cf_free(args->hdr_output);
	cf_free(args->histogram_output);
	cf_free(args->bin_name);
	as_vector_destroy(&args->latency_percentiles);
	cf_free(args->tls_name);
	cf_free(args->hosts);
}

