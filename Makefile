.PHONY: clean
CFLAGS=-O2 -Wall -Wpedantic -Wextra -pthread -ggdb3 -I./crypt
OBJ=brute.o iter.o rec.o common.o main.o multi.o queue.o single.o
TARGET=main
LIBS+=crypt/libcrypt.a

ifeq ($(shell uname -s), Darwin)
	OBJ+=semaphore.o
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
	@${CC} test/encrypt.c -o test/encrypt -I./crypt crypt/libcrypt.a
	@test/test1.sh
	@test/test2.sh
	@test/test3.sh
