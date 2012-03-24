#include <stdlib.h>
#include <string.h>
#include "sound.h"

extern struct sound_driver sound_oss;
extern struct sound_driver sound_alsa;
extern struct sound_driver sound_win32;
extern struct sound_driver sound_coreaudio;

LIST_HEAD(sound_driver_list);

static void register_sound_driver(struct sound_driver *sd)
{
	list_add_tail(&sd->list, &sound_driver_list);	
}

void init_sound_drivers()
{
#ifdef SOUND_COREAUDIO
	register_sound_driver(&sound_coreaudio);
#endif
#ifdef SOUND_WIN32
	register_sound_driver(&sound_win32);
#endif
#ifdef SOUND_OSS
	register_sound_driver(&sound_oss);
#endif
#ifdef SOUND_ALSA
	register_sound_driver(&sound_alsa);
#endif
}

struct sound_driver *select_sound_driver(char *pref, int *rate, int *format)
{
	struct list_head *head;
	struct sound_driver *sd;

	if (pref) {
		list_for_each(head, &sound_driver_list) {
			sd = list_entry(head, struct sound_driver, list);
			if (strcmp(sd->id, pref) == 0) {
				if (sd->init(rate, format) == 0) {
					return sd;
				}
			}
		}
	} else {
		list_for_each(head, &sound_driver_list) {
			sd = list_entry(head, struct sound_driver, list);
			/* Probing */
			if (sd->init(rate, format) == 0) {
				/* found */
				return sd;
			}
		}
	}

	return NULL;
}