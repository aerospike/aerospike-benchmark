import os
import shutil
import socket
import subprocess
import tempfile
import time
import types
import uuid

import aerospike
import docker
import pytest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
SERVER_IMAGE = "aerospike/aerospike-server-enterprise"
SERVER_TAG = "8.2.0.0"
SERVER_READY_TIMEOUT = 60
CLIENT_IMAGE = "asbench-e2e-fips-client"
OWNER_LABEL = "asbench-fips-owner"
FIPS_ENV = {"OPENSSL_FORCE_FIPS_MODE": "1"}
NAMESPACE = "test"
TLS_NAME = "fips-server"
TLS_PORT = 4333
PKI_USER = "fips-pki"
PASSWORD_USER = "fips-user"
PASSWORD = "fips-pw-5Rt8"
KEYS = 100
NON_FIPS_CIPHER = "ECDHE-RSA-CHACHA20-POLY1305"

SERVER_CONF = """security {{
}}

service {{
	cluster-name asbench-fips
	feature-key-file env-b64:FEATURES
	run-as-daemon false
	proto-fd-max 1024
}}

logging {{
	console {{
		context any info
	}}
}}

network {{
	tls {tls_name} {{
		cert-file /opt/work/server.pem
		key-file /opt/work/server-key.pem
		ca-file /opt/work/ca.pem
	}}

	service {{
		address any
		port {port}
		access-address 127.0.0.1
		tls-port {tls_port}
		tls-name {tls_name}
		tls-authenticate-client any
	}}

	heartbeat {{
		mode mesh
		address any
		port 3002
		interval 100
		timeout 3
		connect-timeout-ms 100
	}}

	fabric {{
		port 3001
		address any
	}}
}}

namespace {namespace} {{
	replication-factor 1
	default-ttl 0
	storage-engine memory {{
		data-size 512M
	}}
}}
"""


def feature_key():
	key = "".join(os.environ.get("FEATKEY", "").split())

	if not key:
		if os.environ.get("CI"):
			pytest.fail("FEATKEY must be set in CI: export FEATKEY=$(base64 < "
					"features.conf)")
		pytest.skip("FEATKEY is not set: export FEATKEY=$(base64 < "
				"features.conf) to run the FIPS tests")

	return key


def closed_port():
	with socket.socket() as s:
		s.bind(("127.0.0.1", 0))
		return s.getsockname()[1]


def write(path, content):
	with open(path, "w") as f:
		f.write(content)
	os.chmod(path, 0o644)


def openssl(*args):
	subprocess.run(["openssl"] + list(args), check=True, capture_output=True)


def make_cert(dir, file, name, ca, usage):
	key = os.path.join(dir, file + "-key.pem")
	csr = os.path.join(dir, file + ".csr")
	cert = os.path.join(dir, file + ".pem")
	ext = os.path.join(dir, file + ".ext")
	write(ext, "basicConstraints=CA:FALSE\n"
			"extendedKeyUsage=" + usage + "\n"
			"subjectAltName=DNS:" + name + "\n")
	openssl("req", "-newkey", "rsa:2048", "-nodes", "-keyout", key, "-out", csr,
		"-subj", "/CN=" + name)
	openssl("x509", "-req", "-in", csr, "-CA", ca[0], "-CAkey", ca[1],
		"-CAcreateserial", "-out", cert, "-days", "2", "-extfile", ext)
	os.chmod(key, 0o644)


def make_certs(dir):
	ca = (os.path.join(dir, "ca.pem"), os.path.join(dir, "ca-key.pem"))
	openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", ca[1],
		"-out", ca[0], "-days", "2", "-subj", "/CN=asbench fips test ca")
	make_cert(dir, "server", TLS_NAME, ca, "serverAuth")
	make_cert(dir, PKI_USER, PKI_USER, ca, "clientAuth")


def pid_alive(pid):
	try:
		os.kill(pid, 0)
	except ProcessLookupError:
		return False
	except PermissionError:
		pass
	return True


def remove_leftovers(client):
	for container in client.containers.list(all=True,
			filters={"label": OWNER_LABEL}):
		owner = int(container.labels[OWNER_LABEL])
		if owner == os.getpid() or not pid_alive(owner):
			container.remove(force=True)
	for network in client.networks.list(filters={"label": OWNER_LABEL}):
		owner = int(network.attrs["Labels"][OWNER_LABEL])
		if owner == os.getpid() or not pid_alive(owner):
			network.remove()


def cluster_stable(client):
	keys = set()
	for err, resp in client.info_all("cluster-stable:size=1").values():
		value = (resp or "").split("\t", 1)[-1].strip()
		keys.add(None if err or not value or value.upper().startswith("ERROR")
				else value)
	return len(keys) == 1 and None not in keys


def connect(port, container, user, password):
	config = {"hosts": [("127.0.0.1", port)], "user": user,
		"password": password}
	deadline = time.time() + SERVER_READY_TIMEOUT
	client = None

	while True:
		try:
			client = client or aerospike.client(config).connect()
			if cluster_stable(client):
				return client
		except Exception:
			container.reload()
			if container.status != "running" or time.time() > deadline:
				raise RuntimeError("FIPS test server on port %d is not ready:\n%s"
						% (port, container.logs().decode(errors="replace")))
		time.sleep(0.5)


@pytest.fixture(scope="module")
def cluster():
	"""
	asbench built in a RHEL 9 container against its OpenSSL, and a TLS
	Enterprise node that requires client certificates.
	"""
	key = feature_key()
	docker_client = docker.from_env()
	remove_leftovers(docker_client)
	labels = {OWNER_LABEL: str(os.getpid())}
	work = tempfile.mkdtemp(prefix="asbench-fips-")
	cleanups = []

	try:
		os.chmod(work, 0o755)
		make_certs(work)
		port = closed_port()
		write(os.path.join(work, "aerospike.conf"), SERVER_CONF.format(
			port=port, tls_port=TLS_PORT, tls_name=TLS_NAME, namespace=NAMESPACE))

		network = docker_client.networks.create("asbench-fips-%d" % os.getpid(),
				labels=labels)
		cleanups.append(network.remove)

		try:
			docker_client.images.get("%s:%s" % (SERVER_IMAGE, SERVER_TAG))
		except Exception:
			docker_client.images.pull(SERVER_IMAGE, tag=SERVER_TAG)
		server = docker_client.containers.run(
				"%s:%s" % (SERVER_IMAGE, SERVER_TAG),
				command="/usr/bin/asd --foreground --config-file "
					"/opt/work/aerospike.conf",
				name="asbench-fips-server-%d" % os.getpid(),
				ports={"%d/tcp" % port: ("127.0.0.1", port)},
				volumes={work: {"bind": "/opt/work", "mode": "ro"}},
				environment={"FEATURES": key},
				labels=labels,
				network=network.name,
				detach=True)
		cleanups.append(lambda: server.remove(force=True))

		admin = connect(port, server, "admin", "admin")
		admin.admin_create_user(PKI_USER, "unused-" + PASSWORD, ["read-write"])
		admin.admin_create_user(PASSWORD_USER, PASSWORD, ["read-write"])
		admin.close()
		reader = connect(port, server, PASSWORD_USER, PASSWORD)
		cleanups.append(reader.close)

		server.reload()
		server_ip = server.attrs["NetworkSettings"]["Networks"][network.name][
			"IPAddress"]

		docker_client.images.build(path=os.path.dirname(__file__),
				tag=CLIENT_IMAGE, rm=True)
		client = docker_client.containers.run(
				CLIENT_IMAGE,
				command="sleep infinity",
				name="asbench-fips-client-%d" % os.getpid(),
				labels=labels,
				network=network.name,
				volumes={ROOT: {"bind": "/asbench-src", "mode": "ro"},
					work: {"bind": "/certs", "mode": "ro"}},
				detach=True)
		cleanups.append(lambda: client.remove(force=True))

		def run_in_client(cmd, fips=True):
			result = client.exec_run(["sh", "-c", cmd],
					environment=FIPS_ENV if fips else {})
			return result.exit_code, result.output.decode(errors="replace")

		rc, out = run_in_client("cp -a /asbench-src /build && "
				"{ make -C /build clean; make -C /build VERSION=fips-e2e; } "
				">/tmp/build.log 2>&1 || { tail -40 /tmp/build.log; exit 1; }",
				fips=False)
		assert(rc == 0), "building asbench in %s failed:\n%s" % (CLIENT_IMAGE, out)

		tls = ("-h %s:%s:%d --tls-enable --tls-cafile /certs/ca.pem "
				"--tls-certfile /certs/%s.pem --tls-keyfile /certs/%s-key.pem" %
				(server_ip, TLS_NAME, TLS_PORT, PKI_USER, PKI_USER))

		def asbench(args, fips=True):
			set_name = "fips_" + uuid.uuid4().hex[:8]
			rc, out = run_in_client("/build/target/asbench %s -n %s -s %s -k %d "
					"-w I -z 1 %s </dev/null" % (tls, NAMESPACE, set_name, KEYS,
						args), fips=fips)
			return rc, out, set_name

		def records(set_name):
			return len(reader.query(NAMESPACE, set_name).results())

		yield types.SimpleNamespace(run=run_in_client, asbench=asbench,
				records=records)
	finally:
		for cleanup in reversed(cleanups):
			try:
				cleanup()
			except Exception as e:
				print("FIPS test cleanup failed:", e)
		shutil.rmtree(work, ignore_errors=True)


def test_forced_fips_mode_is_enforced(cluster):
	"""
	Guards the other tests: the container's OpenSSL serves SHA-256 and refuses
	MD5.
	"""
	assert(cluster.run("openssl sha256 /dev/null")[0] == 0)
	assert(cluster.run("openssl md5 /dev/null")[0] != 0)


def test_asbench_loads_the_container_openssl(cluster):
	rc, out = cluster.run("ldd /build/target/asbench", fips=False)

	assert(rc == 0), out
	assert("libssl.so.3 => /lib64/libssl.so.3" in out or
			"libssl.so.3 => /usr/lib64/libssl.so.3" in out), out
	assert("libcrypto.so.3 => /lib64/libcrypto.so.3" in out or
			"libcrypto.so.3 => /usr/lib64/libcrypto.so.3" in out), out


def test_pki_login_writes_over_tls(cluster):
	rc, out, set_name = cluster.asbench("--auth PKI")

	assert(rc == 0), out
	assert(cluster.records(set_name) == KEYS), out


def test_password_login_writes_over_tls(cluster):
	rc, out, set_name = cluster.asbench("-U %s -P %s" % (PASSWORD_USER, PASSWORD))

	assert(rc == 0), out
	assert(cluster.records(set_name) == KEYS), out


def test_tls_refuses_a_cipher_fips_does_not_approve(cluster):
	rc, out, set_name = cluster.asbench("--auth PKI --tls-protocols TLSv1.2 "
			"--tls-cipher-suite " + NON_FIPS_CIPHER)

	assert(rc != 0), out
	assert("no compatible cipher found" in out), out
	assert(cluster.records(set_name) == 0), out


def test_the_same_cipher_writes_without_fips_mode(cluster):
	"""
	Guards the refusal above: the server itself accepts the cipher.
	"""
	rc, out, set_name = cluster.asbench("--auth PKI --tls-protocols TLSv1.2 "
			"--tls-cipher-suite " + NON_FIPS_CIPHER, fips=False)

	assert(rc == 0), out
	assert(cluster.records(set_name) == KEYS), out
