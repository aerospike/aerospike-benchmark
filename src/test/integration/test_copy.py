import lib


def test_copy_same_namespace_smoke():
	lib.run_benchmark(["--workload", "I", "--start-key", "10", "--keys", "20",
			"--object-spec", "I,S10"])
	lib.run_benchmark(["--workload", "CP", "--start-key", "10", "--keys", "20",
			"--to-namespace", lib.NAMESPACE], do_reset=False)
	lib.check_for_range(10, 30)


def test_copy_requires_destination_namespace():
	lib.run_benchmark(["--workload", "CP", "--keys", "1"],
			expect_success=False)


def test_copy_rejects_async():
	lib.run_benchmark(["--workload", "CP", "--keys", "1", "--to-namespace",
			lib.NAMESPACE, "--async"], expect_success=False)
