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

#warning TODO: ADD regex support for parsing results?

#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

#include "dynbuf.h"
#include "debug.h"
#include "wait-interface.h"
#include "connection-info.h"
#include "timeutil.h"
#include "expdecay.h"

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

typedef const char *(*query_function)(void);
query_function select_query_function(void);
static const char *next_random_query(void);
static const char *next_loop_query(void);
static const char *next_query_noloop(void);

static int handle_connected(struct expdecay *, struct conn_info *);
static int handle_readable(struct expdecay *, struct conn_info *);

static int parse_http_result_code(const char *buf, size_t len);
static char *find_char_or_end(const char *buf, char needle, const char *end);

static void signal_handler(int signal);
static int sig_permanent(int sig, void (*handler)(int));
static void report_progress(struct expdecay *query_stats);
static void report_pending(void);

typedef double (*waiter_fn)(double);

static double poisson_wait(double interval);
static double regular_wait(double interval);
static waiter_fn waiter = poisson_wait;

static volatile unsigned int stop_now = 0;
static int loop_mode = 0;
static int random_mode = 0;
static int use_post = 0;
static unsigned int num_parallell = 1;
static const char *query_prefix = "";
static const char *header = "Dummy: dummy";
static const char *output_filename = "cxbench.out";
static const char *error_filename = "cxbench.errors";
static FILE *querylog_file;
static FILE *error_file;
static unsigned long queries_sent = 0;
static unsigned long max_queries = 0;
static double query_interval = 0;
static double time_of_next_query = 0;

int
main(int argc, char **argv)
{
	parse_arguments(argc, argv);
	querylog_file = fopen(output_filename, "a");
	if (!querylog_file) {
		fprintf(stderr, "Cannot open '%s' for appending: %s\n", output_filename,
			strerror(errno));
		exit(EXIT_FAILURE);
	}
	error_file = fopen(error_filename, "a");
	if (!error_file) {
		fprintf(stderr, "Cannot open '%s' for appending: %s\n", error_filename,
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	sig_permanent(SIGINT, signal_handler);
	srand48(time(0) + getpid() * 131);

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
			fprintf(stderr, "test_connection: socket(%d, %d, %d): %s\n", ai->ai_family,
				ai->ai_protocol, SOCK_STREAM, strerror(errno));
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
		{ "help", no_argument, NULL, 'h' },
		{ "debug", no_argument, NULL, 'd' },
		{ "loop", no_argument, NULL, 'l' },
		{ "randomize", no_argument, NULL, 'r' },
		{ "errors", required_argument, NULL, 'e' },
		{ "output", required_argument, NULL, 'o' },
		{ "parallell", required_argument, NULL, 'p' },
		{ "query-prefix", required_argument, NULL, 'q' },
		{ "use-post", no_argument, NULL, 'P' },
		{ "header", required_argument, NULL, 'H' },
		{ "qps", required_argument, NULL, 's' },
		{ "num-queries", required_argument, NULL, 'n' },
		{ "wait-mode", required_argument, NULL, 'w' },
		{ NULL, 0, NULL, 0 }
	};

	int ch;
	while ((ch = getopt_long(argc, argv, "hdlrp:q:PH:e:o:s:n:w:", opts, NULL)) != -1) {
		switch (ch) {
		case 'h':
			usage(argv[0]);
			exit(EXIT_SUCCESS);
			break;
		case 'd':
			increase_debugging();
			break;
		case 'l':
			loop_mode = 1;
			break;
		case 'e':
			error_filename = strdup(optarg);
			break;
		case 'o':
			output_filename = strdup(optarg);
			break;
		case 'r':
			random_mode = 1;
			break;
		case 's':
			{
				char *end;
				double t = strtod(optarg, &end);
				if (*end) {
					fprintf(stderr, "Invalid qps '%s'\n", optarg);
					exit(EXIT_FAILURE);
				}
				if (t < 0) {
					fprintf(stderr, "qps must be >= 0\n");
					exit(EXIT_FAILURE);
				} else if (t > 1e-10) {
					query_interval = 1.0 / t;
				} else {
					query_interval = 0;
				}
				debug("query interval: %.3f\n", query_interval);
			}
			break;
		case 'n':
			{
				char *end;
				max_queries = strtoul(optarg, &end, 10);
				if (*end) {
					fprintf(stderr, "Invalid num-queries '%s'\n", optarg);
					exit(EXIT_FAILURE);
				}
				if (max_queries == 0) {
					fprintf(stderr, "num-queries must be > 0\n");
					exit(EXIT_FAILURE);
				}
			}
			break;
		case 'p':
			num_parallell = atoi(optarg);
			if (num_parallell <= 0) {
				fprintf(stderr, "Cannot run less than 1 query in parallell!\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'q':
			query_prefix = strdup(optarg);
			break;
		case 'P':
			use_post = 1;
			break;
		case 'H':
			header = strdup(optarg);
			break;
		case 'w':
			if (strcasecmp(optarg, "poisson") == 0) {
				waiter = poisson_wait;
			} else if (strcasecmp(optarg, "regular") == 0) {
				waiter = regular_wait;
			} else {
				fprintf(stderr, "Unknown waiter '%s'\n", optarg);
				exit(EXIT_FAILURE);
			}
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
		int error = lookup_addrinfo(ai, host, sizeof host, service, sizeof service);
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

static size_t num_queries = 0;
struct dynbuf queries;
static char **query_list = 0;

enum { MAX_FD_HEADROOM = 20 };

static void
run_benchmark(const char *hostname, const struct addrinfo *target)
{
	query_function get_next_query = select_query_function();
	connection_info = calloc(num_parallell + MAX_FD_HEADROOM, sizeof connection_info[0]);
	init_wait(num_parallell);
	struct expdecay query_stats;

	expdecay_init(&query_stats);
	read_queries();
	double next_report = now() + 1;
	time_of_next_query = now(); /*  + waiter(query_interval); */
	while (wait_num_pending() || !stop_now) {
		double timestamp = now();
		debug("Time until next query: %.3fms\n", (time_of_next_query - timestamp) * 1e3);
		while (!stop_now && wait_num_pending() < num_parallell
		       && timestamp >= time_of_next_query) {
			const char *query = get_next_query();
			if (!query) {
				num_parallell = 0;
				fprintf(stderr, "Finished sending queries\n");
				stop_now = 1;
				goto next;
			}
			initiate_query(hostname, target, query);
			time_of_next_query += waiter(query_interval);
			debug("time_of_next_query = %.3f\n", time_of_next_query);
			queries_sent++;
			if (max_queries && queries_sent >= max_queries) {
				stop_now = 1;
			}
		}
		double delta = next_report - timestamp;
		if (wait_num_pending() < num_parallell && time_of_next_query < next_report) {
			debug("next report in %.3fms, but next query in %.3fms\n",
			      delta * 1e3, 1e3 * (time_of_next_query - timestamp));
			delta = time_of_next_query - timestamp;
		}
 		wait_for_action(&query_stats, stop_now ? 1000 : delta > 0 ? delta : 0);
	next:
		if (stop_now) {
			report_pending();
		} else if (now() >= next_report) {
			report_progress(&query_stats);
			next_report += 1;
		}
	}
}

static void
report_progress(struct expdecay *query_stats)
{
	printf("q: %10lu q/s: %9.7g  \r", queries_sent, expdecay_value(query_stats));
	fflush(stdout);
}

static void
report_pending(void)
{
	printf("STOPPING. Pending queries: %d                      \r", wait_num_pending());
	fflush(stdout);
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
	conn->fd = fd;
	conn->status = CONN_CONNECTING;
	conn->query = query;
	conn->target = target;
	conn->hostname = hostname;
	conn->pending_index = wait_num_pending();
	conn->handler = handle_connected;
	dynbuf_init(&conn->data);

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
	wait_for_connected(conn);
}


#define SWAP(a, b)				\
do {						\
	__typeof(a) tmp = (a);			\
	(a) = (b);				\
	(b) = tmp;				\
} while (0)

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
randomize_query_list(void)
{
	size_t n;
	for (n = 0; n < num_queries - 1; n++) {
		size_t idx = n + drand48() * (num_queries - n);
		SWAP(query_list[n], query_list[idx]);
	}
}

static const char *
next_random_query(void)
{
	unsigned int idx = drand48() * num_queries;
	return query_list[idx];
}

static const char *
next_loop_query(void)
{
	static size_t idx;
	if (idx >= num_queries)
		idx = 0;
	return query_list[idx++];
}

static const char *
next_query_noloop(void)
{
	static size_t idx;
	if (idx >= num_queries)
		return NULL;
	return query_list[idx++];
}

static double
poisson_wait(double interval)
{
	/* Return the number of time units to wait for the next event in a Poisson process
	   where the average waiting time is 1 */
	return interval * -log(1.0 - drand48()); /* 1.0 - drand48() guaranteed > 0 */
}

static double
regular_wait(double interval)
{
	return interval;
}

void
read_queries(void)
{
	/* Read all the queries from stdin into an array. */
	enum { BYTES_PER_READ = 16384 };

	while (1) {
		dynbuf_ensure_space(&queries, BYTES_PER_READ);
		ssize_t l = read(0, queries.buffer + queries.pos, BYTES_PER_READ);
		if (l == -1) {
			fprintf(stderr, "Read error on stdin: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		if (l == 0)
			break;
		queries.pos += l;
	}
	dynbuf_ensure_space(&queries, 1);
	queries.buffer[queries.pos++] = 0;
	dynbuf_shrink(&queries);
	queries.pos--; /* Do not count terminating NUL */

	fprintf(stderr, "Read %llu bytes of queries\n", (unsigned long long)queries.pos);

	char *s = queries.buffer;
	char *end = &queries.buffer[queries.pos - 1];
	num_queries = 1;
	/* Treat last char as newline regardless */
	while ((s = find_char_or_end(s, '\n', end)) != end) {
		num_queries++;
		s++;
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
		s = find_char_or_end(s, '\n', &queries.buffer[queries.pos]);
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
	size_t would_write;
	if (use_post) {
		would_write = snprintf(buf, buf_len,
				      "POST %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nContent-Length: %zu\r\n%s\r\n\r\n%s",
				      query_prefix, host, strlen(query), header, query);
	} else {
		would_write = snprintf(buf, buf_len,
				      "GET %s%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n%s\r\n\r\n",
				      query_prefix, query, host, header);
	}
	return MIN(buf_len - 1, would_write);
}


static int
handle_connected(struct expdecay *query_stats, struct conn_info *conn)
{
	(void)query_stats;
	int fd = conn->fd;
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
		dynbuf_free(&conn->data);
		unregister_wait(conn);
		close(fd);
		errno = saved_errno;
		return -1;
	}
	if ((size_t)written != len) {
		/* @@@ TODO eventually we should handle this to support queries that are longer
		   than the socket buffer */
		fprintf(stderr, "Short write to fd %d: %llu/%llu, aborting query\n", fd,
			(unsigned long long)written, (unsigned long long)len);
		unregister_wait(conn);
		close(fd);
		errno = EWOULDBLOCK;
		return -1;
	}
	buffer[len] = 0;
	debug("Wrote to fd %d %d bytes: '%s'\n", fd, (int)written,
		buffer);

	conn->handler = handle_readable;
	wait_for_read(conn);
	debug("pending_list[%d].events = POLLIN\n", conn->pending_index);
	return 0;
}

static int
handle_readable(struct expdecay *query_stats, struct conn_info *conn)
{
	int fd = conn->fd;
	debug("fd %d is now readable\n", fd);

	if (!conn->first_result_time)
		conn->first_result_time = now();

	int len;
	enum {
		BYTES_PER_NETWORK_READ = 4032,
		INITIAL_DYNBUF_RESERVATION = 8128,
	};
	dynbuf_ensure_space(&conn->data, INITIAL_DYNBUF_RESERVATION);
	do {
		dynbuf_ensure_space(&conn->data, BYTES_PER_NETWORK_READ + 1);
		len = read(fd, conn->data.buffer + conn->data.pos, BYTES_PER_NETWORK_READ);
		if (len > 0) {
			conn->data.pos += len;
			debug("got %d bytes from fd %d\n", len, fd);
		}
	} while (len > 0);

	if (len == 0) {
		double timestamp = now();
		expdecay_update(query_stats, 1, timestamp);
		conn->finished_result_time = timestamp;
		conn->data.buffer[conn->data.pos] = 0; /* Zero terminate the result for str fns */
		debug("EOF on fd %d. Total length = %d\n", fd, (int)conn->data.pos);
		spam("Received data:\n%s\n", conn->data.buffer);
		int http_result_code = parse_http_result_code(conn->data.buffer, conn->data.pos);
		/* @@@ Parse the result more here, e.g. check that various regexes match
		   or similar? */
		fprintf(querylog_file,
			"%.6f RES=%d LEN=%d TC=%.1fms T1=%.1fms TF=%.1fms Q=\"%s\"\n",
			timestamp, http_result_code, (int)conn->data.pos,
			1e3 * (conn->connected_time - conn->connect_time),
			1e3 * (conn->first_result_time - conn->connect_time),
			1e3 * (conn->finished_result_time - conn->connect_time),
			conn->query);

		/* Log the complete query and result if there was an error */
		if (http_result_code < 200 || http_result_code > 299) {
			fprintf(error_file, "%.6f Q=\"%s\"\nERROR RESULT:\n%s\n",
				timestamp, conn->query, conn->data.buffer);
		}

	} else if (len == -1) {
		if (errno == EWOULDBLOCK) {
			debug("must wait for more data from fd %d\n", fd);
			return 1; /* must return and wait for more data */
		}
		if (errno == EINTR) {
			fprintf(stderr, "Read was interrupted.\n");
			return 1;
		}
		fprintf(stderr, "Read error on fd %d: %s\n", fd, strerror(errno));
	}

	dynbuf_free(&conn->data);
	unregister_wait(conn);
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
		line_len = find_char_or_end(buf, '\n', buf + line_len) - buf;
		snprintf(fmt, sizeof fmt, "Invalid HTTP response header: '%%.%d%%s'\n", line_len);
		fprintf(stderr, fmt, buf);
		return -1;
	}
}

static char *
find_char_or_end(const char *string, char needle, const char *end)
{
	char *s = memchr(string, needle, end - string);
	return s ? s : (char *)end;
}


static int
sig_permanent(int sig, void (*handler)(int))
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_flags = 0;
	sa.sa_handler = handler;
	return sigaction(sig, &sa, NULL);
}

static void
signal_handler(int sig)
{
	(void)sig;
	stop_now = 1;
}


static void
usage(const char *name)
{
	fprintf(stderr, "Usage: %s [OPTIONS] <host>:<port>\n\n"
		" -d --debugging : Increase debug level (-d -d for spam)\n"
		" -l --loop-mode : Run the same queries multple times\n"
		" -e --errors <file> : Log all failed queries to <file> [cxbench.errors]\n"
		" -o --output <file> : Write querylog to <file> [cxbench.out]\n"
		" -r --random-mode: Run the queries in random order\n"
		" -p --parallell <n>: Run <n> queries in parallell\n"
		" -s --qps <rate> : Submit queries with <rate> qps. 0 means infinite\n"
		" -n --num-queries <n>: Stop after <n> queries\n"
		" -q --query-prefix <prefix> : Prepend <prefix> to all queries\n"
		" -w --wait-mode <mode> : Wait mode poisson or regular [poisson]\n\n"
		"A list of queries must be given on STDIN.\n\n", name);
}

/* Local Variables: */
/* c-basic-offset:8 */
/* indent-tabs-mode:t */
/* End:  */
