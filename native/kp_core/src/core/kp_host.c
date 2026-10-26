/* A host's native game (kp_host.h). */
#include "kp_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const TILESETS[8] = { "badlands", "platform", "install", "ashworld", "jungle", "desert", "ice",
	"twilight" };

void kp_host_opts(KpHostOpts *o)
{
	memset(o, 0, sizeof *o);
	o->speed = KP_NIL;
}

static void files_of(const KpHostGame *hg, KpGameFiles *f)
{
	memset(f, 0, sizeof *f);
	f->mission_name = hg->name;
	f->dat = hg->files[KP_HOST_DAT];
	f->dat_size = hg->sizes[KP_HOST_DAT];
	f->grp = hg->files[KP_HOST_GRP];
	f->grp_size = hg->sizes[KP_HOST_GRP];
	f->iscript = hg->files[KP_HOST_ISCRIPT];
	f->iscript_size = hg->sizes[KP_HOST_ISCRIPT];
	f->mission = hg->files[KP_HOST_MISSION];
	f->mission_size = hg->sizes[KP_HOST_MISSION];
	f->aiscript = hg->files[KP_HOST_AISCRIPT];
	f->aiscript_size = hg->sizes[KP_HOST_AISCRIPT];
	f->tileset = hg->files[KP_HOST_TILESET];
	f->tileset_size = hg->sizes[KP_HOST_TILESET];
}

int kp_host_game_load(KpHostGame *hg, uint8_t *files[KP_HOST_FILES], const size_t sizes[KP_HOST_FILES],
	const char *name, KpError *err)
{
	KpGameFiles f;
	int k;
	memset(hg, 0, sizeof *hg);
	for (k = 0; k < KP_HOST_FILES; k++) {
		hg->files[k] = files[k];
		hg->sizes[k] = sizes[k];
	}
	if (strlen(name) >= sizeof hg->name) {
		snprintf(err->msg, sizeof err->msg, "game: a name of %d bytes or more", KP_SAVE_NAME);
		kp_host_game_free(hg);
		return -1;
	}
	snprintf(hg->name, sizeof hg->name, "%s", name);
	files_of(hg, &f);
	if (kp_game_data_load(&hg->data, &f, err) != 0) {
		kp_host_game_free(hg);
		return -1;
	}
	return 0;
}

const char *kp_host_tileset_name(const KpHostGame *hg) { return TILESETS[hg->data.mission.era % 8]; }

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
	if (tileset) {
		free(hg->files[KP_HOST_TILESET]);
		hg->files[KP_HOST_TILESET] = tileset;
		hg->sizes[KP_HOST_TILESET] = tileset_size;
	}
	if (!hg->files[KP_HOST_TILESET]) {
		snprintf(err->msg, sizeof err->msg, "game: no tileset");
		kp_host_game_free(hg);
		return -1;
	}
	snprintf(hg->identity, sizeof hg->identity, "%08x-%08x-%08x-%08x-%08x-%08x",
		(unsigned)kp_crc32(hg->files[KP_HOST_DAT], hg->sizes[KP_HOST_DAT]), (unsigned)kp_crc32(d->grp.blob, d->grp.size),
		(unsigned)kp_crc32(d->script.blob, d->script.size),
		(unsigned)kp_crc32(hg->files[KP_HOST_TILESET], hg->sizes[KP_HOST_TILESET]),
		(unsigned)kp_crc32(d->mission.blob, d->mission.size), (unsigned)kp_crc32(d->ai.blob, d->ai.size));
	files_of(hg, &f);
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
	int k;
	if (hg->g) kp_game_free(hg->g);
	kp_game_data_free(&hg->data);
	for (k = 0; k < KP_HOST_FILES; k++) free(hg->files[k]);
	memset(hg, 0, sizeof *hg);
}
