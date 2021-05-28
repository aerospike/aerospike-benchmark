
import lib

udf_module = """
function increment_bin_to_2(rec, bin_name)
	if rec[bin_name] == 1 then
		rec[bin_name] = rec[bin_name] + 1
		aerospike:update(rec)
	end
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

