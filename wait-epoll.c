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
#include <sys/epoll.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "debug.h"
#include "wait-interface.h"
#include "connection-info.h"

static unsigned int pending_queries = 0;
static int epoll_fd = -1;

void
init_wait(int max_pending)
{
	epoll_fd = epoll_create(max_pending);
	if (epoll_fd == -1) {
		fprintf(stderr, "Cannot create poll fd: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

void
wait_for_action(void)
{
	debug("polling for %d fds\n", pending_queries);
	rt_assert(pending_queries > 0);
	
	struct epoll_event *events = alloca(pending_queries * sizeof events[0]);
	int num_fds = epoll_wait(epoll_fd, events, pending_queries, -1);
	if (num_fds == -1) {
		if (errno == EINTR) {
			fprintf(stderr, "epoll_wait was interrupted by a signal.\n");
			return;
		}
		fprintf(stderr, "epoll_wait error: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	debug("%d fds ready for something\n", num_fds);
	
	int n;
	for (n = 0; n < num_fds; n++) {
		int fd = events[n].data.fd;
		connection_info[fd].handler(fd);
	}
}


void
wait_for_connected(struct conn_info *conn)
{
	struct epoll_event ev;

	ev.events = EPOLLOUT;
	ev.data.fd = conn->fd;

	int err = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn->fd, &ev);
	if (err == -1) {
		fprintf(stderr, "epoll_ctl failure in wait_for_connected: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	pending_queries++;
}

void
unregister_wait(int fd)
{
	struct epoll_event dummy;
	int err = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &dummy) == -1;
	if (err == -1) {
		fprintf(stderr, "unregister wait: epoll_ctl(%d, EPOLL_CTL_DEL): %s\n",
			fd, strerror(errno));
		exit(EXIT_FAILURE);
	}
	pending_queries--;
}

void
wait_for_read(struct conn_info *conn)
{
	int fd = conn->fd;

	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = fd;

	int err = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
	if (err == -1) {
		fprintf(stderr, "wait_for_read: epoll_ctl(%d, EPOLL_CTL_MOD): %s\n",
			fd, strerror(errno));
		exit(EXIT_FAILURE);
	}
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
