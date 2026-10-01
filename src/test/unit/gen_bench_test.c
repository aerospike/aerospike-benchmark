#include <check.h>
#include <stdio.h>

#include <benchmark.h>
#include <gen_bench.h>
#include <object_spec.h>
#include <workload.h>


extern void _load_defaults(args_t* args);
extern int _load_defaults_post(args_t* args);
extern void _free_args(args_t* args);


static void
simple_setup(void)
{
	freopen("/dev/null", "w", stdout);
}

static void
simple_teardown(void)
{
}

static void
_run(const char* spec, bool seeded, int threads)
{
	args_t args;
	_load_defaults(&args);
	obj_spec_free(&args.obj_spec);
	ck_assert_int_eq(obj_spec_parse(&args.obj_spec, spec), 0);
	args.gen_bench_iters = 1000;
	args.transaction_worker_threads = threads;
	args.seed_set = seeded;
	args.seed = 7;
	ck_assert_int_eq(_load_defaults_post(&args), 0);
	ck_assert_int_eq(run_gen_bench(&args), 0);
	_free_args(&args);
}

START_TEST(gen_bench_single_thread)
{
	_run("first=@first_name, email=@email, age=@int(18,90)", false, 1);
}
END_TEST

START_TEST(gen_bench_threads_seeded)
{
	_run("id=@uuid, loc=@geojson, orders=[3*{\"sku\":@product,\"qty\":@int(1,9)}]",
			true, 4);
}
END_TEST

START_TEST(gen_bench_legacy_spec)
{
	_run("I4, S16, [3*B8]", false, 2);
}
END_TEST


Suite*
gen_bench_suite(void)
{
	Suite* s;
	TCase* tc;

	s = suite_create("Gen Bench");
	tc = tcase_create("Run");
	tcase_add_checked_fixture(tc, simple_setup, simple_teardown);
	tcase_set_timeout(tc, 60);
	tcase_add_test(tc, gen_bench_single_thread);
	tcase_add_test(tc, gen_bench_threads_seeded);
	tcase_add_test(tc, gen_bench_legacy_spec);
	suite_add_tcase(s, tc);

	return s;
}
