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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "driver.h"
#include "mixer.h"

static int drv_parm = 0;


void *xmp_create_context()
{
	struct xmp_context *ctx;
	struct xmp_options *o;
	uint16 w;

	ctx = calloc(1, sizeof(struct xmp_context));

	if (ctx == NULL)
		return NULL;

	/* Explicitly initialize to keep valgrind happy */
	*ctx->m.mod.name = *ctx->m.mod.type = 0;

	o = &ctx->o;

	w = 0x00ff;
	o->big_endian = (*(char *)&w == 0x00);

	/* Set defaults */
	o->amplify = DEFAULT_AMPLIFY;
	o->freq = 44100;
	o->mix = 70;
	o->resol = 16;
	o->flags = XMP_CTL_FILTER | XMP_CTL_ITPT;
	o->cf_cutoff = 0;

	return ctx;
}

void xmp_free_context(xmp_context ctx)
{
	free(ctx);
}

struct xmp_options *xmp_get_options(xmp_context ctx)
{
	return &((struct xmp_context *)ctx)->o;
}

int xmp_get_flags(xmp_context ctx)
{
	return ((struct xmp_context *)ctx)->m.flags;
}

void xmp_set_flags(xmp_context ctx, int flags)
{
	((struct xmp_context *)ctx)->m.flags = flags;
}

void xmp_init(xmp_context ctx, int argc, char **argv)
{
	int num;

	xmp_init_formats(ctx);

#ifndef __native_client__
	/* must be parsed before loading the rc file. */
	for (num = 1; num < argc; num++) {
		if (!strcmp(argv[num], "--norc"))
			break;
	}
	if (num >= argc)
		_xmp_read_rc((struct xmp_context *)ctx);
#endif
}

void xmp_deinit(xmp_context ctx)
{
	xmp_deinit_formats(ctx);
}

int xmp_open_audio(xmp_context ctx)
{
	//return xmp_drv_open((struct xmp_context *)ctx);
	return xmp_smix_on((struct xmp_context *)ctx);
}

void xmp_close_audio(xmp_context ctx)
{
	//xmp_drv_close((struct xmp_context *)ctx);
	return xmp_smix_off((struct xmp_context *)ctx);
}

void xmp_set_driver_parameter(struct xmp_options *o, char *s)
{
	o->parm[drv_parm] = s;
	while (isspace(*o->parm[drv_parm]))
		o->parm[drv_parm]++;
	drv_parm++;
}

void xmp_channel_mute(xmp_context opaque, int from, int num, int on)
{
	struct xmp_context *ctx = (struct xmp_context *)opaque;

	from += num - 1;

	if (num > 0) {
		while (num--)
			xmp_drv_mute(ctx, from - num, on);
	}
}

int xmp_player_ctl(xmp_context opaque, int cmd, int arg)
{
	struct xmp_context *ctx = (struct xmp_context *)opaque;
	struct xmp_player_context *p = &ctx->p;
	struct xmp_mod_context *m = &ctx->m;

	switch (cmd) {
	case XMP_ORD_PREV:
		if (p->pos > 0)
			p->pos--;
		return p->pos;
	case XMP_ORD_NEXT:
		if (p->pos < m->mod.len)
			p->pos++;
		return p->pos;
	case XMP_ORD_SET:
		if (arg < m->mod.len && arg >= 0) {
			if (p->pos == arg && arg == 0)	/* special case */
				p->pos = -1;
			else
				p->pos = arg;
		}
		return p->pos;
	case XMP_MOD_STOP:
		p->pos = -2;
		break;
	case XMP_MOD_RESTART:
		p->pos = -1;
		break;
	case XMP_GVOL_DEC:
		if (m->volume > 0)
			m->volume--;
		return m->volume;
	case XMP_GVOL_INC:
		if (m->volume < 64)
			m->volume++;
		return m->volume;
	case XMP_TIMER_STOP:
		xmp_drv_stoptimer((struct xmp_context *)ctx);
		break;
	case XMP_TIMER_RESTART:
		xmp_drv_starttimer((struct xmp_context *)ctx);
		break;
	case XMP_SET_FLAG:
		m->flags |= arg;
		break;
	case XMP_RESET_FLAG:
		m->flags &= ~arg;
		break;
	case XMP_TEST_FLAG:
		return (m->flags & arg) != 0;
	}

	return 0;
}

void xmp_play_buffer(xmp_context opaque)
{
	struct xmp_context *ctx = (struct xmp_context *)opaque;

	xmp_drv_bufdump((struct xmp_context *)ctx);
}

void xmp_get_buffer(xmp_context opaque, void **buffer, int *size)
{
	struct xmp_context *ctx = (struct xmp_context *)opaque;

	*size = xmp_smix_softmixer(ctx);
	*buffer = xmp_smix_buffer(ctx);
}

void xmp_get_driver_cfg(xmp_context opaque, int *srate, int *res, int *chn,
			int *itpt)
{
	struct xmp_context *ctx = (struct xmp_context *)opaque;
	struct xmp_driver_context *d = &ctx->d;
	struct xmp_options *o = &ctx->o;

	*srate = d->memavl ? 0 : o->freq;
	*res = o->resol ? o->resol : 8 /* U_LAW */ ;
	*chn = o->outfmt & XMP_FMT_MONO ? 1 : 2;
	*itpt = !!(o->flags & XMP_CTL_ITPT);
}

int xmp_seek_time(xmp_context opaque, int time)
{
	struct xmp_context *ctx = (struct xmp_context *)opaque;
	struct xmp_mod_context *m = &ctx->m;
	int i, t;
	/* _D("seek to %d, total %d", time, xmp_cfg.time); */

	time *= 1000;
	for (i = 0; i < m->mod.len; i++) {
		t = m->xxo_info[i].time;

		_D("%2d: %d %d", i, time, t);

		if (t > time) {
			if (i > 0)
				i--;
			xmp_ord_set(opaque, i);
			return 0;
		}
	}
	return -1;
}
