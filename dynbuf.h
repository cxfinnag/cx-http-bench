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

#endif /* !DYNBUF_H */

/* Local Variables: */
/* c-basic-offset:8 */
/* indent-tabs-mode:t */
/* End:  */
