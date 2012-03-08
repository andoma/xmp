#include "test.h"
#include "../src/mixer.h"
#include "../src/virtual.h"

/*
Case 2: New instrument (no note)

  Instrument -> None    Same    Valid   Inval
PT1.1           -       Play    Play    Cut
PT1.3           -       NewVol  NewVol* Cut
PT2.3           -       NewVol  NewVol* Cut
PT3.15          -       NewVol  NewVol  Cut     <= "Standard"
PT3.61          -       NewVol  NewVol  Cut     <=
PT4b2           -       NewVol  NewVol  Cut     <=
MED             -       Hold    Hold    Cut%
FT2             -       OldVol  OldVol  OldVol
ST3             -       NewVol  NewVol  Cont
IT(s)           -       NewVol  NewVol  Cont
IT(i)           -       NewVol# Play    Cont
DT32            -       NewVol# NewVol# Cut

Play    = Play new note with new default volume
Switch  = Play new note with current volume
NewVol  = Don't play sample, set new default volume
OldVol  = Don't play sample, set old default volume
Cut     = Stop playing sample

  * Protracker 1.3/2.3 switches to new sample in the line after the new
    instrument event. The new instrument is not played from start (i.e. a
    short transient sample may not be played). This behaviour is NOT
    emulated by the current version of xmp.

    00 C-2 03 A0F  <=  Play instrument 03 and slide volume down
    01 --- 02 000  <=  Set volume of instrument 02, playing instrument 03
    02 --- 00 000  <=  Switch to instrument 02 (weird!)

    00 C-2 03 000  <=  Play instrument 03
    01 A-3 02 308  <=  Start portamento with instrument 03
    02 --- 00 xxx  <=  Switch to instrument 02 (weird!)
*/

TEST(test_player_valid_instrument)
{
	xmp_context opaque;
	struct context_data *ctx;
	struct player_data *p;
	struct mixer_voice *vi;
	int voc;

	opaque = xmp_create_context();
	ctx = (struct context_data *)opaque;
	p = &ctx->p;

 	create_simple_module(ctx);
	set_instrument_volume(ctx, 0, 0, 22);
	set_instrument_volume(ctx, 1, 0, 33);
	new_event(ctx, 0, 0, 0, 60, 2, 40, 0x0f, 2, 0, 0);
	new_event(ctx, 0, 1, 0,  0, 1,  0, 0x00, 0, 0, 0);

	xmp_player_start(opaque, 0, 44100, 0);

	/* Row 0 */
	xmp_player_frame(opaque);

	voc = map_virt_channel(p, 0);
	fail_unless(voc >= 0, "virtual map");
	vi = &p->virt.voice_array[voc];

	fail_unless(vi->note == 59, "set note");
	fail_unless(vi->ins  ==  1, "set instrument");
	fail_unless(vi->vol  == 39 * 16, "set volume");
	fail_unless(vi->pos0 ==  0, "sample position");

	xmp_player_frame(opaque);

	/* Row 1: valid instrument with no note (PT 3.15)
	 *
	 * When a new valid instrument, different from the current instrument
	 * and no note is set, PT3.15 keeps playing the current sample but
	 * sets the volume to the new instrument's default volume.
	 */
	xmp_player_frame(opaque);
	fail_unless(vi->ins  ==  1, "not original instrument");
	fail_unless(vi->note == 59, "not same note");
	fail_unless(vi->vol  == 33 * 16, "not new volume");
	fail_unless(vi->pos0 !=  0, "sample reset");

	xmp_player_frame(opaque);

	/* Row 2: valid instrument with no note (FT2)
	 *
	 * When a new valid instrument, different from the current instrument
	 * and no note is set, FT2 keeps playing the current sample but
	 * sets the volume to the old instrument's default volume.
	 */
	set_quirk(ctx, QUIRK_OINSVOL);

	xmp_player_frame(opaque);
	fail_unless(vi->ins  ==  1, "not original instrument");
	fail_unless(vi->note == 59, "not same note");
	fail_unless(vi->vol  == 22 * 16, "not old volume");
	fail_unless(vi->pos0 !=  0, "sample reset");


	
}
END_TEST
