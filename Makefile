.PHONY: clean
CFLAGS=-O2 -Wall -Wpedantic -Wextra -pthread -ggdb3
OBJ=brute.o common.o main.o multi.o queue.o single.o semaphore.o
TARGET=main

ifeq ($(shell uname), Linux)
	LIBS+=-lcrypt
endif

ifeq ($(shell uname -s), Darwin)
	LIBS+=crypt/libcrypt.a
	CFLAGS+=-I./crypt
endif

all: ${TARGET}

${TARGET}: ${OBJ}
	${CC} ${CFLAGS} ${OBJ} ${LIBS} -o ${TARGET}

clean:
	${RM} main *.o
