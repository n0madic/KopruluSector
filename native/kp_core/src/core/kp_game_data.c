/* The data a native game starts from (kp_game_data.h). */
#include "kp_game_data.h"

#include <stdlib.h>
#include <string.h>

void kp_game_shared_free(KpGameShared *s)
{
	kp_grpmeta_free(&s->grp);
	kp_iscript_free(&s->script);
	kp_ai_bins_free(&s->ai);
	memset(s, 0, sizeof *s);
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

void kp_game_start_input(KpStartInput *in, KpGameData *d, KpRngGame *rng, KpRngCrt *crt, int expansion)
{
	memset(in, 0, sizeof *in);
	in->dat = &d->dat;
	in->grp = &d->shared->grp;
	in->script = &d->shared->script;
	in->ai_bins = &d->shared->ai;
	in->mission = &d->mission;
	in->rng = rng;
	in->crt = crt;
	in->expansion = expansion;
	in->speed = KP_DEFAULT_SPEED;
}
