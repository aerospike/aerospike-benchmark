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
#pragma once


#include <aerospike/as_random.h>

#include <benchmark.h>

struct threaddata {
	clientdata* cdata;
	struct thr_coordinator* coord;
	as_random* random;

	// thread index: [0, n_threads)
	uint32_t t_idx;

	// when true, things continue as normal, when set to false, worker
	// threads will stop doing what they're doing and await orders
	bool do_work;
	// when true, all threads will stop doing work and close (note that do_work
	// must also be set to false for this to work)
	bool finished;
};

/*
 * allocates and initializes a new threaddata struct, returning a pointer to it
 */
struct threaddata* init_tdata(clientdata* cdata,
		struct thr_coordinator*, uint32_t thread_idx);

/*
 * destroys a threaddata struct initialized by init_tdata
 */
void destroy_tdata(struct threaddata*);


/*
 * init function for worker threads that will be handling transactions
 *
 * udata should be a pointer to a threaddata struct
 */
void* transaction_worker(void* tdata);

/*
 * init function for worker threads that will be handling asynchronous
 * transactions
 *
 * udata should be a pointer to a threaddata struct
 */
void* transaction_worker_async(void* tdata);

