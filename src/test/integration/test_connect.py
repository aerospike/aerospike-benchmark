
import lib
import os

def test_connect():
	# the only reason this would fail is if the tool can't connect to the
	# cluster
	lib.run_benchmark("--duration 0 --workload RU")

