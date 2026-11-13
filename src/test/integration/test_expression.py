import lib


def test_expression_reads_and_writes_named_integer_bin():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "1",
			"--object-spec", "count=1"])

	lib.run_benchmark(["--workload", "E,100", "--duration", "1",
			"--start-key", "0", "--keys", "1", "--object-spec", "count=1"],
			do_reset=False)
	assert lib.get_record(0)[2]["count"] == 1

	lib.run_benchmark(["--workload", "E,0", "--duration", "1",
			"--start-key", "0", "--keys", "1", "--object-spec", "count=1"],
			do_reset=False)
	assert lib.get_record(0)[2]["count"] > 1


def test_expression_rejects_non_integer_first_bin():
	lib.run_benchmark(["--workload", "E", "--object-spec", "S10"],
			expect_success=False)


def test_expression_rejects_async():
	lib.run_benchmark(["--workload", "E", "--object-spec", "I", "--async"],
			expect_success=False)
