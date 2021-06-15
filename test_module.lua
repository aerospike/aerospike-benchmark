
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

function read_object(rec, obj)
	write_bin_1 = obj["write_bin_1"]
	write_value = obj["random_value"]

	if (not aerospike:exists(rec)) then
		aerospike:create(rec)
	end

	if (write_bin_1) then
		rec["bin_1"] = write_value
	else
		rec["bin_2"] = write_value + 256
	end
	aerospike:update(rec)
end

