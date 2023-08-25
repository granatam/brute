.PHONY: clean
CFLAGS=-O2 -Wall -Wpedantic -Wextra

main: main.o

clean:
	${RM} main *.o
