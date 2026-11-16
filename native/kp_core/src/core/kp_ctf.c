/* Capture the Flag (atlas P07 0x4213c0..0x421760; ADR 0024): the flag beacons' orders CtfCopInit 0x99 (0x421440: the
 * Flag appears once the game is ten minutes old), CtfCopStarted 0x9a (0x4213c0: the beacon scans its box with
 * CtfBeaconCallback 0x4215a0) and CtfCop2 0x9b (0x4214d0: the beacon moves where its owner placed it), and the flag
 * messages ShowFlagCapturedMessage 0x421650, ShowFlagReturnedMessage 0x4216e0 and ShowFlagTakenMessage 0x421760. The
 * beacons are made at the start (kp_gametype.c, CreateFlagBeacons 0x4480f0). */
#include "kp_game_internal.h"
#include "kp_gametype.h"

#define CTF_SECONDS 600      /* CtfCopInit waits for the elapsed seconds 0x5349d8 (unsigned) */
#define BUTTONS_NONE 0xe4    /* SetUnitButtonSet 0x422f00's set 228 */
#define SCAN_TIMER 15        /* +0x85: the next scan after 16 dispatcher visits */
#define DAT_WORKER 0x8
#define SPRITE_SELECTED 0x08 /* the sprite +0xe flag 8 */
#define SOUND_FLAG 0x20      /* + the local race */
#define STRING_TAKEN 0x7d    /* rez\network.tbl, 1-based: "%s had his flag taken!" */
#define STRING_RETURNED 0x7f
#define STRING_CAPTURED 0x81
#define TEAM_STRING 1 /* each string's team form follows it: 0x7e, 0x80, 0x82 "%s had their flag ..." */
/* The CTF placement errors 0x508d90 by the validator's result (entry 0 none) */
static const uint16_t PLACEMENT_STRINGS[12] = { 0, 0x355, 0x354, 0x356, 0x358, 0x35c, 0x35d, 0x35e, 0x35b, 0x35a,
	0x359, 0x357 };

/* The message of player p's flag in a Capture the Flag game: the local race's sound without a unit (0x446940 with
 * [1, 0]) when it has one, and string `string` with the name in p's record; a team game's next string (their flag)
 * with the name of p's force (GetForceName 0x4b2bf0). */
static void message(KpGame *g, int64_t p, int64_t string, int sound)
{
	int64_t race;
	if (!kp_setup_ctf(&g->setup)) return;
	if (sound) {
		/* 0x5264c2 (InitLocalPlayerAndChatTargets 0x443be0): the seat's lobby race, its own record's */
		race = g->w->replay ? (int64_t)KP_RACE_TERRAN : g->players.race[g->local_record];
		kp_unit_sound(g, SOUND_FLAG + race, 0, KP_SOUND_ON, 0, KP_TALK_NONE, 0, 0);
	}
	p = kp_band(p, 0xff);
	if (kp_setup_team_game(&g->setup)) {
		if (p >= KP_PLAYERS) kp_game_fail(g, "ctf: the force of player %lld", (long long)p);
		kp_event(g, KP_EVENT_FORCE_MESSAGE, string + TEAM_STRING, g->players.force[p], 0, NULL);
	} else {
		kp_event(g, KP_EVENT_PLAYER_MESSAGE, string, p, 0, NULL);
	}
}

void kp_ctf_flag_taken(KpGame *g, int64_t p) { message(g, p, STRING_TAKEN, 1); }
void kp_ctf_flag_returned(KpGame *g, int64_t p) { message(g, p, STRING_RETURNED, 0); }

/* CtfBeaconCallback 0x4215a0 over a unit u in beacon b's box: b's own worker carrying a Flag captures it (the Flag's
 * owner's message, the Flag dies with the worker's cargo, DestroyWorkerCargo 0x4271d0); another player's Flag
 * brought home dies (KillUnitFlagged 0x425700). */
static void beacon_visit(KpGame *g, int64_t u, int64_t b)
{
	KpUnits *un = &g->u;
	int64_t carried;
	if (un->owner[u] == un->owner[b]) {
		if (kp_band(KP_DAT(g, units, special_ability_flags, un->type[u]), DAT_WORKER) == 0) return;
		carried = un->union_0xc0[u];
		if (carried == 0 || un->type[carried] != KP_TYPE_FLAG) return;
		message(g, un->owner[carried], STRING_CAPTURED, 1);
		kp_destroy_worker_cargo(g, u);
	} else if (un->type[u] == KP_TYPE_FLAG) {
		message(g, un->owner[u], STRING_CAPTURED, 1);
		kp_kill_flagged(g, u);
	}
}

/* Order_CtfCopStarted 0x4213c0 (order 0x9a): the units in the beacon's box (its units.dat extents about its sprite,
 * 16-bit) in the border query's order (FindUnitInRect 0x442b60, the callback never stops it). */
void kp_order_ctfcop_started(KpGame *g, int64_t n)
{
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n);
	int64_t t = g->u.type[n], x = w->spr.x[s], y = w->spr.y[s], *found;
	int count, k;
	found = kp_query_rect(g, kp_s16v(x - KP_DAT(g, units, unit_extents, t * 4)), kp_s16v(y - KP_DAT(g, units, unit_extents, t * 4 + 1)),
		kp_s16v(x + KP_DAT(g, units, unit_extents, t * 4 + 2)), kp_s16v(y + KP_DAT(g, units, unit_extents, t * 4 + 3)), &count);
	for (k = 0; k < count; k++) beacon_visit(g, found[k], n);
	kp_query_release(g, found);
	g->u.queue_timer[n] = SCAN_TIMER;
}

/* Order_CtfCopInit 0x421440 (order 0x99): from the tenth minute the beacon's buttons go, the placement ends when the
 * beacon is selected (its sprite's flag: the local selection's; 0x508d48 is 0x508d54 outside the commands' execution),
 * and its owner's Flag appears on it (shown, completed in place); the beacon scans. */
void kp_order_ctfcop_init(KpGame *g, int64_t n)
{
	KpImages *w = g->w;
	int32_t s;
	int64_t f;
	if (kp_u32v(kp_scalar(g, KP_ELAPSED)) < CTF_SECONDS) return;
	kp_set_button_set(g, n, BUTTONS_NONE);
	s = kp_unit_sprite(g, n);
	if (kp_band(w->spr.flags[s], SPRITE_SELECTED) != 0 && kp_scalar(g, KP_PLACEMENT_ACTIVE) == 1) {
		kp_placement_close(g); /* EndPlacement 0x47d300(0, 0, 1) */
		kp_event(g, KP_EVENT_CURSOR_MODES, 0, 0, 0, NULL);
	}
	f = kp_unit_create(g, KP_TYPE_FLAG, kp_s16v(w->spr.x[s]), kp_s16v(w->spr.y[s]), g->u.owner[n]);
	if (f == 0) return;
	kp_unit_show(g, f);              /* 0x422f60 */
	kp_unit_complete_in_place(g, f); /* 0x424da0 */
	kp_issue_order(g, n, KP_ORD_CTFCOP_STARTED);
}

/* Order_CtfCop2 0x4214d0 (order 0x9b, every visit): the beacon moves to its order's point when it may stand there
 * (ValidateBuildingPlacement 0x412f10 with [0, 0, 1, 0]), its buttons gone; else its owner sees the error. Then it
 * waits for the Flag again. */
void kp_order_ctfcop2(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], hw = kp_s16v(KP_DAT(g, units, staredit_placement_size, t * 2)),
		hh = kp_s16v(KP_DAT(g, units, staredit_placement_size, t * 2 + 1)), tx, ty, e;
	tx = kp_idiv64(g, kp_s16v(u->order_x[n]) - kp_idiv64(g, hw, 2), 32);
	ty = kp_idiv64(g, kp_s16v(u->order_y[n]) - kp_idiv64(g, hh, 2), 32);
	e = kp_validate_placement(g, n, u->owner[n], tx, ty, t, 0, 1, 0);
	if (e == 0) {
		kp_unit_deactivate_spatial(g, n); /* 0x41a210 */
		kp_unit_relocate(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n])); /* 0x43a270 */
		kp_unit_activate_spatial(g, n);   /* 0x41a050 */
		kp_set_button_set(g, n, BUTTONS_NONE);
	} else {
		kp_worker_placement_error(g, n, e >= 0 && e < 12 ? PLACEMENT_STRINGS[e] : 0);
	}
	kp_issue_order(g, n, KP_ORD_CTFCOP_INIT);
}
