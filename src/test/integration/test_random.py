
import lib

def test_random_read():
	lib.run_benchmark("--duration 1 --workload RU --start-key 0 --keys 100")

	n_recs = len(lib.scan_records())
	# we must have created at least one record, and no more than 100
	assert(n_recs > 0 and n_recs <= 100)

	# now check that no records exist with keys outside the range 0-100
	# count the number of records with keys between 0 and 100
	cnt = 0
	for key in range(0, 100):
		if lib.get_record(key) is not None:
			cnt += 1
	assert(cnt == n_recs)

def test_random_read_async():
	lib.run_benchmark("--duration 1 --workload RU --start-key 0 --keys 100 --async")

	n_recs = len(lib.scan_records())
	# we must have created at least one record, and no more than 100
	assert(n_recs > 0 and n_recs <= 100)

	# now check that no records exist with keys outside the range 0-100
	# count the number of records with keys between 0 and 100
	cnt = 0
	for key in range(0, 100):
		if lib.get_record(key) is not None:
			cnt += 1
	assert(cnt == n_recs)

def test_random_read_batch():
	lib.run_benchmark("--duration 1 --workload RU --start-key 0 --keys 100 --batch-size 16")

	n_recs = len(lib.scan_records())
	# we must have created at least one record, and no more than 100
	assert(n_recs > 0 and n_recs <= 100)

	# now check that no records exist with keys outside the range 0-100
	# count the number of records with keys between 0 and 100
	cnt = 0
	for key in range(0, 100):
		if lib.get_record(key) is not None:
			cnt += 1
	assert(cnt == n_recs)

def test_random_read_batch_async():
	lib.run_benchmark("--duration 1 --workload RU --start-key 0 --keys 100 --batch-size 16 --async")

	n_recs = len(lib.scan_records())
	# we must have created at least one record, and no more than 100
	assert(n_recs > 0 and n_recs <= 100)

	# now check that no records exist with keys outside the range 0-100
	# count the number of records with keys between 0 and 100
	cnt = 0
	for key in range(0, 100):
		if lib.get_record(key) is not None:
			cnt += 1
	assert(cnt == n_recs)

def test_random_read_only():
	lib.run_benchmark("--workload I --start-key 0 --keys 100")

	recs = [lib.get_record(key) for key in range(0, 100)]

	lib.run_benchmark("--duration 1 --workload RU,100 --start-key 0 --keys 100", do_reset=False)

	# this should exactly match recs
	recs2 = [lib.get_record(key) for key in range(0, 100)]

	for rec1, rec2 in zip(recs, recs2):
		(dig1, meta1, bins1) = rec1
		(dig2, meta2, bins2) = rec2

		assert(len(rec1) == len(rec2))
		for bin_name in bins1:
			assert(bins1[bin_name] == bins2[bin_name])

def test_random_read_only_async():
	lib.run_benchmark("--workload I --start-key 0 --keys 100 --async")

	recs = [lib.get_record(key) for key in range(0, 100)]

	lib.run_benchmark("--duration 1 --workload RU,100 --start-key 0 --keys 100 --async", do_reset=False)

	# this should exactly match recs
	recs2 = [lib.get_record(key) for key in range(0, 100)]

	for rec1, rec2 in zip(recs, recs2):
		(dig1, meta1, bins1) = rec1
		(dig2, meta2, bins2) = rec2

		assert(len(rec1) == len(rec2))
		for bin_name in bins1:
			assert(bins1[bin_name] == bins2[bin_name])

