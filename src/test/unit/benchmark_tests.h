#pragma once

#include <check.h>

Suite* setup_suite(void);
Suite* common_suite(void);
Suite* coordinator_suite(void);
Suite* dyn_throttle_suite(void);
Suite* sanity_suite(void);
Suite* hdr_histogram_suite(void);
Suite* hdr_histogram_log_suite(void);
Suite* histogram_suite(void);
Suite* obj_spec_suite(void);
Suite* yaml_parse_suite(void);
Suite* synth_data_suite(void);
Suite* synth_gen_suite(void);
Suite* workload_parse_suite(void);
Suite* cdt_ops_suite(void);
Suite* gen_bench_suite(void);

