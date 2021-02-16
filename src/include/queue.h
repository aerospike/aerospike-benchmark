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

#include <stdint.h>

/*
 * implementation of a lock-free single-popper multiple-pusher thread-safe
 * queue
 */
typedef struct queue_s {
	void** items;
	// length of items - 1 (length is always a power of 2)
	uint32_t len_mask;

	// the position of the next element to be inserted (modulo len)
	uint32_t __attribute__((aligned(8))) pos;
	// the position of the head, i.e. the next element to be popped (modulo len)
	uint32_t __attribute__((aligned(8))) head;

	pthread_cond_t empty_cond;
	pthread_mutex_t e_lock;
} queue_t;


/*
 * initializes the queue with the given length
 */
int queue_init(queue_t* q, uint32_t q_len);

/*
 * frees resources allocated by the queue
 */
void queue_free(queue_t* q);

/*
 * push an item to the back of the queue
 *
 * note that if something tries pushing to the queue when it is at capacity, it
 * results in undefined behavior
 */
void queue_push(queue_t* q, void* item);

/*
 * attempts to pop an item from the end of the queue, returning that item on
 * success and returning NULL if the queue is empty
 */
void* queue_pop(queue_t* q);

