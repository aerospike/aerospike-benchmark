
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

CFLAGS = -std=gnu99 -g -Wall -fPIC -O3
CFLAGS += -fno-common -fno-strict-aliasing
CFLAGS += -D_FILE_OFFSET_BITS=64 -D_REENTRANT -D_GNU_SOURCE

DIR_INCLUDE =  $(ROOT)/src/include
DIR_INCLUDE += $(CLIENT_PATH)/src/include
DIR_INCLUDE += $(CLIENT_PATH)/modules/common/src/include
DIR_INCLUDE += $(CLIENT_PATH)/modules/mod-lua/src/include
DIR_INCLUDE += $(CLIENT_PATH)/modules/base/src/include
INCLUDES = $(DIR_INCLUDE:%=-I%) 

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

ifeq ($(OS),Darwin)
  LDFLAGS += -L/usr/local/opt/openssl/lib
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

LDFLAGS += -lssl -lcrypto -lpthread

ifeq ($(OS),Linux)
  LDFLAGS += -lrt -ldl
else ifeq ($(OS),FreeBSD)
  LDFLAGS += -lrt
endif

# Use the Lua submodule?  [By default, yes.]
USE_LUAMOD = 1

# Use LuaJIT instead of Lua?  [By default, no.]
USE_LUAJIT = 0

# Permit easy overriding of the default.
ifeq ($(USE_LUAJIT),1)
  USE_LUAMOD = 0
endif

ifeq ($(and $(USE_LUAMOD:0=),$(USE_LUAJIT:0=)),1)
  $(error Only at most one of USE_LUAMOD or USE_LUAJIT may be enabled (i.e., set to 1.))
endif

ifeq ($(USE_LUAJIT),1)
  ifeq ($(OS),Darwin)
    LDFLAGS += -pagezero_size 10000 -image_base 100000000
  endif
else
  ifeq ($(USE_LUAMOD),0)
    # Find where the Lua development package is installed in the build environment.
    ifeq ($(OS),Darwin)
      LUA_LIBPATH = $(or \
	$(wildcard /usr/local/lib/liblua.5.1.dylib), \
	$(wildcard /usr/local/lib/liblua.5.1.a), \
	$(wildcard /usr/local/lib/liblua.dylib), \
	$(wildcard /usr/local/lib/liblua.a), \
	   $(error Cannot find liblua 5.1))
      LUA_LIBDIR = $(dir $(LUA_LIBPATH))
      LUA_LIB = $(patsubst lib%,%,$(basename $(notdir $(LUA_LIBPATH))))
    else
      # Linux
      LUA_LIBPATH = $(or \
	$(wildcard /usr/lib/liblua5.1.so), \
	$(wildcard /usr/lib/liblua5.1.a), \
	$(wildcard /usr/lib/x86_64-linux-gnu/liblua5.1.so), \
	$(wildcard /usr/lib/x86_64-linux-gnu/liblua5.1.a), \
	$(wildcard /usr/lib64/liblua-5.1.so), \
	$(wildcard /usr/lib64/liblua-5.1.a), \
	$(wildcard /usr/lib/liblua.so), \
	$(wildcard /usr/lib/liblua.a), \
	   $(error Cannot find liblua 5.1))
      LUA_LIBDIR = $(dir $(LUA_LIBPATH))
      LUA_LIB = $(patsubst lib%,%,$(basename $(notdir $(LUA_LIBPATH))))
    endif
    LDFLAGS += -L$(LUA_LIBDIR) -l$(LUA_LIB)
  endif
endif

LDFLAGS += -lm -lz
TEST_LDFLAGS = $(LDFLAGS) -lcheck 
CC = cc
AR = ar

###############################################################################
##  OBJECTS                                                                  ##
###############################################################################

MAIN_OBJECT = main.o
OBJECTS = benchmark.o histogram.o latency.o linear.o random.o record.o
TEST_OBJECTS = sanity.o setup.o main.o

###############################################################################
##  MAIN TARGETS                                                             ##
###############################################################################


.PHONY: all
all:  build

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
	@echo "      flags:      " $(CFLAGS)
	@echo
	@echo "  LINKER:"
	@echo "      command:    " $(LD)
	@echo "      flags:      " $(LDFLAGS)
	@echo


.PHONY: build
build: target/benchmarks

archive: $(addprefix target/obj/,$(OBJECTS)) target/libbench.a

target/libbench.a: $(addprefix target/obj/,$(OBJECTS))
	$(AR) -rcs $@ $^

.PHONY: test
test: archive | target/obj target/bin
	(make -C $(ROOT)/src/test OBJECT_DIR=$(ROOT)/target/obj TARGET_DIR=$(ROOT)/target/bin \
		CC=$(CC) CFLAGS="$(CFLAGS)" INCLUDES="$(INCLUDES)" \
		LIBS="$(ROOT)/target/libbench.a $(CLIENTREPO)/target/$(PLATFORM)/lib/libaerospike.a" \
		LDFLAGS="$(LDFLAGS)" CLIENTREPO="$(CLIENTREPO)")

.PHONY: clean
clean:
	@rm -rf target test_target

target:
	mkdir $@

target/obj: | target
	mkdir $@

target/bin: | target
	mkdir $@

target/obj/%.o: src/main/%.c | target/obj
	$(CC) $(CFLAGS) -o $@ -c $^ $(INCLUDES)

target/benchmarks: $(addprefix target/obj/,$(MAIN_OBJECT)) $(addprefix target/obj/,$(OBJECTS)) $(CLIENTREPO)/target/$(PLATFORM)/lib/libaerospike.a | target
	$(CC) -o $@ $^ $(LDFLAGS)


.PHONY: run
run: build
	./target/benchmarks -h $(AS_HOST) -p $(AS_PORT)

.PHONY: valgrind
valgrind: build
	valgrind --tool=memcheck --leak-check=yes --show-reachable=yes --num-callers=20 --track-fds=yes -v ./target/benchmarks

.PHONY: test
test: build_test
	./test_target/test

.PHONY: build_test
build_test: test_target/test

test_target: 
	mkdir $@

test_target/obj: | test_target
	mkdir $@

test_target/obj/%.o: src/test/%.c | test_target/obj
	$(CC) $(CFLAGS) -o $@ -c $^

test_target/test: $(addprefix test_target/obj/,$(TEST_OBJECTS)) $(addprefix target/obj/,$(OBJECTS)) $(CLIENTREPO)/target/$(PLATFORM)/lib/libaerospike.a | test_target
	$(CC) -o $@ $^ $(TEST_LDFLAGS)
