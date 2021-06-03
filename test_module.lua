
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

