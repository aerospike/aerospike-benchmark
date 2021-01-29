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

#if 0

extern as_monitor monitor;

static void*
random_worker(void* udata)
{
	clientdata* cdata = (clientdata*)udata;
	threaddata* tdata = create_threaddata(cdata, cdata->key_start, cdata->n_keys);

	// multiply pct by 2**24 before dividing by 100 and casting to an int,
	// since floats have 24 bits of precision including the leading 1,
	// so that read_pct is pct% between 0 and 2**24
	uint32_t read_pct = (uint32_t) ((0x01000000 * tdata->workload.pct) / 100);
	
	while (cdata->valid) {
		// Roll a percentage die.
		uint32_t die = as_random_next_uint32(tdata->random);

		// floats have 24 bits of precision (including implicit leading 1)
		die &= 0x00ffffff;
		
		if (die < read_pct) {
			if (cdata->batch_size <= 1) {
				read_record_sync(cdata, tdata);
			}
			else {
				batch_record_sync(cdata, tdata);
			}
		}
		else {
			uint64_t key = stage_gen_random_key(tdata->stage, tdata->random);
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
#endif
