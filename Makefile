# Copyright (c) 2011, Finn Arne Gangstad <finnag@cxense.com>
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
# 
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

PROGS := cxbench

CC := cc
CFLAGS := -O2 -Wall -W

# Select the best poller depending on the OS
POLLER := wait-poll.o

cxbench: cxbench.o dynbuf.o debug.o ${POLLER}
	${CC} ${LDFLAGS} ${CFLAGS} -o $@ $+

clean:
	git clean -fX

# Manal dependencies for now, this has got to stop soon.
dynbuf.o cxbench.o: dynbuf.h
debug.o cxbench.o: debug.h
${POLLER}: ${POLLER:.o=.h}
