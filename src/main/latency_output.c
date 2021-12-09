
//==========================================================
// Includes
//

#include <latency_output.h>

#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include <aerospike/as_atomic.h>
#include <aerospike/as_string_builder.h>
#include <citrusleaf/cf_clock.h>

#include <hdr_histogram/hdr_histogram_log.h>

#include <common.h>
#include <transaction.h>


//==========================================================
// Public API.
//

int
initialize_histograms(cdata_t* cdata, args_t* args, time_t* start_time,
		hdr_timespec* start_timespec) {
	int ret = 0;
	bool has_writes = stages_contain_writes(&cdata->stages);
	bool has_reads = stages_contain_reads(&cdata->stages);
	bool has_udfs = stages_contain_udfs(&cdata->stages);

	cdata->histogram_period = args->histogram_period;

	if (args->latency) {
		as_vector_init(&cdata->latency_percentiles, args->latency_percentiles.item_size,
				args->latency_percentiles.capacity);
		for (uint32_t i = 0; i < args->latency_percentiles.size; i++) {
			as_vector_append(&cdata->latency_percentiles,
					as_vector_get(&args->latency_percentiles, i));
		}
	}
	
	if (args->latency_histogram) {
		if (args->histogram_output) {
			cdata->histogram_output = fopen(args->histogram_output, "a");
			if (!cdata->histogram_output) {
				fprintf(stderr, "Unable to open %s in append mode\n",
						args->histogram_output);
				ret = -1;
				// follow through with initialization, so cleanup won't segfault
			}
		}
		else {
			cdata->histogram_output = stdout;
		}

		if (has_writes) {
			histogram_init(&cdata->write_histogram, 3, 100, (rangespec_t[]) {
					{ .upper_bound = 4000,   .bucket_width = 100  },
					{ .upper_bound = 64000,  .bucket_width = 1000 },
					{ .upper_bound = 128000, .bucket_width = 4000 }
					});
			histogram_set_name(&cdata->write_histogram, "write_hist");
			histogram_print_info(&cdata->write_histogram, cdata->histogram_output);
		}
		
		if (has_reads) {
			histogram_init(&cdata->read_histogram, 3, 100, (rangespec_t[]) {
					{ .upper_bound = 4000,   .bucket_width = 100  },
					{ .upper_bound = 64000,  .bucket_width = 1000 },
					{ .upper_bound = 128000, .bucket_width = 4000 }
					});
			histogram_set_name(&cdata->read_histogram, "read_hist");
			histogram_print_info(&cdata->read_histogram, cdata->histogram_output);
		}

		if (has_udfs) {
			histogram_init(&cdata->udf_histogram, 3, 100, (rangespec_t[]) {
					{ .upper_bound = 4000,   .bucket_width = 100  },
					{ .upper_bound = 64000,  .bucket_width = 1000 },
					{ .upper_bound = 128000, .bucket_width = 4000 }
					});
			histogram_set_name(&cdata->udf_histogram, "udf_hist");
			histogram_print_info(&cdata->udf_histogram, cdata->histogram_output);
		}
	}
	
	if (args->hdr_output) {
		const static char write_output_prefix[] = "/write_";
		const static char read_output_prefix[] = "/read_";
		const static char udf_output_prefix[] = "/udf_";
		const static char compressed_output_suffix[] = ".hdrhist";
		const static char text_output_suffix[] = ".txt";

		*start_time = time(NULL);
		const char* utc_time = utc_time_str(*start_time);

		size_t prefix_len = strlen(args->hdr_output);

		if (has_writes) {
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

			cdata->hdr_comp_write_output = fopen(cmp_write_output_b.data, "a");
			if (!cdata->hdr_comp_write_output) {
				fprintf(stderr, "Unable to open %s in append mode, reason: %s\n",
						cmp_write_output_b.data, strerror(errno));
				ret = -1;
			}

			cdata->hdr_text_write_output = fopen(txt_write_output_b.data, "a");
			if (!cdata->hdr_text_write_output) {
				fprintf(stderr, "Unable to open %s in append mode, reason: %s\n",
						cmp_write_output_b.data, strerror(errno));
				ret = -1;
			}

			as_string_builder_destroy(&cmp_write_output_b);
			as_string_builder_destroy(&txt_write_output_b);
		}

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

			cdata->hdr_comp_read_output = fopen(cmp_read_output_b.data, "a");
			if (!cdata->hdr_comp_read_output) {
				fprintf(stderr, "Unable to open %s in append mode, reason: %s\n",
						cmp_read_output_b.data, strerror(errno));
				ret = -1;
			}

			cdata->hdr_text_read_output = fopen(txt_read_output_b.data, "a");
			if (!cdata->hdr_text_read_output) {
				fprintf(stderr, "Unable to open %s in append mode, reason: %s\n",
						cmp_read_output_b.data, strerror(errno));
				ret = -1;
			}

			as_string_builder_destroy(&cmp_read_output_b);
			as_string_builder_destroy(&txt_read_output_b);
		}

		if (has_udfs) {
			size_t udf_output_size =
				prefix_len + (sizeof(udf_output_prefix) - 1) +
				UTC_STR_LEN + (sizeof(compressed_output_suffix) - 1) + 1;

			as_string_builder cmp_udf_output_b;
			as_string_builder txt_udf_output_b;
			as_string_builder_inita(&cmp_udf_output_b, udf_output_size, false);
			as_string_builder_inita(&txt_udf_output_b, udf_output_size, false);

			as_string_builder_append(&cmp_udf_output_b, args->hdr_output);
			as_string_builder_append(&cmp_udf_output_b, udf_output_prefix);
			as_string_builder_append(&cmp_udf_output_b, utc_time);

			// duplicate the current buffer into txt (since only the extension differs
			as_string_builder_append(&txt_udf_output_b, cmp_udf_output_b.data);

			as_string_builder_append(&cmp_udf_output_b, compressed_output_suffix);
			as_string_builder_append(&txt_udf_output_b, text_output_suffix);

			cdata->hdr_comp_udf_output = fopen(cmp_udf_output_b.data, "a");
			if (!cdata->hdr_comp_udf_output) {
				fprintf(stderr, "Unable to open %s in append mode, reason: %s\n",
						cmp_udf_output_b.data, strerror(errno));
				ret = -1;
			}

			cdata->hdr_text_udf_output = fopen(txt_udf_output_b.data, "a");
			if (!cdata->hdr_text_udf_output) {
				fprintf(stderr, "Unable to open %s in append mode, reason: %s\n",
						cmp_udf_output_b.data, strerror(errno));
				ret = -1;
			}

			as_string_builder_destroy(&cmp_udf_output_b);
			as_string_builder_destroy(&txt_udf_output_b);
		}

		hdr_gettime(start_timespec);
	}

	if (args->latency || args->hdr_output) {
		if (has_writes) {
			hdr_init(1, 1000000, 3, &cdata->write_hdr);
		}
		if (has_reads) {
			hdr_init(1, 1000000, 3, &cdata->read_hdr);
		}
		if (has_udfs) {
			hdr_init(1, 1000000, 3, &cdata->udf_hdr);
		}
	}
	return ret;
}

void
free_histograms(cdata_t* cdata, args_t* args)
{
	bool has_writes = stages_contain_writes(&cdata->stages);
	bool has_reads = stages_contain_reads(&cdata->stages);
	bool has_udfs = stages_contain_udfs(&cdata->stages);

	if (args->latency) {
		as_vector_destroy(&cdata->latency_percentiles);
	}

	if (args->latency_histogram) {
		if (has_writes) {
			histogram_free(&cdata->write_histogram);
		}
		if (has_reads) {
			histogram_free(&cdata->read_histogram);
		}
		if (has_udfs) {
			histogram_free(&cdata->udf_histogram);
		}

		if (args->histogram_output) {
			fclose(cdata->histogram_output);
		}
	}

	if (args->hdr_output) {
		if (has_writes) {
			if (cdata->hdr_comp_write_output) {
				fclose(cdata->hdr_comp_write_output);
			}
			if (cdata->hdr_text_write_output) {
				fclose(cdata->hdr_text_write_output);
			}
		}

		if (has_reads) {
			if (cdata->hdr_comp_read_output) {
				fclose(cdata->hdr_comp_read_output);
			}
			if (cdata->hdr_text_read_output) {
				fclose(cdata->hdr_text_read_output);
			}
		}

		if (has_udfs) {
			if (cdata->hdr_comp_udf_output) {
				fclose(cdata->hdr_comp_udf_output);
			}
			if (cdata->hdr_text_udf_output) {
				fclose(cdata->hdr_text_udf_output);
			}
		}
	}

	if (args->latency || args->hdr_output) {
		if (has_writes) {
			hdr_close(cdata->write_hdr);
		}
		if (has_reads) {
			hdr_close(cdata->read_hdr);
		}
		if (has_udfs) {
			hdr_close(cdata->udf_hdr);
		}
	}
}

void
record_summary_data(cdata_t* cdata, args_t* args, time_t start_time,
		hdr_timespec* start_timespec) {
	static const int32_t ticks_per_half_distance = 5;
	bool has_writes = stages_contain_writes(&cdata->stages);
	bool has_reads = stages_contain_reads(&cdata->stages);
	bool has_udfs = stages_contain_udfs(&cdata->stages);

	// now record summary HDR hist if enabled
	if (args->hdr_output) {
		hdr_timespec end_timespec;
		hdr_gettime(&end_timespec);

		struct hdr_log_writer writer;
		hdr_log_writer_init(&writer);

		const char* utc_time = utc_time_str(start_time);

		if (has_writes) {
			hdr_log_write_header(&writer, cdata->hdr_comp_write_output,
					utc_time, start_timespec);

			hdr_log_write(&writer, cdata->hdr_comp_write_output,
					start_timespec, &end_timespec, cdata->write_hdr);

			hdr_percentiles_print(cdata->write_hdr, cdata->hdr_text_write_output,
					ticks_per_half_distance, 1., CLASSIC);
		}

		if (has_reads) {
			hdr_log_write_header(&writer, cdata->hdr_comp_read_output,
					utc_time, start_timespec);

			hdr_log_write(&writer, cdata->hdr_comp_read_output,
					start_timespec, &end_timespec, cdata->read_hdr);

			hdr_percentiles_print(cdata->read_hdr, cdata->hdr_text_read_output,
					ticks_per_half_distance, 1., CLASSIC);
		}

		if (has_udfs) {
			hdr_log_write_header(&writer, cdata->hdr_comp_udf_output,
					utc_time, start_timespec);

			hdr_log_write(&writer, cdata->hdr_comp_udf_output,
					start_timespec, &end_timespec, cdata->udf_hdr);

			hdr_percentiles_print(cdata->udf_hdr, cdata->hdr_text_udf_output,
					ticks_per_half_distance, 1., CLASSIC);
		}
	}
}

void*
periodic_output_worker(void* udata)
{
	tdata_t* tdata = (tdata_t*) udata;
	cdata_t* cdata = tdata->cdata;
	thr_coord_t* coord = tdata->coord;

	bool latency = cdata->latency;
	bool has_writes = stages_contain_writes(&cdata->stages);
	bool has_reads = stages_contain_reads(&cdata->stages);
	bool has_udfs = stages_contain_udfs(&cdata->stages);
	uint64_t gen_count = 0;
	histogram_t* write_histogram = &cdata->write_histogram;
	histogram_t* read_histogram = &cdata->read_histogram;
	histogram_t* udf_histogram = &cdata->udf_histogram;
	FILE* histogram_output = cdata->histogram_output;

	struct timespec wake_up;
	clock_gettime(COORD_CLOCK, &wake_up);

	uint64_t start_time = timespec_to_us(&wake_up);
	uint64_t time = start_time;
	uint64_t prev_time = start_time;
	uint64_t prev_time_hist = start_time;
	uint64_t pause_us;

	// first indicate that this thread has no required work to do
	thr_coordinator_complete(coord);

	// throttle this thread to 1 event per second (1M microseconds)
	dyn_throttle_init(&tdata->dyn_throttle, 1000000);

	// when status is COORD_SLEEP_INTERRUPTED, that means it's time to halt this
	// stage and move onto the next one, but we want the logger to still print
	// out the last bit of latency data before moving onto the next stage, so
	// we always check the status at the beginning of the loop and update it
	// right after that
	int status;

	// set to true when this is the first time logging latency data for the
	// current stage
	bool first_log_of_stage = true;

	goto do_sleep;

	while (!as_load_uint8((uint8_t*) &tdata->finished)) {

		clock_gettime(COORD_CLOCK, &wake_up);
		time = timespec_to_us(&wake_up);

		int64_t elapsed = time - prev_time;
		prev_time = time;

		uint64_t write_current = as_fas_uint64(&cdata->write_count, 0);
		uint64_t write_timeout_current = as_fas_uint64(&cdata->write_timeout_count, 0);
		uint64_t write_error_current = as_fas_uint64(&cdata->write_error_count, 0);
		uint64_t read_current = as_fas_uint64(&cdata->read_count, 0);
		uint64_t read_timeout_current = as_fas_uint64(&cdata->read_timeout_count, 0);
		uint64_t read_error_current = as_fas_uint64(&cdata->read_error_count, 0);
		uint64_t udf_current = as_fas_uint64(&cdata->udf_count, 0);
		uint64_t udf_timeout_current = as_fas_uint64(&cdata->udf_timeout_count, 0);
		uint64_t udf_error_current = as_fas_uint64(&cdata->udf_error_count, 0);

		cdata->period_begin = time;

		uint64_t write_tps = (uint64_t)((double)write_current * 1000000 / elapsed + 0.5);
		uint64_t read_tps = (uint64_t)((double)read_current * 1000000 / elapsed + 0.5);
		uint64_t udf_tps = (uint64_t)((double)udf_current * 1000000 / elapsed + 0.5);

		bool any_records = write_current + write_timeout_current + write_error_current +
			read_current + read_timeout_current + read_error_current +
			udf_current + udf_timeout_current + udf_error_current != 0;
		if (any_records) {
			blog_info("");
			if (has_writes) {
				printf("write(tps=%" PRId64 " timeouts=%" PRId64 " errors=%" PRId64 ") ",
						write_tps, write_timeout_current, write_error_current);
			}
			if (has_reads) {
				printf("read(tps=%" PRId64 " timeouts=%" PRId64 " errors=%" PRId64 ") ",
						read_tps, read_timeout_current, read_error_current);
			}
			if (has_udfs) {
				printf("udf(tps=%" PRId64 " timeouts=%" PRId64 " errors=%" PRId64 ") ",
						udf_tps, udf_timeout_current, udf_error_current);
			}
			printf("total(tps=%" PRId64 " timeouts=%" PRId64 " errors=%" PRId64 ")\n",
					write_tps + read_tps + udf_tps,
					write_timeout_current + read_timeout_current + udf_timeout_current,
					write_error_current + read_error_current + udf_error_current);
		}

		++gen_count;

		// print latency information at the very end of the stage no matter what
		if (status == COORD_SLEEP_INTERRUPTED ||
				((gen_count % cdata->histogram_period) == 0)) {
			int64_t elapsed_hist = time - prev_time_hist;
			prev_time_hist = time;

			if (any_records) {
				if (latency) {
					uint64_t elapsed_s = (time - start_time) / 1000000;

					if (has_writes) {
						print_hdr_percentiles(cdata->write_hdr, "write", elapsed_s,
								&cdata->latency_percentiles, stdout);
					}

					if (has_reads) {
						print_hdr_percentiles(cdata->read_hdr,  "read",  elapsed_s,
								&cdata->latency_percentiles, stdout);
					}

					if (has_udfs) {
						print_hdr_percentiles(cdata->udf_hdr,  "udf",  elapsed_s,
								&cdata->latency_percentiles, stdout);
					}
				}
				if (histogram_output != NULL) {
					if (first_log_of_stage) {
						fprint_stage(histogram_output, &cdata->stages,
								tdata->stage_idx);
					}

					if (has_writes) {
						histogram_print_clear(write_histogram, elapsed_hist,
								histogram_output);
					}

					if (has_reads) {
						histogram_print_clear(read_histogram, elapsed_hist,
								histogram_output);
					}

					if (has_udfs) {
						histogram_print_clear(udf_histogram, elapsed_hist,
								histogram_output);
					}

					fflush(histogram_output);
				}
			}
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

				prev_time = time;
				prev_time_hist = time;
				gen_count = 0;
			}
			else {
				// no need to check again
				break;
			}

			first_log_of_stage = true;
		}
		else {
			first_log_of_stage = false;
		}

do_sleep:
		// sleep for 1 second
		pause_us = dyn_throttle_pause_for(&tdata->dyn_throttle, time);
		timespec_add_us(&wake_up, pause_us);
		status = thr_coordinator_sleep(coord, &wake_up);
	}
	return 0;
}

