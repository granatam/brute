.PHONY: all dev debug release check perf clean

CFLAGS ?= -O2 -Wall -Wextra -gdwarf-4
CFLAGS+=-pthread
CFLAGS+=-I./crypt
LIBS+=-levent

CRYPT_LIB=crypt/libcrypt.a

OBJ_DIR=obj
SRC_DIR=src

OBJ=$(addprefix ${OBJ_DIR}/,brute.o iter.o rec.o common.o main.o multi.o \
	queue.o single.o gen.o semaphore.o async_client.o client_common.o \
	sync_client.o async_server.o sync_server.o server_common.o \
	reactor_common.o reactor_client.o reactor_server.o thread_pool.o log.o)
TARGET=brute

TESTS=test/simple-test.py test/client-server-test.py
PERF_TESTS=test/performance-test.py

WITH_VALGRIND ?= false

ifeq (${WITH_VALGRIND}, true)
	# No valgrind on MacOS, also Valgrind tests cannot be run with ASan
	TESTS+=test/valgrind-test.py
endif

ifeq ($(shell uname -s), Darwin)
	BREW_EXISTS := $(shell command -v brew >/dev/null 2>&1 && echo 1)
ifdef BREW_EXISTS
	BREW_PREFIX := $(shell brew --prefix libevent)
	CFLAGS += -I$(BREW_PREFIX)/include
	LIBS += -L$(BREW_PREFIX)/lib
else
$(warning Warning: macOS build configuration requires brew and libevent installed from it. Install brew and libevent or set paths manually.)
endif
endif

all: ${TARGET}

${OBJ_DIR}:
	mkdir -p ${OBJ_DIR}

${TARGET}: ${OBJ} ${CRYPT_LIB}
	${CC} ${CFLAGS} -o ${TARGET} ${OBJ} ${CRYPT_LIB} ${LIBS}

${OBJ_DIR}/%.o: ${SRC_DIR}/%.c | ${OBJ_DIR}
	${CC} ${CFLAGS} -c $< -o $@

dev: CFLAGS += -DLOG_LEVEL=TRACE
dev: clean all

debug: CFLAGS += -DLOG_LEVEL=DEBUG
debug: clean all

release: CFLAGS += -DLOG_LEVEL=ERROR
release: clean all

${CRYPT_LIB}:
	@${MAKE} -C crypt

check: all
	@pytest --hypothesis-show-statistics ${TESTS}

perf: all
	@pytest -rA ${PERF_TESTS}

clean:
	@${RM} ${TARGET}
	@${RM} -r ${OBJ_DIR}
	@${MAKE} -C crypt clean
