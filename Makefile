.PHONY: clean
CFLAGS=-O2 -Wall -Wpedantic -Wextra -pthread -gdwarf-4
OBJ=brute.o iter.o rec.o common.o main.o multi.o queue.o single.o gen.o semaphore.o
TARGET=main
LIBS+=crypt/libcrypt.a
CFLAGS+=-I./crypt
 
ifeq ($(shell uname), Linux)
	TESTS=test/*.py
endif

ifeq ($(shell uname -s), Darwin)
	# No valgrind on MacOS
	TESTS=test/performance-test.py test/test.py
endif

all: ${TARGET}

${TARGET}: ${OBJ} ${LIBS}
	@${CC} ${CFLAGS} ${OBJ} ${LIBS} -o ${TARGET}

crypt/libcrypt.a: 
	@${MAKE} -C crypt

clean:
	@${RM} main *.o
	@${MAKE} -C crypt clean

check:
	@pytest ${TESTS}