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
#include <time.h>

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
wait_for_action(struct expdecay *qps, double timeout)
{
	debug("polling for %d fds\n", pending_queries);
	
	if (!pending_queries) {
		struct timespec ts;
		ts.tv_sec = (time_t)timeout;
		timeout -= ts.tv_sec;
		ts.tv_nsec = 1e9 * timeout;
		nanosleep(&ts, NULL);
		return;
	}

	struct epoll_event *events = alloca(pending_queries * sizeof events[0]);
	int num_fds = epoll_wait(epoll_fd, events, pending_queries, 1e3 * timeout);
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
		struct conn_info *conn = events[n].data.ptr;
		conn->handler(qps, conn);
	}
}


void
wait_for_connected(struct conn_info *conn)
{
	struct epoll_event ev;

	ev.events = EPOLLOUT;
	ev.data.ptr = conn;

	int err = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn->fd, &ev);
	if (err == -1) {
		fprintf(stderr, "wait_for_connected: epoll_ctl(%d, ADD, %d, ..): %s\n",
			epoll_fd, conn->fd, strerror(errno));
		exit(EXIT_FAILURE);
	}
	pending_queries++;
}

void
unregister_wait(struct conn_info *conn)
{
	/* unregister_wait() is always followed by a close(), so we do not have to remove the fd
	   from the epoll set, the OS does so automatically for us */
	(void)conn;
	pending_queries--;
}

void
wait_for_read(struct conn_info *conn)
{
	int fd = conn->fd;

	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.ptr = conn;

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
