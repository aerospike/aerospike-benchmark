import os
import re
import shutil
import string
import subprocess
import tempfile
import time
import types
import uuid

import aerospike
import pytest

import lib
from conftest import (CONNECT_FAILED, PARSED_LOOKING_SECRET, SECRET,
		assert_no_secret, write)

ROOT = lib.absolute_path("../../..")
ASBENCH = os.path.join(ROOT, "test_target", "asbench")
SERVER_IMAGE = "aerospike/aerospike-server-enterprise"
SERVER_TAG = "8.2.0.0"
SERVER_READY_TIMEOUT = 60
REFERENCE = "secrets:asbench:wrong"

USERS = {
	"sa": SECRET,
	"sa_max_len": "m" * 63,
	"sa_reference": REFERENCE,
	"sa_env_looking": PARSED_LOOKING_SECRET,
}

READ_STATS = re.compile(r"\bread\(tps=\d+ \(hit=(\d+) miss=(\d+)\) "
		r"timeouts=(\d+) errors=(\d+)\)")


def feature_key():
	key = os.environ.get("FEATKEY", "")

	if not key:
		if os.environ.get("CI"):
			pytest.fail("FEATKEY must be set in CI to a base64 encoded "
					"features.conf for the Enterprise server")
		pytest.skip("FEATKEY is not set: export FEATKEY=$(base64 < "
				"features.conf) to run the secured server tests")

	return key


def connect_admin(port, container):
	config = {"hosts": [("127.0.0.1", port)], "user": "admin",
		"password": "admin"}
	deadline = time.time() + SERVER_READY_TIMEOUT

	while True:
		try:
			return aerospike.client(config).connect()
		except Exception:
			lib._check_container_running(container)
			if time.time() > deadline:
				raise
			time.sleep(0.5)


@pytest.fixture(scope="module")
def server():
	"""
	A single Enterprise node with security enabled. The harness client is
	logged in as admin, which may also read, and the users in USERS exist.
	"""
	key = feature_key()
	port = lib._pick_base_port()
	name = "asbench-sa-sec-%d" % port
	work = tempfile.mkdtemp(prefix=name + "-")
	container = None
	client = None

	try:
		os.chmod(work, 0o755)
		with open(lib.absolute_path("aerospike_secured.conf")) as f:
			write(os.path.join(work, "aerospike.conf"),
					string.Template(f.read()).substitute(service_port=port,
						namespace=lib.NAMESPACE))

		lib._remove_stale_containers()

		try:
			lib.DOCKER_CLIENT.images.get("%s:%s" % (SERVER_IMAGE, SERVER_TAG))
		except Exception:
			lib.DOCKER_CLIENT.images.pull(SERVER_IMAGE, tag=SERVER_TAG)

		container = lib.DOCKER_CLIENT.containers.run(
				"%s:%s" % (SERVER_IMAGE, SERVER_TAG),
				command="/usr/bin/asd --foreground --config-file "
					"%s/aerospike.conf" % lib.CONTAINER_DIR,
				name=name,
				ports={"%d/tcp" % port: ("127.0.0.1", port)},
				volumes={work: {"bind": lib.CONTAINER_DIR, "mode": "ro"}},
				environment={"FEATURES": key},
				labels={lib.CONTAINER_LABEL: name,
					lib.CONTAINER_OWNER_LABEL: str(os.getpid())},
				detach=True)

		client = connect_admin(port, container)
		client.admin_grant_roles("admin", ["read"])
		client.close()
		client = None
		client = connect_admin(port, container)

		for user, password in USERS.items():
			client.admin_create_user(user, password, ["read-write"])

		yield types.SimpleNamespace(port=port, client=client)
	finally:
		if client is not None:
			client.close()
		if container is not None:
			try:
				container.remove(force=True)
			except Exception as e:
				print("Failed to remove secured server container:", e)
		shutil.rmtree(work, ignore_errors=True)


def run(server, user, args):
	proc = subprocess.run([ASBENCH, "-h", "127.0.0.1:%d" % server.port, "-n",
		lib.NAMESPACE, "-z", "1", "-k", "100", "-U", user] + args, cwd=ROOT,
		stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding="utf-8",
		errors="replace", timeout=60)
	return proc.returncode, proc.stdout


def login_and_write(server, user, args):
	set_name = "sa_" + uuid.uuid4().hex[:8]
	rc, out = run(server, user, ["-s", set_name, "-w", "I"] + args)

	assert(rc == 0), out
	assert(len(server.client.query(lib.NAMESPACE, set_name).results()) == 100), \
			out
	assert_no_secret(out)
	return set_name


def assert_rejected(server, rc, out):
	assert(rc == 1), out
	assert("Failed to connect to seed 127.0.0.1 %d. AEROSPIKE_INVALID_CREDENTIAL "
			"AEROSPIKE_INVALID_CREDENTIAL from 127.0.0.1:%d\n" %
			(server.port, server.port) in out), out
	assert(CONNECT_FAILED in out), out
	assert("--password:" not in out), out
	assert("secret-agent:" not in out), out
	assert_no_secret(out)
	assert("wrong" not in out), out


@pytest.mark.parametrize("password", [
	["-P", "secrets:asbench:pw"],
	["-Psecrets:asbench:pw"],
	["--password=secrets:asbench:pw"],
], ids=["separate", "attached", "long"])
def test_fetched_password_logs_in_and_writes(server, agent, password):
	login_and_write(server, "sa", password + ["--sa-port", agent.port])


def test_reads_under_the_same_login(server, agent):
	password = ["-P", "secrets:asbench:pw", "--sa-port", agent.port]
	set_name = login_and_write(server, "sa", password)
	rc, out = run(server, "sa", ["-s", set_name, "-w", "RU,100", "-t", "2"] +
			password)
	reads = READ_STATS.findall(out)

	assert(rc == 0), out
	assert(reads), out
	assert(sum(int(r[0]) for r in reads) > 0), out
	assert(all(r[1:] == ("0", "0", "0") for r in reads)), out


def test_wrong_fetched_password_is_rejected(server, agent):
	rc, out = run(server, "sa", ["-w", "I", "-P", "secrets:asbench:wrong",
		"--sa-port", agent.port])

	assert_rejected(server, rc, out)


def test_reference_is_not_sent(server, agent):
	"""
	sa_reference's password is the reference itself, so only sending the
	reference instead of the fetched value would log in.
	"""
	rc, out = run(server, "sa_reference", ["-w", "I", "-P", REFERENCE,
		"--sa-port", agent.port])

	assert_rejected(server, rc, out)


def test_fetched_value_is_not_resolved_again(server, agent, monkeypatch):
	monkeypatch.delenv(PARSED_LOOKING_SECRET[len("env:"):], raising=False)

	login_and_write(server, "sa_env_looking", ["-P",
		"secrets:asbench:env_looking", "--sa-port", agent.port])


@pytest.mark.parametrize("address,ca_opt", [
	("localhost:{tls_port}", "--sa-ca-file"),
	("127.0.0.1:{tls_ip_port}", "--sa-ca-file"),
	("localhost:{tls_port}", "--sa-cafile"),
], ids=["hostname", "ip", "legacy_sa_cafile"])
def test_tls_agent(server, agent, address, ca_opt):
	login_and_write(server, "sa", ["-P", "secrets:asbench:pw", "--sa-address",
		address.format(**vars(agent)), ca_opt, agent.ca])


def test_63_byte_password(server, agent):
	login_and_write(server, "sa_max_len", ["-P", "secrets:asbench:max_len",
		"--sa-port", agent.port])
