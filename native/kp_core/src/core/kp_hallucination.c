/* Hallucinations (kp_game.h), ported from the atlas (P13): the copies of CreateHallucinationOf 0x430ed0 with
 * InitHallucinationUnit 0x430960 and PlaceHallucination 0x430fa0, their end on any spell effect (secondary order 149,
 * 0x430c70) and the smoke of their death (Order_Die 0x41e630). */
#include "kp_game_internal.h"

#define DAT_SUBUNIT 0x10
#define STATUS_COMPLETED 0x1
#define STATUS_CAN_TURN 0x10000
#define STATUS_HALLUCINATION 0x40000000
#define HALLUCINATION_BUTTONS 244
#define SECONDARY_HALLUCINATION 149
#define METHOD_NORMAL 0
#define METHOD_HALLUCINATION 16
#define SCARAB_CARRIERS(t) ((t) == 83 || (t) == 81)
#define FACING_RANDOM 30
#define FACINGS 32
#define DEATH_SOUND 619
#define DEATH_SPRITE 374

/* SetSpriteHallucinationDraw 0x4171c0: the normal images draw remapped (the local player's own hallucinations). */
static void hallucination_draw(KpImages *w, int32_t s)
{
	int32_t i;
	for (i = w->spr.images[s].head; i != KP_NONE; i = w->img.next[i]) {
		if (w->img.method[i] != METHOD_NORMAL && w->img.method[i] != METHOD_HALLUCINATION) continue;
		kp_image_set_method(w, i, METHOD_HALLUCINATION);
		w->img.param[i] = 0;
	}
}

/* InitHallucinationUnit 0x430960: the unit and its subunit chain become uncounted, completed hallucinations. */
static void init_hallucination(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	for (;;) {
		kp_account_live(g, n, -1);
		kp_set_button_set(g, n, HALLUCINATION_BUTTONS);
		u->hp[n] = KP_DAT(g, units, hit_points, u->type[n]);
		u->status[n] = kp_bor(u->status[n], STATUS_HALLUCINATION | STATUS_COMPLETED);
		kp_account_live(g, n, 1);
		kp_account_completed(g, n, 1, 0);
		kp_issue_order(g, n, KP_DAT(g, units, human_ai_idle, u->type[n]));
		if (w->replay || u->owner[n] == g->local_player) hallucination_draw(w, kp_unit_sprite(g, n));
		if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_SUBUNIT) != 0) return;
		kp_refresh_unit_strength(g, n);
		kp_set_secondary_order(g, n, SECONDARY_HALLUCINATION);
		n = u->subunit[n];
		if (n == 0) return;
	}
}

/* CreateHallucinationOf 0x430ed0: a hidden copy of target for owner, 0 when no unit is free. */
int64_t kp_create_hallucination(KpGame *g, int64_t target, int64_t owner)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, target);
	int64_t h = kp_unit_create(g, u->type[target], kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), owner), life;
	if (h == 0) return 0; /* ReportUnitCreationError 0x425020: the interface's */
	init_hallucination(g, h);
	life = kp_mod(kp_timed_life(g, h), 65536);
	if (u->remove_timer[h] == 0 || life < u->remove_timer[h]) u->remove_timer[h] = life;
	if (SCARAB_CARRIERS(u->type[h])) kp_hangar_hallucinate(g, h, target);
	kp_ai_assign_completed(g, h, h); /* AssignCompletedUnitAi 0x48d320 */
	return h;
}

/* PlaceHallucination 0x430fa0: 0 when no place fits; else the copy turns at random and appears. */
int kp_place_hallucination(KpGame *g, int64_t h)
{
	int32_t s = kp_unit_sprite(g, h);
	int64_t x, y, d;
	if (!kp_find_unit_placement(g, h, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), NULL, 0, &x, &y)) return 0;
	kp_unit_relocate(g, h, x, y);
	if (kp_band(g->u.status[h], STATUS_CAN_TURN) != 0) {
		d = kp_rng_game_range(g->rng, FACING_RANDOM, 0, FACINGS);
		kp_sprite_set_direction(g->w, s, (int32_t)d);
		kp_flingy_set_heading(g, h, d * 8);
	}
	kp_unit_show(g, h);
	return 1;
}

/* The secondary order Hallucination2 (0x430c70): any spell effect ends a hallucination. */
void kp_order_hallucination_end(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->matrix_damage[n] != 0 || u->stim_timer[n] != 0 || u->ensnare_timer[n] != 0 || u->lockdown_timer[n] != 0
		|| u->irradiate_timer[n] != 0 || u->stasis_timer[n] != 0 || u->parasites[n] != 0 || u->storm_timer[n] != 0
		|| u->plague_timer[n] != 0 || u->blind[n] != 0 || u->maelstrom_timer[n] != 0) {
		kp_unit_kill(g, n);
	}
}

/* A hallucination dies before its time (Order_Die 0x41e630): a sound and a puff of smoke, and it is gone. */
void kp_hallucination_death(KpGame *g, int64_t n)
{
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n), t;
	kp_play_unit_sound(g, DEATH_SOUND, n);
	t = kp_lone_create(w, DEATH_SPRITE, w->spr.x[s], w->spr.y[s], 0);
	if (t != KP_NONE) {
		int32_t ts = w->thg.sprite[t];
		w->spr.elevation[ts] = w->spr.elevation[s] + 1;
		kp_sprite_set_visibility(w, ts, w->spr.visibility[s]);
	}
	kp_unit_hide(g, n);
	kp_unit_destroy(g, n);
}
