#include <check.h>
#include <stdio.h>
#include <string.h>

#include <aerospike/as_operations.h>

#include <benchmark.h>
#include <common.h>
#include <object_spec.h>
#include <workload.h>


extern uint16_t _cdt_count_write_ops(const stage_t* stage);
extern void _build_cdt_write_ops(tdata_t* tdata, const cdata_t* cdata,
		const stage_t* stage, as_operations* ops);
extern void _build_cdt_read_ops(tdata_t* tdata, const stage_t* stage,
		as_operations* ops);
extern void _init_cdt_stage(const cdata_t* cdata, tdata_t* tdata,
		const stage_t* stage);
extern void _terminate_cdt_stage(tdata_t* tdata);


typedef struct cdt_fixture_s {
	stage_t stage;
	tdata_t tdata;
	cdata_t cdata;
} cdt_fixture_t;

static void
_setup(cdt_fixture_t* f, const char* spec, const char* workload, bool random)
{
	memset(f, 0, sizeof(*f));
	ck_assert_int_eq(obj_spec_parse(&f->stage.obj_spec, spec), 0);
	ck_assert_int_eq(parse_workload_type(&f->stage.workload, workload), 0);
	uint32_t n = obj_spec_n_bins(&f->stage.obj_spec);
	f->stage.bin_names = (as_bin_name*) cf_malloc(n * sizeof(as_bin_name));
	ck_assert_int_eq(obj_spec_resolve_bin_names(&f->stage.obj_spec, "b",
				f->stage.bin_names), 0);
	f->stage.random = random;
	f->cdata.compression_ratio = 1.f;
	as_random_init(&f->tdata.random_state);
	f->tdata.random = &f->tdata.random_state;
	_init_cdt_stage(&f->cdata, &f->tdata, &f->stage);
}

static void
_teardown(cdt_fixture_t* f)
{
	_terminate_cdt_stage(&f->tdata);
	cf_free(f->stage.bin_names);
	obj_spec_free(&f->stage.obj_spec);
}

static void
_build_writes(cdt_fixture_t* f, as_operations* ops)
{
	as_operations_init(ops, f->tdata.cdt_n_write_ops);
	_build_cdt_write_ops(&f->tdata, &f->cdata, &f->stage, ops);
}

static void
_build_reads(cdt_fixture_t* f, as_operations* ops)
{
	as_operations_init(ops, f->tdata.cdt_n_read_ops);
	_build_cdt_read_ops(&f->tdata, &f->stage, ops);
}

static void
_assert_op(const as_operations* ops, uint32_t i, as_operator op,
		const char* bin)
{
	ck_assert_uint_lt(i, ops->binops.size);
	ck_assert_int_eq(ops->binops.entries[i].op, op);
	ck_assert_str_eq(ops->binops.entries[i].bin.name, bin);
}


START_TEST(list_append_with_cap)
{
	cdt_fixture_t f;
	as_operations ops;
	_setup(&f, "events=[3*@word]", "C,50,20,5", true);
	ck_assert_uint_eq(f.tdata.cdt_n_write_ops, 2);
	ck_assert_ptr_eq(f.tdata.cdt_write_ops, NULL);

	for (uint32_t i = 0; i < 100; i++) {
		_build_writes(&f, &ops);
		ck_assert_uint_eq(ops.binops.size, 2);
		_assert_op(&ops, 0, AS_OPERATOR_CDT_MODIFY, "events");
		_assert_op(&ops, 1, AS_OPERATOR_CDT_MODIFY, "events");
		as_operations_destroy(&ops);
	}

	ck_assert_ptr_ne(f.tdata.cdt_read_ops, NULL);
	ck_assert_uint_eq(f.tdata.cdt_read_ops->binops.size, 1);
	_assert_op(f.tdata.cdt_read_ops, 0, AS_OPERATOR_CDT_READ, "events");
	_teardown(&f);
}
END_TEST

START_TEST(list_append_no_cap)
{
	cdt_fixture_t f;
	as_operations ops;
	_setup(&f, "[2*I1]", "C,50", true);
	ck_assert_uint_eq(f.tdata.cdt_n_write_ops, 1);
	_build_writes(&f, &ops);
	ck_assert_uint_eq(ops.binops.size, 1);
	_assert_op(&ops, 0, AS_OPERATOR_CDT_MODIFY, "b");
	as_operations_destroy(&ops);
	_teardown(&f);
}
END_TEST

START_TEST(map_put_with_cap)
{
	cdt_fixture_t f;
	as_operations ops;
	_setup(&f, "attrs={3*@word:@int(1,5)}", "C,50,10", true);
	ck_assert_uint_eq(f.tdata.cdt_n_write_ops, 2);
	_build_writes(&f, &ops);
	ck_assert_uint_eq(ops.binops.size, 2);
	_assert_op(&ops, 0, AS_OPERATOR_MAP_MODIFY, "attrs");
	_assert_op(&ops, 1, AS_OPERATOR_MAP_MODIFY, "attrs");
	as_operations_destroy(&ops);
	_assert_op(f.tdata.cdt_read_ops, 0, AS_OPERATOR_MAP_READ, "attrs");
	_teardown(&f);
}
END_TEST

START_TEST(map_increment_per_entry)
{
	cdt_fixture_t f;
	as_operations ops;
	_setup(&f, "scores={5*@username:@int(1,9)}", "CI,30,100,10", true);
	ck_assert_uint_eq(f.tdata.cdt_n_write_ops, 6);
	for (uint32_t i = 0; i < 100; i++) {
		_build_writes(&f, &ops);
		ck_assert_uint_eq(ops.binops.size, 6);
		for (uint32_t j = 0; j < 6; j++) {
			_assert_op(&ops, j, AS_OPERATOR_MAP_MODIFY, "scores");
		}
		as_operations_destroy(&ops);
	}
	_assert_op(f.tdata.cdt_read_ops, 0, AS_OPERATOR_MAP_READ, "scores");
	_teardown(&f);
}
END_TEST

START_TEST(map_increment_const_map)
{
	cdt_fixture_t f;
	as_operations ops;
	_setup(&f, "{\"a\":1,\"b\":2}", "CI,0", true);
	ck_assert_uint_eq(f.tdata.cdt_n_write_ops, 2);
	_build_writes(&f, &ops);
	ck_assert_uint_eq(ops.binops.size, 2);
	as_operations_destroy(&ops);
	_teardown(&f);
}
END_TEST

START_TEST(map_read_by_key)
{
	cdt_fixture_t f;
	as_operations ops;
	_setup(&f, "attrs={3*@pick(\"theme\",\"lang\",\"tz\",\"plan\"):S8}", "CK,90,50",
			true);
	ck_assert_ptr_eq(f.tdata.cdt_read_ops, NULL);
	for (uint32_t i = 0; i < 100; i++) {
		_build_reads(&f, &ops);
		ck_assert_uint_eq(ops.binops.size, 1);
		_assert_op(&ops, 0, AS_OPERATOR_MAP_READ, "attrs");
		as_operations_destroy(&ops);
	}
	_teardown(&f);
}
END_TEST

START_TEST(scalars_and_collections_mixed)
{
	cdt_fixture_t f;
	as_operations ops;
	_setup(&f, "updated=@now, samples=[1*[@timestamp,@double(0,100)]]",
			"C,20,1440,60", true);
	ck_assert_uint_eq(f.tdata.cdt_n_write_ops, 3);
	_build_writes(&f, &ops);
	ck_assert_uint_eq(ops.binops.size, 3);
	_assert_op(&ops, 0, AS_OPERATOR_WRITE, "updated");
	_assert_op(&ops, 1, AS_OPERATOR_CDT_MODIFY, "samples");
	_assert_op(&ops, 2, AS_OPERATOR_CDT_MODIFY, "samples");
	as_operations_destroy(&ops);

	ck_assert_uint_eq(f.tdata.cdt_read_ops->binops.size, 2);
	_assert_op(f.tdata.cdt_read_ops, 0, AS_OPERATOR_READ, "updated");
	_assert_op(f.tdata.cdt_read_ops, 1, AS_OPERATOR_CDT_READ, "samples");
	_teardown(&f);
}
END_TEST

START_TEST(prebuilt_writes_without_random)
{
	cdt_fixture_t f;
	_setup(&f, "[5*I1]", "C,0,20", false);
	ck_assert_ptr_ne(f.tdata.cdt_write_ops, NULL);
	ck_assert_uint_eq(f.tdata.cdt_write_ops->binops.size, 2);
	ck_assert_ptr_eq(f.tdata.cdt_read_ops, NULL);
	_teardown(&f);

	_setup(&f, "[5*I1]", "C,0,20", true);
	ck_assert_ptr_eq(f.tdata.cdt_write_ops, NULL);
	_teardown(&f);
}
END_TEST

START_TEST(read_only_has_no_write_ops)
{
	cdt_fixture_t f;
	_setup(&f, "[5*I1]", "C,100,0,5", false);
	ck_assert_ptr_eq(f.tdata.cdt_write_ops, NULL);
	ck_assert_ptr_ne(f.tdata.cdt_read_ops, NULL);
	_teardown(&f);
}
END_TEST

START_TEST(ttl_propagates)
{
	cdt_fixture_t f;
	as_operations ops;
	_setup(&f, "[I1]", "C,0", true);
	f.stage.ttl = 60;
	_build_writes(&f, &ops);
	ck_assert_uint_eq(ops.ttl, 60);
	as_operations_destroy(&ops);
	_teardown(&f);
}
END_TEST

START_TEST(geojson_list_elements)
{
	cdt_fixture_t f;
	as_operations ops;
	_setup(&f, "visits=[1*{\"at\":@timestamp,\"loc\":@geojson,\"city\":@city}]",
			"C,50,200,20", true);
	for (uint32_t i = 0; i < 100; i++) {
		_build_writes(&f, &ops);
		ck_assert_uint_eq(ops.binops.size, 2);
		as_operations_destroy(&ops);
	}
	_teardown(&f);
}
END_TEST


Suite*
cdt_ops_suite(void)
{
	Suite* s;
	TCase* tc_ops;

	s = suite_create("CDT Ops");

	tc_ops = tcase_create("Build");
	tcase_add_test(tc_ops, list_append_with_cap);
	tcase_add_test(tc_ops, list_append_no_cap);
	tcase_add_test(tc_ops, map_put_with_cap);
	tcase_add_test(tc_ops, map_increment_per_entry);
	tcase_add_test(tc_ops, map_increment_const_map);
	tcase_add_test(tc_ops, map_read_by_key);
	tcase_add_test(tc_ops, scalars_and_collections_mixed);
	tcase_add_test(tc_ops, prebuilt_writes_without_random);
	tcase_add_test(tc_ops, read_only_has_no_write_ops);
	tcase_add_test(tc_ops, ttl_propagates);
	tcase_add_test(tc_ops, geojson_list_elements);
	suite_add_tcase(s, tc_ops);

	return s;
}
