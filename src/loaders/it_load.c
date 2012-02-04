/* Extended Module Player
 * Copyright (C) 1996-2012 Claudio Matsuoka and Hipolito Carraro Jr.
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU General Public License. See doc/COPYING
 * for more information.
 */

#include <time.h>
#include "load.h"
#include "it.h"
#include "period.h"

#define MAGIC_IMPM	MAGIC4('I','M','P','M')
#define MAGIC_IMPS	MAGIC4('I','M','P','S')


static int it_test (FILE *, char *, const int);
static int it_load (struct xmp_context *, FILE *, const int);

struct xmp_loader_info it_loader = {
    "IT",
    "Impulse Tracker",
    it_test,
    it_load
};

#ifdef WIN32
// FIXME: not thread-safe
struct tm *localtime_r(const time_t *timep, struct tm *result)
{
    memcpy(result, localtime(timep), sizeof(struct tm));
    return result;
}
#endif

static int it_test(FILE *f, char *t, const int start)
{
    if (read32b(f) != MAGIC_IMPM)
	return -1;

    read_title(f, t, 26);

    return 0;
}


#define	FX_NONE	0xff
#define FX_XTND 0xfe
#define L_CHANNELS 64


static uint32 *pp_ins;		/* Pointers to instruments */
static uint32 *pp_smp;		/* Pointers to samples */
static uint32 *pp_pat;		/* Pointers to patterns */
static uint8 arpeggio_val[64];
static uint8 last_h[64], last_fxp[64];

static uint8 fx[] = {
	/*   */ FX_NONE,
	/* A */ FX_S3M_TEMPO,
	/* B */ FX_JUMP,
	/* C */ FX_BREAK,
	/* D */ FX_VOLSLIDE,
	/* E */ FX_PORTA_DN,
	/* F */ FX_PORTA_UP,
	/* G */ FX_TONEPORTA,
	/* H */ FX_VIBRATO,
	/* I */ FX_TREMOR,
	/* J */ FX_ARPEGGIO,
	/* K */ FX_VIBRA_VSLIDE,
	/* L */ FX_TONE_VSLIDE,
	/* M */ FX_TRK_VOL,
	/* N */ FX_TRK_VSLIDE,
	/* O */ FX_OFFSET,
	/* P */ FX_NONE,
	/* Q */ FX_MULTI_RETRIG,
	/* R */ FX_TREMOLO,
	/* S */ FX_XTND,
	/* T */ FX_IT_BPM,
	/* U */ FX_NONE,
	/* V */ FX_GLOBALVOL,
	/* W */ FX_G_VOLSLIDE,
	/* X */ FX_NONE,
	/* Y */ FX_NONE,
	/* Z */ FX_FLT_CUTOFF
};

static int dca2nna[] = { 0, 2, 3 };
static int new_fx;

int itsex_decompress8 (FILE *, void *, int, int);
int itsex_decompress16 (FILE *, void *, int, int);


static void xlat_fx(int c, struct xmp_event *e)
{
    uint8 h = MSN(e->fxp), l = LSN(e->fxp);

    switch (e->fxt = fx[e->fxt]) {
    case FX_ARPEGGIO:		/* Arpeggio */
	if (e->fxp)
	    arpeggio_val[c] = e->fxp;
	else
	    e->fxp = arpeggio_val[c];
	break;
    case FX_JUMP:
	e->fxp = ord_xlat[e->fxp];
	break;
    case FX_VIBRATO:		/* Old or new vibrato */
	if (new_fx)
	    e->fxt = FX_FINE2_VIBRA;
	break;
    case FX_XTND:		/* Extended effect */
	e->fxt = FX_EXTENDED;

	if (h == 0 && e->fxp == 0) {
	    h = last_h[c];
	    e->fxp = last_fxp[c];
	} else {
	    last_h[c] = h;
	    last_fxp[c] = e->fxp;
	}

	switch (h) {
	case 0x1:		/* Glissando */
	    e->fxp = 0x30 | l;
	    break;
	case 0x2:		/* Finetune */
	    e->fxp = 0x50 | l;
	    break;
	case 0x3:		/* Vibrato wave */
	    e->fxp = 0x40 | l;
	    break;
	case 0x4:		/* Tremolo wave */
	    e->fxp = 0x70 | l;
	    break;
	case 0x5:		/* Panbrello wave -- NOT IMPLEMENTED */
	    e->fxt = e->fxp = 0;
	    break;
	case 0x6:		/* Pattern delay */
	    e->fxp = 0xe0 | l;
	    break;
	case 0x7:		/* Instrument functions */
	    e->fxt = FX_IT_INSTFUNC;
	    e->fxp &= 0x0f;
	    break;
	case 0x8:		/* Set pan position */
	    e->fxt = FX_SETPAN;
	    e->fxp = l << 4;
	    break;
	case 0x9:		/* 0x91 = set surround -- NOT IMPLEMENTED */
	    e->fxt = e->fxp = 0;
	    break;
	case 0xb:		/* Pattern loop */
	    e->fxp = 0x60 | l;
	    break;
	case 0xc:		/* Note cut */
	case 0xd:		/* Note delay */
	    if ((e->fxp = l) == 0)
		e->fxp++;	/* SD0 and SC0 becomes SD1 and SC1 */
	    e->fxp |= h << 4;
	    break;
	case 0xe:		/* Pattern delay */
	    break;
	default:
	    e->fxt = e->fxp = 0;
	}
	break;
    case FX_FLT_CUTOFF:
	if (e->fxp > 0x7f && e->fxp < 0x90) {	/* Resonance */
	    e->fxt = FX_FLT_RESN;
	    e->fxp = (e->fxp - 0x80) * 16;
	} else {		/* Cutoff */
	    e->fxp *= 2;
	}
	break;
    case FX_TREMOR:
	if (!LSN(e->fxp))
	    e->fxp |= 0x01; 
	if (!MSN(e->fxp))
	    e->fxp |= 0x10; 
	break;
    case FX_NONE:		/* No effect */
	e->fxt = e->fxp = 0;
	break;
    }
}


static void xlat_volfx(struct xmp_event *event)
{
    int b;

    b = event->vol;
    event->vol = 0;

    if (b <= 0x40) {
	event->vol = b + 1;
    } else if (b >= 65 && b <= 74) {
	event->f2t = FX_EXTENDED;
	event->f2p = (EX_F_VSLIDE_UP << 4) | (b - 65);
    } else if (b >= 75 && b <= 84) {
	event->f2t = FX_EXTENDED;
	event->f2p = (EX_F_VSLIDE_DN << 4) | (b - 75);
    } else if (b >= 85 && b <= 94) {
	event->f2t = FX_VOLSLIDE_2;
	event->f2p = (b - 85) << 4;
    } else if (b >= 95 && b <= 104) {
	event->f2t = FX_VOLSLIDE_2;
	event->f2p = b - 95;
    } else if (b >= 105 && b <= 114) {
	event->f2t = FX_PORTA_DN;
	event->f2p = (b - 105) << 2;
    } else if (b >= 115 && b <= 124) {
	event->f2t = FX_PORTA_UP;
	event->f2p = (b - 115) << 2;
    } else if (b >= 128 && b <= 192) {
	if (b == 192)
	    b = 191;
	event->f2t = FX_SETPAN;
	event->f2p = (b - 128) << 2;
    } else if (b >= 193 && b <= 202) {
	event->f2t = FX_TONEPORTA;
	event->f2p = 1 << (b - 193);
#if 0
    } else if (b >= 193 && b <= 202) {
	event->f2t = FX_VIBRATO;
	event->f2p = ???;
#endif
    }
}


static void fix_name(uint8 *s, int l)
{
    int i;

    /* IT names can have 0 at start of data, replace with space */
    for (l--, i = 0; i < l; i++) {
	if (s[i] == 0)
	    s[i] = ' ';
    }
    for (i--; i >= 0 && s[i] == ' '; i--) {
	if (s[i] == ' ')
	    s[i] = 0;
    }
}


static int it_load(struct xmp_context *ctx, FILE *f, const int start)
{
    struct xmp_mod_context *m = &ctx->m;
    struct xmp_module *mod = &m->mod;
    struct xmp_options *o = &ctx->o;
    int r, c, i, j, k, pat_len;
    struct xmp_event *event, dummy, lastevent[L_CHANNELS];
    struct it_file_header ifh;
    struct it_instrument1_header i1h;
    struct it_instrument2_header i2h;
    struct it_sample_header ish;
    struct it_envelope env;
    uint8 b, mask[L_CHANNELS];
    int max_ch, flag;
    int inst_map[120], inst_rmap[XXM_KEY_MAX];
    char tracker_name[40];

    LOAD_INIT();

    /* Load and convert header */
    read32b(f);		/* magic */

    fread(&ifh.name, 26, 1, f);
    ifh.hilite_min = read8(f);
    ifh.hilite_maj = read8(f);

    ifh.ordnum = read16l(f);
    ifh.insnum = read16l(f);
    ifh.smpnum = read16l(f);
    ifh.patnum = read16l(f);

    ifh.cwt = read16l(f);
    ifh.cmwt = read16l(f);
    ifh.flags = read16l(f);
    ifh.special = read16l(f);

    ifh.gv = read8(f);
    ifh.mv = read8(f);
    ifh.is = read8(f);
    ifh.it = read8(f);
    ifh.sep = read8(f);
    ifh.pwd = read8(f);

    ifh.msglen = read16l(f);
    ifh.msgofs = read32l(f);
    ifh.rsvd = read32l(f);

    fread(&ifh.chpan, 64, 1, f);
    fread(&ifh.chvol, 64, 1, f);

    strncpy(mod->name, (char *)ifh.name, XMP_NAMESIZE);
    mod->len = ifh.ordnum;
    mod->ins = ifh.insnum;
    mod->smp = ifh.smpnum;
    mod->pat = ifh.patnum;
    pp_ins = mod->ins ? calloc(4, mod->ins) : NULL;
    pp_smp = calloc(4, mod->smp);
    pp_pat = calloc(4, mod->pat);
    mod->tpo = ifh.is;
    mod->bpm = ifh.it;
    mod->flg = ifh.flags & IT_LINEAR_FREQ ? XXM_FLG_LINEAR : 0;
    mod->flg |= (ifh.flags & IT_USE_INST) && (ifh.cmwt >= 0x200) ?
					XXM_FLG_INSVOL : 0;

    mod->chn = 64;	/* Effects in muted channels are still processed! */

    for (i = 0; i < 64; i++) {
	if (ifh.chpan[i] == 100)	/* Surround -> center */
	    ifh.chpan[i] = 32;

	if (ifh.chpan[i] & 0x80) {	/* Channel mute */
	    ifh.chvol[i] = 0;
	    mod->xxc[i].flg |= XXM_CHANNEL_MUTE;
	}

	if (ifh.flags & IT_STEREO) {
	    mod->xxc[i].pan = (int)ifh.chpan[i] * 0x80 >> 5;
	    if (mod->xxc[i].pan > 0xff)
		mod->xxc[i].pan = 0xff;
	} else {
	    mod->xxc[i].pan = 0x80;
	}

	mod->xxc[i].vol = ifh.chvol[i];
    }
    fread(mod->xxo, 1, mod->len, f);
    clean_s3m_seq(&m->mod, mod->xxo);

    new_fx = ifh.flags & IT_OLD_FX ? 0 : 1;

    /* S3M skips pattern 0xfe */
    for (i = 0; i < (mod->len - 1); i++) {
	if (mod->xxo[i] == 0xfe) {
	    memmove(&mod->xxo[i], &mod->xxo[i + 1], mod->len - i - 1);
	    mod->len--;
	}
    }
    for (i = 0; i < mod->ins; i++)
	pp_ins[i] = read32l(f);
    for (i = 0; i < mod->smp; i++)
	pp_smp[i] = read32l(f);
    for (i = 0; i < mod->pat; i++)
	pp_pat[i] = read32l(f);

    m->c4rate = C4_NTSC_RATE;
    m->quirk |= QUIRK_FINEFX | QUIRK_ENVFADE;

    /* Identify tracker */

    switch (ifh.cwt >> 8) {
    case 0x00:
	strcpy(tracker_name, "unmo3");
	break;
    case 0x01:
    case 0x02:		/* test from Schism Tracker sources */
	if (ifh.cmwt == 0x0200 && ifh.cwt == 0x0214
		&& ifh.flags == 9 && ifh.special == 0
		&& ifh.hilite_maj == 0 && ifh.hilite_min == 0
		&& ifh.insnum == 0 && ifh.patnum + 1 == ifh.ordnum
		&& ifh.gv == 128 && ifh.mv == 100 && ifh.is == 1
		&& ifh.sep == 128 && ifh.pwd == 0
		&& ifh.msglen == 0 && ifh.msgofs == 0 && ifh.rsvd == 0)
	{
                strcpy(tracker_name, "OpenSPC conversion");
	} else if (ifh.cmwt == 0x0200 && ifh.cwt == 0x0217) {
	    strcpy(tracker_name, "ModPlug Tracker 1.16");
	    /* ModPlug Tracker files aren't really IMPM 2.00 */
	    ifh.cmwt = ifh.flags & IT_USE_INST ? 0x214 : 0x100;	
	} else if (ifh.cwt == 0x0216) {
	    strcpy(tracker_name, "Impulse Tracker 2.14v3");
	} else if (ifh.cwt == 0x0217) {
	    strcpy(tracker_name, "Impulse Tracker 2.14v5");
	} else if (ifh.cwt == 0x0214 && !memcmp(&ifh.rsvd, "CHBI", 4)) {
	    strcpy(tracker_name, "Chibi Tracker");
	} else {
	    snprintf(tracker_name, 40, "Impulse Tracker %d.%02x",
			(ifh.cwt & 0x0f00) >> 8, ifh.cwt & 0xff);
	}
	break;
    case 0x08:
    case 0x7f:
	if (ifh.cwt == 0x0888) {
	    strcpy(tracker_name, "OpenMPT 1.17+");
	} else if (ifh.cwt == 0x7fff) {
	    strcpy(tracker_name, "munch.py");
	} else {
	    snprintf(tracker_name, 40, "unknown (%04x)", ifh.cwt);
	}
	break;
    default:
	switch (ifh.cwt >> 12) {
	case 0x1: {
	    uint16 cwtv = ifh.cwt & 0x0fff;
	    struct tm version;
	    time_t version_sec;

	    if (cwtv > 0x50) {
		version_sec = ((cwtv - 0x050) * 86400) + 1254355200;
		if (localtime_r(&version_sec, &version)) {
		    snprintf(tracker_name, 40, "Schism Tracker %04d-%02d-%02d",
				version.tm_year + 1900, version.tm_mon + 1,
				version.tm_mday);
                }
	    } else {
	    	snprintf(tracker_name, 40, "Schism Tracker 0.%x", cwtv);
	    }
	    break; }
	case 0x5:
	    snprintf(tracker_name, 40, "OpenMPT %d.%02x",
			(ifh.cwt & 0x0f00) >> 8, ifh.cwt & 0xff);
	    if (memcmp(&ifh.rsvd, "OMPT", 4))
		strncat(tracker_name, " (compat.)", 40);
	    break;
	default:
	    snprintf(tracker_name, 40, "unknown (%04x)", ifh.cwt);
	}
    }

    set_type(m, "IMPM %d.%02x (%s)",
			ifh.cmwt >> 8, ifh.cmwt & 0xff, tracker_name);

    MODULE_INFO();

    _D(_D_INFO "Instrument/FX mode: %s/%s",
			ifh.flags & IT_USE_INST ? ifh.cmwt >= 0x200 ?
			"new" : "old" : "sample",
			ifh.flags & IT_OLD_FX ? "old" : "IT");

    if (~ifh.flags & IT_USE_INST)
	mod->ins = mod->smp;

    if (ifh.special & IT_HAS_MSG) {
	if ((m->comment = malloc(ifh.msglen + 1)) == NULL)
	    return -1;
	i = ftell(f);
	fseek(f, start + ifh.msgofs, SEEK_SET);

	_D(_D_INFO "Message length : %d", ifh.msglen);

	for (j = 0; j < ifh.msglen; j++) {
	    b = read8(f);
	    if (b == '\r')
		b = '\n';
	    if ((b < 32 || b > 127) && b != '\n' && b != '\t')
		b = '.';
	    m->comment[j] = b;
	}
	m->comment[j] = 0;

	fseek(f, i, SEEK_SET);
    }

    INSTRUMENT_INIT();

    _D(_D_INFO "Instruments: %d", mod->ins);

    for (i = 0; i < mod->ins; i++) {
	/*
	 * IT files can have three different instrument types: 'New'
	 * instruments, 'old' instruments or just samples. We need a
	 * different loader for each of them.
	 */

	if ((ifh.flags & IT_USE_INST) && (ifh.cmwt >= 0x200)) {
	    /* New instrument format */
	    fseek(f, start + pp_ins[i], SEEK_SET);

	    i2h.magic = read32b(f);
	    fread(&i2h.dosname, 12, 1, f);
	    i2h.zero = read8(f);
	    i2h.nna = read8(f);
	    i2h.dct = read8(f);
	    i2h.dca = read8(f);
	    i2h.fadeout = read16l(f);

	    i2h.pps = read8(f);
	    i2h.ppc = read8(f);
	    i2h.gbv = read8(f);
	    i2h.dfp = read8(f);
	    i2h.rv = read8(f);
	    i2h.rp = read8(f);
	    i2h.trkvers = read16l(f);

	    i2h.nos = read8(f);
	    i2h.rsvd1 = read8(f);
	    fread(&i2h.name, 26, 1, f);

	    fix_name(i2h.name, 26);

	    i2h.ifc = read8(f);
	    i2h.ifr = read8(f);
	    i2h.mch = read8(f);
	    i2h.mpr = read8(f);
	    i2h.mbnk = read16l(f);
	    fread(&i2h.keys, 240, 1, f);

	    copy_adjust(mod->xxi[i].name, i2h.name, 24);
	    mod->xxi[i].rls = i2h.fadeout << 6;

	    /* Envelopes */

#define BUILD_ENV(X) { \
            env.flg = read8(f); \
            env.num = read8(f); \
            env.lpb = read8(f); \
            env.lpe = read8(f); \
            env.slb = read8(f); \
            env.sle = read8(f); \
            for (j = 0; j < 25; j++) { \
            	env.node[j].y = read8(f); \
            	env.node[j].x = read16l(f); \
            } \
            env.unused = read8(f); \
	    mod->xxi[i].X##ei.flg = env.flg & IT_ENV_ON ? XXM_ENV_ON : 0; \
	    mod->xxi[i].X##ei.flg |= env.flg & IT_ENV_LOOP ? XXM_ENV_LOOP : 0; \
	    mod->xxi[i].X##ei.flg |= env.flg & IT_ENV_SLOOP ? (XXM_ENV_SUS|XXM_ENV_SLOOP) : 0; \
	    mod->xxi[i].X##ei.npt = env.num; \
	    mod->xxi[i].X##ei.sus = env.slb; \
	    mod->xxi[i].X##ei.sue = env.sle; \
	    mod->xxi[i].X##ei.lps = env.lpb; \
	    mod->xxi[i].X##ei.lpe = env.lpe; \
	    for (j = 0; j < env.num; j++) { \
		mod->xxi[i].X##ei.data[j * 2] = env.node[j].x; \
		mod->xxi[i].X##ei.data[j * 2 + 1] = env.node[j].y; \
	    } \
}

	    BUILD_ENV(a);
	    BUILD_ENV(p);
	    BUILD_ENV(f);
	    
	    if (mod->xxi[i].pei.flg & XXM_ENV_ON)
		for (j = 0; j < mod->xxi[i].pei.npt; j++)
		    mod->xxi[i].pei.data[j * 2 + 1] += 32;

	    if (env.flg & IT_ENV_FILTER) {
		mod->xxi[i].fei.flg |= XXM_ENV_FLT;
		for (j = 0; j < env.num; j++) {
		    mod->xxi[i].fei.data[j * 2 + 1] += 32;
		    mod->xxi[i].fei.data[j * 2 + 1] *= 4;
		}
	    } else {
		/* Pitch envelope is *50 to get fine interpolation */
		for (j = 0; j < env.num; j++)
		    mod->xxi[i].fei.data[j * 2 + 1] *= 50;
	    }

	    /* See how many different instruments we have */
	    for (j = 0; j < XXM_KEY_MAX; j++)
		inst_map[j] = -1;

	    for (k = j = 0; j < XXM_KEY_MAX; j++) {
		c = i2h.keys[25 + j * 2] - 1;
		if (c < 0) {
		    mod->xxi[i].map[j].ins = 0xff;	/* No sample */
		    mod->xxi[i].map[j].xpo = 0;
		    continue;
		}
		if (inst_map[c] == -1) {
		    inst_map[c] = k;
		    inst_rmap[k] = c;
		    k++;
		}
		mod->xxi[i].map[j].ins = inst_map[c];
		mod->xxi[i].map[j].xpo = i2h.keys[24 + j * 2] - (j + 12);
	    }

	    mod->xxi[i].nsm = k;
	    mod->xxi[i].vol = i2h.gbv >> 1;

	    if (k) {
		mod->xxi[i].sub = calloc(sizeof (struct xmp_subinstrument), k);
		for (j = 0; j < k; j++) {
		    mod->xxi[i].sub[j].sid = inst_rmap[j];
		    mod->xxi[i].sub[j].nna = i2h.nna;
		    mod->xxi[i].sub[j].dct = i2h.dct;
		    mod->xxi[i].sub[j].dca = dca2nna[i2h.dca & 0x03];
		    mod->xxi[i].sub[j].pan = i2h.dfp & 0x80 ? 0x80 : i2h.dfp * 4;
		    mod->xxi[i].sub[j].ifc = i2h.ifc;
		    mod->xxi[i].sub[j].ifr = i2h.ifr;
	        }
	    }

	    _D(_D_INFO
			"[%2X] %-26.26s %-4.4s %-4.4s %-4.4s %4d %4d  %2x "
			"%02x %c%c%c %3d %02x %02x",
		i, i2h.name,
		i2h.nna < 4 ? nna[i2h.nna] : "none",
		i2h.dct < 4 ? dct[i2h.dct] : "none",
		i2h.dca < 3 ? nna[dca2nna[i2h.dca]] : "none",
		i2h.fadeout,
		i2h.gbv,
		i2h.dfp & 0x80 ? 0x80 : i2h.dfp * 4,
		i2h.rv,
		mod->xxi[i].aei.flg & XXM_ENV_ON ? 'V' : '-',
		mod->xxi[i].pei.flg & XXM_ENV_ON ? 'P' : '-',
		env.flg & 0x01 ? env.flg & 0x80 ? 'F' : 'P' : '-',
		mod->xxi[i].nsm,
		i2h.ifc,
		i2h.ifr
	    );

	} else if (ifh.flags & IT_USE_INST) {
/* Old instrument format */
	    fseek(f, start + pp_ins[i], SEEK_SET);

	    i1h.magic = read32b(f);
	    fread(&i1h.dosname, 12, 1, f);

	    i1h.zero = read8(f);
	    i1h.flags = read8(f);
	    i1h.vls = read8(f);
	    i1h.vle = read8(f);
	    i1h.sls = read8(f);
	    i1h.sle = read8(f);
	    i1h.rsvd1 = read16l(f);
	    i1h.fadeout = read16l(f);

	    i1h.nna = read8(f);
	    i1h.dnc = read8(f);
	    i1h.trkvers = read16l(f);
	    i1h.nos = read8(f);
	    i1h.rsvd2 = read8(f);

	    fread(&i1h.name, 26, 1, f);

	    fix_name(i1h.name, 26);

	    fread(&i1h.rsvd3, 6, 1, f);
	    fread(&i1h.keys, 240, 1, f);
	    fread(&i1h.epoint, 200, 1, f);
	    fread(&i1h.enode, 50, 1, f);

	    copy_adjust(mod->xxi[i].name, i1h.name, 24);

	    mod->xxi[i].rls = i1h.fadeout << 7;

	    mod->xxi[i].aei.flg = i1h.flags & IT_ENV_ON ? XXM_ENV_ON : 0;
	    mod->xxi[i].aei.flg |= i1h.flags & IT_ENV_LOOP ? XXM_ENV_LOOP : 0;
	    mod->xxi[i].aei.flg |= i1h.flags & IT_ENV_SLOOP ? (XXM_ENV_SUS|XXM_ENV_SLOOP) : 0;
	    mod->xxi[i].aei.lps = i1h.vls;
	    mod->xxi[i].aei.lpe = i1h.vle;
	    mod->xxi[i].aei.sus = i1h.sls;
	    mod->xxi[i].aei.sue = i1h.sle;

	    for (k = 0; i1h.enode[k * 2] != 0xff; k++);
	    for (mod->xxi[i].aei.npt = k; k--; ) {
		mod->xxi[i].aei.data[k * 2] = i1h.enode[k * 2];
		mod->xxi[i].aei.data[k * 2 + 1] = i1h.enode[k * 2 + 1];
	    }
	    
	    /* See how many different instruments we have */
	    for (j = 0; j < XXM_KEY_MAX; j++)
		inst_map[j] = -1;

	    for (k = j = 0; j < XXM_KEY_MAX; j++) {
		c = i1h.keys[25 + j * 2] - 1;
		if (c < 0) {
		    mod->xxi[i].map[j].ins = 0xff;	/* No sample */
		    mod->xxi[i].map[j].xpo = 0;
		    continue;
		}
		if (inst_map[c] == -1) {
		    inst_map[c] = k;
		    inst_rmap[k] = c;
		    k++;
		}
		mod->xxi[i].map[j].ins = inst_map[c];
		mod->xxi[i].map[j].xpo = i1h.keys[24 + j * 2] - (j + 12);
	    }

	    mod->xxi[i].nsm = k;
	    mod->xxi[i].vol = i2h.gbv >> 1;

	    if (k) {
		mod->xxi[i].sub = calloc(sizeof (struct xmp_subinstrument), k);
		for (j = 0; j < k; j++) {
		    mod->xxi[i].sub[j].sid = inst_rmap[j];
		    mod->xxi[i].sub[j].nna = i1h.nna;
		    mod->xxi[i].sub[j].dct = i1h.dnc ? XXM_DCT_NOTE : XXM_DCT_OFF;
		    mod->xxi[i].sub[j].dca = XXM_DCA_CUT;
		    mod->xxi[i].sub[j].pan = 0x80;
	        }
	    }

	    _D(_D_INFO "[%2X] %-26.26s %-4.4s %-4.4s %4d %2d %c%c%c %3d",
		i, i1h.name,
		i1h.nna < 4 ? nna[i1h.nna] : "none",
		i1h.dnc ? "on" : "off",
		i1h.fadeout,
		mod->xxi[i].aei.npt,
		mod->xxi[i].aei.flg & XXM_ENV_ON ? 'V' : '-',
		mod->xxi[i].aei.flg & XXM_ENV_LOOP ? 'L' : '-',
		mod->xxi[i].aei.flg & XXM_ENV_SUS ? 'S' : '-',
		mod->xxi[i].nsm
	    );
	}
    }

    _D(_D_INFO "Stored Samples: %d", mod->smp);

    for (i = 0; i < mod->smp; i++) {
	if (~ifh.flags & IT_USE_INST)
	    mod->xxi[i].sub = calloc(sizeof (struct xmp_subinstrument), 1);
	fseek(f, start + pp_smp[i], SEEK_SET);

	ish.magic = read32b(f);
	fread(&ish.dosname, 12, 1, f);
	ish.zero = read8(f);
	ish.gvl = read8(f);
	ish.flags = read8(f);
	ish.vol = read8(f);
	fread(&ish.name, 26, 1, f);

	fix_name(ish.name, 26);

	ish.convert = read8(f);
	ish.dfp = read8(f);
	ish.length = read32l(f);
	ish.loopbeg = read32l(f);
	ish.loopend = read32l(f);
	ish.c5spd = read32l(f);
	ish.sloopbeg = read32l(f);
	ish.sloopend = read32l(f);
	ish.sample_ptr = read32l(f);

	ish.vis = read8(f);
	ish.vid = read8(f);
	ish.vir = read8(f);
	ish.vit = read8(f);

	/* Changed to continue to allow use-brdg.it and use-funk.it to
	 * load correctly (both IT 2.04)
	 */
	if (ish.magic != MAGIC_IMPS)
	    continue;
	
	if (ish.flags & IT_SMP_16BIT) {
	    mod->xxs[i].flg = XMP_SAMPLE_16BIT;
	}
	mod->xxs[i].len = ish.length;
	mod->xxs[i].lps = ish.loopbeg;
	mod->xxs[i].lpe = ish.loopend;

	mod->xxs[i].flg |= ish.flags & IT_SMP_LOOP ? XMP_SAMPLE_LOOP : 0;
	mod->xxs[i].flg |= ish.flags & IT_SMP_BLOOP ? XMP_SAMPLE_LOOP_BIDIR : 0;

	if (~ifh.flags & IT_USE_INST) {
	    /* Create an instrument for each sample */
	    mod->xxi[i].sub[0].vol = ish.vol;
	    mod->xxi[i].sub[0].pan = 0x80;
	    mod->xxi[i].sub[0].sid = i;
	    mod->xxi[i].nsm = !!(mod->xxs[i].len);
	    copy_adjust(mod->xxi[i].name, ish.name, 24);
	} else {
	    copy_adjust(mod->xxs[i].name, ish.name, 24);
	}

#define MAX(x) ((x) > 0xfffff ? 0xfffff : (x))

	_D(_D_INFO "\n[%2X] %-26.26s %05x%c%05x %05x %05x %05x "
		    "%02x%02x %02x%02x %5d ",
		    i, ifh.flags & IT_USE_INST ?
				mod->xxi[i].name : mod->xxs[i].name,
		    mod->xxs[i].len,
		    ish.flags & IT_SMP_16BIT ? '+' : ' ',
		    MAX(mod->xxs[i].lps), MAX(mod->xxs[i].lpe),
		    MAX(ish.sloopbeg), MAX(ish.sloopend),
		    ish.flags, ish.convert,
		    ish.vol, ish.gvl, ish.c5spd);

	/* Convert C5SPD to relnote/finetune
	 *
	 * In IT we can have a sample associated with two or more
	 * instruments, but c5spd is a sample attribute -- so we must
	 * scan all xmp instruments to set the correct transposition
	 */
	
	for (j = 0; j < mod->ins; j++) {
	    for (k = 0; k < mod->xxi[j].nsm; k++) {
		if (mod->xxi[j].sub[k].sid == i) {
		    mod->xxi[j].sub[k].vol = ish.vol;
		    mod->xxi[j].sub[k].gvl = ish.gvl;
		    c2spd_to_note(ish.c5spd, &mod->xxi[j].sub[k].xpo, &mod->xxi[j].sub[k].fin);
		}
	    }
	}

	if (ish.flags & IT_SMP_SAMPLE && mod->xxs[i].len > 1) {
	    int cvt = 0;

	    fseek(f, start + ish.sample_ptr, SEEK_SET);

	    if (~ish.convert & IT_CVT_SIGNED)
		cvt |= SAMPLE_FLAG_UNS;

	    /* Handle compressed samples using Tammo Hinrichs' routine */
	    if (ish.flags & IT_SMP_COMP) {
		uint8 *buf;
		buf = calloc(1, mod->xxs[i].len * 2);

		if (ish.flags & IT_SMP_16BIT) {
		    itsex_decompress16(f, buf, mod->xxs[i].len, 
					ish.convert & IT_CVT_DIFF);

		    /* decompression generates native-endian samples, but
		     * we want little-endian */
		    if (o->big_endian)
			xmp_cvt_sex(mod->xxs[i].len, buf);
		} else {
		    itsex_decompress8(f, buf, mod->xxs[i].len, 
					ish.convert & IT_CVT_DIFF);
		}

		load_sample(ctx, NULL, i,
				SAMPLE_FLAG_NOLOAD | cvt, &mod->xxs[i], buf);
		free (buf);
	    } else {
		load_sample(ctx, f, i, cvt, &mod->xxs[i], NULL);
	    }
	}
    }

    _D(_D_INFO "Stored Patterns: %d", mod->pat);

    mod->trk = mod->pat * mod->chn;
    memset(arpeggio_val, 0, 64);
    memset(last_h, 0, 64);
    memset(last_fxp, 0, 64);

    PATTERN_INIT();

    /* Read patterns */
    for (max_ch = i = 0; i < mod->pat; i++) {
	PATTERN_ALLOC (i);
	r = 0;
	/* If the offset to a pattern is 0, the pattern is empty */
	if (!pp_pat[i]) {
	    mod->xxp[i]->rows = 64;
	    mod->xxt[i * mod->chn] = calloc (sizeof (struct xmp_track) +
		sizeof (struct xmp_event) * 64, 1);
	    mod->xxt[i * mod->chn]->rows = 64;
	    for (j = 0; j < mod->chn; j++)
		mod->xxp[i]->index[j] = i * mod->chn;
	    continue;
	}
	fseek(f, start + pp_pat[i], SEEK_SET);
	pat_len = read16l(f) /* - 4*/;
	mod->xxp[i]->rows = read16l(f);
	TRACK_ALLOC (i);
	memset (mask, 0, L_CHANNELS);
	read16l(f);
	read16l(f);

	while (--pat_len >= 0) {
	    b = read8(f);
	    if (!b) {
		r++;
		continue;
	    }
	    c = (b - 1) & 63;

	    if (b & 0x80) {
		mask[c] = read8(f);
		pat_len--;
	    }
	    /*
	     * WARNING: we IGNORE events in disabled channels. Disabled
	     * channels should be muted only, but we don't know the
	     * real number of channels before loading the patterns and
	     * we don't want to set it to 64 channels.
	     */
	    event = c >= mod->chn ? &dummy : &EVENT (i, c, r);
	    if (mask[c] & 0x01) {
		b = read8(f);

		if (b > 0x7f && b < 0xfd)
			b = 0;

		switch (b) {
		case 0xff:	/* key off */
		    b = XMP_KEY_OFF;
		    break;
		case 0xfe:	/* cut */
		    b = XMP_KEY_CUT;
		    break;
		case 0xfd:	/* fade */
		    b = XMP_KEY_FADE;
		    break;
		default:
		    if (b < 11)
			b = 0;
		    else
			b -= 11;
		}
		lastevent[c].note = event->note = b;
		pat_len--;
	    }
	    if (mask[c] & 0x02) {
		b = read8(f);
		lastevent[c].ins = event->ins = b;
		pat_len--;
	    }
	    if (mask[c] & 0x04) {
		b = read8(f);
		lastevent[c].vol = event->vol = b;
		xlat_volfx (event);
		pat_len--;
	    }
	    if (mask[c] & 0x08) {
		b = read8(f);
		event->fxt = b;
		event->fxp = read8(f);
		xlat_fx (c, event);
		lastevent[c].fxt = event->fxt;
		lastevent[c].fxp = event->fxp;
		pat_len -= 2;
	    }
	    if (mask[c] & 0x10) {
		event->note = lastevent[c].note;
	    }
	    if (mask[c] & 0x20) {
		event->ins = lastevent[c].ins;
	    }
	    if (mask[c] & 0x40) {
		event->vol = lastevent[c].vol;
		xlat_volfx (event);
	    }
	    if (mask[c] & 0x80) {
		event->fxt = lastevent[c].fxt;
		event->fxp = lastevent[c].fxp;
	    }
	}

	/* Scan channels, look for unused tracks */
	for (c = mod->chn - 1; c >= max_ch; c--) {
	    for (flag = j = 0; j < mod->xxt[mod->xxp[i]->index[c]]->rows; j++) {
		event = &EVENT (i, c, j);
		if (event->note || event->vol || event->ins || event->fxt ||
		    event->fxp || event->f2t || event->f2p) {
		    flag = 1;
		    break;
		}
	    }
	    if (flag && c > max_ch)
		max_ch = c;
	}
    }

    free(pp_pat);
    free(pp_smp);
    if (pp_ins)		/* sample mode has no instruments */
	free(pp_ins);

    mod->chn = max_ch + 1;
    m->flags |= XMP_CTL_VIRTUAL | XMP_CTL_FILTER;
    m->quirk |= QUIRKS_IT;
    if (~ifh.flags & IT_LINK_GXX)
	m->quirk |= QUIRK_UNISLD;

    return 0;
}
