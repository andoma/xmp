#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <xmp.h>
#include "sound.h"
#include "common.h"

extern int optind;
extern struct sound_driver sound_alsa;
extern struct sound_driver sound_null;

struct sound_driver *sound = &sound_alsa;

#ifdef HAVE_SIGNAL_H
static void cleanup(int sig)
{
	signal(SIGTERM, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGFPE, SIG_DFL);
	signal(SIGSEGV, SIG_DFL);

	sound->deinit();
	reset_tty();

	signal(sig, SIG_DFL);
	kill(getpid (), sig);
}
#endif

static void show_info(int what, struct xmp_module_info *mi)
{
	printf("\r%78.78s\n", " ");
	switch (what) {
	case '?':
		info_help();
		break;
	case 'i':
		info_instruments_compact(mi);
		break;
	case 'm':
		info_mod(mi);
		break;
	}
}

static void shuffle(int argc, char **argv)
{
	int i, j;
	char *x;

	for (i = 1; i < argc; i++) {
		j = 1 + rand() % (argc - 1);
		x = argv[i];
		argv[i] = argv[j];
		argv[j] = x;
	}
}

static void check_pause(xmp_context handle, struct control *ctl,
                        struct xmp_module_info *mi)
{
	if (ctl->pause) {
		sound->pause();
		info_frame(mi, ctl, 1);
		while (ctl->pause) {
			usleep(100000);
			read_command(handle, ctl);
			if (ctl->display) {
				show_info(ctl->display, mi);
				info_frame(mi, ctl, 1);
				ctl->display = 0;
			}
		}
		sound->resume();
	}
}

int main(int argc, char **argv)
{
	xmp_context handle;
	struct xmp_module_info mi;
	struct options options;
	struct control control;
	int i;
	int first;
	int skipprev;
#ifndef WIN32
	struct timeval tv;
	struct timezone tz;

	gettimeofday(&tv, &tz);
	srand(tv.tv_usec);
#else
	srand(GetTickCount());
#endif

	memset(&options, 0, sizeof (struct options));
	memset(&control, 0, sizeof (struct control));
	options.verbose = 1;

	get_options(argc, argv, &options);

	if (options.random) {
		shuffle(argc - optind + 1, &argv[optind - 1]);
	}

	if (options.silent) {
		sound = &sound_null;
	}

	if (sound->init(44100, 2) < 0) {
		fprintf(stderr, "%s: can't initialize sound\n", argv[0]);
		exit(EXIT_FAILURE);
	}

#ifdef HAVE_SIGNAL_H
	signal(SIGTERM, cleanup);
	signal(SIGINT, cleanup);
	signal(SIGFPE, cleanup);
	signal(SIGSEGV, cleanup);
	signal(SIGQUIT, cleanup);
#endif

	set_tty();

	handle = xmp_create_context();

	skipprev = 0;

	for (first = optind; optind < argc; optind++) {
		printf("\nLoading %s... (%d of %d)\n",
			argv[optind], optind - first + 1, argc - first);

		if (xmp_load_module(handle, argv[optind]) < 0) {
			fprintf(stderr, "%s: error loading %s\n", argv[0],
				argv[optind]);
			if (skipprev) {
		        	optind -= 2;
				if (optind < first) {
					optind += 2;
				}
			}
			continue;
		}
		skipprev = 0;

		if (xmp_player_start(handle, options.start, 44100, 0) == 0) {
			int refresh_line = 1;

			/* Mute channels */

			for (i = 0; i < XMP_MAX_CHANNELS; i++) {
				xmp_channel_mute(handle, i, options.mute[i]);
			}

			/* Show module data */

			xmp_player_get_info(handle, &mi);

			if (options.verbose > 0) {
				info_mod(&mi);
			}
			if (options.verbose == 2) {
				info_instruments_compact(&mi);
			}
	

			/* Play module */

			while (xmp_player_frame(handle) == 0) {
				int old_loop = mi.loop_count;
				
				xmp_player_get_info(handle, &mi);
				if (!control.loop && old_loop != mi.loop_count)
					break;

				info_frame(&mi, &control, refresh_line);
				sound->play(mi.buffer, mi.buffer_size);

				read_command(handle, &control);

				if (control.display) {
					show_info(control.display, &mi);
					control.display = 0;
					refresh_line = 1;
				}

				check_pause(handle, &control, &mi);

				options.start = 0;
			}
			xmp_player_end(handle);
		}

		xmp_release_module(handle);
		printf("\n");

		if (control.skip == -1) {
			optind -= optind > first ? 2 : 1;
			skipprev = 1;
		} else if (control.skip == -2) {
			goto end;
		}
		control.skip = 0;
	}

	sound->flush();

end:
	xmp_free_context(handle);
	reset_tty();
	sound->deinit();

	exit(EXIT_SUCCESS);
}