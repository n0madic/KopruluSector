/* The data a native game starts from (kp_game_data.h). */
#include "kp_game_data.h"

#include <stdlib.h>
#include <string.h>

int kp_game_shared_load(KpGameShared *s, const uint8_t *grp, size_t grp_size, const uint8_t *iscript,
	size_t iscript_size, const uint8_t *aiscript, size_t aiscript_size, KpError *err)
{
	memset(s, 0, sizeof *s);
	if (kp_grpmeta_load(&s->grp, grp, grp_size, "grpmeta.kpd", NULL, err) != 0
		|| kp_iscript_load(&s->script, iscript, iscript_size, "iscript.kpd", NULL, err) != 0
		|| kp_ai_bins_load(&s->ai, aiscript, aiscript_size, "aiscript.kpd", err) != 0) {
		kp_game_shared_free(s);
		return -1;
	}
	return 0;
}

void kp_game_shared_free(KpGameShared *s)
{
	kp_grpmeta_free(&s->grp);
	kp_iscript_free(&s->script);
	kp_ai_bins_free(&s->ai);
	memset(s, 0, sizeof *s);
}

int kp_game_data_load(KpGameData *d, const KpGameShared *shared, const uint8_t *dat, size_t dat_size,
	uint8_t *mission, size_t mission_size, const char *name, KpError *err)
{
	memset(d, 0, sizeof *d);
	d->shared = shared;
	if (kp_dat_load(&d->dat, dat, dat_size, "dat.kpd", NULL, err) != 0) {
		free(mission);
		kp_game_data_free(d);
		return -1;
	}
	if (kp_mission_take(&d->mission, mission, mission_size, name ? name : "mission", NULL, err) != 0) {
		kp_game_data_free(d);
		return -1;
	}
	return 0;
}

int kp_game_data_from(KpGameData *d, const KpGameShared *shared, const KpDat *dat, KpMission *mission, KpError *err)
{
	memset(d, 0, sizeof *d);
	d->shared = shared;
	d->mission = *mission;
	memset(mission, 0, sizeof *mission);
	if (kp_dat_copy(&d->dat, dat, err) != 0) {
		kp_game_data_free(d);
		return -1;
	}
	return 0;
}

void kp_game_data_free(KpGameData *d)
{
	kp_dat_free(&d->dat);
	kp_mission_free(&d->mission);
	memset(d, 0, sizeof *d);
}

void kp_game_seed(KpRngGame *rng, KpRngCrt *crt, uint32_t seed)
{
	kp_rng_game_init(rng, seed);
	kp_rng_game_seed(rng, seed);
	kp_rng_game_set_enabled(rng, 1);
	kp_rng_crt_init(crt, seed);
}

void kp_game_start_input(KpStartInput *in, KpGameData *d, const uint8_t *tileset, size_t tileset_size, KpRngGame *rng,
	KpRngCrt *crt, int expansion)
{
	memset(in, 0, sizeof *in);
	in->dat = &d->dat;
	in->grp = &d->shared->grp;
	in->script = &d->shared->script;
	in->ai_bins = &d->shared->ai;
	in->mission = &d->mission;
	in->tileset = tileset;
	in->tileset_size = tileset_size;
	in->rng = rng;
	in->crt = crt;
	in->expansion = expansion;
	in->speed = KP_DEFAULT_SPEED;
}
