
#include <check.h>
#include <stdio.h>

#include <cyaml/cyaml.h>

#include <benchmark.h>
#include <object_spec.h>
#include <workload.h>


#define TEST_SUITE_NAME "yaml"

#define TMP_FILE_LOC "/tmp"


static void assert_workloads_eq(const struct stages* parsed,
		const struct stages* expected)
{
	ck_assert_int_eq(parsed->n_stages, expected->n_stages);
	for (uint32_t i = 0; i < parsed->n_stages; i++) {
		struct stage* a = &parsed->stages[i];
		struct stage* b = &expected->stages[i];

		ck_assert_uint_eq(a->duration, b->duration);
		ck_assert_str_eq(a->desc, b->desc);
		ck_assert_uint_eq(a->tps, b->tps);
		ck_assert_uint_eq(a->key_start, b->key_start);
		ck_assert_uint_eq(a->key_end, b->key_end);
		ck_assert_uint_eq(a->pause, b->pause);
		ck_assert_uint_eq(a->batch_size, b->batch_size);
		ck_assert(a->async == b->async);
		ck_assert(a->random == b->random);

		ck_assert_uint_eq(a->workload.type, b->workload.type);
		if (a->workload.type == WORKLOAD_TYPE_RANDOM) {
			ck_assert_float_eq(a->workload.pct, b->workload.pct);
		}

		char buf[1024];
		snprint_obj_spec(&a->obj_spec, buf, sizeof(buf));
		ck_assert_str_eq(buf, b->obj_spec_str);

		if (a->read_bins == NULL || b->read_bins == NULL) {
			ck_assert_ptr_eq(a->read_bins, NULL);
			ck_assert_ptr_eq(b->read_bins, NULL);
		}
		else {
			uint32_t j;
			for (j = 0; a->read_bins[j] != NULL && b->read_bins[j] != NULL; j++) {
				ck_assert_str_eq(a->read_bins[j], b->read_bins[j]);
			}
			ck_assert_ptr_eq(a->read_bins[j], NULL);
			ck_assert_ptr_eq(b->read_bins[j], NULL);
		}
	}
}


#define DEFINE_TEST(test_name, file_contents, stages_struct) \
START_TEST(test_name) \
{ \
	FILE* tmp = fopen(TMP_FILE_LOC "/test.yml", "w+");		\
	ck_assert_ptr_ne(tmp, NULL);							\
	struct stages expected = stages_struct;					\
	struct stages parsed;									\
	arguments args;											\
	\
	args.start_key = 1;										\
	args.keys = 100000;										\
	args.bin_name = "testbin";								\
	obj_spec_parse(&args.obj_spec, "I");					\
	\
	fwrite(file_contents, 1, sizeof(file_contents) - 1,		\
			tmp);											\
	fclose(tmp);											\
	ck_assert_int_eq(parse_workload_config_file(			\
				TMP_FILE_LOC "/test.yml", &parsed,			\
				&args), 0);									\
	assert_workloads_eq(&parsed, &expected);				\
	\
	free_workload_config(&parsed);							\
	remove(TMP_FILE_LOC "/test.yml");						\
} \
END_TEST


DEFINE_TEST(test_simple,
		"- stage: 1\n"
		"  desc: \"test stage\"\n"
		"  duration: 20\n"
		"  workload: I",
		((struct stages) {
			(struct stage[]) {{
				.duration = 20,
				.desc = "test stage",
				.tps = 0,
				.key_start = 1,
				.key_end = 100001,
				.pause = 0,
				.batch_size = 1,
				.async = false,
				.random = false,
				.workload = (struct workload) {
					.type = WORKLOAD_TYPE_LINEAR,
				},
				.obj_spec_str = "I4",
				.read_bins = NULL
			},},
			1,
			true
		}));


Suite*
yaml_parse_suite(void)
{
	Suite* s;
	TCase* tc_simple;

	s = suite_create("Yaml");

	tc_simple = tcase_create("Simple");
	tcase_add_test(tc_simple, test_simple);
	suite_add_tcase(s, tc_simple);

	return s;
}

