
import lib

def test_linear_delete():
	# first fill up the database
	lib.run_benchmark("--workload I --startKey 0 --keys 100")
	lib.check_for_range(0, 100)
	# then delete it all
	lib.run_benchmark("--workload DB --startKey 0 --keys 100")
	lib.check_for_range(0, 0)

