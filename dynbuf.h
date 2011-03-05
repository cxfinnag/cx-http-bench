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

#ifndef DYNBUF_H
#define DYNBUF_H

/* A dynamic buffer that can be read into and reinitialised. Will resize
 * to handle data coming in. */

#include <sys/types.h>

struct dynbuf {
	char *buffer;
	size_t pos;
	size_t alloc;
};

void dynbuf_init(struct dynbuf *);
void dynbuf_free(struct dynbuf *);
void dynbuf_set_reserve(struct dynbuf *, size_t n); /* Make room for n more bytes */
void dynbuf_store(struct dynbuf *, const char *, size_t n); /* Store <n> bytes into dynbuf */
void dynbuf_shrink(struct dynbuf *); /* Shrink the allocation to exactly fit what is in the dynbuf */

#endif /* !DYNBUF_H */

/* Local Variables: */
/* c-basic-offset:8 */
/* indent-tabs-mode:t */
/* End:  */
