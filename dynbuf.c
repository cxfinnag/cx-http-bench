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

/* Local Variables: */
/* c-basic-offset:8 */
/* indent-tabs-mode:t */
/* End:  */
