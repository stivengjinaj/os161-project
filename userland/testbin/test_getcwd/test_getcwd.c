#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <limits.h>

/*
 * pwd - print working directory.
 * Usage: pwd
 *
 * Just uses the getcwd library call (which in turn uses the __getcwd
 * system call.)
 */

int
main(void)
{
	char buf[PATH_MAX+1], *p;
	p = getcwd(buf, sizeof(buf));
	if (p == NULL) {
		err(1, ".");
	}
	printf("getcwd Test Summary:\n");
    printf("P: %c\n", *p);
	printf("CWD: %s\n", buf);
	return 0;
}
