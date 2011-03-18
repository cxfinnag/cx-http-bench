#ifndef WAIT_POLL_H
#define WAIT_POLL_H

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

/*
 * This is the generic wait interface which needs to be implemented by
 * all the architecture specific poll-like implementations.
 */

struct conn_info;

void init_wait(int max_pending);
void wait_for_action(void);
void unregister_wait(int fd);
unsigned int wait_num_pending(void);
void wait_for_connected(struct conn_info *conn);
void wait_for_read(struct conn_info *conn);

#endif /* !WAIT_POLL_H  */

/* Local Variables: */
/* c-basic-offset:8 */
/* indent-tabs-mode:t */
/* End:  */
