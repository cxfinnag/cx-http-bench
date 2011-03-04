
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

static void usage(const char *name);
struct addrinfo *lookup_host(const char *address);
static void print_addresses(const struct addrinfo *ai);
static void parse_arguments(int argc, char **argv);
const struct addrinfo *select_address(const struct addrinfo *addr);
static int lookup_addrinfo(const struct addrinfo *, char *host, size_t hostlen, char *port, size_t portlen);
static void run_benchmark(const struct addrinfo *addr);
static void read_queries(void);

static double now();

static int loop_mode = 0;
static int random_mode = 0;
static int num_parallell = 1;
static const char *query_prefix = "";

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

	run_benchmark(target);
	
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
		printf("Testing connection to %s:%s...\n", host, port);
	
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
		strlcpy(host, "(invalid)", hostlen);
		strlcpy(port, "(invalid)", portlen);
	}

	return error;
}

static void
parse_arguments(int argc, char **argv)
{
	static struct option opts[] = {
		{ "loop", no_argument, NULL, 'l' },
		{ "randomize", no_argument, NULL, 'r' },
		{ "parallell", required_argument, NULL, 'p' },
		{ "query-prefix", required_argument, NULL, 'q' },
		{ NULL, 0, NULL, 0 }
	};

	int ch;
	while ((ch = getopt_long(argc, argv, "lrp:", opts, NULL)) != -1) {
		switch (ch) {
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
		printf("Address: %s port %s\n", host, service);
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

	size_t name_len = colon - address + 1;
	char *name = alloca(name_len);
	strlcpy(name, address, name_len);

	size_t port_len = strlen(colon);
	char *port = alloca(port_len);
	strlcpy(port, colon + 1, port_len);
	
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
static char *queries = 0;
static char **query_list = 0;

static void
run_benchmark(const struct addrinfo *target)
{
	read_queries();
}


enum {
	BYTES_PER_READ = 16384
};
static void
read_queries()
{
	size_t queries_alloc = 0;
	size_t queries_pos = 0;

	while (1) {
		if (BYTES_PER_READ + queries_pos >= queries_alloc) {
			/* >= to keep room for a potential added trailing 0 */
			queries_alloc += BYTES_PER_READ + queries_alloc;
			queries = realloc(queries, queries_alloc);
			if (!queries) {
				fprintf(stderr, "Failed to allocate %llu bytes: %s\n",
					(unsigned long long)queries_alloc, strerror(errno));
				exit(EXIT_FAILURE);
			}
		}
					 
		ssize_t l = read(0, queries + queries_pos, BYTES_PER_READ);
		if (l == -1) {
			fprintf(stderr, "Read error on stdin: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		if (l == 0)
			break;
		queries_pos += l;
	}
	realloc(queries, queries_pos + 1); /* Shrink the allocation when done reading */
	fprintf(stderr, "Read %llu bytes of queries\n", (unsigned long long)queries_pos);
	
	char *s;
	char *end = &queries[queries_pos];
	for (s = memchr(queries, '\n', end - queries); s; s = memchr(s + 1, '\n', end - (s + 1))) {
		num_queries++;
	}
	if (queries[queries_pos - 1] != '\n') {
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
	s = queries;
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
	return MIN((sizeof buf) - 1, would_write);
}


static void
usage(const char *name)
{
	fprintf(stderr, "Usage: %s [OPTIONS] host:port\n\n"
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
