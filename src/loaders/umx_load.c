/* Extended Module Player UMX module loader
 * Copyright (C) 2007 Claudio Matsuoka
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU General Public License. See doc/COPYING
 * for more information.
 */

#include "load.h"

#define TEST_SIZE 1500

#define MAGIC_UMX	MAGIC4(0xc1,0x83,0x2a,0x9e)
#define MAGIC_IMPM	MAGIC4('I','M','P','M')
#define MAGIC_SCRM	MAGIC4('S','C','R','M')
#define MAGIC_M_K_	MAGIC4('M','.','K','.')

extern struct format_loader xm_loader;
extern struct format_loader it_loader;
extern struct format_loader s3m_loader;
extern struct format_loader mod_loader;

static int umx_test (FILE *, char *, const int);
static int umx_load (struct context_data *, FILE *, const int);

struct format_loader umx_loader = {
	"UMX",
	"Epic Games Unreal/UT",
	umx_test,
	umx_load
};

static int umx_test(FILE *f, char *t, const int start)
{
	int i, offset = -1;
	uint8 buf[TEST_SIZE], *b = buf;
	uint32 id;

	if (fread(buf, 1, TEST_SIZE, f) < TEST_SIZE)
		return -1;
;
	id = readmem32b(b);

	if (id != MAGIC_UMX)
		return -1;

	for (i = 0; i < TEST_SIZE; i++, b++) {
		id = readmem32b(b);

		if (!memcmp(b, "Extended Module:", 16)) {
			offset = i;
			break;
		}
		if (id == MAGIC_IMPM) { 
			offset = i;
			break;
		}
		if (i > 44 && id == MAGIC_SCRM) { 
			offset = i - 44;
			break;
		}
		if (i > 1080 && id == MAGIC_M_K_) { 
			offset = i - 1080;
			break;
		}
	}
	
	if (offset < 0)
		return -1;

	return 0;
}

static int umx_load(struct context_data *ctx, FILE *f, const int start)
{
	struct module_data *m = &ctx->m;
	int i;
	uint8 buf[TEST_SIZE], *b = buf;
	uint32 id;

	LOAD_INIT();

	_D(_D_INFO "Container type : Epic Games UMX");

	fread(buf, 1, TEST_SIZE, f);

	for (i = 0; i < TEST_SIZE; i++, b++) {
		id = readmem32b(b);

		if (!memcmp(b, "Extended Module:", 16))
			return xm_loader.loader(ctx, f, i);

		if (id == MAGIC_IMPM)
			return it_loader.loader(ctx, f, i);

		if (i > 44 && id == MAGIC_SCRM)
			return s3m_loader.loader(ctx, f, i - 44);

		if (i > 1080 && id == MAGIC_M_K_)
			return mod_loader.loader(ctx, f, i - 1080);
	}
	
	return -1;
}





