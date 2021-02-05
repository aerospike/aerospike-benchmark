/*
 * Check: a unit test framework for C
 * Copyright (C) 2001, 2002 Arien Malec
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <stdlib.h>
#include <check.h>
#include "benchmark.h"

#define TEST_SUITE_NAME "Setup"
arguments *args;
clientdata *data;

/**
 * Any setup code to run each test goes here
 *
 * @return  void  
 */
static void 
setup(void) 
{
	args = cf_malloc(sizeof(arguments));
	args->hosts = strdup("127.0.0.1");
	args->port = 3000;
	args->user = 0;
	args->password[0] = 0;
	args->namespace = "test";
	args->set = "testset";
	args->transaction_worker_threads = 16;
	args->batch_size = 0;
	args->enable_compression = false;
	args->compression_ratio = 1.f;
	args->read_socket_timeout = AS_POLICY_SOCKET_TIMEOUT_DEFAULT;
	args->write_socket_timeout = AS_POLICY_SOCKET_TIMEOUT_DEFAULT;
	args->read_total_timeout = AS_POLICY_TOTAL_TIMEOUT_DEFAULT;
	args->write_total_timeout = AS_POLICY_TOTAL_TIMEOUT_DEFAULT;
	args->max_retries = 1;
	args->debug = false;
	args->latency = false;
	args->use_shm = false;
	args->replica = AS_POLICY_REPLICA_SEQUENCE;
	args->read_mode_ap = AS_POLICY_READ_MODE_AP_ONE;
	args->read_mode_sc = AS_POLICY_READ_MODE_SC_SESSION;
	args->write_commit_level = AS_POLICY_COMMIT_LEVEL_ALL;
	args->durable_deletes = false;
	args->conn_pools_per_node = 1;
	args->async = false;
	args->async_max_commands = 50;
	args->event_loop_capacity = 1;
	args->auth_mode = AS_AUTH_INTERNAL;	
	
	data = cf_malloc(sizeof(clientdata));
	data->namespace = args->namespace;
	data->set = args->set;
	data->transaction_worker_threads = args->transaction_worker_threads;
	data->batch_size = args->batch_size;
	data->compression_ratio = args->compression_ratio;
	data->transactions_count = 0;
	data->latency = args->latency;
	data->debug = args->debug;
	data->async = args->async;
	data->async_max_commands = args->async_max_commands;
}

/**
 * setup's friend. Does the opposite and cleans up the test
 *
 * @return  void  
 */
static void
teardown(void)
{
//do some tearing
	free(args->hosts);
	free(args);
	free(data);
}

/**
 * We went through all of the trouble to set up and tear down. We had
 * better test it a little.
 */
START_TEST(test_setup) {
	histogram_init(&data->read_histogram, 3, 100, (rangespec_t[]) {
			{ .upper_bound = 4000,   .bucket_width = 100  },
			{ .upper_bound = 64000,  .bucket_width = 1000 },
			{ .upper_bound = 128000, .bucket_width = 4000 }
			});
	ck_assert_int_eq(data->read_histogram.underflow_cnt, 0);
	histogram_free(&data->read_histogram);
}
END_TEST

Suite*
setup_suite(void) {
	Suite* s;
	TCase* tc_core;

	s = suite_create(TEST_SUITE_NAME);

	/* Core test case */
	tc_core = tcase_create("Core");
	tcase_add_checked_fixture(tc_core, setup, teardown);
	tcase_add_test(tc_core, test_setup);	
	suite_add_tcase(s, tc_core);

	return s;
}
