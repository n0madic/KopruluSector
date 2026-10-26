/* A host's native game (kp_host.h). */
#include "kp_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *kp_read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	uint8_t *data = NULL;
	long n;
	*size = 0;
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
		data = (uint8_t *)malloc((size_t)n + 1);
		if (data && fread(data, 1, (size_t)n, f) == (size_t)n) {
			data[n] = 0;
			*size = (size_t)n;
		} else {
			free(data);
			data = NULL;
		}
	}
	fclose(f);
	return data;
}

void kp_host_opts(KpHostOpts *o)
{
	memset(o, 0, sizeof *o);
	o->speed = KP_NIL;
}

int kp_host_game_load(KpHostGame *hg, const uint8_t *const files[KP_HOST_FILES], const size_t sizes[KP_HOST_FILES],
	const char *name, KpError *err)
{
	KpGameFiles f;
	memset(hg, 0, sizeof *hg);
	if (strlen(name) >= sizeof hg->name) {
		snprintf(err->msg, sizeof err->msg, "game: a name of %d bytes or more", KP_SAVE_NAME);
		return -1;
	}
	snprintf(hg->name, sizeof hg->name, "%s", name);
	memset(&f, 0, sizeof f);
	f.mission_name = hg->name;
	f.dat = files[KP_HOST_DAT];
	f.dat_size = sizes[KP_HOST_DAT];
	f.grp = files[KP_HOST_GRP];
	f.grp_size = sizes[KP_HOST_GRP];
	f.iscript = files[KP_HOST_ISCRIPT];
	f.iscript_size = sizes[KP_HOST_ISCRIPT];
	f.mission = files[KP_HOST_MISSION];
	f.mission_size = sizes[KP_HOST_MISSION];
	f.aiscript = files[KP_HOST_AISCRIPT];
	f.aiscript_size = sizes[KP_HOST_AISCRIPT];
	if (kp_game_data_load(&hg->data, &f, err) != 0) {
		kp_host_game_free(hg);
		return -1;
	}
	hg->dat_crc = kp_crc32(files[KP_HOST_DAT], sizes[KP_HOST_DAT]);
	return 0;
}

const char *kp_host_tileset_name(const KpHostGame *hg) { return KP_TILESET_NAMES[hg->data.mission.tileset]; }

int kp_host_game_start(KpHostGame *hg, uint8_t *tileset, size_t tileset_size, uint32_t seed, int expansion,
	const KpHostOpts *opts, KpError *err)
{
	KpHostOpts defaults;
	KpGameFiles f;
	KpStartInput in;
	const KpGameData *d = &hg->data;
	if (!opts) {
		kp_host_opts(&defaults);
		opts = &defaults;
	}
	free(hg->tileset);
	hg->tileset = tileset;
	hg->tileset_size = tileset_size;
	if (!tileset) {
		snprintf(err->msg, sizeof err->msg, "game: no tileset");
		kp_host_game_free(hg);
		return -1;
	}
	snprintf(hg->identity, sizeof hg->identity, "%08x-%08x-%08x-%08x-%08x-%08x", (unsigned)hg->dat_crc,
		(unsigned)kp_crc32(d->grp.blob, d->grp.size), (unsigned)kp_crc32(d->script.blob, d->script.size),
		(unsigned)kp_crc32(tileset, tileset_size), (unsigned)kp_crc32(d->mission.blob, d->mission.size),
		(unsigned)kp_crc32(d->ai.blob, d->ai.size));
	memset(&f, 0, sizeof f);
	f.tileset = tileset;
	f.tileset_size = tileset_size;
	kp_game_seed(&hg->rng, &hg->crt, seed);
	kp_game_start_input(&in, &hg->data, &f, &hg->rng, &hg->crt, expansion);
	in.no_units = opts->no_units;
	in.no_events = !opts->events && !opts->trace;
	in.trace_triggers = opts->trace;
	in.mission_index = opts->mission_index;
	in.cheats = opts->cheats;
	in.carry = opts->carry;
	in.turn = opts->turn;
	if (opts->speed != KP_NIL) in.speed = opts->speed;
	hg->g = kp_start(&in, err);
	if (!hg->g) {
		kp_host_game_free(hg);
		return -1;
	}
	hg->g->ctx = hg;
	return 0;
}

void kp_host_game_free(KpHostGame *hg)
{
	if (hg->g) kp_game_free(hg->g);
	kp_game_data_free(&hg->data);
	free(hg->tileset);
	memset(hg, 0, sizeof *hg);
}
