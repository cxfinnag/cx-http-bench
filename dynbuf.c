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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "dynbuf.h"

void
dynbuf_init(struct dynbuf *d)
{
	d->buffer = NULL;
	d->pos = 0;
	d->alloc = 0;
}

void
dynbuf_free(struct dynbuf *d)
{
	free(d->buffer);
	dynbuf_init(d);
}


#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

void
dynbuf_set_reserve(struct dynbuf *d, size_t n)
{
	if (d->pos + n > d->alloc) {
		size_t new_size = MAX(d->alloc * 2, d->pos + n);
		d->buffer = realloc(d->buffer, new_size);
		if (!d->buffer) {
			fprintf(stderr, "realloc failed: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		d->alloc = new_size;
	}
}

void
dynbuf_store(struct dynbuf *d, const char *buf, size_t n)
{
	dynbuf_set_reserve(d, n);
	memcpy(d->buffer, buf, n);
	d->pos += n;
}

void
dynbuf_shrink(struct dynbuf *d)
{
	char *s = realloc(d->buffer, d->pos);
	if (s) {
		d->buffer = s;
		d->alloc = d->pos;
	}
};


/* Local Variables: */
/* c-basic-offset:8 */
/* indent-tabs-mode:t */
/* End:  */
