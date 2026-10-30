import base64
import contextlib
import errno
import glob
import json
import os
import pty
import re
import select
import shutil
import socket
import ssl
import struct
import subprocess
import tempfile
import threading
import time
import types
import uuid

import pytest

import lib

ROOT = lib.absolute_path("../../..")
ASBENCH = os.path.join(ROOT, "test_target", "asbench")
SECRET = "s3cr3t-pw"
PARSED_LOOKING_SECRET = "env:ASBENCH_IT_NEVER_SET"
PROMPTED = "secrets:asbench:pw"
PIPED = "secrets:asbench:kp"
UNREACHABLE = ["-z", "1", "-h", "127.0.0.1", "-p", "1"]
CONNECT_FAILED = "ERROR Failed to connect\n"
TLS_KEY_FAILED = re.compile(r"ERROR (Failed to decrypt private key|"
		r"Private key password is invalid|PEM_read_bio_PrivateKey failed)")
DEPRECATED_CAFILE = \
		"Argument --sa-cafile is deprecated. Use --sa-ca-file instead.\n"
NO_USER_WARNING = "Warning: --password: ignored because no user is set\n"
ERROR_REPLY = json.dumps({"Error": "not found"}).encode()

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


def secret_reply(value):
	return json.dumps({"SecretValue": b64(value)}).encode()


def closed_port():
	with socket.socket() as s:
		s.bind(("127.0.0.1", 0))
		return str(s.getsockname()[1])


def run(args, env=None, stdin="", timeout=60):
	full_env = dict(os.environ)
	full_env.update(env or {})
	proc = subprocess.run([ASBENCH] + UNREACHABLE + args, cwd=ROOT,
			env=full_env, input=stdin, stdout=subprocess.PIPE,
			stderr=subprocess.STDOUT, encoding="utf-8", errors="replace",
			timeout=timeout)
	return proc.returncode, proc.stdout


def run_with_prompt(args, typed):
	pid, fd = pty.fork()

	if pid == 0:
		os.chdir(ROOT)
		os.execv(ASBENCH, [ASBENCH] + UNREACHABLE + args)

	out = b""
	sent = False

	while True:
		ready, _, _ = select.select([fd], [], [], 30)
		if not ready:
			break
		try:
			chunk = os.read(fd, 1024)
		except OSError:
			break
		if not chunk:
			break
		out += chunk
		if not sent and b"Password: " in out:
			os.write(fd, typed.encode() + b"\n")
			sent = True

	os.close(fd)
	os.waitpid(pid, 0)
	return out.decode(errors="replace").replace("\r\n", "\n")


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


def assert_password_resolved(rc, out):
	assert(rc != 0)
	assert("--password:" not in out), out
	assert("secret-agent:" not in out), out
	assert(CONNECT_FAILED in out), out
	assert_no_secret(out)


@pytest.fixture
def fake_agent():
	"""
	Starts agents on loopback that answer one request with the given bytes, or
	never answer when given None. length replaces the length in the reply
	header and close ends the connection right after the reply. The request
	body each agent got is kept in start.requests by port.
	"""
	servers = []
	requests = {}

	def start(response, length=None, close=False):
		srv = socket.socket()
		srv.bind(("127.0.0.1", 0))
		srv.listen(1)
		port = str(srv.getsockname()[1])
		done = threading.Event()

		def serve():
			try:
				conn, _ = srv.accept()
			except OSError:
				return
			with conn:
				header = lib._recv_exact(conn, 8)
				if len(header) == 8:
					requests[port] = lib._recv_exact(conn,
							struct.unpack(">II", header)[1])
				if response is not None:
					conn.sendall(struct.pack(">II", AGENT_MAGIC,
						len(response) if length is None else length) + response)
				if not close:
					done.wait(10)

		threading.Thread(target=serve, daemon=True).start()
		servers.append((srv, done))
		return port

	start.requests = requests
	yield start

	for srv, done in servers:
		done.set()
		lib.shutdown_listener(srv)


def connect_pending(addr, port, socks):
	"""
	Starts a connect to addr:port and returns True if it is still pending after
	200 ms. The socket is kept open in socks.
	"""
	s = socket.socket()
	socks.append(s)
	s.setblocking(False)
	if s.connect_ex((addr, port)) not in (errno.EINPROGRESS, errno.EWOULDBLOCK):
		return False
	_, writable, _ = select.select([], [s], [], 0.2)
	return not writable


@pytest.fixture
def blackhole():
	"""
	An address and port where a TCP connect never completes. Linux drops SYNs
	once a listener's accept queue is full. macOS resets instead, but drops
	SYNs to 127.0.0.2, which lo0 does not have.
	"""
	socks = []
	listener = socket.socket()
	socks.append(listener)
	listener.bind(("127.0.0.1", 0))
	listener.listen(1)
	port = listener.getsockname()[1]
	found = None

	for _ in range(64):
		if connect_pending("127.0.0.1", port, socks):
			found = ("127.0.0.1", port)
			break
		if socks[-1].getsockopt(socket.SOL_SOCKET, socket.SO_ERROR) != 0:
			break

	for addr in ["127.0.0.2", "192.0.2.1"]:
		if found is None and connect_pending(addr, 3005, socks):
			found = (addr, 3005)

	try:
		if found is None:
			pytest.skip("no address where a TCP connect stays pending")
		yield found
	finally:
		for s in socks:
			s.close()


@contextlib.contextmanager
def endless_fifo(path):
	"""
	A FIFO at path that yields 64 KiB every 10 ms until the block exits.
	"""
	os.mkfifo(path)
	done = threading.Event()

	def write():
		try:
			with open(path, "wb", buffering=0) as f:
				while not done.is_set():
					f.write(b"A" * 65536)
					time.sleep(0.01)
		except OSError:
			pass

	thread = threading.Thread(target=write, daemon=True)
	thread.start()
	try:
		yield path
	finally:
		done.set()
		try:
			os.close(os.open(path, os.O_RDONLY | os.O_NONBLOCK))
		except OSError:
			pass
		thread.join(5)
		os.unlink(path)


@pytest.fixture(scope="module")
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
					ctx.minimum_version = ssl.TLSVersion.TLSv1_2
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


@pytest.fixture(scope="module")
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


# no agent and no server needed

@pytest.mark.parametrize("opt,args", [
	("--password", ["-U", "admin", "-P", "secrets:asbench:pw"]),
	("--tls-keyfile-password", ["--tls-enable", "--tls-keyfile", "/dev/null",
		"--tls-keyfile-password", "secrets:asbench:pw"]),
])
def test_agent_unreachable(opt, args):
	rc, out = run(args + ["--sa-port", closed_port()])

	assert(rc != 0)
	assert(opt + ": secret agent request for secrets:asbench:pw failed: "
			"connection or protocol error\n" in out), out
	assert("secret-agent: ERR: connect failed, errno: " in out), out
	assert("(Connection refused)" in out), out
	assert(CONNECT_FAILED not in out), out


def test_agent_unreachable_names_errno():
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", "--sa-port",
		closed_port()])
	m = re.search(r"^secret-agent: ERR: connect failed\b.*, errno: (\d+) "
			r"\((.*)\)$", out, re.M)

	assert(rc == 1), out
	assert(m), out
	assert(int(m.group(1)) == errno.ECONNREFUSED), out
	assert(m.group(2) == os.strerror(errno.ECONNREFUSED)), out


def test_password_not_fetched_without_user():
	rc, out = run(["-P", "secrets:asbench:pw", "--sa-port", closed_port()])

	assert(NO_USER_WARNING in out), out
	assert("secret-agent:" not in out), out
	assert("secret agent request" not in out), out
	assert(CONNECT_FAILED in out), out


def test_user_is_not_fetched():
	login = lib.capture_login(["-U", "secrets:asbench:user", "-P", SECRET,
		"--sa-port", closed_port()])
	assert(login == lib.capture_login(["-U", "secrets:asbench:user",
		"--password=b64:" + b64(SECRET), "--sa-port", closed_port()]))
	assert(b"secrets:asbench:user" in login), login


def test_pki_password_not_fetched():
	rc, out = run(["--auth", "PKI", "-U", "admin", "-P", "secrets:asbench:pw",
		"--sa-port", closed_port(), "--gen-bench", "1"])

	assert(rc == 0), out
	assert("secret-agent:" not in out), out
	assert("--password:" not in out), out


@pytest.mark.parametrize("path,resource,key", [
	('secrets:nope","Resource":"asbench:pw', 'nope","Resource":"asbench', "pw"),
	('secrets:asbench:pw"', "asbench", 'pw"'),
	("secrets:asbench:p\\w", "asbench", "p\\w"),
	('secrets:a\\"b:pw', 'a\\"b', "pw"),
], ids=["quote_in_resource", "quote_in_key", "backslash_in_key",
	"escaped_quote_in_resource"])
def test_secret_path_is_escaped_in_request(fake_agent, path, resource, key):
	port = fake_agent(ERROR_REPLY)
	rc, out = run(["-U", "admin", "-P", path, "--sa-port", port])
	body = fake_agent.requests.get(port)

	assert(body is not None), out
	try:
		fields = json.loads(body, object_pairs_hook=lambda pairs: pairs)
	except ValueError as e:
		pytest.fail("request %r is not JSON: %s" % (body, e))
	assert(sorted(fields) == [("Resource", resource), ("SecretKey", key)]), \
			body
	assert(rc == 1), out


def test_truncated_reply_is_a_connection_error(fake_agent):
	reply = secret_reply(SECRET)
	port = fake_agent(reply, length=len(reply) + 50, close=True)
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", "--sa-port", port])

	assert("--password: secret agent request for secrets:asbench:pw failed: "
			"connection or protocol error\n" in out), out
	assert(rc == 1), out
	assert(CONNECT_FAILED not in out), out
	assert_no_secret(out)


def test_fetched_tls_keyfile_password_1024_bytes(fake_agent, files):
	port = fake_agent(secret_reply("k" * 1024))
	rc, out = run(["--tls-enable", "--tls-keyfile", files.key,
		"--tls-keyfile-password", "secrets:asbench:long", "--sa-port", port])

	assert(rc != 0)
	assert("--tls-keyfile-password:" not in out), out
	assert(TLS_KEY_FAILED.search(out)), out
	assert("k" * 1024 not in out), out


def test_fetched_tls_keyfile_password_over_1024_bytes_fails(fake_agent, files):
	port = fake_agent(secret_reply("k" * 1025))
	rc, out = run(["--tls-enable", "--tls-keyfile", files.key,
		"--tls-keyfile-password", "secrets:asbench:long", "--sa-port", port])

	assert(rc == 1), out
	assert("--tls-keyfile-password: value from secrets:asbench:long is longer "
			"than 1024 bytes\n" in out), out
	assert(not TLS_KEY_FAILED.search(out)), out
	assert("k" * 1025 not in out), out
	assert(b64("k" * 1025)[:16] not in out), out


FAILURES = {
	"unreachable": lambda agent, files: ["-U", "admin", "-P",
		"secrets:asbench:pw", "--sa-port", closed_port()],
	"no_answer": lambda agent, files: ["-U", "admin", "-P",
		"secrets:asbench:pw", "--sa-port", agent(None), "--sa-timeout", "300"],
	"malformed_reply": lambda agent, files: ["-U", "admin", "-P",
		"secrets:asbench:pw", "--sa-port",
		agent(('{"SecretValue":"%s' % b64(SECRET)).encode())],
	"agent_error": lambda agent, files: ["-U", "admin", "-P",
		"secrets:asbench:pw", "--sa-port", agent(ERROR_REPLY)],
	"empty_value": lambda agent, files: ["-U", "admin", "-P",
		"secrets:asbench:pw", "--sa-port",
		agent(json.dumps({"SecretValue": ""}).encode())],
	"too_long": lambda agent, files: ["-U", "admin", "-P",
		"secrets:asbench:pw", "--sa-port", agent(secret_reply(SECRET * 8))],
	"nul_byte": lambda agent, files: ["-U", "admin", "-P",
		"secrets:asbench:pw", "--sa-port",
		agent(secret_reply(SECRET + "\0" + SECRET))],
	"missing_cafile": lambda agent, files: ["-U", "admin", "-P",
		"secrets:asbench:pw", "--sa-port", closed_port(), "--sa-cafile",
		files.missing],
	"keyfile_unreachable": lambda agent, files: ["--tls-enable",
		"--tls-keyfile", files.key, "--tls-keyfile-password",
		"secrets:asbench:pw", "--sa-port", closed_port()],
	"keyfile_too_long": lambda agent, files: ["--tls-enable", "--tls-keyfile",
		files.key, "--tls-keyfile-password", "secrets:asbench:pw",
		"--sa-port", agent(secret_reply(SECRET * 120))],
	"keyfile_nul_byte": lambda agent, files: ["--tls-enable", "--tls-keyfile",
		files.key, "--tls-keyfile-password", "secrets:asbench:pw",
		"--sa-port", agent(secret_reply(SECRET + "\0" + SECRET))],
}


@pytest.mark.parametrize("case", sorted(FAILURES))
def test_failure_exits_1_without_secret(fake_agent, files, case):
	rc, out = run(FAILURES[case](fake_agent, files))

	assert(rc == 1), out
	assert(CONNECT_FAILED not in out), out
	assert_no_secret(out)


@pytest.mark.parametrize("sa_args", [
	["--sa-address", "[::1]", "--sa-port", "{closed}"],
	["--sa-address", "[::1]:{closed}"],
	["--sa-address", "::1", "--sa-port", "{closed}"],
	["--sa-address", "localhost:{closed}"],
	["--sa-address=127.0.0.1:{closed}"],
])
def test_address_forms_parse(sa_args):
	closed = closed_port()
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw"] +
			[a.format(closed=closed) for a in sa_args])

	assert(rc != 0)
	assert("invalid value" not in out), out
	assert("--password: secret agent request for secrets:asbench:pw failed: "
			"connection or protocol error\n" in out), out


@pytest.mark.parametrize("opt,value,expected", [
	("--sa-timeout", "abc", "--sa-timeout: invalid value abc, expected an "
		"integer from 1 to 2147483647"),
	("--sa-timeout", "0", "--sa-timeout: invalid value 0, expected an integer "
		"from 1 to 2147483647"),
	("--sa-timeout", "-1", "--sa-timeout: invalid value -1, expected an "
		"integer from 1 to 2147483647"),
	("--sa-timeout", "2147483648", "--sa-timeout: invalid value 2147483648, "
		"expected an integer from 1 to 2147483647"),
	("--sa-timeout", "+5", "--sa-timeout: invalid value +5, expected an "
		"integer from 1 to 2147483647"),
	("--sa-timeout", " 5", "--sa-timeout: invalid value  5, expected an "
		"integer from 1 to 2147483647"),
	("--sa-timeout", "\uff15", "--sa-timeout: invalid value \uff15, expected "
		"an integer from 1 to 2147483647"),
	("--sa-port", "0", "--sa-port: invalid value 0"),
	("--sa-port", "65536", "--sa-port: invalid value 65536"),
	("--sa-port", "http", "--sa-port: invalid value http"),
	("--sa-address", "", "--sa-address: invalid value "),
	("--sa-address", ":3005", "--sa-address: invalid value :3005"),
	("--sa-address", "host:", "--sa-address: invalid value host:"),
	("--sa-address", "host:99999", "--sa-address: invalid value host:99999"),
	("--sa-address", "[::1", "--sa-address: invalid value [::1"),
	("--sa-address", "[]:3005", "--sa-address: invalid value []:3005"),
	("--sa-address", "[::1]x", "--sa-address: invalid value [::1]x"),
	("--sa-address", "host:3005:x", "--sa-address: invalid value host:3005:x"),
	("--sa-address", "10.0.0.1:3005:1",
		"--sa-address: invalid value 10.0.0.1:3005:1"),
	("--sa-address", "1::2::3", "--sa-address: invalid value 1::2::3"),
	("--sa-address", "1::2::3%lo0", "--sa-address: invalid value 1::2::3%lo0"),
	("--sa-address", "fe80::1%", "--sa-address: invalid value fe80::1%"),
	("--sa-address", "[host:3005:x]",
		"--sa-address: invalid value [host:3005:x]"),
	("--sa-address", "[host:3005:x]:3005",
		"--sa-address: invalid value [host:3005:x]:3005"),
	("--sa-address", "[a:b]", "--sa-address: invalid value [a:b]"),
	("--sa-address", "[fe80::1%]:3005",
		"--sa-address: invalid value [fe80::1%]:3005"),
])
def test_bad_option(opt, value, expected):
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", opt, value])

	assert(rc != 0)
	assert(expected + "\n" in out), out
	assert("secret-agent:" not in out), out
	assert(CONNECT_FAILED not in out), out


# a literal password never contacts the agent, so reaching the server proves
# the address parsed
@pytest.mark.parametrize("value", ["fe80::1%lo0", "fe80::1%1",
	"[fe80::1%lo0]", "[fe80::1%lo0]:3005", "[::1]:3005", "::ffff:127.0.0.1"])
def test_ipv6_address_accepted(value):
	rc, out = run(["-U", "admin", "-P", "literal", "--sa-address", value])

	assert("invalid value" not in out), out
	assert(CONNECT_FAILED in out), out


# the library default is 1000 ms, so without the setting both cases fail
@pytest.mark.parametrize("timeout_ms,at_least,under", [
	(300, None, 0.8),
	(2000, 1.9, None),
])
def test_timeout(fake_agent, timeout_ms, at_least, under):
	port = fake_agent(None)

	start = time.monotonic()
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", "--sa-port", port,
		"--sa-timeout", str(timeout_ms)])
	elapsed = time.monotonic() - start

	assert(rc != 0)
	assert("secret-agent: ERR: socket poll timed out\n" in out), out
	assert("--password: secret agent request for secrets:asbench:pw failed: "
			"timed out\n" in out), out
	assert(CONNECT_FAILED not in out), out
	if at_least is not None:
		assert(elapsed >= at_least), elapsed
	if under is not None:
		assert(elapsed < under), elapsed


def test_connect_timeout(blackhole):
	addr, port = blackhole

	start = time.monotonic()
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", "--sa-address",
		addr, "--sa-port", str(port), "--sa-timeout", "300"])
	elapsed = time.monotonic() - start

	assert(rc != 0)
	assert("secret-agent: ERR: connect timed out\n" in out), out
	assert("--password: secret agent request for secrets:asbench:pw failed: "
			"timed out\n" in out), out
	assert(CONNECT_FAILED not in out), out
	assert(0.29 <= elapsed < 0.8), elapsed


def test_malformed_response_is_not_echoed(fake_agent):
	port = fake_agent(('{"SecretValue":"%s' % b64(SECRET)).encode())
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", "--sa-port", port])

	assert(rc != 0)
	assert("secret-agent: ERR: failed to parse response JSON line\n" in out), out
	assert("--password: secret agent request for secrets:asbench:pw failed: "
			"bad request\n" in out), out
	assert_no_secret(out)


def test_unreadable_cafile(files):
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", "--sa-cafile",
		files.missing, "--sa-port", closed_port()])

	assert(rc != 0)
	assert("--sa-cafile: cannot read file %s: No such file or directory\n" %
			files.missing in out), out
	assert("secret-agent:" not in out), out
	assert(CONNECT_FAILED not in out), out


def test_cafile_only_read_when_secrets_used(files):
	rc, out = run(["-U", "admin", "-P", "literal", "--sa-cafile", files.missing])

	assert(rc != 0)
	assert(files.missing not in out), out
	assert(CONNECT_FAILED in out), out


@pytest.mark.parametrize("opt", ["--sa-ca-file", "--sa-cafile"])
@pytest.mark.parametrize("content", ["", "not a certificate\n"],
		ids=["empty", "not_pem"])
def test_unusable_cafile_names_option(files, opt, content):
	path = os.path.join(files.dir, "unusable-%d.pem" % len(content))
	write(path, content)
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", opt, path,
		"--sa-port", closed_port()])

	assert(rc == 1), out
	assert("%s: cannot load TLS CA file %s: " % (opt, path) in out), out
	assert("secret-agent: ERR: connect" not in out), out


@pytest.mark.parametrize("opt", ["--sa-ca-file", "--sa-cafile"])
def test_empty_ca_path_names_option(fake_agent, opt):
	port = fake_agent(secret_reply(SECRET))
	rc, out = run(["-U", "u", "-P", "secrets:a:b", "--sa-port", port, opt, ""])

	assert(rc == 1), out
	assert(re.search(r"^%s: " % re.escape(opt), out, re.M)), out
	assert(port not in fake_agent.requests), out
	assert_no_secret(out)


def test_endless_cafile_is_rejected(files):
	path = os.path.join(files.dir, "endless-ca.pem")

	with endless_fifo(path):
		try:
			rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw",
				"--sa-cafile", path, "--sa-port", closed_port()], timeout=15)
		except subprocess.TimeoutExpired:
			pytest.fail("--sa-cafile %s still being read after 15 s" % path)

	assert(rc == 1), out
	assert(re.search(r"^--sa-cafile: .*%s" % re.escape(path), out, re.M)), out


@pytest.mark.parametrize("args", [
	["--sa-ca-file", "{ca}"],
	["--sa-ca-file={ca}"],
], ids=["separate", "attached"])
def test_sa_ca_file_parses(args):
	rc, out = run(["-U", "admin", "-P", "literal"] +
			[a.format(ca="/nonexistent/ca.pem") for a in args])

	assert("unrecognized option" not in out), out
	assert("--sa-ca-file" not in out), out
	assert(CONNECT_FAILED in out), out


@pytest.mark.parametrize("args", [
	["--sa-cafile", "{other}", "--sa-ca-file", "{missing}"],
	["--sa-ca-file", "{missing}", "--sa-cafile", "{other}"],
], ids=["sa_ca_file_last", "sa_ca_file_first"])
def test_sa_ca_file_beats_sa_cafile(files, args):
	other = os.path.join(files.dir, "other-missing.pem")
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", "--sa-port",
		closed_port()] + [a.format(other=other, missing=files.missing)
			for a in args])

	assert(rc == 1), out
	assert("--sa-ca-file: " in out and files.missing in out), out
	assert(other not in out), out


def test_prompted_password_is_literal():
	rc, out = run(["-U", "admin", "-P", "--sa-port", closed_port()],
			stdin=PROMPTED + "\n")

	assert("Enter Password" not in out), out
	assert("secret-agent:" not in out), out
	assert("--password:" not in out), out
	assert(CONNECT_FAILED in out), out


def test_prompted_password_reaches_login_literally():
	typed = lib.capture_login(["-U", "admin", "-P", "--sa-port", closed_port()],
			stdin=PROMPTED + "\n")
	assert(typed == lib.capture_login(["-U", "admin", "-P",
			"b64:" + b64(PROMPTED)]))
	assert(typed != lib.literal_login(SECRET))


def test_prompted_tls_keyfile_password_is_literal(files):
	out = run_with_prompt(["--tls-enable", "--tls-keyfile", files.prompted_key,
		"--tls-keyfile-password", "--sa-port", closed_port()], PROMPTED)

	assert("Enter TLS-Keyfile Password: " in out), out
	assert("secret-agent:" not in out), out
	assert("--tls-keyfile-password:" not in out), out
	assert(not TLS_KEY_FAILED.search(out)), out
	assert(CONNECT_FAILED in out), out


def test_piped_tls_keyfile_password_is_literal(fake_agent, files):
	port = fake_agent(secret_reply(SECRET))
	rc, out = run(["--tls-enable", "--tls-keyfile", files.piped_key,
		"--tls-keyfile-password", "--sa-port", port], stdin=PIPED + "\n")

	assert(port not in fake_agent.requests), out
	assert("Enter TLS-Keyfile Password" not in out), out
	assert("secret-agent:" not in out), out
	assert("--tls-keyfile-password:" not in out), out
	assert(not TLS_KEY_FAILED.search(out)), out
	assert(CONNECT_FAILED in out), out


HELP_SECRET_AGENT = """\
   --sa-address <host>[:<port>]  # Default: 127.0.0.1:3005
   "<host>[:<port>]" or "[<ipv6>][:<port>]".
   Secret Agent hostname or IP address.

   --sa-port <port>     # Default: 3005
   Secret Agent port, 1 to 65535. Overrides a port in --sa-address.

   --sa-timeout <ms>    # Default: 1000
   Timeout in milliseconds for Secret Agent requests, an integer from 1 to
   2147483647.

   --sa-ca-file <path>  # Default: none
   Path to a CA certificate file. Enables TLS to the Secret Agent and verifies
   its certificate against this CA. The agent's hostname or IP address must be
   in the certificate.

"""


def test_help():
	rc, out = run(["--help"])

	assert(rc == 0)
	assert(HELP_SECRET_AGENT in out), out
	assert(out.count("5) Aerospike Secret Agent: "
			"'secrets:<resource>:<key>'\n") == 2), out
	assert("not verified" not in out)
	assert("--sa-cafile" not in out), out


# the log filter

LIB_DIR = os.path.join(ROOT, "modules", "secret-agent-client", "src")
COMMON_C = os.path.join(ROOT, "src", "main", "common.c")
C_STRING = r'"(?:[^"\\]|\\.)*"'

# library log lines asbench prints with their arguments, and the argument
# expressions reviewed as safe
REVIEWED_LOG_ARGS = {
	"ERR: failed to lookup address: %s": "addr",
	"ERR: connect failed, errno: %d": "conn_err",
	"ERR: response: %.*s": "(int)payload_len, payload_str",
	"ERR: SSL_connect failed: %s": "errbuf",
	"ERR: SSL_connect I/O error: %s": "errbuf",
	"ERR: SSL_connect certificate verify failed: %s (%ld)":
		"X509_verify_cert_error_string(rv), rv",
	"ERR: unable to set TLS peer name: %s": "host",
}


def c_unescape(literal):
	return literal[1:-1].encode().decode("unicode_escape")


def strip_c(src, where):
	"""
	Blanks out comments and replaces character literals with 0, keeping string
	literals and line breaks, so only live code is scanned.
	"""
	out = []
	i, n = 0, len(src)

	while i < n:
		if src.startswith("//", i):
			end = src.find("\n", i)
			i = n if end < 0 else end
		elif src.startswith("/*", i):
			end = src.find("*/", i + 2)
			if end < 0:
				raise AssertionError("%s: unterminated comment" % where)
			out.append("\n" * src.count("\n", i, end))
			i = end + 2
		elif src[i] in "\"'":
			quote, j = src[i], i + 1
			while j < n and src[j] != quote:
				if src[j] == "\n":
					raise AssertionError("%s:%d: unterminated %s literal" %
							(where, src.count("\n", 0, i) + 1,
							"string" if quote == '"' else "character"))
				j += 2 if src[j] == "\\" else 1
			if j >= n:
				raise AssertionError("%s: unterminated literal at the end" % where)
			out.append(src[i:j + 1] if quote == '"' else "0")
			i = j + 1
		else:
			out.append(src[i])
			i += 1

	return "".join(out)


def call_text(src, start, where):
	"""
	Text between the parentheses of a call whose '(' ends just before start,
	skipping string literals.
	"""
	depth, i, in_string = 1, start, False
	while depth:
		if i >= len(src):
			raise AssertionError("%s: call is not closed" % where)
		c = src[i]
		if in_string:
			if c == "\\":
				i += 1
			elif c == '"':
				in_string = False
		elif c == '"':
			in_string = True
		elif c == "(":
			depth += 1
		elif c == ")":
			depth -= 1
		i += 1
	return src[start:i - 1]


def library_log_calls():
	paths = sorted(glob.glob(os.path.join(LIB_DIR, "main", "*.c")) +
			glob.glob(os.path.join(LIB_DIR, "include", "*.h")))
	assert(paths), "no secret agent client sources under %s" % LIB_DIR
	calls = []

	for path in paths:
		name = os.path.relpath(path, LIB_DIR)
		with open(path) as f:
			src = strip_c(f.read(), name)
		for m in re.finditer(r"\bsa_g_log_function\s*\(", src):
			where = "%s:%d" % (name, src.count("\n", 0, m.start()) + 1)
			text = call_text(src, m.end(), where)
			literal = re.match(r"\s*((?:%s\s*)+)(.*)$" % C_STRING, text, re.S)
			if literal is None:
				calls.append((where, None, " ".join(text.split())))
				continue
			fmt = "".join(c_unescape(s) for s in
					re.findall(C_STRING, literal.group(1)))
			calls.append((where, fmt,
					" ".join(literal.group(2).lstrip(",").split())))

	return calls


def asbench_log_allow_list():
	with open(COMMON_C) as f:
		src = strip_c(f.read(), "common.c")
	m = re.search(r"SA_LOG_ALLOW\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
	assert(m), "SA_LOG_ALLOW not found in %s" % COMMON_C
	allowed = [c_unescape(s) for s in
			re.findall(r"\{\s*(" + C_STRING + r")\s*,", m.group(1))]
	assert(allowed), "SA_LOG_ALLOW in %s has no entries" % COMMON_C
	return allowed


def test_log_filter_strip_c():
	src = ('a(); // b("x");\n/* c("y");\n */ d(\'(\', "/* e */", \'"\');\n'
			'f("g\\"h");')
	assert(strip_c(src, "t") ==
			'a(); \n\n d(0, "/* e */", 0);\nf("g\\"h");')


def test_log_filter_library_sources_found():
	paths = glob.glob(os.path.join(LIB_DIR, "main", "*.c"))
	assert(paths), "no library .c files under %s" % LIB_DIR
	assert(glob.glob(os.path.join(LIB_DIR, "include", "*.h"))), \
			"no library .h files under %s" % LIB_DIR
	assert(len(library_log_calls()) > 20)


def test_log_filter_allow_list_is_reviewed():
	assert(sorted(asbench_log_allow_list()) == sorted(REVIEWED_LOG_ARGS))


def test_log_filter_every_library_line_is_allowed_or_truncated():
	allowed = set(asbench_log_allow_list())
	seen = set()
	problems = []

	for where, fmt, args in library_log_calls():
		if fmt is None:
			problems.append("%s: format is not a string literal: %s" %
					(where, args))
		elif fmt in allowed:
			seen.add(fmt)
			if args != REVIEWED_LOG_ARGS.get(fmt):
				problems.append("%s: allowed line %r now passes %r, review it" %
						(where, fmt, args))
		elif "%" in fmt and not fmt[:fmt.index("%")].rstrip(" :,-("):
			problems.append("%s: %r has no fixed text to print" % (where, fmt))

	problems += ["allowed line %r is not in the library" % fmt
			for fmt in sorted(allowed - seen)]
	assert(problems == [])


# against a real agent

def test_login_uses_secret(agent):
	assert(lib.capture_login(["-U", "admin", "-P", "secrets:asbench:pw",
		"--sa-port", agent.port]) == lib.literal_login(SECRET))


def test_login_uses_wrong_secret(agent):
	login = lib.capture_login(["-U", "admin", "--sa-address",
		"127.0.0.1:" + agent.port, "--password=secrets:asbench:wrong"])
	assert(login == lib.literal_login("wrong"))
	assert(login != lib.literal_login(SECRET))


def test_login_uses_secret_over_tls(agent):
	assert(lib.capture_login(["-U", "admin", "-Psecrets:asbench:pw",
		"--sa-address", "localhost:" + agent.tls_port, "--sa-cafile",
		agent.ca]) == lib.literal_login(SECRET))


def test_login_uses_secret_over_tls_by_ip(agent):
	assert(lib.capture_login(["-U", "admin", "-Psecrets:asbench:pw",
		"--sa-address", "127.0.0.1:" + agent.tls_ip_port, "--sa-cafile",
		agent.ca]) == lib.literal_login(SECRET))


def test_login_uses_secret_over_tls_with_sa_ca_file(agent):
	assert(lib.capture_login(["-U", "admin", "-Psecrets:asbench:pw",
		"--sa-address", "localhost:" + agent.tls_port, "--sa-ca-file",
		agent.ca]) == lib.literal_login(SECRET))


def test_sa_cafile_is_deprecated(agent):
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", "--sa-address",
		"localhost:" + agent.tls_port, "--sa-cafile", agent.ca])

	assert(DEPRECATED_CAFILE in out), out
	assert_password_resolved(rc, out)


@pytest.mark.parametrize("args,verified", [
	(["--sa-cafile", "{missing}", "--sa-ca-file", "{ca}"], True),
	(["--sa-ca-file", "{ca}", "--sa-cafile", "{missing}"], True),
	(["--sa-cafile", "{ca}", "--sa-ca-file", "{other_ca}"], False),
	(["--sa-ca-file", "{other_ca}", "--sa-cafile", "{ca}"], False),
], ids=["ca_last", "ca_first", "other_ca_last", "other_ca_first"])
def test_sa_ca_file_wins_over_tls(agent, files, args, verified):
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", "--sa-address",
		"localhost:" + agent.tls_port] + [a.format(missing=files.missing,
			ca=agent.ca, other_ca=agent.other_ca) for a in args])

	assert("unrecognized option" not in out), out
	if verified:
		assert_password_resolved(rc, out)
	else:
		assert(rc == 1), out
		assert_verify_failed(out, "unable to get local issuer certificate")


def test_quote_in_resource_does_not_fetch_another_secret(agent):
	path = 'secrets:nope","Resource":"asbench:pw'
	rc, out = run(["-U", "admin", "-P", path, "--sa-port", agent.port])

	assert("--password: secret agent request for %s failed: bad request\n" %
			path in out), out
	assert(rc == 1), out
	assert(CONNECT_FAILED not in out), out
	assert_no_secret(out)


def test_quote_in_key_fails_at_once(agent):
	path = 'secrets:asbench:pw"'
	start = time.monotonic()
	rc, out = run(["-U", "admin", "-P", path, "--sa-port", agent.port,
		"--sa-timeout", "5000"])
	elapsed = time.monotonic() - start

	assert("--password: secret agent request for %s failed: bad request\n" %
			path in out), out
	assert(rc == 1), out
	assert(elapsed < 2.5), elapsed


def test_login_uses_63_byte_secret(agent):
	assert(lib.capture_login(["-U", "admin", "-P", "secrets:asbench:max_len",
		"--sa-port", agent.port]) == lib.literal_login("m" * 63))


def test_64_byte_secret_fails(agent):
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:too_long",
		"--sa-port", agent.port])

	assert(rc != 0)
	assert("--password: value from secrets:asbench:too_long is longer than 63 "
			"bytes\n" in out), out
	assert("L" * 64 not in out), out
	assert(b64("L" * 64)[:8] not in out), out
	assert(CONNECT_FAILED not in out), out


def test_tls_keyfile_password(agent, files):
	rc, out = run(["--tls-enable", "--tls-keyfile", files.key,
		"--tls-keyfile-password", "secrets:asbench:pw", "--sa-port", agent.port])

	assert("--tls-keyfile-password:" not in out), out
	assert(not TLS_KEY_FAILED.search(out)), out
	assert(CONNECT_FAILED in out), out
	assert_no_secret(out)


def test_tls_keyfile_wrong_password(agent, files):
	rc, out = run(["--tls-enable", "--tls-keyfile", files.key,
		"--tls-keyfile-password=secrets:asbench:wrong", "--sa-port", agent.port])

	assert(rc != 0)
	assert("--tls-keyfile-password:" not in out), out
	assert(TLS_KEY_FAILED.search(out)), out
	assert(CONNECT_FAILED not in out), out


def test_value_is_not_trimmed(agent, files):
	rc, out = run(["--tls-enable", "--tls-keyfile", files.key,
		"--tls-keyfile-password", "secrets:asbench:trailing_newline",
		"--sa-port", agent.port])

	assert(rc != 0)
	assert(TLS_KEY_FAILED.search(out)), out


def test_value_is_not_parsed_again(agent, files):
	rc, out = run(["--tls-enable", "--tls-keyfile", files.parsed_looking_key,
		"--tls-keyfile-password", "secrets:asbench:env_looking", "--sa-port",
		agent.port])

	assert("--tls-keyfile-password:" not in out), out
	assert("environment variable" not in out), out
	assert(not TLS_KEY_FAILED.search(out)), out
	assert(CONNECT_FAILED in out), out


def test_resource_selects_file(agent, files):
	rc, out = run(["--tls-enable", "--tls-keyfile", files.key,
		"--tls-keyfile-password", "secrets:other:pw", "--sa-port", agent.port])

	assert(TLS_KEY_FAILED.search(out)), out


@pytest.mark.parametrize("args", [
	["-Psecrets:asbench:pw", "--sa-address", "127.0.0.1:{port}"],
	["-P", "secrets:asbench:pw", "--sa-address=127.0.0.1:{port}"],
	["--password=secrets:asbench:pw", "--sa-port", "{port}"],
	["--sa-port", "{port}", "--password", "secrets:asbench:pw"],
	["-P", "secrets:asbench:pw", "--sa-address", "127.0.0.1:{closed}",
		"--sa-port", "{port}"],
	["-P", "secrets:asbench:pw", "--sa-port", "{port}", "--sa-address",
		"127.0.0.1:{closed}"],
	["-P", "secrets:asbench:pw", "--sa-address", "localhost", "--sa-port",
		"{port}"],
	["-P", "secrets:asbench:pw", "--sa-port", "{port}", "--sa-timeout", "5000"],
], ids=["password_first_attached", "password_first_separate", "password_long",
	"agent_first", "explicit_port_after_address",
	"explicit_port_before_address", "hostname", "timeout"])
def test_password(agent, args):
	closed = closed_port()
	rc, out = run(["-U", "admin"] +
			[a.format(port=agent.port, closed=closed) for a in args])

	assert_password_resolved(rc, out)


def test_tls_to_agent(agent):
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", "--sa-address",
		"localhost", "--sa-port", agent.tls_port, "--sa-cafile", agent.ca])

	assert_password_resolved(rc, out)


def assert_verify_failed(out, reason):
	assert("secret-agent: ERR: SSL_connect certificate verify failed: %s (" %
			reason in out), out
	assert("--password: secret agent request for secrets:asbench:pw failed: "
			"connection or protocol error\n" in out), out
	assert(CONNECT_FAILED not in out), out
	assert_no_secret(out)


def test_tls_unrelated_ca_fails(agent):
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", "--sa-address",
		"localhost:" + agent.tls_port, "--sa-cafile", agent.other_ca])

	assert(rc != 0)
	assert_verify_failed(out, "unable to get local issuer certificate")


def test_tls_agent_certificate_as_ca_fails(agent):
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", "--sa-address",
		"localhost:" + agent.tls_port, "--sa-cafile", agent.dns_cert])

	assert(rc != 0)
	assert_verify_failed(out, "unable to get local issuer certificate")


def test_tls_ip_not_in_certificate_fails(agent):
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", "--sa-address",
		"127.0.0.1:" + agent.tls_port, "--sa-cafile", agent.ca])

	assert(rc != 0)
	assert_verify_failed(out, "IP address mismatch")


def test_tls_hostname_not_in_certificate_fails(agent):
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", "--sa-address",
		"localhost:" + agent.tls_ip_port, "--sa-cafile", agent.ca])

	assert(rc != 0)
	assert_verify_failed(out, "hostname mismatch")


def test_tls_agent_without_cafile(agent):
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", "--sa-address",
		"localhost:" + agent.tls_port, "--sa-timeout", "2000"])

	assert(rc != 0)
	assert("--password: secret agent request for secrets:asbench:pw failed: "
			in out), out
	assert(CONNECT_FAILED not in out), out


def test_settings_dump_and_debug_hide_secret(agent):
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:pw", "--sa-port",
		agent.port, "-d"])

	assert("user:                   admin\n" in out), out
	assert_password_resolved(rc, out)


@pytest.mark.parametrize("path,detail", [
	("secrets:nope:pw",
		"secret-agent: ERR: response: resource not found in config: nope\n"),
	("secrets:asbench:nope", "secret-agent: ERR: response: nope not present "
		"in file %s/secrets.json\n" % AGENT_DIR),
	("secrets:asbench:empty", "secret-agent: ERR: empty secret\n"),
	("secrets:", "secret-agent: ERR: empty secret key\n"),
], ids=["missing_resource", "missing_key", "empty_value", "no_key"])
def test_agent_error(agent, path, detail):
	rc, out = run(["-U", "admin", "-P", path, "--sa-port", agent.port])

	assert(rc != 0)
	assert(detail in out), out
	assert("--password: secret agent request for %s failed: bad request\n" %
			path in out), out
	assert(CONNECT_FAILED not in out), out
	assert_no_secret(out)


def test_value_with_nul_byte(agent):
	rc, out = run(["-U", "admin", "-P", "secrets:asbench:with_nul", "--sa-port",
		agent.port])

	assert(rc != 0)
	assert("--password: secret agent returned a value containing a NUL byte "
			"for secrets:asbench:with_nul\n" in out), out
	assert(CONNECT_FAILED not in out), out
	assert("xyzzy" not in out), out
