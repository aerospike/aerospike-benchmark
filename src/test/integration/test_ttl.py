
import subprocess

import pytest

import lib

ROOT = lib.absolute_path("../../..")

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

def run_without_server(args):
	proc = subprocess.run(["test_target/asbench", "-h", "127.0.0.1:1",
		"--gen-bench", "10", "-z", "1"] + args, cwd=ROOT,
		stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
	return proc.returncode, proc.stdout

@pytest.mark.parametrize("pct", ["-1", "0", "1", "100"])
def test_read_touch_ttl_percent_valid(pct):
	rc, out = run_without_server(["--read-touch-ttl-percent", pct])
	assert(rc == 0), out

@pytest.mark.parametrize("pct", ["-2", "101", "abc", "", "50x", "1.5"])
def test_read_touch_ttl_percent_invalid(pct):
	rc, out = run_without_server(["--read-touch-ttl-percent", pct])
	assert(rc == 1), out
	assert(f'Invalid read-touch-ttl-percent "{pct}", expected -1 to 100' in out)

@pytest.mark.parametrize("workload", [
	["--workload", "RU,100"],
	["--workload", "RU,100", "--batch-read-size", "10"],
	["--workload", "RU,100", "--async"],
])
def test_read_touch_ttl_percent_reads(workload):
	lib.run_benchmark(["--workload", "I", "--keys", "100", "--expiration-time", "60"])
	rc, out = lib.run_benchmark_output(workload + ["--keys", "100", "--duration", "1",
		"--read-touch-ttl-percent", "80"], do_reset=False)
	assert(rc == 0), out
	assert("read touch ttl percent: 80" in out)
	lib.check_for_range(1, 101)
