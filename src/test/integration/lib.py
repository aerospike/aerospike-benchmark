
import codecs
import os
import string
import sys
import aerospike
import docker
import atexit
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
STATE_DIRECTORIES = ["state-%d" % i for i in range(1, N_NODES+1)]
UDF_DIRECTORIES = ["udf-%d" % i for i in range(1, N_NODES+1)]

if sys.platform == "linux":
	USE_VALGRIND = True
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

# set when the cluser is up and running
RUNNING = False

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
	os.mkdir(work, 0o755)

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
		"state_directory": "/opt/aerospike/work/state-" + str(index),
		"udf_directory": "/opt/aerospike/work/udf-" + str(index),
		"service_port": str(base),
		"fabric_port": str(base + 1),
		"heartbeat_port": str(base + 2),
		"info_port": str(base + 3),
		"peer_connection": "# no peer connection" if not peer_addr \
				else "mesh-seed-address-port " + peer_addr[0] + " " + str(peer_addr[1] + 2)
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


def start():
	global CLIENT
	global NODES
	global RUNNING

	if not RUNNING:
		RUNNING = True
		print("Starting asd")

		init_work_dir()
		init_state_dirs()

		temp_file = absolute_path("aerospike.conf")
		mount_dir = absolute_path(WORK_DIRECTORY)

		first_base = PORT
		first_ip = None
		for index in range(1, 3):
			base = first_base + 1000 * (index - 1)
			conf_file = create_conf_file(temp_file, base,
					None if index == 1 else (first_ip, first_base),
					index)
			cmd = '/usr/bin/asd --foreground --config-file %s --instance %s' % ('/opt/aerospike/work/' + get_file(conf_file, base=mount_dir), str(index - 1))
			print('running in docker: %s' % cmd)
			container = DOCKER_CLIENT.containers.run("aerospike/aerospike-server",
					command=cmd,
					ports={
						str(base) + '/tcp': str(base),
						str(base + 1) + '/tcp': str(base + 1),
						str(base + 2) + '/tcp': str(base + 2),
						str(base + 3) + '/tcp': str(base + 3)
					},
					volumes={ mount_dir: { 'bind': '/opt/aerospike/work', 'mode': 'rw' } },
					tty=True, detach=True, name='aerospike-%d' % (index))
			NODES[index-1] = container
			if index == 1:
				container.reload()
				first_ip = container.attrs["NetworkSettings"]["Networks"]["bridge"]["IPAddress"]

		print("Connecting client")
		config = {"hosts": [("127.0.0.1", PORT)]}

		for attempt in range(CLIENT_ATTEMPTS):
			try:
				CLIENT = aerospike.client(config).connect()
				break
			except Exception:
				if attempt < CLIENT_ATTEMPTS - 1:
					safe_sleep(.2)
				else:
					raise

		print("Client connected")


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
		CLIENT.udf_remove(udf["name"])
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


def run_benchmark(args, expected_ret=0):
	start()
	assert(os.system("test_target/benchmark %s" % args) == expected_ret)


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

