#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <include/ibooter.h>

static FILE* dbg = NULL;

void debug(const char* format, ...)
{
	#ifdef DEBUG
	va_list vargs;
	va_start(vargs, format);
	vfprintf((dbg) ? dbg : stderr,format, vargs);
	va_end(vargs);
	#endif
}

bool is_IMG3(const char *file)
{
	bool ret;
	int fd;
	char begin_file[19], filetype[5], imgtype[5];

	memset(begin_file, 0, sizeof(begin_file));
	memset(filetype, 0, sizeof(filetype));
	memset(imgtype, 0, sizeof(imgtype));

	fd = open(file, O_RDONLY);
	if (fd == -1) {
		printf("Could not open %s : %s\n", file, strerror(errno));
		exit(1);
	}

	read(fd, begin_file, 18);
	strncpy(filetype, begin_file + 0, 4);

	if (!strcmp(filetype, "3gmI"))
		ret = true;
	else
		ret = false;

	close(fd);
	return ret;
}