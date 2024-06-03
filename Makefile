.PHONY: clean
CFLAGS=-O2 -Wall -Wextra -pthread -gdwarf-4 -DLOG_LEVEL=TRACE
OBJ=brute.o iter.o rec.o common.o main.o multi.o queue.o single.o gen.o semaphore.o async_client.o client_common.o sync_client.o async_server.o sync_server.o server_common.o thread_pool.o log.o
TARGET=brute
LIBS+=crypt/libcrypt.a
CFLAGS+=-I./crypt

TESTS=test/simple-test.py test/client-server-test.py
WITH_PERF_TEST ?= false

ifeq ($(shell uname), Linux)
	# No valgrind on MacOS
	TESTS+=test/valgrind-test.py
endif

ifeq (${WITH_PERF_TEST}, true)
	TESTS+=test/performance-test.py
endif

all: ${TARGET}

# TODO: debug, release and dev recipes with differnet log levels

${TARGET}: ${OBJ} ${LIBS}
	@${CC} ${CFLAGS} ${OBJ} ${LIBS} -o ${TARGET}

crypt/libcrypt.a:
	@${MAKE} -C crypt

clean:
	@${RM} ${TARGET} *.o
	@${MAKE} -C crypt clean

check: all
	@pytest --hypothesis-show-statistics ${TESTS}
