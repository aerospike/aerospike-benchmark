
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


#define N_TRIALS 100
#define TARGET_FREQ 100000


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

static float var(uint64_t *times, uint32_t n_times)
{
	float u = mean(times, n_times);
	double tot = 0;
	for (uint32_t i = 0; i < n_times; i++) {
		double diff = (times[i] - u);
		tot += diff * diff;
	}
	return (double) (tot / (n_times - 1));
}


/*
 * calculate the baseline standard deviation of the throttler when only pausing,
 * which is essentially measuring the standard deviation of usleep
 */
static float calc_baseline_var(float target_freq, uint32_t n_trials,
		rand_sleep_time_fn rst_fn)	
{
	dyn_throttle_t thr;
	as_random r;

	dyn_throttle_init(&thr, target_freq);
	as_random_init(&r);

	uint64_t* diffs = (uint64_t*) cf_malloc(n_trials * sizeof(uint64_t));
	uint64_t* rst_results = (uint64_t*) cf_malloc(n_trials * sizeof(uint64_t));

	uint64_t last_time = cf_getus();
	uint64_t us_pause = dyn_throttle_pause_for(&thr, last_time);
	(void) us_pause;
	usleep((uint32_t) target_freq);

	for (uint32_t i = 0; i < n_trials; i++) {
		float y = (float) (as_random_next_uint32(&r) / ((double) UINT32_MAX));
		uint64_t sleep_time = rst_fn(y);
		(void) sleep_time;
		rst_results[i] = sleep_time;
		usleep(0);

		uint64_t t = cf_getus();
		diffs[i] = t - last_time;

		us_pause = dyn_throttle_pause_for(&thr, t);
		(void) us_pause;
		last_time = t;
		usleep(us_pause);//(uint32_t) target_freq);
	}
	float baseline_var = var(diffs, n_trials);

	cf_free(rst_results);
	cf_free(diffs);
	return baseline_var;
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

	float baseline_var = calc_baseline_var(target_freq, n_trials, rst_fn);

	dyn_throttle_init(&thr, target_freq);
	as_random_init(&r);

	uint64_t* diffs = (uint64_t*) cf_malloc(n_trials * sizeof(uint64_t));
	uint64_t* rst_results = (uint64_t*) cf_malloc(n_trials * sizeof(uint64_t));

	uint64_t last_time = cf_getus();
	uint64_t us_pause = dyn_throttle_pause_for(&thr, last_time);
	usleep(us_pause);

	for (uint32_t i = 0; i < n_trials; i++) {

		float y = (float) (as_random_next_uint32(&r) / ((double) UINT32_MAX));
		uint64_t sleep_time = rst_fn(y);
		rst_results[i] = sleep_time;
		usleep(sleep_time);

		uint64_t t = cf_getus();
		diffs[i] = t - last_time;

		us_pause = dyn_throttle_pause_for(&thr, t);
		last_time = t;

		usleep(us_pause);
	}
	float u = mean(diffs, n_trials);
	float v = var(diffs, n_trials);

	float u_rst = mean(rst_results, n_trials);
	float v_rst = var(rst_results, n_trials);

	// the amount of variance that's accounted for by usleep
	float frac = 1 - (u_rst / u);
	float usleep_var_estimate = (frac * frac) * baseline_var;

	float u_error = (u - target_freq) / target_freq;

	float stddev_error = (sqrtf(v) - sqrtf(v_rst + usleep_var_estimate)) /
				sqrtf(v_rst + usleep_var_estimate);

	/*
	printf("trial run:\n\tvar: %f\n", baseline_var);
	printf("measured:\n\tmean: %f\n\tvar: %f\n", u, v);
	printf("from fn:\n\tmean: %f\n\tvar: %f\n", u_rst, v_rst);
	printf("usleep_var_estimate: %f\n", usleep_var_estimate);

	printf("u var: %f\n", v);
	printf("r var: %f\n", v_rst + usleep_var_estimate);

	printf("u stddev: %f\n", sqrtf(v));
	printf("r stddev: %f\n", sqrtf(v_rst + usleep_var_estimate));
	printf("error: %f\n", stddev_error);*/

	// percent error between the expected mean frequency and requested frequency
	// must be low
	ck_assert_float_eq_tol(u_error, 0, 0.01f);
	// percent error between the expected variance (calculated by accounting
	// for variance in usleep and variance in the random sleep time function)
	// and the measured variance
	ck_assert_float_eq_tol(stddev_error, 0, 0.15f);

	cf_free(rst_results);
	cf_free(diffs);
	dyn_throttle_free(&thr);
}


uint64_t zero_fn(float y)
{
	return 0;
}

START_TEST(test_zero_distribution)
{
	run_test(10000, 1000, zero_fn);
}
END_TEST


uint64_t triangle_fn(float y)
{
	uint32_t s;
	if (y < .5f) {
		s = (uint32_t) nearbyintf(2500 * sqrtf(2 * y) + 2500);
	}
	else {
		s = (uint32_t) nearbyintf(-2500 * sqrtf(2 - 2 * y) + 7500);
	}
	return s;
}


START_TEST(test_triangle_distribution)
{
	run_test(10000, 1000, triangle_fn);
}
END_TEST


uint64_t uniform_fn(float y)
{
	return ((uint64_t) nearbyintf(5000 * y)) + 2500;
}


START_TEST(test_uniform_distribution)
{
	run_test(10000, 1000, uniform_fn);
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

