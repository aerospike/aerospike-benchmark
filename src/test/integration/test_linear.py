
import lib

def test_linear_write_keys():
	lib.run_benchmark("--workload I --keys 100")
	lib.check_for_range(1, 101)

def test_linear_write_start_key():
	lib.run_benchmark("--workload I --startKey 1000 --keys 1000")
	lib.check_for_range(1000, 2000)

