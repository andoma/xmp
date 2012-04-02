#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "sound.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

static int fd;
static long size;
static int swap_endian;

struct sound_driver sound_file;

static int init(struct options *options)
{
	char **parm = options->driver_parm;
	char *buf;

	swap_endian = 0;

	parm_init(parm);
	chkparm1("endian",
		swap_endian = (is_big_endian() ^ strcmp(token, "big")));
	parm_end();

	if (options->out_file == NULL) {
		options->out_file = "out.raw";
	}

	if (strcmp(options->out_file, "-")) {
		fd = open(options->out_file, O_WRONLY | O_CREAT | O_TRUNC
							| O_BINARY, 0644);
		if (fd < 0)
			return -1;
	} else {
		fd = 1;
	}

	if (strcmp(options->out_file, "-")) {
		int len = strlen(sound_file.description) +
				strlen(options->out_file) + 8;
		if ((buf = malloc(len)) == NULL)
			return -1;
		snprintf(buf, len, "%s: %s", sound_file.description,
						options->out_file);
		sound_file.description = buf;
	} else {
		sound_file.description = strdup("stdout");
	}

	return 0;
}

static void play(void *b, int len)
{
	if (swap_endian) {
		convert_endian(b, len);
	}
	write(fd, b, len);
	size += len;
}

static void deinit()
{
	free(sound_file.description);
}

static void flush()
{
}

static void onpause()
{
}

static void onresume()
{
}

static char *help[] = {
	"big-endian", "Force big-endian 16-bit samples",
	"little-endian", "Force little-endian 16-bit samples",
	NULL
};

struct sound_driver sound_file = {
	"file",
	"Raw file writer",
	help,
	init,
	deinit,
	play,
	flush,
	onpause,
	onresume
};

