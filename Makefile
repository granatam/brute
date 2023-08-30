.PHONY: clean
CFLAGS=-O2 -Wall -Wpedantic -Wextra -pthread
OBJ=brute.o common.o main.o multi.o queue.o single.o
TARGET=main

ifeq ($(shell uname), Linux)
	LIBS+=-lcrypt
endif

all: ${TARGET}

${TARGET}: ${OBJ}
	${CC} ${CFLAGS} ${OBJ} ${LIBS} -o ${TARGET}

clean:
	${RM} main *.o
