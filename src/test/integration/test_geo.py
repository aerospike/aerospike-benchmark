import aerospike
from aerospike import predicates as p

import lib

N_KEYS = 500

SF_BBOX = "37.70,37.80,-122.50,-122.40"
SF_CENTER = (-122.45, 37.75)
NYC = (-74.0, 40.7)


def insert(spec, keys=N_KEYS):
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", str(keys),
		"-o", spec])


def test_geojson_is_native_particle():
	insert("loc=@geojson")
	lib.check_for_range(0, N_KEYS, lambda meta, key, bins:
			lib.obj_spec_is_geojson_point(bins["loc"]))


def test_geojson_bbox():
	insert("loc=@geojson(" + SF_BBOX + ")")
	lib.check_for_range(0, N_KEYS, lambda meta, key, bins:
			lib.obj_spec_is_geojson_point(bins["loc"], 37.70, 37.80, -122.50, -122.40))


def test_geo_circle_particle():
	insert("zone=@geo_circle(100,500)")
	lib.check_for_range(0, N_KEYS, lambda meta, key, bins:
			lib.obj_spec_is_geo_circle(bins["zone"], 100, 500))


def test_within_radius_query():
	insert("loc=@geojson(" + SF_BBOX + "), city=@city")
	lib.create_index("geo", "loc", "idx_loc")

	near = lib.query_keys(p.geo_within_radius("loc", SF_CENTER[0], SF_CENTER[1],
		20000), expected=N_KEYS)
	assert(len(near) == N_KEYS)

	far = lib.query_keys(p.geo_within_radius("loc", NYC[0], NYC[1], 20000))
	assert(len(far) == 0)


def test_within_region_query():
	insert("loc=@geojson(" + SF_BBOX + ")")
	lib.create_index("geo", "loc", "idx_loc_region")
	region = aerospike.GeoJSON({"type": "Polygon", "coordinates": [[
		[-122.51, 37.69], [-122.39, 37.69], [-122.39, 37.81], [-122.51, 37.81],
		[-122.51, 37.69]]]})
	keys = lib.query_keys(p.geo_within_geojson_region("loc", region.dumps()),
			expected=N_KEYS)
	assert(len(keys) == N_KEYS)


def test_geojson_in_list_with_list_index():
	insert("visits=[3*@geojson(" + SF_BBOX + ")]")
	lib.check_for_range(0, N_KEYS, lambda meta, key, bins:
			[lib.obj_spec_is_geojson_point(v, 37.70, 37.80, -122.50, -122.40)
				for v in bins["visits"]])
	lib.create_index("geo", "visits", "idx_visits", aerospike.INDEX_TYPE_LIST)
	keys = lib.query_keys(p.geo_within_radius("visits", SF_CENTER[0], SF_CENTER[1],
		20000, aerospike.INDEX_TYPE_LIST), expected=N_KEYS)
	assert(len(keys) == N_KEYS)


def test_geojson_as_map_values():
	insert("places={2*@city:@geojson}")
	lib.check_for_range(0, N_KEYS, lambda meta, key, bins:
			[lib.obj_spec_is_geojson_point(v) for v in bins["places"].values()])
