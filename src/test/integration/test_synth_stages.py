import lib


def write_stages(text):
	lib.start()
	path = lib.temporary_path("yml")
	with open(path, "w") as f:
		f.write(text)
	return path


PRELOAD_THEN_CDT = """
- stage: 1
  desc: "preload"
  workload: I
  key-start: 0
  key-end: 50
  object-spec: 'profile=@full_name, events=[2*{"ts":@timestamp,"type":@pick("a","b")}]'
- stage: 2
  desc: "cdt"
  workload: C,80,20,5
  duration: 3
  key-start: 0
  key-end: 50
"""


def test_preload_then_cdt_inherits_spec():
	path = write_stages(PRELOAD_THEN_CDT)
	rc, out = lib.run_benchmark_output(["--workload-stages", path])
	assert(rc == 0)
	assert("Stage 1: the object spec uses @generators" in out)

	def check(meta, key, bins):
		assert(set(bins) == {"profile", "events"})
		lib.obj_spec_is_ascii_str(bins["profile"])
		assert(2 <= len(bins["events"]) <= 20)
		for ev in bins["events"]:
			lib.obj_spec_in(ev["type"], ["a", "b"])
	lib.check_for_range(0, 50, check)


SEEDED = """
- stage: 1
  workload: I
  key-start: 0
  key-end: 100
  object-spec: 'email=@email, loc=@geojson, tags=[3*@word]'
"""


def test_seed_with_workload_stages():
	path = write_stages(SEEDED)
	lib.run_benchmark(["--workload-stages", path, "--seed", "11"])
	a = lib.get_records(0, 100)
	lib.run_benchmark(["--workload-stages", path, "--seed", "11", "-z", "2"])
	assert(lib.get_records(0, 100) == a)


BAD = """
- stage: 1
  workload: CI
  key-start: 0
  key-end: 10
  object-spec: '{3*S4:S4}'
"""


def test_invalid_stage_fails():
	path = write_stages(BAD)
	lib.run_benchmark(["--workload-stages", path], expect_success=False)
