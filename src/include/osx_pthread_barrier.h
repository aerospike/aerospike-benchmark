/*******************************************************************************
 * Copyright 2020-2021 by Aerospike.
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
#ifdef __APPLE__

#include <stddef.h>
#include <stdint.h>

/*
 * returned by pthread_barrier_wait to exactly one of the threads waiting on
 * the barrier
 */
#define PTHREAD_BARRIER_SERIAL_THREAD -1

/*
 * this implementation of pthread barriers assumes that only "count" threads
 * will be trying to pass the barrier at any given time
 */
typedef struct pthread_barrier {
	pthread_cond_t cond;
	pthread_mutex_t lock;

	// the number of threads that must meet at the barrier
	uint32_t count;
	// the count of threads that have met the barrier
	_Atomic(uint32_t) in;
	// the round number (starts at 0, incremented each time all threads reach
	// the barrier)
	_Atomic(uint32_t) current_round;
} pthread_barrier_t;

int32_t pthread_barrier_init(pthread_barrier_t*, void* attr,
		uint32_t count);
int32_t pthread_barrier_destroy(pthread_barrier_t*);

/*
 * waits at the barrier until "count" threads have called this function.
 *
 * returns PTHREAD_BARRIER_SERIAL_THREAD to one of the calling threads and 0
 * to the rest
 */
int32_t pthread_barrier_wait(pthread_barrier_t* barrier);

#endif /* __APPLE__ */
