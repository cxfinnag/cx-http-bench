
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

static void usage(const char *name);
struct addrinfo *lookup_host(const char *address);
static void print_addresses(const struct addrinfo *ai);
static void parse_arguments(int argc, char **argv);
static int test_connection(const struct addrinfo *addr);
static int lookup_addrinfo(const struct addrinfo *, char *host, size_t hostlen, char *port, size_t portlen);

static int loop_mode = 0;
static int random_mode = 0;
static int num_parallell = 1;

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
	
	if (test_connection(res) != 0) {
		fprintf(stderr, "Cannot connect to benchmark server, aborting.\n");
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(res);

	printf("options parsed, good to GO!\n");
	
	exit(EXIT_SUCCESS);
}

static int
test_connection(const struct addrinfo *addr)
{
	int fd = socket(addr->ai_family, SOCK_STREAM, addr->ai_protocol);
	if (fd == -1) {
		fprintf(stderr, "test_connection: socket(%d, %d, %d): %s\n",
			addr->ai_family, addr->ai_protocol, SOCK_STREAM,
			strerror(errno));
		return -1;
	}
	char host[NI_MAXHOST];
	char port[NI_MAXSERV];
	lookup_addrinfo(addr, host, sizeof host, port, sizeof port);
	printf("Testing connection to %s:%s...\n", host, port);
								 
	int error = connect(fd, addr->ai_addr, addr->ai_addrlen);
	if (error == -1) {
		fprintf(stderr, "Failed to connect: %s\n", strerror(errno));
		goto out;
	}
	fprintf(stderr, "Connection OK.\n");
 out:
	close(fd);
	return error;
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
		case '0':
			printf("uh huh how did we get here: gol = 0\n");
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

	char *name = alloca(colon - address + 1);
	char *port = alloca(strlen(colon));

	memcpy(name, address, colon - address);
	name[colon - address] = 0;
	strcpy(port, colon + 1);
	
	struct addrinfo hints;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP; /* probably doesn't matter */
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

static void
usage(const char *name)
{
	fprintf(stderr, "Usage: %s [OPTIONS] host:port\n\n"
		" -l --loop-mode : Run the same queries multple times\n"
		" -r --random-mode: Run the queries in random order\n"
		" -p --parallell <n>: Run <n> queries in parallell\n\n"
		"A list of queries must be given on STDIN.\n\n", name);
}

/* Local Variables: */
/* c-basic-offset:8 */
/* indent-tabs-mode:t */
/* End:  */
