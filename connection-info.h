#ifndef CONNECTION_INFO_H
#define CONNECTION_INFO_H

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

#include "dynbuf.h"

struct conn_info;

typedef int (*event_handler)(struct conn_info *);

enum conn_info_status {
	CONN_UNUSED = 0, /* Should be 0 for easy memset cleaning of all statuses */
	CONN_CONNECTING,
	CONN_CONNECTED,
	CONN_WAITING_RESULT,
	CONN_MORE_RESULTS
};

struct conn_info {
	double connect_time;
	double connected_time;
	double first_result_time;
	double finished_result_time;

	const char *query;
	const struct addrinfo *target;
	const char *hostname;
	event_handler handler;
	unsigned int pending_index;
	enum conn_info_status status;
	int fd;

	struct dynbuf data;
};

extern struct conn_info *connection_info;


#endif /* !CONNECTION_INFO_H */

/* Local Variables: */
/* c-basic-offset:8 */
/* indent-tabs-mode:t */
/* End:  */
