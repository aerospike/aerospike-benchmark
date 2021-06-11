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

//==========================================================
// Includes.
//

#include <stdlib.h>
#include <time.h>

#include <aerospike/aerospike_info.h>
#include <aerospike/as_config.h>
#include <aerospike/as_event.h>
#include <aerospike/as_log.h>
#include <aerospike/as_monitor.h>
#include <aerospike/as_random.h>

#include <hdr_histogram/hdr_time.h>
#include <hdr_histogram/hdr_histogram_log.h>
#include <benchmark.h>
#include <common.h>
#include <latency_output.h>
#include <transaction.h>


//==========================================================
// Forward declarations.
//

LOCAL_HELPER bool as_client_log_cb(as_log_level level, const char* func,
		const char* file, uint32_t line, const char* fmt, ...);
LOCAL_HELPER int connect_to_server(args_t* args, aerospike* client);
LOCAL_HELPER bool is_single_bin(aerospike* client, const char* namespace);
LOCAL_HELPER tdata_t* init_tdata(cdata_t* cdata, thr_coord_t* coord,
		uint32_t t_idx);
LOCAL_HELPER void destroy_tdata(tdata_t* tdata);
LOCAL_HELPER int _run(cdata_t* cdata);


//==========================================================
// Public API.
//

int
run_benchmark(args_t* args)
{
	as_error err;

	cdata_t data;
	memset(&data, 0, sizeof(cdata_t));
	data.namespace = args->namespace;
	data.set = args->set;
	data.transaction_worker_threads = args->transaction_worker_threads;
	data.compression_ratio = args->compression_ratio;
	stages_move(&data.stages, &args->stages);
	data.latency = args->latency;
	data.debug = args->debug;
	data.async_max_commands = args->async_max_commands;

	time_t start_time;
	hdr_timespec start_timespec;

	if (args->debug) {
		as_log_set_level(AS_LOG_LEVEL_DEBUG);
	}
	else {
		as_log_set_level(AS_LOG_LEVEL_INFO);
	}

	as_log_set_callback(as_client_log_cb);

	int ret = connect_to_server(args, &data.client);

	if (ret != 0) {
		goto cleanup1;
	}

	bool single_bin = is_single_bin(&data.client, args->namespace);

	if (single_bin) {
		data.bin_name = "";

		if (obj_spec_n_bins(&args->obj_spec) > 1) {
			fprintf(stderr, "Single bin database, but obj_spec has > 1 bin\n");
			ret = -1;
		}
	}
	else {
		data.bin_name = args->bin_name;
	}

	if (initialize_histograms(&data, args, &start_time, &start_timespec) != 0) {
		ret = -1;
		goto cleanup2;
	}

	for (uint32_t i = 0; i < data.stages.n_stages && ret == 0; i++) {
		if (!obj_spec_bin_name_compatible(&data.stages.stages[i].obj_spec,
					data.bin_name)) {
			ret = -1;
			goto cleanup3;
		}
	}

	ret = _run(&data);

#ifdef __linux__
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif /* __linux__ */
	// don't worry, will be initialized (would have to make args const
	// to suppress)
	record_summary_data(&data, args, start_time, &start_timespec);
#ifdef __linux__
#pragma GCC diagnostic pop
#endif /* __linux__ */

cleanup3:
	free_histograms(&data, args);

cleanup2:
	aerospike_close(&data.client, &err);
	aerospike_destroy(&data.client);

	if (stages_contain_async(&args->stages)) {
		as_event_close_loops();
	}

cleanup1:
	free_workload_config(&data.stages);
	
	return ret;
}


//==========================================================
// Local helpers.
//

LOCAL_HELPER bool
as_client_log_cb(as_log_level level, const char* func, const char* file,
		uint32_t line, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	blog_detailv(level, fmt, ap);
	va_end(ap);
	printf("\n");
	return true;
}

LOCAL_HELPER int
connect_to_server(args_t* args, aerospike* client)
{
	if (stages_contain_async(&args->stages)) {
#if AS_EVENT_LIB_DEFINED
		if (! as_event_create_loops(args->event_loop_capacity)) {
			blog_error("Failed to create asynchronous event loops\n");
			return 2;
		}
#else
		blog_error("Must 'make EVENT_LIB=<libname>' to use asynchronous functions.\n");
		return 2;
#endif
	}
	
	as_config cfg;
	as_config_init(&cfg);
	
	if (! as_config_add_hosts(&cfg, args->hosts, args->port)) {
		blog_error("Invalid host(s) %s\n", args->hosts);
		return 3;
	}

	as_config_set_user(&cfg, args->user, args->password);
	cfg.use_shm = args->use_shm;
	cfg.conn_timeout_ms = 10000;
	cfg.login_timeout_ms = 10000;
	cfg.use_services_alternate = args->use_services_alternate;

	// Disable batch/scan/query thread pool because these commands are not used in benchmarks.
	cfg.thread_pool_size = 0;
	cfg.conn_pools_per_node = args->conn_pools_per_node;

	if (cfg.async_max_conns_per_node < (uint32_t)args->async_max_commands) {
		cfg.async_max_conns_per_node = args->async_max_commands;
	}

	as_policies* p = &cfg.policies;

	p->read.base.socket_timeout = args->read_socket_timeout;
	p->read.base.total_timeout = args->read_total_timeout;
	p->read.base.max_retries = args->max_retries;
	p->read.base.compress = args->enable_compression;
	p->read.replica = args->replica;
	p->read.read_mode_ap = args->read_mode_ap;
	p->read.read_mode_sc = args->read_mode_sc;

	p->write.base.socket_timeout = args->write_socket_timeout;
	p->write.base.total_timeout = args->write_total_timeout;
	p->write.base.max_retries = args->max_retries;
	p->write.base.compress = args->enable_compression;
	p->write.replica = args->replica;
	p->write.commit_level = args->write_commit_level;
	p->write.durable_delete = args->durable_deletes;

	p->operate.base.socket_timeout = args->write_socket_timeout;
	p->operate.base.total_timeout = args->write_total_timeout;
	p->operate.base.max_retries = args->max_retries;
	p->operate.base.compress = args->enable_compression;
	p->operate.replica = args->replica;
	p->operate.commit_level = args->write_commit_level;
	p->operate.durable_delete = args->durable_deletes;
	p->operate.read_mode_ap = args->read_mode_ap;
	p->operate.read_mode_sc = args->read_mode_sc;

	p->remove.base.socket_timeout = args->write_socket_timeout;
	p->remove.base.total_timeout = args->write_total_timeout;
	p->remove.base.max_retries = args->max_retries;
	p->remove.base.compress = args->enable_compression;
	p->remove.replica = args->replica;
	p->remove.commit_level = args->write_commit_level;
	p->remove.durable_delete = args->durable_deletes;

	p->batch.base.socket_timeout = args->read_socket_timeout;
	p->batch.base.total_timeout = args->read_total_timeout;
	p->batch.base.max_retries = args->max_retries;
	p->batch.base.compress = args->enable_compression;
	p->batch.replica = args->replica;
	p->batch.read_mode_ap = args->read_mode_ap;
	p->batch.read_mode_sc = args->read_mode_sc;

	p->info.timeout = 10000;

	// Transfer ownership of all heap allocated TLS fields via shallow copy.
	memcpy(&cfg.tls, &args->tls, sizeof(as_config_tls));
	cfg.auth_mode = args->auth_mode;

	aerospike_init(client, &cfg);
	
	as_error err;
	
	if (aerospike_connect(client, &err) != AEROSPIKE_OK) {
		blog_error("%s\n", err.message);
		aerospike_destroy(client);
		return 1;
	}
	return 0;
}

LOCAL_HELPER bool
is_single_bin(aerospike* client, const char* namespace)
{
	char filter[256];
	sprintf(filter, "namespace/%s", namespace);
	
	char* res = 0;
	as_error err;
	as_status rc = aerospike_info_any(client, &err, 0, filter, &res);
	bool single_bin = false;
	
	if (rc == AEROSPIKE_OK) {
		static const char* search = "single-bin=";
		char* p = strstr(res, search);
		
		if (p) {
			// The compiler (with -O3 flag) will know search is a literal and
			// optimize strlen accordingly.
			p += strlen(search);
			char* q = strchr(p, ';');
			
			if (q) {
				*q = 0;
				
				if (strcmp(p, "true") == 0) {
					single_bin = true;
				}
			}
		}
		free(res);
	}
	else {
		blog_error("Info request failed: %d - %s\n", err.code, err.message);
	}
	return single_bin;
}

/*
 * allocates and initializes a new threaddata struct, returning a pointer to it
 */
LOCAL_HELPER tdata_t*
init_tdata(cdata_t* cdata, thr_coord_t* coord,
		uint32_t t_idx)
{
	tdata_t* tdata = (tdata_t*) cf_malloc(sizeof(tdata_t));

	tdata->cdata = cdata;
	tdata->coord = coord;
	tdata->random = as_random_instance();
	tdata->t_idx = t_idx;
	// always start on the first stage
	tdata->stage_idx = 0;

	tdata->do_work = true;
	tdata->finished = false;

	return tdata;
}

LOCAL_HELPER void
destroy_tdata(tdata_t* tdata)
{
}

LOCAL_HELPER int
_run(cdata_t* cdata)
{
	int ret = 0;
	thr_coord_t coord;

	// first figure out how many threads we'll be spawning
	uint32_t n_threads;
	void* (*worker_fn)(void*);

	// output thread + all the worker threads
	n_threads = 1 + cdata->transaction_worker_threads;
	worker_fn = transaction_worker;

	// initialize the list of all thread pointers/data pointers
	tdata_t** tdatas = (tdata_t**) cf_malloc(n_threads * sizeof(tdata_t*));

	for (uint32_t i = 0; i < n_threads; i++) {
		tdatas[i] = init_tdata(cdata, &coord, i);
	}

	// pause before the first workload stage (using the logger thread's
	// as_random)
	stage_random_pause(tdatas[n_threads - 1]->random, &cdata->stages.stages[0]);

	// then initialize the thread coordinator struct, before spawning any
	// threads which will be referencing it
	thr_coordinator_init(&coord, n_threads);

	pthread_t* threads = (pthread_t*) cf_malloc(n_threads * sizeof(pthread_t));

	// then initialize periodic output thread
	tdata_t* out_worker_tdata = tdatas[n_threads - 1];
	if (pthread_create(&threads[n_threads - 1], NULL, periodic_output_worker,
				out_worker_tdata) != 0) {
		blog_error("Failed to create output thread\n");
		cf_free(threads);
		cf_free(tdatas);
		thr_coordinator_free(&coord);
		return -1;
	}

	// then create all the worker threads
	blog_info("Start %d transaction threads\n", n_threads - 1);

	uint32_t i;
	// since the output worker threaddata is in slot 0
	for (i = 0; i < n_threads - 1; i++) {
		tdata_t* tdata = tdatas[i];

		if (pthread_create(&threads[i], NULL, worker_fn, tdata) != 0) {
			blog_error("Failed to create transaction worker thread\n");
			ret = -1;

			// go to clean up the rest of the threads that have already
			// been spawned
			break;
		}
	}

	if (ret == 0) {
		struct coordinator_worker_args_s coord_args = {
			.coord = &coord,
			.cdata = cdata,
			.tdatas = tdatas,
			.n_threads = n_threads
		};
		// and now enter the coordinator funtion
		coordinator_worker(&coord_args);
	}

	i--;
	for (;;) {
		if (i >= n_threads) {
			// go back and free the logger thread
			i = n_threads - 1;
		}

		// by this point, if all went well, the coordinator thread should have
		// already closed all of these threads, but in the case that something
		// went wrong before we started the coordinator, we need to tell each
		// of these threads to exit
		if (ret != 0) {
			// make thread exit
			// note that we must update finished before do_work, since we don't
			// want any of the threads to enter the pthread barrier 
			as_store_uint8((uint8_t*) &tdatas[i]->finished, true);
			as_store_uint8((uint8_t*) &tdatas[i]->do_work, false);
		}
		pthread_join(threads[i], NULL);
		destroy_tdata(tdatas[i]);
		cf_free(tdatas[i]);

		if (i == n_threads - 1) {
			break;
		}
		i--;
	}

	cf_free(threads);
	cf_free(tdatas);
	thr_coordinator_free(&coord);

	return ret;
}

