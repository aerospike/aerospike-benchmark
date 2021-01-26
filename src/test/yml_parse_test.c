
#include <check.h>
#include <stdio.h>

#include <cyaml/cyaml.h>

#include <benchmark.h>
#include <workload.h>


#define TEST_SUITE_NAME "yaml"



Suite*
yaml_parse_suite(void)
{
	Suite* s;

	s = suite_create("Yaml");

	struct arguments_t args;
	struct stages stages;

	args.start_key = 1;
	args.keys = 1000000;

	if (parse_workload_config_file("src/test/test.yaml", &stages, &args) != 0) {
		return s;
	}

	stages_print(&stages);

	free_workload_config(&stages);

	return s;
}

