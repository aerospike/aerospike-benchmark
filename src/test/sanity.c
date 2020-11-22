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
	args->start_key = 1;
	args->keys = 1000000;
	args->numbins = 1;
	args->bintype = 'I';
	args->binlen = 50;
	args->binlen_type = LEN_TYPE_COUNT;
	args->random = false;
	args->transactions_limit = 0;
	args->init = false;
	args->init_pct = 100;
	args->read_pct = 50;
	args->del_bin = false;
	args->threads = 16;
	args->throughput = 0;
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
	args->latency_columns = 4;
	args->latency_shift = 3;
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
	data->threads = args->threads;
	data->throughput = args->throughput;
	data->batch_size = args->batch_size;
	data->read_pct = args->read_pct;
	data->del_bin = args->del_bin;
	data->compression_ratio = args->compression_ratio;
	data->bintype = args->bintype;
	data->binlen = args->binlen;
	data->binlen_type = args->binlen_type;
	data->numbins = args->numbins;
	data->random = args->random;
	data->transactions_limit = args->transactions_limit;
	data->transactions_count = 0;
	data->latency = args->latency;
	data->debug = args->debug;
	data->valid = 1;
	data->async = args->async;
	data->async_max_commands = args->async_max_commands;
	data->fixed_value = NULL;
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
	free(args);
	free(data);
}

/**
 * Really make sure five is represented accurately
 */
START_TEST(test_sanity)
{
	uint16_t five = 5;
	char *five_str = "five";
	ck_assert_int_eq(five, 5);
	ck_assert_str_eq("five", five_str);
}
END_TEST

/**
 * The thing about 5 is that it is always trying to be tricky. Here
 * we double check that five is for sure 5 and "five"
 */
START_TEST(test_still_sane) 
{
	uint16_t five = 5;
	char* five_str = "five";

	ck_assert_msg(five == 5 && strcmp(five_str, "five") == 0,
					"five should be 5 and five_str should be \"five\"");

}
END_TEST

/**
 * We went through all of the trouble to set up and tear down. We had
 * better test it a little.
 */
START_TEST(test_init_sane) {
	latency_init(&data->read_latency, args->latency_columns, args->latency_shift);
	ck_assert_int_eq(data->read_latency.last_bucket, 3);
	latency_free(&data->read_latency);
}
END_TEST

static Suite*
sanity_suite(void) {
	Suite* s;
	TCase* tc_core;

	s = suite_create("Sanity");

	/* Core test case */
	tc_core = tcase_create("Core");
	tcase_add_checked_fixture(tc_core, setup, teardown);
	tcase_add_test(tc_core, test_sanity);
	tcase_add_test(tc_core, test_still_sane);
	tcase_add_test(tc_core, test_init_sane);	
	suite_add_tcase(s, tc_core);

	return s;
}

int 
main(void) {
	int number_failed;
	Suite* s;
	SRunner* sr;

	s = sanity_suite();
	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
