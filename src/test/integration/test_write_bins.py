
import lib

def test_write_bins_simple():
	def check_bins(b):
		assert(len(b) == 2)
		lib.obj_spec_is_I1(b["testbin"])
		lib.obj_spec_is_I3(b["testbin_3"])

	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o I1,I2,I3,I4 --random --writeBins 1,3")
	lib.check_for_range(0, 100, lambda meta, key, bins: check_bins(bins))

def test_write_bins_random():
	def check_bins(b):
		assert(len(b) == 2)

		assert(type(b["testbin_2"]) is list)
		lib.obj_spec_is_D(b["testbin_2"][0])
		lib.obj_spec_is_S(b["testbin_2"][1], 10)

		lib.obj_spec_is_B(b["testbin_5"], 10)

	lib.run_benchmark("--workload RU --duration 1 --startKey 0 --keys 200 " +
			"-o I5,[D,S10],I3,D,B10 --random --writeBins 2,5")
	# it's very, very likely that after 1 second, all 200 key values would
	# have been randomly chosen
	lib.check_for_range(0, 200, lambda meta, key, bins: check_bins(bins))

def test_write_bins_delete():
	def check_bins_before(b):
		assert(len(b) == 4)
		lib.obj_spec_is_I1(b["testbin"])
		lib.obj_spec_is_I2(b["testbin_2"])
		lib.obj_spec_is_I3(b["testbin_3"])
		lib.obj_spec_is_I4(b["testbin_4"])

	def check_bins_after(b):
		assert(len(b) == 2)
		lib.obj_spec_is_I2(b["testbin_2"])
		lib.obj_spec_is_I4(b["testbin_4"])

	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o I1,I2,I3,I4 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: check_bins_before(bins))
	lib.run_benchmark("--workload DB --startKey 0 --keys 100 -o I1,I2,I3,I4 " +
			"--writeBins 1,3", do_reset=False)
	lib.check_for_range(0, 100, lambda meta, key, bins: check_bins_after(bins))


