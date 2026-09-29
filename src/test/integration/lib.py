
import codecs
import datetime
import ipaddress
import json
import os
import random
import re
import socket
import uuid
import string
import subprocess
import sys
import aerospike
import docker
import atexit
import shutil
import signal
import time

# the number of server nodes to use
N_NODES = 2

# each node uses 4 consecutive ports (service, fabric, heartbeat, info), and
# node i starts at PORT + 1000 * i
PORTS_PER_NODE = 4
NODE_PORT_STRIDE = 1000


def _port_free(port):
	with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
		try:
			s.bind(("0.0.0.0", port))
		except OSError:
			return False
	return True


def _pick_base_port():
	"""
	Picks a random base port such that every port of every cluster node is
	free, so the tests never collide with an Aerospike server (or anything
	else) already running on this machine.
	"""
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

# every test session gets its own work directory and container names (keyed by
# its port range), so a session never reuses a path or name left by another one
SESSION_ID = "asbench-it-%d" % PORT
WORK_DIRECTORY = "work-%d" % PORT
LUA_DIRECTORY = WORK_DIRECTORY + "/lua"
CONTAINER_LABEL = "asbench-integration"
CONTAINER_OWNER_LABEL = "asbench-integration-pid"
SERVER_IMAGE = "aerospike/aerospike-server:6.0.0.8"
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
	"""
	Removes containers left behind by earlier test sessions that were killed
	before they could clean up: stopped ones, and running ones whose owning
	test process no longer exists. Containers of a live concurrent session and
	containers without this harness's label are never touched.
	"""
	stale = DOCKER_CLIENT.containers.list(all=True,
			filters={"label": CONTAINER_LABEL})
	for container in stale:
		owner = container.labels.get(CONTAINER_OWNER_LABEL, "")
		dead_owner = owner.isdigit() and not _pid_alive(int(owner))
		if container.status != "running" or dead_owner:
			print("Removing stale test container", container.name)
			container.remove(force=True)


def _parse_info(response):
	"""
	Turns an info response ("cmd\tk1=v1;k2=v2") into a dict.
	"""
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
	"""
	Blocks until every node reports a cluster of N_NODES and no partitions are
	migrating, so tests never run against a half formed cluster.
	"""
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
	"""
	Disconnects the client, removes the containers and deletes the work
	directory. Every step runs even if an earlier one fails.
	"""
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


# synthetic data (@generator) validation
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
	"""
	Turns a record value into plain python data so records can be compared.
	"""
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
	"""
	Returns {key: bins} for every key in [key_start, key_end), with values
	normalized for comparison.
	"""
	recs = {}
	for key in range(key_start, key_end):
		record = get_record(key)
		assert(record is not None)
		recs[key] = {name: normalize(v) for name, v in record[2].items()}
	return recs

def run_benchmark_output(args, ip=None, port=PORT, do_reset=True):
	"""
	Runs asbench like run_benchmark, returning (exit code, combined output).
	"""
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
	"""
	Creates a secondary index and remembers it so reset() removes it.
	kind is "geo" or "numeric"/"string" for scalar bins; pass index_type
	(e.g. aerospike.INDEX_TYPE_LIST) for collection element indexes.
	"""
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
	"""
	Runs a secondary index query and returns the set of matching integer keys,
	retrying while the index is still being built.
	"""
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

