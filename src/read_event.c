#include <string.h>
#include "common.h"
#include "player.h"
#include "effects.h"
#include "virtual.h"
#include "period.h"


static inline void copy_channel(struct player_data *p, int to, int from)
{
	if (to > 0 && to != from) {
		memcpy(&p->xc_data[to], &p->xc_data[from],
					sizeof (struct channel_data));
	}
}

static struct xmp_subinstrument *get_subinstrument(struct context_data *ctx,
						   int ins, int key)
{
	struct module_data *m = &ctx->m;
	struct xmp_module *mod = &m->mod;

	if (ins >= 0 && key >= 0) {
		if (mod->xxi[ins].map[key].ins != 0xff) {
			int mapped = mod->xxi[ins].map[key].ins;
			return &mod->xxi[ins].sub[mapped];
		}
	}

	return NULL;
}

static void set_effect_defaults(struct context_data *ctx, int note,
				struct xmp_subinstrument *sub,
				struct channel_data *xc)
{
	struct module_data *m = &ctx->m;

	if (sub != NULL && note >= 0) {
		xc->pan = sub->pan;
		xc->finetune = sub->fin;
		xc->gvl = sub->gvl;

		if (sub->ifc & 0x80) {
			xc->filter.cutoff = (sub->ifc - 0x80) * 2;
		} else {
			xc->filter.cutoff = 0xff;
		}

		if (sub->ifr & 0x80) {
			xc->filter.resonance = (sub->ifr - 0x80) * 2;
		} else {
			xc->filter.resonance = 0;
		}

		set_lfo_depth(&xc->insvib.lfo, sub->vde);
		set_lfo_rate(&xc->insvib.lfo, sub->vra >> 2);
		set_lfo_waveform(&xc->insvib.lfo, sub->vwf);
		xc->insvib.sweep = sub->vsw;

		xc->v_idx = xc->p_idx = xc->f_idx = 0;

		set_lfo_phase(&xc->vibrato, 0);
		set_lfo_phase(&xc->tremolo, 0);

		xc->freq.s_end = xc->period = note_to_period(note,
				xc->finetune, HAS_QUIRK(QUIRK_LINEAR));
	}
}


#define IS_TONEPORTA(x) ((x) == FX_TONEPORTA || (x) == FX_TONE_VSLIDE)


static int read_event_mod(struct context_data *ctx, struct xmp_event *e, int chn, int ctl)
{
	struct player_data *p = &ctx->p;
	struct module_data *m = &ctx->m;
	struct xmp_module *mod = &m->mod;
	struct channel_data *xc = &p->xc_data[chn];
	int note, key, flags;
	int cont_sample;
	struct xmp_subinstrument *sub;
	int new_invalid_ins = 0;

	flags = 0;
	note = -1;
	key = e->note;
	cont_sample = 0;

	/* Check instrument */

	if (e->ins) {
		int ins = e->ins - 1;
		flags = NEW_INS | RESET_VOL | RESET_ENV;
		xc->fadeout = 0x8000;	/* for painlace.mod pat 0 ch 3 echo */
		xc->per_flags = 0;
		xc->offset_val = 0;

		if (IS_VALID_INSTRUMENT(ins)) {
			xc->ins = ins;
		} else {
			new_invalid_ins = 1;
			virtch_resetchannel(ctx, chn);
		}

		xc->med.arp = xc->med.aidx = 0;
	}

	/* Check note */

	if (key) {
		flags |= NEW_NOTE;

		if (key == XMP_KEY_FADE) {
			SET(FADEOUT);
			flags &= ~(RESET_VOL | RESET_ENV);
		} else if (key == XMP_KEY_CUT) {
			virtch_resetchannel(ctx, chn);
		} else if (key == XMP_KEY_OFF) {
			SET(RELEASE);
			flags &= ~(RESET_VOL | RESET_ENV);
		} else if (IS_TONEPORTA(e->fxt) || IS_TONEPORTA(e->f2t)) {

			/* When a toneporta is issued after a keyoff event,
			 * retrigger the instrument (xr-nc.xm, bug #586377)
			 *
			 *   flags |= NEW_INS;
			 *   xc->ins = ins;
			 *
			 * (From Decibelter - Cosmic 'Wegian Mamas.xm p04 ch7)
			 * We don't retrigger the sample, it simply continues.
			 * This is important to play sweeps and loops correctly.
			 */
			//cont_sample = 1;

			/* set key to 0 so we can have the tone portamento from
			 * the original note (see funky_stars.xm pos 5 ch 9)
			 */
			key = 0;

			/* And do the same if there's no keyoff (see
			 * comic bakery remix.xm pos 1 ch 3)
			 */
		}
	}

	if ((uint32)key <= XMP_MAX_KEYS && key > 0) {
		xc->key = --key;

		sub = get_subinstrument(ctx, xc->ins, key);

		if (!new_invalid_ins && sub != NULL) {
			int transp = mod->xxi[xc->ins].map[key].xpo;
			int smp;

			note = key + sub->xpo + transp;
			smp = sub->sid;

			if (mod->xxs[smp].len == 0) {
				smp = -1;
			}

			if (smp >= 0 && smp < mod->smp) {
				int to = virtch_setpatch(ctx, chn, xc->ins,
					smp, note, sub->nna, sub->dct,
					sub->dca, ctl, cont_sample);

				if (to < 0) {
					return -1;
				}
printf("to=%d from=%d\n", to, chn);

				copy_channel(p, to, chn);

				xc->smp = smp;
			}
		} else {
			flags = 0;
		}
	}

	sub = get_subinstrument(ctx, xc->ins, xc->key);

	set_effect_defaults(ctx, note, sub, xc);

	/* Reset flags */
	xc->delay = xc->retrig.delay = 0;
	xc->flags = flags | (xc->flags & 0xff000000); /* keep persistent flags */

	reset_stepper(&xc->arpeggio);
	xc->tremor.val = 0;

	/* Process new volume */
	if (e->vol) {
		xc->volume = e->vol - 1;
		RESET(RESET_VOL);
		SET(NEW_VOL);
	}

	process_fx(ctx, chn, e->note, e->f2t, e->f2p, xc, 1);
	process_fx(ctx, chn, e->note, e->fxt, e->fxp, xc, 0);

	if (sub == NULL) {
		return 0;
	}

	if (note >= 0) {
		xc->note = note;

		if (cont_sample == 0) {
			virtch_voicepos(ctx, chn, xc->offset_val);
			if (TEST(OFFSET) && HAS_QUIRK(QUIRK_FX9BUG)) {
				xc->offset_val <<= 1;
			}
		}
		RESET(OFFSET);
	}

	if (xc->key < 0) {
		return 0;
	}

	if (TEST(RESET_ENV)) {
		RESET(RELEASE | FADEOUT);
	}

	if (TEST(RESET_VOL)) {
		xc->volume = sub->vol;
		SET(NEW_VOL);
	}

	return 0;
}

static int read_event_ft2(struct context_data *ctx, struct xmp_event *e, int chn, int ctl)
{
	struct player_data *p = &ctx->p;
	struct module_data *m = &ctx->m;
	struct xmp_module *mod = &m->mod;
	struct channel_data *xc = &p->xc_data[chn];
	int note, key, flags;
	int cont_sample;
	struct xmp_subinstrument *sub;
	int new_invalid_ins;
	int is_toneporta;

	flags = 0;
	note = -1;
	key = e->note;
	cont_sample = 0;
	new_invalid_ins = 0;
	is_toneporta = 0;

	if (IS_TONEPORTA(e->fxt) || IS_TONEPORTA(e->f2t)) {
		is_toneporta = 1;
	}

	/* Check instrument */

	if (e->ins) {
		int ins = e->ins - 1;
		flags = NEW_INS | RESET_VOL | RESET_ENV;
		xc->fadeout = 0x8000;	/* for painlace.mod pat 0 ch 3 echo */
		xc->per_flags = 0;

		if (IS_VALID_INSTRUMENT(ins)) {
			xc->ins = ins;
		} else {
			new_invalid_ins = 1;

			/* If no note is set FT2 doesn't cut on invalid
			 * instruments (it keeps playing the previous one).
			 * If a note is set it cuts the current sample.
			 */
			flags = 0;

			if (is_toneporta) {
				key = 0;
			}
		}

		xc->med.arp = xc->med.aidx = 0;
	}

	/* Check note */

	if (key) {
		flags |= NEW_NOTE;

		if (key == XMP_KEY_FADE) {
			SET(FADEOUT);
			flags &= ~(RESET_VOL | RESET_ENV);
		} else if (key == XMP_KEY_CUT) {
			virtch_resetchannel(ctx, chn);
		} else if (key == XMP_KEY_OFF) {
			SET(RELEASE);
			flags &= ~(RESET_VOL | RESET_ENV);
		} else if (is_toneporta) {

			/* When a toneporta is issued after a keyoff event,
			 * retrigger the instrument (xr-nc.xm, bug #586377)
			 *
			 *   flags |= NEW_INS;
			 *   xc->ins = ins;
			 *
			 * (From Decibelter - Cosmic 'Wegian Mamas.xm p04 ch7)
			 * We don't retrigger the sample, it simply continues.
			 * This is important to play sweeps and loops correctly
			 */
			cont_sample = 1;

			/* set key to 0 so we can have the tone portamento from
			 * the original note (see funky_stars.xm pos 5 ch 9)
			 */
			key = 0;

			/* And do the same if there's no keyoff (see
			 * comic bakery remix.xm pos 1 ch 3)
			 */
		}

		if (new_invalid_ins) {
			virtch_resetchannel(ctx, chn);
		}
	}

	/* FT2: Retrieve old instrument volume */
	if (e->ins) {
		struct xmp_subinstrument *sub;

		if (key == 0 || key >= XMP_KEY_OFF) {
			/* Previous instrument */
			sub = get_subinstrument(ctx, xc->ins_oinsvol, xc->key);

			/* No note */
			if (sub != NULL) {
				xc->volume = sub->vol;
				flags |= NEW_VOL;
				flags &= ~RESET_VOL;
			}
		} else {
			/* Retrieve volume when we have note */

			/* and only if we have instrument, otherwise we're in
			 * case 1: new note and no instrument
			 */
			if (e->ins) {
				/* Current instrument */
				sub = get_subinstrument(ctx, xc->ins, key - 1);
				if (sub != NULL) {
					xc->volume = sub->vol;
				} else {
					xc->volume = 0;
				}
				xc->ins_oinsvol = xc->ins;
				flags |= NEW_VOL;
				flags &= ~RESET_VOL;
			}
		}
	}

	if ((uint32)key <= XMP_MAX_KEYS && key > 0) {
		xc->key = --key;

		sub = get_subinstrument(ctx, xc->ins, key);

		if (!new_invalid_ins && sub != NULL) {
			int transp = mod->xxi[xc->ins].map[key].xpo;
			int smp;

			note = key + sub->xpo + transp;
			smp = sub->sid;

			if (mod->xxs[smp].len == 0) {
				smp = -1;
			}

			if (smp >= 0 && smp < mod->smp) {
				int to = virtch_setpatch(ctx, chn, xc->ins,
					smp, note, sub->nna, sub->dct,
					sub->dca, ctl, cont_sample);

				if (to < 0) {
					return -1;
				}

				copy_channel(p, to, chn);

				xc->smp = smp;
			}
		} else {
			flags = 0;
		}
	}

	sub = get_subinstrument(ctx, xc->ins, xc->key);

	set_effect_defaults(ctx, note, sub, xc);

	/* Reset flags */
	xc->delay = xc->retrig.delay = 0;
	xc->flags = flags | (xc->flags & 0xff000000); /* keep persistent flags */

	reset_stepper(&xc->arpeggio);
	xc->tremor.val = 0;

	/* Process new volume */
	if (e->vol) {
		xc->volume = e->vol - 1;
		RESET(RESET_VOL);
		SET(NEW_VOL);
	}

	/* FT2: always reset sample offset */
	xc->offset_val = 0;

	process_fx(ctx, chn, e->note, e->f2t, e->f2p, xc, 1);
	process_fx(ctx, chn, e->note, e->fxt, e->fxp, xc, 0);

	if (sub == NULL) {
		return 0;
	}

	if (note >= 0) {
		xc->note = note;

		if (cont_sample == 0) {
			virtch_voicepos(ctx, chn, xc->offset_val);
			if (TEST(OFFSET)) {
				xc->offset_val <<= 1;
			}
		}
		RESET(OFFSET);
	}

	if (xc->key < 0) {
		return 0;
	}

	if (TEST(RESET_ENV)) {
		RESET(RELEASE | FADEOUT);
	}

	if (TEST(RESET_VOL)) {
		xc->volume = sub->vol;
		SET(NEW_VOL);
	}

	return 0;
}

static int read_event_st3(struct context_data *ctx, struct xmp_event *e, int chn, int ctl)
{
	struct player_data *p = &ctx->p;
	struct module_data *m = &ctx->m;
	struct xmp_module *mod = &m->mod;
	struct channel_data *xc = &p->xc_data[chn];
	int note, key, flags;
	int cont_sample;
	struct xmp_subinstrument *sub;
	int not_same_ins;
	int is_toneporta;

	flags = 0;
	note = -1;
	key = e->note;
	cont_sample = 0;
	not_same_ins = 0;
	is_toneporta = 0;

	if (IS_TONEPORTA(e->fxt) || IS_TONEPORTA(e->f2t)) {
		is_toneporta = 1;
	}

	/* Check instrument */

	if (e->ins) {
		int ins = e->ins - 1;
		flags = NEW_INS | RESET_VOL | RESET_ENV;
		xc->fadeout = 0x8000;	/* for painlace.mod pat 0 ch 3 echo */
		xc->per_flags = 0;
		xc->offset_val = 0;

		if (IS_VALID_INSTRUMENT(ins)) {
			/* valid ins */
			if (xc->ins != ins) {
				not_same_ins = 1;
				if (!is_toneporta) {
					xc->ins = ins;
				} else {
					/* Get new instrument volume */
					sub = get_subinstrument(ctx, ins, key);
					if (sub != NULL) {
						xc->volume = sub->vol;
						flags &= ~RESET_VOL;
					}
				}
			}
		} else {
			/* invalid ins */

			/* Ignore invalid instruments */
			flags = 0;
		}

		xc->med.arp = xc->med.aidx = 0;
	}

	/* Check note */

	if (key) {
		flags |= NEW_NOTE;

		if (key == XMP_KEY_FADE) {
			SET(FADEOUT);
			flags &= ~(RESET_VOL | RESET_ENV);
		} else if (key == XMP_KEY_CUT) {
			virtch_resetchannel(ctx, chn);
		} else if (key == XMP_KEY_OFF) {
			SET(RELEASE);
			flags &= ~(RESET_VOL | RESET_ENV);
		} else if (is_toneporta) {

			/* Always retrig in tone portamento: Fix portamento in
			 * 7spirits.s3m, mod.Biomechanoid
			 */
			if (not_same_ins) {
				xc->offset_val = 0;
			} else {
				cont_sample = 1;
			}
			key = 0;
		}
	}

	if ((uint32)key <= XMP_MAX_KEYS && key > 0) {
		xc->key = --key;

		sub = get_subinstrument(ctx, xc->ins, key);

		if (sub != NULL) {
			int transp = mod->xxi[xc->ins].map[key].xpo;
			int smp;

			note = key + sub->xpo + transp;
			smp = sub->sid;

			if (mod->xxs[smp].len == 0) {
				smp = -1;
			}

			if (smp >= 0 && smp < mod->smp) {
				int to = virtch_setpatch(ctx, chn, xc->ins,
					smp, note, sub->nna, sub->dct,
					sub->dca, ctl, cont_sample);

				if (to < 0) {
					return -1;
				}

				copy_channel(p, to, chn);
				xc->smp = smp;
			}
		} else {
			flags = 0;
		}
	}

	sub = get_subinstrument(ctx, xc->ins, xc->key);

	set_effect_defaults(ctx, note, sub, xc);

	/* Reset flags */
	xc->delay = xc->retrig.delay = 0;
	xc->flags = flags | (xc->flags & 0xff000000); /* keep persistent flags */

	reset_stepper(&xc->arpeggio);
	xc->tremor.val = 0;

	/* Process new volume */
	if (e->vol) {
		xc->volume = e->vol - 1;
		RESET(RESET_VOL);
		SET(NEW_VOL);
	}

	process_fx(ctx, chn, e->note, e->f2t, e->f2p, xc, 1);
	process_fx(ctx, chn, e->note, e->fxt, e->fxp, xc, 0);

	if (sub == NULL) {
		return 0;
	}

	if (note >= 0) {
		xc->note = note;

		if (cont_sample == 0) {
			virtch_voicepos(ctx, chn, xc->offset_val);
			if (TEST(OFFSET)) {
				xc->offset_val <<= 1;
			}
		}
		RESET(OFFSET);
	}

	if (xc->key < 0) {
		return 0;
	}

	if (TEST(RESET_ENV)) {
		RESET(RELEASE | FADEOUT);
	}

	if (TEST(RESET_VOL)) {
		xc->volume = sub->vol;
		SET(NEW_VOL);
	}

	/* ST3: check QUIRK_ST3GVOL only in ST3 event reader */
	if (HAS_QUIRK(QUIRK_ST3GVOL) && TEST(NEW_VOL)) {
		xc->volume = xc->volume * p->gvol.volume / m->volbase;
	}

	return 0;
}

static int read_event_it(struct context_data *ctx, struct xmp_event *e, int chn, int ctl)
{
	struct player_data *p = &ctx->p;
	struct module_data *m = &ctx->m;
	struct xmp_module *mod = &m->mod;
	struct channel_data *xc = &p->xc_data[chn];
	int note, key, flags;
	int cont_sample;
	struct xmp_subinstrument *sub;
	int not_same_ins;
	int new_invalid_ins;
	int is_toneporta;
	int candidate_ins;

	/* Emulate Impulse Tracker "always read instrument" bug */
	if (e->note && !e->ins && xc->delayed_ins) {
		e->ins = xc->delayed_ins;
		xc->delayed_ins = 0;
	}

	flags = 0;
	note = -1;
	key = e->note;
	cont_sample = 0;
	not_same_ins = 0;
	new_invalid_ins = 0;
	is_toneporta = 0;
	candidate_ins = xc->ins;

	if (IS_TONEPORTA(e->fxt) || IS_TONEPORTA(e->f2t)) {
		is_toneporta = 1;
	}

	/* Check instrument */

	if (e->ins) {
		int ins = e->ins - 1;
		flags = NEW_INS | RESET_VOL | RESET_ENV;
		xc->fadeout = 0x8000;	/* for painlace.mod pat 0 ch 3 echo */
		xc->per_flags = 0;

		if (IS_VALID_INSTRUMENT(ins)) {
			/* valid ins */

			if (!key) {
				/* IT: Reset note for every new != ins */
				if (xc->ins == ins) {
					flags = NEW_INS | RESET_VOL;
				} else {
					key = xc->key + 1;
				}
			}
			if (xc->ins != ins) {
				not_same_ins = 1;
				if (!is_toneporta) {
					candidate_ins = ins;
				} else {
					/* Get new instrument volume */
					sub = get_subinstrument(ctx, ins, key);
					if (sub != NULL) {
						xc->volume = sub->vol;
						flags &= ~RESET_VOL;
					}
				}
			}
		} else {
			/* Ignore invalid instruments */
			new_invalid_ins = 1;
			flags = 0;
		}

		xc->med.arp = xc->med.aidx = 0;
	}

	/* Check note */

	if (key && !new_invalid_ins) {
		flags |= NEW_NOTE;

		if (key == XMP_KEY_FADE) {
			SET(FADEOUT);
			flags &= ~(RESET_VOL | RESET_ENV);
		} else if (key == XMP_KEY_CUT) {
			virtch_resetchannel(ctx, chn);
		} else if (key == XMP_KEY_OFF) {
			SET(RELEASE);
			flags &= ~(RESET_VOL | RESET_ENV);
		} else if (is_toneporta) {

			/* Always retrig on tone portamento: Fix portamento in
			 * 7spirits.s3m, mod.Biomechanoid
			 */
			if (not_same_ins) {
				flags |= NEW_INS;
			} else {
				cont_sample = 1;
			}
			key = 0;
		}
	}

	if ((uint32)key <= XMP_MAX_KEYS && key > 0 && !new_invalid_ins) {
		xc->key = --key;

		sub = get_subinstrument(ctx, candidate_ins, key);

		if (sub != NULL) {
			int transp = mod->xxi[candidate_ins].map[key].xpo;
			int smp;

			note = key + sub->xpo + transp;
			smp = sub->sid;

			if (mod->xxs[smp].len == 0) {
				smp = -1;
			}

			if (smp >= 0 && smp < mod->smp) {
				int to = virtch_setpatch(ctx, chn, candidate_ins,
					smp, note, sub->nna, sub->dct,
					sub->dca, ctl, cont_sample);

				if (to < 0) {
					return -1;
				}

				copy_channel(p, to, chn);

				xc->smp = smp;
			}
		} else {
			flags = 0;
		}
	}

	if (IS_VALID_INSTRUMENT(candidate_ins)) {
		xc->ins = candidate_ins;
	}

	sub = get_subinstrument(ctx, xc->ins, xc->key);

	set_effect_defaults(ctx, note, sub, xc);
	
	/* Reset flags */
	xc->delay = xc->retrig.delay = 0;
	xc->flags = flags | (xc->flags & 0xff000000); /* keep persistent flags */

	reset_stepper(&xc->arpeggio);
	xc->tremor.val = 0;

	/* Process new volume */
	if (e->vol) {
		xc->volume = e->vol - 1;
		RESET(RESET_VOL);
		SET(NEW_VOL);
	}

	/* IT: always reset sample offset */
	xc->offset_val = 0;

	process_fx(ctx, chn, e->note, e->f2t, e->f2p, xc, 1);
	process_fx(ctx, chn, e->note, e->fxt, e->fxp, xc, 0);

	if (sub == NULL) {
		return 0;
	}

	if (note >= 0) {
		xc->note = note;

		if (cont_sample == 0) {
			virtch_voicepos(ctx, chn, xc->offset_val);
			if (TEST(OFFSET)) {
				xc->offset_val <<= 1;
			}
		}
		RESET(OFFSET);
	}

	if (xc->key < 0) {
		return 0;
	}

	if (TEST(RESET_ENV)) {
		RESET(RELEASE | FADEOUT);
	}

	if (TEST(RESET_VOL)) {
		xc->volume = sub->vol;
		SET(NEW_VOL);
	}

	return 0;
}


int read_event(struct context_data *ctx, struct xmp_event *e, int chn, int ctl)
{
	struct module_data *m = &ctx->m;

	switch (m->read_event_type) {
	case READ_EVENT_MOD:
		return read_event_mod(ctx, e, chn, ctl);
	case READ_EVENT_FT2:
		return read_event_ft2(ctx, e, chn, ctl);
	case READ_EVENT_ST3:
		return read_event_st3(ctx, e, chn, ctl);
	case READ_EVENT_IT:
		return read_event_it(ctx, e, chn, ctl);
	default:
		return read_event_mod(ctx, e, chn, ctl);
	}
}
