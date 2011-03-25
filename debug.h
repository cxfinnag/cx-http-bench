/*
 * Copyright (c) 2011, Finn Arne Gangstad <finnag@cxense.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>
#include <unistd.h>

extern int is_debugging;

void increase_debugging();

#define debug(format, args...)				\
do {							\
	if (is_debugging) {				\
		fprintf(stderr, format , ##args);	\
	}						\
} while(0)

#define spam(format, args...)				\
do {							\
	if (is_debugging > 1) {				\
		fprintf(stderr, format , ##args);	\
	}						\
} while(0)


#define rt_assert(x)							\
do {									\
	if (!(x)) {							\
		fprintf(stderr, "%s:%d %s ASSERT FAILURE %s - PAUSING\n", __FILE__, __LINE__, \
			__PRETTY_FUNCTION__, #x);			\
		while (1) {						\
			pause();					\
			sleep(1);					\
		}							\
	}								\
} while (0);

#endif /* !DEBUG_H */

/* Local Variables: */
/* c-basic-offset:8 */
/* indent-tabs-mode:t */
/* End:  */

