/* CD-ROM file loading. On the console this reads files from the ISO9660 disc; the host test build (CUBE_HOST) reads ../maps/. */
#include "cube.h"
#include <stdio.h>
#include <string.h>
#ifdef CUBE_HOST
#include <ctype.h>
int cd_init(void) { return 1; }
int cd_load(const char *name, void *buf, int bufsize) {
	char path[64] = "../maps/"; int i, n = (int)strlen(path); FILE *f; size_t got;
	for (i = 0; name[i] && n < 60; i++) path[n++] = (char)tolower((unsigned char)name[i]);
	path[n] = 0;
	f = fopen(path, "rb"); if (!f) return 0;
	got = fread(buf, 1, bufsize, f); fclose(f);
	return (int)got;
}
#else
#include <psxcd.h>
int cd_init(void) { CdInit(); return 1; }
int cd_fails;                              /* (debugging) retries, + 100 for each read given up on */
/* reads file `name` (8.3, root directory) into buf (4-byte aligned, bufsize a multiple of 2048); returns its size or 0 */
int cd_load(const char *name, void *buf, int bufsize) {
	char path[32]; CdlFILE file; int len;
	uint8_t mode = CdlModeSpeed;
	music_cd_taken();                        /* the drive leaves the music for a moment */
	/* stop the music's stream first and drop its mode (real-time XA, filtered): read straight after it, the drive
	   could still hand over music sectors - a map's textures came back as music now and then */
	CdControlB(CdlPause, 0, 0);
	CdControlB(CdlSetmode, &mode, 0);
	path[0] = '\\'; strncpy(path + 1, name, 24); path[25] = 0;
	if (!CdSearchFile(&file, path)) return 0;
	len = ((int)file.size + 2047) & ~2047;
	if (len > bufsize) return 0;
	{	/* a read can fail (the drive busy with what it was doing a moment ago): try again a few times */
		int tries;
		for (tries = 0; tries < 4; tries++) {
			CdControl(CdlSetloc, &file.pos, 0);
			CdRead(len / 2048, (uint32_t *)buf, CdlModeSpeed);
			if (CdReadSync(0, 0) >= 0) { cd_fails += tries; return (int)file.size; }
			VSync(0);
		}
	}
	cd_fails += 100;
	return 0;
}
#endif
