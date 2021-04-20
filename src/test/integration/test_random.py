
import lib

def test_random_read():
	lib.run_benchmark("--duration 1 --workload RU --startKey 0 --keys 100")

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

