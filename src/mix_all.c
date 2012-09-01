/* Extended Module Player
 * Copyright (C) 1996-2012 Claudio Matsuoka and Hipolito Carraro Jr.
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU Lesser General Public License. See COPYING.LIB
 * for more information.
 */

#include "common.h"
#include "virtual.h"
#include "mixer.h"
#include "synth.h"


/* Mixers
 *
 * To increase performance eight mixers are defined, one for each
 * combination of the following parameters: interpolation, resolution
 * and number of channels.
 */
#define INTERPOLATE() do { \
    smp_l1 = sptr[pos]; \
    smp_dt = sptr[pos + 1] - smp_l1; \
    smp_in = smp_l1 + ((frac * smp_dt) >> SMIX_SHIFT); \
} while (0)

#define DONT_INTERPOLATE() do { \
    smp_in = sptr[pos]; \
} while (0)

#define UPDATE_POS() do { \
    frac += step; \
    pos += frac >> SMIX_SHIFT; \
    frac &= SMIX_MASK; \
} while (0)

#define MIX_STEREO() do { \
    *(buffer++) += smp_in * vr; \
    *(buffer++) += smp_in * vl; \
} while (0)

#define MIX_MONO() do { \
    *(buffer++) += smp_in * vl; \
} while (0)

#define MIX_STEREO_AC() do { \
    if (vi->attack) { \
	int a = SLOW_ATTACK - vi->attack; \
	*(buffer++) += (smp_in * vr * a) >> SLOW_ATTACK_SHIFT; \
	*(buffer++) += (smp_in * vl * a) >> SLOW_ATTACK_SHIFT; \
	vi->attack--; \
    } else { \
	*(buffer++) += smp_in * vr; \
	*(buffer++) += smp_in * vl; \
    } \
} while (0)

#define MIX_STEREO_AC_FILTER() do { \
    sr = (a0 * smp_in * vr + b0 * fr1 + b1 * fr2) >> FILTER_SHIFT; \
    fr2 = fr1; fr1 = sr; \
    sl = (a0 * smp_in * vl + b0 * fl1 + b1 * fl2) >> FILTER_SHIFT; \
    fl2 = fl1; fl1 = sl; \
    if (vi->attack) { \
	int a = SLOW_ATTACK - vi->attack; \
	*(buffer++) += (sr * a) >> SLOW_ATTACK_SHIFT; \
	*(buffer++) += (sl * a) >> SLOW_ATTACK_SHIFT; \
	vi->attack--; \
    } else { \
	*(buffer++) += sr; \
	*(buffer++) += sl; \
    } \
} while (0)

#define MIX_MONO_AC() do { \
    if (vi->attack) { \
	*(buffer++) += (smp_in * vl * (SLOW_ATTACK - vi->attack)) >> SLOW_ATTACK_SHIFT; \
	vi->attack--; \
    } else { \
	*(buffer++) += smp_in * vl; \
    } \
} while (0)

#define MIX_MONO_AC_FILTER() do { \
    sl = (a0 * smp_in * vl + b0 * fl1 + b1 * fl2) >> FILTER_SHIFT; \
    fl2 = fl1; fl1 = sl; \
    if (vi->attack) { \
	*(buffer++) += (sl * (SLOW_ATTACK - vi->attack)) >> SLOW_ATTACK_SHIFT; \
	vi->attack--; \
    } else { \
	*(buffer++) += sl; \
    } \
} while (0)

#define VAR_NORM(x) \
    register int smp_in; \
    x *sptr = vi->sptr; \
    int pos = vi->pos; \
    int frac = vi->frac

#define VAR_ITPT(x) \
    VAR_NORM(x); \
    int smp_l1, smp_dt

#define VAR_FILTER_MONO \
    int fl1 = vi->filter.l1, fl2 = vi->filter.l2; \
    int64 a0 = vi->filter.a0, b0 = vi->filter.b0, b1 = vi->filter.b1; \
    int sl

#define VAR_FILTER_STEREO \
    VAR_FILTER_MONO; \
    int fr1 = vi->filter.r1, fr2 = vi->filter.r2; \
    int sr

#define SAVE_FILTER_MONO() do { \
    vi->filter.l1 = fl1; \
    vi->filter.l2 = fl2; \
} while (0)

#define SAVE_FILTER_STEREO() do { \
    SAVE_FILTER_MONO(); \
    vi->filter.r1 = fr1; \
    vi->filter.r2 = fr2; \
} while (0)

#define SMIX_MIXER(f) void f(struct mixer_voice *vi, int *buffer, \
    int count, int vl, int vr, int step)


/* Handler for 8 bit samples, interpolated stereo output
 */
SMIX_MIXER(smix_stereo_8bit_linear)
{
    VAR_ITPT(int8);
    while (count--) { INTERPOLATE(); MIX_STEREO_AC(); UPDATE_POS(); }
}


/* Handler for 16 bit samples, interpolated stereo output
 */
SMIX_MIXER(smix_stereo_16bit_linear)
{
    VAR_ITPT(int16);

    vl >>= 8;
    vr >>= 8;
    while (count--) { INTERPOLATE(); MIX_STEREO_AC(); UPDATE_POS(); }
}


/* Handler for 8 bit samples, non-interpolated stereo output
 */
SMIX_MIXER(smix_stereo_8bit_nearest)
{
    VAR_NORM(int8);
    while (count--) { DONT_INTERPOLATE(); MIX_STEREO(); UPDATE_POS(); }
}


/* Handler for 16 bit samples, non-interpolated stereo output
 */
SMIX_MIXER(smix_stereo_16bit_nearest)
{
    VAR_NORM(int16);

    vl >>= 8;
    vr >>= 8;
    while (count--) { DONT_INTERPOLATE(); MIX_STEREO(); UPDATE_POS(); }
}


/* Handler for 8 bit samples, interpolated mono output
 */
SMIX_MIXER(smix_mono_8bit_linear)
{
    VAR_ITPT(int8);

    while (count--) { INTERPOLATE(); MIX_MONO_AC(); UPDATE_POS(); }
}


/* Handler for 16 bit samples, interpolated mono output
 */
SMIX_MIXER(smix_mono_16bit_linear)
{
    VAR_ITPT(int16);

    vl >>= 8;
    while (count--) { INTERPOLATE(); MIX_MONO_AC(); UPDATE_POS(); }
}


/* Handler for 8 bit samples, non-interpolated mono output
 */
SMIX_MIXER(smix_mono_8bit_nearest)
{
    VAR_NORM(int8);

    while (count--) { DONT_INTERPOLATE(); MIX_MONO(); UPDATE_POS(); }
}


/* Handler for 16 bit samples, non-interpolated mono output
 */
SMIX_MIXER(smix_mono_16bit_nearest)
{
    VAR_NORM(int16);

    vl >>= 8;
    while (count--) { DONT_INTERPOLATE(); MIX_MONO(); UPDATE_POS(); }
}

/*
 * Filtering mixers
 */

/* Handler for 8 bit samples, interpolated stereo output
 */
SMIX_MIXER(smix_stereo_8bit_linear_filter)
{
    VAR_ITPT(int8);
    VAR_FILTER_STEREO;

    while (count--) { INTERPOLATE(); MIX_STEREO_AC_FILTER(); UPDATE_POS(); }
    SAVE_FILTER_STEREO();
}


/* Handler for 16 bit samples, interpolated stereo output
 */
SMIX_MIXER(smix_stereo_16bit_linear_filter)
{
    VAR_ITPT(int16);
    VAR_FILTER_STEREO;

    vl >>= 8;
    vr >>= 8;
    while (count--) { INTERPOLATE(); MIX_STEREO_AC_FILTER(); UPDATE_POS(); }
    SAVE_FILTER_STEREO();
}


/* Handler for 8 bit samples, interpolated mono output
 */
SMIX_MIXER(smix_mono_8bit_linear_filter)
{
    VAR_ITPT(int8);
    VAR_FILTER_MONO;

    while (count--) { INTERPOLATE(); MIX_MONO_AC_FILTER(); UPDATE_POS(); }
    SAVE_FILTER_MONO();
}


/* Handler for 16 bit samples, interpolated mono output
 */
SMIX_MIXER(smix_mono_16bit_linear_filter)
{
    VAR_ITPT(int16);
    VAR_FILTER_MONO;

    vl >>= 8;
    while (count--) { INTERPOLATE(); MIX_MONO_AC_FILTER(); UPDATE_POS(); }
    SAVE_FILTER_MONO();
}

