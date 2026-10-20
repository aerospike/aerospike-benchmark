
import pytest

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

@pytest.mark.parametrize("workload", [
	["--workload", "RU,100"],
	["--workload", "RU,100", "--batch-read-size", "10"],
	["--workload", "RU,100", "--async"],
	["--workload", "C,100,0,5"],
	["--workload", "C,100,0,5", "--async"],
])
def test_read_touch_ttl_percent_reads(workload):
	lib.run_benchmark(["--workload", "I", "--keys", "100", "--expiration-time", "60",
		"-o", "[5*I1]"])
	rc, out = lib.run_benchmark_output(workload + ["--keys", "100", "--duration", "1",
		"-o", "[5*I1]", "--read-touch-ttl-percent", "80"], do_reset=False)
	assert(rc == 0), out
	assert("read touch ttl percent: 80" in out), out
