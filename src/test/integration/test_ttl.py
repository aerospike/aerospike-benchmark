
import lib

def test_ttl_1():
	lib.run_benchmark(["--workload", "I", "--keys", "100", "--expiration-time", "1"])
	lib.check_for_range(1, 101)
	lib.safe_sleep(5)
	lib.check_for_range(0, 0)

def test_ttl_no_change():
	lib.run_benchmark(["--workload", "I", "--keys", "100", "--expiration-time", "10"])
	lib.safe_sleep(6)
	lib.check_for_range(1, 101)
	lib.run_benchmark(["--workload", "I", "--keys", "100", "--expiration-time", "-2"], do_reset=False)
	lib.safe_sleep(6)
	lib.check_for_range(0, 0)

def test_ttl_inf():
	lib.run_benchmark(["--workload", "I", "--keys", "100", "--expiration-time", "-1"])
	lib.safe_sleep(5)
	lib.check_for_range(1, 101)

