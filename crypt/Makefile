TARGET=libcrypt.a
CFLAGS=-I./
OBJ=crypt_blowfish.o crypt_des.o crypt_md5.o crypt_r.o crypt_sha256.o crypt_sha512.o crypt.o encrypt.o

all: ${TARGET}
${TARGET}: ${OBJ}
	@ar -rcs ${TARGET} ${OBJ}

clean:
	@${RM} -rf *.o *.a