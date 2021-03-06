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
#include <sys/event.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "debug.h"
#include "wait-interface.h"
#include "connection-info.h"

static unsigned int pending_queries = 0;
static int kqueue_fd = -1;

void
init_wait(int max_pending)
{
	(void)max_pending;
	kqueue_fd = kqueue();
	if (kqueue_fd == -1) {
		fprintf(stderr, "Cannot create kqueue fd: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

void
wait_for_action(struct expdecay *query_stats, double timeout)
{
	struct timespec ts;

	debug("polling for %d fds\n", pending_queries);
	
	ts.tv_sec = (int)timeout;
	timeout -= ts.tv_sec;
	ts.tv_nsec = 1e9 * timeout;

	if (pending_queries == 0) {
		nanosleep(&ts, NULL);
		return;
	}

	struct kevent *events = alloca(pending_queries * sizeof events[0]);
	int num_fds = kevent(kqueue_fd, NULL, 0, events, pending_queries, &ts);
	if (num_fds == -1) {
		if (errno == EINTR) {
			fprintf(stderr, "kevent was interrupted by a signal.\n");
			return;
		}
		fprintf(stderr, "kevent error: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	debug("%d fds ready for something\n", num_fds);
	
	int n;
	for (n = 0; n < num_fds; n++) {
		struct conn_info *conn = events[n].udata;
		conn->handler(query_stats, conn);
	}
}


void
wait_for_connected(struct conn_info *conn)
{
	struct kevent kev;
	int fd = conn->fd;

	EV_SET(&kev, fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT,  0, 0, conn);
	int err = kevent(kqueue_fd, &kev, 1, NULL, 0, NULL);
	if (err == -1) {
		fprintf(stderr, "wait_for_connected: kevent(%d, %d, EVFILT_WRITE, EV_ADD): %s\n",
			kqueue_fd, fd, strerror(errno));
		exit(EXIT_FAILURE);
	}
	pending_queries++;
}

void
unregister_wait(struct conn_info *conn)
{
	/* We do not have to do any kqueue magic, unregister_wait() is always followed
	   by a close() which will automagically removes the fd from the kqueue */
	(void)conn;
	pending_queries--;
}

void
wait_for_read(struct conn_info *conn)
{
	struct kevent kev;
	EV_SET(&kev, conn->fd, EVFILT_READ, EV_ADD,  0, 0, conn);
	int err = kevent(kqueue_fd, &kev, 1, NULL, 0, NULL);
	if (err == -1) {
		fprintf(stderr, "wait_for_read: kevent(%d, %d, EVFILT_READ, EV_ADD): %s\n",
			kqueue_fd, conn->fd, strerror(errno));
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
