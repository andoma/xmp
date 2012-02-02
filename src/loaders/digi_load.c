/* Extended Module Player
 * Copyright (C) 1996-2012 Claudio Matsuoka and Hipolito Carraro Jr
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU General Public License. See doc/COPYING
 * for more information.
 */

/* Based on the DIGI Booster player v1.6 by Tap (Tomasz Piasta), with the
 * help of Louise Heimann <louise.heimann@softhome.net>. The following
 * DIGI Booster effects are _NOT_ recognized by this player:
 *
 * 8xx robot
 * e00 filter off
 * e01 filter on
 * e30 backwd play sample
 * e31 backwd play sample+loop
 * e50 channel off
 * e51 channel on
 * e8x sample offset 2
 * e9x retrace
 */

#include "load.h"


static int digi_test (FILE *, char *, const int);
static int digi_load (struct xmp_context *, FILE *, const int);

struct xmp_loader_info digi_loader = {
    "DIGI",
    "DIGI Booster",
    digi_test,
    digi_load
};

static int digi_test(FILE *f, char *t, const int start)
{
    char buf[20];

    if (fread(buf, 1, 20, f) < 20)
	return -1;

    if (memcmp(buf, "DIGI Booster module", 19))
	return -1;

    fseek(f, 156, SEEK_CUR);
    fseek(f, 3 * 4 * 32, SEEK_CUR);
    fseek(f, 2 * 1 * 32, SEEK_CUR);

    read_title(f, t, 32);

    return 0;
}


struct digi_header {
    uint8 id[20];		/* ID: "DIGI Booster module\0" */
    uint8 vstr[4];		/* Version string: "Vx.y" */
    uint8 ver;			/* Version hi-nibble.lo-nibble */
    uint8 chn;			/* Number of channels */
    uint8 pack;			/* PackEnable */
    uint8 unknown[19];		/* ??!? */
    uint8 pat;			/* Number of patterns */
    uint8 len;			/* Song length */
    uint8 ord[128];		/* Orders */
    uint32 slen[31];		/* Sample length for 31 samples */
    uint32 sloop[31];		/* Sample loop start for 31 samples */
    uint32 sllen[31];		/* Sample loop length for 31 samples */
    uint8 vol[31];		/* Instrument volumes */
    uint8 fin[31];		/* Finetunes */
    uint8 title[32];		/* Song name */
    uint8 insname[31][30];	/* Instrument names */
};


static int digi_load(struct xmp_context *ctx, FILE *f, const int start)
{
    struct xmp_mod_context *m = &ctx->m;
    struct xmp_event *event = 0;
    struct digi_header dh;
    uint8 digi_event[4], chn_table[64];
    uint16 w;
    int i, j, k, c;

    LOAD_INIT();

    fread(&dh.id, 20, 1, f);

    fread(&dh.vstr, 4, 1, f);
    dh.ver = read8(f);
    dh.chn = read8(f);
    dh.pack = read8(f);
    fread(&dh.unknown, 19, 1, f);
    dh.pat = read8(f);
    dh.len = read8(f);
    fread(&dh.ord, 128, 1, f);

    for (i = 0; i < 31; i++)
	dh.slen[i] = read32b(f);
    for (i = 0; i < 31; i++)
	dh.sloop[i] = read32b(f);
    for (i = 0; i < 31; i++)
	dh.sllen[i] = read32b(f);
    for (i = 0; i < 31; i++)
	dh.vol[i] = read8(f);
    for (i = 0; i < 31; i++)
	dh.fin[i] = read8(f);

    fread(&dh.title, 32, 1, f);

    for (i = 0; i < 31; i++)
        fread(&dh.insname[i], 30, 1, f);

    m->mod.ins = 31;
    m->mod.smp = m->mod.ins;
    m->mod.pat = dh.pat + 1;
    m->mod.chn = dh.chn;
    m->mod.trk = m->mod.pat * m->mod.chn;
    m->mod.len = dh.len + 1;
    m->mod.flg |= XXM_FLG_MODRNG;

    copy_adjust(m->mod.name, dh.title, 32);
    set_type(m, "DIGI (DIGI Booster %-4.4s)", dh.vstr);

    MODULE_INFO();
 
    for (i = 0; i < m->mod.len; i++)
	m->mod.xxo[i] = dh.ord[i];
 
    INSTRUMENT_INIT();

    /* Read and convert instruments and samples */

    for (i = 0; i < m->mod.ins; i++) {
	m->mod.xxi[i].sub = calloc(sizeof (struct xmp_subinstrument), 1);
	m->mod.xxi[i].nsm = !!(m->mod.xxs[i].len = dh.slen[i]);
	m->mod.xxs[i].lps = dh.sloop[i];
	m->mod.xxs[i].lpe = dh.sloop[i] + dh.sllen[i];
	m->mod.xxs[i].flg = m->mod.xxs[i].lpe > 0 ? XMP_SAMPLE_LOOP : 0;
	m->mod.xxi[i].sub[0].vol = dh.vol[i];
	m->mod.xxi[i].sub[0].fin = dh.fin[i];
	m->mod.xxi[i].sub[0].pan = 0x80;
	m->mod.xxi[i].sub[0].sid = i;

	copy_adjust(m->mod.xxi[i].name, dh.insname[i], 30);

	_D(_D_INFO "[%2X] %-30.30s %04x %04x %04x %c V%02x", i,
		m->mod.xxi[i].name, m->mod.xxs[i].len, m->mod.xxs[i].lps, m->mod.xxs[i].lpe,
		m->mod.xxs[i].flg & XMP_SAMPLE_LOOP ? 'L' : ' ', m->mod.xxi[i].sub[0].vol);
    }

    PATTERN_INIT();

    /* Read and convert patterns */
    _D(_D_INFO "Stored patterns: %d", m->mod.pat);

    for (i = 0; i < m->mod.pat; i++) {
	PATTERN_ALLOC (i);
	m->mod.xxp[i]->rows = 64;
	TRACK_ALLOC (i);

	if (dh.pack) {
	    w = (read16b(f) - 64) >> 2;
	    fread (chn_table, 1, 64, f);
	} else {
	    w = 64 * m->mod.chn;
	    memset (chn_table, 0xff, 64);
	}

	for (j = 0; j < 64; j++) {
	    for (c = 0, k = 0x80; c < m->mod.chn; c++, k >>= 1) {
	        if (chn_table[j] & k) {
		    fread (digi_event, 4, 1, f);
		    event = &EVENT (i, c, j);
	            cvt_pt_event (event, digi_event);
		    switch (event->fxt) {
		    case 0x08:		/* Robot */
			event->fxt = event->fxp = 0;
			break;
		    case 0x0e:
			switch (MSN (event->fxp)) {
			case 0x00:
			case 0x03:
			case 0x08:
			case 0x09:
			    event->fxt = event->fxp = 0;
			    break;
			case 0x04:
			    event->fxt = 0x0c;
			    event->fxp = 0x00;
			    break;
			}
		    }
		    w--;
		}
	    }
	}

	if (w) {
	    _D(_D_CRIT "Corrupted file (w = %d)", w);
	}
    }

    /* Read samples */
    _D(_D_INFO "Stored samples: %d", m->mod.smp);
    for (i = 0; i < m->mod.ins; i++) {
	load_patch(ctx, f, m->mod.xxi[i].sub[0].sid, 0,
	    &m->mod.xxs[m->mod.xxi[i].sub[0].sid], NULL);
    }

    return 0;
}
