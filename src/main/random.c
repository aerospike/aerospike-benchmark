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
#include <aerospike/as_atomic.h>
#include <aerospike/as_monitor.h>
#include <aerospike/as_random.h>
#include <aerospike/as_sleep.h>
#include <citrusleaf/cf_clock.h>
#include <pthread.h>

extern as_monitor monitor;

static void*
ticker_worker(void* udata)
{
	clientdata* data = (clientdata*)udata;
	latency* write_latency = &data->write_latency;
	latency* read_latency = &data->read_latency;
	bool latency = data->latency;
	char latency_header[500];
	char latency_detail[500];
	uint64_t gen_count = 0;
	histogram* write_histogram = &data->write_histogram;
	histogram* read_histogram = &data->read_histogram;
	FILE* histogram_output = data->histogram_output;
	
	uint64_t start_time = cf_getus();
	uint64_t prev_time = start_time;
	data->period_begin = prev_time;
	
	if (latency) {
		latency_set_header(write_latency, latency_header);
	}
	as_sleep(1000);
	
	while (data->valid) {
		uint64_t time = cf_getus();
		int64_t elapsed = time - prev_time;
		prev_time = time;
		
		uint32_t write_current = as_fas_uint32(&data->write_count, 0);
		uint32_t write_timeout_current = as_fas_uint32(&data->write_timeout_count, 0);
		uint32_t write_error_current = as_fas_uint32(&data->write_error_count, 0);
		uint32_t read_current = as_fas_uint32(&data->read_count, 0);
		uint32_t read_timeout_current = as_fas_uint32(&data->read_timeout_count, 0);
		uint32_t read_error_current = as_fas_uint32(&data->read_error_count, 0);
		uint64_t transactions_current = as_load_uint64(&data->transactions_count);

		data->period_begin = time;
	
		uint32_t write_tps = (uint32_t)((double)write_current * 1000 / elapsed + 0.5);
		uint32_t read_tps = (uint32_t)((double)read_current * 1000 / elapsed + 0.5);
		
		blog_info("write(tps=%d timeouts=%d errors=%d) read(tps=%d timeouts=%d errors=%d) total(tps=%d timeouts=%d errors=%d)",
			write_tps, write_timeout_current, write_error_current,
			read_tps, read_timeout_current, read_error_current,
			write_tps + read_tps, write_timeout_current + read_timeout_current, write_error_current + read_error_current);

		if (latency) {
			blog_line("%s", latency_header);
			latency_print_results(write_latency, "write", latency_detail);
			blog_line("%s", latency_detail);
			latency_print_results(read_latency, "read", latency_detail);
			blog_line("%s", latency_detail);

			uint64_t elapsed_s = (time - start_time) / 1000000;
			print_hdr_percentiles(data->write_hdr, "write", elapsed_s,
					&data->latency_percentiles, stdout);
			print_hdr_percentiles(data->read_hdr,  "read",  elapsed_s,
					&data->latency_percentiles, stdout);
		}
		
		++gen_count;
		
		if ((histogram_output != NULL) && ((gen_count % data->histogram_period) == 0)) {
			histogram_print_clear(write_histogram, data->histogram_period, histogram_output);
			histogram_print_clear(read_histogram, data->histogram_period, histogram_output);
			fflush(histogram_output);
		}

		if ((data->transactions_limit > 0) && (transactions_current > data->transactions_limit)) {
			blog_line("Performed %" PRIu64 " (> %" PRIu64 ") transactions. Shutting down...", transactions_current, data->transactions_limit);
			data->valid = false;
			continue;
		}

		as_sleep(1000);
	}
	return 0;
}

static void*
random_worker(void* udata)
{
	clientdata* cdata = (clientdata*)udata;
	threaddata* tdata = create_threaddata(cdata, cdata->key_start, cdata->n_keys);
	int read_pct = cdata->read_pct;
	int die;
	
	while (cdata->valid) {
		// Roll a percentage die.
		die = as_random_next_uint32(tdata->random) % 100;
		
		if (die < read_pct) {
			if (cdata->batch_size <= 1) {
				read_record_sync(cdata, tdata);
			}
			else {
				batch_record_sync(cdata, tdata);
			}
		}
		else {
			uint64_t key = as_random_next_uint64(tdata->random) % cdata->n_keys + cdata->key_start;
			write_record_sync(cdata, tdata, key);
		}
		as_incr_uint64(&cdata->transactions_count);

		throttle(cdata);
	}
	destroy_threaddata(tdata);
	return 0;
}

static void
random_worker_async(clientdata* cdata)
{
	// Generate max command writes to seed the event loops.
	// Then start a new command in each command callback.
	// This effectively throttles new command generation, by only allowing
	// asyncMaxCommands at any point in time.
	as_monitor_begin(&monitor);
	
	int max = cdata->async_max_commands;
	
	for (int i = 0; i < max; i++) {
		// Allocate separate buffers for each seed command and reuse them in callbacks.
		threaddata* tdata = create_threaddata(cdata, cdata->key_start, cdata->n_keys);
		as_incr_uint32(&cdata->tdata_count);

		// Start seed commands on random event loops.
		random_read_write_async(cdata, tdata, 0);
	}
	as_monitor_wait(&monitor);
}

int
random_read_write(clientdata* cdata)
{
	blog_info("Read/write using %u records", cdata->n_keys);
	
	pthread_t ticker;
	if (pthread_create(&ticker, 0, ticker_worker, cdata) != 0) {
		cdata->valid = false;
		blog_error("Failed to create thread.");
		return -1;
	}
	
	if (cdata->async) {
		// Asynchronous mode.
		random_worker_async(cdata);
	}
	else {
		// Synchronous mode.
		int max = cdata->threads;
		blog_info("Start %d generator threads", max);
		pthread_t* threads = alloca(sizeof(pthread_t) * max);
		
		for (int i = 0; i < max; i++) {
			if (pthread_create(&threads[i], 0, random_worker, cdata) != 0) {
				cdata->valid = false;
				blog_error("Failed to create thread.");
				return -1;
			}
		}
		
		for (int i = 0; i < max; i++) {
			pthread_join(threads[i], 0);
		}
	}
	cdata->valid = false;
	pthread_join(ticker, 0);
	return 0;
}
