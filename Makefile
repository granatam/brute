.PHONY: clean
CFLAGS=-O2 -Wall -Wpedantic -Wextra -pthread -ggdb3
OBJ=brute.o iter.o rec.o common.o main.o multi.o queue.o single.o gen.o semaphore.o
TARGET=main
 
ifeq ($(shell uname), Linux)
	LIBS+=-lcrypt
	TESTS=test/*.py
endif

ifeq ($(shell uname -s), Darwin)
	LIBS+=crypt/libcrypt.a
	CFLAGS+=-I./crypt
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