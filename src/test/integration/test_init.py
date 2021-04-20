
import lib
import os

def test_no_connect():
	# test's running the benchmark when the cluster is unreachable
	lib.run_benchmark("--duration 0 --workload RU", ip="127.0.0.1", port=1000,
			expect_success=False)

def test_bad_port():
	# test's running the benchmark when the cluster is unreachable
	lib.run_benchmark("--duration 0 --workload RU", port=1000,
			expect_success=False)

def test_connect():
	# the only reason this would fail is if the tool can't connect to the
	# cluster
	lib.run_benchmark("--duration 0 --workload RU")

