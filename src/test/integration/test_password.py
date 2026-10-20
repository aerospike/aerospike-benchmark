import base64
import contextlib
import os
import subprocess
import sys
import threading

import pytest

import lib

ROOT = lib.absolute_path("../../..")
SECRET = "s3cr3t-pw"
ENV_VAR = "ASBENCH_IT_PW"
ENV_UNSET = "ASBENCH_IT_PW_UNSET"


def run_no_server(args, env=None, stdin=None, gen_bench=True, timeout=None):
	full_env = {k: v for k, v in os.environ.items()
			if k not in (ENV_VAR, ENV_UNSET)}
	full_env.update(env or {})
	cmd = ["test_target/asbench", "-h", "127.0.0.1:1", "-z", "1"]
	if gen_bench:
		cmd += ["--gen-bench", "1"]
	proc = subprocess.run(cmd + args, cwd=ROOT, env=full_env,
			input=stdin if stdin is not None else "", stdout=subprocess.PIPE,
			stderr=subprocess.STDOUT, text=True, start_new_session=True,
			timeout=timeout)
	return proc.returncode, proc.stdout


@contextlib.contextmanager
def fifo(path, data, endless=False):
	"""
	Feeds data through a FIFO at path and holds the write end open until the
	block exits.
	"""
	os.mkfifo(path)
	done = threading.Event()

	def write():
		try:
			with open(path, "wb", buffering=0) as f:
				f.write(data)
				while endless and not done.is_set():
					f.write(data)
				done.wait()
		except BrokenPipeError:
			pass

	thread = threading.Thread(target=write, daemon=True)
	thread.start()
	try:
		yield path
	finally:
		done.set()
		thread.join(5)


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


def test_password_file_nul_does_not_prompt(tmp_path):
	path = tmp_path / "pw"
	path.write_bytes(b"\0" + SECRET.encode() + b"\n")
	rc, out = run_no_server(["-U", "admin", "-P", "file:%s" % path])
	assert(rc != 0)
	assert(("--password: file %s contains a NUL byte" % path) in out), out
	assert("Enter Password" not in out), out


def test_endless_password_file_is_rejected(tmp_path):
	with fifo(str(tmp_path / "pw"), b"p" * 4096, endless=True) as path:
		rc, out = run_no_server(["-U", "admin", "-P", "file:%s" % path],
				timeout=10)
	assert(rc != 0)
	assert(("--password: value from file %s is longer than 63 bytes" % path)
			in out), out


def test_password_flag_then_help():
	rc, out = run_no_server(["-P", "--help"], gen_bench=False)
	assert(rc == 0), out
	assert("Usage:" in out), out
	assert("Enter Password" not in out), out


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


def test_prompted_tls_keyfile_password_is_resolved():
	rc, out = run_no_server(["--tls-enable", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password"], stdin="env:%s\n" % ENV_UNSET)
	assert(rc != 0)
	assert("Enter TLS-Keyfile Password" in out), out
	assert("keyfile pw:" not in out), out
	assert(("--tls-keyfile-password: environment variable %s is not set or "
			"empty" % ENV_UNSET) in out), out


def encrypted_key(tmp_path, password):
	path = tmp_path / "key.pem"
	subprocess.run(["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt",
		"ec_paramgen_curve:P-256", "-aes-256-cbc", "-pass", "pass:" + password,
		"-out", str(path)], check=True, stdout=subprocess.PIPE,
		stderr=subprocess.STDOUT)
	return path


def run_tls_key(key, keyfile_password, stdin=None):
	return run_no_server(["--tls-enable", "--tls-keyfile", str(key),
		"--tls-keyfile-password", keyfile_password], stdin=stdin,
		gen_bench=False)


def test_tls_key_fixture_tells_passwords_apart(tmp_path):
	key = encrypted_key(tmp_path, "pw")
	rc, out = run_tls_key(key, "pw")
	assert(rc != 0)
	assert("Failed to connect" in out), out
	assert("private key" not in out.lower()), out
	rc, out = run_tls_key(key, "wrong")
	assert(rc != 0)
	assert("private key" in out.lower()), out


@pytest.mark.parametrize("contents", [b"pw\n\n", b"pw\r", b"pw\nmore",
		b"pw\r\n\r\n"])
def test_tls_keyfile_password_file_first_line(tmp_path, contents):
	key = encrypted_key(tmp_path, "pw")
	path = tmp_path / "pw"
	path.write_bytes(contents)
	rc, out = run_tls_key(key, "file:%s" % path)
	assert(rc != 0)
	assert("Failed to connect" in out), out
	assert("private key" not in out.lower()), out


def test_tls_keyfile_password_file_empty_first_line(tmp_path):
	key = encrypted_key(tmp_path, "pw")
	path = tmp_path / "pw"
	path.write_bytes(b"\npw\n")
	rc, out = run_tls_key(key, "file:%s" % path)
	assert(rc != 0)
	assert(("--tls-keyfile-password: password from first line of file %s is "
			"empty\n" % path) in out), out


def test_tls_keyfile_password_file_stops_at_first_line(tmp_path):
	key = encrypted_key(tmp_path, "pw")
	with fifo(str(tmp_path / "pw"), b"pw\n") as path:
		rc, out = run_no_server(["--tls-enable", "--tls-keyfile", str(key),
			"--tls-keyfile-password", "file:%s" % path], gen_bench=False,
			timeout=10)
	assert(rc != 0)
	assert("Failed to connect" in out), out
	assert("private key" not in out.lower()), out


def test_prompted_tls_keyfile_password_b64(tmp_path):
	key = encrypted_key(tmp_path, "pw")
	rc, out = run_no_server(["--tls-enable", "--tls-keyfile", str(key),
		"--tls-keyfile-password"], stdin="b64:%s\n" %
		base64.b64encode(b"pw").decode(), gen_bench=False)
	assert(rc != 0)
	assert("Enter TLS-Keyfile Password" in out), out
	assert("Failed to connect" in out), out
	assert("private key" not in out.lower()), out


def test_prompted_tls_keyfile_password_file(tmp_path):
	key = encrypted_key(tmp_path, "pw")
	path = tmp_path / "pw"
	path.write_bytes(b"pw\nmore\n")
	rc, out = run_no_server(["--tls-enable", "--tls-keyfile", str(key),
		"--tls-keyfile-password"], stdin="file:%s\n" % path, gen_bench=False)
	assert(rc != 0)
	assert("Enter TLS-Keyfile Password" in out), out
	assert("Failed to connect" in out), out
	assert("private key" not in out.lower()), out


def test_login_fixture_tells_passwords_apart():
	assert(lib.literal_login(SECRET) != lib.literal_login("wrong"))


def test_login_uses_env_password():
	assert(lib.capture_login(["-U", "admin", "-P", "env:" + ENV_VAR],
			env={ENV_VAR: SECRET}) == lib.literal_login(SECRET))


def test_login_uses_env_password_wrong_value():
	login = lib.capture_login(["-U", "admin", "-P", "env:" + ENV_VAR],
			env={ENV_VAR: "wrong"})
	assert(login == lib.literal_login("wrong"))
	assert(login != lib.literal_login(SECRET))


def test_login_uses_b64_password():
	assert(lib.capture_login(["-U", "admin", "--password=b64:czNjcjN0LXB3"]) ==
			lib.literal_login(SECRET))


def test_login_uses_file_password(tmp_path):
	path = tmp_path / "pw"
	path.write_text(SECRET + "\n")
	assert(lib.capture_login(["-U", "admin", "-Pfile:%s" % path]) ==
			lib.literal_login(SECRET))


def test_password_not_read_without_user():
	rc, out = run_no_server(["-P", "env:" + ENV_UNSET])
	assert(rc == 0), out
	assert("--password:" not in out), out
	assert("Enter Password" not in out), out


def test_bare_password_prompts_without_user():
	rc, out = run_no_server(["-P"], stdin="typed\n")
	assert(rc == 0), out
	assert("Enter Password" in out), out


def test_login_uses_63_byte_env_password():
	value = "x" * 63
	assert(lib.capture_login(["-U", "admin", "-P", "env:" + ENV_VAR],
			env={ENV_VAR: value}) == lib.literal_login(value))


def test_64_byte_env_password_fails():
	rc, out = run_no_server(["-U", "admin", "-P", "env:" + ENV_VAR],
			env={ENV_VAR: "y" * 64})
	assert(rc != 0)
	assert(("--password: value from environment variable %s is longer than 63 "
			"bytes\n" % ENV_VAR) in out), out
	assert("y" * 64 not in out)
	assert("Enter Password" not in out)


def test_login_uses_63_byte_file_password(tmp_path):
	value = "x" * 63
	path = tmp_path / "pw"
	path.write_text(value + "\n")
	assert(lib.capture_login(["-U", "admin", "-P", "file:%s" % path]) ==
			lib.literal_login(value))


def test_64_byte_file_password_fails(tmp_path):
	path = tmp_path / "pw"
	path.write_text("y" * 64 + "\n")
	rc, out = run_no_server(["-U", "admin", "-P", "file:%s" % path])
	assert(rc != 0)
	assert(("--password: value from file %s is longer than 63 bytes\n" % path)
			in out), out
	assert("y" * 64 not in out)


def test_settings_dump_hides_password():
	rc, out = run_no_server(["-U", "admin", "-P", "env:" + ENV_VAR, "-k", "1",
		"-t", "1", "-d"], env={ENV_VAR: SECRET}, gen_bench=False)
	assert("user:                   admin" in out), out
	assert(SECRET not in out)


def run_option(label, value, env=None):
	if label == "--password":
		return run_no_server(["-U", "admin", "-P", value], env=env)
	return run_no_server(["--tls-keyfile", "key.pem", label, value], env=env)


@pytest.mark.parametrize("label", ["--password", "--tls-keyfile-password"])
@pytest.mark.parametrize("value,message", [
	("env:", "env: requires a variable name"),
	("env-b64:", "env-b64: requires a variable name"),
	("file:", "file: requires a path"),
])
def test_prefix_without_name(label, value, message):
	rc, out = run_option(label, value)
	assert(rc != 0)
	assert(("%s: %s\n" % (label, message)) in out), out


@pytest.mark.parametrize("label", ["--password", "--tls-keyfile-password"])
@pytest.mark.parametrize("value,env,message", [
	("env-b64:" + ENV_VAR, {ENV_VAR: "Cg=="},
		"password from environment variable %s is empty" % ENV_VAR),
	("b64:Cg==", {}, "password from b64: value is empty"),
	("file:{path}", {}, "password from file {path} is empty"),
])
def test_empty_password(tmp_path, label, value, env, message):
	path = tmp_path / "pw"
	path.write_bytes(b"")
	rc, out = run_option(label, value.format(path=path), env=env)
	assert(rc != 0)
	assert(("%s: %s\n" % (label, message.format(path=path))) in out), out


def test_password_file_only_line_ending_is_empty(tmp_path):
	path = tmp_path / "pw"
	path.write_bytes(b"\r\n")
	rc, out = run_no_server(["-U", "admin", "-P", "file:%s" % path])
	assert(rc != 0)
	assert(("--password: password from file %s is empty\n" % path) in out), out


def test_64_byte_literal_password_fails():
	rc, out = run_no_server(["-U", "admin", "-P", "y" * 64])
	assert(rc != 0)
	assert("--password: password is longer than 63 bytes\n" in out), out
	assert("y" * 64 not in out)
	assert("Enter Password" not in out)


def test_64_byte_typed_password_fails():
	rc, out = run_no_server(["-U", "admin", "-P"], stdin="y" * 64 + "\n")
	assert(rc != 0)
	assert("Enter Password" in out), out
	assert("--password: password is longer than 63 bytes\n" in out), out
	assert("y" * 64 not in out)


def test_64_byte_typed_password_without_flag_fails():
	rc, out = run_no_server(["-U", "admin"], stdin="y" * 100 + "\n")
	assert(rc != 0)
	assert("Enter Password" in out), out
	assert("--password: password is longer than 63 bytes\n" in out), out


def test_64_byte_b64_password_fails():
	value = base64.b64encode(b"y" * 64).decode()
	rc, out = run_no_server(["-U", "admin", "-P", "b64:" + value])
	assert(rc != 0)
	assert("--password: decoded b64: value is longer than 63 bytes\n" in out), out


def test_login_uses_63_byte_typed_password():
	value = "x" * 63
	assert(lib.capture_login(["-U", "admin", "-P"], stdin=value + "\n") ==
			lib.literal_login(value))


def test_login_uses_typed_password_unparsed():
	typed = "env:" + ENV_VAR
	login = lib.capture_login(["-U", "admin", "-P"], stdin=typed + "\n",
			env={ENV_VAR: SECRET})
	assert(login != lib.literal_login(SECRET))
	assert(login == lib.capture_login(["-U", "admin", "-P",
		"b64:" + base64.b64encode(typed.encode()).decode()]))


@pytest.mark.parametrize("value", ["env:" + ENV_UNSET, "env-b64:" + ENV_UNSET,
		"b64:!", "file:/nonexistent/asbench_pw"])
def test_password_without_user_warns(value):
	rc, out = run_no_server(["-P", value], gen_bench=False)
	assert("Warning: --password: ignored because no user is set\n" in out), out
	assert("environment variable" not in out), out
	assert("cannot read file" not in out), out


def test_literal_password_without_user_does_not_warn():
	rc, out = run_no_server(["-P", SECRET], gen_bench=False)
	assert("ignored because no user is set" not in out), out


@pytest.mark.parametrize("auth", ["PKI", "pki"])
@pytest.mark.parametrize("value", ["env:" + ENV_UNSET, "file:", "y" * 64])
def test_pki_password_not_resolved(auth, value):
	rc, out = run_no_server(["-U", "admin", "--auth", auth, "-P", value])
	assert(rc == 0), out
	assert("--password:" not in out), out
	assert("Enter Password" not in out), out


def test_pki_password_without_user_does_not_warn():
	rc, out = run_no_server(["--auth", "PKI", "-P", "env:" + ENV_UNSET],
			gen_bench=False)
	assert("ignored because no user is set" not in out), out


def test_tls_keyfile_password_not_read_without_keyfile():
	rc, out = run_no_server(["--tls-keyfile-password", "env:" + ENV_UNSET])
	assert(rc == 0), out
	assert("--tls-keyfile-password:" not in out), out


def test_tls_keyfile_password_1024_bytes(tmp_path):
	value = "k" * 1024
	key = encrypted_key(tmp_path, value)
	for keyfile_password, env in [(value, {}), ("env:" + ENV_VAR,
			{ENV_VAR: value})]:
		rc, out = run_no_server(["--tls-enable", "--tls-keyfile", str(key),
			"--tls-keyfile-password", keyfile_password], env=env,
			gen_bench=False)
		assert(rc != 0)
		assert("Failed to connect" in out), out
		assert("private key" not in out.lower()), out


@pytest.mark.parametrize("value,env,message", [
	("y" * 1025, {}, "password is longer than 1024 bytes"),
	("env:" + ENV_VAR, {ENV_VAR: "y" * 1025},
		"value from environment variable %s is longer than 1024 bytes" % ENV_VAR),
	("b64:" + base64.b64encode(b"y" * 1025).decode(), {},
		"decoded b64: value is longer than 1024 bytes"),
	("file:{path}", {}, "value from file {path} is longer than 1024 bytes"),
])
def test_tls_keyfile_password_over_1024_bytes_fails(tmp_path, value, env,
		message):
	path = tmp_path / "pw"
	path.write_bytes(b"y" * 1025 + b"\nmore\n")
	rc, out = run_option("--tls-keyfile-password", value.format(path=path),
			env=env)
	assert(rc != 0)
	assert(("--tls-keyfile-password: %s\n" % message.format(path=path)) in out), \
			out
	assert("y" * 1025 not in out)


def test_prompted_tls_keyfile_password_env_over_1024_bytes_fails():
	rc, out = run_no_server(["--tls-enable", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password"], stdin="env:%s\n" % ENV_VAR,
		env={ENV_VAR: "y" * 1025})
	assert(rc != 0)
	assert("Enter TLS-Keyfile Password" in out), out
	assert(("--tls-keyfile-password: value from environment variable %s is "
			"longer than 1024 bytes\n" % ENV_VAR) in out), out


@pytest.mark.skipif(sys.platform == "darwin",
		reason="macOS getpass() keeps only the first 128 typed bytes")
def test_prompted_tls_keyfile_password_over_1024_bytes_fails():
	rc, out = run_no_server(["--tls-enable", "--tls-keyfile", "key.pem",
		"--tls-keyfile-password"], stdin="y" * 1025 + "\n")
	assert(rc != 0)
	assert("Enter TLS-Keyfile Password" in out), out
	assert("--tls-keyfile-password: password is longer than 1024 bytes\n" in
			out), out


HELP_PASSWORD = """\
-P[<password>] --password[=<password>]  # Default: empty
   Password used to authenticate with cluster. Default: none
   It can be one of the following:
     1) Environment variable: 'env:<VAR>', used exactly as set
     2) Base64 encoded environment variable: 'env-b64:<VAR>'
     3) Base64 encoded string: 'b64:<BASE64>'
     4) File: 'file:<PATH>', the whole file less one trailing '\\n' or
        '\\r\\n'. PATH should name a regular file.
     5) String: 'PASSWORD'
   Prefixes are case-sensitive. A base64 value may be line wrapped,
   and one trailing newline is dropped after decoding. A 'b64:' value
   on the command line is as visible as a literal password.
   A password that itself begins with 'env:', 'env-b64:', 'b64:' or
   'file:' must be given as 'b64:' or typed at the prompt.
   The password is read only when a user is set and the auth mode is
   not PKI. It must be at most 63 bytes.
   User will be prompted on command line if -P specified and no
   password is given. The typed password is used as is.
   If the password is given, it can follow -P directly (ie. -Pmypass) or as
   the next argument (ie. -P mypass).

"""

HELP_TLS_KEYFILE_PASSWORD = """\
   --tls-keyfile-password=TLS_KEYFILE_PASSWORD
   Password to load protected tls-keyfile.
   It can be one of the following:
     1) Environment variable: 'env:<VAR>', used exactly as set
     2) Base64 encoded environment variable: 'env-b64:<VAR>'
     3) Base64 encoded string: 'b64:<BASE64>'
     4) File: 'file:<PATH>', the first line of the file
     5) String: 'PASSWORD'
   Prefixes are case-sensitive. A base64 value may be line wrapped,
   and one trailing newline is dropped after decoding.
   A password that itself begins with 'env:', 'env-b64:', 'b64:' or
   'file:' must be given as 'b64:'.
   The password is read only when a key file is set. It must be at
   most 1024 bytes.
   Default: none
   User will be prompted on command line if --tls-keyfile-password
   specified and no password is given. The typed password
   accepts the forms above.

"""


@pytest.mark.parametrize("block", [HELP_PASSWORD, HELP_TLS_KEYFILE_PASSWORD],
		ids=["password", "tls-keyfile-password"])
def test_help_describes_password_sources(block):
	rc, out = run_no_server(["--help"], gen_bench=False)
	assert(rc == 0), out
	assert(block in out), out
