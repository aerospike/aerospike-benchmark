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

#include <time.h>

#include <hdr_histogram/hdr_histogram.h>
#include <hdr_histogram/hdr_time.h>
#include <benchmark.h>
#include <common.h>

/*
 * initialize the histograms in cdata according to the arguments in args,
 * setting the start time and start_timespec if args->hdr_output is not NULL
 * (i.e. if the summary histogram is enabled)
 */
int initialize_histograms(clientdata* data, arguments* args,
		time_t* start_time, hdr_timespec* start_timespec);

/*
 * frees the histograms in cdata
 */
void free_histograms(clientdata* data, arguments* args);

/*
 * to be called after the benchmark is complete in order to save summary data
 * from the cumulative histograms
 */
void record_summary_data(clientdata* data, arguments* args, time_t start_time,
		hdr_timespec* start_timespec);


/*
 * init function of the worker thread responsible for periodic output
 *
 * udata should be a pointer to a clientdata struct
 */
void* periodic_output_worker(void* cdata);

