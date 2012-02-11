/*
 * XMP plugin for XMMS/Beep/Audacious
 * Written by Claudio Matsuoka, 2000-04-30
 * Based on J. Nick Koston's MikMod plugin for XMMS
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>

#include <audacious/configdb.h>
#include <audacious/plugin.h>

#if __AUDACIOUS_PLUGIN_API__ < 16
#include <audacious/util.h>
#else
#include <gtk/gtk.h>
#define aud_tuple_new_from_filename	tuple_new_from_filename
#define aud_tuple_associate_string	tuple_associate_string
#define aud_tuple_associate_int		tuple_associate_int
#endif

#include "xmp.h"
#include "common.h"
#include "driver.h"

#if __AUDACIOUS_PLUGIN_API__ >= 12
#define CONST12 const
#else
#define CONST12
#endif

static void	init		(void);
static int	is_our_file	(CONST12 char *);
static void	play_file	(InputPlayback *);
static void	stop		(InputPlayback *);
static void	mod_pause	(InputPlayback *, short);
static void	seek		(InputPlayback *, int);
static int	get_time	(InputPlayback *);
static void	*play_loop	(void *);
static void	aboutbox	(void);
#if __AUDACIOUS_PLUGIN_API__ >= 8
static int	is_our_file_from_vfs(CONST12 char *, VFSFile *);
#endif
#if __AUDACIOUS_PLUGIN_API__ < 12
static void	get_song_info	(char *, char **, int *);
#endif
static void	configure	(void);
static void	config_ok	(GtkWidget *, gpointer);
static void	mseek		(InputPlayback *, gulong);
static void	cleanup		(void);

#if __AUDACIOUS_PLUGIN_API__ >= 2
static Tuple	*get_song_tuple	(CONST12 char *);
#endif

static GThread *decode_thread;
static GStaticMutex load_mutex = G_STATIC_MUTEX_INIT;


#define FREQ_SAMPLE_44 0
#define FREQ_SAMPLE_22 1
#define FREQ_SAMPLE_11 2

static struct {
	InputPlayback *ipb;
#if __AUDACIOUS_PLUGIN_API__ < 16
	AFormat fmt;
#else
	int fmt;
#endif
	int nch;
} play_data;

typedef struct {
	gint mixing_freq;
	gint force8bit;
	gint force_mono;
	gint interpolation;
	gint filter;
	gint convert8bit;
	gint fixloops;
	gint loop;
	gint modrange;
	gint pan_amplitude;
	gint time;
	struct xmp_module_info mod_info;
} XMPConfig;


XMPConfig xmp_cfg;
static gboolean xmp_plugin_audio_error = FALSE;

static GtkWidget *Res_16;
static GtkWidget *Res_8;
static GtkWidget *Chan_ST;
static GtkWidget *Chan_MO;
static GtkWidget *Sample_44;
static GtkWidget *Sample_22;
static GtkWidget *Sample_11;
static GtkWidget *Convert_Check;
static GtkWidget *Fixloops_Check;
static GtkWidget *Modrange_Check;
static GtkWidget *Interp_Check;
static GtkWidget *Filter_Check;
static GtkObject *pansep_adj;

static GtkWidget *xmp_conf_window = NULL;
static GtkWidget *about_window = NULL;

int skip = 0;
static short audio_open = FALSE;

/* Filtering files by suffix is really stupid. */
static const gchar *fmts[] = {
	"xm", "mod", "m15", "it", "s2m", "s3m", "stm", "stx", "med", "dmf",
	"mtm", "ice", "imf", "ptm", "mdl", "ult", "liq", "psm", "amf",
        "rtm", "pt3", "tcb", "dt", "gtk", "dtt", "mgt", "digi", "dbm",
	"emod", "okt", "sfx", "far", "umx", "stim", "mtp", "ims", "669",
	"fnk", "funk", "amd", "rad", "hsc", "alm", "kris", "ksm", "unic",
	"zen", "crb", "tdd", "gmc", "gdm", "mdz", "xmz", "s3z", "j2b", NULL
};


InputPlugin xmp_ip = {
	.description	= "XMP Plugin " VERSION,
	.init		= init,
	.about		= aboutbox,
	.configure	= configure,
	.is_our_file	= is_our_file,
	.play_file	= play_file,
	.stop		= stop,
	.pause		= mod_pause,
	.seek		= seek,
	.get_time	= get_time,
#if __AUDACIOUS_PLUGIN_API__ >= 8
	.is_our_file_from_vfs = is_our_file_from_vfs,
#endif
#if __AUDACIOUS_PLUGIN_API__ < 12
	.get_song_info	= get_song_info,
#endif
	.cleanup	= cleanup,
#if __AUDACIOUS_PLUGIN_API__ >= 2
	.get_song_tuple	= get_song_tuple,
	.mseek		= mseek,
	.vfs_extensions = fmts,
#endif
};


#if __AUDACIOUS_PLUGIN_API__ >= 2

InputPlugin *xmp_iplist[] = { &xmp_ip, NULL };

DECLARE_PLUGIN(xmp, NULL, NULL, xmp_iplist, NULL, NULL, NULL, NULL, NULL);

#endif
 
extern struct xmp_drv_info drv_smix;


static void strip_vfs(char *s)
{
	int len;
	char *c;

	g_static_mutex_lock(&load_mutex);
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
	g_static_mutex_unlock(&load_mutex);
}

static void aboutbox()
{
	GtkWidget *vbox1;
	GtkWidget *label1;
	GtkWidget *about_exit;
	GtkWidget *scroll1;
	GtkWidget *table1;
	GtkWidget *label_fmt, *label_trk;
	struct xmp_fmt_info *f, *fmt;
	int i;

	if (about_window) {
		gdk_window_raise(about_window->window);
		return;
	}

	about_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_type_hint(GTK_WINDOW(about_window),
					GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_object_set_data(GTK_OBJECT(about_window),
		"about_window", about_window);
	gtk_window_set_title(GTK_WINDOW(about_window),"About the XMP Plugin");
	gtk_window_set_policy(GTK_WINDOW(about_window), FALSE, FALSE, FALSE);
	gtk_signal_connect(GTK_OBJECT(about_window), "destroy",
		GTK_SIGNAL_FUNC(gtk_widget_destroyed), &about_window);
	gtk_container_border_width(GTK_CONTAINER(about_window), 10);
	gtk_widget_realize(about_window);

	vbox1 = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(about_window), vbox1);
	gtk_object_set_data(GTK_OBJECT(about_window), "vbox1", vbox1);
	gtk_widget_show(vbox1);
	gtk_container_border_width(GTK_CONTAINER(vbox1), 10);

	label1 = gtk_label_new(
		"Extended Module Player " VERSION "\n"
		"Written by Claudio Matsuoka and Hipolito Carraro Jr.\n"
		"\n"
		"Portions Copyright (C) 1998,2000 Olivier Lapicque,\n"
		"(C) 1998 Tammo Hinrichs, (C) 1998 Sylvain Chipaux,\n"
		"(C) 1997 Bert Jahn, (C) 1999 Tatsuyuki Satoh, (C)\n"
		"1995-1999 Arnaud Carre, (C) 2001-2006 Russell Marks,\n"
		"(C) 2005-2006 Michael Kohn\n"
		"\n"
		"Supported module formats:"
	);
	gtk_object_set_data(GTK_OBJECT(label1), "label1", label1);
	gtk_box_pack_start(GTK_BOX(vbox1), label1, TRUE, TRUE, 0);

	scroll1 = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll1),
			GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_widget_set_size_request(scroll1, 290, 100);
	gtk_object_set_data(GTK_OBJECT(scroll1), "scroll1", scroll1);
	gtk_widget_set (scroll1, "height", 100, NULL);
	gtk_box_pack_start(GTK_BOX(vbox1), scroll1, TRUE, TRUE, 0);

	xmp_get_fmt_info(&fmt);
	table1 = gtk_table_new(100, 2, FALSE);
	for (i = 0, f = fmt; f; i++, f = f->next) {
		label_fmt = gtk_label_new(f->id);
		label_trk = gtk_label_new(f->tracker);
		gtk_label_set_justify (GTK_LABEL (label_fmt), GTK_JUSTIFY_LEFT);
		gtk_label_set_justify (GTK_LABEL (label_trk), GTK_JUSTIFY_LEFT);
		gtk_table_attach_defaults (GTK_TABLE (table1),
						label_fmt, 0, 1, i, i + 1);
		gtk_table_attach_defaults (GTK_TABLE (table1),
						label_trk, 1, 2, i, i + 1);
	}

	gtk_table_resize (GTK_TABLE (table1), i + 1, 3);
	gtk_object_set_data(GTK_OBJECT(table1), "table1", table1);
	
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll1),
								table1);

	about_exit = gtk_button_new_with_label("Ok");
	gtk_signal_connect_object(GTK_OBJECT(about_exit), "clicked",
		GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(about_window));

	gtk_object_set_data(GTK_OBJECT(about_window), "about_exit", about_exit);
	gtk_box_pack_start(GTK_BOX(vbox1), about_exit, FALSE, FALSE, 0);

	gtk_widget_show_all(about_window);
}


static xmp_context ctx;


static void stop(InputPlayback *ipb)
{
	if (!ipb->playing)
		return;

	_D("*** stop!");
	xmp_stop_module(ctx); 

	ipb->playing = 0;
	g_thread_join(decode_thread);
	ipb->output->close_audio();
        audio_open = FALSE;
}

static void seek(InputPlayback *ipb, int time)
{
	mseek(ipb, time * 1000);
}

static void mseek(InputPlayback *ipb, unsigned long time)
{
	int i, t;
	struct player_data *p = &((struct context_data *)ctx)->p;

	_D("seek to %ld, total %d", time, xmp_cfg.time);

	for (i = 0; i < xmp_cfg.mod_info.len; i++) {
		t = p->m.xxo_info[i].time;

		_D("%2d: %ld %d", i, time, t);

		if (t > time) {
			int a;
			if (i > 0)
				i--;
			a = xmp_ord_set(ctx, i);
#if __AUDACIOUS_PLUGIN_API__ < 13 || __AUDACIOUS_PLUGIN_API__ >= 16
			ipb->output->flush(p->m.xxo_info[i].time);
#else
			ipb->output->flush(p->m.xxo_info[i].time / 1000);
#endif
			break;
		}
	}
}

static void mod_pause(InputPlayback *ipb, short p)
{
	ipb->output->pause(p);
}


static int get_time(InputPlayback *ipb)
{
	if (xmp_plugin_audio_error)
		return -2;
	if (!ipb->playing)
		return -1;

#if __AUDACIOUS_PLUGIN_API__ < 13
	return ipb->output->output_time();
#else
	return ipb->output->written_time();
#endif
}


#if __AUDACIOUS_PLUGIN_API__ < 2

InputPlugin *get_iplugin_info()
{
	return &xmp_ip;
}

#endif


static void init(void)
{
#if __AUDACIOUS_PLUGIN_API__ < 16
	ConfigDb *cfg;
#else
	mcs_handle_t *cfg;
#endif

	_D("Plugin init");
	xmp_drv_register(&drv_smix);
	ctx = xmp_create_context();

	xmp_cfg.mixing_freq = 0;
	xmp_cfg.convert8bit = 0;
	xmp_cfg.fixloops = 0;
	xmp_cfg.modrange = 0;
	xmp_cfg.force8bit = 0;
	xmp_cfg.force_mono = 0;
	xmp_cfg.interpolation = TRUE;
	xmp_cfg.filter = TRUE;
	xmp_cfg.pan_amplitude = 80;

#define CFGREADINT(x) aud_cfg_db_get_int (cfg, "XMP", #x, &xmp_cfg.x)

	if ((cfg = aud_cfg_db_open())) {
		CFGREADINT(mixing_freq);
		CFGREADINT(force8bit);
		CFGREADINT(convert8bit);
		CFGREADINT(modrange);
		CFGREADINT(fixloops);
		CFGREADINT(force_mono);
		CFGREADINT(interpolation);
		CFGREADINT(filter);
		CFGREADINT(pan_amplitude);

		aud_cfg_db_close(cfg);
	}

	xmp_init(ctx, 0, NULL);
}


static void cleanup()
{
	xmp_free_context(ctx);
}


static int is_our_file(CONST12 char *filename)
{
	_D("filename = %s", filename);
	strip_vfs((char *)filename);		/* Sorry, no VFS support */

	if (xmp_test_module(ctx, (char *)filename, NULL) == 0)
		return 1;

	return 0;
}

#if __AUDACIOUS_PLUGIN_API__ >= 8

static int is_our_file_from_vfs(CONST12 char* filename, VFSFile *vfsfile)
{
	_D("filename = %s", filename);
	strip_vfs((char *)filename);		/* Sorry, no VFS support */

	if (xmp_test_module(ctx, (char *)filename, NULL) == 0)
		return 1;

	return 0;
}

#endif

#if __AUDACIOUS_PLUGIN_API__ < 12

static void get_song_info(char *filename, char **title, int *length)
{
	xmp_context ctx2;
	int lret;
	struct xmp_module_info mi;
	struct xmp_options *opt;

	_D("filename = %s", filename);
	strip_vfs(filename);		/* Sorry, no VFS support */

	/* Create new context to load a file and get the length */

	ctx2 = xmp_create_context();
	opt = xmp_get_options(ctx2);
	opt->skipsmp = 1;	/* don't load samples */

	g_static_mutex_lock(&load_mutex);
	lret = xmp_load_module(ctx2, filename);
	g_static_mutex_unlock(&load_mutex);

	if (lret < 0) {
		xmp_free_context(ctx2);
		return;
	}

	*length = lret;
	xmp_get_module_info(ctx2, &mi);
	*title = g_strdup(mi.name);

	xmp_release_module(ctx2);
	xmp_free_context(ctx2);
}

#endif

#if __AUDACIOUS_PLUGIN_API__ >= 2

static Tuple *get_song_tuple(CONST12 char *filename)
{
	Tuple *tuple;
	xmp_context ctx2;
	int lret;
	struct xmp_module_info mi;
	struct xmp_options *opt;

	_D("filename = %s", filename);
	strip_vfs((char *)filename);		/* Sorry, no VFS support */

	tuple = aud_tuple_new_from_filename(filename);

	/* Create new context to load a file and get the length */

	ctx2 = xmp_create_context();
	opt = xmp_get_options(ctx2);
	opt->skipsmp = 1;	/* don't load samples */

	g_static_mutex_lock(&load_mutex);
	lret = xmp_load_module(ctx2, (char *)filename);
	g_static_mutex_unlock(&load_mutex);

	if (lret < 0) {
		xmp_free_context(ctx2);
		return NULL;
	}

	xmp_get_module_info(ctx2, &mi);

	aud_tuple_associate_string(tuple, FIELD_TITLE, NULL, mi.name);
	aud_tuple_associate_string(tuple, FIELD_CODEC, NULL, mi.type);
	aud_tuple_associate_int(tuple, FIELD_LENGTH, NULL, lret);

	xmp_release_module(ctx2);
	xmp_free_context(ctx2);

	return tuple;
}

#endif


static void play_file(InputPlayback *ipb)
{
	char *filename = ipb->filename;
	int channelcnt = 1;
	FILE *f;
	struct xmp_options *opt;
	int lret;
	
	_D("play: %s\n", filename);
	opt = xmp_get_options(ctx);

	/* Sorry, no VFS support */
	strip_vfs(filename);

	_D("play_file: %s", filename);

	stop(ipb);	/* sanity check */

	if ((f = fopen(filename,"rb")) == 0) {
		ipb->playing = 0;
		return;
	}
	fclose(f);

	xmp_plugin_audio_error = FALSE;
	ipb->playing = 1;

	opt->resol = 8;
	opt->verbosity = 0;
	opt->drv_id = "smix";

	switch (xmp_cfg.mixing_freq) {
	case 1:
		opt->freq = 22050;	/* 1:2 mixing freq */
		break;
	case 2:
		opt->freq = 11025;	/* 1:4 mixing freq */
		break;
	default:
		opt->freq = 44100;	/* standard mixing freq */
		break;
	}

	if (xmp_cfg.force8bit == 0)
		opt->resol = 16;

	if (xmp_cfg.force_mono == 0) {
		channelcnt = 2;
		opt->outfmt &= ~XMP_FORMAT_MONO;
	} else {
		opt->outfmt |= XMP_FORMAT_MONO;
	}

	if (xmp_cfg.interpolation == 1)
		opt->flags |= XMP_CTL_ITPT;
	else
		opt->flags &= ~XMP_CTL_ITPT;

	if (xmp_cfg.filter == 1)
		opt->flags |= XMP_CTL_FILTER;
	else
		opt->flags &= ~XMP_CTL_FILTER;

	opt->mix = xmp_cfg.pan_amplitude;

	play_data.ipb = ipb;
	play_data.fmt = opt->resol == 16 ? FMT_S16_NE : FMT_U8;
	play_data.nch = opt->outfmt & XMP_FORMAT_MONO ? 1 : 2;
	
	if (audio_open)
	    ipb->output->close_audio();
	
	if (!ipb->output->open_audio(play_data.fmt, opt->freq, play_data.nch)) {
	    ipb->error = TRUE;
	    xmp_plugin_audio_error = TRUE;
	    return;
	}
	
	audio_open = TRUE;

	xmp_open_audio(ctx);

	_D("*** loading: %s", filename);

	g_static_mutex_lock(&load_mutex);
	lret =  xmp_load_module(ctx, filename);
	g_static_mutex_unlock(&load_mutex);

	if (lret < 0) {
#if __AUDACIOUS_PLUGIN_API__ < 13
		xmp_ip.set_info_text("Error loading mod");
#endif
		ipb->playing = 0;
		xmp_close_audio(ctx);
		return;
	}

	xmp_cfg.time = lret;
	xmp_get_module_info(ctx, &xmp_cfg.mod_info);

#if __AUDACIOUS_PLUGIN_API__ >= 2
#if __AUDACIOUS_PLUGIN_API__ >= 12
	ipb->set_params(ipb, xmp_cfg.mod_info.name, lret,
		xmp_cfg.mod_info.chn * 1000, opt->freq, channelcnt);
#else
	ipb->set_params(ipb, xmp_cfg.mod_info.name, lret, 0,
						opt->freq, channelcnt);
#endif
	ipb->playing = 1;
	ipb->eof = 0;
	ipb->error = FALSE;

	decode_thread = g_thread_self();
	ipb->set_pb_ready(ipb);
	play_loop(ipb);
#else
	xmp_ip.set_info(xmp_cfg.mod_info.name, lret, 0, opt->freq, channelcnt);
	decode_thread = g_thread_create(play_loop, ipb, TRUE, NULL);
#endif
}


static gpointer play_loop(gpointer arg)
{
	InputPlayback *ipb = arg;
	void *data;
	int size;

	xmp_player_start(ctx);
	while (xmp_player_frame(ctx) == 0) {
		xmp_get_buffer(ctx, &data, &size);

#if __AUDACIOUS_PLUGIN_API__ >= 17
		while (ipb->playing && ipb->output->buffer_playing())
			g_usleep (10000);
		ipb->output->write_audio(data, size);
#elif __AUDACIOUS_PLUGIN_API__ >= 2
		play_data.ipb->pass_audio(play_data.ipb, play_data.fmt,
			play_data.nch, size, data, &play_data.ipb->playing);

#else
		xmp_ip.add_vis_pcm(xmp_ip.output->written_time(),
			xmp_cfg.force8bit ? FMT_U8 : FMT_S16_NE,
			xmp_cfg.force_mono ? 1 : 2, size, data);
	
		while (xmp_ip.output->buffer_free() < size && play_data.ipb->playing)
			usleep(10000);

		if (play_data.ipb->playing)
			xmp_ip.output->write_audio(data, size);
#endif
	}


	xmp_player_end(ctx);

	xmp_release_module(ctx);
	xmp_close_audio(ctx);

	ipb->eof = 1;
	ipb->playing = 0;

	return NULL;
}


static void configure()
{
	GtkWidget *notebook1;
	GtkWidget *vbox;
	GtkWidget *vbox1;
	GtkWidget *hbox1;
	GtkWidget *Resolution_Frame;
	GtkWidget *vbox4;
	GSList *resolution_group = NULL;
	GtkWidget *Channels_Frame;
	GtkWidget *vbox5;
	GSList *vbox5_group = NULL;
	GtkWidget *Downsample_Frame;
	GtkWidget *vbox3;
	GSList *sample_group = NULL;
	GtkWidget *vbox6;
	GtkWidget *Quality_Label;
	GtkWidget *Options_Label;
	GtkWidget *pansep_label, *pansep_hscale;
	GtkWidget *bbox;
	GtkWidget *ok;
	GtkWidget *cancel;

	if (xmp_conf_window) {
		gdk_window_raise(xmp_conf_window->window);
		return;
	}

	xmp_conf_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_type_hint(GTK_WINDOW(xmp_conf_window),
					GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window),
		"xmp_conf_window", xmp_conf_window);
	gtk_window_set_title(GTK_WINDOW(xmp_conf_window), "XMP Configuration");
	gtk_window_set_policy(GTK_WINDOW(xmp_conf_window), FALSE, FALSE, FALSE);
	gtk_window_set_position(GTK_WINDOW(xmp_conf_window), GTK_WIN_POS_MOUSE);
	gtk_signal_connect(GTK_OBJECT(xmp_conf_window), "destroy",
		GTK_SIGNAL_FUNC(gtk_widget_destroyed),&xmp_conf_window);
	gtk_container_border_width(GTK_CONTAINER(xmp_conf_window), 10);

	vbox = gtk_vbox_new(FALSE, 10);
	gtk_container_add(GTK_CONTAINER(xmp_conf_window), vbox);

	notebook1 = gtk_notebook_new();
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window),"notebook1", notebook1);
	gtk_widget_show(notebook1);
	gtk_box_pack_start(GTK_BOX(vbox), notebook1, TRUE, TRUE, 0);
	gtk_container_border_width(GTK_CONTAINER(notebook1), 3);

	vbox1 = gtk_vbox_new(FALSE, 0);
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window),"vbox1", vbox1);
	gtk_widget_show(vbox1);

	hbox1 = gtk_hbox_new(FALSE, 0);
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window),"hbox1", hbox1);
	gtk_widget_show(hbox1);
	gtk_box_pack_start(GTK_BOX(vbox1), hbox1, TRUE, TRUE, 0);

	Resolution_Frame = gtk_frame_new("Resolution");
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window),
		"Resolution_Frame", Resolution_Frame);
	gtk_widget_show(Resolution_Frame);
	gtk_box_pack_start(GTK_BOX(hbox1), Resolution_Frame, TRUE, TRUE, 0);
	gtk_container_set_border_width(GTK_CONTAINER (Resolution_Frame), 5);

	vbox4 = gtk_vbox_new(FALSE, 0);
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window),"vbox4", vbox4);
	gtk_widget_show(vbox4);
	gtk_container_add(GTK_CONTAINER(Resolution_Frame), vbox4);

	Res_16 = gtk_radio_button_new_with_label(resolution_group, "16 bit");
	resolution_group = gtk_radio_button_group(GTK_RADIO_BUTTON(Res_16));
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window), "Res_16", Res_16);
	gtk_widget_show(Res_16);
	gtk_box_pack_start(GTK_BOX(vbox4), Res_16, TRUE, TRUE, 0);
	if (xmp_cfg.force8bit == 0)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(Res_16), TRUE);

	Res_8 = gtk_radio_button_new_with_label(resolution_group, "8 bit");
	resolution_group = gtk_radio_button_group(GTK_RADIO_BUTTON(Res_8));
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window), "Res_8", Res_8);
	gtk_widget_show(Res_8);
	gtk_box_pack_start(GTK_BOX(vbox4), Res_8, TRUE, TRUE, 0);
	if (xmp_cfg.force8bit == 1)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(Res_8), TRUE);

	Channels_Frame = gtk_frame_new("Channels");
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window),
		"Channels_Frame", Channels_Frame);
	gtk_widget_show(Channels_Frame);
	gtk_box_pack_start(GTK_BOX(hbox1), Channels_Frame, TRUE, TRUE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(Channels_Frame), 5);

	vbox5 = gtk_vbox_new(FALSE, 0);
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window), "vbox5", vbox5);
	gtk_widget_show(vbox5);
	gtk_container_add(GTK_CONTAINER(Channels_Frame), vbox5);

	Chan_ST = gtk_radio_button_new_with_label(vbox5_group, "Stereo");
	vbox5_group = gtk_radio_button_group(GTK_RADIO_BUTTON(Chan_ST));
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window), "Chan_ST", Chan_ST);
	gtk_widget_show(Chan_ST);
	gtk_box_pack_start(GTK_BOX(vbox5), Chan_ST, TRUE, TRUE, 0);
	if (xmp_cfg.force_mono == 0)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(Chan_ST), TRUE);

	Chan_MO = gtk_radio_button_new_with_label(vbox5_group, "Mono");
	vbox5_group = gtk_radio_button_group(GTK_RADIO_BUTTON(Chan_MO));
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window), "Chan_MO", Chan_MO);
	gtk_widget_show(Chan_MO);
	gtk_box_pack_start(GTK_BOX(vbox5), Chan_MO, TRUE, TRUE, 0);
	if (xmp_cfg.force_mono == 1)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(Chan_MO), TRUE);

	Downsample_Frame = gtk_frame_new("Sampling rate");
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window),
		"Downsample_Frame", Downsample_Frame);
	gtk_widget_show(Downsample_Frame);
	gtk_box_pack_start(GTK_BOX(vbox1), Downsample_Frame, TRUE, TRUE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(Downsample_Frame), 5);

	vbox3 = gtk_vbox_new(FALSE, 0);
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window), "vbox3", vbox3);
	gtk_widget_show(vbox3);
	gtk_container_add(GTK_CONTAINER(Downsample_Frame), vbox3);

	Sample_44 = gtk_radio_button_new_with_label(sample_group, "44 kHz");
	sample_group = gtk_radio_button_group(GTK_RADIO_BUTTON(Sample_44));
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window),"Sample_44", Sample_44);
	gtk_widget_show(Sample_44);
	gtk_box_pack_start(GTK_BOX(vbox3), Sample_44, TRUE, TRUE, 0);
	if (xmp_cfg.mixing_freq == FREQ_SAMPLE_44)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(Sample_44),TRUE);

	Sample_22 = gtk_radio_button_new_with_label(sample_group, "22 kHz");
	sample_group = gtk_radio_button_group(GTK_RADIO_BUTTON(Sample_22));
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window), "Sample_22",Sample_22);
	gtk_widget_show(Sample_22);
	gtk_box_pack_start(GTK_BOX(vbox3), Sample_22, TRUE, TRUE, 0);
	if (xmp_cfg.mixing_freq == FREQ_SAMPLE_22)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(Sample_22),TRUE);

	Sample_11 = gtk_radio_button_new_with_label(sample_group, "11 kHz");
	sample_group = gtk_radio_button_group(GTK_RADIO_BUTTON(Sample_11));
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window), "Sample_11",Sample_11);
	gtk_widget_show(Sample_11);
	gtk_box_pack_start(GTK_BOX(vbox3), Sample_11, TRUE, TRUE, 0);
	if (xmp_cfg.mixing_freq == FREQ_SAMPLE_11)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(Sample_11),TRUE);

	vbox6 = gtk_vbox_new(FALSE, 0);
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window), "vbox6", vbox6);
	gtk_widget_show(vbox6);

	/* Options */

#define OPTCHECK(w,l,o) {						\
	w = gtk_check_button_new_with_label(l);				\
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window), #w, w);	\
	gtk_widget_show(w);						\
	gtk_box_pack_start(GTK_BOX(vbox6), w, TRUE, TRUE, 0);		\
	if (xmp_cfg.o == 1)						\
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE); \
}

	OPTCHECK(Convert_Check, "Convert 16 bit samples to 8 bit", convert8bit);
	OPTCHECK(Fixloops_Check, "Fix sample loops", fixloops);
	OPTCHECK(Modrange_Check, "Force 3 octave range in standard MOD files",
		modrange);
	OPTCHECK(Interp_Check, "Enable 32-bit linear interpolation",
		interpolation);
	OPTCHECK(Filter_Check, "Enable IT filters", filter);

	pansep_label = gtk_label_new("Pan amplitude (%)");
	gtk_widget_show(pansep_label);
	gtk_box_pack_start(GTK_BOX(vbox6), pansep_label, TRUE, TRUE, 0);
	pansep_adj = gtk_adjustment_new(xmp_cfg.pan_amplitude,
		0.0, 100.0, 1.0, 10.0, 1.0);
	pansep_hscale = gtk_hscale_new(GTK_ADJUSTMENT(pansep_adj));
	gtk_scale_set_digits(GTK_SCALE(pansep_hscale), 0);
	gtk_scale_set_draw_value(GTK_SCALE(pansep_hscale), TRUE);
	gtk_scale_set_value_pos(GTK_SCALE(pansep_hscale), GTK_POS_BOTTOM);
	gtk_widget_show(pansep_hscale);
	gtk_box_pack_start(GTK_BOX(vbox6), pansep_hscale, TRUE, TRUE, 0);

	Quality_Label = gtk_label_new("Quality");
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window),
		"Quality_Label", Quality_Label);
	gtk_widget_show(Quality_Label);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook1), vbox1, Quality_Label);

	Options_Label = gtk_label_new("Options");
	gtk_object_set_data(GTK_OBJECT(xmp_conf_window),
		"Options_Label", Options_Label);
	gtk_widget_show(Options_Label);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook1), vbox6, Options_Label);

	bbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
	gtk_button_box_set_spacing(GTK_BUTTON_BOX(bbox), 5);
	gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);

	ok = gtk_button_new_with_label("Ok");
	gtk_signal_connect(GTK_OBJECT(ok), "clicked",
		GTK_SIGNAL_FUNC(config_ok), NULL);
	GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(bbox), ok, TRUE, TRUE, 0);
	gtk_widget_show(ok);
	gtk_widget_grab_default(ok);

	cancel = gtk_button_new_with_label("Cancel");
	gtk_signal_connect_object(GTK_OBJECT(cancel), "clicked",
		GTK_SIGNAL_FUNC(gtk_widget_destroy),
		GTK_OBJECT(xmp_conf_window));
	GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(bbox), cancel, TRUE, TRUE, 0);
	gtk_widget_show(cancel);

	gtk_widget_show(bbox);

	gtk_widget_show(vbox);
	gtk_widget_show(xmp_conf_window);
}


static void config_ok(GtkWidget *widget, gpointer data)
{
#if __AUDACIOUS_PLUGIN_API__ < 16
	ConfigDb *cfg;
#else
	mcs_handle_t *cfg;
#endif
	struct xmp_options *opt;

	opt = xmp_get_options(ctx);

	if (GTK_TOGGLE_BUTTON(Res_16)->active)
		xmp_cfg.force8bit = 0;
	if (GTK_TOGGLE_BUTTON(Res_8)->active)
		xmp_cfg.force8bit = 1;

	if (GTK_TOGGLE_BUTTON(Chan_ST)->active)
		xmp_cfg.force_mono = 0;
	if (GTK_TOGGLE_BUTTON(Chan_MO)->active)
		xmp_cfg.force_mono = 1;

	if (GTK_TOGGLE_BUTTON(Sample_44)->active)
		xmp_cfg.mixing_freq = 0;
	if (GTK_TOGGLE_BUTTON(Sample_22)->active)
		xmp_cfg.mixing_freq = 1;
	if (GTK_TOGGLE_BUTTON(Sample_11)->active)
		xmp_cfg.mixing_freq = 2;

	xmp_cfg.interpolation = !!GTK_TOGGLE_BUTTON(Interp_Check)->active;
	xmp_cfg.filter = !!GTK_TOGGLE_BUTTON(Filter_Check)->active;
	xmp_cfg.convert8bit = !!GTK_TOGGLE_BUTTON(Convert_Check)->active;
	xmp_cfg.modrange = !!GTK_TOGGLE_BUTTON(Modrange_Check)->active;
	xmp_cfg.fixloops = !!GTK_TOGGLE_BUTTON(Fixloops_Check)->active;

	xmp_cfg.pan_amplitude = (guchar)GTK_ADJUSTMENT(pansep_adj)->value;
        opt->mix = xmp_cfg.pan_amplitude;

	cfg = aud_cfg_db_open();

#define CFGWRITEINT(x) aud_cfg_db_set_int (cfg, "XMP", #x, xmp_cfg.x)

	CFGWRITEINT(mixing_freq);
	CFGWRITEINT(force8bit);
	CFGWRITEINT(convert8bit);
	CFGWRITEINT(modrange);
	CFGWRITEINT(fixloops);
	CFGWRITEINT(force_mono);
	CFGWRITEINT(interpolation);
	CFGWRITEINT(filter);
	CFGWRITEINT(pan_amplitude);

	aud_cfg_db_close(cfg);

	gtk_widget_destroy(xmp_conf_window);
}

