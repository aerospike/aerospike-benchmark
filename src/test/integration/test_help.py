
import lib

def test_help_long():
	# test that the help command return code is 0
	lib.run_benchmark(["--help"])
