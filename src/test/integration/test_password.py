import os
import subprocess

import lib

ROOT = lib.absolute_path("../../..")
SECRET = "s3cr3t-pw"
ENV_VAR = "ASBENCH_IT_PW"
ENV_UNSET = "ASBENCH_IT_PW_UNSET"


def run_no_server(args, env=None, stdin=None, gen_bench=True):
	full_env = {k: v for k, v in os.environ.items()
			if k not in (ENV_VAR, ENV_UNSET)}
	full_env.update(env or {})
	cmd = ["test_target/asbench", "-h", "127.0.0.1:1", "-z", "1"]
	if gen_bench:
		cmd += ["--gen-bench", "1"]
	proc = subprocess.run(cmd + args, cwd=ROOT, env=full_env,
			input=stdin if stdin is not None else "", stdout=subprocess.PIPE,
			stderr=subprocess.STDOUT, text=True)
	return proc.returncode, proc.stdout


def test_password_env():
	rc, out = run_no_server(["-U", "admin", "-P", "env:" + ENV_VAR],
			env={ENV_VAR: SECRET})
	assert(rc == 0), out
	assert("Enter Password" not in out)
	assert(SECRET not in out)


def test_password_env_unset():
	rc, out = run_no_server(["-U", "admin", "-P", "env:" + ENV_UNSET])
	assert(rc != 0)
	assert(("--password: environment variable %s is not set or empty" %
			ENV_UNSET) in out), out
	assert("Enter Password" not in out)


def test_password_b64_invalid_hides_payload():
	rc, out = run_no_server(["-U", "admin", "--password=b64:" + SECRET])
	assert(rc != 0)
	assert("--password: invalid base64 in b64: value" in out), out
	assert(SECRET not in out)


def test_password_file_missing():
	rc, out = run_no_server(["-U", "admin", "-P", "file:/nonexistent/asbench_pw"])
	assert(rc != 0)
	assert("--password: cannot read file /nonexistent/asbench_pw" in out), out


def test_prompted_password_is_literal():
	rc, out = run_no_server(["-U", "admin", "-P"], stdin="env:%s\n" % ENV_UNSET)
	assert(rc == 0), out
	assert("Enter Password" in out)


def test_tls_keyfile_password_env_unset():
	rc, out = run_no_server(["--tls-enable", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password", "env:" + ENV_UNSET])
	assert(rc != 0)
	assert(("--tls-keyfile-password: environment variable %s is not set or "
			"empty" % ENV_UNSET) in out), out


def test_settings_dump_hides_password():
	rc, out = run_no_server(["-U", "admin", "-P", "env:" + ENV_VAR, "-k", "1",
		"-t", "1", "-d"], env={ENV_VAR: SECRET}, gen_bench=False)
	assert("user:                   admin" in out), out
	assert(SECRET not in out)
