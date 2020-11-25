/*******************************************************************************
 * Copyright 2008-2018 by Aerospike.
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

/*
 * used to implement a data structure with two instances, one as the primary
 * instance, and another as the secondary instance. When an atomic
 * read-and-reset operation needs to be performed (i.e. for a historgram, to
 * print the current state and then clear), the two buffers are swapped so the
 * former primary buffer can be read while the new primary buffer is used for
 * any subsequent concurrent modifications
 */
typedef struct swap_buffer {
	void * primary;
	void * secondary;
} swap_buffer_t;


void* swap_buffer_primary(swap_buffer_t * buf);

/*
 * swaps the primary and secondary buffers, returning a pointer to the former
 * primary buffer
 */
void* swap_buffer_swap(swap_buffer_t * buf);

