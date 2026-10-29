import base64
import json
import os
import shutil
import socket
import ssl
import struct
import subprocess
import tempfile
import time
import types
import uuid

import pytest

import lib

SECRET = "s3cr3t-pw"
PARSED_LOOKING_SECRET = "env:ASBENCH_IT_NEVER_SET"
PROMPTED = "secrets:asbench:pw"
PIPED = "secrets:asbench:kp"
CONNECT_FAILED = "ERROR Failed to connect\n"

AGENT_IMAGE = "aerospike/aerospike-secret-agent"
AGENT_TAG = "1.1.0"
AGENT_PORT = "3005/tcp"
AGENT_DIR = "/opt/work"
AGENT_MAGIC = 0x51DEC1CC

SECRETS = {
	"pw": SECRET,
	"wrong": "wrong",
	"env_looking": PARSED_LOOKING_SECRET,
	"trailing_newline": SECRET + "\n",
	"with_nul": "ab\0xyzzy",
	"empty": "",
	"max_len": "m" * 63,
	"too_long": "L" * 64,
}


def b64(value):
	return base64.b64encode(value.encode()).decode()


def write(path, content, mode=0o644):
	with open(path, "w") as f:
		f.write(content)
	os.chmod(path, mode)


def make_key(path, password):
	subprocess.run(["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt",
		"ec_paramgen_curve:P-256", "-aes-256-cbc", "-pass", "pass:" + password,
		"-out", path], check=True, capture_output=True)
	os.chmod(path, 0o644)


def openssl(*args):
	subprocess.run(["openssl"] + list(args), check=True, capture_output=True)


def make_ca(dir, name):
	key = os.path.join(dir, name + "-key.pem")
	cert = os.path.join(dir, name + ".pem")
	openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", key,
		"-out", cert, "-days", "2", "-subj", "/CN=asbench test " + name)
	return cert, key


def make_cert(dir, name, ca, san):
	"""
	An agent certificate issued by ca whose only subject alternative name is
	san. The subject CN is not a host name, so it never matches either.
	"""
	key = os.path.join(dir, name + "-key.pem")
	csr = os.path.join(dir, name + ".csr")
	cert = os.path.join(dir, name + ".pem")
	ext = os.path.join(dir, name + ".ext")
	write(ext, "basicConstraints=CA:FALSE\n"
			"keyUsage=digitalSignature,keyEncipherment\n"
			"extendedKeyUsage=serverAuth\n"
			"subjectKeyIdentifier=hash\n"
			"authorityKeyIdentifier=keyid,issuer\n"
			"subjectAltName=" + san + "\n")
	openssl("req", "-newkey", "rsa:2048", "-nodes", "-keyout", key, "-out", csr,
		"-subj", "/CN=asbench test agent")
	openssl("x509", "-req", "-in", csr, "-CA", ca[0], "-CAkey", ca[1],
		"-CAcreateserial", "-out", cert, "-days", "2", "-extfile", ext)
	os.chmod(key, 0o644)
	return cert, key


def assert_no_secret(out):
	for value in [SECRET, b64(SECRET), b64(SECRET)[:8], b64("wrong")]:
		assert(value not in out), out


@pytest.fixture(scope="session")
def files():
	dir = tempfile.mkdtemp(prefix="asbench-sa-")
	os.chmod(dir, 0o755)
	key = os.path.join(dir, "key.pem")
	make_key(key, SECRET)
	parsed_looking_key = os.path.join(dir, "key-parsed-looking.pem")
	make_key(parsed_looking_key, PARSED_LOOKING_SECRET)
	prompted_key = os.path.join(dir, "key-prompted.pem")
	make_key(prompted_key, PROMPTED)
	piped_key = os.path.join(dir, "key-piped.pem")
	make_key(piped_key, PIPED)

	yield types.SimpleNamespace(dir=dir, key=key,
			parsed_looking_key=parsed_looking_key, prompted_key=prompted_key,
			piped_key=piped_key, missing=os.path.join(dir, "missing.pem"))

	shutil.rmtree(dir, ignore_errors=True)


def wait_for_agent(container, port, tls, timeout=60):
	# a bare TCP connect is not enough: Docker's port proxy accepts before the
	# agent listens. Readiness only, so the certificate is not checked here.
	body = json.dumps({"Resource": "asbench", "SecretKey": "probe"}).encode()
	request = struct.pack(">II", AGENT_MAGIC, len(body)) + body
	deadline = time.time() + timeout

	while time.time() < deadline:
		try:
			with socket.create_connection(("127.0.0.1", port), timeout=2) as raw:
				conn = raw
				if tls:
					ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
					ctx.check_hostname = False
					ctx.verify_mode = ssl.CERT_NONE
					conn = ctx.wrap_socket(raw)
				conn.sendall(request)
				header = conn.recv(8)
				if len(header) == 8 and \
						struct.unpack(">I", header[:4])[0] == AGENT_MAGIC:
					return
		except (OSError, ssl.SSLError):
			pass
		time.sleep(0.2)

	raise RuntimeError("secret agent not ready on port %d:\n%s" % (port,
		container.logs().decode(errors="replace")))


def start_agent(containers, dir, name, tls):
	tls_conf = ""
	if tls:
		tls_conf = '    tls:\n      cert-file: "%s/%s"\n      key-file: "%s/%s"\n' % (
				AGENT_DIR, os.path.basename(tls[0]), AGENT_DIR,
				os.path.basename(tls[1]))

	config = "%s.yaml" % name
	write(os.path.join(dir, config),
			"service:\n"
			"  tcp:\n"
			"    endpoint: 0.0.0.0:3005\n"
			+ tls_conf +
			"secret-manager:\n"
			"  file:\n"
			"    resources:\n"
			'      asbench: "%s/secrets.json"\n'
			'      other: "%s/other.json"\n'
			"log:\n"
			"  level: debug\n" % (AGENT_DIR, AGENT_DIR))

	container = lib.DOCKER_CLIENT.containers.run(
			"%s:%s" % (AGENT_IMAGE, AGENT_TAG),
			command="--config-file %s/%s" % (AGENT_DIR, config),
			name="asbench-test-secret-agent-%s-%s" % (name, uuid.uuid4().hex[:12]),
			ports={AGENT_PORT: ("127.0.0.1", None)},
			volumes={dir: {"bind": AGENT_DIR, "mode": "ro"}},
			labels={lib.CONTAINER_LABEL: "asbench-secret-agent",
				lib.CONTAINER_OWNER_LABEL: str(os.getpid())},
			platform="linux/amd64", detach=True)
	containers.append(container)

	deadline = time.time() + 30
	while True:
		container.reload()
		bindings = (container.attrs["NetworkSettings"]["Ports"] or {}).get(
				AGENT_PORT)
		if bindings:
			port = int(bindings[0]["HostPort"])
			break
		if time.time() > deadline:
			raise RuntimeError("secret agent container %s has no port binding" %
					container.name)
		time.sleep(0.2)

	wait_for_agent(container, port, tls is not None)
	return str(port)


@pytest.fixture(scope="session")
def agent(files):
	"""
	Runs aerospike-secret-agent in Docker with the file backend, once in plain
	TCP and once with TLS. No server needed.
	"""
	containers = []

	try:
		write(os.path.join(files.dir, "secrets.json"),
				json.dumps({k: b64(v) for k, v in SECRETS.items()}))
		write(os.path.join(files.dir, "other.json"),
				json.dumps({"pw": b64("other")}))
		ca = make_ca(files.dir, "ca")
		other_ca, _ = make_ca(files.dir, "other-ca")
		dns_cert = make_cert(files.dir, "agent-dns", ca, "DNS:localhost")
		ip_cert = make_cert(files.dir, "agent-ip", ca, "IP:127.0.0.1")

		try:
			lib.DOCKER_CLIENT.images.get("%s:%s" % (AGENT_IMAGE, AGENT_TAG))
		except Exception:
			lib.DOCKER_CLIENT.images.pull(AGENT_IMAGE, tag=AGENT_TAG,
					platform="linux/amd64")

		port = start_agent(containers, files.dir, "tcp", None)
		tls_port = start_agent(containers, files.dir, "tls-dns", dns_cert)
		tls_ip_port = start_agent(containers, files.dir, "tls-ip", ip_cert)

		yield types.SimpleNamespace(port=port, tls_port=tls_port,
				tls_ip_port=tls_ip_port, ca=ca[0], other_ca=other_ca,
				dns_cert=dns_cert[0])
	finally:
		for container in containers:
			try:
				container.remove(force=True)
			except Exception as e:
				print("Failed to remove secret agent container:", e)
