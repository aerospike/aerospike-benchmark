
AS_HOST := 127.0.0.1
AS_PORT := 3000

ifndef CLIENTREPO
$(error Please set the CLIENTREPO environment variable)
endif

CLIENT_PATH = $(CLIENTREPO)
ARCH = $(shell uname -m)
PLATFORM = $(OS)-$(ARCH)
ROOT = $(CURDIR)
NAME = $(shell basename $(ROOT))
OS = $(shell uname)
ifeq ($(OS),Darwin)
	ARCH = $(shell uname -m)
else
	ARCH = $(shell uname -m)
endif

CFLAGS = -std=gnu99 -Wall -fPIC -O3 -MMD -MP
CFLAGS += -fno-common -fno-strict-aliasing
CFLAGS += -D_FILE_OFFSET_BITS=64 -D_REENTRANT -D_GNU_SOURCE

DIR_INCLUDE =  $(ROOT)/src/include
DIR_INCLUDE += $(ROOT)/modules
DIR_INCLUDE += $(ROOT)/modules/libcyaml/include
DIR_INCLUDE += $(CLIENT_PATH)/src/include
DIR_INCLUDE += $(CLIENT_PATH)/modules/common/src/include
DIR_INCLUDE += $(CLIENT_PATH)/modules/mod-lua/src/include
DIR_INCLUDE += $(CLIENT_PATH)/modules/base/src/include
INCLUDES = $(DIR_INCLUDE:%=-I%) 

DIR_ENV = $(ROOT)/env

ifneq ($(ARCH),$(filter $(ARCH),ppc64 ppc64le))
  CFLAGS += -march=nocona
endif

ifeq ($(OS),Darwin)
  CFLAGS += -D_DARWIN_UNLIMITED_SELECT
else ifeq ($(OS),Linux)
  CFLAGS += -rdynamic
endif

CFLAGS += $(INCLUDES) -I/usr/local/include

ifeq ($(EVENT_LIB),libev)
  CFLAGS += -DAS_USE_LIBEV
endif

ifeq ($(EVENT_LIB),libuv)
  CFLAGS += -DAS_USE_LIBUV
endif

ifeq ($(EVENT_LIB),libevent)
  CFLAGS += -DAS_USE_LIBEVENT
endif

LDFLAGS = -L/usr/local/lib

ifeq ($(OPENSSL_STATIC_PATH),)
  ifeq ($(OS),Darwin)
    LDFLAGS += -L/usr/local/opt/openssl/lib
  endif
  LDFLAGS += -lssl
  LDFLAGS += -lcrypto
else
  LDFLAGS += $(OPENSSL_STATIC_PATH)/libssl.a
  LDFLAGS += $(OPENSSL_STATIC_PATH)/libcrypto.a
endif

ifeq ($(EVENT_LIB),libev)
  LDFLAGS += -lev
endif

ifeq ($(EVENT_LIB),libuv)
  LDFLAGS += -luv
endif

ifeq ($(EVENT_LIB),libevent)
  LDFLAGS += -levent_core -levent_pthreads
endif

LDFLAGS += -lpthread

ifeq ($(OS),Linux)
  LDFLAGS += -lrt -ldl
else ifeq ($(OS),FreeBSD)
  LDFLAGS += -lrt
endif

ifeq ($(LIBYAML_STATIC_PATH),)
  LDFLAGS += -lyaml
else
  LDFLAGS += $(LIBYAML_STATIC_PATH)/libyaml.a
endif

LDFLAGS += -lm -lz -lcyaml
TEST_LDFLAGS = $(LDFLAGS) -Ltest_target/lib -lcheck 
LDFLAGS += -Ltarget/lib -flto
CC = cc
AR = ar

BUILD_CFLAGS = $(CFLAGS) -flto
TEST_CFLAGS = $(CFLAGS) -g -D_TEST

###############################################################################
##  OBJECTS                                                                  ##
###############################################################################

_MAIN_OBJECT = main.o
_SRC = $(filter-out src/main/main.c,$(shell find src/main -type f -name '*.c'))
_OBJECTS = $(patsubst src/main/%.c,%.o,$(_SRC))

_HDR_OBJECTS = hdr_histogram.o hdr_histogram_log.o hdr_encoding.o hdr_time.o

_TEST_SRC = $(shell find src/test -type f -name '*.c')
_TEST_OBJECTS = $(patsubst src/test/%.c,%.o,$(_TEST_SRC))

MAIN_OBJECT = $(addprefix target/obj/,$(_MAIN_OBJECT))
OBJECTS = $(addprefix target/obj/,$(_OBJECTS))
HDR_OBJECTS = $(addprefix target/obj/hdr_histogram/,$(_HDR_OBJECTS))

TEST_MAIN_OBJECT = $(MAIN_OBJECT:target/%=test_target/%)
TEST_BENCH_OBJECTS = $(OBJECTS:target/%=test_target/%)
TEST_HDR_OBJECTS = $(HDR_OBJECTS:target/%=test_target/%)
TEST_OBJECTS = $(addprefix test_target/obj/,$(_TEST_OBJECTS)) $(TEST_BENCH_OBJECTS) $(TEST_HDR_OBJECTS)

MAIN_DEPENDENCIES = $(MAIN_OBJECT:%.o=%.d)
DEPENDENCIES = $(OBJECTS:%.o=%.d)
HDR_DEPENDENCIES = $(HDR_OBJECTS:%.o=%.d)
TEST_DEPENDENCIES = $(TEST_OBJECTS:%.o=%.d) $(DEPENDENCIES:target/%=test_target/%) $(HDR_DEPENDENCIES:target/%=test_target/%)


###############################################################################
##  MAIN TARGETS                                                             ##
###############################################################################


.PHONY: all
all:  build

.PHONY: info
info:
	@echo
	@echo "  NAME:       " $(NAME) 
	@echo "  OS:         " $(OS)
	@echo "  ARCH:       " $(ARCH)
	@echo "  CLIENTREPO: " $(CLIENT_PATH)
	@echo "  WD:         " $(shell pwd)	
	@echo
	@echo "  PATHS:"
	@echo "      source:     " $(SOURCE)
	@echo "      target:     " $(TARGET_BASE)
	@echo "      includes:   " $(INC_PATH)
	@echo "      libraries:  " $(LIB_PATH)
	@echo
	@echo "  COMPILER:"
	@echo "      command:    " $(CC)
	@echo "      flags:      " $(BUILD_CFLAGS)
	@echo
	@echo "  LINKER:"
	@echo "      command:    " $(LD)
	@echo "      flags:      " $(LDFLAGS)
	@echo


.PHONY: build
build: target/benchmark

.PHONY: archive
archive: $(OBJECTS) target/libbench.a

target/libbench.a: $(OBJECTS)
	$(AR) -rcs $@ $(OBJECTS)


.PHONY: clean
clean:
	rm -rf target test_target $(DIR_ENV)
	$(MAKE) clean -C modules/libcyaml

target:
	mkdir $@

target/obj: | target
	mkdir $@

target/bin: | target
	mkdir $@

target/lib: | target
	mkdir $@

target/obj/hdr_histogram: | target/obj
	mkdir $@

target/obj/%.o: src/main/%.c | target/obj
	$(CC) $(BUILD_CFLAGS) -o $@ -c $< $(INCLUDES)

target/obj/hdr_histogram%.o: modules/hdr_histogram/%.c | target/obj/hdr_histogram
	$(CC) $(BUILD_CFLAGS) -o $@ -c $< $(INCLUDES)

target/lib/libcyaml.a: modules/libcyaml/build/debug/libcyaml.a | target/lib
	cp $< $@

target/benchmark: $(MAIN_OBJECT) $(OBJECTS) $(HDR_OBJECTS) target/lib/libcyaml.a $(CLIENTREPO)/target/$(PLATFORM)/lib/libaerospike.a | target
	$(CC) -o $@ $(MAIN_OBJECT) $(OBJECTS) $(HDR_OBJECTS) $(CLIENTREPO)/target/$(PLATFORM)/lib/libaerospike.a $(LDFLAGS)

-include $(wildcard $(MAIN_DEPENDENCIES))
-include $(wildcard $(DEPENDENCIES))
-include $(wildcard $(HDR_DEPENDENCIES))

modules/libcyaml/build/debug/libcyaml.a:
	$(MAKE) -C modules/libcyaml

.PHONY: run
run: build
	./target/benchmark -h $(AS_HOST) -p $(AS_PORT)

.PHONY: test
test: unit integration

# unit testing
.PHONY: unit
unit: | test_target/test
	@echo
	@#valgrind --tool=memcheck --leak-check=full --track-origins=yes ./test_target/test
	@./test_target/test

test_target:
	mkdir $@

test_target/obj: | test_target
	mkdir $@

test_target/obj/unit: | test_target/obj
	mkdir $@

test_target/obj/hdr_histogram: | test_target/obj
	mkdir $@

test_target/lib: | test_target
	mkdir $@

test_target/obj/unit/%.o: src/test/unit/%.c | test_target/obj/unit
	$(CC) $(TEST_CFLAGS) -o $@ -c $<

test_target/obj/%.o: src/main/%.c | test_target/obj
	$(CC) $(TEST_CFLAGS) -fprofile-arcs -ftest-coverage -coverage -o $@ -c $<

test_target/obj/hdr_histogram%.o: modules/hdr_histogram/%.c | test_target/obj/hdr_histogram
	$(CC) $(TEST_CFLAGS) -fprofile-arcs -ftest-coverage -coverage -o $@ -c $<

test_target/test: $(TEST_OBJECTS) target/lib/libcyaml.a $(CLIENTREPO)/target/$(PLATFORM)/lib/libaerospike.a | test_target
	$(CC) -fprofile-arcs -coverage -o $@ $(TEST_OBJECTS) $(CLIENTREPO)/target/$(PLATFORM)/lib/libaerospike.a $(TEST_LDFLAGS)

# build the benchmark executable with code coverage
test_target/lib/libcyaml.a: modules/libcyaml/build/debug/libcyaml.a | test_target/lib
	cp $< $@

test_target/benchmark: $(TEST_MAIN_OBJECT) $(TEST_BENCH_OBJECTS) $(TEST_HDR_OBJECTS) test_target/lib/libcyaml.a $(CLIENTREPO)/target/$(PLATFORM)/lib/libaerospike.a | test_target
	$(CC) -fprofile-arcs -coverage -o $@ $(TEST_MAIN_OBJECT) $(TEST_BENCH_OBJECTS) $(TEST_HDR_OBJECTS) $(CLIENTREPO)/target/$(PLATFORM)/lib/libaerospike.a $(TEST_LDFLAGS)

-include $(wildcard $(TEST_DEPENDENCIES))

# integration testing
.PHONY: integration
integration: test_target/benchmark
	@./integration_tests.sh $(DIR_ENV)

# Summary requires the lcov tool to be installed
.PHONY: coverage-unit
coverage-unit: do-unit
	@echo
	@lcov --no-external --capture --initial --directory test_target --output-file test_target/aerospike-benchmark-unit.info
	@lcov --directory test_target --capture --quiet --output-file test_target/aerospike-benchmark-unit.info
	@lcov --summary test_target/aerospike-benchmark-unit.info

.PHONY: coverage-integration
coverage-integration: do-integration
	@echo
	@lcov --no-external --capture --initial --directory test_target --output-file test_target/aerospike-benchmark-integration.info
	@lcov --directory test_target --capture --quiet --output-file test_target/aerospike-benchmark-integration.info
	@lcov --summary test_target/aerospike-benchmark-integration.info

.PHONY: coverage-all
coverage-all: | coverage-init
	@$(MAKE) -C . unit
	@$(MAKE) -C . integration
	@echo
	@lcov --no-external --capture --initial --directory test_target --output-file test_target/aerospike-benchmark-all.info
	@lcov --directory test_target --capture --quiet --output-file test_target/aerospike-benchmark-all.info
	@lcov --summary test_target/aerospike-benchmark-all.info

.PHONY: coverage-init
coverage-init:
	@lcov --zerocounters --directory test_target

.PHONY: do-unit
do-unit: | coverage-init
	@$(MAKE) -C . unit

.PHONY: do-integration
do-integration: | coverage-init
	@$(MAKE) -C . integration

.PHONY: report-unit
report-unit: test_target/aerospike-benchmark-unit.info
	@lcov -l test_target/aerospike-benchmark-unit.info

.PHONY: report-integration
report-integration: test_target/aerospike-benchmark-integration.info
	@lcov -l test_target/aerospike-benchmark-integration.info

.PHONY: report-all
report-all: test_target/aerospike-benchmark-all.info
	@lcov -l test_target/aerospike-benchmark-all.info

.PHONY: report-display-unit
report-display-unit: | test_target/aerospike-benchmark-unit.info
	@echo
	@rm -rf test_target/html
	@mkdir -p test_target/html
	@genhtml --prefix test_target/html --ignore-errors source test_target/aerospike-benchmark-unit.info --legend --title "test lcov" --output-directory test_target/html
	@xdg-open file://$(ROOT)/test_target/html/index.html

.PHONY: report-display-integration
report-display-integration: | test_target/aerospike-benchmark-integration.info
	@echo
	@rm -rf test_target/html
	@mkdir -p test_target/html
	@genhtml --prefix test_target/html --ignore-errors source test_target/aerospike-benchmark-integration.info --legend --title "test lcov" --output-directory test_target/html
	@xdg-open file://$(ROOT)/test_target/html/index.html

.PHONY: report-display-all
report-display-all: | test_target/aerospike-benchmark-all.info
	@echo
	@rm -rf test_target/html
	@mkdir -p test_target/html
	@genhtml --prefix test_target/html --ignore-errors source test_target/aerospike-benchmark-all.info --legend --title "test lcov" --output-directory test_target/html
	@xdg-open file://$(ROOT)/test_target/html/index.html

