import aerospike
import pytest

import lib

N_KEYS = 100

GENERATORS = [
	("@first_name", lambda v: lib.obj_spec_is_ascii_str(v)),
	("@last_name", lambda v: lib.obj_spec_is_ascii_str(v)),
	("@full_name", lambda v: (lib.obj_spec_is_ascii_str(v), v.index(" "))),
	("@username", lambda v: lib.obj_spec_is_ascii_str(v)),
	("@email", lib.obj_spec_is_email),
	("@phone", lib.obj_spec_is_phone),
	("@street", lambda v: (lib.obj_spec_is_ascii_str(v), int(v.split(" ")[0]))),
	("@city", lambda v: lib.obj_spec_is_ascii_str(v)),
	("@state", lambda v: lib.obj_spec_is_ascii_str(v)),
	("@state_abbr", lib.obj_spec_is_state_abbr),
	("@zip", lib.obj_spec_is_zip),
	("@country", lambda v: lib.obj_spec_is_ascii_str(v)),
	("@country_code", lib.obj_spec_is_state_abbr),
	("@lat", lambda v: lib.obj_spec_is_double_range(v, 24, 49)),
	("@lon(-10,10)", lambda v: lib.obj_spec_is_double_range(v, -10, 10)),
	("@geojson", lib.obj_spec_is_geojson_point),
	("@geo_circle", lib.obj_spec_is_geo_circle),
	("@company", lambda v: lib.obj_spec_is_ascii_str(v)),
	("@job_title", lambda v: lib.obj_spec_is_ascii_str(v)),
	("@ipv4", lib.obj_spec_is_ipv4),
	("@ipv6", lib.obj_spec_is_ipv6),
	("@mac", lib.obj_spec_is_mac),
	("@url", lambda v: (lib.obj_spec_is_ascii_str(v), v.startswith("https://www.") or 1 / 0)),
	("@domain", lambda v: (lib.obj_spec_is_ascii_str(v), lib.DOMAIN_RE.match(v) or 1 / 0)),
	("@uuid", lib.obj_spec_is_uuid),
	("@word", lambda v: lib.obj_spec_is_words(v, 1)),
	("@words(4)", lambda v: lib.obj_spec_is_words(v, 4)),
	("@sentence", lambda v: (lib.obj_spec_is_ascii_str(v), v.endswith(".") or 1 / 0)),
	("@lorem(30)", lambda v: lib.obj_spec_is_ascii_str(v, 30, 30)),
	("@product", lambda v: lib.obj_spec_is_ascii_str(v)),
	("@color", lambda v: lib.obj_spec_is_ascii_str(v)),
	("@credit_card", lib.obj_spec_is_credit_card),
	("@date", lambda v: lib.obj_spec_is_date(v)),
	("@date(\"2020-01-01\",\"2020-12-31\",\"%d/%m/%Y\")",
		lambda v: lib.obj_spec_is_date(v, "%d/%m/%Y")),
	("@timestamp(1700000000,1800000000)",
		lambda v: lib.obj_spec_is_int_range(v, 1700000000, 1800000000)),
	("@now", lambda v: lib.obj_spec_is_int_range(v, 1600000000000, 4000000000000)),
	("@int(18,90)", lambda v: lib.obj_spec_is_int_range(v, 18, 90)),
	("@double(0.5,2.5)", lambda v: lib.obj_spec_is_double_range(v, 0.5, 2.5)),
	("@pick(\"red\",\"green\",\"blue\")", lambda v: lib.obj_spec_in(v, ["red", "green", "blue"])),
	("@pick(\"gold\":1,\"basic\":99)", lambda v: lib.obj_spec_in(v, ["gold", "basic"])),
	("@fmt(\"#{first_name}.#{last_name}@corp.example\")",
		lambda v: (lib.obj_spec_is_ascii_str(v), v.endswith("@corp.example") or 1 / 0)),
]

CUSTOMER = ("id=@uuid, first=@first_name, last=@last_name, email=@email, "
	"age=@int(18,90), city=@city, loc=@geojson, "
	"orders=[3*{\"sku\":@product,\"qty\":@int(1,9),\"price\":@double(1,500)}]")


def insert(spec, extra=None, keys=N_KEYS, do_reset=True, expect_success=True):
	args = ["--workload", "I", "--start-key", "0", "--keys", str(keys), "-o", spec]
	if extra:
		args += extra
	lib.run_benchmark(args, do_reset=do_reset, expect_success=expect_success)


@pytest.mark.parametrize("spec,check", GENERATORS, ids=[g[0] for g in GENERATORS])
def test_generator(spec, check):
	insert(spec)
	lib.check_for_range(0, N_KEYS, lambda meta, key, bins: check(bins["testbin"]))


def test_named_bins():
	insert("first=@first_name, email=@email, age=@int(18,90), city=@city, tags=[3*@word]")

	def check(meta, key, bins):
		assert(set(bins) == {"first", "email", "age", "city", "tags"})
		lib.obj_spec_is_ascii_str(bins["first"])
		lib.obj_spec_is_email(bins["email"])
		lib.obj_spec_is_int_range(bins["age"], 18, 90)
		assert(type(bins["tags"]) is list and len(bins["tags"]) == 3)
	lib.check_for_range(0, N_KEYS, check)


def test_named_repeats():
	insert("tags=3*@word, n=@int(1,9)")
	lib.check_for_range(0, N_KEYS, lambda meta, key, bins:
			(assert_eq(set(bins), {"tags", "tags_2", "tags_3", "n"})))


def assert_eq(a, b):
	assert a == b, "%r != %r" % (a, b)


def test_named_bins_write_bins():
	insert("first=@first_name, email=@email, age=@int(18,90)", ["--write-bins", "1,3"])
	lib.check_for_range(0, N_KEYS, lambda meta, key, bins:
			assert_eq(set(bins), {"first", "age"}))


def test_named_bins_read_bins():
	spec = "first=@first_name, email=@email, age=@int(18,90), city=@city"
	insert(spec, ["--seed", "3"])
	before = lib.get_records(0, N_KEYS)
	lib.run_benchmark(["--duration", "1", "--workload", "RU,100", "--start-key",
		"0", "--keys", str(N_KEYS), "-o", spec, "--read-bins", "2,4"], do_reset=False)
	assert(lib.get_records(0, N_KEYS) == before)


def test_mixed_legacy_and_generators():
	insert("I4, @email, [3*@word], {2*@word:@int(1,5)}")

	def check(meta, key, bins):
		lib.obj_spec_is_I4(bins["testbin"])
		lib.obj_spec_is_email(bins["testbin_2"])
		assert(len(bins["testbin_3"]) == 3)
		assert(len(bins["testbin_4"]) == 2)
	lib.check_for_range(0, N_KEYS, check)


def test_nested_documents():
	insert(CUSTOMER)

	def check(meta, key, bins):
		lib.obj_spec_is_uuid(bins["id"])
		lib.obj_spec_is_geojson_point(bins["loc"])
		assert(len(bins["orders"]) == 3)
		for order in bins["orders"]:
			assert(set(order) == {"sku", "qty", "price"})
			lib.obj_spec_is_int_range(order["qty"], 1, 9)
			lib.obj_spec_is_double_range(order["price"], 1, 500)
	lib.check_for_range(0, N_KEYS, check)


def seeded_snapshot(spec, seed, extra=None):
	insert(spec, ["--seed", str(seed)] + (extra or []))
	return lib.get_records(0, N_KEYS)


def test_seed_deterministic_across_threads_batch_async():
	base = seeded_snapshot(CUSTOMER, 42, ["--threads", "4"])
	assert(seeded_snapshot(CUSTOMER, 42, ["--threads", "1"]) == base)
	assert(seeded_snapshot(CUSTOMER, 42, ["--batch-write-size", "10"]) == base)
	assert(seeded_snapshot(CUSTOMER, 42, ["--async"]) == base)


def test_seed_differs():
	a = seeded_snapshot(CUSTOMER, 42)
	b = seeded_snapshot(CUSTOMER, 43)
	differ = sum(1 for k in a if a[k] != b[k])
	assert(differ >= 0.9 * N_KEYS)


def test_seed_distinct_records():
	a = seeded_snapshot("email=@email", 1)
	assert(len({r["email"] for r in a.values()}) >= 0.95 * N_KEYS)


def test_seed_legacy_types():
	spec = "I4, S8, [3*I2], D"
	assert(seeded_snapshot(spec, 5) == seeded_snapshot(spec, 5, ["-z", "3"]))


def test_seed_partial_write_matches_full():
	spec = "a=@email, b=@uuid, c=@int(1,1000000)"
	full = seeded_snapshot(spec, 9)
	partial = seeded_snapshot(spec, 9, ["--write-bins", "2"])
	for key in full:
		assert(partial[key] == {"b": full[key]["b"]})


def test_forced_random_note():
	rc, out = lib.run_benchmark_output(["--workload", "I", "--start-key", "0",
		"--keys", "10", "-o", "@email"])
	assert(rc == 0)
	assert("every write generates a new record" in out)
	assert(len({r["testbin"] for r in lib.get_records(0, 10).values()}) >= 9)


@pytest.mark.parametrize("spec", [
	"@nope",
	"@int(90,18)",
	"a=@int(1,2), a=@word",
	"abcdefghijklmnop=@int(1,2)",
	"{@geojson:I}",
	"{60*@state:I}",
	"I, testbin=S4",
	"[a=I]",
])
def test_rejected_specs(spec):
	insert(spec, expect_success=False)


def test_gen_bench_smoke():
	rc, out = lib.run_benchmark_output(["--gen-bench", "1000", "-z", "2", "-o", CUSTOMER])
	assert(rc == 0)
	assert("records/s" in out)
	assert("avg payload" in out)
