/*
 * XMP plugin for XMMS/Beep/Audacious
 * Written by Claudio Matsuoka, 2000-04-30
 * Based on J. Nick Koston's MikMod plugin for XMMS
 */

/* Audacious 3.0 port/rewrite for Fedora by Michael Schwendt
 * TODO: list of supported formats missing in 'About' dialog
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>

#include <audacious/configdb.h>
#include <audacious/plugin.h>
#include <audacious/preferences.h>
#include <libaudgui/libaudgui-gtk.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <xmp.h>

#ifdef _DEBUG
#define _D(x...) printf(x)
#else
#define _D(x...)
#endif

static GMutex *seek_mutex;
static GCond *seek_cond;
static gint jumpToTime = -1;
static gboolean stop_flag = FALSE;

static xmp_context ctx;


static void strip_vfs(char *s) {
	int len;
	char *c;

	if (!s) {
		return;
	}
	_D("%s", s);
	if (!memcmp(s, "file://", 7)) {
		len = strlen(s);
		memmove(s, s + 7, len - 6);
	}

	for (c = s; *c; c++) {
		if (*c == '%' && isxdigit(*(c + 1)) && isxdigit(*(c + 2))) {
			char val[3];
			val[0] = *(c + 1);
			val[1] = *(c + 2);
			val[2] = 0;
			*c++ = strtoul(val, NULL, 16);
			len = strlen(c);
			memmove(c, c + 2, len - 1);
		}
	}
}


static void stop(InputPlayback *playback)
{
	_D("*** stop!");
	g_mutex_lock(seek_mutex);
	if (!stop_flag) {
		xmp_stop_module(ctx); 
		stop_flag = TRUE;
		playback->output->abort_write();
		g_cond_signal(seek_cond);
	}
	g_mutex_unlock(seek_mutex);
}


static void mseek(InputPlayback *playback, gint msec)
{
	g_mutex_lock(seek_mutex);
	if (!stop_flag) {
		jumpToTime = msec;
		playback->output->abort_write();
		g_cond_signal(seek_cond);
		g_cond_wait(seek_cond, seek_mutex);
	}
	g_mutex_unlock(seek_mutex);
}


static void mod_pause(InputPlayback *playback, gboolean p)
{
	g_mutex_lock(seek_mutex);
	if (!stop_flag) {
		playback->output->pause(p);
	}
	g_mutex_unlock(seek_mutex);
}


static gboolean init(void)
{
	mcs_handle_t *cfg;

	_D("Plugin init");
	ctx = xmp_create_context();

	jumpToTime = -1;
	seek_mutex = g_mutex_new();
	seek_cond = g_cond_new();

	return TRUE;
}


static void cleanup()
{
	g_cond_free(seek_cond);
	g_mutex_free(seek_mutex);
	xmp_free_context(ctx);
}


static int is_our_file_from_vfs(const char* _filename, VFSFile *vfsfile)
{
	gchar *filename = g_strdup(_filename);
	gboolean ret;

	_D("filename = %s", filename);
	strip_vfs(filename);		/* Sorry, no VFS support */

	ret = (xmp_test_module(filename, NULL) == 0);

	g_free(filename);
	return ret;
}


Tuple *probe_for_tuple(const gchar *_filename, VFSFile *fd)
{
	gchar *filename = g_strdup(_filename);
	int len;
	Tuple *tuple;
	struct xmp_test_info info;

	_D("filename = %s", filename);
	strip_vfs(filename);		/* Sorry, no VFS support */

	xmp_test_module(filename, &info);

	tuple = tuple_new_from_filename(filename);
	tuple_associate_string(tuple, FIELD_TITLE, NULL, info.name);
	tuple_associate_string(tuple, FIELD_CODEC, NULL, info.type);
	//tuple_associate_int(tuple, FIELD_LENGTH, NULL, len);

	return tuple;
}


static gboolean play(InputPlayback *ipb, const gchar *_filename, VFSFile *file, gint start_time, gint stop_time, gboolean pause)
{
	int channelcnt = 1;
	FILE *f;
	int lret, fmt, nch;
	void *data;
	int size;
	gchar *filename = g_strdup(_filename);
	Tuple *tuple;
	struct xmp_module_info info;
	
	_D("play: %s\n", filename);
	if (file == NULL) {
		return FALSE;
	}

	strip_vfs(filename);  /* Sorry, no VFS support */

	_D("play_file: %s", filename);

	jumpToTime = (start_time > 0) ? start_time : -1;
	stop_flag = FALSE;

	if ((f = fopen(filename, "rb")) == 0) {
		goto PLAY_ERROR_1;
	}
	fclose(f);

	if (!ipb->output->open_audio(FMT_S16_NE, 44100, 2)) {
		goto PLAY_ERROR_1;
	}

	_D("*** loading: %s", filename);

	xmp_load_module(ctx, filename);
	g_free(filename);

	if (lret < 0) {
		goto PLAY_ERROR_1;
	}

	xmp_player_start(ctx, 44100, 0);
	xmp_player_get_info(ctx, &info);

	tuple = tuple_new_from_filename(filename);
	tuple_associate_string(tuple, FIELD_TITLE, NULL, info.mod->name);
	tuple_associate_string(tuple, FIELD_CODEC, NULL, info.mod->type);
	tuple_associate_int(tuple, FIELD_LENGTH, NULL, info.total_time);
	ipb->set_tuple(ipb, tuple);

	//ipb->set_params(ipb, xmp_cfg.mod_info.chn * 1000, opt->freq, channelcnt);
	ipb->set_pb_ready(ipb);

	stop_flag = FALSE;

	while (!stop_flag) {
		if (stop_time >= 0 && ipb->output->written_time() >= stop_time) {
			goto DRAIN;
        	}

		g_mutex_lock(seek_mutex);
		if (jumpToTime != -1) {
			xmp_seek_time(ctx, time);
			ipb->output->flush(jumpToTime);
			jumpToTime = -1;
			g_cond_signal(seek_cond);
		}
		g_mutex_unlock(seek_mutex);

		if ((xmp_player_frame(ctx) != 0) && jumpToTime < 0) {
			stop_flag = TRUE;
 DRAIN:
			while (!stop_flag && ipb->output->buffer_playing()) {
				g_usleep(20000);
			}
			break;
		}

        	if (!stop_flag && jumpToTime < 0) {
			xmp_player_get_info(ctx, &info);
			ipb->output->write_audio(info.buffer, info.buffer_size);
        	}

	}

	g_mutex_lock(seek_mutex);
	stop_flag = TRUE;
	g_cond_signal(seek_cond);  /* wake up any waiting request */
	g_mutex_unlock(seek_mutex);

	ipb->output->close_audio();
	xmp_player_end(ctx);
	xmp_release_module(ctx);

	return TRUE;

 PLAY_ERROR_1:
	g_free(filename);
	return FALSE;
}

void xmp_aud_about()
{
	static GtkWidget *about_window = NULL;

	audgui_simple_message(&about_window, GTK_MESSAGE_INFO,
                          g_strdup_printf(
                "Extended Module Player plugin %s", VERSION),
                "Written by Michael Schwendt and Claudio Matsuoka.\n"
                "\n"
		/* TODO: list */
		/* "Supported module formats:" */
	);
}

/* Filtering files by suffix is really stupid. */
const gchar* const fmts[] = {
	"xm", "mod", "m15", "it", "s2m", "s3m", "stm", "stx", "med", "dmf",
	"mtm", "ice", "imf", "ptm", "mdl", "ult", "liq", "psm", "amf",
        "rtm", "pt3", "tcb", "dt", "gtk", "dtt", "mgt", "digi", "dbm",
	"emod", "okt", "sfx", "far", "umx", "stim", "mtp", "ims", "669",
	"fnk", "funk", "amd", "rad", "hsc", "alm", "kris", "ksm", "unic",
	"zen", "crb", "tdd", "gmc", "gdm", "mdz", "xmz", "s3z", "j2b", NULL
};

#ifndef AUD_INPUT_PLUGIN
#define AUD_INPUT_PLUGIN(x...) InputPlugin xmp_ip = { x };
#endif

AUD_INPUT_PLUGIN (
#if _AUD_PLUGIN_VERSION > 20
	.name		= "XMP Plugin " VERSION,
#else
	.description	= "XMP Plugin " VERSION,
#endif
	.init		= init,
	.about		= xmp_aud_about,
	.play		= play,
	.stop		= stop,
	.pause		= mod_pause,
	.probe_for_tuple = probe_for_tuple,
	.is_our_file_from_vfs = is_our_file_from_vfs,
	.cleanup	= cleanup,
	.mseek		= mseek,
#if _AUD_PLUGIN_VERSION > 20
	.extensions	= fmts,
#else
	.vfs_extensions	= fmts,
#endif
)

#if _AUD_PLUGIN_VERSION <= 20
static InputPlugin *xmp_iplist[] = { &xmp_ip, NULL };

SIMPLE_INPUT_PLUGIN(xmp, xmp_iplist);
#endif
