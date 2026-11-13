import re
import shlex
import subprocess

import pytest

import lib

README = lib.absolute_path("../../../README.md")
ROOT = lib.absolute_path("../../..")


def test_help_long():
	# test that the help command return code is 0
	lib.run_benchmark(["--help"])


def help_text():
	return subprocess.run(["test_target/asbench", "--help"], cwd=ROOT,
			stdout=subprocess.PIPE, text=True, check=True).stdout


def test_help_says_sa_options_are_command_line_only():
	# asbackup, aql and asadm read [secret-agent] from astools.conf; asbench does not.
	text = " ".join(help_text().split())
	assert("asbench reads no tools config file, so a [secret-agent] section in "
			"astools.conf does not apply." in text)


def readme_text():
	with open(README) as f:
		return f.read()


def readme_generators():
	return set(re.findall(r"^\| `(@[a-z_0-9]+)` \|", readme_text(), re.M))


def test_readme_lists_every_generator_in_help():
	gens = readme_generators()
	assert(len(gens) >= 39)
	text = help_text()
	for gen in gens:
		assert(gen in text), gen


def test_help_lists_seed_and_cdt():
	text = help_text()
	for token in ["--seed", "--read-touch-ttl-percent", "C[I|K]", "-w C,80,100,10",
			"-w CI,30,1000,10", "-w CK,90,50", "-w GH", "Bin names:"]:
		assert(token in text), token


def readme_commands():
	"""
	Every asbench command line and bare -o spec shown in README code blocks.
	"""
	text = readme_text()
	cmds = []
	for block in re.findall(r"```sh\n(.*?)```", text, re.S):
		joined = block.replace("\\\n", " ")
		for line in joined.splitlines():
			line = line.strip()
			if re.match(r"(target/)?asbench ", line) and "--help" not in line and \
					"--workload-stages" not in line:
				cmds.append(shlex.split(line)[1:])
			elif line.startswith("-o "):
				cmds.append(shlex.split(line))
	return cmds


def test_readme_has_commands():
	assert(len(readme_commands()) >= 10)


@pytest.mark.parametrize("args", readme_commands(), ids=lambda a: " ".join(a)[:60])
def test_readme_command_parses(args):
	proc = subprocess.run(["test_target/asbench"] + args + ["--gen-bench", "10",
		"-z", "1"], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
		text=True)
	assert(proc.returncode == 0), proc.stdout


def test_gen_bench_without_server():
	proc = subprocess.run(["test_target/asbench", "-h", "127.0.0.1:1",
		"--gen-bench", "1000", "-o", "@email, [3*@word]"], cwd=ROOT,
		stdout=subprocess.PIPE, text=True)
	assert(proc.returncode == 0)
	assert("records/s" in proc.stdout)


@pytest.mark.parametrize("count", ["-5", "-1", " 10", "+10", "0", "abc", "0x10",
	"", "10x", "18446744073709551616"])
def test_gen_bench_invalid_count(count):
	proc = subprocess.run(["test_target/asbench", "-h", "127.0.0.1:1",
		"--gen-bench", count, "-o", "I"], cwd=ROOT, stdout=subprocess.PIPE,
		stderr=subprocess.STDOUT, text=True, timeout=10)
	assert(proc.returncode == 1), proc.stdout
	assert(f'Invalid gen-bench record count "{count}"' in proc.stdout), proc.stdout


def test_print_args_shows_seed():
	rc, out = lib.run_benchmark_output(["--workload", "I", "--keys", "1",
		"--seed", "99"])
	assert(rc == 0)
	assert("seed:                   99" in out)


@pytest.mark.parametrize("seed", ["-1", " 42", "+42", "0x10", "", "abc", "42x",
	"1.5", "18446744073709551616"])
def test_seed_invalid(seed):
	rc, out = run_without_server(["--seed", seed])
	assert(rc == 1), out
	assert(f'Invalid seed "{seed}", expected an unsigned 64-bit integer' in out), out


@pytest.mark.parametrize("seed,shown", [("0", "0"), ("42", "42"), ("010", "10"),
	("18446744073709551615", "18446744073709551615")])
def test_seed_valid_is_decimal(seed, shown):
	rc, out = lib.run_benchmark_output(["--workload", "I", "--keys", "1",
		"--seed", seed])
	assert(rc == 0), out
	assert(f"seed:                   {shown}\n" in out), out


def run_without_server(args):
	proc = subprocess.run(["test_target/asbench", "-h", "127.0.0.1:1",
		"--gen-bench", "10", "-z", "1"] + args, cwd=ROOT,
		stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
	return proc.returncode, proc.stdout


@pytest.mark.parametrize("pct", ["-1", "0", "1", "100"])
def test_read_touch_ttl_percent_valid(pct):
	rc, out = run_without_server(["--read-touch-ttl-percent", pct])
	assert(rc == 0), out


@pytest.mark.parametrize("pct", ["-2", "101", "abc", "", "50x", "1.5"])
def test_read_touch_ttl_percent_invalid(pct):
	rc, out = run_without_server(["--read-touch-ttl-percent", pct])
	assert(rc == 1), out
	assert(f'Invalid read-touch-ttl-percent "{pct}", expected -1 to 100' in out), out


def test_print_args_shows_read_touch_ttl_percent():
	rc, out = lib.run_benchmark_output(["--workload", "I", "--keys", "1",
		"--read-touch-ttl-percent", "-1"])
	assert(rc == 0), out
	assert("read touch ttl percent: -1" in out), out
