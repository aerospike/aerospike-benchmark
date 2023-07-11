
import lib

def test_linear_delete():
	# first fill up the database
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100"])
	lib.check_for_range(0, 100)
	# then delete it all
	lib.run_benchmark(["--workload", "DB", "--start-key", "0", "--keys", "100"])
	lib.check_for_range(0, 0)

def test_linear_delete_async():
	# first fill up the database
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100"])
	lib.check_for_range(0, 100)
	# then delete it all
	lib.run_benchmark(["--workload", "DB", "--start-key", "0", "--keys", "100", "--async"])
	lib.check_for_range(0, 0)

def test_linear_delete_subset():
	# first fill up the database
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "1000"])
	lib.check_for_range(0, 1000)
	# then delete a subset of the database
	lib.run_benchmark(["--workload", "DB", "--start-key", "300", "--keys", "500"], do_reset=False)
	lib.check_recs_exist_in_range(0, 300)
	lib.check_recs_exist_in_range(800, 1000)
	assert(len(lib.scan_records()) == 500)

def test_linear_delete_subset_async():
	# first fill up the database
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "1000"])
	lib.check_for_range(0, 1000)
	# then delete a subset of the database
	lib.run_benchmark(["--workload", "DB", "--start-key", "300", "--keys", "500", "--async"], do_reset=False)
	lib.check_recs_exist_in_range(0, 300)
	lib.check_recs_exist_in_range(800, 1000)
	assert(len(lib.scan_records()) == 500)

def test_linear_delete_batch():
	# first fill up the database
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "--batch-size", "5", "--threads", "2"])
	lib.check_for_range(0, 100)
	# then delete it all
	lib.run_benchmark(["--workload", "DB", "--start-key", "0", "--keys", "100", "--batch-size", "5"])
	lib.check_for_range(0, 0)

def test_linear_delete_async_batch():
	# first fill up the database
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "--batch-size", "10", "--async"])
	lib.check_for_range(0, 100)
	# then delete it all
	lib.run_benchmark(["--workload", "DB", "--start-key", "0", "--keys", "100", "--batch-delete-size", "10", "--async"])
	lib.check_for_range(0, 0)

def test_linear_delete_subset_batch():
	# first fill up the database
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "1000", "--batch-size", "5", "--async"])
	breakpoint()
	lib.check_for_range(0, 1000)
	# then delete a subset of the database
	lib.run_benchmark(["--workload", "DB", "--start-key", "300", "--keys", "500", "--batch-delete-size", "5", "--threads", "2"], do_reset=False)
	lib.check_recs_exist_in_range(0, 300)
	lib.check_recs_exist_in_range(800, 1000)
	assert(len(lib.scan_records()) == 500)

def test_linear_delete_subset_async_batch():
	# first fill up the database
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "1000"])
	lib.check_for_range(0, 1000)
	# then delete a subset of the database
	lib.run_benchmark(["--workload", "DB", "--start-key", "300", "--keys", "500", "--batch-delete-size", "50", "--async"], do_reset=False)
	lib.check_recs_exist_in_range(0, 300)
	lib.check_recs_exist_in_range(800, 1000)
	assert(len(lib.scan_records()) == 500)

