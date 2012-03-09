#include "test.h"
#include "../src/mixer.h"
#include "../src/virtual.h"

TEST(test_no_quirk_old_ins_vol)
{
	xmp_context opaque;
	struct context_data *ctx;
	struct player_data *p;
	struct mixer_voice *vi;
	int voc;

	opaque = xmp_create_context();
	ctx = (struct context_data *)opaque;
	p = &ctx->p;

 	create_simple_module(ctx, 3, 2);
	set_instrument_volume(ctx, 0, 0, 22);
	set_instrument_volume(ctx, 1, 0, 33);
	set_instrument_volume(ctx, 2, 0, 11);
	new_event(ctx, 0, 0, 0, 60, 1, 44, 0x0f, 2, 0, 0);
	new_event(ctx, 0, 1, 0,  0, 2,  0, 0x00, 0, 0, 0);
	new_event(ctx, 0, 2, 0,  0, 3,  0, 0x00, 0, 0, 0);
	new_event(ctx, 0, 3, 0,  0, 4,  0, 0x00, 0, 0, 0);
	new_event(ctx, 0, 4, 0, 60, 1, 44, 0x00, 0, 0, 0);
	new_event(ctx, 0, 5, 0, 50, 0,  0, 0x00, 0, 0, 0);

	xmp_player_start(opaque, 0, 44100, 0);

	/* Row 0 */
	xmp_player_frame(opaque);

	voc = map_virt_channel(p, 0);
	fail_unless(voc >= 0, "virtual map");
	vi = &p->virt.voice_array[voc];

	fail_unless(vi->note == 59, "set note");
	fail_unless(vi->ins  ==  0, "set instrument");
	fail_unless(vi->vol  == 43 * 16, "set volume");
	fail_unless(vi->pos0 ==  0, "sample position");

	xmp_player_frame(opaque);

	/* Row 1 */
	xmp_player_frame(opaque);
	fail_unless(vi->ins  ==  0, "not original instrument");
	fail_unless(vi->note == 59, "not same note");
	fail_unless(vi->vol  == 33 * 16, "not new volume");
	fail_unless(vi->pos0 !=  0, "sample reset");
	xmp_player_frame(opaque);

	/* Row 2 - valid ins, play with original volume, not previous */
	xmp_player_frame(opaque);
	fail_unless(vi->ins  ==  0, "not original instrument");
	fail_unless(vi->note == 59, "not same note");
	fail_unless(vi->vol  == 11 * 16, "not new volume");
	fail_unless(vi->pos0 !=  0, "sample reset");
	xmp_player_frame(opaque);

	/* Row 3 - invalid ins, play with original volume, not previous */
	xmp_player_frame(opaque);
	fail_unless(vi->vol  ==  0, "didn't cut");
	xmp_player_frame(opaque);

	/* Row 4 - set note again */
	xmp_player_frame(opaque);
	fail_unless(vi->note == 59, "set note");
	fail_unless(vi->ins  ==  0, "set instrument");
	fail_unless(vi->vol  == 43 * 16, "set volume");
	fail_unless(vi->pos0 ==  0, "sample position");
	xmp_player_frame(opaque);

	/* Row 5 - check new note, no instrument */
	xmp_player_frame(opaque);
	fail_unless(vi->ins  ==  0, "not original instrument");
	fail_unless(vi->note == 49, "not new note");
	fail_unless(vi->vol  == 43 * 16, "not current volume");
	fail_unless(vi->pos0 ==  0, "sample reset");
	xmp_player_frame(opaque);
}
END_TEST
