import glob
import os
import re

import pytest

import lib

EXAMPLES_DIR = lib.absolute_path("../../../examples/synth")
N_KEYS = 200


def shrink(text):
	text = re.sub(r"duration: \d+", "duration: 2", text)
	return re.sub(r"key-end: \d+", "key-end: %d" % (1 + N_KEYS), text)


def check_customers(bins):
	lib.obj_spec_is_uuid(bins["id"])
	lib.obj_spec_is_email(bins["email"])
	lib.obj_spec_is_zip(bins["zip"])
	lib.obj_spec_in(bins["tier"], ["basic", "gold", "platinum"])


def check_orders(bins):
	assert(len(bins["items"]) == 3)
	assert(re.match(r"^\d+ .+, .+, [A-Z]{2} \d{5}$", bins["ship_to"]))


def check_iot(bins):
	lib.obj_spec_is_geojson_point(bins["loc"], 37.70, 37.80, -122.50, -122.40)
	lib.obj_spec_is_geo_circle(bins["zone"], 50, 500)
	lib.obj_spec_is_mac(bins["mac"])


def check_feed(bins):
	assert(0 < len(bins["events"]) <= 100)
	for ev in bins["events"]:
		lib.obj_spec_in(ev["type"], ["view", "click", "buy"])


def check_timeseries(bins):
	assert(0 < len(bins["samples"]) <= 1440)


def check_sessions(bins):
	assert(0 < len(bins["attrs"]) <= 50)


def check_leaderboard(bins):
	assert(0 < len(bins["scores"]) <= 1000)
	assert(all(type(v) is int for v in bins["scores"].values()))


def check_alltypes(bins):
	assert(len(bins) == 37)
	assert(bins["verbatim"] == "This")
	lib.obj_spec_is_geojson_point(bins["geo"])
	lib.obj_spec_is_geo_circle(bins["zone"])
	assert(re.match(r"^\$\d+$", bins["number_string"]))
	assert(re.match(r"^[0-9.]+:\d+$", bins["ipport"]))
	assert(len(bins["listmapslist"]) == 3)
	for m in bins["listmapslist"]:
		assert(len(m) == 3)
		for ip, cities in m.items():
			lib.obj_spec_is_ipv4(ip)
			assert(len(cities) == 3)


def check_selectivity(bins):
	assert(0 <= int(bins["s100"]) <= 99)
	assert(0 <= int(bins["s10"]) <= 9)
	assert(bins["s1"] == "0")
	lib.obj_spec_in(bins["tier"], ["gold", "silver", "bronze"])
	assert(set(bins["attrs"]) == {"color", "size", "weight"})


def check_nested(bins):
	assert(re.match(r"^c:\d{9}\|a:.+$", bins["cid"]))
	lib.obj_spec_is_zip(bins["profile"]["address"]["zip"])
	assert(set(bins["profile"]["name"]) == {"first", "last"})
	assert(len(bins["endpoints"]) == 3)
	for ep in bins["endpoints"]:
		lib.obj_spec_is_ipv4(ep["ip"])
		lib.obj_spec_is_mac(ep["mac"])
	assert(len(bins["routes"]) == 3)
	assert(len(bins["tags"]) == 5)


def check_large(bins):
	assert(len(bins["payload"]) == 1024)
	lib.obj_spec_in(bins["partner"], ["1212121212", "3434343434", "5656565656"])


def check_session_cache(bins, meta):
	lib.obj_spec_is_uuid(bins["sid"])
	assert(len(bins["cart"]) == 3)
	assert(1700 <= meta["ttl"] <= 1800)


def check_batch_catalog(bins):
	assert(re.match(r"^SKU-\d{6}$", bins["sku"]))
	lib.obj_spec_is_int_range(bins["stock"], 0, 500)
	assert(len(bins["tags"]) == 3)


def check_partial_updates(bins):
	assert(set(bins) == {"first", "last", "email", "balance", "last_seen", "address"})
	lib.obj_spec_is_double_range(bins["balance"], 0, 10000)


def check_ramp(bins):
	lib.obj_spec_is_geojson_point(bins["loc"])


EXPECT_EMPTY = {"write_delete.yaml"}
WITH_META = {"session_cache.yaml"}

CHECKS = {
	"alltypes.yaml": check_alltypes,
	"selectivity.yaml": check_selectivity,
	"nested_documents.yaml": check_nested,
	"write_delete.yaml": None,
	"large_documents.yaml": check_large,
	"session_cache.yaml": check_session_cache,
	"batch_catalog.yaml": check_batch_catalog,
	"partial_updates.yaml": check_partial_updates,
	"ramp.yaml": check_ramp,
	"customers.yaml": check_customers,
	"orders.yaml": check_orders,
	"iot_geo.yaml": check_iot,
	"feed.yaml": check_feed,
	"timeseries.yaml": check_timeseries,
	"sessions.yaml": check_sessions,
	"leaderboard.yaml": check_leaderboard,
}


def test_every_example_has_a_check():
	names = {os.path.basename(p) for p in glob.glob(os.path.join(EXAMPLES_DIR, "*.yaml"))}
	assert(names == set(CHECKS))


@pytest.mark.parametrize("name", sorted(CHECKS))
def test_example(name):
	with open(os.path.join(EXAMPLES_DIR, name)) as f:
		text = shrink(f.read())
	lib.start()
	path = lib.temporary_path("yml")
	with open(path, "w") as f:
		f.write(text)

	lib.run_benchmark(["--workload-stages", path])
	recs = lib.scan_records()
	if name in EXPECT_EMPTY:
		assert(len(recs) == 0)
		return
	assert(len(recs) > 0)
	for rec in recs:
		if name in WITH_META:
			CHECKS[name](rec[2], rec[1])
		else:
			CHECKS[name](rec[2])
