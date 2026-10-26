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

int kp_host_shared_load(KpHostShared *sh, const uint8_t *grp, size_t grp_size, const uint8_t *iscript,
	size_t iscript_size, const uint8_t *aiscript, size_t aiscript_size, KpError *err)
{
	memset(sh, 0, sizeof *sh);
	if (kp_game_shared_load(&sh->data, grp, grp_size, iscript, iscript_size, aiscript, aiscript_size, err) != 0) return -1;
	sh->grp_crc = kp_crc32(sh->data.grp.blob, sh->data.grp.size);
	sh->script_crc = kp_crc32(sh->data.script.blob, sh->data.script.size);
	sh->ai_crc = kp_crc32(sh->data.ai.blob, sh->data.ai.size);
	return 0;
}

void kp_host_shared_free(KpHostShared *sh)
{
	kp_game_shared_free(&sh->data);
	memset(sh, 0, sizeof *sh);
}

int kp_host_game_load(KpHostGame *hg, const KpHostShared *sh, const uint8_t *dat, size_t dat_size, uint8_t *mission,
	size_t mission_size, const char *name, KpError *err)
{
	memset(hg, 0, sizeof *hg);
	if (!name) name = "mission";
	if (strlen(name) >= sizeof hg->name) {
		snprintf(err->msg, sizeof err->msg, "game: a name of %d bytes or more", KP_SAVE_NAME);
		free(mission);
		return -1;
	}
	snprintf(hg->name, sizeof hg->name, "%s", name);
	hg->shared = sh;
	if (kp_game_data_load(&hg->data, &sh->data, dat, dat_size, mission, mission_size, hg->name, err) != 0) {
		kp_host_game_free(hg);
		return -1;
	}
	hg->dat_crc = kp_crc32(dat, dat_size);
	return 0;
}

void kp_host_tileset_path(const KpHostGame *hg, char *out, size_t size)
{
	snprintf(out, size, KP_TILESET_PATH, KP_TILESET_NAMES[hg->data.mission.tileset]);
}

int kp_host_game_start(KpHostGame *hg, uint8_t *tileset, size_t tileset_size, uint32_t seed, int expansion,
	const KpHostOpts *opts, KpError *err)
{
	KpHostOpts defaults;
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
		(unsigned)hg->shared->grp_crc, (unsigned)hg->shared->script_crc, (unsigned)kp_crc32(tileset, tileset_size),
		(unsigned)kp_crc32(d->mission.blob, d->mission.size), (unsigned)hg->shared->ai_crc);
	kp_game_seed(&hg->rng, &hg->crt, seed);
	kp_game_start_input(&in, &hg->data, tileset, tileset_size, &hg->rng, &hg->crt, expansion);
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
