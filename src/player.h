/* Extended Module Player
 * Copyright (C) 1996-2012 Claudio Matsuoka and Hipolito Carraro Jr
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU General Public License. See doc/COPYING
 * for more information.
 */

#ifndef __XMP_PLAYER_H
#define __XMP_PLAYER_H

#include "stepper.h"
#include "lfo.h"
#include "envelope.h"

/* Quirk control */
#define HAS_QUIRK(x)	(m->quirk & (x))

/* Channel flag control */
#define SET(f)		SET_FLAG(xc->flags,f)
#define RESET(f) 	RESET_FLAG(xc->flags,f)
#define TEST(f)		TEST_FLAG(xc->flags,f)

/* Persistent effect flag control */
#define SET_PER(f)	SET_FLAG(xc->per_flags,f)
#define RESET_PER(f)	RESET_FLAG(xc->per_flags,f)
#define TEST_PER(f)	TEST_FLAG(xc->per_flags,f)

/* Global flags */
#define PATTERN_BREAK	0x0001 
#define PATTERN_LOOP	0x0002 
#define MODULE_ABORT	0x0004 
#define MODULE_RESTART	0x0008 

#define DOENV_RELEASE	((TEST (RELEASE) || act == VIRTCH_ACTION_OFF))

struct retrig_control {
	int s;
	int m;
	int d;
};

/* The following macros are used to set the flags for each channel */
#define VOL_SLIDE	0x00000001
#define PAN_SLIDE	0x00000002
#define TONEPORTA	0x00000004
#define PITCHBEND	0x00000008
#define VIBRATO		0x00000010
#define TREMOLO		0x00000020
#define FINE_VOLS	0x00000040
#define FINE_BEND	0x00000080
#define NEW_PAN		0x00000100
#define FINETUNE	0x00000200
#define OFFSET		0x00000400
#define TRK_VSLIDE	0x00000800
#define TRK_FVSLIDE	0x00001000
#define RESET_VOL	0x00002000
#define RESET_ENV	0x00004000
#define IS_VALID	0x00008000
#define NEW_INS		0x00010000
#define NEW_VOL		0x00020000
#define VOL_SLIDE_2	0x00080000
#define NOTE_SLIDE	0x00100000
#define FINE_NSLIDE	0x00200000
#define NEW_NOTE	0x00400000
#define FINE_TPORTA	0x00800000

/* These need to be "persistent" between frames */
#define IS_READY	0x01000000
#define FADEOUT		0x02000000
#define RELEASE		0x04000000

/* Prefixes: 
 * a_ for arpeggio
 * v_ for volume slide
 * p_ for pan
 * f_ for frequency (period) slide
 * s_ for slide to note (tone portamento)
 */

struct instrument_vibrato {
	int phase;
	int sweep;
};


/* MED support */

struct channel_data {
	int flags;		/* Channel flags */
	int per_flags;		/* Persistent effect channel flags */
	int note;		/* Note number */
	int key;		/* Key number */
	double period;		/* Amiga or linear period */
	int finetune;		/* Guess what */
	int ins;		/* Instrument number */
	int smp;		/* Sample number */
	int insdef;		/* Instrument default */
	int pan;		/* Current pan */
	int masterpan;		/* Master pan -- for S3M set pan effect */
	int mastervol;		/* Master vol -- for IT track vol effect */
	int delay;		/* Note delay in frames */
	int keyoff;		/* Key off counter */
	int fadeout;		/* Current fadeout (release) value */
	int gliss;		/* Glissando active */
	int volume;		/* Current volume */
	int gvl;		/* Global volume for instrument for IT */
	int p_val;		/* Current pan value */
	int offset;		/* Sample offset memory */
	int offset_val;		/* Sample offset */

	uint16 v_idx;		/* Volume envelope index */
	uint16 p_idx;		/* Pan envelope index */
	uint16 f_idx;		/* Freq envelope index */

	struct lfo vibrato;
	struct lfo tremolo;
	struct stepper arpeggio;

	struct {
		struct lfo lfo;
		int sweep;
	} insvib;

	struct {
		int type;	/* Retrig type */
		int count;	/* Retrig counter */
		int delay;	/* Retrig delay in frames */
		int val;	/* Retrig parameters */
	} retrig;

	struct {
		int val;	/* Tremor */
		int count_up;	/* Tremor counter (up cycle) */
		int count_dn;	/* Tremor counter (down cycle) */
	} tremor;

	struct {
		int slide;	/* Volume slide value */
		int fslide;	/* Fine volume slide value */
		int slide2;	/* Volume slide value */
		int memory;	/* Volume slide effect memory */
	} vol;

	struct {
		int slide;	/* Track volume slide value */
		int fslide;	/* Track fine volume slide value */
		int memory;	/* Track volume slide effect memory */
	} trackvol;

	struct {
		int slide;	/* Frequency slide value */
		int fslide;	/* Fine frequency slide value */
		int porta;	/* Portamento effect memory */
		int s_end;	/* Target period for tone portamento */
		int s_sgn;	/* Tone portamento up/down switch */
		int s_val;	/* Delta for tone portamento */
	} freq;

	struct {
		int slide;	/* PTM note slide amount */
		int fslide;	/* OKT fine note slide amount */
		int speed;	/* PTM note slide speed */
		int count;	/* PTM note slide counter */
	} noteslide;

	struct {
		int speed;
		int count;
		int pos;
	} invloop;

	struct {
		int cutoff;	/* IT filter cutoff frequency */
		int cutoff2;	/* IT filter cutoff frequency (with envelope) */
		int resonance;	/* IT filter resonance */
		int B0;		/* IT filter variables */
		int B1;
		int B2;
	} filter;

	struct med_channel {
		int vp;		/* MED synth volume sequence table pointer */
		int vv;		/* MED synth volume slide value */
		int vs;		/* MED synth volume speed */
		int vc;		/* MED synth volume speed counter */
		int vw;		/* MED synth volume wait counter */
		int wp;		/* MED synth waveform sequence table pointer */
		int wv;		/* MED synth waveform slide value */
		int ws;		/* MED synth waveform speed */
		int wc;		/* MED synth waveform speed counter */
		int ww;		/* MED synth waveform wait counter */
		int period;	/* MED synth period for RES */
		int arp;	/* MED synth arpeggio start */
		int aidx;	/* MED synth arpeggio index */
		int vwf;	/* MED synth vibrato waveform */
		int vib_depth;	/* MED synth vibrato depth */
		int vib_speed;	/* MED synth vibrato speed */
		int vib_idx;	/* MED synth vibrato index */
		int vib_wf;	/* MED synth vibrato waveform */
	} med;

	struct xmp_event *delayed_event;
	int delayed_ins;	/* IT save instrument emulation */

	int info_period;	/* Period */
	int info_pitchbend;	/* Linear pitchbend */
};


void process_fx(struct context_data *, int, uint8, uint8, uint8, struct channel_data *, int);
void med_synth(struct context_data *, int, struct channel_data *, int);
int get_med_arp(struct module_data *, struct channel_data *);
int get_med_vibrato(struct channel_data *);
void filter_setup(struct context_data *, struct channel_data *, int);
void update_invloop(struct module_data *, struct channel_data *);

#endif /* __XMP_PLAYER_H */
