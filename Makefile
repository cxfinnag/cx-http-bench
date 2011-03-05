

PROGS := cxbench

CC := cc
CFLAGS := -O2 -Wall -W

cxbench: cxbench.o dynbuf.o
	${CC} ${LDFLAGS} ${CFLAGS} -o $@ $+

dynbuf.o cxbench.o: dynbuf.h
