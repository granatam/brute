.PHONY: clean
CFLAGS=-O2 -Wall -Wpedantic -Wextra -pthread
LIBS=-lcrypt
OBJ=brute.o common.o main.o multi.o queue.o single.o
TARGET=main

all: ${TARGET}

${TARGET}: ${OBJ}
	${CC} ${CFLAGS} ${OBJ} ${LIBS} -o ${TARGET}

clean:
	${RM} main *.o
