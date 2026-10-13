/* The data a native game starts from (kp_game_data.h). */
#include "kp_game_data.h"

#include <string.h>

int kp_game_data_load(KpGameData *d, const KpGameFiles *f, KpError *err)
{
	if (kp_dat_load(&d->dat, f->dat, f->dat_size, "dat.kpd", NULL, err) != 0) return -1;
	if (kp_grpmeta_load(&d->grp, f->grp, f->grp_size, "grpmeta.kpd", NULL, err) != 0) return -1;
	if (kp_iscript_load(&d->script, f->iscript, f->iscript_size, "iscript.kpd", NULL, err) != 0) return -1;
	return kp_mission_load(&d->mission, f->mission, f->mission_size, f->mission_name ? f->mission_name : "mission", NULL, err);
}

void kp_game_data_free(KpGameData *d)
{
	kp_dat_free(&d->dat);
	kp_grpmeta_free(&d->grp);
	kp_iscript_free(&d->script);
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

void kp_game_start_input(KpStartInput *in, KpGameData *d, const KpGameFiles *f, KpRngGame *rng, KpRngCrt *crt,
	int expansion)
{
	memset(in, 0, sizeof *in);
	in->dat = &d->dat;
	in->grp = &d->grp;
	in->script = &d->script;
	in->mission = &d->mission;
	in->tileset = f->tileset;
	in->tileset_size = f->tileset_size;
	in->rng = rng;
	in->crt = crt;
	in->expansion = expansion;
}
