
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <x86intrin.h>

#include "aerospike/as_random.h"

#include "histogram.h"

#define HIST_ASSERT(expr) \
	assert(expr)

#define N_INSERTIONS 10000000


int main(int argc, char * argv[])
{
	histogram h;

	// sequential test

	histogram_init(&h, 3, 100, (rangespec_t[]) {
			{ .upper_bound = 4000,   .bucket_width = 100  },
			{ .upper_bound = 64000,  .bucket_width = 1000 },
			{ .upper_bound = 128000, .bucket_width = 4000 }
			});

	for (delay_t us = 1; us < 128500; us++) {
		histogram_add(&h, us);
	}


	HIST_ASSERT(h.underflow_cnt == 99);
	HIST_ASSERT(h.overflow_cnt == 500);

	// 115 buckets in total
	for (size_t i = 0; i < 115; i++) {
		if (i < 39) {
			HIST_ASSERT(h.buckets[i] == 100);
		}
		else if (i < 99) {
			HIST_ASSERT(h.buckets[i] == 1000);
		}
		else {
			HIST_ASSERT(h.buckets[i] == 4000);
		}
	}


	histogram_clear(&h);

	// timed randomized test
	as_random * r = as_random_instance();

	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC, &start);
	uint64_t c1 = __rdtsc();
	for (size_t cnt = 0; cnt < N_INSERTIONS; cnt++) {
		delay_t delay = as_random_next_uint64(r);
		delay %= 129000;
		histogram_add(&h, delay);
	}
	uint64_t c2 = __rdtsc();
	clock_gettime(CLOCK_MONOTONIC, &end);

	printf("Randomized test on %d insertions: %f s\n",
			N_INSERTIONS,
			((end.tv_sec  - start.tv_sec) +
			 (end.tv_nsec - start.tv_nsec) * 0.000000001));
	printf("clock cycles/insertion: %f\n",
			(c2 - c1) / ((double) N_INSERTIONS));
	histogram_print_info(&h, stdout);
	histogram_print(&h, 1, stdout);

	histogram_free(&h);
	return 0;
}


