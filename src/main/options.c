/* Extended Module Player
 * Copyright (C) 1996-2012 Claudio Matsuoka and Hipolito Carraro Jr
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU General Public License. See doc/COPYING
 * for more information.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include "xmp.h"

extern char *optarg;
static int o, i;
static char *token;

extern int probeonly;
extern int loadonly;
extern int randomize;
extern int nocmd;
#ifdef HAVE_SYS_RTPRIO_H
extern int rt;
#endif

#define OPT_LOADONLY	0x103
#define OPT_FX9BUG	0x105
#define OPT_PROBEONLY	0x106
#define OPT_STDOUT	0x109
#define OPT_STEREO	0x10a
#define OPT_NOCMD	0x10b
#define OPT_REALTIME	0x10c
#define OPT_FIXLOOP	0x10d
#define OPT_CRUNCH	0x10e
#define OPT_NOFILTER	0x10f
#define OPT_VBLANK	0x110
#define OPT_SHOWTIME	0x111
#define OPT_DUMP	0x112

static void exclude_formats(char *list)
{
	char *tok;

	tok = strtok(list, ", ");
	while (tok) {
		xmp_enable_format(tok, 0);
		tok = strtok(NULL, ", ");
	}
}

static void list_wrap(char *s, int l, int r, int v)
{
	int i;
	static int c = 0;
	static int m = 0;
	char *t;

	if (s == NULL) {
		for (i = 0; i < l; i++)
			printf(" ");
		c = l;
		m = r;
		return;
	} else if (c > l) {
		c++;
		printf(v ? "," : " ");
	}

	t = strtok(s, " ");

	while (t) {
		if ((c + strlen(t) + 1) > m) {
			c = l;
			printf("\n");
			for (i = 0; i < l; i++) {
				printf(" ");
			}
		} else if (c > l) {
			printf(" ");
		}
		c += strlen(t) + 1;
		printf("%s", t);
		t = strtok(NULL, " ");
	}
}

static void usage(char *s, struct xmp_options *opt)
{
	struct xmp_fmt_info *f, *fmt;
	struct xmp_drv_info *d, *drv;
	char **hlp, buf[80];
	int i;

	printf("Usage: %s [options] [modules]\n", s);

	printf("\nRegistered module loaders:\n");
	xmp_get_fmt_info(&fmt);
	list_wrap(NULL, 3, 78, 1);

	for (i = 0, f = fmt; f; i++, f = f->next) {
		snprintf(buf, 80, "%s (%s)", f->id, f->tracker);
		list_wrap(buf, 3, 0, 1);
	}

	snprintf(buf, 80, "[%d registered loaders]", i);
	list_wrap(buf, 3, 0, 0);
	printf("\n");

	printf("\nAvailable drivers:\n");

	xmp_get_drv_info(&drv);
	list_wrap(NULL, 3, 78, 1);
	for (d = drv; d; d = d->next) {
		snprintf(buf, 80, "%s (%s)", d->id, d->description);
		list_wrap(buf, 3, 0, 1);
	}

	printf("\n");

	for (d = drv; d; d = d->next) {
		if (d->help)
			printf("\n%s options:\n", d->description);
		for (hlp = d->help; hlp && *hlp; hlp += 2)
			printf("   -D%-20.20s %s\n", hlp[0], hlp[1]);
	}

	printf("\nPlayer control options:\n"
	       "   -D parameter[=val]     Pass configuration parameter to the output driver\n"
	       "   -d --driver name       Force output to the specified device\n"
	       "   --fix-sample-loops     Use sample loop start /2 in MOD/UNIC/NP3\n"
	       "   --offset-bug-emulation Emulate Protracker 2.x bug in effect 9\n"
	       "   -l --loop              Enable module looping\n"
	       "   -M --mute ch-list      Mute the specified channels\n"
	       "   --nocmd                Disable interactive commands\n"
	       "   --norc                 Don't read configuration files\n"
	       "   -R --random            Random order playing\n"
#ifdef HAVE_SYS_RTPRIO_H
	       "   --realtime             Run in real-time priority\n"
#endif
	       "   -S --solo ch-list      Set channels to solo mode\n"
	       "   -s --start num         Start from the specified order\n"
	       "   -T --tempo num         Initial tempo (default 6)\n"
	       "   -t --time num          Maximum playing time in seconds\n"
	       "   --vblank               Force vblank timing in Amiga modules (no CIA)\n"
	       "\nPlayer sound options:\n"
	       "   -8 --8bit              Convert 16 bit samples to 8 bit\n"
	       "   -m --mono              Mono output\n"
	       "   --nofilter             Disable IT filter\n"
	       "   --nopan                Disable dynamic panning\n"
	       "   -P --pan pan           Percentual pan amplitude (default %d%%)\n"
	       "   -r --reverse           Reverse left/right stereo channels\n"
	       "   --stereo               Stereo output\n"
	       "\nSoftware mixer options:\n"
	       "   -a --amplify {0|1|2|3} Amplification factor: 0=Normal, 1=x2, 2=x4, 3=x8\n"
	       "   -b --bits {8|16}       Software mixer resolution (8 or 16 bits)\n"
	       "   -c --stdout            Mix the module to stdout\n"
	       "   -F --click-filter      Apply low pass filter to reduce clicks\n"
	       "   -f --frequency rate    Sampling rate in hertz (default %d Hz)\n"
	       "   -i --interpolate       Use linear interpolation (default %s)\n"
	       "   -n --nearest           Use nearest neighbor interpolation\n"
	       "   -o --output-file name  Mix the module to file ('-' for stdout)\n"
	       "   -u --unsigned          Set the mixer to use unsigned samples\n"
	       "\nEnvironment options:\n"
	       "   -I --instrument-path   Set pathname to external samples\n"
	       "\nInformation options:\n"
	       "   -h --help              Print a summary of the command line options\n"
	       "   --load-only            Load module and exit\n"
	       "   --probe-only           Probe audio device and exit\n"
	       "   -q --quiet             Quiet mode (verbosity level = 0)\n"
	       "   --show-time            Display elapsed and remaining time\n"
	       "   -V --version           Print version information\n"
	       "   -v --verbose           Verbose mode (incremental)\n",
	       opt->mix, opt->freq,
	       opt->flags & XMP_CTL_ITPT ? "enabled" : "disabled");
}

void get_options(int argc, char **argv, struct xmp_options *opt,
		 xmp_context ctx)
{
	int optidx = 0;
#define OPTIONS "a:b:cD:d:f:hI:lM:mo:qRS:s:T:t:uVv"
	static struct option lopt[] = {
		{"amplify", 1, 0, 'a'},
		{"bits", 1, 0, 'b'},
		{"driver", 1, 0, 'd'},
		{"frequency", 1, 0, 'f'},
		{"offset-bug-emulation", 0, 0, OPT_FX9BUG},
		{"help", 0, 0, 'h'},
		{"instrument-path", 1, 0, 'I'},
		{"load-only", 0, 0, OPT_LOADONLY},
		{"loop", 0, 0, 'l'},
		{"mono", 0, 0, 'm'},
		{"mute", 1, 0, 'M'},
		{"nocmd", 0, 0, OPT_NOCMD},
		{"output-file", 1, 0, 'o'},
		{"pan", 1, 0, 'P'},
		{"quiet", 0, 0, 'q'},
		{"random", 0, 0, 'R'},
		{"solo", 1, 0, 'S'},
		{"start", 1, 0, 's'},
		{"stdout", 0, 0, 'c'},
		{"tempo", 1, 0, 'T'},
		{"time", 1, 0, 't'},
		{"unsigned", 0, 0, 'u'},
		{"version", 0, 0, 'V'},
		{"verbose", 0, 0, 'v'},
		{NULL, 0, 0, 0}
	};

	i = 0;
	while ((o = getopt_long(argc, argv, OPTIONS, lopt, &optidx)) != -1) {
		switch (o) {
		case 'a':
			opt->amplify = atoi(optarg);
			break;
		case 'b':
			opt->resol = atoi(optarg);
			if (opt->resol != 8 && opt->resol != 16)
				opt->resol = 16;
			break;
		case 'c':
			opt->outfile = "-";
			break;
		case 'D':
			xmp_set_driver_parameter(opt, optarg);
			break;
		case 'd':
			opt->drv_id = optarg;
			break;
		case OPT_FX9BUG:
			opt->quirk |= XMP_QRK_FX9BUG;
			break;
		case 'f':
			opt->freq = strtoul(optarg, NULL, 0);
			break;
		case 'I':
			opt->ins_path = optarg;
			break;
		case 'l':
			opt->flags |= XMP_CTL_LOOP;
			break;
		case OPT_LOADONLY:
			loadonly = 1;
			break;
		case 'm':
			opt->outfmt |= XMP_FORMAT_MONO;
			break;
		case 'o':
			opt->outfile = optarg;
			if (strlen(optarg) >= 4 &&
			    !strcasecmp(optarg + strlen(optarg) - 4, ".wav")) {
				opt->drv_id = "wav";
			}
			break;
		case 'P':
			opt->mix = strtoul(optarg, NULL, 0);
			if (opt->mix < 0)
				opt->mix = 0;
			if (opt->mix > 100)
				opt->mix = 100;
			break;
		case 'q':
			//opt->verbosity = 0;
			break;
		case 'R':
			randomize = 1;
			break;
		case 'M':
		case 'S':
			if (o == 'S')
				xmp_channel_mute(ctx, 0, 64, 1);
			token = strtok(optarg, ",");
			while (token) {
				int a, b;
				char buf[40];
				if (strchr(token, '-')) {
					b = strcspn(token, "-");
					strncpy(buf, token, b);
					a = atoi(buf);
					strncpy(buf, token + b + 1,
						strlen(token) - b - 1);
					b = atoi(buf);
				} else
					a = b = atoi(token);
				for (; b >= a; b--) {
					if (b < 64)
						xmp_channel_mute(ctx, b, 1,
								 (o == 'M'));
				}
				token = strtok(NULL, ",");
			}
			break;
		case 's':
			opt->start = strtoul(optarg, NULL, 0);
			break;
		case OPT_STEREO:
			opt->outfmt &= ~XMP_FORMAT_MONO;
			break;
		case 'T':
			opt->tempo = strtoul(optarg, NULL, 0);
			break;
		case 't':
			opt->time = strtoul(optarg, NULL, 0);
			break;
		case 'u':
			opt->outfmt |= XMP_FMT_UNS;
			break;
		case 'V':
			puts("Extended Module Player " VERSION);
			exit(0);
		case 'v':
			//opt->verbosity++;
			break;
		case 'h':
			usage(argv[0], opt);
		default:
			exit(-1);
		}
	}

	/* Set limits */
	if (opt->freq < 1000)
		opt->freq = 1000;	/* Min. rate 1 kHz */
	if (opt->freq > 48000)
		opt->freq = 48000;	/* Max. rate 48 kHz */
}
