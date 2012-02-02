/* Extended Module Player
 * Copyright (C) 1996-2012 Claudio Matsuoka and Hipolito Carraro Jr
 *
 * DMF sample decompressor Copyright (C) 2000 Olivier Lapicque
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU General Public License. See doc/COPYING
 * for more information.
 */

#include <assert.h>

#include "load.h"
#include "iff.h"
#include "period.h"

#define MAGIC_DDMF	MAGIC4('D','D','M','F')


static int dmf_test(FILE *, char *, const int);
static int dmf_load (struct xmp_context *, FILE *, const int);

struct xmp_loader_info dmf_loader = {
	"DMF",
	"X-Tracker",
	dmf_test,
	dmf_load
};

static int dmf_test(FILE * f, char *t, const int start)
{
	if (read32b(f) != MAGIC_DDMF)
		return -1;

	fseek(f, 9, SEEK_CUR);
	read_title(f, t, 30);

	return 0;
}


static int ver;
static uint8 packtype[256];


struct hnode {
	short int left, right;
	uint8 value;
};

struct htree {
	uint8 *ibuf, *ibufmax;
	uint32 bitbuf;
	int bitnum;
	int lastnode, nodecount;
	struct hnode nodes[256];
};


static uint8 read_bits(struct htree *tree, int nbits)
{
	uint8 x = 0, bitv = 1;
	while (nbits--) {
		if (tree->bitnum) {
			tree->bitnum--;
		} else {
			tree->bitbuf = (tree->ibuf < tree->ibufmax) ?
							*(tree->ibuf++) : 0;
			tree->bitnum = 7;
		}
		if (tree->bitbuf & 1) x |= bitv;
		bitv <<= 1;
		tree->bitbuf >>= 1;
	}
	return x;
}

/* tree: [8-bit value][12-bit index][12-bit index] = 32-bit */
static void new_node(struct htree *tree)
{
	uint8 isleft, isright;
	int actnode;

	actnode = tree->nodecount;

	if (actnode > 255)
		return;

	tree->nodes[actnode].value = read_bits(tree, 7);
	isleft = read_bits(tree, 1);
	isright = read_bits(tree, 1);
	actnode = tree->lastnode;

	if (actnode > 255)
		return;

	tree->nodecount++;
	tree->lastnode = tree->nodecount;

	if (isleft) {
		tree->nodes[actnode].left = tree->lastnode;
		new_node(tree);
	} else {
		tree->nodes[actnode].left = -1;
	}

	tree->lastnode = tree->nodecount;

	if (isright) {
		tree->nodes[actnode].right = tree->lastnode;
		new_node(tree);
	} else {
		tree->nodes[actnode].right = -1;
	}
}

static int unpack(uint8 *psample, uint8 *ibuf, uint8 *ibufmax, uint32 maxlen)
{
	struct htree tree;
	int i, actnode;
	uint8 value, sign, delta = 0;
	
	memset(&tree, 0, sizeof(tree));
	tree.ibuf = ibuf;
	tree.ibufmax = ibufmax;
	new_node(&tree);
	value = 0;

	for (i = 0; i < maxlen; i++) {
		actnode = 0;
		sign = read_bits(&tree, 1);

		do {
			if (read_bits(&tree, 1))
				actnode = tree.nodes[actnode].right;
			else
				actnode = tree.nodes[actnode].left;
			if (actnode > 255) break;
			delta = tree.nodes[actnode].value;
			if ((tree.ibuf >= tree.ibufmax) && (!tree.bitnum)) break;
		} while ((tree.nodes[actnode].left >= 0) &&
					(tree.nodes[actnode].right >= 0));

		if (sign)
			delta ^= 0xff;
		value += delta;
		psample[i] = i ? value : 0;
	}

	return tree.ibuf - ibuf;
}


/*
 * IFF chunk handlers
 */

static void get_sequ(struct xmp_context *ctx, int size, FILE *f)
{
	struct xmp_mod_context *m = &ctx->m;
	int i;

	read16l(f);	/* sequencer loop start */
	read16l(f);	/* sequencer loop end */

	m->mod.len = (size - 4) / 2;
	if (m->mod.len > 255)
		m->mod.len = 255;

	for (i = 0; i < m->mod.len; i++)
		m->mod.xxo[i] = read16l(f);
}

static void get_patt(struct xmp_context *ctx, int size, FILE *f)
{
	struct xmp_mod_context *m = &ctx->m;
	int i, j, r, chn;
	int patsize;
	int info, counter, data;
	int track_counter[32];
	struct xmp_event *event;

	m->mod.pat = read16l(f);
	m->mod.chn = read8(f);
	m->mod.trk = m->mod.chn * m->mod.pat;

	PATTERN_INIT();

	_D(_D_INFO "Stored patterns: %d", m->mod.pat);

	for (i = 0; i < m->mod.pat; i++) {
		PATTERN_ALLOC(i);
		chn = read8(f);
		read8(f);		/* beat */
		m->mod.xxp[i]->rows = read16l(f);
		TRACK_ALLOC(i);

		patsize = read32l(f);

		for (j = 0; j < chn; j++)
			track_counter[j] = 0;

		for (counter = r = 0; r < m->mod.xxp[i]->rows; r++) {
			if (counter == 0) {
				/* global track */
				info = read8(f);
				counter = info & 0x80 ? read8(f) : 0;
				data = info & 0x3f ? read8(f) : 0;
			} else {
				counter--;
			}

			for (j = 0; j < chn; j++) {
				int b, fxt, fxp;

				event = &EVENT(i, j, r);

				if (track_counter[j] == 0) {
					b = read8(f);
		
					if (b & 0x80)
						track_counter[j] = read8(f);
					if (b & 0x40)
						event->ins = read8(f);
					if (b & 0x20)
						event->note = 12 + read8(f);
					if (b & 0x10)
						event->vol = read8(f);
					if (b & 0x08) {	/* instrument effect */
						fxt = read8(f);
						fxp = read8(f);
					}
					if (b & 0x04) {	/* note effect */
						fxt = read8(f);
						fxp = read8(f);
					}
					if (b & 0x02) {	/* volume effect */
						fxt = read8(f);
						fxp = read8(f);
						switch (fxt) {
						case 0x02:
							event->fxt = FX_VOLSLIDE_DN;
							event->fxp = fxp;
							break;
						}
					}
				} else {
					track_counter[j]--;
				}
			}
		}
	}
}

static void get_smpi(struct xmp_context *ctx, int size, FILE *f)
{
	struct xmp_mod_context *m = &ctx->m;
	int i, namelen, c3spd, flag;
	uint8 name[30];

	m->mod.ins = m->mod.smp = read8(f);

	INSTRUMENT_INIT();

	_D(_D_INFO "Instruments: %d", m->mod.ins);

	for (i = 0; i < m->mod.ins; i++) {
		int x;

		m->mod.xxi[i].sub = calloc(sizeof (struct xmp_subinstrument), 1);
		
		namelen = read8(f);
		x = namelen - fread(name, 1, namelen > 30 ? 30 : namelen, f);
		copy_adjust(m->mod.xxi[i].name, name, namelen);
		name[namelen] = 0;
		while (x--)
			read8(f);

		m->mod.xxs[i].len = read32l(f);
		m->mod.xxs[i].lps = read32l(f);
		m->mod.xxs[i].lpe = read32l(f);
		m->mod.xxi[i].nsm = !!m->mod.xxs[i].len;
		c3spd = read16l(f);
		c2spd_to_note(c3spd, &m->mod.xxi[i].sub[0].xpo, &m->mod.xxi[i].sub[0].fin);
		m->mod.xxi[i].sub[0].vol = read8(f);
		m->mod.xxi[i].sub[0].pan = 0x80;
		m->mod.xxi[i].sub[0].sid = i;
		flag = read8(f);
		m->mod.xxs[i].flg = flag & 0x01 ? XMP_SAMPLE_LOOP : 0;
		if (ver >= 8)
			fseek(f, 8, SEEK_CUR);	/* library name */
		read16l(f);	/* reserved -- specs say 1 byte only*/
		read32l(f);	/* sampledata crc32 */

		packtype[i] = (flag & 0x0c) >> 2;
		_D(_D_INFO "[%2X] %-30.30s %05x %05x %05x %c P%c %5d V%02x",
				i, name, m->mod.xxs[i].len, m->mod.xxs[i].lps & 0xfffff,
				m->mod.xxs[i].lpe & 0xfffff,
				m->mod.xxs[i].flg & XMP_SAMPLE_LOOP ? 'L' : ' ',
				'0' + packtype[i],
				c3spd, m->mod.xxi[i].sub[0].vol);
	}
}

static void get_smpd(struct xmp_context *ctx, int size, FILE *f)
{
	struct xmp_mod_context *m = &ctx->m;
	int i;
	int smpsize;
	uint8 *data, *ibuf;

	_D(_D_INFO "Stored samples: %d", m->mod.ins);

	for (smpsize = i = 0; i < m->mod.smp; i++) {
		if (m->mod.xxs[i].len > smpsize)
			smpsize = m->mod.xxs[i].len;
	}

	/* why didn't we mmap this? */
	data = malloc(smpsize);
	assert(data != NULL);
	ibuf = malloc(smpsize);
	assert(ibuf != NULL);

	for (i = 0; i < m->mod.smp; i++) {
		smpsize = read32l(f);
		if (smpsize == 0)
			continue;

		switch (packtype[i]) {
		case 0:
			load_patch(ctx, f, m->mod.xxi[i].sub[0].sid,
						0, &m->mod.xxs[m->mod.xxi[i].sub[0].sid], NULL);
			break;
		case 1:
			fread(ibuf, smpsize, 1, f);
			unpack(data, ibuf, ibuf + smpsize, m->mod.xxs[i].len);
			load_patch(ctx, NULL, i,
					XMP_SMP_NOLOAD, &m->mod.xxs[i], (char *)data);
			break;
		default:
			fseek(f, smpsize, SEEK_CUR);
		}
	}

	free(ibuf);
	free(data);
}

static int dmf_load(struct xmp_context *ctx, FILE *f, const int start)
{
	struct xmp_mod_context *m = &ctx->m;
	uint8 date[3];
	char tracker_name[10];

	LOAD_INIT();

	read32b(f);		/* DDMF */

	ver = read8(f);
	fread(tracker_name, 8, 1, f);
	tracker_name[8] = 0;
	snprintf(m->mod.type, XMP_NAMESIZE,
		"D-Lusion Digital Music File v%d (%s)", ver, tracker_name);
	tracker_name[8] = 0;
	fread(m->mod.name, 30, 1, f);
	fseek(f, 20, SEEK_CUR);
	fread(date, 3, 1, f);
	
	MODULE_INFO();
	_D(_D_INFO "Creation date: %02d/%02d/%04d", date[0],
						date[1], 1900 + date[2]);
	
	/* IFF chunk IDs */
	iff_register("SEQU", get_sequ);
	iff_register("PATT", get_patt);
	iff_register("SMPI", get_smpi);
	iff_register("SMPD", get_smpd);
	iff_setflag(IFF_LITTLE_ENDIAN);

	/* Load IFF chunks */
	while (!feof(f))
		iff_chunk(ctx, f);

	m->volbase = 0xff;

	iff_release();

	return 0;
}
