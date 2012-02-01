/* Extended Module Player
 * Copyright (C) 1996-2012 Claudio Matsuoka and Hipolito Carraro Jr
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU General Public License. See doc/COPYING
 * for more information.
 */

/* Reverse engineered from the two SFX files in the Delitracker mods disk
 * and music from Future Wars, Twinworld and Operation Stealth. Effects
 * must be verified/implemented.
 */

/* From the ExoticRipper docs:
 * [SoundFX 2.0 is] simply the same as SoundFX 1.3, except that it
 * uses 31 samples [instead of 15].
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "period.h"
#include "load.h"

#define MAGIC_SONG	MAGIC4('S','O','N','G')


static int sfx_test (FILE *, char *, const int);
static int sfx_load (struct xmp_context *, FILE *, const int);

struct xmp_loader_info sfx_loader = {
    "SFX",
    "SoundFX",
    sfx_test,
    sfx_load
};

static int sfx_test(FILE *f, char *t, const int start)
{
    uint32 a, b;

    fseek(f, 4 * 15, SEEK_CUR);
    a = read32b(f);
    fseek(f, 4 * 15, SEEK_CUR);
    b = read32b(f);

    if (a != MAGIC_SONG && b != MAGIC_SONG)
	return -1;

    read_title(f, t, 0);

    return 0;
}


struct sfx_ins {
    uint8 name[22];		/* Instrument name */
    uint16 len;			/* Sample length in words */
    uint8 finetune;		/* Finetune */
    uint8 volume;		/* Volume (0-63) */
    uint16 loop_start;		/* Sample loop start in bytes */
    uint16 loop_length;		/* Sample loop length in words */
};

struct sfx_header {
    uint32 magic;		/* 'SONG' */
    uint16 delay;		/* Delay value (tempo), default is 0x38e5 */
    uint16 unknown[7];		/* ? */
};

struct sfx_header2 {
    uint8 len;			/* Song length */
    uint8 restart;		/* Restart pos (?) */
    uint8 order[128];		/* Order list */
};


static int sfx_13_20_load(struct xmp_context *ctx, FILE *f, const int nins, const int start)
{
    struct xmp_mod_context *m = &ctx->m;
    int i, j;
    struct xmp_event *event;
    struct sfx_header sfx;
    struct sfx_header2 sfx2;
    uint8 ev[4];
    int ins_size[31];
    struct sfx_ins ins[31];	/* Instruments */

    LOAD_INIT();

    for (i = 0; i < nins; i++)
	ins_size[i] = read32b(f);

    sfx.magic = read32b(f);
    sfx.delay = read16b(f);
    fread(&sfx.unknown, 14, 1, f);

    if (sfx.magic != MAGIC_SONG)
	return -1;

    m->mod.ins = nins;
    m->mod.smp = m->mod.ins;
    m->mod.bpm = 14565 * 122 / sfx.delay;

    for (i = 0; i < m->mod.ins; i++) {
	fread(&ins[i].name, 22, 1, f);
	ins[i].len = read16b(f);
	ins[i].finetune = read8(f);
	ins[i].volume = read8(f);
	ins[i].loop_start = read16b(f);
	ins[i].loop_length = read16b(f);
    }

    sfx2.len = read8(f);
    sfx2.restart = read8(f);
    fread(&sfx2.order, 128, 1, f);

    m->mod.len = sfx2.len;
    if (m->mod.len > 0x7f)
	return -1;

    memcpy (m->mod.xxo, sfx2.order, m->mod.len);
    for (m->mod.pat = i = 0; i < m->mod.len; i++)
	if (m->mod.xxo[i] > m->mod.pat)
	    m->mod.pat = m->mod.xxo[i];
    m->mod.pat++;

    m->mod.trk = m->mod.chn * m->mod.pat;

    strcpy (m->mod.type, m->mod.ins == 15 ? "SoundFX 1.3" : "SoundFX 2.0");

    MODULE_INFO();

    INSTRUMENT_INIT();

    for (i = 0; i < m->mod.ins; i++) {
	m->mod.xxi[i].sub = calloc(sizeof (struct xmp_subinstrument), 1);
	m->mod.xxi[i].nsm = !!(m->mod.xxs[i].len = ins_size[i]);
	m->mod.xxs[i].lps = ins[i].loop_start;
	m->mod.xxs[i].lpe = m->mod.xxs[i].lps + 2 * ins[i].loop_length;
	m->mod.xxs[i].flg = ins[i].loop_length > 1 ? XMP_SAMPLE_LOOP : 0;
	m->mod.xxi[i].sub[0].vol = ins[i].volume;
	m->mod.xxi[i].sub[0].fin = (int8)(ins[i].finetune << 4); 
	m->mod.xxi[i].sub[0].pan = 0x80;
	m->mod.xxi[i].sub[0].sid = i;

	copy_adjust(m->mod.xxi[i].name, ins[i].name, 22);

	_D(_D_INFO "[%2X] %-22.22s %04x %04x %04x %c  %02x %+d",
		i, m->mod.xxi[i].name, m->mod.xxs[i].len, m->mod.xxs[i].lps, m->mod.xxs[i].lpe,
		m->mod.xxs[i].flg & XMP_SAMPLE_LOOP ? 'L' : ' ', m->mod.xxi[i].sub[0].vol,
		m->mod.xxi[i].sub[0].fin >> 4);
    }

    PATTERN_INIT();

    _D(_D_INFO "Stored patterns: %d", m->mod.pat);

    for (i = 0; i < m->mod.pat; i++) {
	PATTERN_ALLOC(i);
	m->mod.xxp[i]->rows = 64;
	TRACK_ALLOC(i);

	for (j = 0; j < 64 * m->mod.chn; j++) {
	    event = &EVENT(i, j % m->mod.chn, j / m->mod.chn);
	    fread(ev, 1, 4, f);

	    event->note = period_to_note ((LSN (ev[0]) << 8) | ev[1]);
	    event->ins = (MSN (ev[0]) << 4) | MSN (ev[2]);
	    event->fxp = ev[3];

	    switch (LSN(ev[2])) {
	    case 0x1:			/* Arpeggio */
		event->fxt = FX_ARPEGGIO;
		break;
	    case 0x02:			/* Pitch bend */
		if (event->fxp >> 4) {
		    event->fxt = FX_PORTA_DN;
		    event->fxp >>= 4;
		} else if (event->fxp & 0x0f) {
		    event->fxt = FX_PORTA_UP;
		    event->fxp &= 0x0f;
		}
		break;
	    case 0x5:			/* Volume up */
		event->fxt = FX_VOLSLIDE_DN;
		break;
	    case 0x6:			/* Set volume (attenuation) */
		event->fxt = FX_VOLSET;
		event->fxp = 0x40 - ev[3];
		break;
	    case 0x3:			/* LED on */
	    case 0x4:			/* LED off */
	    case 0x7:			/* Set step up */
	    case 0x8:			/* Set step down */
	    default:
		event->fxt = event->fxp = 0;
		break;
	    }
	}
    }

    m->mod.flg |= XXM_FLG_MODRNG;

    /* Read samples */

    _D(_D_INFO "Stored samples: %d", m->mod.smp);

    for (i = 0; i < m->mod.ins; i++) {
	if (m->mod.xxs[i].len <= 2)
	    continue;
	load_patch(ctx, f, i, 0, &m->mod.xxs[i], NULL);
    }

    return 0;
}


static int sfx_load(struct xmp_context *ctx, FILE *f, const int start)
{
    if (sfx_13_20_load(ctx, f, 15, start) < 0)
	return sfx_13_20_load(ctx, f, 31, start);
    return 0;
}
