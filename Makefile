
AS_HOST := 127.0.0.1
AS_PORT := 3000

# deprecated: support for explicit client repo
ifdef CLIENTREPO
$(warning Setting CLIENTREPO explicitly is deprecated, the c-client is now a submodule of asbackup)
DIR_C_CLIENT := $(CLIENTREPO)
endif

ARCH = $(shell uname -m)
PLATFORM = $(OS)-$(ARCH)
VERSION := $(shell git describe --tags --always --abbrev=9 2>/dev/null; if [ $${?} != 0 ]; then echo 'unknown'; fi)
ROOT = $(CURDIR)
NAME = $(shell basename $(ROOT))
OS = $(shell uname)
ARCH = $(shell uname -m)

M1_HOME_BREW =
ifeq ($(OS),Darwin)
  ifneq ($(wildcard /opt/homebrew),)
    M1_HOME_BREW = true
  endif
endif

# M1 macs brew install openssl under /opt/homebrew/opt/openssl
# set OPENSSL_PREFIX to the prefix for your openssl if it is installed elsewhere
OPENSSL_PREFIX ?= /usr/local/opt/openssl
ifdef M1_HOME_BREW
  OPENSSL_PREFIX = /opt/homebrew/opt/openssl
endif

CMAKE3_CHECK := $(shell cmake3 --help > /dev/null 2>&1 || (echo "cmake3 not found"))
CMAKE_CHECK := $(shell cmake --help > /dev/null 2>&1 || (echo "cmake not found"))

ifeq ($(CMAKE3_CHECK),)
  CMAKE := cmake3
else
ifeq ($(CMAKE_CHECK),)
  CMAKE := cmake
else
  $(error "no cmake binary found")
endif
endif

CFLAGS = -std=gnu11 -Wall -fPIC -O3 -MMD -MP
CFLAGS += -fno-common -fno-strict-aliasing
CFLAGS += -D_FILE_OFFSET_BITS=64 -D_REENTRANT -D_GNU_SOURCE
CFLAGS += -DTOOL_VERSION=\"$(VERSION)\"

DIR_LIBYAML ?= $(ROOT)/modules/libyaml
DIR_LIBYAML_BUILD := $(DIR_LIBYAML)/build
DIR_LIBCYAML ?= $(ROOT)/modules/libcyaml
DIR_LIBCYAML_BUILD_REL ?= build/release
DIR_LIBCYAML_BUILD ?= $(ROOT)/modules/libcyaml/$(DIR_LIBCYAML_BUILD_REL)

DIR_C_CLIENT ?= $(ROOT)/modules/c-client
C_CLIENT_LIB := $(DIR_C_CLIENT)/target/$(PLATFORM)/lib/libaerospike.a

DIR_INCLUDE =  $(ROOT)/src/include
DIR_INCLUDE += $(ROOT)/modules
DIR_INCLUDE += $(DIR_LIBYAML)/include
DIR_INCLUDE += $(DIR_LIBCYAML)/include
DIR_INCLUDE += $(DIR_C_CLIENT)/src/include
DIR_INCLUDE += $(DIR_C_CLIENT)/modules/common/src/include
DIR_INCLUDE += $(DIR_C_CLIENT)/modules/mod-lua/src/include
DIR_INCLUDE += $(DIR_C_CLIENT)/modules/base/src/include
INCLUDES = $(DIR_INCLUDE:%=-I%) 

DIR_ENV = $(ROOT)/env

ifneq ($(ARCH),$(filter $(ARCH),ppc64 ppc64le aarch64 arm64))
  CFLAGS += -march=nocona
endif

ifeq ($(OS),Darwin)
  CFLAGS += -D_DARWIN_UNLIMITED_SELECT
else ifeq ($(OS),Linux)
  CFLAGS += -rdynamic
endif

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
    LDFLAGS += -L$(OPENSSL_PREFIX)/lib
  endif
  LDFLAGS += -lssl
  LDFLAGS += -lcrypto
else
  LDFLAGS += $(OPENSSL_STATIC_PATH)/libssl.a
  LDFLAGS += $(OPENSSL_STATIC_PATH)/libcrypto.a
endif

ifeq ($(EVENT_LIB),libev)
  ifeq ($(LIBEV_STATIC_PATH),)
    LDFLAGS += -lev
  else
    LDFLAGS += $(LIBEV_STATIC_PATH)/libev.a
  endif
endif

ifeq ($(EVENT_LIB),libuv)
  ifeq ($(LIBUV_STATIC_PATH),)
    LDFLAGS += -luv
  else
    LDFLAGS += $(LIBUV_STATIC_PATH)/libuv.a
  endif
endif

ifeq ($(EVENT_LIB),libevent)
  ifeq ($(LIBEVENT_STATIC_PATH),)
    LDFLAGS += -levent_core -levent_pthreads
  else
    LDFLAGS += $(LIBEVENT_STATIC_PATH)/libevent_core.a $(LIBEVENT_STATIC_PATH)/libevent_pthreads.a
  endif
endif

LDFLAGS += -lpthread

ifeq ($(OS),Linux)
  LDFLAGS += -lrt -ldl
else ifeq ($(OS),FreeBSD)
  LDFLAGS += -lrt
endif

LDFLAGS += -lm -lz

# if this is an m1 mac using homebrew
# add the new homebrew lib and include path
# incase dependencies are installed there
# NOTE: /usr/local/include will be checked first
ifdef M1_HOME_BREW
  LDFLAGS += -L/opt/homebrew/lib
  INCLUDES += -I/opt/homebrew/include
endif

TEST_LDFLAGS = $(LDFLAGS) -Ltest_target/lib -lcheck 
BUILD_LDFLAGS = $(LDFLAGS) -Ltarget/lib

CC ?= cc
LD := $(CC)
AR = ar

BUILD_CFLAGS = $(CFLAGS)
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
	@echo "  CLIENTREPO: " $(DIR_C_CLIENT)
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
	@echo "      flags:      " $(BUILD_LDFLAGS)
	@echo


.PHONY: build
build: target/asbench

.PHONY: archive
archive: $(OBJECTS) target/libbench.a

target/libbench.a: $(OBJECTS)
	$(AR) -rcs $@ $(OBJECTS)


.PHONY: clean
clean:
	rm -rf target test_target $(DIR_ENV)
	$(MAKE) clean -C $(DIR_LIBCYAML)
	if [ -d $(DIR_LIBYAML_BUILD) ]; then $(MAKE) clean -C $(DIR_LIBYAML_BUILD); fi
	rm -rf $(DIR_LIBYAML_BUILD)
	$(MAKE) -C $(DIR_C_CLIENT) clean

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

target/lib/libyaml.a: $(DIR_LIBYAML_BUILD)/libyaml.a | target/lib
	cp $< $@

target/lib/libcyaml.a: $(DIR_LIBCYAML_BUILD)/libcyaml.a | target/lib
	cp $< $@

$(C_CLIENT_LIB):
	$(MAKE) -C $(DIR_C_CLIENT)

target/asbench: $(MAIN_OBJECT) $(OBJECTS) $(HDR_OBJECTS) target/lib/libcyaml.a target/lib/libyaml.a $(C_CLIENT_LIB) | target
	$(CC) -o $@ $(MAIN_OBJECT) $(OBJECTS) $(HDR_OBJECTS) target/lib/libcyaml.a target/lib/libyaml.a $(C_CLIENT_LIB) $(BUILD_LDFLAGS) 

-include $(wildcard $(MAIN_DEPENDENCIES))
-include $(wildcard $(DEPENDENCIES))
-include $(wildcard $(HDR_DEPENDENCIES))

$(DIR_LIBYAML_BUILD):
	mkdir $@

$(DIR_LIBYAML_BUILD)/libyaml.a: | $(DIR_LIBYAML_BUILD)
	cd $(DIR_LIBYAML_BUILD) && $(CMAKE) $(DIR_LIBYAML) -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5
	$(MAKE) -C $(DIR_LIBYAML_BUILD)

$(DIR_LIBCYAML_BUILD)/libcyaml.a:
	$(MAKE) -C $(DIR_LIBCYAML) $(DIR_LIBCYAML_BUILD_REL)/libcyaml.a LIBYAML_CFLAGS="-I$(DIR_LIBYAML)/include" VERSION_DEVEL=0

.PHONY: run
run: build
	./target/asbench -h $(AS_HOST) -p $(AS_PORT)

.PHONY: test
test: unit integration

# unit testing
.PHONY: unit
unit: |  test_target/test
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
	$(CC) $(TEST_CFLAGS) -o $@ -c $< $(INCLUDES)

test_target/obj/%.o: src/main/%.c | test_target/obj
	$(CC) $(TEST_CFLAGS) -fprofile-arcs -ftest-coverage -coverage -o $@ -c $< $(INCLUDES)

test_target/obj/hdr_histogram%.o: modules/hdr_histogram/%.c | test_target/obj/hdr_histogram
	$(CC) $(TEST_CFLAGS) -fprofile-arcs -ftest-coverage -coverage -o $@ -c $< $(INCLUDES)

test_target/test: $(TEST_OBJECTS) test_target/lib/libcyaml.a test_target/lib/libyaml.a $(C_CLIENT_LIB) | test_target
	$(CC) -fprofile-arcs -coverage -o $@ $(TEST_OBJECTS) test_target/lib/libcyaml.a test_target/lib/libyaml.a $(C_CLIENT_LIB) $(TEST_LDFLAGS)

# build the benchmark executable with code coverage
test_target/lib/libyaml.a: $(DIR_LIBYAML_BUILD)/libyaml.a | test_target/lib
	cp $< $@

test_target/lib/libcyaml.a: $(DIR_LIBCYAML_BUILD)/libcyaml.a | test_target/lib
	cp $< $@

test_target/asbench: $(TEST_MAIN_OBJECT) $(TEST_BENCH_OBJECTS) $(TEST_HDR_OBJECTS) test_target/lib/libcyaml.a test_target/lib/libyaml.a $(C_CLIENT_LIB) | test_target
	$(CC) -fprofile-arcs -coverage -o $@ $(TEST_MAIN_OBJECT) $(TEST_BENCH_OBJECTS) test_target/lib/libcyaml.a test_target/lib/libyaml.a $(TEST_HDR_OBJECTS) $(C_CLIENT_LIB) $(TEST_LDFLAGS)

-include $(wildcard $(TEST_DEPENDENCIES))

# integration testing
.PHONY: integration
integration: test_target/asbench
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

