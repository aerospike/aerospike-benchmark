/*******************************************************************************
 * Copyright 2020 by Aerospike.
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

#include <stddef.h>
#include <stdint.h>


// the approximate number of records to average over
#define DYN_THROTTLE_N 20
// the weight that new records have in moving the avg_fn_delay
#define DYN_THROTTLE_ALPHA (1.f / DYN_THROTTLE_N)

typedef struct dyn_throttle {
	// the number of records that have been recorded
	uint64_t n_records;

	// the last record that was recorded + the sleep time returned
	uint64_t last_rec;

	// the target period (i.e. average units of time between records)
	float target_period;

	// a rolling average of the excess amount of time between records not
	// accounted for by the pausing
	float avg_fn_delay;
} dyn_throttle_t;


int dyn_throttle_init(dyn_throttle_t*, float target_period);

// nothing needs to be done for free
#define dyn_throttle_free(thr)

/*
 * sets the throttler last_rec such that the next call to dyn_throttle_pause_for
 * with parameter "next_rec" won't affect the learned avg_fn_delay
 */
void dyn_throttle_reset_time(dyn_throttle_t*, uint64_t next_rec);

/*
 * records the time "rec" in the time history table and returns the amount of
 * time to pause for (in the same units as rec)
 */
uint64_t dyn_throttle_pause_for(dyn_throttle_t*, uint64_t rec);



