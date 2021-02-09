
#include <latency_output.h>

#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include <aerospike/as_atomic.h>
#include <aerospike/as_string_builder.h>
#include <citrusleaf/cf_clock.h>

#include <hdr_histogram/hdr_histogram_log.h>

#include <transaction.h>


int initialize_histograms(clientdata* data, arguments* args,
		time_t* start_time, hdr_timespec* start_timespec) {
	int ret = 0;
	bool has_reads = stages_contains_reads(&data->stages);

	if (args->latency) {
		//latency_init(&data->write_latency, args->latency_columns, args->latency_shift);
		hdr_init(1, 1000000, 3, &data->write_hdr);
		as_vector_init(&data->latency_percentiles, args->latency_percentiles.item_size,
				args->latency_percentiles.capacity);
		for (uint32_t i = 0; i < args->latency_percentiles.size; i++) {
			as_vector_append(&data->latency_percentiles,
					as_vector_get(&args->latency_percentiles, i));
		}

		if (has_reads) {
			//latency_init(&data->read_latency, args->latency_columns, args->latency_shift);
			hdr_init(1, 1000000, 3, &data->read_hdr);
		}
	}
	
	if (args->latency_histogram) {
		if (args->histogram_output) {
			data->histogram_output = fopen(args->histogram_output, "a");
			if (!data->histogram_output) {
				fprintf(stderr, "Unable to open %s in append mode\n",
						args->histogram_output);
				ret = -1;
				// follow through with initialization, so cleanup won't segfault
			}
		}
		else {
			data->histogram_output = stdout;
		}

		histogram_init(&data->write_histogram, 3, 100, (rangespec_t[]) {
				{ .upper_bound = 4000,   .bucket_width = 100  },
				{ .upper_bound = 64000,  .bucket_width = 1000 },
				{ .upper_bound = 128000, .bucket_width = 4000 }
				});
		histogram_set_name(&data->write_histogram, "write_hist");
		histogram_print_info(&data->write_histogram, data->histogram_output);
		
		if (has_reads) {
			histogram_init(&data->read_histogram, 3, 100, (rangespec_t[]) {
					{ .upper_bound = 4000,   .bucket_width = 100  },
					{ .upper_bound = 64000,  .bucket_width = 1000 },
					{ .upper_bound = 128000, .bucket_width = 4000 }
					});
			histogram_set_name(&data->read_histogram, "read_hist");
			histogram_print_info(&data->read_histogram, data->histogram_output);
		}

		data->histogram_period = args->histogram_period;

	}
	
	if (args->hdr_output) {
		const static char write_output_prefix[] = "/write_";
		const static char read_output_prefix[] = "/read_";
		const static char compressed_output_suffix[] = ".hdrhist";
		const static char text_output_suffix[] = ".txt";

		*start_time = time(NULL);
		const char* utc_time = utc_time_str(*start_time);

		size_t prefix_len = strlen(args->hdr_output);
		size_t write_output_size =
			prefix_len + (sizeof(write_output_prefix) - 1) +
			UTC_STR_LEN + (sizeof(compressed_output_suffix) - 1) + 1;

		as_string_builder cmp_write_output_b;
		as_string_builder txt_write_output_b;
		as_string_builder_inita(&cmp_write_output_b, write_output_size, false);
		as_string_builder_inita(&txt_write_output_b, write_output_size, false);

		as_string_builder_append(&cmp_write_output_b, args->hdr_output);
		as_string_builder_append(&cmp_write_output_b, write_output_prefix);
		as_string_builder_append(&cmp_write_output_b, utc_time);

		// duplicate the current buffer into txt (since only the extension differs
		as_string_builder_append(&txt_write_output_b, cmp_write_output_b.data);

		as_string_builder_append(&cmp_write_output_b, compressed_output_suffix);
		as_string_builder_append(&txt_write_output_b, text_output_suffix);

		data->hdr_comp_write_output = fopen(cmp_write_output_b.data, "a");
		if (!data->hdr_comp_write_output) {
			fprintf(stderr, "Unable to open %s in append mode, reason: %s\n",
					cmp_write_output_b.data, strerror(errno));
			ret = -1;
		}

		data->hdr_text_write_output = fopen(txt_write_output_b.data, "a");
		if (!data->hdr_text_write_output) {
			fprintf(stderr, "Unable to open %s in append mode, reason: %s\n",
					cmp_write_output_b.data, strerror(errno));
			ret = -1;
		}

		as_string_builder_destroy(&cmp_write_output_b);
		as_string_builder_destroy(&txt_write_output_b);

		hdr_init(1, 1000000, 3, &data->summary_write_hdr);

		if (has_reads) {
			size_t read_output_size =
				prefix_len + (sizeof(read_output_prefix) - 1) +
				UTC_STR_LEN + (sizeof(compressed_output_suffix) - 1) + 1;

			as_string_builder cmp_read_output_b;
			as_string_builder txt_read_output_b;
			as_string_builder_inita(&cmp_read_output_b, read_output_size, false);
			as_string_builder_inita(&txt_read_output_b, read_output_size, false);

			as_string_builder_append(&cmp_read_output_b, args->hdr_output);
			as_string_builder_append(&cmp_read_output_b, read_output_prefix);
			as_string_builder_append(&cmp_read_output_b, utc_time);

			// duplicate the current buffer into txt (since only the extension differs
			as_string_builder_append(&txt_read_output_b, cmp_read_output_b.data);

			as_string_builder_append(&cmp_read_output_b, compressed_output_suffix);
			as_string_builder_append(&txt_read_output_b, text_output_suffix);

			data->hdr_comp_read_output = fopen(cmp_read_output_b.data, "a");
			if (!data->hdr_comp_read_output) {
				fprintf(stderr, "Unable to open %s in append mode, reason: %s\n",
						cmp_read_output_b.data, strerror(errno));
				ret = -1;
			}

			data->hdr_text_read_output = fopen(txt_read_output_b.data, "a");
			if (!data->hdr_text_read_output) {
				fprintf(stderr, "Unable to open %s in append mode, reason: %s\n",
						cmp_read_output_b.data, strerror(errno));
				ret = -1;
			}

			as_string_builder_destroy(&cmp_read_output_b);
			as_string_builder_destroy(&txt_read_output_b);

			hdr_init(1, 1000000, 3, &data->summary_read_hdr);
		}

		hdr_gettime(start_timespec);
	}
	return ret;
}


void free_histograms(clientdata* data, arguments* args)
{
	bool has_reads = stages_contains_reads(&data->stages);

	if (args->latency) {
		//latency_free(&data->write_latency);
		hdr_close(data->write_hdr);

		as_vector_destroy(&data->latency_percentiles);

		if (has_reads) {
			//latency_free(&data->read_latency);
			hdr_close(data->read_hdr);
		}
	}

	if (args->latency_histogram) {
		histogram_free(&data->write_histogram);
		
		if (has_reads) {
			histogram_free(&data->read_histogram);
		}

		if (args->histogram_output) {
			fclose(data->histogram_output);
		}
	}

	if (args->hdr_output) {
		hdr_close(data->summary_write_hdr);
		if (data->hdr_comp_write_output) {
			fclose(data->hdr_comp_write_output);
		}
		if (data->hdr_text_write_output) {
			fclose(data->hdr_text_write_output);
		}

		if (has_reads) {
			hdr_close(data->summary_read_hdr);
			if (data->hdr_comp_read_output) {
				fclose(data->hdr_comp_read_output);
			}
			if (data->hdr_text_read_output) {
				fclose(data->hdr_text_read_output);
			}
		}
	}
}


void record_summary_data(clientdata* data, arguments* args, time_t start_time,
		hdr_timespec* start_timespec) {
	static const int32_t ticks_per_half_distance = 5;
	bool has_reads = stages_contains_reads(&data->stages);

	// now record summary HDR hist if enabled
	if (args->hdr_output) {
		hdr_timespec end_timespec;
		hdr_gettime(&end_timespec);

		struct hdr_log_writer writer;
		hdr_log_writer_init(&writer);

		const char* utc_time = utc_time_str(start_time);
		hdr_log_write_header(&writer, data->hdr_comp_write_output,
				utc_time, start_timespec);

		hdr_log_write(&writer, data->hdr_comp_write_output,
				start_timespec, &end_timespec, data->summary_write_hdr);

		hdr_percentiles_print(data->summary_write_hdr, data->hdr_text_write_output,
				ticks_per_half_distance, 1., CLASSIC);

		if (has_reads) {
			hdr_log_write_header(&writer, data->hdr_comp_read_output,
					utc_time, start_timespec);

			hdr_log_write(&writer, data->hdr_comp_read_output,
					start_timespec, &end_timespec, data->summary_read_hdr);

			hdr_percentiles_print(data->summary_read_hdr, data->hdr_text_read_output,
					ticks_per_half_distance, 1., CLASSIC);
		}
	}
}


void* periodic_output_worker(void* udata)
{
	struct threaddata* tdata = (struct threaddata*) udata;
	clientdata* data = tdata->cdata;
	struct thr_coordinator* coord = tdata->coord;

	//latency* write_latency = &data->write_latency;
	//latency* read_latency = &data->read_latency;
	bool latency = data->latency;
	bool has_reads = stages_contains_reads(&data->stages);
	//char latency_header[500];
	//char latency_detail[500];
	uint64_t gen_count = 0;
	histogram* write_histogram = &data->write_histogram;
	histogram* read_histogram = &data->read_histogram;
	FILE* histogram_output = data->histogram_output;

	struct timespec wake_up;
	clock_gettime(COORD_CLOCK, &wake_up);

	uint64_t start_time = timespec_to_us(&wake_up);
	uint64_t time = start_time;
	uint64_t prev_time = start_time;
	uint64_t pause_us;

	// first indicate that this thread has no required work to do
	thr_coordinator_complete(coord);

	// throttle this thread to 1 event per second (1M microseconds)
	dyn_throttle_init(&tdata->dyn_throttle, 1000000);

	/*if (latency) {
		latency_set_header(write_latency, latency_header);
	}*/

	// when status is COORD_SLEEP_INTERRUPTED, that means it's time to halt this
	// stage and move onto the next one, but we want the logger to still print
	// out the last bit of latency data before moving onto the next stage, so
	// we always check the status at the beginning of the loop and update it
	// right after that
	int status;

	goto do_sleep;

	while (!as_load_uint8((uint8_t*) &tdata->finished)) {

		clock_gettime(COORD_CLOCK, &wake_up);
		time = timespec_to_us(&wake_up);

		int64_t elapsed = time - prev_time;
		prev_time = time;

		uint32_t write_current = as_fas_uint32(&data->write_count, 0);
		uint32_t write_timeout_current = as_fas_uint32(&data->write_timeout_count, 0);
		uint32_t write_error_current = as_fas_uint32(&data->write_error_count, 0);
		uint32_t read_current = as_fas_uint32(&data->read_count, 0);
		uint32_t read_timeout_current = as_fas_uint32(&data->read_timeout_count, 0);
		uint32_t read_error_current = as_fas_uint32(&data->read_error_count, 0);
		//uint64_t transactions_current = as_load_uint64(&data->transactions_count);

		data->period_begin = time;

		uint32_t write_tps = (uint32_t)((double)write_current * 1000000 / elapsed + 0.5);
		uint32_t read_tps = (uint32_t)((double)read_current * 1000000 / elapsed + 0.5);

		blog_info("write(tps=%d timeouts=%d errors=%d) ",
				write_tps, write_timeout_current, write_error_current);
		if (has_reads) {
			blog("read(tps=%d timeouts=%d errors=%d) ",
					read_tps, read_timeout_current, read_error_current);
		}
		blog_line("total(tps=%d timeouts=%d errors=%d)",
				write_tps + read_tps, write_timeout_current + read_timeout_current,
				write_error_current + read_error_current);

		if (latency) {
			/*blog_line("%s", latency_header);
			latency_print_results(write_latency, "write", latency_detail);
			blog_line("%s", latency_detail);
			latency_print_results(read_latency, "read", latency_detail);
			blog_line("%s", latency_detail);*/

			uint64_t elapsed_s = (time - start_time) / 1000000;
			print_hdr_percentiles(data->write_hdr, "write", elapsed_s,
					&data->latency_percentiles, stdout);
			if (has_reads) {
				print_hdr_percentiles(data->read_hdr,  "read",  elapsed_s,
						&data->latency_percentiles, stdout);
			}
		}

		++gen_count;

		if ((histogram_output != NULL) && ((gen_count % data->histogram_period) == 0)) {
			histogram_print_clear(write_histogram, data->histogram_period, histogram_output);
			if (has_reads) {
				histogram_print_clear(read_histogram, data->histogram_period, histogram_output);
			}
			fflush(histogram_output);
		}

		if (status == COORD_SLEEP_INTERRUPTED) {
			thr_coordinator_wait(coord);

			// check to make sure we're not finished before resetting everything
			if (!as_load_uint8((uint8_t*) &tdata->finished)) {
				// first indicate that this thread has no required work to do
				thr_coordinator_complete(coord);
				// so the logger doesn't immediately go back to waiting again
				status = COORD_SLEEP_TIMEOUT;

				// and lastly set the throttler to think it was called one
				// second ago (since we don't want the time spent at the
				// synchronization barrier to mess with it)
				clock_gettime(COORD_CLOCK, &wake_up);
				time = timespec_to_us(&wake_up);
				dyn_throttle_reset_time(&tdata->dyn_throttle, time);
			}
			else {
				// no need to check again
				break;
			}
		}

do_sleep:
		// sleep for 1 second
		pause_us = dyn_throttle_pause_for(&tdata->dyn_throttle, time);
		timespec_add_us(&wake_up, pause_us);
		status = thr_coordinator_sleep(coord, &wake_up);
	}
	return 0;
}

