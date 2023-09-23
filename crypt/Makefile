TARGET=libcrypt.a
OBJ=crypt.o crypt_util.o

all: ${TARGET}
${TARGET}: ${OBJ}
	ar -rcs ${TARGET} ${OBJ}

clean:
	rm -rf *.o *.a