
import codecs
import datetime
import fcntl
import functools
import ipaddress
import json
import os
import pty
import random
import re
import select
import socket
import uuid
import string
import subprocess
import sys
import threading
import aerospike
import docker
import atexit
import shutil
import signal
import termios
import time
import types

# the number of server nodes to use
N_NODES = 2

PORTS_PER_NODE = 4
NODE_PORT_STRIDE = 1000


def _port_free(port):
	with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
		try:
			s.bind(("127.0.0.1", port))
		except OSError:
			return False
	return True


def _pick_base_port():
	rng = random.SystemRandom()
	for _ in range(200):
		base = rng.randrange(10000, 60000, 10)
		ports = [base + NODE_PORT_STRIDE * n + i for n in range(N_NODES)
				for i in range(PORTS_PER_NODE)]
		if all(_port_free(p) for p in ports):
			return base
	raise RuntimeError("could not find free ports for the test cluster")


# the port to use for one of the cluster nodes
PORT = _pick_base_port()
# the namespace to be used for the tests
NAMESPACE = "test"
# the set to be used for the tests
SET = "test"
CLIENT_ATTEMPTS = 20

# per-session names: Docker Desktop serves a stale mount if a path is reused
SESSION_ID = "asbench-it-%d" % PORT
WORK_DIRECTORY = "work-%d" % PORT
LUA_DIRECTORY = WORK_DIRECTORY + "/lua"
CONTAINER_LABEL = "asbench-integration"
CONTAINER_OWNER_LABEL = "asbench-integration-pid"
SERVER_IMAGE = "aerospike/aerospike-server:8.2.0.0"
CLUSTER_READY_TIMEOUT = 60
STATE_DIRECTORIES = ["state-%d" % i for i in range(1, N_NODES+1)]
UDF_DIRECTORIES = ["udf-%d" % i for i in range(1, N_NODES+1)]

if sys.platform == "linux":
	USE_VALGRIND = False
else:
	USE_VALGRIND = False
DOCKER_CLIENT = docker.from_env()

# a list of docker instances running server nodes
NODES = [None for i in range(N_NODES)]
# the aerospike client
CLIENT = None

FILE_COUNT = 0

SETS = []
INDEXES = []
UDFS = []
SERVER_IP = None

# used for testing, disable to connect to a locally running aerospike server
USE_DOCKER_SERVERS=True

# set when the cluser is up and running
RUNNING = False

# where to mount work directory in the docker container
CONTAINER_DIR = "/opt/work"

def graceful_exit(sig, frame):
	signal.signal(sig, g_orig_handlers[sig])
	stop()
	os.kill(os.getpid(), sig)

def safe_sleep(secs):
	"""
	Sleeps, even in the presence of signals.
	"""
	start = time.time()
	end = start + secs

	while start < end:
		time.sleep(end - start)
		start = time.time()

def absolute_path(*path):
	"""
	Turns the given path into an absolute path.
	"""
	if len(path) == 1 and os.path.isabs(path[0]):
		return path[0]

	return os.path.abspath(os.path.join(os.path.dirname(__file__), *path))

def remove_dir(path):
	"""
	Removes a directory.
	"""
	print("Removing directory", path)

	for root, dirs, files in os.walk(path, False):
		for name in dirs:
			os.rmdir(os.path.join(root, name))

		for name in files:
			os.remove(os.path.join(root, name))

	os.rmdir(path)

def remove_work_dir():
	"""
	Removes the work directory.
	"""
	print("Removing work directory")
	work = absolute_path(WORK_DIRECTORY)
	lua = absolute_path(LUA_DIRECTORY)

	if os.path.exists(lua):
		remove_dir(lua)

	if os.path.exists(work):
		remove_dir(work)

def remove_state_dirs():
	"""
	Removes the runtime state directories.
	"""
	print("Removing state directories")

	for walker in STATE_DIRECTORIES:
		state = absolute_path(WORK_DIRECTORY, walker)

		if os.path.exists(state):
			remove_dir(state)

	for walker in UDF_DIRECTORIES:
		udf = absolute_path(WORK_DIRECTORY, walker)

		if os.path.exists(udf):
			remove_dir(udf)

def init_work_dir():
	"""
	Creates an empty work directory.
	"""
	remove_work_dir()
	print("Creating work directory")
	work = absolute_path(WORK_DIRECTORY)
	lua = absolute_path(LUA_DIRECTORY)
	os.mkdir(work, 0o755)
	os.mkdir(lua, 0o755)

def init_state_dirs():
	"""
	Creates empty state directories.
	"""
	remove_state_dirs()
	print("Creating state directories")

	for walker in STATE_DIRECTORIES:
		state = absolute_path(os.path.join(WORK_DIRECTORY, walker))
		os.mkdir(state, 0o755)
		smd = absolute_path(os.path.join(WORK_DIRECTORY, walker, "smd"))
		os.mkdir(smd, 0o755)

	for walker in UDF_DIRECTORIES:
		udf = absolute_path(os.path.join(WORK_DIRECTORY, walker))
		os.mkdir(udf, 0o755)

def temporary_path(extension):
	global FILE_COUNT
	"""
	Generates a path to a temporary file in the work directory using the
	given extension.
	"""
	FILE_COUNT += 1
	file_name = "tmp-" + ("%05d" % FILE_COUNT) + "." + extension
	return absolute_path(os.path.join(WORK_DIRECTORY, file_name))

def create_conf_file(temp_file, base, peer_addr, index):
	"""
	Create an Aerospike configuration file from the given template.
	"""
	with codecs.open(temp_file, "r", "UTF-8") as file_obj:
		temp_content = file_obj.read()

	params = {
		"state_directory": CONTAINER_DIR + "/state-" + str(index),
		"udf_directory": CONTAINER_DIR + "/udf-" + str(index),
		"service_port": str(base),
		"fabric_port": str(base + 1),
		"heartbeat_port": str(base + 2),
		"info_port": str(base + 3),
		"peer_connection": "# no peer connection" if not peer_addr \
				else "mesh-seed-address-port " + peer_addr[0] + " " + str(peer_addr[1] + 2),
		"namespace": NAMESPACE
	}

	temp = string.Template(temp_content)
	conf_content = temp.substitute(params)
	conf_file = temporary_path("conf")

	with codecs.open(conf_file, "w", "UTF-8") as file_obj:
		file_obj.write(conf_content)

	return conf_file

def get_file(path, base=None):
	if base is None:
		return os.path.basename(os.path.realpath(path))
	elif path.startswith(base):
		if path[len(base)] == '/':
			return path[len(base) + 1:]
		else:
			return path[len(base):]
	else:
		raise Exception('path %s is not in the directory %s' % (path, base))


def _pid_alive(pid):
	try:
		os.kill(pid, 0)
	except ProcessLookupError:
		return False
	except PermissionError:
		return True
	return True


def _remove_stale_containers():
	# never touch unlabeled containers or those of a live concurrent session
	stale = DOCKER_CLIENT.containers.list(all=True,
			filters={"label": CONTAINER_LABEL})
	for container in stale:
		owner = container.labels.get(CONTAINER_OWNER_LABEL, "")
		dead_owner = owner.isdigit() and not _pid_alive(int(owner))
		if container.status != "running" or dead_owner:
			print("Removing stale test container", container.name)
			container.remove(force=True)
			session = container.labels.get(CONTAINER_LABEL, "")
			if session.startswith("asbench-it-") and session != SESSION_ID:
				work = absolute_path("work-" + session[len("asbench-it-"):])
				if os.path.isdir(work):
					shutil.rmtree(work, ignore_errors=True)


def _parse_info(response):
	if "\t" in response:
		response = response.split("\t", 1)[1]
	return dict(kv.split("=", 1) for kv in response.strip().split(";") if "=" in kv)


def _cluster_state():
	sizes = []
	remaining = 0
	for err, resp in CLIENT.info_all("statistics").values():
		if err is None:
			sizes.append(_parse_info(resp).get("cluster_size"))
	for err, resp in CLIENT.info_all("namespace/" + NAMESPACE).values():
		if err is None:
			stats = _parse_info(resp)
			remaining += int(stats.get("migrate_tx_partitions_remaining", 0))
			remaining += int(stats.get("migrate_rx_partitions_remaining", 0))
	return sizes, remaining


def _wait_for_cluster():
	deadline = time.time() + CLUSTER_READY_TIMEOUT
	state = None
	while time.time() < deadline:
		try:
			state = _cluster_state()
			sizes, remaining = state
			if len(sizes) == N_NODES and all(s == str(N_NODES) for s in sizes) \
					and remaining == 0:
				return
		except Exception as e:
			state = e
		safe_sleep(0.25)
	raise RuntimeError("test cluster did not form within %ds (last state: %r)" %
			(CLUSTER_READY_TIMEOUT, state))


def _check_container_running(container):
	container.reload()
	if container.status != "running":
		logs = container.logs().decode("utf-8", "replace")[-4000:]
		raise RuntimeError("test container %s is %s:\n%s" % (container.name,
			container.status, logs))


def _start_cluster():
	global CLIENT
	global NODES
	global SERVER_IP

	if USE_DOCKER_SERVERS:
		print("Starting asd")
		_remove_stale_containers()

		init_work_dir()
		init_state_dirs()

		temp_file = absolute_path("aerospike.conf")
		mount_dir = absolute_path(WORK_DIRECTORY)

		first_base = PORT
		for index in range(1, N_NODES + 1):
			base = first_base + NODE_PORT_STRIDE * (index - 1)
			conf_file = create_conf_file(temp_file, base,
					None if index == 1 else (SERVER_IP, first_base),
					index)
			cmd = '/usr/bin/asd --foreground --config-file %s --instance %s' % (CONTAINER_DIR + '/' + get_file(conf_file, base=mount_dir), str(index - 1))
			print('running in docker: %s' % cmd)
			container = DOCKER_CLIENT.containers.run(SERVER_IMAGE,
					command=cmd,
					ports={
						str(base + i) + '/tcp': str(base + i)
						for i in range(PORTS_PER_NODE)
					},
					volumes={ mount_dir: { 'bind': CONTAINER_DIR, 'mode': 'rw' } },
					labels={ CONTAINER_LABEL: SESSION_ID,
						CONTAINER_OWNER_LABEL: str(os.getpid()) },
					tty=True, detach=True, name='%s-%d' % (SESSION_ID, index))
			NODES[index-1] = container
			_check_container_running(container)
			if index == 1:
				SERVER_IP = container.attrs["NetworkSettings"]["Networks"]["bridge"]["IPAddress"]

	print("Connecting client")
	SERVER_IP = "127.0.0.1"
	config = {
		"hosts": [(SERVER_IP, PORT)],
		"lua": { "user_path": absolute_path(LUA_DIRECTORY) }
	}

	for attempt in range(CLIENT_ATTEMPTS):
		try:
			CLIENT = aerospike.client(config).connect()
			break
		except Exception:
			for node in NODES:
				if node is not None:
					_check_container_running(node)
			if attempt < CLIENT_ATTEMPTS - 1:
				safe_sleep(1)
			else:
				raise

	print("Client connected, waiting for the cluster to form")
	_wait_for_cluster()
	print("Cluster ready")


def start(do_reset=True):
	global RUNNING

	if not RUNNING:
		try:
			_start_cluster()
		except BaseException:
			_teardown()
			raise
		RUNNING = True
	else:
		if do_reset:
			# if the cluster is already up and running, reset it
			reset()


def _teardown():
	global CLIENT
	global NODES

	if CLIENT is not None:
		try:
			CLIENT.close()
		except Exception as e:
			print("Failed to close client:", e)
		CLIENT = None

	for i in range(0, N_NODES):
		if NODES[i] is not None:
			try:
				NODES[i].remove(force=True)
			except Exception as e:
				print("Failed to remove container:", e)
			NODES[i] = None

	try:
		remove_state_dirs()
		remove_work_dir()
	except Exception as e:
		print("Failed to remove work directory:", e)


def stop():
	global RUNNING

	"""
	Disconnects the client and stops the running asd process.
	"""
	if RUNNING:
		RUNNING = False
		_teardown()

def reset():
	global UDFS
	global INDEXES
	"""
	Nukes the server, removing all records, indexes, udfs, etc.
	"""
	print("resetting the database")
	
	# truncate the set
	for set_name in [SET,]:
		if set_name is not None:
			set_name = set_name.strip()
		CLIENT.truncate(NAMESPACE, None if not set_name else set_name, 0)

	# delete all udfs
	for udf in UDFS:
		CLIENT.udf_remove(udf)
	UDFS = []

	# delete all indexes
	for index in INDEXES:
		try:
			CLIENT.index_remove(NAMESPACE, index)
		except aerospike.exception.IndexNotFound:
			# the index may not actually be there if we are only backing up certain
			# sets, but this is ok, so fail silently
			pass
	INDEXES = []


def run_benchmark(args, ip=None, port=PORT, expect_success=True, do_reset=True):
	global SERVER_IP

	start(do_reset=do_reset)
	directory = absolute_path("../../..")

	if ip is None:
		ip = SERVER_IP

	if USE_VALGRIND:
		cmd = ["valgrind", "--tool=memcheck", "--leak-check=full", "--track-origins=yes"]
	else:
		cmd = []
	cmd += ["test_target/asbench", "-h", f"{ip}:{port}", "-n", NAMESPACE, "-s", SET] + args

	print("executing:", ' '.join(cmd))
	if expect_success:
		subprocess.check_call(cmd, cwd=directory)
	else:
		try:
			subprocess.check_call(cmd, cwd=directory)
		except subprocess.CalledProcessError:
			pass
		else:
			assert False, "Process returned 0 exit code"


def _recv_exact(conn, size):
	data = b""
	while len(data) < size:
		chunk = conn.recv(size - len(data))
		if not chunk:
			break
		data += chunk
	return data


def shutdown_listener(srv):
	"""
	Closes a listening socket so that a thread blocked in accept() wakes up,
	which close() alone does not do on Linux.
	"""
	try:
		srv.shutdown(socket.SHUT_RDWR)
	except OSError:
		# macOS raises ENOTCONN for a listening socket
		pass
	srv.close()


def run_job(args, typed=None, env=None, keys=b"", ignore=(), foreground=True,
		split_output=False):
	"""
	Runs asbench as a shell runs a job: in its own process group under a
	session leader whose controlling terminal is a pty, in the foreground
	unless foreground is False. The pty is asbench's stdin, and its stdout and
	stderr unless split_output sends them to two pipes. Signals in ignore start
	out ignored, as after trap '' in a shell. Once asbench turns echo off,
	sends keys, then types typed and a newline. A job that stops is killed.
	Returns asbench's wait status, what the pty showed, its stdout and stderr
	when split, and whether echo is on after it ends.
	"""
	full_env = dict(os.environ)
	full_env.update(env or {})
	root = absolute_path("../../..")
	cmd = [os.path.join(root, "test_target/asbench")] + args
	master, slave = pty.openpty()
	status_r, status_w = os.pipe()
	done_r, done_w = os.pipe()
	out_r, out_w = os.pipe() if split_output else (None, slave)
	err_r, err_w = os.pipe() if split_output else (None, slave)
	leader = os.fork()

	if leader == 0:
		try:
			os.close(done_w)
			os.setsid()
			fcntl.ioctl(slave, termios.TIOCSCTTY, 0)
			go_r, go_w = os.pipe()
			job = os.fork()
			if job == 0:
				os.setpgid(0, 0)
				os.read(go_r, 1)
				for sig in ignore:
					signal.signal(sig, signal.SIG_IGN)
				os.dup2(slave, 0)
				os.dup2(out_w, 1)
				os.dup2(err_w, 2)
				os.chdir(root)
				os.execve(cmd[0], cmd, full_env)
			for fd in {out_w, err_w} - {slave}:
				os.close(fd)
			os.setpgid(job, job)
			if foreground:
				os.tcsetpgrp(slave, job)
			os.write(go_w, b"x")
			_, status = os.waitpid(job, os.WUNTRACED)
			if os.WIFSTOPPED(status):
				os.kill(job, signal.SIGKILL)
				os.waitpid(job, 0)
			os.write(status_w, b"%d" % status)
			# macOS revokes the terminal when its session leader exits
			os.read(done_r, 1)
		except BaseException as e:
			os.write(status_w, repr(e).encode())
		finally:
			os._exit(0)

	for fd in {status_w, done_r, out_w, err_w} - {slave}:
		os.close(fd)

	outputs = {fd: b"" for fd in (master, out_r, err_r) if fd is not None}
	open_fds = set(outputs)
	status = None
	sent = False
	deadline = time.time() + 60

	try:
		while status is None and time.time() < deadline:
			for fd in select.select(list(open_fds) + [status_r], [], [], 0.05)[0]:
				data = os.read(fd, 1024)
				if fd == status_r:
					assert data.isdigit(), \
							"the job's session leader failed: %r" % data
					status = int(data)
				elif data:
					outputs[fd] += data
				else:
					open_fds.discard(fd)
			if foreground and status is None and not sent and \
					not termios.tcgetattr(slave)[3] & termios.ECHO:
				os.write(master, keys)
				time.sleep(0.5 if keys else 0)
				if typed is not None:
					os.write(master, typed.encode() + b"\n")
				sent = True

		assert status is not None, "asbench did not finish"

		while open_fds and select.select(list(open_fds), [], [], 0.2)[0]:
			for fd in select.select(list(open_fds), [], [], 0)[0]:
				data = os.read(fd, 1024)
				if data:
					outputs[fd] += data
				else:
					open_fds.discard(fd)

		echo_after = bool(termios.tcgetattr(slave)[3] & termios.ECHO)
	finally:
		os.close(done_w)
		if status is None:
			os.kill(leader, signal.SIGKILL)
		os.waitpid(leader, 0)
		for fd in [master, slave, status_r] + [fd for fd in (out_r, err_r)
				if fd is not None]:
			os.close(fd)

	text = {fd: data.decode(errors="replace") for fd, data in outputs.items()}
	return types.SimpleNamespace(status=status, out=text[master],
			stdout=text.get(out_r), stderr=text.get(err_r), echo_after=echo_after)


def run_typed(args, typed, env=None, **job):
	"""
	Runs asbench as a foreground job at a terminal, types typed and a newline
	once it prompts for a password, and returns its exit code and output. Any
	other run_job option can be given.
	"""
	job = run_job(args, typed, env, **job)
	return os.waitstatus_to_exitcode(job.status), job.out


def capture_login(args, env=None, stdin="", typed=None, **job):
	"""
	Runs asbench against a fake server that records what the client sends and
	never answers, and returns the login request, which holds the credential.
	If typed is given, it is typed at a terminal instead of piped on stdin,
	and any other run_job option can be given.
	"""
	srv = socket.socket()
	srv.bind(("127.0.0.1", 0))
	srv.listen(8)
	packets = []

	def serve():
		while True:
			try:
				conn, _ = srv.accept()
			except OSError:
				return
			with conn:
				conn.settimeout(5)
				header = _recv_exact(conn, 8)
				size = int.from_bytes(header, "big") & 0xFFFFFFFFFFFF
				packets.append(header + _recv_exact(conn, size))

	thread = threading.Thread(target=serve, daemon=True)
	thread.start()

	full_env = dict(os.environ)
	full_env.update(env or {})
	args = ["-z", "1", "-h", "127.0.0.1", "-p", str(srv.getsockname()[1])] + args
	try:
		if typed is None:
			proc = subprocess.run(["test_target/asbench"] + args,
					cwd=absolute_path("../../.."), env=full_env, input=stdin,
					stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
					timeout=60)
			rc, out = proc.returncode, proc.stdout
		else:
			rc, out = run_typed(args, typed, env, **job)
	finally:
		shutdown_listener(srv)
		thread.join(5)

	assert rc != 0, out
	assert len(set(packets)) == 1, "expected one distinct login request, " \
			"got %d:\n%s" % (len(set(packets)), out)
	# proto version 2, type 2 (admin)
	assert packets[0][:2] == b"\x02\x02", packets[0][:8]
	return packets[0]


@functools.lru_cache(maxsize=None)
def literal_login(password):
	"""
	The login request for a literal -P value, captured once and shared by the
	tests that compare against it. password must not start with a source
	prefix.
	"""
	return capture_login(["-U", "admin", "-P", password])


def set_namespace_config(param, value):
	cmd = "set-config:context=namespace;id=%s;%s=%s" % (NAMESPACE, param, value)
	for err, resp in CLIENT.info_all(cmd).values():
		assert(err is None), err
		assert(resp.strip().endswith("ok")), resp

def scan_records():
	recs = []
	CLIENT.scan(NAMESPACE, SET).foreach(lambda record: recs.append(record))
	return recs

def get_record(key):
	return CLIENT.get((NAMESPACE, SET, key))

def upload_udf(file_name, file_contents):
	assert(file_name[-4:] == '.lua')
	file_path = absolute_path(os.path.join(WORK_DIRECTORY, file_name))
	with open(file_path, 'w') as file:
		file.write(file_contents)
	CLIENT.udf_put(file_path, 0)
	UDFS.append(file_name[:-4])


# record structure validation
def obj_spec_is_b(val):
	assert(type(val) is bool)

def obj_spec_is_const_b(val, cnst):
	assert(type(val) is bool)
	assert(val == cnst)

def obj_spec_is_I1(val):
	assert(type(val) is int)
	assert(0 <= val < 256)

def obj_spec_is_I2(val):
	assert(type(val) is int)
	assert(256 <= val < 65536)

def obj_spec_is_I3(val):
	assert(type(val) is int)
	assert(65536 <= val < 2**24)

def obj_spec_is_I4(val):
	assert(type(val) is int)
	assert(2**24 <= val < 2**32)

def obj_spec_is_I5(val):
	assert(type(val) is int)
	assert(2**32 <= val < 2**40)

def obj_spec_is_I6(val):
	assert(type(val) is int)
	assert(2**40 <= val < 2**48)

def obj_spec_is_I7(val):
	assert(type(val) is int)
	assert(2**48 <= val < 2**56)

def obj_spec_is_I8(val):
	assert(type(val) is int)
	assert(2**56 <= val < 2**63 or -2**63 <= val < 0)

def obj_spec_is_const_I(val, cnst):
	assert(type(val) is int)
	assert(val == cnst)

def obj_spec_is_D(val):
	assert(type(val) is float)

def obj_spec_is_const_D(val, cnst):
	assert(type(val) is float)
	assert(val == cnst)

def obj_spec_is_S(val, size):
	assert(type(val) is str)
	assert(len(val) == size)
	for ch in val:
		assert('a' <= ch <= 'z' or '0' <= ch <= '9')

def obj_spec_is_const_S(val, cnst):
	assert(type(val) is str)
	assert(val == cnst)

def obj_spec_is_B(val, size):
	assert(type(val) is bytes)
	assert(len(val) == size)

def check_recs_exist_in_range(key_start, key_end, obj_checker=None):
	for key in range(key_start, key_end):
		record = get_record(key)
		assert(record is not None)

		if obj_checker is not None:
			obj_checker(*record)

def check_for_range(key_start, key_end, obj_checker=None):
	assert(len(scan_records()) == key_end - key_start)
	check_recs_exist_in_range(key_start, key_end, obj_checker=obj_checker)


ASCII_RE = re.compile(r"^[\x20-\x7e]+$")
USERNAME_RE = re.compile(r"^[a-z0-9._]+$")
DOMAIN_RE = re.compile(r"^[a-z0-9]+\.[a-z]+$")

def obj_spec_is_ascii_str(val, min_len=1, max_len=1024):
	assert(type(val) is str)
	assert(min_len <= len(val) <= max_len)
	assert(ASCII_RE.match(val))

def obj_spec_is_email(val):
	obj_spec_is_ascii_str(val)
	user, domain = val.split("@")
	assert(USERNAME_RE.match(user))
	assert("." in domain)

def obj_spec_is_ipv4(val):
	assert(type(val) is str)
	ip = ipaddress.ip_address(val)
	assert(ip.version == 4)

def obj_spec_is_ipv6(val):
	assert(type(val) is str)
	ip = ipaddress.ip_address(val)
	assert(ip.version == 6)

def obj_spec_is_mac(val):
	assert(re.match(r"^[0-9a-f]{2}(:[0-9a-f]{2}){5}$", val))

def obj_spec_is_uuid(val):
	u = uuid.UUID(val)
	assert(u.version == 4)
	assert(str(u) == val)

def obj_spec_is_zip(val):
	assert(re.match(r"^[0-9]{5}$", val))

def obj_spec_is_phone(val):
	assert(re.match(r"^[2-9][0-9]{2}-[2-9][0-9]{2}-[0-9]{4}$", val))

def obj_spec_is_state_abbr(val):
	assert(re.match(r"^[A-Z]{2}$", val))

def obj_spec_is_date(val, fmt="%Y-%m-%d"):
	assert(type(val) is str)
	datetime.datetime.strptime(val, fmt)

def obj_spec_is_int_range(val, lo, hi):
	assert(type(val) is int)
	assert(lo <= val <= hi)

def obj_spec_is_double_range(val, lo, hi):
	assert(type(val) is float)
	assert(lo <= val <= hi)

def obj_spec_is_words(val, n):
	obj_spec_is_ascii_str(val)
	assert(len(val.split(" ")) == n)

def obj_spec_in(val, options):
	assert(val in options)

def obj_spec_is_credit_card(val):
	assert(re.match(r"^[0-9]{15,16}$", val))
	digits = [int(c) for c in val]
	total = 0
	for i, d in enumerate(reversed(digits)):
		if i % 2 == 1:
			d *= 2
			if d > 9:
				d -= 9
		total += d
	assert(total % 10 == 0)

def geojson_dict(val):
	assert(isinstance(val, aerospike.GeoJSON))
	return json.loads(val.dumps())

def obj_spec_is_geojson_point(val, lat_min=24, lat_max=49, lon_min=-125,
		lon_max=-66):
	g = geojson_dict(val)
	assert(g["type"] == "Point")
	lon, lat = g["coordinates"]
	assert(lat_min <= lat <= lat_max)
	assert(lon_min <= lon <= lon_max)

def obj_spec_is_geo_circle(val, r_min=100, r_max=5000):
	g = geojson_dict(val)
	assert(g["type"] == "AeroCircle")
	(lon, lat), radius = g["coordinates"]
	assert(r_min <= radius <= r_max)

def normalize(val):
	if isinstance(val, aerospike.GeoJSON):
		return ("geojson", val.dumps())
	if isinstance(val, dict):
		return {normalize_key(k): normalize(v) for k, v in val.items()}
	if isinstance(val, list):
		return [normalize(v) for v in val]
	return val

def normalize_key(key):
	return key if not isinstance(key, (list, dict)) else repr(key)

def get_records(key_start, key_end):
	recs = {}
	for key in range(key_start, key_end):
		record = get_record(key)
		assert(record is not None)
		recs[key] = {name: normalize(v) for name, v in record[2].items()}
	return recs

def run_benchmark_output(args, ip=None, port=PORT, do_reset=True):
	start(do_reset=do_reset)
	directory = absolute_path("../../..")
	if ip is None:
		ip = SERVER_IP
	cmd = ["test_target/asbench", "-h", f"{ip}:{port}", "-n", NAMESPACE, "-s",
			SET] + args
	print("executing:", ' '.join(cmd))
	proc = subprocess.run(cmd, cwd=directory, stdout=subprocess.PIPE,
			stderr=subprocess.STDOUT, text=True)
	print(proc.stdout)
	return proc.returncode, proc.stdout

def create_index(kind, bin_name, index_name, index_type=None):
	if index_type is None:
		if kind == "geo":
			CLIENT.index_geo2dsphere_create(NAMESPACE, SET, bin_name, index_name)
		elif kind == "string":
			CLIENT.index_string_create(NAMESPACE, SET, bin_name, index_name)
		else:
			CLIENT.index_integer_create(NAMESPACE, SET, bin_name, index_name)
	else:
		datatype = {"geo": aerospike.INDEX_GEO2DSPHERE,
				"string": aerospike.INDEX_STRING}.get(kind, aerospike.INDEX_NUMERIC)
		CLIENT.index_list_create(NAMESPACE, SET, bin_name, datatype, index_name)
	INDEXES.append(index_name)

def query_keys(predicate, expected=None, attempts=20):
	keys = set()
	for attempt in range(attempts):
		keys = set()
		q = CLIENT.query(NAMESPACE, SET)
		q.where(predicate)
		for (key, meta, bins) in q.results():
			keys.add(key[2] if key[2] is not None else bytes(key[3]))
		if expected is None or len(keys) >= expected:
			break
		safe_sleep(0.5)
	return keys


def stop_silent():
	# silence stderr and stdout
	stdout_tmp = sys.stdout
	stderr_tmp = sys.stderr
	null = open(os.devnull, 'w')
	sys.stdout = null
	sys.stderr = null
	try:
		stop()
		sys.stdout = stdout_tmp
		sys.stderr = stderr_tmp
	except:
		sys.stdout = stdout_tmp
		sys.stderr = stderr_tmp
		raise

g_orig_handlers = {sig: signal.getsignal(sig) for sig in (signal.SIGINT, signal.SIGTERM)}
for _sig in g_orig_handlers:
	signal.signal(_sig, graceful_exit)

# shut down the aerospike cluster when the tests are over
atexit.register(stop_silent)

