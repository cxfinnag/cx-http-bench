
#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <getopt.h>

static void usage(const char *name);

int
main(int argc, char **argv)
{
	int loop_mode = 0;
	int random_mode = 0;
	int num_parallell = 1;

	int num_parallel;
	
	/* input: stdin? */
	/* server:port */
	/* options:
	   -l --loop [reuse stdin]
	   -r --randomize [queries in random order]
	   -p --parallell [run p queries in parallell]
	*/

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

	printf("options parsed, good to GO!\n");
	exit(EXIT_SUCCESS);
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
