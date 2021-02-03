
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <citrusleaf/cf_clock.h>
#include <aerospike/as_random.h>

#include "dynamic_throttle.h"


#define TEST_SUITE_NAME "dynamic throttle"


static float mean(uint64_t *times, uint32_t n_times)
{
	uint64_t tot = 0;
	for (uint32_t i = 0; i < n_times; i++) {
		tot += times[i];
	}
	return ((float) tot) / n_times;
}

static float std_dev(uint64_t *times, uint32_t n_times)
{
	float u = mean(times, n_times);
	uint64_t tot = 0;
	for (uint32_t i = 0; i < n_times; i++) {
		float diff = (times[i] - u);
		tot += diff * diff;
	}
	return sqrtf(((float) tot) / n_times);
}


static void do_stuff()
{
	dyn_throttle_t thr;
	as_random r;

	float target_freq = 100000;
	dyn_throttle_init(&thr, target_freq);

	as_random_init(&r);

	uint64_t last_time = cf_getus();
	uint64_t us_pause = dyn_throttle_pause_for(&thr, last_time);
	usleep(us_pause);

	uint64_t diffs[100];

	for (uint32_t i = 0; i < 100; i++) {

		float y = (float) (as_random_next_uint32(&r) / ((double) UINT32_MAX));
		uint32_t s;
		if (y < .5f) {
			s = (uint32_t) nearbyintf(50000 * sqrtf(y) + 25000);
		}
		else {
			s = (uint32_t) nearbyintf(-50000 * sqrtf(1 - y) + 75000);
		}
		usleep(s);

		uint64_t t = cf_getus();
		printf("go %f (%f)\n", (t - last_time) / 1000000., thr.avg_fn_delay);
		diffs[i] = t - last_time;

		us_pause = dyn_throttle_pause_for(&thr, t);
		last_time = t;

		usleep(us_pause);
	}
	printf("mean: %f\nstd dev: %f\n",
			mean(diffs, 100), std_dev(diffs, 100));

	dyn_throttle_free(&thr);
}


Suite*
dyn_throttle_suite(void)
{
	Suite* s;
	TCase* tc_core;

	s = suite_create("Dynamic Throttle");

	//do_stuff();
	tc_core = tcase_create("Core");
	suite_add_tcase(s, tc_core);

	return s;
}

