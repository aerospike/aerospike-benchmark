import aerospike
import pytest
from aerospike_helpers.operations import map_operations

import lib

N_KEYS = 50
DURATION = "3"


def preload(spec, keys=N_KEYS):
	lib.run_benchmark(["--workload", "I", "--start-key", "0", "--keys", str(keys),
		"-o", spec, "--random"])


def run_cdt(workload, spec, extra=None, keys=N_KEYS, do_reset=False,
		expect_success=True):
	args = ["--duration", DURATION, "--workload", workload, "--start-key", "0",
		"--keys", str(keys), "-o", spec]
	if extra:
		args += extra
	lib.run_benchmark(args, do_reset=do_reset, expect_success=expect_success)


def all_bins():
	return [rec[2] for rec in lib.scan_records()]


@pytest.mark.parametrize("mode", [[], ["--async"]], ids=["sync", "async"])
def test_list_append_cap(mode):
	preload("[5*I1]")
	run_cdt("C,0,20,5", "[5*I1]", mode)

	def check(meta, key, bins):
		assert(type(bins["testbin"]) is list)
		assert(len(bins["testbin"]) == 20)
	lib.check_for_range(0, N_KEYS, check)


def test_list_append_no_preload():
	run_cdt("C,0,20", "[3*I2]", do_reset=True)
	recs = all_bins()
	assert(0 < len(recs) <= N_KEYS)
	for bins in recs:
		assert(0 < len(bins["testbin"]) <= 20)


def test_list_append_unbounded_grows():
	preload("[I1]")
	run_cdt("C,0", "[2*I1]")
	total = sum(len(b["testbin"]) for b in all_bins())
	assert(total > N_KEYS * 2)


@pytest.mark.parametrize("mode", [[], ["--async"]], ids=["sync", "async"])
def test_map_put_cap(mode):
	preload("{3*S4:I1}")
	run_cdt("C,0,10,3", "{3*S4:I1}", mode)

	def check(meta, key, bins):
		assert(isinstance(bins["testbin"], dict))
		assert(3 <= len(bins["testbin"]) <= 10)
	lib.check_for_range(0, N_KEYS, check)


@pytest.mark.parametrize("mode", [[], ["--async"]], ids=["sync", "async"])
def test_map_increment(mode):
	run_cdt("CI,0,0,5", "scores={4*S1:I1}", mode, keys=20, do_reset=True)
	recs = all_bins()
	assert(len(recs) > 0)
	total = 0
	for bins in recs:
		for v in bins["scores"].values():
			assert(type(v) is int)
			total += v
	assert(total > 20 * 4 * 255)


def test_map_increment_cap_keeps_top_by_rank():
	run_cdt("CI,0,8,5", "scores={5*@username:@int(1,100)}", keys=10, do_reset=True)
	for bins in all_bins():
		assert(len(bins["scores"]) <= 8)


def test_leaderboard_top_k_read():
	run_cdt("CI,0,1000", "scores={5*@username:I1}", keys=10, do_reset=True)
	run_cdt("CI,100,1000,10", "scores={5*@username:I1}")
	for key in range(10):
		_, _, bins = lib.CLIENT.operate((lib.NAMESPACE, lib.SET, key), [
			map_operations.map_get_by_rank_range("scores", -10, 10,
				aerospike.MAP_RETURN_KEY_VALUE)])
		flat = bins["scores"]
		assert(len(flat) == 20)
		values = flat[1::2]
		assert(values == sorted(values))


def test_map_get_by_key():
	preload("attrs={3*@pick(\"theme\",\"lang\",\"tz\",\"plan\"):S8}")
	run_cdt("CK,50,10", "attrs={3*@pick(\"theme\",\"lang\",\"tz\",\"plan\"):S8}")
	for bins in all_bins():
		assert(len(bins["attrs"]) <= 10)
		assert(set(bins["attrs"]) <= {"theme", "lang", "tz", "plan"})


def test_cdt_read_only_leaves_records_unchanged():
	preload("[5*I1], {3*S4:I1}")
	before = lib.get_records(0, N_KEYS)
	run_cdt("C,100,0,5", "[5*I1], {3*S4:I1}")
	assert(lib.get_records(0, N_KEYS) == before)


@pytest.mark.parametrize("mode", [[], ["--async"]], ids=["sync", "async"])
def test_activity_feed_with_generators(mode):
	spec = ("events=[1*{\"ts\":@timestamp(1700000000,1800000000),"
		"\"type\":@pick(\"view\",\"click\",\"buy\"),\"sku\":@product}], "
		"updated=@now")
	run_cdt("C,50,20,5", spec, mode, do_reset=True)
	recs = all_bins()
	assert(len(recs) > 0)
	for bins in recs:
		assert(0 < len(bins["events"]) <= 20)
		lib.obj_spec_is_int_range(bins["updated"], 1600000000000, 4000000000000)
		for ev in bins["events"]:
			assert(set(ev) == {"ts", "type", "sku"})
			lib.obj_spec_in(ev["type"], ["view", "click", "buy"])


def test_time_series_samples():
	spec = "samples=[1*[@timestamp,@double(0,100)]]"
	run_cdt("C,20,30,10", spec, do_reset=True)
	for bins in all_bins():
		assert(0 < len(bins["samples"]) <= 30)
		for sample in bins["samples"]:
			assert(len(sample) == 2)
			lib.obj_spec_is_double_range(sample[1], 0, 100)


def test_geo_checkins_list():
	spec = "visits=[1*{\"at\":@timestamp,\"loc\":@geojson,\"city\":@city}]"
	run_cdt("C,50,200,20", spec, do_reset=True)
	for bins in all_bins():
		for visit in bins["visits"]:
			lib.obj_spec_is_geojson_point(visit["loc"])


@pytest.mark.parametrize("workload,spec", [
	("C", "I"),
	("CI", "{3*S4:S4}"),
	("CI", "[3*I1]"),
	("CI", "{1001*@username:@int(1,9)}"),
	("CK", "[3*I1]"),
])
def test_cdt_rejects_spec(workload, spec):
	run_cdt(workload, spec, do_reset=True, expect_success=False)


def test_cdt_rejects_batch():
	run_cdt("C", "[3*I1]", ["--batch-size", "5"], do_reset=True,
			expect_success=False)


def test_cdt_rejects_bad_workload():
	run_cdt("C,101", "[3*I1]", do_reset=True, expect_success=False)
