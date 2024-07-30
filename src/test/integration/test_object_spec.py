
import lib

def test_b():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "b", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_b(bins["testbin"]))

def test_const_b_true():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "true", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_const_b(bins["testbin"], True))

def test_const_b_false():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "false", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_const_b(bins["testbin"], False))

def test_I1():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "I1", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_I1(bins["testbin"]))

def test_I2():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "I2", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_I2(bins["testbin"]))

def test_I3():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "I3", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_I3(bins["testbin"]))

def test_I4():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "I4", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_I4(bins["testbin"]))

def test_I5():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "I5", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_I5(bins["testbin"]))

def test_I6():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "I6", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_I6(bins["testbin"]))

def test_I7():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "I7", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_I7(bins["testbin"]))

def test_I8():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "I8", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_I8(bins["testbin"]))

def test_const_I():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "123", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_const_I(bins["testbin"], 123))

def test_D():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "D", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_D(bins["testbin"]))

def test_const_D():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "123.456", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_const_D(bins["testbin"], 123.456))

def test_S1():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "S1", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 1))

def test_S2():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "S2", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 2))

def test_S3():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "S3", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 3))

def test_S4():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "S4", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 4))

def test_S5():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "S5", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 5))

def test_S6():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "S6", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 6))

def test_S7():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "S7", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 7))

def test_S8():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "S8", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 8))

def test_S100():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "S100", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 100))

def test_S10000():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "S10000", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_S(bins["testbin"], 10000))

def test_const_S():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "\"test string\"", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_const_S(bins["testbin"], "test string"))

def test_B1():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "B1", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 1))

def test_B2():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "B2", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 2))

def test_B3():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "B3", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 3))

def test_B4():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "B4", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 4))

def test_B5():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "B5", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 5))

def test_B6():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "B6", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 6))

def test_B7():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "B7", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 7))

def test_B8():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "B8", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 8))

def test_B100():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "B100", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 100))

def test_B10000():
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o", "B10000", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: lib.obj_spec_is_B(bins["testbin"], 10000))

def test_list():
	def check_bin(b):
		assert(type(b) is list)
		assert(len(b) == 7)
		lib.obj_spec_is_I1(b[0])
		lib.obj_spec_is_I2(b[1])
		lib.obj_spec_is_I3(b[2])
		lib.obj_spec_is_S(b[3], 10)
		lib.obj_spec_is_B(b[4], 20)
		lib.obj_spec_is_D(b[5])
		lib.obj_spec_is_b(b[6])

	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100",
		"-o", "[I1,I2,I3,S10,B20,D,b]", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: check_bin(bins["testbin"]))

def test_map():
	def check_bin(b):
		# Python client may return a aerospike.KeyOrderedDict
		assert(issubclass(type(b), dict))
		assert(len(b) == 50)
		for key in b:
			lib.obj_spec_is_S(key, 5)
			lib.obj_spec_is_I4(b[key])

	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100",
		"-o", "{50*S5:I4}", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: check_bin(bins["testbin"]))

def test_const_map():
	def check_bin(b):
		# Python client may return a aerospike.KeyOrderedDict
		assert(issubclass(type(b), dict))
		assert(len(b) == 1)
		for key in b:
			lib.obj_spec_is_const_I(key, 123)
			lib.obj_spec_is_const_S(b[key], "string")

	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100",
		"-o", "{123:\"string\"}", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: check_bin(bins["testbin"]))

def test_compound():
	def check_bin(b):
		assert(type(b) is list)
		assert(len(b) == 3)

		# Python client may return a aerospike.KeyOrderedDict
		assert(issubclass(type(b[0]), dict))
		assert(len(b[0]) == 50)
		for key in b[0]:
			lib.obj_spec_is_S(key, 5)
			lib.obj_spec_is_I4(b[0][key])

		lib.obj_spec_is_I3(b[1])

		assert(type(b[2]) is list)
		assert(len(b[2]) == 4)

		lib.obj_spec_is_D(b[2][0])
		lib.obj_spec_is_I2(b[2][1])

		# Python client may return a aerospike.KeyOrderedDict
		assert(issubclass(type(b[2][2]), dict))
		assert(len(b[2][2]) == 10)
		for key in b[2][2]:
			lib.obj_spec_is_I5(key)
			lib.obj_spec_is_S(b[2][2][key], 11)
		lib.obj_spec_is_b(b[2][3])

	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100",
		"-o", "[{50*S5:I4},I3,[D,I2,{10*I5:S11},b]]", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: check_bin(bins["testbin"]))

def test_multiple_bins():
	def check_bins(b):
		assert(len(b) == 7)
		lib.obj_spec_is_I1(b["testbin"])
		lib.obj_spec_is_I2(b["testbin_2"])
		lib.obj_spec_is_I3(b["testbin_3"])
		lib.obj_spec_is_S(b["testbin_4"], 10)
		lib.obj_spec_is_B(b["testbin_5"], 20)
		lib.obj_spec_is_D(b["testbin_6"])
		lib.obj_spec_is_b(b["testbin_7"])

	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100", "-o",
		"I1,I2,I3,S10,B20,D,b", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: check_bins(bins))

def test_compound_multiple_bins():
	def check_bins(b):
		assert(len(b) == 7)

		assert(type(b["testbin"]) is list)
		lib.obj_spec_is_I1(b["testbin"][0])

		# Python client may return a aerospike.KeyOrderedDict
		assert(issubclass(type(b["testbin"][1]), dict))
		assert(len(b["testbin"][1]) == 45)
		for key in b["testbin"][1]:
			lib.obj_spec_is_S(key, 32)
			lib.obj_spec_is_B(b["testbin"][1][key], 20)

		lib.obj_spec_is_I2(b["testbin_2"])
		lib.obj_spec_is_I3(b["testbin_3"])

		# Python client may return a aerospike.KeyOrderedDict
		assert(issubclass(type(b["testbin_4"]), dict))
		assert(len(b["testbin_4"]) == 1)
		for key in b["testbin_4"]:
			lib.obj_spec_is_S(key, 10)
			lib.obj_spec_is_I4(b["testbin_4"][key])

		lib.obj_spec_is_B(b["testbin_5"], 20)

		assert(type(b["testbin_6"]) is list)
		assert(len(b["testbin_6"]) == 10)
		for item in b["testbin_6"]:
			lib.obj_spec_is_D(item)

		lib.obj_spec_is_b(b["testbin_7"])

	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", "100",
		"-o", "[I1,{45*S32:B20}],I2,I3,{S10:I4},B20,[10*D],b", "--random"])
	lib.check_for_range(0, 100, lambda meta, key, bins: check_bins(bins))

