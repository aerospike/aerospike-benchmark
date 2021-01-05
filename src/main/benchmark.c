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
#include "benchmark.h"
#include "common.h"
#include "aerospike/aerospike_info.h"
#include "aerospike/as_config.h"
#include "aerospike/as_event.h"
#include "aerospike/as_log.h"
#include "aerospike/as_monitor.h"
#include "aerospike/as_random.h"
#include "aerospike/as_string_builder.h"
#include <hdr_histogram/hdr_time.h>
#include <hdr_histogram/hdr_histogram_log.h>
#include <stdlib.h>
#include <time.h>

as_monitor monitor;

static bool
as_client_log_callback(as_log_level level, const char * func, const char * file, uint32_t line, const char * fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	blog_detailv(level, fmt, ap);
	va_end(ap);
	return true;
}

static int
connect_to_server(arguments* args, aerospike* client)
{
	if (args->async) {
		as_monitor_init(&monitor);

#if AS_EVENT_LIB_DEFINED
		if (! as_event_create_loops(args->event_loop_capacity)) {
			blog_error("Failed to create asynchronous event loops");
			return 2;
		}
#else
		blog_error("Must 'make EVENT_LIB=<libname>' to use asynchronous functions.");
		return 2;
#endif
	}
	
	as_config cfg;
	as_config_init(&cfg);
	
	if (! as_config_add_hosts(&cfg, args->hosts, args->port)) {
		blog_error("Invalid host(s) %s", args->hosts);
		return 3;
	}

	as_config_set_user(&cfg, args->user, args->password);
	cfg.use_shm = args->use_shm;
	cfg.conn_timeout_ms = 10000;
	cfg.login_timeout_ms = 10000;

	// Disable batch/scan/query thread pool because these commands are not used in benchmarks.
	cfg.thread_pool_size = 0;
	cfg.conn_pools_per_node = args->conn_pools_per_node;

	if (cfg.async_max_conns_per_node < (uint32_t)args->async_max_commands) {
		cfg.async_max_conns_per_node = args->async_max_commands;
	}

	as_policies* p = &cfg.policies;

	p->read.base.socket_timeout = args->read_socket_timeout;
	p->read.base.total_timeout = args->read_total_timeout;
	p->read.base.max_retries = args->max_retries;
	p->read.base.compress = args->enable_compression;
	p->read.replica = args->replica;
	p->read.read_mode_ap = args->read_mode_ap;
	p->read.read_mode_sc = args->read_mode_sc;

	p->write.base.socket_timeout = args->write_socket_timeout;
	p->write.base.total_timeout = args->write_total_timeout;
	p->write.base.max_retries = args->max_retries;
	p->write.base.compress = args->enable_compression;
	p->write.replica = args->replica;
	p->write.commit_level = args->write_commit_level;
	p->write.durable_delete = args->durable_deletes;

	p->operate.base.socket_timeout = args->write_socket_timeout;
	p->operate.base.total_timeout = args->write_total_timeout;
	p->operate.base.max_retries = args->max_retries;
	p->operate.base.compress = args->enable_compression;
	p->operate.replica = args->replica;
	p->operate.commit_level = args->write_commit_level;
	p->operate.durable_delete = args->durable_deletes;
	p->operate.read_mode_ap = args->read_mode_ap;
	p->operate.read_mode_sc = args->read_mode_sc;

	p->remove.base.socket_timeout = args->write_socket_timeout;
	p->remove.base.total_timeout = args->write_total_timeout;
	p->remove.base.max_retries = args->max_retries;
	p->remove.base.compress = args->enable_compression;
	p->remove.replica = args->replica;
	p->remove.commit_level = args->write_commit_level;
	p->remove.durable_delete = args->durable_deletes;

	p->batch.base.socket_timeout = args->read_socket_timeout;
	p->batch.base.total_timeout = args->read_total_timeout;
	p->batch.base.max_retries = args->max_retries;
	p->batch.base.compress = args->enable_compression;
	p->batch.replica = args->replica;
	p->batch.read_mode_ap = args->read_mode_ap;
	p->batch.read_mode_sc = args->read_mode_sc;

	p->info.timeout = 10000;

	// Transfer ownership of all heap allocated TLS fields via shallow copy.
	memcpy(&cfg.tls, &args->tls, sizeof(as_config_tls));
	cfg.auth_mode = args->auth_mode;

	aerospike_init(client, &cfg);
	
	as_error err;
	
	if (aerospike_connect(client, &err) != AEROSPIKE_OK) {
		blog_error("%s", err.message);
		aerospike_destroy(client);
		return 1;
	}
	return 0;
}

static bool
is_single_bin(aerospike* client, const char* namespace)
{
	char filter[256];
	sprintf(filter, "namespace/%s", namespace);
	
	char* res = 0;
	as_error err;
	as_status rc = aerospike_info_any(client, &err, 0, filter, &res);
	bool single_bin = false;
	
	if (rc == AEROSPIKE_OK) {
		static const char* search = "single-bin=";
		char* p = strstr(res, search);
		
		if (p) {
			// The compiler (with -O3 flag) will know search is a literal and optimize strlen accordingly.
			p += strlen(search);
			char* q = strchr(p, ';');
			
			if (q) {
				*q = 0;
				
				if (strcmp(p, "true") == 0) {
					single_bin = true;
				}
			}
		}
		free(res);
	}
	else {
		blog_error("Info request failed: %d - %s", err.code, err.message);
	}
	return single_bin;
}

bool
is_stop_writes(aerospike* client, const char* namespace)
{
	char filter[256];
	sprintf(filter, "namespace/%s", namespace);
	
	char* res = 0;
	as_error err;
	as_status rc = aerospike_info_any(client, &err, NULL, filter, &res);
	bool stop_writes = false;
	
	if (rc == AEROSPIKE_OK) {
		static const char* search = "stop-writes=";
		char* p = strstr(res, search);
		
		if (p) {
			// The compiler (with -O3 flag) will know search is a literal and optimize strlen accordingly.
			p += strlen(search);
			char* q = strchr(p, ';');
			
			if (q) {
				*q = 0;
				
				if (strcmp(p, "true") == 0) {
					stop_writes = true;
				}
			}
		}
	}
	else {
		blog_error("Info request failed: %d - %s", err.code, err.message);
	}
	free(res);
	return stop_writes;
}

int
run_benchmark(arguments* args)
{
	clientdata data;
	memset(&data, 0, sizeof(clientdata));
	data.namespace = args->namespace;
	data.set = args->set;
	data.threads = args->threads;
	data.throughput = args->throughput;
	data.batch_size = args->batch_size;
	data.read_pct = args->read_pct;
	data.del_bin = args->del_bin;
	data.compression_ratio = args->compression_ratio;
	data.bintype = args->bintype;
	data.binlen = args->binlen;
	data.binlen_type = args->binlen_type;
	data.numbins = args->numbins;
	data.random = args->random;
	data.transactions_limit = args->transactions_limit;
	data.transactions_count = 0;
	data.latency = args->latency;
	data.debug = args->debug;
	data.valid = 1;
	data.async = args->async;
	data.async_max_commands = args->async_max_commands;
	data.fixed_value = NULL;

	// set to 0 when any step in initialization fails
	int valid = 1;
	time_t start_time;
	hdr_timespec start_timespec;

	if (args->debug) {
		as_log_set_level(AS_LOG_LEVEL_DEBUG);
	}
	else {
		as_log_set_level(AS_LOG_LEVEL_INFO);
	}

	as_log_set_callback(as_client_log_callback);

	int ret = connect_to_server(args, &data.client);
	
	if (ret != 0) {
		return ret;
	}
	
	bool single_bin = is_single_bin(&data.client, args->namespace);
	
	if (single_bin) {
		data.bin_name = "";
	}
	else {
		data.bin_name = "testbin";
	}

	if (! args->random) {
		gen_value(args, &data.fixed_value);
	}
	
	if (args->latency) {
		latency_init(&data.write_latency, args->latency_columns, args->latency_shift);
		hdr_init(1, 1000000, 3, &data.write_hdr);
		as_vector_init(&data.latency_percentiles, args->latency_percentiles.item_size,
				args->latency_percentiles.capacity);
		for (uint32_t i = 0; i < args->latency_percentiles.size; i++) {
			as_vector_append(&data.latency_percentiles,
					as_vector_get(&args->latency_percentiles, i));
		}

		if (! args->init) {
			latency_init(&data.read_latency, args->latency_columns, args->latency_shift);
			hdr_init(1, 1000000, 3, &data.read_hdr);
		}
	}
	
	if (args->latency_histogram) {
		data.histogram_output = fopen(args->histogram_output, "a");
		if (!data.histogram_output) {
			fprintf(stderr, "Unable to open %s in append mode\n",
					args->histogram_output);
			valid = 0;
			// follow through with initialization, so cleanup won't segfault
		}

		histogram_init(&data.write_histogram, 3, 100, (rangespec_t[]) {
				{ .upper_bound = 4000,   .bucket_width = 100  },
				{ .upper_bound = 64000,  .bucket_width = 1000 },
				{ .upper_bound = 128000, .bucket_width = 4000 }
				});
		histogram_set_name(&data.write_histogram, "write_hist");
		histogram_print_info(&data.write_histogram, data.histogram_output);
		
		if (! args->init) {
			histogram_init(&data.read_histogram, 3, 100, (rangespec_t[]) {
					{ .upper_bound = 4000,   .bucket_width = 100  },
					{ .upper_bound = 64000,  .bucket_width = 1000 },
					{ .upper_bound = 128000, .bucket_width = 4000 }
					});
			histogram_set_name(&data.read_histogram, "read_hist");
			histogram_print_info(&data.read_histogram, data.histogram_output);
		}

		data.histogram_period = args->histogram_period;

	}
	
	if (args->hdr_output) {
		const static char write_output_prefix[] = "/write_";
		const static char read_output_prefix[] = "/read_";
		const static char output_suffix[] = ".hdrhist";

		start_time = time(NULL);
		const char* utc_time = utc_time_str(start_time);

		size_t prefix_len = strlen(args->hdr_output);
		size_t write_output_size =
			prefix_len + (sizeof(write_output_prefix) - 1) +
			UTC_STR_LEN + (sizeof(output_suffix) - 1) + 1;

		as_string_builder write_output_b;
		as_string_builder_inita(&write_output_b, write_output_size, false);

		as_string_builder_append(&write_output_b, args->hdr_output);
		as_string_builder_append(&write_output_b, write_output_prefix);
		as_string_builder_append(&write_output_b, utc_time);
		as_string_builder_append(&write_output_b, output_suffix);

		data.hdr_write_output = fopen(write_output_b.data, "a");
		if (!data.hdr_write_output) {
			fprintf(stderr, "Unable to open %s in append mode, reason: %s\n",
					write_output_b.data, strerror(errno));
			valid = 0;
		}

		as_string_builder_destroy(&write_output_b);

		hdr_init(1, 1000000, 3, &data.summary_write_hdr);

		if (! args->init) {
			size_t read_output_size =
				prefix_len + (sizeof(read_output_prefix) - 1) +
				UTC_STR_LEN + (sizeof(output_suffix) - 1) + 1;

			as_string_builder read_output_b;
			as_string_builder_inita(&read_output_b, read_output_size, false);

			as_string_builder_append(&read_output_b, args->hdr_output);
			as_string_builder_append(&read_output_b, read_output_prefix);
			as_string_builder_append(&read_output_b, utc_time);
			as_string_builder_append(&read_output_b, output_suffix);

			data.hdr_read_output = fopen(read_output_b.data, "a");
			if (!data.hdr_read_output) {
				fprintf(stderr, "Unable to open %s in append mode, reason: %s\n",
						read_output_b.data, strerror(errno));
				valid = 0;
			}

			as_string_builder_destroy(&read_output_b);

			hdr_init(1, 1000000, 3, &data.summary_read_hdr);
		}

		hdr_gettime(&start_timespec);
	}

	data.key_start = args->start_key;
	data.key_count = 0;

	if (valid) {
		if (args->init) {
			data.n_keys = (uint64_t)((double)args->keys / 100.0 * args->init_pct + 0.5);
			ret = linear_write(&data);
		}
		else {
			data.n_keys = args->keys;
			ret = random_read_write(&data);
		}

		// now record summary HDR hist if enabled
		if (args->hdr_output) {
			hdr_timespec end_timespec;
			hdr_gettime(&end_timespec);

			struct hdr_log_writer writer;
			hdr_log_writer_init(&writer);

#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
			// don't worry, will be initialized (would have to make args const
			// to suppress)
			const char* utc_time = utc_time_str(start_time);
#pragma GCC diagnostic pop
			hdr_log_write_header(&writer, data.hdr_write_output,
					utc_time, &start_timespec);

			hdr_log_write(&writer, data.hdr_write_output,
					&start_timespec, &end_timespec, data.summary_write_hdr);
			fclose(data.hdr_write_output);

			if (! args->init) {
				hdr_log_write_header(&writer, data.hdr_read_output,
						utc_time, &start_timespec);

				hdr_log_write(&writer, data.hdr_read_output,
						&start_timespec, &end_timespec, data.summary_read_hdr);
				printf("total num entries: %lu\n", hdr_total_count(data.summary_read_hdr));
				fclose(data.hdr_read_output);
			}
		}
	}

	if (!args->random) {
		as_val_destroy(data.fixed_value);
	}

	if (args->latency) {
		latency_free(&data.write_latency);
		hdr_close(data.write_hdr);

		as_vector_destroy(&data.latency_percentiles);

		if (! args->init) {
			latency_free(&data.read_latency);
			hdr_close(data.read_hdr);
		}
	}

	if (args->latency_histogram) {
		histogram_free(&data.write_histogram);
		
		if (! args->init) {
			histogram_free(&data.read_histogram);
		}

		fclose(data.histogram_output);
	}

	as_error err;
	aerospike_close(&data.client, &err);
	aerospike_destroy(&data.client);
	
	if (args->async) {
		as_event_close_loops();
		as_monitor_destroy(&monitor);
	}
	
	return ret;
}
