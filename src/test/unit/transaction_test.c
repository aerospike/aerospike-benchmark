#include <check.h>

#include <aerospike/as_integer.h>
#include <aerospike/as_operations.h>
#include <aerospike/as_record.h>
#include <aerospike/as_string.h>

#include "benchmark_tests.h"


extern void _build_copy_ops(as_operations* ops, const as_record* rec);


START_TEST(copy_ops_preserve_bins)
{
	as_record rec;
	as_record_inita(&rec, 2);
	as_record_set_int64(&rec, "int_bin", 42);
	as_record_set_str(&rec, "str_bin", "hello");

	as_operations ops;
	_build_copy_ops(&ops, &rec);

	ck_assert_uint_eq(ops.binops.size, 2);

	as_binop* int_op = &ops.binops.entries[0];
	ck_assert_int_eq(int_op->op, AS_OPERATOR_WRITE);
	ck_assert_str_eq(int_op->bin.name, "int_bin");
	ck_assert_int_eq(as_integer_get((as_integer*) int_op->bin.valuep), 42);
	ck_assert_ptr_eq(int_op->bin.valuep, rec.bins.entries[0].valuep);

	as_binop* str_op = &ops.binops.entries[1];
	ck_assert_int_eq(str_op->op, AS_OPERATOR_WRITE);
	ck_assert_str_eq(str_op->bin.name, "str_bin");
	ck_assert_str_eq(as_string_get((as_string*) str_op->bin.valuep), "hello");
	ck_assert_ptr_eq(str_op->bin.valuep, rec.bins.entries[1].valuep);

	as_operations_destroy(&ops);
	as_record_destroy(&rec);
}
END_TEST

START_TEST(copy_ops_preserve_ttl)
{
	as_record rec;
	as_record_inita(&rec, 1);
	as_record_set_int64(&rec, "value", 7);
	rec.ttl = 12345;

	as_operations ops;
	_build_copy_ops(&ops, &rec);

	ck_assert_uint_eq(ops.ttl, 12345);

	as_operations_destroy(&ops);
	as_record_destroy(&rec);
}
END_TEST

Suite*
transaction_suite(void)
{
	Suite* suite = suite_create("Transaction Copy");
	TCase* copy = tcase_create("Copy Operations");
	tcase_add_test(copy, copy_ops_preserve_bins);
	tcase_add_test(copy, copy_ops_preserve_ttl);
	suite_add_tcase(suite, copy);
	return suite;
}
