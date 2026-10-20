
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

def record_ttls():
	return {bytes(key[3]): meta["ttl"] for key, meta, _ in lib.scan_records()}

def read_with_touch(workload, pct):
	# 30s TTL and an 8s wait put every record inside the 80% touch window, so
	# a touch resets its TTL above anything it had before the reads
	lib.run_benchmark(["--workload", "I", "--keys", "20", "--expiration-time", "30",
		"-o", "[5*I1]"])
	lib.safe_sleep(8)
	before = record_ttls()
	assert(len(before) == 20)
	rc, out = lib.run_benchmark_output(workload + ["--keys", "20", "--duration", "1",
		"-o", "[5*I1]", "--read-touch-ttl-percent", pct], do_reset=False)
	assert(rc == 0), out
	assert(f"read touch ttl percent: {pct}" in out), out
	after = record_ttls()
	assert(after.keys() == before.keys())
	return before, after

@pytest.mark.parametrize("workload", [
	["--workload", "RU,100"],
	["--workload", "RU,100", "--batch-read-size", "10"],
	["--workload", "RU,100", "--async"],
	["--workload", "C,100,0,5"],
	["--workload", "C,100,0,5", "--async"],
])
def test_read_touch_ttl_percent_resets_ttl(workload):
	before, after = read_with_touch(workload, "80")
	for digest, ttl in before.items():
		assert(after[digest] > ttl), (ttl, after[digest])

def test_read_touch_ttl_percent_never():
	before, after = read_with_touch(["--workload", "RU,100"], "-1")
	for digest, ttl in before.items():
		assert(after[digest] <= ttl), (ttl, after[digest])
