

PROGS := cxbench

CC := cc
CFLAGS := -O2 -Wall -W

cxbench: cxbench.o
	${CC} ${LDFLAGS} ${CFLAGS} -o $@ $+

