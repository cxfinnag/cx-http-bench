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

#include <sys/types.h>
#include <poll.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "debug.h"
#include "wait-interface.h"
#include "connection-info.h"

static struct pollfd *pending_list;
static unsigned int pending_queries = 0;

void
init_wait(int max_pending)
{
	pending_list = calloc(max_pending, sizeof(pending_list[0]));
}

void
wait_for_action(void)
{
	debug("polling for %d fds\n", pending_queries);
	int num_fds = poll(pending_list, pending_queries, -1);
	if (num_fds == -1) {
		if (errno == EINTR) {
			fprintf(stderr, "Poll was interrupted by a signal.\n");
			return;
		}
		fprintf(stderr, "Poll error: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	debug("%d fds ready for something\n", num_fds);

	unsigned int n;
	for (n = 0; num_fds && n < pending_queries; n++) {
		struct pollfd *p = &pending_list[n];
		if (p->revents) {
			int fd = p->fd;
			connection_info[fd].handler(fd);
			num_fds--;
		}
	}
}


void
wait_for_connected(struct conn_info *conn)
{
	memset(&pending_list[pending_queries], 0, sizeof pending_list[pending_queries]);
	pending_list[pending_queries].fd = conn->fd;
	pending_list[pending_queries].events = POLLOUT;
	pending_queries++;
}

#define SWAP(a, b)				\
do {						\
	__typeof(a) tmp = (a);			\
	(a) = (b);				\
	(b) = tmp;				\
} while (0)

void
unregister_wait(int fd)
{
	const struct conn_info *conn = &connection_info[fd];
	const unsigned int my_pending_index = conn->pending_index;

	rt_assert(my_pending_index < pending_queries);

	pending_queries--;
	/* The last slot in pending_list needs to be moved up to our slot if we are 
	   not the last one. */
	if (my_pending_index != pending_queries) {
		struct pollfd *pf = &pending_list[pending_queries];
		pending_list[my_pending_index] = *pf;
		struct conn_info *moved_conn = &connection_info[pf->fd];
		moved_conn->pending_index = my_pending_index;
	}
}

void
wait_for_read(struct conn_info *conn)
{
	pending_list[conn->pending_index].events = POLLIN;
}

unsigned int
wait_num_pending(void)
{
	return pending_queries;
}

/* Local Variables: */
/* c-basic-offset:8 */
/* indent-tabs-mode:t */
/* End:  */
