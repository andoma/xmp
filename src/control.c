/* Extended Module Player
 * Copyright (C) 1996-2012 Claudio Matsuoka and Hipolito Carraro Jr
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU Lesser General Public License. See COPYING.LIB
 * for more information.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdarg.h>

#include "format.h"
#include "virtual.h"
#include "mixer.h"
#include "unxz/xz.h"

const char *xmp_version = XMP_VERSION;
const unsigned int xmp_vercode = XMP_VERCODE;

xmp_context xmp_create_context()
{
	struct context_data *ctx;

	ctx = calloc(1, sizeof(struct context_data));
	if (ctx == NULL) {
		return NULL;
	}

	return (xmp_context)ctx;
}

void xmp_free_context(xmp_context opaque)
{
	free(opaque);
}

static void set_position(struct context_data *ctx, int pos, int dir)
{
	struct player_data *p = &ctx->p;
	struct module_data *m = &ctx->m;
	struct xmp_module *mod = &m->mod;
	struct flow_control *f = &p->flow;
	int seq, start;

	/* If dir is 0, we can jump to a different sequence */
	if (dir == 0) {
		seq = get_sequence(ctx, pos);
	} else {
		seq = p->sequence;
	}

	start = m->seq_data[seq].entry_point;

	if (seq >= 0) {
		p->sequence = seq;

		if (pos >= 0) {
			if (mod->xxo[pos] == 0xff) {
				return;
			}

			while (mod->xxo[pos] == 0xfe && pos > start) {
				if (dir < 0) {
					pos--;
				} else {
					pos++;
				}
			}

			if (pos > p->scan[seq].ord) {
				f->end_point = 0;
			} else {
				f->num_rows = mod->xxp[mod->xxo[p->ord]]->rows;
				f->end_point = p->scan[seq].num;
			}
		}

		if (pos < m->mod.len) {
			if (pos == 0) {
				p->pos = -1;
			} else {
				p->pos = pos;
			}
		}
	}
}

int xmp_next_position(xmp_context opaque)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct player_data *p = &ctx->p;
	struct module_data *m = &ctx->m;

	if (p->pos < m->mod.len)
		set_position(ctx, p->pos + 1, 1);
	return p->pos;
}

int xmp_prev_position(xmp_context opaque)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct player_data *p = &ctx->p;
	struct module_data *m = &ctx->m;

	if (p->pos == m->seq_data[p->sequence].entry_point) {
		set_position(ctx, -1, -1);
	} else if (p->pos > m->seq_data[p->sequence].entry_point) {
		set_position(ctx, p->pos - 1, -1);
	}
	return p->pos;
}

int xmp_set_position(xmp_context opaque, int pos)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct player_data *p = &ctx->p;

	set_position(ctx, pos, 0);
	return p->pos;
}

void xmp_stop_module(xmp_context opaque)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct player_data *p = &ctx->p;

	p->pos = -2;
}

void xmp_restart_module(xmp_context opaque)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct player_data *p = &ctx->p;

	p->pos = -1;
}

int xmp_seek_time(xmp_context opaque, int time)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct player_data *p = &ctx->p;
	struct module_data *m = &ctx->m;
	int i, t;

	for (i = m->mod.len - 1; i >= 0; i--) {
		int pat = m->mod.xxo[i];
		if (pat >= m->mod.pat) {
			continue;
		}
		if (get_sequence(ctx, i) != p->sequence) {
			continue;
		}
		t = m->xxo_info[i].time;
		if (time >= t) {
			set_position(ctx, i, 1);
			break;
		}
	}
	if (i < 0) {
		xmp_set_position(opaque, 0);
	}

	return p->pos < 0 ? 0 : p->pos;
}

int xmp_channel_mute(xmp_context opaque, int chn, int status)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct player_data *p = &ctx->p;
	int ret;

	if (chn < 0 || chn >= XMP_MAX_CHANNELS) {
		return -XMP_ERROR_INVALID;
	}
	
	ret = p->channel_mute[chn];

	if (status >= 2) {
		p->channel_mute[chn] = !p->channel_mute[chn];
	} else if (status >= 0) {
		p->channel_mute[chn] = status;
	}

	return ret;
}

int xmp_channel_vol(xmp_context opaque, int chn, int val)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct player_data *p = &ctx->p;
	int ret;

	if (chn < 0 || chn >= XMP_MAX_CHANNELS) {
		return -XMP_ERROR_INVALID;
	}
	
	ret = p->channel_vol[chn];

	if (val >= 0 || val <= 100) {
		p->channel_vol[chn] = val;
	}

	return ret;
}

int xmp_mixer_set(xmp_context opaque, int parm, int val)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct mixer_data *s = &ctx->s;
	int ret = -XMP_ERROR_INVALID;

	switch (parm) {
	case XMP_MIXER_AMP:
		if (val >= 0 && val <= 3) {
			s->amplify = val;
			ret = 0;
		}
		break;
	case XMP_MIXER_MIX:
		if (val >= 0 && val <= 100) {
			s->mix = val;
			ret = 0;
		}
		break;
	case XMP_MIXER_INTERP:
		if (val >= XMP_INTERP_NEAREST && val <= XMP_INTERP_LINEAR) {
			s->interp = val;
			ret = 0;
		}
		break;
	case XMP_MIXER_DSP:
		s->dsp = val;
		ret = 0;
		break;
	}

	return ret;
}

int xmp_mixer_get(xmp_context opaque, int parm)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct mixer_data *s = &ctx->s;
	int ret = -XMP_ERROR_INVALID;

	switch (parm) {
	case XMP_MIXER_AMP:
		ret = s->amplify;
		break;
	case XMP_MIXER_MIX:
		ret = s->mix;
		break;
	case XMP_MIXER_INTERP:
		ret = s->interp;
		break;
	case XMP_MIXER_DSP:
		ret = s->dsp;
		break;
	}

	return ret;
}

char **xmp_get_format_list()
{
	return format_list();
}

void xmp_inject_event(xmp_context opaque, int channel, struct xmp_event *e)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct player_data *p = &ctx->p;

	memcpy(&p->inject_event[channel], e, sizeof(struct xmp_event));
	p->inject_event[channel]._flag = 1;
}

