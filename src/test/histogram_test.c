
#include <stdio.h>

#include "histogram.h"


int main(int argc, char * argv[])
{
	histogram h;

	histogram_init(&h, 3, 100, (rangespec_t[]) {
			{ .upper_bound = 4000,   .bucket_width = 100  },
			{ .upper_bound = 64000,  .bucket_width = 1000 },
			{ .upper_bound = 128000, .bucket_width = 4000 }
			});

	histogram_print(&h);

	histogram_free(&h);
	return 0;
}


