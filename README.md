# Aerospike Benchmark Tool

![Build:Main](https://github.com/citrusleaf/aerospike-benchmark/workflows/Build:Main/badge.svg)
[![codecov](https://codecov.io/gh/aerospike/aerospike-benchmark/branch/main/graph/badge.svg?token=TPGZT8V6AA)](https://codecov.io/gh/aerospike/aerospike-benchmark)

This project contains the files necessary to build the C client benchmarking tool.  This program is used to insert data and generate load emulating real-world usage patterns of the database.

## Wiki
For more information on how to use the benchmark tool and configure it to your needs, visit the wiki [here](https://github.com/aerospike/aerospike-benchmark/wiki).

## Get started

### Dependencies

Asbackup builds the [Aerospike C Client](https://github.com/aerospike/aerospike-client-c) as a submodule.
Make sure all the C clients build dependencies are installed before starting the asbackup build.

Additional external dependencies:
 * OpenSSL (libssl and libcrypto)
 * libyaml-devel
 * libev, libuv, or libevent, if an event library is used

This project uses git submodules, so you will need to initialize and update submodules before building this project.

	$ git submodule update --init --recursive

### Build

To build the benchmark tool, run:
```sh
make [EVENT_LIB=libev|libuv|libevent]
```
with `EVENT_LIB` matching the event library used when the C client was compiled, if one was used (it is necessary to build with an event library to use async commands). If an event library is defined, it must be installed separately. Event libraries usually install into `/usr/local/lib`. Most operating systems do not search `/usr/local/lib` by default. Therefore, the following `LD_LIBRARY_PATH` setting may be necessary:
```sh
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib
```

Now, the benchmark executable will be located at `target/benchmark`.


#### Build with static linkage

To statically link the benchmark tool to external libraries, pass `<LIB_NAME>_STATIC_PATH`, set to the directory containing the respective static libraries, as an argument to `make`. To get these, you'll likely need to compile the library itself from source.

For example, for static linking against OpenSSL, with libssl.a and libcrypto.a in the directory /usr/local/lib, compile with `OPENSSL_STATIC_PATH=/usr/local/lib`.


### Running Tests and Coverage

To run the unit tests, call

```sh
make test [EVENT_LIB=libev|libuv|libevent]
```

To generate coverage data for the unit tests, run

```sh
make coverage [EVENT_LIB=libev|libuv|libevent]
```
> note: this will rerun the tests.

To view coverage data in the console, run

```sh
make report
```

## Example usage

To run a random read/update workload for 30 seconds, run:
```sh
target/benchmark --workload RU,50 --duration 30
```

To:
 * Connect to localhost:3000 using namespace "test".
 * Read 80% and write 20% of the time using 8 concurrent threads.
 * Use 1000000 keys and 1400 length byte array values using a single bin.
 * Timeout after 50ms for reads and writes.
 * Restrict transactions/second to 2500.
```sh
target/benchmarks -h 127.0.0.1 -p 3000 -n test -k 1000000 -o B1400 -w RU,80 -g 2500 -T 50 -z 8
```

To:
 * Benchmark asynchronous methods using 1 event loop.
 * Limit the maximum number of concurrent commands to 50.
 * Use and 50% read 50% write pattern.
```sh
target/benchmarks -h 127.0.0.1 -p 3000 -n test -k 1000000 -o S:50 -w RU,50 --async --asyncMaxCommands 50 --eventLoops 1
```

Command line usage can be read with:
```sh
target/benchmark --help
```
