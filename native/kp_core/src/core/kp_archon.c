/* Archon merges (kp_game.h), ported from the atlas (P10, P25): the commands Merge Archon 0x2a (0x4737b0) and Merge
 * Dark Archon 0x5a (0x473870) with TakeNearestUnit 0x472040, the orders ArchonWarp 105 (0x42f830) and DarkArchonMeld
 * 183 (0x42fa70) with the merged statuses (0x42f3f0), and CompletingArchonSummon 106 (0x42f9e0). */
#include "kp_game_internal.h"

#define HIGH_TEMPLAR 67
#define DARK_TEMPLAR 61
#define ARCHON 68
#define DARK_ARCHON 63
#define ARCHON_WARP 23 /* the techs */
#define DARK_ARCHON_MELD 28
#define ORDER_ARCHON_WARP 105
#define ORDER_COMPLETING_SUMMON 106
#define ORDER_DARK_ARCHON_MELD 183
#define ORDER_RESET_COLLISION 150
#define STATUS_CAN_MOVE 0x20000
#define STATUS_COLLIDES 0x100000
#define STATUS_NOT_INTERRUPTIBLE 0x1000
#define SPRITE_NO_BREAK 0x80
#define SIGNAL_SUMMONED 0x04
#define NO_BUTTONS 228
#define ANIM_INIT 0
#define ANIM_SPECIAL1 13
#define DECLOAK_SOUND 274
#define MELD_ENERGY 0x3200
#define CHEAT_FAST_BUILD 0x2
#define NO_TYPE 228

/* TakeNearestUnit 0x472040: the nearest of count candidates (by squared distance, the first minimum) leaves the
 * array; the earlier best takes its slot. */
static int64_t take_nearest(KpGame *g, int64_t n, int64_t *candidates, int count)
{
	int32_t s = kp_unit_sprite(g, n);
	int64_t best = 0, x = kp_s16v(g->w->spr.x[s]), y = kp_s16v(g->w->spr.y[s]);
	uint32_t best_d = 0x7fffffff;
	int k;
	for (k = 0; k < count; k++) {
		int64_t c = candidates[k], dx, dy;
		int32_t cs;
		uint32_t d;
		if (c == 0) continue;
		cs = kp_unit_sprite(g, c);
		dx = kp_s16v(g->w->spr.x[cs]) - x;
		dy = kp_s16v(g->w->spr.y[cs]) - y;
		d = (uint32_t)(dx * dx + dy * dy);
		if (d < best_d) {
			candidates[k] = best;
			best = c;
			best_d = d;
		}
	}
	return best;
}

/* ExecMergeArchon 0x4737b0 / ExecMergeDarkArchon 0x473870: the selected templars pair with their nearest. */
void kp_command_merge(KpGame *g, int64_t player, int dark)
{
	int64_t units[KP_SLOTS], templars[KP_SLOTS] = { 0 }, type = dark ? DARK_TEMPLAR : HIGH_TEMPLAR;
	int64_t order = dark ? ORDER_DARK_ARCHON_MELD : ORDER_ARCHON_WARP;
	int count = kp_selected_units(g, player, units), n = 0, k;
	if (count == 0) return;
	/* only the first selected unit's tech is checked; its error feedback is the interface's */
	if (kp_can_use_tech(g, dark ? DARK_ARCHON_MELD : ARCHON_WARP, player, units[0]) != 1) return;
	for (k = 0; k < count; k++) {
		if (g->u.type[units[k]] == type) templars[n++] = units[k];
	}
	for (k = 0; k < n - 1; k++) {
		int64_t a = templars[k], b;
		if (a == 0) continue;
		b = take_nearest(g, a, &templars[k + 1], n - 1 - k);
		if (b == 0) continue;
		kp_issue_order_target(g, a, order, b);
		kp_issue_order_target(g, b, order, a);
	}
}

static void larger_byte(int64_t *into, int64_t from)
{
	if (kp_mod(from, 256) > kp_mod(*into, 256)) *into = kp_mod(from, 256);
}

/* MergeStatusTimers 0x42f3f0: the archon keeps the longer of each effect and the partner's acid spores. */
static void merge_statuses(KpGame *g, int64_t n, int64_t p)
{
	KpUnits *u = &g->u;
	if (kp_mod(u->matrix_damage[p], 65536) > kp_mod(u->matrix_damage[n], 65536)) u->matrix_damage[n] = u->matrix_damage[p];
	larger_byte(&u->matrix_timer[n], u->matrix_timer[p]);
	larger_byte(&u->stim_timer[n], u->stim_timer[p]);
	larger_byte(&u->ensnare_timer[n], u->ensnare_timer[p]);
	larger_byte(&u->lockdown_timer[n], u->lockdown_timer[p]);
	larger_byte(&u->maelstrom_timer[n], u->maelstrom_timer[p]);
	if (kp_mod(u->irradiate_timer[p], 256) > kp_mod(u->irradiate_timer[n], 256)) {
		u->irradiate_timer[n] = u->irradiate_timer[p];
		u->irradiated_by[n] = u->irradiated_by[p];
		u->irradiate_player[n] = u->irradiate_player[p];
	}
	larger_byte(&u->stasis_timer[n], u->stasis_timer[p]);
	larger_byte(&u->plague_timer[n], u->plague_timer[p]);
	u->parasites[n] = kp_bor(u->parasites[n], u->parasites[p]);
	u->blind[n] = kp_bor(u->blind[n], u->blind[p]);
	u->acid_spores[n] = u->acid_spores[p];
	u->acid_time0[n] = u->acid_time0[p];
	u->acid_time1[n] = u->acid_time1[p];
	u->acid_time2[n] = u->acid_time2[p];
	u->acid_time3[n] = u->acid_time3[p];
	u->acid_time4[n] = u->acid_time4[p];
	u->acid_time5[n] = u->acid_time5[p];
	u->acid_time6[n] = u->acid_time6[p];
	u->acid_time7[n] = u->acid_time7[p];
	u->acid_time8[n] = u->acid_time8[p];
}

/* Order_ArchonWarp 0x42f830 (order 105) and OrderDarkArchonMeld 0x42fa70 (order 183): the pair walk together and the
 * unit becomes the archon when they meet; the partner dies. */
void kp_order_archon_merge(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int dark = u->order[n] == ORDER_DARK_ARCHON_MELD;
	int64_t p = u->order_unit[n], d, meet = dark ? 20 : 2, near = dark ? 19 : 10;
	int32_t s, ps;
	if (p == 0 || u->owner[p] != u->owner[n] || u->order[p] != u->order[n] || u->order_unit[p] != n) {
		kp_stop_and_face(g, n);
		kp_order_done(g, n);
		return;
	}
	if (kp_band(u->status[n], STATUS_COLLIDES) != 0 && kp_unit_box_within(g, n, kp_sarv(kp_s32v(u->speed2[n] * 2), 8), p)) {
		int64_t head = u->queue_head[n];
		u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_COLLIDES));
		/* PrependOrNewOrder 0x429b20 */
		if (head != KP_NONE) kp_order_insert_before(g, n, ORDER_RESET_COLLISION, 0, 0, 0, NO_TYPE, head);
		else kp_order_queue(g, n, ORDER_RESET_COLLISION, 0, 0, 0, NO_TYPE);
	}
	s = kp_unit_sprite(g, n);
	ps = kp_unit_sprite(g, p);
	d = kp_approx_distance(kp_s16v(g->w->spr.x[ps]) - kp_s16v(g->w->spr.x[s]),
		kp_s16v(g->w->spr.y[ps]) - kp_s16v(g->w->spr.y[s]));
	if (kp_u32v(d) > meet) {
		if (u->order_state[n] != 0 && kp_move_arrival(g, n) == 2) {
			kp_stop_and_face(g, n);
			kp_order_done(g, n);
			return;
		}
		u->order_state[n] = 1;
		if (kp_u32v(d) > near) {
			kp_move_to_point_throttled(g, n, kp_s16v(g->w->spr.x[ps]), kp_s16v(g->w->spr.y[ps]));
		} else {
			int64_t mx = (kp_s16v(g->w->spr.x[s]) + kp_s16v(g->w->spr.x[ps])) / 2;
			int64_t my = (kp_s16v(g->w->spr.y[s]) + kp_s16v(g->w->spr.y[ps])) / 2;
			kp_move_to_point_throttled(g, n, kp_s16v(mx), kp_s16v(my));
		}
		return;
	}
	merge_statuses(g, n, p);
	kp_change_unit_type(g, n, dark ? DARK_ARCHON : ARCHON);
	if (dark) kp_decloak(g, n, DECLOAK_SOUND);
	s = kp_unit_sprite(g, n);
	g->w->spr.flags[s] = (int32_t)kp_band(g->w->spr.flags[s], kp_bnot(SPRITE_NO_BREAK));
	kp_sprite_play(g->w, s, ANIM_SPECIAL1, 1);
	kp_set_button_set(g, n, NO_BUTTONS);
	u->kills[n] = kp_mod(u->kills[n] + u->kills[p], 256);
	if (dark) u->energy[n] = MELD_ENERGY;
	kp_kill_flagged(g, p);
	u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_CAN_MOVE));
	kp_reset_movement(g, n);
	kp_issue_order(g, n, ORDER_COMPLETING_SUMMON);
}

/* OrderCompletingArchonSummon 0x42f9e0 (order 106): the build time runs out, the summon animation plays and the
 * archon is free to move. */
void kp_order_completing_summon(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->order_state[n] == 0) {
		int32_t s;
		/* the build-timer step 0x41a540 */
		if (u->build_time[n] != 0) {
			int64_t t = kp_mod(u->build_time[n], 65536) - (kp_band(kp_scalar(g, KP_CHEATS), CHEAT_FAST_BUILD) != 0 ? 16 : 1);
			u->build_time[n] = t < 0 ? 0 : t;
			return;
		}
		s = kp_unit_sprite(g, n);
		g->w->spr.flags[s] = (int32_t)kp_band(g->w->spr.flags[s], kp_bnot(SPRITE_NO_BREAK));
		kp_sprite_play(g->w, s, ANIM_INIT, 1);
		u->order_state[n] = 1;
		return;
	}
	if (u->order_state[n] != 1 || kp_band(u->order_signal[n], SIGNAL_SUMMONED) == 0) return;
	u->status[n] = kp_bor(u->status[n], STATUS_CAN_MOVE);
	u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_SUMMONED));
	kp_reset_movement(g, n);
	kp_set_button_set(g, n, u->type[n]);
	/* the interface refresh 0x4a7ed0 and the completion voice 0x446650 are the presentation's */
	kp_order_queue(g, n, KP_DAT(g, units, human_ai_idle, u->type[n]), 0, 0, 0, NO_TYPE);
	/* UnlockAndEndOrder 0x429d00 */
	u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_NOT_INTERRUPTIBLE));
	kp_order_done(g, n);
}
