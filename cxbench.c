/*
 * cxbench
 * 
 * A benchmarking tool for benchmarking HTTP services.
 *
 */

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

#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <sys/time.h>
#include <math.h>
#include <fcntl.h>
#include <poll.h>

#include "dynbuf.h"

static void usage(const char *name);
struct addrinfo *lookup_host(const char *address);
static void print_addresses(const struct addrinfo *ai);
static void parse_arguments(int argc, char **argv);
const struct addrinfo *select_address(const struct addrinfo *addr);
static int lookup_addrinfo(const struct addrinfo *, char *host, size_t hostlen, char *port, size_t portlen);
static void run_benchmark(const char *hostname, const struct addrinfo *addr);
static void read_queries(void);
static void randomize_query_list();
static void initiate_query(const char *hostname, const struct addrinfo *target, const char *query);
static void wait_for_action(void);
static void wait_for_connected(int fd);
static void unregister_wait(int fd);

static double now(void);

typedef const char *(*query_function)(void);
query_function select_query_function(void);
static const char *next_random_query(void);
static const char *next_loop_query(void);
static const char *next_query_noloop(void);

typedef int (*event_handler)(int);
static int handle_connected(int);
static int handle_readable(int);

static int parse_http_result_code(const char *buf, size_t len);

static int debugging = 0;

#define debug(format, args...)				\
do {							\
	if (debugging) {				\
		fprintf(stderr, format , ##args);	\
	}						\
} while(0)

#define rt_assert(x)							\
do {									\
	if (!(x)) {							\
		fprintf(stderr, "%s:%d %s ASSERT FAILURE %s - PAUSING\n", __FILE__, __LINE__, \
			__PRETTY_FUNCTION__, #x);			\
		while (1) {						\
			pause();					\
			sleep(1);					\
		}							\
	}								\
} while (0);

static int loop_mode = 0;
static int random_mode = 0;
static unsigned int num_parallell = 1;
static const char *query_prefix = "";

enum conn_info_status {
	CONN_UNUSED = 0, /* Should be 0 for easy memset cleaning of all statuses */
	CONN_CONNECTING,
	CONN_CONNECTED,
	CONN_WAITING_RESULT,
	CONN_MORE_RESULTS
};

static struct conn_info {
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

	struct dynbuf data;
} *connection_info;

static struct pollfd *pending_list;

int
main(int argc, char **argv)
{
	parse_arguments(argc, argv);
	
	argc -= optind;
	argv += optind;

	struct addrinfo *res = lookup_host(argv[0]);
	if (!res) {
		fprintf(stderr, "Host lookup failed.");
		exit(EXIT_FAILURE);
	}
	print_addresses(res);
	
	const struct addrinfo *target = select_address(res);
	if (!target) {
		fprintf(stderr, "Cannot connect to benchmark server, aborting.\n");
		exit(EXIT_FAILURE);
	}

	run_benchmark(argv[0], target);
	
	freeaddrinfo(res);
	
	exit(EXIT_SUCCESS);
}

const struct addrinfo *
select_address(const struct addrinfo *ai)
{
	/* Go through all the addresses in ai, ai->next... and find one we can connect to.
	   Return the first one that answers, or NULL if connection fails to all of them. */
	for (; ai; ai = ai->ai_next) {
		int fd = socket(ai->ai_family, SOCK_STREAM, ai->ai_protocol);
		if (fd == -1) {
			fprintf(stderr, "test_connection: socket(%d, %d, %d): %s\n",
				ai->ai_family, ai->ai_protocol, SOCK_STREAM,
				strerror(errno));
			continue;
		}

		char host[NI_MAXHOST];
		char port[NI_MAXSERV];
		lookup_addrinfo(ai, host, sizeof host, port, sizeof port);
		fprintf(stderr, "Testing connection to %s:%s...\n", host, port);
	
		double t1 = now();
		int connect_status = connect(fd, ai->ai_addr, ai->ai_addrlen);
		int saved_errno = errno;
		close(fd);

		if (connect_status == 0) {
			double t2 = now();
			fprintf(stderr, "Connection OK in %.3fms.\n", 1e3 * (t2 - t1));
			return ai;
		}
			
		fprintf(stderr, "Failed to connect: %s\n", strerror(saved_errno));
	}
	return NULL;
}


static int
lookup_addrinfo(const struct addrinfo *ai, char *host, size_t hostlen, char *port, size_t portlen)
{
	
	int error = getnameinfo(ai->ai_addr, ai->ai_addrlen, host, hostlen,
				port, portlen, NI_NUMERICHOST | NI_NUMERICSERV);
	if (error) {
		strncpy(host, "(invalid)", hostlen);
		host[hostlen - 1] = 0;
		strncpy(port, "(invalid)", portlen);
		port[portlen - 1] = 0;
	}

	return error;
}

static void
parse_arguments(int argc, char **argv)
{
	static struct option opts[] = {
		{ "debug", no_argument, NULL, 'd' },
		{ "loop", no_argument, NULL, 'l' },
		{ "randomize", no_argument, NULL, 'r' },
		{ "parallell", required_argument, NULL, 'p' },
		{ "query-prefix", required_argument, NULL, 'q' },
		{ NULL, 0, NULL, 0 }
	};

	int ch;
	while ((ch = getopt_long(argc, argv, "dlrp:", opts, NULL)) != -1) {
		switch (ch) {
		case 'd':
			debugging++;
			break;
		case 'l':
			loop_mode = 1;
			break;
		case 'r':
			random_mode = 1;
			break;
		case 'p':
			num_parallell = atoi(optarg);
			if (num_parallell <= 0) {
				fprintf(stderr, "Cannot run less than 1 query "
					"in parallell!\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'q':
			query_prefix = strdup(optarg);
			break;
		default:
			usage(argv[0]);
			exit(EXIT_FAILURE);
			break;
		}
	}

	if (argc - optind != 1) {
		if (argc - optind < 1)
			fprintf(stderr, "Missing host:port argument!\n");
		else
			fprintf(stderr, "Too many arguments!\n");
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}
}

static void
print_addresses(const struct addrinfo *ai)
{
	char host[NI_MAXHOST];
	char service[NI_MAXSERV];

	for (; ai; ai = ai->ai_next) {
		int error = lookup_addrinfo(ai, host, sizeof host, service,
					    sizeof service);
		if (error) {
			fprintf(stderr, "numeric conv: %s\n", gai_strerror(error));
			continue;
		}
		fprintf(stderr, "Address: %s port %s\n", host, service);
	}
}


struct addrinfo *
lookup_host(const char *address)
{
	const char *colon = strchr(address, ':');
	if (!colon) {
		fprintf(stderr, "missing : in host/port name\n");
		return NULL;
	}

	size_t name_len = colon - address;
	char *name = alloca(name_len + 1);
	memcpy(name, address, name_len);
	name[name_len] = 0;

	size_t port_len = strlen(colon + 1);
	char *port = alloca(port_len + 1);
	memcpy(port, colon + 1, port_len);
	port[port_len] = 0;
	
	struct addrinfo hints;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP; /* matters for ports 512..520 or so */
	hints.ai_flags = AI_ADDRCONFIG; /* Only get ipv4/ipv6 if we have
					   configured interfaces for them */
	struct addrinfo *res;
	int error = getaddrinfo(name, port, &hints, &res);
	if (error) {
		fprintf(stderr, "Resolver error: %s\n", gai_strerror(error));
		return NULL;
	}
	return res;
}

static double now()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + 1e-6 * tv.tv_usec;
}

static size_t num_queries = 0;
struct dynbuf queries;
static char **query_list = 0;
static unsigned int pending_queries = 0;

enum { MAX_FD_HEADROOM = 20 };

static void
run_benchmark(const char *hostname, const struct addrinfo *target)
{
	query_function fn = select_query_function();
	connection_info = calloc(num_parallell + MAX_FD_HEADROOM, sizeof connection_info[0]);
	pending_list = calloc(num_parallell, sizeof(pending_list[0]));

	read_queries();
	while (pending_queries + num_parallell > 0) {
		unsigned int n;
		enum { MAX_CONNS_IN_ONE_SHOT = 3 }; /* Avoid going bananas with connections */
		for (n = 0; pending_queries < num_parallell && n < MAX_CONNS_IN_ONE_SHOT; n++) {
			const char *query = fn();
			if (!query) {
				num_parallell = 0;
				fprintf(stderr, "Finished sending queries\n");
				break;
			}
			initiate_query(hostname, target, query);
		}
 		wait_for_action();
	}
}

static void
initiate_query(const char *hostname, const struct addrinfo *target, const char *query)
{
	int fd = socket(target->ai_family, SOCK_STREAM, target->ai_protocol);
	if (fd == -1) {
		fprintf(stderr, "initiate_query: socket() fails: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	rt_assert((unsigned int)fd < num_parallell + MAX_FD_HEADROOM);

	/* Make sure socket is nonblocking, we don't want to wait! */
	if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) == -1) {
		fprintf(stderr, "initiate_query: fcntl fails: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	struct conn_info *conn = &connection_info[fd];
	conn->connect_time = now();
	conn->status = CONN_CONNECTING;
	conn->query = query;
	conn->target = target;
	conn->hostname = hostname;
	conn->pending_index = pending_queries;
	conn->handler = handle_connected;
	dynbuf_init(&connection_info[fd].data);

	int error = connect(fd, target->ai_addr, target->ai_addrlen);
	if (error == -1) {
		if (errno != EINPROGRESS) {
			fprintf(stderr, "connect fails immediately: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		} else {
			debug("connect on fd %d in progress\n", fd);
		}
	} else {
		debug("connect on fd %d connected immediately!\n", fd);
	}
	wait_for_connected(fd);
	pending_queries++;
}

static void
wait_for_connected(int fd)
{
	memset(&pending_list[pending_queries], 0, sizeof pending_list[pending_queries]);
	pending_list[pending_queries].fd = fd;
	pending_list[pending_queries].events = POLLOUT;
}

#define SWAP(a, b)				\
do {						\
	__typeof(a) tmp = (a);			\
	(a) = (b);				\
	(b) = tmp;				\
} while (0)

static void
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

static void
wait_for_action(void)
{
	debug("polling for %d fds\n", pending_queries);
	int num_fds = poll(pending_list, pending_queries, -1);
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

query_function
select_query_function(void)
{
 	if (loop_mode) {
		if (random_mode)
			return next_random_query;
		return next_loop_query;
	}
	if (random_mode)
		randomize_query_list();
	return next_query_noloop;
}



static void
randomize_query_list()
{
	size_t n;
	for (n = 0; n < num_queries - 1; n++) {
		size_t idx = n + drand48() * (num_queries - n);
		SWAP(query_list[n], query_list[idx]);
	}
}

static const char *
next_random_query()
{
	unsigned int idx = drand48() * num_queries;
	return query_list[idx];
}

static const char *
next_loop_query()
{
	static size_t idx;
	if (idx >= num_queries)
		idx = 0;
	return query_list[idx++];
}

static const char *
next_query_noloop()
{
	static size_t idx;
	if (idx >= num_queries)
		return NULL;
	return query_list[idx++];
}

static double
poisson_wait()
{
	/* Return the number of time units to wait for the next event in a Poisson process
	   with expected value 1 */
	double r = drand48();
	if (r < 1e-15) {
		/* Values should be 0, 35527e-15 * N, N = 1, 2, ... 2 ^ 48 */
		r = 1e-15; /* protect against zero value */
	}
	return -log(1.0 - r);
}

static void
read_queries()
{
	/* Read all the queries from stdin into an array. */
	enum { BYTES_PER_READ = 16384 };

	while (1) {
		dynbuf_set_reserve(&queries, BYTES_PER_READ);
		ssize_t l = read(0, queries.buffer + queries.pos, BYTES_PER_READ);
		if (l == -1) {
			fprintf(stderr, "Read error on stdin: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		if (l == 0)
			break;
		queries.pos += l;
	}
	dynbuf_set_reserve(&queries, 1);
	queries.buffer[queries.pos++] = 0;
	dynbuf_shrink(&queries);

	fprintf(stderr, "Read %llu bytes of queries\n", (unsigned long long)queries.pos);
	
	char *s;
	char *end = &queries.buffer[queries.pos];
	for (s = memchr(queries.buffer, '\n', end - queries.buffer);
	     s;
	     s = memchr(s + 1, '\n', end - (s + 1))) {
		num_queries++;
	}
	if (queries.buffer[queries.pos - 1] != '\n') {
		fprintf(stderr, "Last line did not have a newline, adding additional line.\n");
		++num_queries;
	}
	fprintf(stderr, " - found %llu queries\n", (unsigned long long)num_queries);
	query_list = malloc(num_queries * sizeof(query_list[0]));
	if (!query_list) {
		fprintf(stderr, "Failed to allocate memory for query list: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	size_t n;
	s = queries.buffer;
	for (n = 0; n < num_queries; n++) {
		query_list[n] = s;
		s = memchr(s, '\n', end - s);
		if (!s) {
			s = end;
		}
		*s = 0;
		s++;
	}
}

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

size_t
generate_query(char *buf, size_t buf_len, const char *host, const char *query)
{
	size_t would_write = snprintf(buf, buf_len,
				      "GET %s%s HTTP/1.1\r\n"
				      "Host: %s\r\n"
				      "Connection: close\r\n\r\n", query_prefix, query, host);
	return MIN(buf_len - 1, would_write);
}


static int
handle_connected(int fd)
{
	struct conn_info *conn = &connection_info[fd];
	debug("fd %d is now connected\n", fd);
	conn->status = CONN_CONNECTED;
	conn->connected_time = now();
	conn->first_result_time = 0;
	conn->finished_result_time = 0;
	
	char buffer[20000];
	size_t len = generate_query(buffer, sizeof buffer, conn->hostname, conn->query);

	ssize_t written = write(fd, buffer, len);
	int saved_errno = errno;
	if (written == -1) {
		fprintf(stderr, "Write to fd %d fails: %s\n", fd, strerror(errno));
		conn->status = CONN_UNUSED;
		close(fd);
		unregister_wait(fd);
		errno = saved_errno;
		return -1;
	}
	if ((size_t)written != len) {
		/* @@@ TODO eventually we should handle this to support queries that are longer
		   than the socket buffer */
		fprintf(stderr, "Short write to fd %d: %llu/%llu, aborting query\n",
			fd, (unsigned long long)written, (unsigned long long)len);
		close(fd);
		unregister_wait(fd);
		errno = EWOULDBLOCK;
		return -1;
	}
	buffer[len] = 0;
	debug("Wrote to fd %d %d bytes: 'Get %s...'\n", fd, (int)written,
		conn->query);
	
	conn->handler = handle_readable;
	pending_list[conn->pending_index].events = POLLIN;
	debug("pending_list[%d].events = POLLIN\n", conn->pending_index);
	return 0;
}

static int
handle_readable(int fd)
{
	struct conn_info *conn = &connection_info[fd];
	debug("fd %d is now readable\n", fd);
	
	if (!conn->first_result_time)
		conn->first_result_time = now();
	
	int len;
	enum { BYTES_PER_NETWORK_READ = 4000 };
	do {
		dynbuf_set_reserve(&conn->data, BYTES_PER_NETWORK_READ + 1);
		len = read(fd, conn->data.buffer + conn->data.pos, BYTES_PER_NETWORK_READ);
		if (len > 0) {
			conn->data.pos += len;
			debug("got %d bytes from fd %d\n", len, fd);
		}
	} while (len > 0);
	if (len == 0) {
		conn->finished_result_time = now();
		conn->data.buffer[conn->data.pos] = 0; /* Zero terminate the result for str fns */
		debug("EOF on fd %d. Total length = %d\n", fd, (int)conn->data.pos);
		int http_result_code = parse_http_result_code(conn->data.buffer, conn->data.pos);
		printf("%.6f RES=%d LEN=%d TC=%.1fms T1=%.1fms TF=%.1fms Q=\"%s\"\n",
		       now(), http_result_code, (int)conn->data.pos,
		       1e3 * (conn->connected_time - conn->connect_time),
		       1e3 * (conn->first_result_time - conn->connect_time),
		       1e3 * (conn->finished_result_time - conn->connect_time),
		       conn->query);
	} else if (len == -1) {
		if (errno == EWOULDBLOCK) {
			debug("must wait for more data from fd %d\n", fd);
			return 1; /* must return and wait for more data */
		}
		debug("Read error on fd %d: %s\n", fd, strerror(errno));
	}
	
	unregister_wait(fd);
	close(fd);
	return len;
}

static int
parse_http_result_code(const char *buf, size_t len)
{
	enum { RESULT_CODE_LEN = 3 };
	const int header_start_len = strlen("HTTP/1.1 ");
	char *end;

	if (len < header_start_len + RESULT_CODE_LEN)
		goto bad_header;
	if (memcmp(buf, "HTTP/1.1 ", header_start_len) != 0
	    && memcmp(buf, "HTTP/1.0 ", header_start_len) != 0)
		goto bad_header;
	
	unsigned long http_result = strtoul(buf + header_start_len, &end, 10);
	if (http_result < 100 || http_result > 999
	    || end != buf + header_start_len + RESULT_CODE_LEN)
		goto bad_header;

	return http_result;

 bad_header:
	{
		int line_len = MIN(100, len); /* Print max 100 bytes of the first line of result */
		char fmt[50];
		char *newline = memchr(buf, '\n', line_len);
		if (newline)
			line_len = newline - buf;
		snprintf(fmt, sizeof fmt, "Invalid HTTP response header: '%%.%d%%s'\n", line_len);
		fprintf(stderr, fmt, buf);
		return -1;
	}
}

static void
usage(const char *name)
{
	fprintf(stderr, "Usage: %s [OPTIONS] host:port\n\n"
		" -d --debugging : Enable debug spam\n"
		" -l --loop-mode : Run the same queries multple times\n"
		" -r --random-mode: Run the queries in random order\n"
		" -p --parallell <n>: Run <n> queries in parallell\n"
		" -q --query-prefix <prefix> : Prepend <prefix> to all queries\n\n"
		"A list of queries must be given on STDIN.\n\n", name);
}

/* Local Variables: */
/* c-basic-offset:8 */
/* indent-tabs-mode:t */
/* End:  */
