.SUFFIXES:.c
.PHONY: all dev debug release check perf clean

OPT_LEVEL ?= -O2
CFLAGS ?= ${OPT_LEVEL} -Wall -Wextra ${DBG_FLAGS}
CFLAGS +=-pthread -I./crypt
LIBS+=-levent

CRYPT_LIB=crypt/libcrypt.a

SRC_DIR=src
OBJ_DIR=obj
DEP_DIR=${OBJ_DIR}/.deps

SRCS = $(wildcard ${SRC_DIR}/*.c)
SRCS_BASENAMES := $(notdir ${SRCS})
OBJS := $(addprefix ${OBJ_DIR}/,$(SRCS_BASENAMES:.c=.o))
DEPS := $(addprefix ${DEP_DIR}/,$(SRCS_BASENAMES:.c=.d))

TARGET=brute

TESTS=test/simple-test.py test/client-server-test.py
PERF_TESTS=test/performance-test.py

WITH_VALGRIND ?= false

ifeq (${WITH_VALGRIND}, true)
	# No valgrind on MacOS, also Valgrind tests cannot be run with ASan
	TESTS+=test/valgrind-test.py
endif

ifeq ($(shell uname -s), Darwin)
	BREW_EXISTS := $(shell command -v brew >/dev/null 2>&1 && echo 1)
	ifeq (${BREW_EXISTS}, 1)
		BREW_PREFIX := $(shell brew --prefix libevent)
		ifneq (${BREW_PREFIX},)
			CFLAGS += -I${BREW_PREFIX}/include
			LIBS += -L${BREW_PREFIX}/lib
		else
			$(warning Warning: brew found but libevent prefix is unavailable.)
		endif
	else
		$(warning Warning: macOS build configuration requires libevent installed from brew.)
	endif
endif

all: ${TARGET}

${OBJ_DIR} ${DEP_DIR}:
	mkdir -p $@

${TARGET}: ${OBJS} ${CRYPT_LIB}
	${CC} ${CFLAGS} -o $@ $^ ${LIBS}

-include ${DEPS}

${OBJ_DIR}/%.o: ${SRC_DIR}/%.c | ${OBJ_DIR} ${DEP_DIR}
	${CC} ${CFLAGS} -MT $@ -MMD -MP -MF ${DEP_DIR}/$*.d -c $< -o $@

dev: OPT_LEVEL := -O0
dev: DBG_FLAGS := -g3 -gdwarf-4
dev: CFLAGS += -DLOG_LEVEL=TRACE
dev: clean all

debug: OPT_LEVEL := -O1
debug: DBG_FLAGS := -g1 -gdwarf-4
debug: CFLAGS += -DLOG_LEVEL=DEBUG
debug: clean all

release: OPT_LEVEL := -O3
release: DBG_FLAGS := -DNDEBUG -g0
release: CFLAGS += -DLOG_LEVEL=ERROR
release: clean all

${CRYPT_LIB}:
	@${MAKE} -C crypt

check: all
	@pytest --hypothesis-show-statistics ${TESTS}

perf: all
	@pytest -rA ${PERF_TESTS}

clean:
	@${RM} ${TARGET}
	@${RM} -r ${OBJ_DIR}
	@${MAKE} -C crypt clean
