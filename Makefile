.PHONY: clean
CFLAGS=-O2 -Wall -Wpedantic -Wextra
OBJ=brute.o common.o main.o multi.o queue.o single.o
TARGET=main

all: ${TARGET}

${TARGET}: ${OBJ}

clean:
	${RM} main *.o
