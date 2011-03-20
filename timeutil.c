#include <sys/time.h>

#include <stdio.h> /* for NULL of all things */

#include "timeutil.h"

double now(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + 1e-6 * tv.tv_usec;
}
