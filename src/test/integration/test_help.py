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
	for token in ["--seed", "C[I|K]", "-w C,80,100,10", "-w CI,30,1000,10",
			"-w CK,90,50", "Bin names:"]:
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


def test_print_args_shows_seed():
	rc, out = lib.run_benchmark_output(["--workload", "I", "--keys", "1",
		"--seed", "99"])
	assert(rc == 0)
	assert("seed:                   99" in out)
