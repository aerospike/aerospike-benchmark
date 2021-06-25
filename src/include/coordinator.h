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

#include <pthread.h>

#include <benchmark.h>
#include <osx_pthread_barrier.h>


#define COORD_CLOCK CLOCK_MONOTONIC


/*
 * returned by thr_coordinator_sleep to indicate that the requested sleep
 * duration has passed
 */
#define COORD_SLEEP_TIMEOUT     0
/*
 * returned by the_coordinator_sleep to indicate that the sleep was interrupted
 * because all threads have finished their required work
 */
#define COORD_SLEEP_INTERRUPTED 1


// forward declare to avoid circular inclusion
struct threaddata_s;

typedef struct thr_coordinator_s {
	/*
	 * condition variable that the coordinator thread waits on after the stage
	 * duration has elapsed but not all threads have completed their alloted
	 * workload
	 */
	pthread_cond_t complete;
	pthread_mutex_t c_lock;

	pthread_barrier_t barrier;
	uint32_t n_threads;
	// number of threads which have yet to call thr_coordinator_complete this
	// stage, plus this thread (which decrements this variable after returning
	// from the as_sleep call, i.e. once the minimum required duration of the
	// stage has elapsed)
	uint32_t unfinished_threads;
} thr_coord_t;

struct coordinator_worker_args_s {
	thr_coord_t* coord;
	cdata_t* cdata;

	// list of thread data pointers
	struct threaddata_s** tdatas;
	uint32_t n_threads;
};

/*
 * initializes the given thread coordinator struct
 */
int thr_coordinator_init(thr_coord_t*, uint32_t n_threads);

void thr_coordinator_free(thr_coord_t*);


/*
 * causes all threads to wait at a barrier until the coordinator thread
 * executes some code, after which the thread coordinator will release all
 * the threads again
 *
 * this is safe to call whenever during a stage, even before every other thread
 * has called thr_coordinator_complete
 */
void thr_coordinator_wait(thr_coord_t*);

/*
 * notifies the thread coordinator that this thread has completed its task.
 * once both the last thread has notified the thread coordinator that it's
 * complete and the minimum stage duration has elapsed, the coordinator will
 * complete the stage
 *
 * this call is non-blocking, and it may be called at the very beginning of a
 * stage if a thread doesn't have any specific task to complete and may be
 * stopped at any point (like the logging thread, or the transaction threads
 * performing a read/update workload)
 */
void thr_coordinator_complete(thr_coord_t*);

/*
 * puts the calling thread to sleep until the given wakeup time, either
 * returning when that time has been reached, or when the workload has completed
 *
 * returns either (see definitions above):
 * 	COORD_SLEEP_TIMEOUT
 * 	COORD_SLEEP_INTERRUPTED
 *
 * wakeup_time must be given by the CLOCK_MONOTONIC clock
 */
int thr_coordinator_sleep(thr_coord_t*, const struct timespec* wakeup_time);


/*
 * init function for the thread coordinator thread
 *
 * should be passed a pointer to a clientdata struct
 */
void* coordinator_worker(void* cdata);

