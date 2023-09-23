.PHONY: clean
CFLAGS=-O2 -Wall -Wpedantic -Wextra -pthread -ggdb3
OBJ=brute.o common.o main.o multi.o queue.o single.o
TARGET=main

ifeq ($(shell uname), Linux)
	LIBS+=-lcrypt
endif

ifeq ($(shell uname -s), Darwin)
	LIBS+=crypt/libcrypt.a
	CFLAGS+=-I./crypt
	OBJ+=semaphore.o
endif

all: ${TARGET}

${TARGET}: ${OBJ} ${LIBS}
	${CC} ${CFLAGS} ${OBJ} ${LIBS} -o ${TARGET}

crypt/libcrypt.a: 
	${MAKE} -C crypt

clean:
	${RM} main *.o
	${MAKE} -C crypt clean

