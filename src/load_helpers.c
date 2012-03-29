#include <string.h>
#include <fnmatch.h>
#include "common.h"
#include "synth.h"

int exclude_match(char *name)
{
	int i;

	static const char *exclude[] = {
		"README", "readme",
		"*.DIZ", "*.diz",
		"*.NFO", "*.nfo",
		"*.TXT", "*.txt",
		"*.EXE", "*.exe",
		"*.COM", "*.com",
		NULL
	};

	for (i = 0; exclude[i] != NULL; i++) {
		if (fnmatch(exclude[i], name, 0) == 0) {
			return 0;
		}
	}

	return -1;
}

void initialize_module_data(struct module_data *m)
{
	int i;

	/* Reset variables */
	memset(m->mod.name, 0, XMP_NAME_SIZE);
	memset(m->mod.type, 0, XMP_NAME_SIZE);
	m->rrate = PAL_RATE;
	m->c4rate = C4_PAL_RATE;
	m->volbase = 0x40;
	m->vol_table = NULL;
	m->quirk = 0;
	m->read_event_type = READ_EVENT_MOD;
	m->comment = NULL;

	/* Set defaults */
    	m->mod.pat = 0;
    	m->mod.trk = 0;
    	m->mod.chn = 4;
    	m->mod.ins = 0;
    	m->mod.smp = 0;
    	m->mod.spd = 6;
    	m->mod.bpm = 125;
    	m->mod.len = 0;
    	m->mod.rst = 0;
    	m->mod.gvl = 0x40;

	m->synth = &synth_null;
	m->extra = NULL;
	m->time_factor = DEFAULT_TIME_FACTOR;
	m->med_vol_table = NULL;
	m->med_wav_table = NULL;

	for (i = 0; i < 64; i++) {
		m->mod.xxc[i].pan = (((i + 1) / 2) % 2) * 0xff;
		m->mod.xxc[i].vol = 0x40;
		m->mod.xxc[i].flg = 0;
	}
}

void fix_module_instruments(struct module_data *m)
{
	int i, j;

	/* Set appropriate values for instrument volumes and subinstrument
	 * global volumes when QUIRK_INSVOL is not set, to keep volume values
	 * consistent if the user inspects struct xmp_module. We can later
	 * set vlumes in the loaders and eliminate the quirk.
	 */
	for (i = 0; i < m->mod.ins; i++) {
		if (~m->quirk & QUIRK_INSVOL) {
			m->mod.xxi[i].vol = m->volbase;
		}
		for (j = 0; j < m->mod.xxi[i].nsm; j++) {
			if (~m->quirk & QUIRK_INSVOL) {
				m->mod.xxi[i].sub[j].gvl = m->volbase;
			}
		}
	}
}

