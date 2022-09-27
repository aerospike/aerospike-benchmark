
import lib

def test_version_long():
	# test that the version command return code is 0
	lib.run_benchmark(["--version"])


def test_version_short():
	# test that the version command return code is 0
	lib.run_benchmark(["-v"])
