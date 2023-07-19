
import codecs
import os
import string
import subprocess
import sys
import aerospike
import docker
import atexit
import shutil
import signal
import time

# the port to use for one of the cluster nodes
PORT = 3000
# the namespace to be used for the tests
NAMESPACE = "test"
# the set to be used for the tests
SET = "test"
CLIENT_ATTEMPTS = 20

# the number of server nodes to use
N_NODES = 2

WORK_DIRECTORY = "work"
LUA_DIRECTORY = "work/lua"
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
USE_DOCKER_SERVERS=False

# set when the cluser is up and running
RUNNING = False

# where to mount work directory in the docker container
CONTAINER_DIR = "/opt/work"

def graceful_exit(sig, frame):
	signal.signal(signal.SIGINT, g_orig_int_handler)
	stop()
	os.kill(os.getpid(), signal.SIGINT)

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


def start(do_reset=True):
	global CLIENT
	global NODES
	global RUNNING
	global SERVER_IP

	if not RUNNING:
		RUNNING = True

		if USE_DOCKER_SERVERS:
			print("Starting asd")

			init_work_dir()
			init_state_dirs()

			temp_file = absolute_path("aerospike.conf")
			mount_dir = absolute_path(WORK_DIRECTORY)

			first_base = PORT
			for index in range(1, 3):
				base = first_base + 1000 * (index - 1)
				conf_file = create_conf_file(temp_file, base,
						None if index == 1 else (SERVER_IP, first_base),
						index)
				cmd = '/usr/bin/asd --foreground --config-file %s --instance %s' % (CONTAINER_DIR + '/' + get_file(conf_file, base=mount_dir), str(index - 1))
				print('running in docker: %s' % cmd)
				container = DOCKER_CLIENT.containers.run("aerospike/aerospike-server:6.0.0.8",
						command=cmd,
						ports={
							str(base) + '/tcp': str(base),
							str(base + 1) + '/tcp': str(base + 1),
							str(base + 2) + '/tcp': str(base + 2),
							str(base + 3) + '/tcp': str(base + 3)
						},
						volumes={ mount_dir: { 'bind': CONTAINER_DIR, 'mode': 'rw' } },
						tty=True, detach=True, name='aerospike-%d' % (index))
				NODES[index-1] = container
				if index == 1:
					container.reload()
					SERVER_IP = container.attrs["NetworkSettings"]["Networks"]["bridge"]["IPAddress"]
		else:
			SERVER_IP = "127.0.0.1"

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
				if attempt < CLIENT_ATTEMPTS - 1:
					safe_sleep(1)
				else:
					raise

		print("Client connected")
	else:
		if do_reset:
			# if the cluster is already up and running, reset it
			reset()


def stop():
	global CLIENT
	global RUNNING
	global NODES

	"""
	Disconnects the client and stops the running asd process.
	"""
	if RUNNING:
		print("Disconnecting client")

		if CLIENT is None:
			print("No connected client")
		else:
			CLIENT.close()
			CLIENT = None

		print("Stopping asd")
		for i in range(0, N_NODES):
			if NODES[i] is not None:
				NODES[i].stop()
				NODES[i].remove()
				NODES[i] = None

		remove_state_dirs()
		remove_work_dir()

		RUNNING = False

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
			assert(False, "Process returned 0 exit code")
		except subprocess.CalledProcessError:
			pass

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

g_orig_int_handler = signal.getsignal(signal.SIGINT)
signal.signal(signal.SIGINT, graceful_exit)

# shut down the aerospike cluster when the tests are over
atexit.register(stop_silent)

