/*
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
#include "benchmark_tests.h"


static SRunner* g_sr;

void
set_fork_status(enum fork_status status)
{
	srunner_set_fork_status(g_sr, status);
}

int 
main(void) {
	int number_failed;
	Suite* s;

	s = sanity_suite();
	g_sr = srunner_create(s);
	//srunner_add_suite(g_sr, setup_suite());
	//srunner_add_suite(g_sr, common_suite());
	//srunner_add_suite(g_sr, coordinator_suite());
	//srunner_add_suite(g_sr, dyn_throttle_suite());
	//srunner_add_suite(g_sr, hdr_histogram_suite());
	//srunner_add_suite(g_sr, hdr_histogram_log_suite());
	//srunner_add_suite(g_sr, histogram_suite());
	srunner_add_suite(g_sr, obj_spec_suite());
	//srunner_add_suite(g_sr, yaml_parse_suite());

	srunner_set_fork_status(g_sr, CK_NOFORK);

	srunner_run_all(g_sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(g_sr);
	srunner_free(g_sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
