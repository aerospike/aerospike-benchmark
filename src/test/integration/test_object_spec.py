
import lib

def test_I1():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o I1 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_I1(bins["testbin"]))

def test_I2():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o I2 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_I2(bins["testbin"]))

def test_I3():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o I3 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_I3(bins["testbin"]))

def test_I4():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o I4 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_I4(bins["testbin"]))

def test_I5():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o I5 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_I5(bins["testbin"]))

def test_I6():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o I6 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_I6(bins["testbin"]))

def test_I7():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o I7 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_I7(bins["testbin"]))

def test_I8():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o I8 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_I8(bins["testbin"]))

def test_D():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o D --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_D(bins["testbin"]))

def test_S1():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o S1 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 1))

def test_S2():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o S2 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 2))

def test_S3():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o S3 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 3))

def test_S4():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o S4 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 4))

def test_S5():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o S5 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 5))

def test_S6():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o S6 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 6))

def test_S7():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o S7 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 7))

def test_S8():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o S8 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 8))

def test_S100():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o S100 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 100))

def test_S10000():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o S10000 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 10000))

def test_B1():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o B1 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 1))

def test_B2():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o B2 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 2))

def test_B3():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o B3 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 3))

def test_B4():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o B4 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 4))

def test_B5():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o B5 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 5))

def test_B6():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o B6 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 6))

def test_B7():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o B7 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 7))

def test_B8():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o B8 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 8))

def test_B100():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o B100 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 100))

def test_B10000():
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o B10000 --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 10000))

def test_list():
	def check_bin(b):
		assert(type(b) is list)
		assert(len(b) == 6)
		lib.obj_spec_is_I1(b[0])
		lib.obj_spec_is_I2(b[1])
		lib.obj_spec_is_I3(b[2])
		lib.obj_spec_is_S(b[3], 10)
		lib.obj_spec_is_B(b[4], 20)
		lib.obj_spec_is_D(b[5])

	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o [I1,I2,I3,S10,B20,D] --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: check_bin(bins["testbin"]))

def test_map():
	def check_bin(b):
		assert(type(b) is dict)
		assert(len(b) == 50)
		for key in b:
			lib.obj_spec_is_S(key, 5)
			lib.obj_spec_is_I4(b[key])

	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o {50*S5:I4} --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: check_bin(bins["testbin"]))

def test_compound():
	def check_bin(b):
		assert(type(b) is list)
		assert(len(b) == 3)

		assert(type(b[0]) is dict)
		assert(len(b[0]) == 50)
		for key in b[0]:
			lib.obj_spec_is_S(key, 5)
			lib.obj_spec_is_I4(b[0][key])

		lib.obj_spec_is_I3(b[1])

		assert(type(b[2]) is list)
		assert(len(b[2]) == 3)

		lib.obj_spec_is_D(b[2][0])
		lib.obj_spec_is_I2(b[2][1])
		assert(type(b[2][2]) is dict)
		assert(len(b[2][2]) == 10)
		for key in b[2][2]:
			lib.obj_spec_is_I5(key)
			lib.obj_spec_is_S(b[2][2][key], 11)

	lib.run_benchmark("--workload I --startKey 0 --keys 100 " +
			"-o [{50*S5:I4},I3,[D,I2,{10*I5:S11}]] --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: check_bin(bins["testbin"]))

def test_multiple_bins():
	def check_bins(b):
		assert(len(b) == 6)
		lib.obj_spec_is_I1(b["testbin"])
		lib.obj_spec_is_I2(b["testbin_2"])
		lib.obj_spec_is_I3(b["testbin_3"])
		lib.obj_spec_is_S(b["testbin_4"], 10)
		lib.obj_spec_is_B(b["testbin_5"], 20)
		lib.obj_spec_is_D(b["testbin_6"])

	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o I1,I2,I3,S10,B20,D --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: check_bins(bins))

def test_compound_multiple_bins():
	def check_bins(b):
		assert(len(b) == 6)

		assert(type(b["testbin"]) is list)
		lib.obj_spec_is_I1(b["testbin"][0])
		assert(type(b["testbin"][1]) is dict)
		assert(len(b["testbin"][1]) == 45)
		for key in b["testbin"][1]:
			lib.obj_spec_is_S(key, 32)
			lib.obj_spec_is_B(b["testbin"][1][key], 20)

		lib.obj_spec_is_I2(b["testbin_2"])
		lib.obj_spec_is_I3(b["testbin_3"])

		assert(type(b["testbin_4"]) is dict)
		assert(len(b["testbin_4"]) == 1)
		for key in b["testbin_4"]:
			lib.obj_spec_is_S(key, 10)
			lib.obj_spec_is_I4(b["testbin_4"][key])

		lib.obj_spec_is_B(b["testbin_5"], 20)

		assert(type(b["testbin_6"]) is list)
		assert(len(b["testbin_6"]) == 10)
		for item in b["testbin_6"]:
			lib.obj_spec_is_D(item)

	lib.run_benchmark("--workload I --startKey 0 --keys 100 " +
			"-o [I1,{45*S32:B20}],I2,I3,{S10:I4},B20,[10*D] --random")
	lib.check_for_range(0, 100, lambda meta, key, bins: check_bins(bins))

