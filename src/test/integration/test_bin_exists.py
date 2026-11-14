import lib


def test_bin_exists_default_and_named_bin():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "1",
			"--object-spec", "present=I,other=S10"])
	before = lib.get_record(0)[2]

	lib.run_benchmark(["--workload", "BE", "--duration", "1",
			"--start-key", "0", "--keys", "1",
			"--object-spec", "present=I,other=S10"], do_reset=False)
	lib.run_benchmark(["--workload", "BE,present", "--duration", "1",
			"--start-key", "0", "--keys", "1",
			"--object-spec", "present=I,other=S10"], do_reset=False)

	assert lib.get_record(0)[2] == before


def test_bin_exists_rejects_async():
	lib.run_benchmark(["--workload", "BE", "--async"],
			expect_success=False)
