
#include <assert.h>
#include <stdio.h>

#include "histogram.h"

#define HIST_ASSERT(expr) \
	assert(expr)


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

	histogram_print(&h);

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


	histogram_free(&h);
	return 0;
}


