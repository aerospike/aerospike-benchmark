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
#include <benchmark.h>
#include <common.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#undef _UNICODE  // Use ASCII getopt version on windows.
#endif
#include <getopt.h>

static const char* short_options = "h:p:U:P::n:s:K:k:b:o:Rt:w:z:g:T:dL:SC:N:B:M:Y:Dac:W:u";

static struct option long_options[] = {
	{"help",                 no_argument,       0, '9'},
	{"hosts",                required_argument, 0, 'h'},
	{"port",                 required_argument, 0, 'p'},
	{"user",                 required_argument, 0, 'U'},
	{"password",             optional_argument, 0, 'P'},
	{"namespace",            required_argument, 0, 'n'},
	{"set",                  required_argument, 0, 's'},
	{"startKey",             required_argument, 0, 'K'},
	{"keys",                 required_argument, 0, 'k'},
	{"objectSpec",           required_argument, 0, 'o'},
	{"random",               no_argument,       0, 'R'},
	{"duration",             required_argument, 0, 't'},
	{"workload",             required_argument, 0, 'w'},
	{"workloadStages",       required_argument, 0, '.'},
	{"threads",              required_argument, 0, 'z'},
	{"throughput",           required_argument, 0, 'g'},
	{"batchSize",            required_argument, 0, '0'},
	{"compress",             no_argument,       0, '4'},
	{"compressionRatio",     required_argument, 0, '5'},
	{"socketTimeout",        required_argument, 0, '1'},
	{"readSocketTimeout",    required_argument, 0, '2'},
	{"writeSocketTimeout",   required_argument, 0, '3'},
	{"timeout",              required_argument, 0, 'T'},
	{"readTimeout",          required_argument, 0, 'X'},
	{"writeTimeout",         required_argument, 0, 'V'},
	{"maxRetries",           required_argument, 0, 'r'},
	{"debug",                no_argument,       0, 'd'},
	{"latency",              required_argument, 0, 'L'},
	{"percentiles",          required_argument, 0, '8'},
	{"outputFile",           required_argument, 0, '6'},
	{"outputPeriod",         required_argument, 0, '7'},
	{"hdrHist",              required_argument, 0, '/'},
	{"shared",               no_argument,       0, 'S'},
	{"replica",              required_argument, 0, 'C'},
	{"readModeAP",           required_argument, 0, 'N'},
	{"readModeSC",           required_argument, 0, 'B'},
	{"commitLevel",          required_argument, 0, 'M'},
	{"connPoolsPerNode",     required_argument, 0, 'Y'},
	{"durableDelete",        no_argument,       0, 'D'},
	{"async",                no_argument,       0, 'a'},
	{"asyncMaxCommands",     required_argument, 0, 'c'},
	{"eventLoops",           required_argument, 0, 'W'},
	{"tlsEnable",            no_argument,       0, 'A'},
	{"tlsCaFile",            required_argument, 0, 'E'},
	{"tlsCaPath",            required_argument, 0, 'F'},
	{"tlsProtocols",         required_argument, 0, 'G'},
	{"tlsCipherSuite",       required_argument, 0, 'H'},
	{"tlsCrlCheck",          no_argument,       0, 'I'},
	{"tlsCrlCheckAll",       no_argument,       0, 'J'},
	{"tlsCertBlackList",     required_argument, 0, 'O'},
	{"tlsLogSessionInfo",    no_argument,       0, 'Q'},
	{"tlsKeyFile",           required_argument, 0, 'Z'},
	{"tlsCertFile",          required_argument, 0, 'y'},
	{"tlsLoginOnly",         no_argument,       0, 'f'},
	{"auth",                 required_argument, 0, 'e'},
	{"usage",                no_argument,       0, 'u'},
	{0, 0, 0, 0}
};

static void
print_usage(const char* program)
{
	blog_line("Usage: %s <options>", program);
	blog_line("options:");
	blog_line("");

	blog_line("   --help");
	blog_line("   Prints this message");
	blog_line("");

	blog_line("-h --hosts <host1>[:<tlsname1>][:<port1>],...  # Default: localhost");
	blog_line("   Server seed hostnames or IP addresses.");
	blog_line("   The tlsname is only used when connecting with a secure TLS enabled server.");
	blog_line("   If the port is not specified, the default port is used. Examples:");
	blog_line("");
	blog_line("   host1");
	blog_line("   host1:3000,host2:3000");
	blog_line("   192.168.1.10:cert1:3000,192.168.1.20:cert2:3000");
	blog_line("");

	blog_line("-p --port <port> # Default: 3000");
	blog_line("   Server default port.");
	blog_line("");

	blog_line("-U --user <user name> # Default: empty");
	blog_line("   User name for Aerospike servers that require authentication.");
	blog_line("");

	blog_line("-P[<password>]  # Default: empty");
	blog_line("   User's password for Aerospike servers that require authentication.");
	blog_line("   If -P is set, the actual password if optional. If the password is not given,");
	blog_line("   the user will be prompted on the command line.");
	blog_line("   If the password is given, it must be provided directly after -P with no");
	blog_line("   intervening space (ie. -Pmypass).");
	blog_line("");

	blog_line("-n --namespace <ns>   # Default: test");
	blog_line("   Aerospike namespace.");
	blog_line("");

	blog_line("-s --set <set name>   # Default: testset");
	blog_line("   Aerospike set name.");
	blog_line("");

	blog_line("-K --startKey <start> # Default: 0");
	blog_line("   Set the starting value of the working set of keys. If using an");
	blog_line("   'insert' workload, the start_value indicates the first value to");
	blog_line("   write. Otherwise, the start_value indicates the smallest value in");
	blog_line("   the working set of keys.");
	blog_line("");

	blog_line("-k --keys <count>     # Default: 1000000");
	blog_line("   Set the number of keys the client is dealing with. If using an");
	blog_line("   'insert' workload (detailed below), the client will write this");
	blog_line("   number of keys, starting from value = startKey. Otherwise, the");
	blog_line("   client will read and update randomly across the values between");
	blog_line("   startKey and startKey + num_keys.  startKey can be set using");
	blog_line("   '-K' or '--startKey'.");
	blog_line("");

	blog_line("-o --objectSpec describes a comma-separated bin specification");
	blog_line("   Scalar bins:");
	blog_line("      I<bytes> | B<size> | S<length> | D # Default: I");
	blog_line("");
	blog_line("      I) Generate an integer bin or value in a specific byte range");
	blog_line("            (treat I as I4)");
	blog_line("         I1 for 0 - 255");
	blog_line("         I2 for 256 - 65535");
	blog_line("         I3 for 65536 - 2^24-1");
	blog_line("         I4 for 2^24 - 2^32-1");
	blog_line("         I5 for 2^32 - 2^40-1");
	blog_line("         I6 for 2^40 - 2^48-1");
	blog_line("         I7 for 2^48 - 2^56-1");
	blog_line("         I8 for 2^56 - 2^64-1");
	blog_line("      B) Generate a bytes bin or value with an bytearray of random bytes");
	blog_line("         B12 - generates a bytearray of 12 random bytes");
	blog_line("      S) Generate a string bin or value made of space-separated a-z{1,9} words");
	blog_line("         S16 - a string with a 16 character length. ex: \"uir a mskd poiur\"");
	blog_line("      D) Generate a Double bin or value (8 byte)");
	blog_line("");
	blog_line("   Collection bins:");
	blog_line("      [] - a list");
	blog_line("         [3*I2] - ex: [312, 1651, 756]");
	blog_line("         [I2, S4, I2] - ex: [892, \"sf b\", 8712]");
	blog_line("         [2*S12, 3*I1] - ex: [\"be lkr sqp s\", \"ndvi qd r fr\", 18, 109, 212]");
	blog_line("         [3*[I1, I1]] - ex: [[1,11],[123,321],[78,241]]");
	blog_line("");
	blog_line("      {} - a map");
	blog_line("         {5*S1:I1} - ex {\"a\":1, \"b\":2, \"d\":4, \"z\":26, \"e\":5}");
	blog_line("         {2*S1:[3*I:1]} - ex {\"a\": [1,2,3], \"b\": [6,7,8]}");
	blog_line("");
	blog_line("   Example:");
	blog_line("      -o I2,S12,[3*I1] => b1: 478; b2: \"dfoiu weop g\"; b3: [12, 45, 209])");
	blog_line("");

	blog_line("-R --random          # Default: static fixed bin values");
	blog_line("   Use dynamically generated random bin values instead of default static fixed bin values.");
	blog_line("");

	blog_line("-t --duration        # Default: 10s (for random read/write workload)");
	blog_line("    Specifies the minimum amount of time the benchmark will run for.");
	blog_line("");

	blog_line("-w --workload I,<percent> | RU,<read percent> | DB  # Default: RU,50");
	blog_line("   Desired workload.");
	blog_line("   -w I,60  : Linear 'insert' workload initializing 60%% of the keys.");
	blog_line("   -w RU,80 : Random read/update workload with 80%% reads and 20%% writes.");
	blog_line("   -w DB    : Bin delete workload.");
	blog_line("");

	blog_line("-z --threads <count> # Default: 16");
	blog_line("   Load generating thread count.");
	blog_line("");

	blog_line("-g --throughput <tps> # Default: 0");
	blog_line("   Throttle transactions per second to a maximum value.");
	blog_line("   If tps is zero, do not throttle throughput.");
	blog_line("   Used in read/write mode only.");
	blog_line("");

	blog_line("   --batchSize <size> # Default: 0");
	blog_line("   Enable batch mode with number of records to process in each batch get call.");
	blog_line("   Batch mode is valid only for RU (read update) workloads. Batch mode is disabled by default.");
	blog_line("");

	blog_line("   --compress");
	blog_line("   Enable binary data compression through the aerospike client.");
	blog_line("   Internally, this sets the compression policy to true.");
	blog_line("");

	blog_line("   --compressionRatio <ratio> # Default: 1");
	blog_line("   Sets the desired compression ratio for binary data.");
	blog_line("   Causes the benchmark tool to generate data which will roughly compress by this proportion.");
	blog_line("");

	blog_line("   --socketTimeout <ms> # Default: 30000");
	blog_line("   Read/Write socket timeout in milliseconds.");
	blog_line("");

	blog_line("   --readSocketTimeout <ms> # Default: 30000");
	blog_line("   Read socket timeout in milliseconds.");
	blog_line("");

	blog_line("   --writeSocketTimeout <ms> # Default: 30000");
	blog_line("   Write socket timeout in milliseconds.");
	blog_line("");

	blog_line("-T --timeout <ms>    # Default: 0");
	blog_line("   Read/Write total timeout in milliseconds.");
	blog_line("");

	blog_line("   --readTimeout <ms> # Default: 0");
	blog_line("   Read total timeout in milliseconds.");
	blog_line("");

	blog_line("   --writeTimeout <ms> # Default: 0");
	blog_line("   Write total timeout in milliseconds.");
	blog_line("");

	blog_line("   --maxRetries <number> # Default: 1");
	blog_line("   Maximum number of retries before aborting the current transaction.");
	blog_line("");

	blog_line("-d --debug           # Default: debug mode is false.");
	blog_line("   Run benchmarks in debug mode.");
	blog_line("");

	blog_line("-L --latency <columns>,<shift> # Default: latency display is off.");
	blog_line("   Show transaction latency percentages using elapsed time ranges.");
	blog_line("   <columns> Number of elapsed time ranges.");
	blog_line("   <shift>   Power of 2 multiple between each range starting at column 3.");
	blog_line("");

	blog_line("   --percentiles <p1>[,<p2>[,<p3>...]] # Default: \"50,90,99,99.9,99.99\".");
	blog_line("   Specified the latency percentiles to display in the cumulative latency");
	blog_line("   histogram.");
	blog_line("");

	blog_line("   --outputFile  # Default: stdout");
	blog_line("   Specifies an output file to write periodic latency data, which enables");
	blog_line("   the tracking of transaction latencies in microseconds in a histogram.");
	blog_line("   Currently uses the default layout. Add documentation here later.");
	blog_line("   The file is opened in append mode.");
	blog_line("");

	blog_line("   --outputPeriod  # Default: 1s");
	blog_line("   Specifies the period between successive snapshots of the periodic");
	blog_line("   latency histogram.");
	blog_line("");

	blog_line("   --hdrHist=<path/to/output>  # Default: off");
	blog_line("   Enables the cumulative HDR histogram and specifies the directory to");
	blog_line("   dump the cumulative HDR histogram summary.");
	blog_line("");

	blog_line("-S --shared          # Default: false");
	blog_line("   Use shared memory cluster tending.");
	blog_line("");

	blog_line("-C --replica {master,any,sequence} # Default: master");
	blog_line("   Which replica to use for reads.");
	blog_line("");

	blog_line("-N --readModeAP {one,all} # Default: one");
	blog_line("   Read mode for AP (availability) namespaces.");
	blog_line("");

	blog_line("-B --readModeSC {session,linearize,allowReplica,allowUnavailable} # Default: session");
	blog_line("   Read mode for SC (strong consistency) namespaces.");
	blog_line("");

	blog_line("-M --commitLevel {all,master} # Default: all");
	blog_line("   Write commit guarantee level.");
	blog_line("");

	blog_line("-Y --connPoolsPerNode <num>  # Default: 1");
	blog_line("   Number of connection pools per node.");
	blog_line("");

	blog_line("-D --durableDelete  # Default: durableDelete mode is false.");
	blog_line("   All transactions will set the durable-delete flag which indicates");
	blog_line("   to the server that if the transaction results in a delete, to generate");
	blog_line("   a tombstone for the deleted record.");
	blog_line("");

	blog_line("-a --async # Default: synchronous mode");
	blog_line("   Enable asynchronous mode.");
	blog_line("");

	blog_line("-c --asyncMaxCommands <command count> # Default: 50");
	blog_line("   Maximum number of concurrent asynchronous commands that are active at any point");
	blog_line("   in time.");
	blog_line("");

	blog_line("-W --eventLoops <thread count> # Default: 1");
	blog_line("   Number of event loops (or selector threads) when running in asynchronous mode.");
	blog_line("");

	blog_line("   --tlsEnable         # Default: TLS disabled");
	blog_line("   Enable TLS.");
	blog_line("");

	blog_line("   --tlsCaFile <path>");
	blog_line("   Set the TLS certificate authority file.");
	blog_line("");

	blog_line("   --tlsCaPath <path>");
	blog_line("   Set the TLS certificate authority directory.");
	blog_line("");

	blog_line("   --tlsProtocols <protocols>");
	blog_line("   Set the TLS protocol selection criteria.");
	blog_line("");

	blog_line("   --tlsCipherSuite <suite>");
	blog_line("   Set the TLS cipher selection criteria.");
	blog_line("");

	blog_line("   --tlsCrlCheck");
	blog_line("   Enable CRL checking for leaf certs.");
	blog_line("");

	blog_line("   --tlsCrlCheckAll");
	blog_line("   Enable CRL checking for all certs.");
	blog_line("");

	blog_line("   --tlsCertBlackList <path>");
	blog_line("   Path to a certificate blacklist file.");
	blog_line("");

	blog_line("   --tlsLogSessionInfo");
	blog_line("   Log TLS connected session info.");
	blog_line("");

	blog_line("   --tlsKeyFile <path>");
	blog_line("   Set the TLS client key file for mutual authentication.");
	blog_line("");

	blog_line("   --tlsCertFile <path>");
	blog_line("   Set the TLS client certificate chain file for mutual authentication.");
	blog_line("");

	blog_line("   --tlsLoginOnly");
	blog_line("   Use TLS for node login only.");
	blog_line("");

	blog_line("   --auth {INTERNAL,EXTERNAL,EXTERNAL_SECURE} # Default: INTERNAL");
	blog_line("   Set authentication mode when user/password is defined.");
	blog_line("");

	blog_line("-u --usage           # Default: usage not printed.");
	blog_line("   Display program usage.");
	blog_line("");
}

static const char*
boolstring(bool val)
{
	if (val) {
		return "true";
	}
	else {
		return "false";
	}
}

static void
print_args(arguments* args)
{
	blog_line("hosts:                  %s", args->hosts);
	blog_line("port:                   %d", args->port);
	blog_line("user:                   %s", args->user);
	blog_line("namespace:              %s", args->namespace);
	blog_line("set:                    %s", args->set);
	blog_line("startKey:               %" PRIu64, args->start_key);
	blog_line("keys/records:           %" PRIu64, args->keys);

	char buf[1024];
	snprint_obj_spec(&args->obj_spec, buf, sizeof(buf));
	blog_line("object spec:            %s", buf);

	stages_print(&args->stages);

	blog_line("random values:          %s", boolstring(args->random));

	blog_line("threads:                %d", args->transaction_worker_threads);

	blog_line("batch size:             %d", args->batch_size);
	blog_line("enable compression:     %s", boolstring(args->enable_compression));
	blog_line("compression ratio:      %f", args->compression_ratio);
	blog_line("read socket timeout:    %d ms", args->read_socket_timeout);
	blog_line("write socket timeout:   %d ms", args->write_socket_timeout);
	blog_line("read total timeout:     %d ms", args->read_total_timeout);
	blog_line("write total timeout:    %d ms", args->write_total_timeout);
	blog_line("max retries:            %d", args->max_retries);
	blog_line("debug:                  %s", boolstring(args->debug));

	if (args->latency) {
		blog_line("latency:                %d columns, shift exponent %d",
				args->latency_columns, args->latency_shift);

		blog("hdr histogram format:   UTC-time, seconds-running, total, "
				"min-latency, max-latency, ");
		for (uint32_t i = 0; i < args->latency_percentiles.size; i++) {
			if (i == 0) {
				blog("%g%%",
						*(double*) as_vector_get(&args->latency_percentiles, i));
			}
			else {
				blog(",%g%%",
						*(double*) as_vector_get(&args->latency_percentiles, i));
			}
		}
		blog_line("");
	}
	else {
		blog_line("latency:                false");
	}

	if (args->latency_histogram) {
		blog_line("latency histogram:      true");
		blog_line("histogram output file:  %s",
				(args->histogram_output ? args->histogram_output : "stdout"));
		blog_line("histogram period:       %ds", args->histogram_period);
	}
	else {
		blog_line("latency histogram:      false");
	}

	if (args->hdr_output) {
		blog_line("cumulative HDR hist:    true");
		blog_line("cumulative HDR output:  %s", args->hdr_output);
	}
	else {
		blog_line("cumulative HDR hist:    false");
	}

	blog_line("shared memory:          %s", boolstring(args->use_shm));

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

	blog_line("read replica:           %s", str);
	blog_line("read mode AP:           %s",
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

	blog_line("read mode SC:           %s", str);
	blog_line("write commit level:     %s",
			(AS_POLICY_COMMIT_LEVEL_ALL == args->write_commit_level ?
			 "all" : "master"));
	blog_line("conn pools per node:    %d", args->conn_pools_per_node);
	blog_line("asynchronous mode:      %s", args->async ? "on" : "off");

	if (args->async) {
		blog_line("async max commands:     %d", args->async_max_commands);
		blog_line("event loops:            %d", args->event_loop_capacity);
	}

	if (args->tls.enable) {
		blog_line("TLS:                    enabled");
		blog_line("TLS cafile:             %s", args->tls.cafile);
		blog_line("TLS capath:             %s", args->tls.capath);
		blog_line("TLS protocols:          %s", args->tls.protocols);
		blog_line("TLS cipher suite:       %s", args->tls.cipher_suite);
		blog_line("TLS crl check:          %s", boolstring(args->tls.crl_check));
		blog_line("TLS crl check all:      %s", boolstring(args->tls.crl_check_all));
		blog_line("TLS cert blacklist:     %s", args->tls.cert_blacklist);
		blog_line("TLS log session info:   %s", boolstring(args->tls.log_session_info));
		blog_line("TLS keyfile:            %s", args->tls.keyfile);
		blog_line("TLS certfile:           %s", args->tls.certfile);
		blog_line("TLS login only:         %s", boolstring(args->tls.for_login_only));
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
	blog_line("auth mode:              %s", s);
}

static int
validate_args(arguments* args)
{
	if (args->start_key == ULLONG_MAX) {
		blog_line("Invalid start key: %" PRIu64, args->start_key);
		return 1;
	}

	if (args->keys == ULLONG_MAX) {
		blog_line("Invalid number of keys: %" PRIu64, args->keys);
		return 1;
	}

	if (args->transaction_worker_threads <= 0 ||
			args->transaction_worker_threads > 10000) {
		blog_line("Invalid number of threads: %d  Valid values: [1-10000]",
				args->transaction_worker_threads);
		return 1;
	}

	if (!args->enable_compression && args->compression_ratio != 1.f) {
		blog_line("Compression ratio specified without enabling compression, "
				"add the --compress option when running");
		return 1;
	}

	if (args->compression_ratio < 0.001 || args->compression_ratio > 1) {
		blog_line("Compression ratio must be in the range [0.001, 1]\n");
		return 1;
	}

	if (args->read_socket_timeout < 0) {
		blog_line("Invalid read socket timeout: %d  Valid values: [>= 0]",
				args->read_socket_timeout);
		return 1;
	}

	if (args->write_socket_timeout < 0) {
		blog_line("Invalid write socket timeout: %d  Valid values: [>= 0]",
				args->write_socket_timeout);
		return 1;
	}

	if (args->read_total_timeout < 0) {
		blog_line("Invalid read total timeout: %d  Valid values: [>= 0]",
				args->read_total_timeout);
		return 1;
	}

	if (args->write_total_timeout < 0) {
		blog_line("Invalid write total timeout: %d  Valid values: [>= 0]",
				args->write_total_timeout);
		return 1;
	}

	if (args->latency_columns < 0 || args->latency_columns > 16) {
		blog_line("Invalid latency columns: %d  Valid values: [1-16]",
				args->latency_columns);
		return 1;
	}

	if (args->latency_shift < 0 || args->latency_shift > 5) {
		blog_line("Invalid latency shift: %d  Valid values: [1-5]",
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
				blog_line("Invalid percentile \"%f\"\n", itm);
				return 1;
			}
		}
		for (uint32_t i = 1; i < perc->size; i++) {
			double l = *(double*) as_vector_get(perc, i - 1);
			double r = *(double*) as_vector_get(perc, i);
			if (l >= r) {
				blog_line("%f >= %f, out of order in percentile list", l, r);
				return 1;
			}
		}
	}

	if (args->latency_histogram && args->histogram_period <= 0) {
		blog_line("Invalid histogram period: %ds", args->histogram_period);
		return 1;
	}

	if (args->conn_pools_per_node <= 0 || args->conn_pools_per_node > 1000) {
		blog_line("Invalid connPoolsPerNode: %d  Valid values: [1-1000]",
				args->conn_pools_per_node);
		return 1;
	}

	if (args->async) {
		if (args->async_max_commands <= 0 || args->async_max_commands > 5000) {
			blog_line("Invalid asyncMaxCommands: %d  Valid values: [1-5000]",
					args->async_max_commands);
			return 1;
		}

		if (args->event_loop_capacity <= 0 || args->event_loop_capacity > 1000) {
			blog_line("Invalid eventLoops: %d  Valid values: [1-1000]",
					args->event_loop_capacity);
			return 1;
		}
	}
	return 0;
}

static struct stage* get_or_init_stage(arguments* args)
{
	if (args->stages.stages == NULL) {
		args->stages.stages = (struct stage*) cf_calloc(1, sizeof(struct stage));
		args->stages.n_stages = 1;
		args->stages.valid = true;

		args->stages.stages[0].duration = -1LU;
		args->stages.stages[0].key_start = -1LU;
		args->stages.stages[0].key_end = -1LU;
	}
	return &args->stages.stages[0];
}

static int
set_args(int argc, char * const * argv, arguments* args)
{
	int option_index = 0;
	int c;

	while ((c = getopt_long(argc, argv, short_options, long_options,
					&option_index)) != -1) {
		switch (c) {
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

			case 'n':
				args->namespace = optarg;
				break;

			case 's':
				args->set = optarg;
				break;

			case 'K':
				args->start_key = strtoull(optarg, NULL, 10);
				break;

			case 'k':
				args->keys = strtoull(optarg, NULL, 10);
				break;

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
				args->random = true;
				break;

			case 't': {
				if (args->workload_stages_file != NULL) {
					fprintf(stderr, "Cannot specify both a workload stages "
							"file and the duration flag");
					return -1;
				}
				struct stage* stage = get_or_init_stage(args);
				char* endptr;
				stage->duration = strtoull(optarg, &endptr, 10);
				if (*optarg == '\0' || *endptr != '\0') {
					blog_line("string \"%s\" is not a decimal point number",
							optarg);
					return -1;
				}
				break;
			}

			case 'w': {
				if (args->workload_stages_file != NULL) {
					fprintf(stderr, "Cannot specify both a workload stages "
							"file and the workload flag");
					return -1;
				}
				struct stage* stage = get_or_init_stage(args);
				stage->workload_str = strdup(optarg);
				break;
			}

			case '.': {
				if (args->stages.stages != NULL) {
					fprintf(stderr, "Cannot specify both a workload stages "
							"file and the workload flag");
					return -1;
				}
				args->workload_stages_file = strdup(optarg);
				break;
			}

			case 'z':
				args->transaction_worker_threads = atoi(optarg);
				break;

			case 'g': {
				if (args->workload_stages_file != NULL) {
					fprintf(stderr, "Cannot specify both a workload stages "
							"file and the throughput flag");
					return -1;
				}
				struct stage* stage = get_or_init_stage(args);
				stage->tps = atoi(optarg);
				break;
			}

			case '0':
				args->batch_size = atoi(optarg);
				break;

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

			case 'L': {
				args->latency = true;
				char* tmp = strdup(optarg);
				char* p = strchr(tmp, ',');

				if (p) {
					*p = '\0';
					args->latency_columns = atoi(tmp);
					args->latency_shift = atoi(p + 1);
				}
				else {
					args->latency_columns = 4;
					args->latency_shift = 3;
				}
				free(tmp);
				break;
			}

			case '8': {
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
						blog_line("string \"%s\" is not a floating point number",
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
			}

			case '6': {
				args->latency_histogram = true;
				args->histogram_output = strdup(optarg);
				break;
			}

			case '7': {
				args->histogram_period = atoi(optarg);
				break;
			}

			case '/': {
				args->hdr_output = strdup(optarg);
				break;
			}

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
					blog_line("replica must be master | any | sequence");
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
					blog_line("readModeAP must be one or all");
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
					blog_line("readModeSC must be session | linearize | "
							"allowReplica | allowUnavailable");
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
					blog_line("commitLevel be all or master");
					return 1;
				}
				break;

			case 'Y':
				args->conn_pools_per_node = atoi(optarg);
				break;

			case 'D':
				args->durable_deletes = true;
				break;

			case 'a':
				args->async = true;
				break;

			case 'c':
				args->async_max_commands = atoi(optarg);
				break;

			case 'W':
				args->event_loop_capacity = atoi(optarg);
				break;

			case 'A':
				args->tls.enable = true;
				break;

			case 'E':
				args->tls.cafile = strdup(optarg);
				break;

			case 'F':
				args->tls.capath = strdup(optarg);
				break;

			case 'G':
				args->tls.protocols = strdup(optarg);
				break;

			case 'H':
				args->tls.cipher_suite = strdup(optarg);
				break;

			case 'I':
				args->tls.crl_check = true;
				break;

			case 'J':
				args->tls.crl_check_all = true;
				break;

			case 'O':
				args->tls.cert_blacklist = strdup(optarg);
				break;

			case 'Q':
				args->tls.log_session_info = true;
				break;

			case 'Z':
				args->tls.keyfile = strdup(optarg);
				break;

			case 'y':
				args->tls.certfile = strdup(optarg);
				break;

			case 'f':
				args->tls.for_login_only = true;
				break;

			case 'e':
				if (! as_auth_mode_from_string(&args->auth_mode, optarg)) {
					blog_line("invalid authentication mode: %s", optarg);
					return 1;
				}
				break;

			case 'u':
			default:
				return 1;
		}
	}
	return validate_args(args);
}

static void
_load_defaults(arguments* args)
{
	args->hosts = strdup("127.0.0.1");
	args->port = 3000;
	args->user = 0;
	args->password[0] = 0;
	args->namespace = "test";
	args->set = "testset";
	args->bin_name = "testbin";
	args->start_key = 1;
	args->keys = 1000000;
	__builtin_memset(&args->stages, 0, sizeof(struct stages));
	args->workload_stages_file = NULL;
	obj_spec_parse(&args->obj_spec, "I");
	args->random = false;
	args->transaction_worker_threads = 16;
	args->batch_size = 0;
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
	args->async = false;
	args->async_max_commands = 50;
	args->event_loop_capacity = 1;
	memset(&args->tls, 0, sizeof(as_config_tls));
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

static int
_load_defaults_post(arguments* args)
{
	int res = 0;

	if (args->workload_stages_file != NULL) {
		res = parse_workload_config_file(args->workload_stages_file,
				&args->stages, args);
	}
	else {
		struct stage* stage = get_or_init_stage(args);

		stage->desc = strdup("default config (specify your own with "
				"--workloadStages)");

		if (stage->workload_str == NULL) {
			stage->workload_str = strdup("RU");
		}

		stages_set_defaults_and_parse(&args->stages, args);
	}

	return res;
}

int
main(int argc, char * const * argv)
{
	arguments args;
	_load_defaults(&args);

	int ret = set_args(argc, argv, &args);

	if (ret == 0) {
		ret = _load_defaults_post(&args);
	}

	if (ret == 0) {
		print_args(&args);
		run_benchmark(&args);
	}
	else if (ret != -1) {
		blog_line("Run with --help for usage information and flag options.");
	}

	free_workload_config(&args.stages);
	obj_spec_free(&args.obj_spec);
	if (args.workload_stages_file) {
		cf_free(args.workload_stages_file);
	}
	if (args.hdr_output) {
		cf_free(args.hdr_output);
	}
	if (args.histogram_output) {
		cf_free(args.histogram_output);
	}
	as_vector_destroy(&args.latency_percentiles);

	free(args.hosts);
	return ret;
}
