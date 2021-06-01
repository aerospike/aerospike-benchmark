
import lib

udf_module = """
function increment_bin_to_2(rec, bin_name)
	if rec[bin_name] == 1 then
		rec[bin_name] = rec[bin_name] + 1
		aerospike:update(rec)
	end
end

function set_bin(rec, bin_name, bool_val)
	rec[bin_name] = bool_val
	aerospike:update(rec)
end

"""

def test_random_udf():
	lib.upload_udf("test_module.lua", udf_module)
	# initialize all records to have just one bin "testbin" with value 1
	lib.run_benchmark("--workload I --startKey 0 --keys 20 -o 1")
	# the UDF should eventually reach all 20 records, incrementing "testbin" to 2
	lib.run_benchmark("--duration 1 --workload RUF,0,0 -upn test_module " +
			"-ufn increment_bin_to_2 -ufv \\\"testbin\\\" --startKey 0 --keys 20",
			do_reset=False)
	lib.check_for_range(0, 20, lambda meta, key, bins: lib.obj_spec_is_const_I(bins["testbin"], 2))

def test_random_udf_async():
	lib.upload_udf("test_module.lua", udf_module)
	# initialize all records to have just one bin "testbin" with value 1
	lib.run_benchmark("--workload I --startKey 0 --keys 20 -o 1")
	# the UDF should eventually reach all 20 records, incrementing "testbin" to 2
	lib.run_benchmark("--duration 1 --workload RUF,0,0 -upn test_module " +
			"-ufn increment_bin_to_2 -ufv \\\"testbin\\\" --startKey 0 " +
			"--keys 20 --async",
			do_reset=False)
	lib.check_for_range(0, 20, lambda meta, key, bins: lib.obj_spec_is_const_I(bins["testbin"], 2))

def test_random_udf_subrange():
	lib.upload_udf("test_module.lua", udf_module)
	# initialize all records to have just one bin "testbin" with value 1
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o 1")
	# the UDF should eventually reach these 20 records, incrementing "testbin" to 2
	lib.run_benchmark("--duration 1 --workload RUF,0,0 -upn test_module " +
			"-ufn increment_bin_to_2 -ufv \\\"testbin\\\" --startKey 15 --keys 20",
			do_reset=False)

	assert(len(lib.scan_records()) == 100)
	lib.check_recs_exist_in_range(0, 15, lambda meta, key, bins: lib.obj_spec_is_const_I(bins["testbin"], 1))
	lib.check_recs_exist_in_range(15, 35, lambda meta, key, bins: lib.obj_spec_is_const_I(bins["testbin"], 2))
	lib.check_recs_exist_in_range(35, 100, lambda meta, key, bins: lib.obj_spec_is_const_I(bins["testbin"], 1))

def test_random_udf_subrange_async():
	lib.upload_udf("test_module.lua", udf_module)
	# initialize all records to have just one bin "testbin" with value 1
	lib.run_benchmark("--workload I --startKey 0 --keys 100 -o 1")
	# the UDF should eventually reach these 20 records, incrementing "testbin" to 2
	lib.run_benchmark("--duration 1 --workload RUF,0,0 -upn test_module " +
			"-ufn increment_bin_to_2 -ufv \\\"testbin\\\" --startKey 15 " +
			"--keys 20 --async",
			do_reset=False)

	assert(len(lib.scan_records()) == 100)
	lib.check_recs_exist_in_range(0, 15, lambda meta, key, bins: lib.obj_spec_is_const_I(bins["testbin"], 1))
	lib.check_recs_exist_in_range(15, 35, lambda meta, key, bins: lib.obj_spec_is_const_I(bins["testbin"], 2))
	lib.check_recs_exist_in_range(35, 100, lambda meta, key, bins: lib.obj_spec_is_const_I(bins["testbin"], 1))

def test_set_bin_random_bool():
	true_cnt = 0
	false_cnt = 0
	def check_bin(meta, key, bins):
		nonlocal true_cnt
		nonlocal false_cnt

		assert("bool_bin" in bins)
		lib.obj_spec_is_b(bins["bool_bin"])
		if bins["bool_bin"]:
			true_cnt += 1
		else:
			false_cnt += 1

	lib.upload_udf("test_module.lua", udf_module)
	# initialize all records to have just one bin "testbin" with value 1
	lib.run_benchmark("--workload I --startKey 0 --keys 20 -o 1")
	# the UDF should eventually reach all 20 records, incrementing "testbin" to 2
	lib.run_benchmark("--duration 1 --workload RUF,0,0 -upn test_module " +
			"-ufn set_bin -ufv \\\"bool_bin\\\",b --startKey 0 --keys 20",
			do_reset=False)
	lib.check_for_range(0, 20, check_bin)
	assert(true_cnt > 0 and false_cnt > 0)

def test_set_bin_random_bool_async():
	true_cnt = 0
	false_cnt = 0
	def check_bin(meta, key, bins):
		nonlocal true_cnt
		nonlocal false_cnt

		assert("bool_bin" in bins)
		lib.obj_spec_is_b(bins["bool_bin"])
		if bins["bool_bin"]:
			true_cnt += 1
		else:
			false_cnt += 1

	lib.upload_udf("test_module.lua", udf_module)
	# initialize all records to have just one bin "testbin" with value 1
	lib.run_benchmark("--workload I --startKey 0 --keys 20 -o 1")
	# the UDF should eventually reach all 20 records, incrementing "testbin" to 2
	lib.run_benchmark("--duration 1 --workload RUF,0,0 -upn test_module " +
			"-ufn set_bin -ufv \\\"bool_bin\\\",b --startKey 0 --keys 20 " +
			"--async",
			do_reset=False)
	lib.check_for_range(0, 20, check_bin)
	assert(true_cnt > 0 and false_cnt > 0)

