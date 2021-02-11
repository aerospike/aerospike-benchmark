Aerospike Benchmark
=============================

![Build:Main](https://github.com/citrusleaf/aerospike-benchmark/workflows/Build:Main/badge.svg)
[![codecov](https://codecov.io/gh/aerospike/aerospike-benchmark/branch/main/graph/badge.svg?token=TPGZT8V6AA)](https://codecov.io/gh/aerospike/aerospike-benchmark)

This project contains the files necessary to build C client benchmarks. 
This program is used to insert data and generate load. 

Build instructions:

```sh
export CLIENTREPO=<location of the C client repository>
make clean
make test
make [EVENT_LIB=libev|libuv|libevent]
```

The EVENT_LIB setting must also match the same setting when building the client itself.
If an event library is defined, it must be installed separately.  Event libraries usually
install into /usr/local/lib.  Most operating systems do not search /usr/local/lib by 
default.  Therefore, the following LD_LIBRARY_PATH setting may be necessary.

```sh
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib
```

Build examples:

```sh
make                     # synchronous functionality only
make EVENT_LIB=libev     # synchronous and asynchronous functionality with libev
make EVENT_LIB=libuv     # synchronous and asynchronous functionality with libuv
make EVENT_LIB=libevent  # synchronous and asynchronous functionality with libevent
```

The command line usage can be obtained by:

```sh
target/benchmarks --help
```

## Running Tests and Coverage

To run the unit tests, call

```sh
make test [EVENT_LIB=...]
```

To generate coverage data for the unit tests, run

```sh
make coverage [EVENT_LIB=...]
```
> note: this will rerun the tests.

To view coverage data in the console, run

```sh
make report
```

## Wiki
For more information on how to use the benchmark tool and configure it to your needs, visit the wiki [here](https://github.com/aerospike/aerospike-benchmark/wiki).
