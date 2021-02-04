
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <citrusleaf/cf_clock.h>
#include <aerospike/as_random.h>

#include <common.h>
#include <dynamic_throttle.h>


#define TEST_SUITE_NAME "dynamic throttle"


/*
 * given 0 <= alpha < 1, returns an amount of time to sleep in microseconds
 */
typedef uint64_t (*rand_sleep_time_fn)(float alpha);


static float mean(uint64_t *times, uint32_t n_times)
{
	uint64_t tot = 0;
	for (uint32_t i = 0; i < n_times; i++) {
		tot += times[i];
	}
	return (float) (((double) tot) / n_times);
}


/*
 * runs a test in which rst_fn is used to calculate a random amount of time to
 * sleep
 */
static void run_test(float target_freq, uint32_t n_trials,
		rand_sleep_time_fn rst_fn)
{
	dyn_throttle_t thr;
	as_random r;

	dyn_throttle_init(&thr, target_freq);
	as_random_init(&r);

	uint64_t* diffs = (uint64_t*) cf_malloc(n_trials * sizeof(uint64_t));

	uint64_t last_time = cf_getus();
	uint64_t us_pause = dyn_throttle_pause_for(&thr, last_time);
	usleep(us_pause);

	for (uint32_t i = 0; i < n_trials; i++) {

		float y = (float) (as_random_next_uint32(&r) / ((double) UINT32_MAX));
		uint64_t sleep_time = rst_fn(y);
		usleep(sleep_time);

		uint64_t t = cf_getus();
		diffs[i] = t - last_time;

		us_pause = dyn_throttle_pause_for(&thr, t);
		last_time = t;

		usleep(us_pause);
	}
	float u = mean(diffs, n_trials);
	float u_error = (u - target_freq) / target_freq;

	// percent error between the expected mean frequency and requested frequency
	// must be low
	ck_assert_float_eq_tol(u_error, 0, 0.01f);

	cf_free(diffs);
	dyn_throttle_free(&thr);
}


uint64_t zero_fn(float y)
{
	return 0;
}

START_TEST(test_zero_distribution)
{
	run_test(1000, 1000, zero_fn);
}
END_TEST


uint64_t triangle_fn(float y)
{
	uint32_t s;
	if (y < .5f) {
		s = (uint32_t) nearbyintf(250 * sqrtf(2 * y) + 250);
	}
	else {
		s = (uint32_t) nearbyintf(-250 * sqrtf(2 - 2 * y) + 750);
	}
	return s;
}


START_TEST(test_triangle_distribution)
{
	run_test(1000, 1000, triangle_fn);
}
END_TEST


uint64_t uniform_fn(float y)
{
	return ((uint64_t) nearbyintf(500 * y)) + 250;
}


START_TEST(test_uniform_distribution)
{
	run_test(1000, 1000, uniform_fn);
}
END_TEST


Suite*
dyn_throttle_suite(void)
{
	Suite* s;
	TCase* tc_core;

	s = suite_create("Dynamic Throttle");

	tc_core = tcase_create("Core");
	tcase_add_test(tc_core, test_zero_distribution);
	tcase_add_test(tc_core, test_triangle_distribution);
	tcase_add_test(tc_core, test_uniform_distribution);
	suite_add_tcase(s, tc_core);

	return s;
}

