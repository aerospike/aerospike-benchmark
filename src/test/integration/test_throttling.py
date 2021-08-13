
import lib

def test_tps_simple():
	# test throughput throttling with simple objects and one thread
	lib.run_benchmark("--workload RU,0.0001 --duration 5 --start-key 0 " +
			"--keys 1000000000 -o I --throughput 1000 -z 1")
	n_records = len(lib.scan_records())
	assert(4950 <= n_records <= 5050)

def test_tps_simple_async():
	# test throughput throttling with simple objects and one thread
	lib.run_benchmark("--workload RU,0.0001 --duration 5 --start-key 0 " +
			"--keys 1000000000 -o I --throughput 1000 -z 1 --async")
	n_records = len(lib.scan_records())
	assert(4950 <= n_records <= 5050)

def test_tps_multithreaded():
	# test throughput throttling with simple objects and one thread
	lib.run_benchmark("--workload RU,0.0001 --duration 5 --start-key 0 " +
			"--keys 1000000000 -o I --throughput 1000 -z 16")
	n_records = len(lib.scan_records())
	# there is much higher variance with multiple threads
	assert(4950 <= n_records <= 5050)

def test_tps_multithreaded_async():
	# test throughput throttling with simple objects and one thread
	lib.run_benchmark("--workload RU,0.0001 --duration 5 --start-key 0 " +
			"--keys 1000000000 -o I --throughput 1000 -z 16 --async")
	n_records = len(lib.scan_records())
	# there is much higher variance with multiple threads
	assert(4950 <= n_records <= 5050)

def test_tps_read_write():
	# test throughput throttling with simple objects and one thread
	lib.run_benchmark("--workload RU,50 --duration 5 --start-key 0 " +
			"--keys 1000000000 -o I --throughput 1000 -z 1")
	n_records = len(lib.scan_records())
	assert(2350 <= n_records <= 2650)

def test_tps_read_write_async():
	# test throughput throttling with simple objects and one thread
	lib.run_benchmark("--workload RU,50 --duration 5 --start-key 0 " +
			"--keys 1000000000 -o I --throughput 1000 -z 1 --async")
	n_records = len(lib.scan_records())
	assert(2400 <= n_records <= 2600)

def test_tps_read_write_high_variance():
	# test throughput throttling writing complex objects and reading simple ones
	lib.run_benchmark("--workload RU,50 --duration 5 --start-key 0 " +
			"--keys 1000000000 -o \"I,{500*S64:[10*I,B128]}\" --throughput 500 " +
			"-z 1 --read-bins 1")
	n_records = len(lib.scan_records())
	assert(1150 <= n_records <= 1350)

def test_tps_read_write_high_variance_async():
	# test throughput throttling writing complex objects and reading simple ones
	lib.run_benchmark("--workload RU,50 --duration 5 --start-key 0 " +
			"--keys 1000000000 -o \"I,{500*S64:[10*I,B128]}\" --throughput 500 " +
			"-z 1 --read-bins 1 --async")
	n_records = len(lib.scan_records())
	assert(1150 <= n_records <= 1350)

