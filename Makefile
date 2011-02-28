

PROGS := cxbench

CC := cc
CFLAGS := -O2 -mcpu=native -mtune=native -Wall -W

cxbench: cxbench.o
	${CC} ${LDFLAGS} ${CFLAGS} -o $@ $+

