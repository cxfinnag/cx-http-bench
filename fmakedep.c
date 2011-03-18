/*
 * Copyright (C) 2002 Finn Arne Gangstad.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the author be held liable for any damages
 * arising from the use of this software.
 * 
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 * 
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 * 
 */

/*
 * A fast (hopefully) makedep program
 * Written by Finn Arne Gangstad <finnag@pvv.org> 2002-02-08
 *
 * fmakedep takes the following options:
 * 
 * --no-sys-includes - ignore anything in /usr/include
 * --dep-header=<string> - put <string> first in the output
 *   (typically you want the name of the .o file there at least, and
 *    most likely the .d file generated too)
 * --disable-caching - Don't use .fmdc-prefixed cache files to speed things up.
 * 
 * It understands -I options, and will ignore all other options, so you can
 * pass it CFLAGS without problems.
 *
 *
 * Search rules:
 *
 * #include <x> searches:
 *   1. any dirs specified with -I
 *   2. Compiler specific dirs
 * 
 * #include "x" searches:
 *   1. The same dir the file is in
 *   2. Any dirs specified with -I
 *   3. compiler specific dirs (/usr/include + compiler internal)
 * 
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

struct incdir {
    struct incdir *next;
    const char *path;
    int is_system;
};

/* Files should be identical if they have the same device number and the
   same inode number. */
struct dependency {
    const char *name;
    dev_t dev;
    ino_t ino;
};

static unsigned int max_deps = 0;
static unsigned int num_deps = 0;

static struct dependency *deps;


static int verbosity = 0;

static struct incdir *sys_inc_list;
static struct incdir *inc_list;
static void append_list(struct incdir *list, struct incdir *entry);
  
static struct incdir *add_incdir(const char *s, int is_system);
static void find_deps(const char *filename);
static void check_line_for_deps(const char *line, FILE *cachef);
static void make_path_copy(const char *filename, char *path);
static void add_dependency(char *f, const struct stat *st);
static void simplify_path(char *f);
static void resolve_dependencies();

static int use_sys_includes = 1;
static int use_caching = 1;

int
main(int argc, char **argv)
{
    int n;
    unsigned int dhl = strlen("--dep-header=");
    const char *header = "";

    add_incdir(".", 0);
    for (n = 1; n < argc; ++n) {
	const char *s = argv[n];
	if (strcmp(s, "--no-sys-includes") == 0)
	    use_sys_includes = 0;
	else if (memcmp(s, "--dep-header=", dhl) == 0)
	    header = strdup(s + dhl);
	else if (strcmp(s, "--disable-caching") == 0)
	    use_caching = 0;
	else if (s[0] != '-')
	    goto done;
	else if (strcmp(s, "--") == 0) {
	    ++n;
	    goto done;
	} else if (s[0] == '-' && s[1] == 'I') {
	    const char *v = s[2] ? s + 2 : argv[++n];
	    add_incdir(v, 0);
	} else if (verbosity > 2) {
	    fprintf(stderr, "[debug] ignoring option '%s'\n", s);
	}
    }
 done:
    add_incdir("/usr/include", 1);
    sys_inc_list = inc_list->next;

    for (; n < argc; ++n) {
	struct stat st;
	if (stat(argv[n], &st) == 0) {
	    char *s = strdup(argv[n]);
	    unsigned int m;

	    printf("%s", header);

	    /* Don't free the last time - program is exiting anyway */
	    for (m = 0; m < num_deps; ++m) {
		free((char *)deps[m].name);
	    }

	    num_deps = 0;
	    add_dependency(s, &st);
	    resolve_dependencies();	    
	    free(s);
	    printf("\n");
	}
    }
    exit(EXIT_SUCCESS);
}


static void
resolve_dependencies()
{
    unsigned int n;
    for (n = 0; n < num_deps; ++n) {
	find_deps(deps[n].name); /* find_deps will add to the deps array */
    }
}


struct incdir *
add_incdir(const char *s, int is_system)
{
    struct incdir *id = malloc(sizeof(*id));
    id->is_system = is_system;
    id->next = NULL;
    if (strcmp(s, ".") == 0 || strcmp(s, "./") == 0)
	s = "";
    id->path = strdup(s);
    if (!inc_list) {
	inc_list = id;
    } else {
	append_list(inc_list, id);
    }
    if (verbosity > 2)
	fprintf(stderr, "[debug] Added include dir '%s'\n", id->path);
    return id;
}




/* Append item to singly linked list */
static void
append_list(struct incdir *list, struct incdir *entry)
{
    while (list->next) {
	list = list->next;
    }
    list->next = entry;
}


/* Find all #include lines in a memory buffer (ie a file in memory) */
static void
scan_buffer(const char *line, const char *end, FILE *cachef)
{
    for (;;) {
        while (line < end && *line != '\n' && isspace(*line))
            ++line;
        if (line == end)
            return;
	if (*line == '#')
	    check_line_for_deps(line, cachef);
        line = memchr(line, '\n', end - line);
        if (line == NULL)
          return;
        ++line;
    }    
}

static void
make_cache_name(char *cached, const char *filename)
{
    char *rslash = strrchr(filename, '/');
    if (rslash) {
	unsigned int len = rslash - filename + 1;
	memcpy(cached, filename, len);
	cached += len;
	filename += len;
    }
    memcpy(cached, ".fmdc.", 6);
    cached += 6; /* strlen(".fmdc."); */
    strcpy(cached, filename);
}


/* Find all dependencies in the file <filename> */
static void
find_deps(const char *filename)
{
    unsigned int x = strlen(filename);
    char *path = alloca(x + 1);
    char *cached = alloca(x + 7);
    char *cached_new  = alloca(x + 20);
    char *buf, *end;
    struct stat st;
    FILE *cachef = NULL;

    int fd = open(filename, O_RDONLY);
    
    if (fd == -1) {
	fprintf(stderr, "Failed to open '%s': %s\n", filename,
		strerror(errno));
	return;
    }

    fstat(fd, &st);

    if (use_caching) {
	struct stat stc;
	make_cache_name(cached, filename);
	if (stat(cached, &stc) == 0
	    && stc.st_mtime - st.st_mtime >= 0) {
	    /* Use the cached file instead */
	    close(fd);
	    fd = open(cached, O_RDONLY);
	    if (fd == -1) {
		fprintf(stderr, "Failed to open '%s': %s\n",
			cached, strerror(errno));
		return;
	    }
	    st = stc; /* copy stat info from cache file instead */
	} else {
	    /* Write a cache file */
	    snprintf(cached_new, x + 20, "%s.%x", cached, getpid());
	    cachef = fopen(cached_new ,"w");
	}
    }

    /* Ignore files that are too short to have an include stmt */
    if (st.st_size < (off_t)strlen("#include <>"))
	goto out_premalloc;

    buf = malloc(st.st_size + 1);
    /* Read the entire file in one go */
    if (read(fd, buf, st.st_size) != st.st_size) {
	fprintf(stderr, "Failed to read '%s': %s\n", filename,
		strerror(errno));
	goto out;
    }
    buf[st.st_size] = 0;
    end = &buf[st.st_size] - 1;
    
    make_path_copy(filename, path);
    inc_list->path = path;

    scan_buffer(buf, end, cachef);

 out:
    free(buf);
 out_premalloc:
    if (cachef) {
	fclose(cachef);
	rename(cached_new, cached);
    }
    close(fd);
}


static inline const char *
skip_spaces(const char *s) 
{
    if (*s == ' ')
	goto skip_em; /* really unlikely */
    if (*s == '\t')
	goto skip_em; /* really unlikely */

    return s;

skip_em:
    do {
	++s;
    } while (*s == ' ' || *s == '\t');
    return s;
}


/* Try to find the include file <start> (len bytes long) given the
   include paths in <list>. */
static void
add_include(const char *start, unsigned int len, struct incdir *list)
{
    char buf[4000];
    char *s = alloca(len + 1);
    memcpy(s, start, len);
    s[len] = 0;

    for (; list; list = list->next) {
	struct stat st;
	snprintf(buf, sizeof buf, "%s%s%s", list->path,
		  *list->path ? "/" : "", s);
	if (stat(buf, &st) == 0) {
	    if (use_sys_includes || !list->is_system) {
		add_dependency(buf, &st);
	    }
	    goto found;
	}
    }
    if (verbosity > 0)
	fprintf(stderr, "No includes found matching '%s'\n", s);
    
 found:
    ;
}

static void
extract_incname(const char *start, char endchar, struct incdir *list)
{
    const char *end = start;
    while (*end != endchar) {
	if (!*end)
	    return;
	++end;
    }
    add_include(start, end - start, list);
}
    

static void
check_line_for_deps(const char *line, FILE *cachef)
{
    const char *s = line;
    s = skip_spaces(++s);
    if (strncmp("include", s, 7) != 0)
	return;
    s += 7;
    if (*s != ' ' && *s != '\t')
	return;
    s = skip_spaces(s);

    if (cachef) {
	char *end = strchr(line, '\n');
	if (end) {
	    if (fwrite(line, end + 1 - line, 1, cachef) != 1) {
                fputs("Failed writing to cache file\n", stderr);
                exit(1);
            }
	}
    }

    if (*s == '"') {
	extract_incname(s + 1, '"', inc_list);
    } else if (*s == '<') {
	extract_incname(s + 1, '>', sys_inc_list);
    }
}

/* path must be a writeable buffer at least as long as filename.
   the pathname for <filename> will be written to <path>. */
static void
make_path_copy(const char *filename, char *path)
{
    const char *last_slash = strrchr(filename, '/');
    if (!last_slash) {
	*path = 0;
    } else {
	unsigned int len = last_slash - filename;
	memcpy(path, filename, len);
	path[len] = 0;
	if (len == 1 && path[0] == '.') {
	    path[0] = 0;
	}
    }
}


/* Add the dependency to the file with name <f>, which is already
   verified to exist. The stat-buffer <st> must contain the equivalent
   of stat(f, st). Dependencies to files that are already in the
   dependency list will be silently ignored. */
static void
add_dependency(char *f, const struct stat *st)
{
    unsigned int n;
    struct dependency *dep;
    simplify_path(f);
    
    for (n = 0; n < num_deps; ++n) {
	if (st->st_ino == deps[n].ino && st->st_dev == deps[n].dev)
	    return; /* This dependency already exists. */
    }
	
    ++num_deps;
    if (num_deps > max_deps) {
	max_deps = num_deps * 2;
	deps = realloc(deps, max_deps * sizeof(*deps));
    }
    dep = &deps[num_deps - 1];
    dep->name = strdup(f);
    dep->ino = st->st_ino;
    dep->dev = st->st_dev;
    
    printf(" %s", f); /* Print the newly discovered dependency on stdout */
}


/* Remove any xxx/../ components from the filename to avoid confusion.
   Works in-place on the given file name */
static void
simplify_path(char *f)
{
    char *s;
    while ((s = strstr(f + 1, "/../"))) {
	char *rslash;
	*s = 0; /* Limit the strrchr to before "/../" */
	rslash = strrchr(f, '/');
	*s = '/';
	if (!rslash)
	    rslash = f - 1; /* "fake" a slash just before the start */
	memmove(rslash + 1, s + 4, strlen(s + 4) + 1);
    }
}
