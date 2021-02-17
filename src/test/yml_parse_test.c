
#include <check.h>
#include <stdio.h>

#include <cyaml/cyaml.h>

#include <benchmark.h>
#include <object_spec.h>
#include <workload.h>


#define TEST_SUITE_NAME "yaml"

#define TMP_FILE_LOC "/tmp"


static void assert_workloads_eq(const stages_t* parsed,
		const stages_t* expected)
{
	ck_assert_int_eq(parsed->n_stages, expected->n_stages);
	for (uint32_t i = 0; i < parsed->n_stages; i++) {
		stage_t* a = &parsed->stages[i];
		stage_t* b = &expected->stages[i];

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

		ck_assert_uint_eq(a->n_read_bins, b->n_read_bins);
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

		ck_assert_uint_eq(a->n_write_bins, b->n_write_bins);
		if (a->write_bins == NULL || b->write_bins == NULL) {
			ck_assert_ptr_eq(a->write_bins, NULL);
			ck_assert_ptr_eq(b->write_bins, NULL);
		}
		else {
			uint32_t j;
			for (j = 0; a->write_bins[j] != NULL && b->write_bins[j] != NULL; j++) {
				ck_assert_str_eq(a->write_bins[j], b->write_bins[j]);
			}
			ck_assert_ptr_eq(a->write_bins[j], NULL);
			ck_assert_ptr_eq(b->write_bins[j], NULL);
		}
	}
}


#define DEFINE_TEST(test_name, file_contents, stages_struct) \
START_TEST(test_name) \
{ \
	FILE* tmp = fopen(TMP_FILE_LOC "/test.yml", "w+");		\
	ck_assert_ptr_ne(tmp, NULL);							\
	stages_t expected = stages_struct;					\
	stages_t parsed;									\
	args_t args;											\
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
		((stages_t) {
			(stage_t[]) {{
				.duration = 20,
				.desc = "test stage",
				.tps = 0,
				.key_start = 1,
				.key_end = 100001,
				.pause = 0,
				.batch_size = 1,
				.async = false,
				.random = false,
				.workload = (workload_t) {
					.type = WORKLOAD_TYPE_LINEAR,
				},
				.obj_spec_str = "I4",
				.read_bins = NULL,
				.write_bins = NULL
			},},
			1,
			true
		}));


DEFINE_TEST(test_tps,
		"- stage: 1\n"
		"  desc: \"test stage\"\n"
		"  duration: 20\n"
		"  workload: I\n"
		"  tps: 321",
		((stages_t) {
			(stage_t[]) {{
				.duration = 20,
				.desc = "test stage",
				.tps = 321,
				.key_start = 1,
				.key_end = 100001,
				.pause = 0,
				.batch_size = 1,
				.async = false,
				.random = false,
				.workload = (workload_t) {
					.type = WORKLOAD_TYPE_LINEAR,
				},
				.obj_spec_str = "I4",
				.read_bins = NULL,
				.write_bins = NULL
			},},
			1,
			true
		}));


DEFINE_TEST(test_key_start,
		"- stage: 1\n"
		"  desc: \"test stage\"\n"
		"  duration: 20\n"
		"  workload: I\n"
		"  key-start: 543",
		((stages_t) {
			(stage_t[]) {{
				.duration = 20,
				.desc = "test stage",
				.tps = 0,
				.key_start = 543,
				.key_end = 100543,
				.pause = 0,
				.batch_size = 1,
				.async = false,
				.random = false,
				.workload = (workload_t) {
					.type = WORKLOAD_TYPE_LINEAR,
				},
				.obj_spec_str = "I4",
				.read_bins = NULL,
				.write_bins = NULL
			},},
			1,
			true
		}));


DEFINE_TEST(test_key_end,
		"- stage: 1\n"
		"  desc: \"test stage\"\n"
		"  duration: 20\n"
		"  workload: I\n"
		"  key-end: 1321",
		((stages_t) {
			(stage_t[]) {{
				.duration = 20,
				.desc = "test stage",
				.tps = 0,
				.key_start = 1,
				.key_end = 1321,
				.pause = 0,
				.batch_size = 1,
				.async = false,
				.random = false,
				.workload = (workload_t) {
					.type = WORKLOAD_TYPE_LINEAR,
				},
				.obj_spec_str = "I4",
				.read_bins = NULL,
				.write_bins = NULL
			},},
			1,
			true
		}));


DEFINE_TEST(test_pause,
		"- stage: 1\n"
		"  desc: \"test stage\"\n"
		"  duration: 20\n"
		"  workload: I\n"
		"  pause: 231",
		((stages_t) {
			(stage_t[]) {{
				.duration = 20,
				.desc = "test stage",
				.tps = 0,
				.key_start = 1,
				.key_end = 100001,
				.pause = 231,
				.batch_size = 1,
				.async = false,
				.random = false,
				.workload = (workload_t) {
					.type = WORKLOAD_TYPE_LINEAR,
				},
				.obj_spec_str = "I4",
				.read_bins = NULL,
				.write_bins = NULL
			},},
			1,
			true
		}));


DEFINE_TEST(test_batch_size,
		"- stage: 1\n"
		"  desc: \"test stage\"\n"
		"  duration: 20\n"
		"  workload: I\n"
		"  batch-size: 32",
		((stages_t) {
			(stage_t[]) {{
				.duration = 20,
				.desc = "test stage",
				.tps = 0,
				.key_start = 1,
				.key_end = 100001,
				.pause = 0,
				.batch_size = 32,
				.async = false,
				.random = false,
				.workload = (workload_t) {
					.type = WORKLOAD_TYPE_LINEAR,
				},
				.obj_spec_str = "I4",
				.read_bins = NULL,
				.write_bins = NULL
			},},
			1,
			true
		}));


DEFINE_TEST(test_async,
		"- stage: 1\n"
		"  desc: \"test stage\"\n"
		"  duration: 20\n"
		"  workload: I\n"
		"  async: true",
		((stages_t) {
			(stage_t[]) {{
				.duration = 20,
				.desc = "test stage",
				.tps = 0,
				.key_start = 1,
				.key_end = 100001,
				.pause = 0,
				.batch_size = 1,
				.async = true,
				.random = false,
				.workload = (workload_t) {
					.type = WORKLOAD_TYPE_LINEAR,
				},
				.obj_spec_str = "I4",
				.read_bins = NULL,
				.write_bins = NULL
			},},
			1,
			true
		}));


DEFINE_TEST(test_random,
		"- stage: 1\n"
		"  desc: \"test stage\"\n"
		"  duration: 20\n"
		"  workload: I\n"
		"  random: true",
		((stages_t) {
			(stage_t[]) {{
				.duration = 20,
				.desc = "test stage",
				.tps = 0,
				.key_start = 1,
				.key_end = 100001,
				.pause = 0,
				.batch_size = 1,
				.async = false,
				.random = true,
				.workload = (workload_t) {
					.type = WORKLOAD_TYPE_LINEAR,
				},
				.obj_spec_str = "I4",
				.read_bins = NULL,
				.write_bins = NULL
			},},
			1,
			true
		}))


DEFINE_TEST(test_workload_ru_default,
		"- stage: 1\n"
		"  desc: \"test stage\"\n"
		"  duration: 20\n"
		"  workload: RU\n",
		((stages_t) {
			(stage_t[]) {{
				.duration = 20,
				.desc = "test stage",
				.tps = 0,
				.key_start = 1,
				.key_end = 100001,
				.pause = 0,
				.batch_size = 1,
				.async = false,
				.random = false,
				.workload = (workload_t) {
					.type = WORKLOAD_TYPE_RANDOM,
					.pct = 50
				},
				.obj_spec_str = "I4",
				.read_bins = NULL,
				.write_bins = NULL
			},},
			1,
			true
		}));


DEFINE_TEST(test_workload_ru_pct,
		"- stage: 1\n"
		"  desc: \"test stage\"\n"
		"  duration: 20\n"
		"  workload: RU,75.2\n",
		((stages_t) {
			(stage_t[]) {{
				.duration = 20,
				.desc = "test stage",
				.tps = 0,
				.key_start = 1,
				.key_end = 100001,
				.pause = 0,
				.batch_size = 1,
				.async = false,
				.random = false,
				.workload = (workload_t) {
					.type = WORKLOAD_TYPE_RANDOM,
					.pct = 75.2
				},
				.obj_spec_str = "I4",
				.read_bins = NULL,
				.write_bins = NULL
			},},
			1,
			true
		}));


DEFINE_TEST(test_workload_db,
		"- stage: 1\n"
		"  desc: \"test stage\"\n"
		"  duration: 20\n"
		"  workload: DB\n",
		((stages_t) {
			(stage_t[]) {{
				.duration = 20,
				.desc = "test stage",
				.tps = 0,
				.key_start = 1,
				.key_end = 100001,
				.pause = 0,
				.batch_size = 1,
				.async = false,
				.random = false,
				.workload = (workload_t) {
					.type = WORKLOAD_TYPE_DELETE
				},
				.obj_spec_str = "I4",
				.read_bins = NULL,
				.write_bins = NULL
			},},
			1,
			true
		}));



DEFINE_TEST(test_obj_spec,
		"- stage: 1\n"
		"  desc: \"test stage\"\n"
		"  duration: 20\n"
		"  workload: I\n"
		"  object-spec: I,D,{3*S10:[B20,D,I8]}",
		((stages_t) {
			(stage_t[]) {{
				.duration = 20,
				.desc = "test stage",
				.tps = 0,
				.key_start = 1,
				.key_end = 100001,
				.pause = 0,
				.batch_size = 1,
				.async = false,
				.random = false,
				.workload = (workload_t) {
					.type = WORKLOAD_TYPE_LINEAR,
				},
				.obj_spec_str = "I4,D,{3*S10:[B20,D,I8]}",
				.read_bins = NULL,
				.write_bins = NULL
			},},
			1,
			true
		}));


DEFINE_TEST(test_read_bins,
		"- stage: 1\n"
		"  desc: \"test stage\"\n"
		"  duration: 20\n"
		"  workload: RU\n"
		"  object-spec: I,I,I,I,I\n"
		"  read-bins: 1,3,5",
		((stages_t) {
			(stage_t[]) {{
				.duration = 20,
				.desc = "test stage",
				.tps = 0,
				.key_start = 1,
				.key_end = 100001,
				.pause = 0,
				.batch_size = 1,
				.async = false,
				.random = false,
				.workload = (workload_t) {
					.type = WORKLOAD_TYPE_RANDOM,
					.pct = 50
				},
				.obj_spec_str = "I4,I4,I4,I4,I4",
				.read_bins = (char*[]) {
					"testbin",
					"testbin_3",
					"testbin_5",
					NULL
				},
				.n_read_bins = 3,
				.write_bins = NULL
			},},
			1,
			true
		}));


DEFINE_TEST(test_write_bins,
		"- stage: 1\n"
		"  desc: \"test stage\"\n"
		"  duration: 20\n"
		"  workload: RU\n"
		"  object-spec: I,I,I,I,I\n"
		"  write-bins: 1,3,5",
		((stages_t) {
			(stage_t[]) {{
				.duration = 20,
				.desc = "test stage",
				.tps = 0,
				.key_start = 1,
				.key_end = 100001,
				.pause = 0,
				.batch_size = 1,
				.async = false,
				.random = false,
				.workload = (workload_t) {
					.type = WORKLOAD_TYPE_RANDOM,
					.pct = 50
				},
				.obj_spec_str = "I4,I4,I4,I4,I4",
				.read_bins = NULL,
				.write_bins = (char*[]) {
					"testbin",
					"testbin_3",
					"testbin_5",
					NULL
				},
				.n_write_bins = 3
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
	tcase_add_test(tc_simple, test_tps);
	tcase_add_test(tc_simple, test_key_start);
	tcase_add_test(tc_simple, test_key_end);
	tcase_add_test(tc_simple, test_pause);
	tcase_add_test(tc_simple, test_batch_size);
	tcase_add_test(tc_simple, test_async);
	tcase_add_test(tc_simple, test_random);
	tcase_add_test(tc_simple, test_workload_ru_default);
	tcase_add_test(tc_simple, test_workload_ru_pct);
	tcase_add_test(tc_simple, test_workload_db);
	tcase_add_test(tc_simple, test_obj_spec);
	tcase_add_test(tc_simple, test_read_bins);
	tcase_add_test(tc_simple, test_write_bins);
	suite_add_tcase(s, tc_simple);

	return s;
}

