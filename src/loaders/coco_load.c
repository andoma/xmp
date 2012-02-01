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

#include "load.h"

static int coco_test (FILE *, char *, const int);
static int coco_load (struct xmp_context *, FILE *, const int);

struct xmp_loader_info coco_loader = {
	"COCO",
	"Coconizer",
	coco_test,
	coco_load
};

static int check_cr(uint8 *s, int n)
{
	while (n--) {
		if (*s++ == 0x0d)
			return 0;
	}

	return -1;
}

static int coco_test(FILE *f, char *t, const int start)
{
	uint8 x, buf[20];
	uint32 y;
	int n, i;

	x = read8(f);

	/* check number of channels */
	if (x != 0x84 && x != 0x88)
		return -1;

	fread(buf, 1, 20, f);		/* read title */
	if (check_cr(buf, 20) != 0)
		return -1;

	n = read8(f);			/* instruments */
	if (n > 100)
		return -1;

	read8(f);			/* sequences */
	read8(f);			/* patterns */

	y = read32l(f);
	if (y < 64 || y > 0x00100000)	/* offset of sequence table */
		return -1;

	y = read32l(f);			/* offset of patterns */
	if (y < 64 || y > 0x00100000)
		return -1;

	for (i = 0; i < n; i++) {
		int ofs = read32l(f);
		int len = read32l(f);
		int vol = read32l(f);
		int lps = read32l(f);
		int lsz = read32l(f);

		if (ofs < 64 || ofs > 0x00100000)
			return -1;

		if (vol > 0xff)
			return -1;

		if (len > 0x00100000 || lps > 0x00100000 || lsz > 0x00100000)
			return -1;

		if (lps + lsz - 1 > len)
			return -1;

		fread(buf, 1, 11, f);
		if (check_cr(buf, 11) != 0)
			return -1;

		read8(f);	/* unused */
	}

	fseek(f, start + 1, SEEK_SET);
	read_title(f, t, 20);

#if 0
	for (i = 0; i < 20; i++) {
		if (t[i] == 0x0d)
			t[i] = 0;
	}
#endif
	
	return 0;
}


static void fix_effect(struct xxm_event *e)
{
	switch (e->fxt) {
	case 0x00:			/* 00 xy Normal play or Arpeggio */
		e->fxt = FX_ARPEGGIO;
		/* x: first halfnote to add
		   y: second halftone to subtract */
		break;
	case 0x01:			/* 01 xx Slide Up */
	case 0x05:
		e->fxt = FX_PORTA_UP;
		break;
	case 0x02:			/* 02 xx Slide Down */
	case 0x06:
		e->fxt = FX_PORTA_DN;
		break;
	case 0x03:
		e->fxt = FX_VOLSLIDE_UP;	/* FIXME: it's fine */
		break;
	case 0x04:
		e->fxt = FX_VOLSLIDE_DN;	/* FIXME: it's fine */
		break;
	case 0x07:
		e->fxt = FX_SETPAN;
		break;
	case 0x08:			/* FIXME */
	case 0x09:
	case 0x0a:
	case 0x0b:
		e->fxt = e->fxp = 0;
		break;
	case 0x0c:
		e->fxt = FX_VOLSET;
		e->fxp = 0xff - e->fxp;
		break;
	case 0x0d:
		e->fxt = FX_BREAK;
		break;
	case 0x0e:
		e->fxt = FX_JUMP;
		break;
	case 0x0f:
		e->fxt = FX_TEMPO;
		break;
	case 0x10:			/* unused */
		e->fxt = e->fxp = 0;
		break;
	case 0x11:
	case 0x12:			/* FIXME */
		e->fxt = e->fxp = 0;
		break;
	case 0x13:
		e->fxt = FX_VOLSLIDE_UP;
		break;
	case 0x14:
		e->fxt = FX_VOLSLIDE_DN;
		break;
	default:
		e->fxt = e->fxp = 0;
	}
}

static int coco_load(struct xmp_context *ctx, FILE *f, const int start)
{
	struct xmp_player_context *p = &ctx->p;
	struct xmp_mod_context *m = &p->m;
	struct xxm_event *event;
	int i, j;
	int seq_ptr, pat_ptr, smp_ptr[100];

	LOAD_INIT();

	m->xxh->chn = read8(f) & 0x3f;
	read_title(f, m->name, 20);

	for (i = 0; i < 20; i++) {
		if (m->name[i] == 0x0d)
			m->name[i] = 0;
	}

	strcpy(m->type, "Coconizer");

	m->xxh->ins = m->xxh->smp = read8(f);
	m->xxh->len = read8(f);
	m->xxh->pat = read8(f);
	m->xxh->trk = m->xxh->pat * m->xxh->chn;

	seq_ptr = read32l(f);
	pat_ptr = read32l(f);

	MODULE_INFO();
	INSTRUMENT_INIT();

	m->vol_table = arch_vol_table;
	m->volbase = 0xff;

	for (i = 0; i < m->xxh->ins; i++) {
		m->xxi[i].sub = calloc(sizeof (struct xxm_subinstrument), 1);

		smp_ptr[i] = read32l(f);
		m->xxs[i].len = read32l(f);
		m->xxi[i].sub[0].vol = 0xff - read32l(f);
		m->xxi[i].sub[0].pan = 0x80;
		m->xxs[i].lps = read32l(f);
                m->xxs[i].lpe = m->xxs[i].lps + read32l(f);
		if (m->xxs[i].lpe)
			m->xxs[i].lpe -= 1;
		m->xxs[i].flg = m->xxs[i].lps > 0 ?  XMP_SAMPLE_LOOP : 0;
		fread(m->xxi[i].name, 1, 11, f);
		for (j = 0; j < 11; j++) {
			if (m->xxi[i].name[j] == 0x0d)
				m->xxi[i].name[j] = 0;
		}
		read8(f);	/* unused */

		m->xxi[i].nsm = !!m->xxs[i].len;
		m->xxi[i].sub[0].sid = i;

		_D(_D_INFO "[%2X] %-10.10s  %05x %05x %05x %c V%02x",
				i, m->xxi[i].name,
				m->xxs[i].len, m->xxs[i].lps, m->xxs[i].lpe,
				m->xxs[i].flg & XMP_SAMPLE_LOOP ? 'L' : ' ',
				m->xxi[i].sub[0].vol);
	}

	/* Sequence */

	fseek(f, start + seq_ptr, SEEK_SET);
	for (i = 0; ; i++) {
		uint8 x = read8(f);
		if (x == 0xff)
			break;
		m->xxo[i] = x;
	}
	for (i++; i % 4; i++)	/* for alignment */
		read8(f);


	/* Patterns */

	PATTERN_INIT();

	_D(_D_INFO "Stored patterns: %d", m->xxh->pat);

	for (i = 0; i < m->xxh->pat; i++) {
		PATTERN_ALLOC (i);
		m->xxp[i]->rows = 64;
		TRACK_ALLOC (i);

		for (j = 0; j < (64 * m->xxh->chn); j++) {
			event = &EVENT (i, j % m->xxh->chn, j / m->xxh->chn);
			event->fxp = read8(f);
			event->fxt = read8(f);
			event->ins = read8(f);
			event->note = read8(f);

			fix_effect(event);
		}
	}

	/* Read samples */

	_D(_D_INFO "Stored samples : %d", m->xxh->smp);

	for (i = 0; i < m->xxh->ins; i++) {
		if (m->xxi[i].nsm == 0)
			continue;

		fseek(f, start + smp_ptr[i], SEEK_SET);
		xmp_drv_loadpatch(ctx, f, m->xxi[i].sub[0].sid,
				XMP_SMP_VIDC, &m->xxs[m->xxi[i].sub[0].sid], NULL);
	}

	for (i = 0; i < m->xxh->chn; i++)
		m->xxc[i].pan = (((i + 3) / 2) % 2) * 0xff;

	return 0;
}
