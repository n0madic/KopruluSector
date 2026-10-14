/* Unit selection (kp_game.h). */
#include "kp_game_internal.h"

#define RECENT_FIRST 10
#define SPRITE_SELECTED 0x08
#define SPRITE_CIRCLE 0x01
#define SPRITE_HIDDEN 0x20
#define CIRCLE_FIRST 0x231
#define DAT_SUBUNIT 0x10
#define DAT_INVINCIBLE 0x20000000
#define NUCLEAR_MISSILE 0x0e
#define PYLON 0x9c
#define COLOR_OWN 0
#define COLOR_ALLY 1
#define COLOR_ENEMY 2
#define DAT_BUILDING 0x1
#define DAT_POWERUP 0x800
#define CURSOR_ANIMATION 2
#define TARGET_FLASH 31

static int single_only(int64_t t)
{
	return t == 0x0d || t == 0x24 || t == 0x59 || t == 0x5a || t == 0x5d || t == 0x5e || t == 0x5f || t == 0x60
		|| t == 0x69 || t == 0xca;
}

static int64_t flags(KpGame *g, int64_t t) { return KP_DAT(g, units, special_ability_flags, t); }

void kp_selection_init(KpGame *g)
{
	int p, k, i;
	int64_t me = g->local_player;
	kp_scalar_set(g, KP_ORDER_MARKER, 0);
	for (k = 0; k < KP_SLOTS; k++) g->local_selection[k] = 0;
	for (p = 0; p < KP_SELECTION_PLAYERS; p++) {
		for (k = 0; k < KP_SLOTS; k++) g->selections[p][k] = 0;
		for (k = 0; k < KP_RECENT; k++) g->recent_stamps[p][k] = 0;
		for (i = 0; i < KP_GROUPS; i++) {
			for (k = 0; k < KP_SLOTS; k++) g->groups[p][i][k] = 0;
		}
	}
	for (p = 0; p <= 11; p++) {
		if (p == me) g->circle_colors[p] = COLOR_OWN;
		else g->circle_colors[p] = kp_alliance(g, me, p) == 0 ? COLOR_ENEMY : COLOR_ALLY;
	}
	/* 0x52904a, read by the fog ghosts' flash */
	g->w->has_circle_colors = 1;
	for (p = 0; p < 256; p++) g->w->circle_colors[p] = p <= 11 ? (int32_t)g->circle_colors[p] : -1;
}

int64_t kp_unit_ref(KpGame *g, int64_t n)
{
	if (n == 0) return 0;
	return kp_bor(n, g->u.uniqueness[n] * 0x800);
}

static int64_t circle_color(KpGame *g, int64_t owner)
{
	if (owner < 0 || owner > 11) kp_game_fail(g, "selection: no circle colour of player %lld", (long long)owner);
	return g->circle_colors[owner];
}

/* ShowSelectionHealthBar 0x417110. */
static void show_health_bar(KpGame *g, int32_t s, int64_t slot, int64_t color)
{
	KpImages *w = g->w;
	if (kp_band(w->spr.flags[s], SPRITE_SELECTED) != 0) return;
	w->spr.flags[s] = (int32_t)kp_bor(w->spr.flags[s], SPRITE_SELECTED);
	w->spr.selection[s] = (int32_t)slot;
	kp_health_bar_create(w, s);
	if (kp_band(w->spr.flags[s], SPRITE_CIRCLE) == 0) {
		if (kp_selection_circle_create(w, s, (int32_t)color, CIRCLE_FIRST) != KP_NONE) {
			w->spr.flags[s] = (int32_t)kp_bor(w->spr.flags[s], SPRITE_CIRCLE);
		}
	}
}

void kp_add_selection_circle(KpGame *g, int64_t n, int64_t slot)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n];
	if (kp_band(flags(g, t), DAT_INVINCIBLE) != 0) kp_game_fail(g, "selection: circle without a health bar (0x415340)");
	show_health_bar(g, kp_unit_sprite(g, n), slot, circle_color(g, u->owner[n]));
	if (u->owner[n] == g->local_player && kp_band(u->status[n], 1) != 0 && t == PYLON) {
		kp_game_fail(g, "selection: pylon power field (0x430320)");
	}
}

static void select_into_slot(KpGame *g, int64_t n, int64_t slot)
{
	kp_add_selection_circle(g, n, slot);
	if (kp_band(flags(g, g->u.type[n]), DAT_SUBUNIT) != 0) g->local_selection[slot] = g->u.subunit[n];
}

void kp_flash_selection(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s;
	if (kp_band(flags(g, u->type[n]), DAT_SUBUNIT) != 0) return;
	s = kp_unit_sprite(g, n);
	if (kp_band(g->w->spr.flags[s], SPRITE_HIDDEN) != 0) return;
	kp_sprite_flash(g->w, s, (int32_t)circle_color(g, u->owner[n]));
}

void kp_remove_selection_circle(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n);
	if (kp_band(flags(g, u->type[n]), DAT_INVINCIBLE) != 0) {
		kp_sprite_remove_circle(w, s);
		w->spr.flags[s] = (int32_t)kp_band(w->spr.flags[s], 0xf7);
		return;
	}
	kp_sprite_remove_health_bar(w, s);
	if (u->owner[n] == g->local_player && kp_band(u->status[n], 1) != 0 && u->type[n] == PYLON) {
		kp_game_fail(g, "selection: hiding the power fields of a deselected Pylon (0x42fda0)");
	}
}

static void deselect(KpGame *g, int64_t n)
{
	if (kp_band(flags(g, g->u.type[n]), DAT_SUBUNIT) != 0) n = g->u.subunit[n];
	kp_remove_selection_circle(g, n);
}

void kp_select_locally(KpGame *g, const int64_t *units, int count)
{
	int64_t *sel = g->local_selection;
	int k;
	if (count > KP_SLOTS) kp_game_fail(g, "selection: %d units in a selection", count);
	for (k = 0; k < KP_SLOTS; k++) {
		int64_t n = sel[k];
		if (n == 0) break;
		sel[k] = 0;
		deselect(g, n);
	}
	for (k = 0; k < count; k++) {
		sel[k] = units[k];
		select_into_slot(g, kp_game_unit(g, sel[k]), k);
	}
}

void kp_move_placement_cursor(KpGame *g, int64_t x, int64_t y)
{
	KpImages *w = g->w;
	int32_t s;
	if (w->cursor < 0 || w->cursor >= KP_LONE) kp_game_fail(g, "selection: no placement cursor");
	s = kp_check_sprite(w, w->thg.sprite[w->cursor]);
	kp_scalar_set(g, KP_ORDER_MARKER, 0);
	kp_sprite_move(w, s, (int32_t)(x + 1), (int32_t)(y + 1));
	kp_sprite_play(w, s, CURSOR_ANIMATION, 1);
	kp_scalar_set(g, KP_ORDER_MARKER, 1);
}

void kp_mark_target(KpGame *g, int64_t x, int64_t y, int64_t unit)
{
	KpImages *w = g->w;
	int32_t t = kp_fog_at_point(w, (int32_t)x, (int32_t)y);
	if (t != KP_NONE) w->spr.sel_timer[kp_check_sprite(w, w->fog.sprite[t])] = TARGET_FLASH;
	else if (unit != 0) w->spr.sel_timer[kp_unit_sprite(g, unit)] = TARGET_FLASH;
	else kp_move_placement_cursor(g, x, y);
}

int kp_multi_selectable(KpGame *g, int64_t n)
{
	int64_t t = g->u.type[n], f = flags(g, t);
	if (kp_band(f, DAT_BUILDING) != 0 || kp_band(f, DAT_POWERUP) != 0) return 0;
	if (kp_unit_disabled(g, n)) return 0;
	return !(t >= 0xcb && t <= 0xd5) && !single_only(t);
}

static int put_in_row(KpGame *g, int64_t n, int64_t p, int64_t k)
{
	if (n == 0 || p > 7 || k >= KP_SLOTS) return 0;
	if (kp_band(g->w->spr.flags[kp_unit_sprite(g, n)], SPRITE_HIDDEN) != 0) return 0;
	if (k > 0 && (!kp_multi_selectable(g, n) || g->u.owner[n] != kp_scalar(g, KP_COMMAND_PLAYER))) return 0;
	g->selections[p][k] = n;
	return 1;
}

static void assign_group(KpGame *g, int64_t p, int64_t group)
{
	int64_t *row = g->groups[p][group];
	int count = 0, k;
	for (k = 0; k < KP_SLOTS; k++) row[k] = 0;
	for (k = 0; k < KP_SLOTS; k++) {
		int64_t n = g->selections[p][k], ref;
		if (n == 0 || g->u.owner[n] != kp_scalar(g, KP_COMMAND_PLAYER)) return;
		ref = kp_unit_ref(g, n);
		if (ref != 0) {
			row[count++] = ref;
			if (count >= KP_SLOTS) return;
		}
	}
}

static void record_recent(KpGame *g, int64_t p)
{
	int64_t *stamps = g->recent_stamps[p], slot = 0xff, oldest = 0xffff;
	int k;
	for (k = KP_RECENT - 1; k >= 0; k--) {
		if (stamps[k] < oldest) {
			slot = k;
			oldest = stamps[k];
		}
	}
	if (slot == 0xff) kp_game_fail(g, "selection: no recent group slot");
	assign_group(g, p, slot + RECENT_FIRST);
	stamps[slot] = kp_mod(kp_scalar(g, KP_TURN), 65536);
}

void kp_command_select(KpGame *g, int64_t p, const int64_t *refs, int count)
{
	int64_t *sel;
	int added = 0, k, r;
	if (count > KP_SLOTS) return;
	if (p < 0 || p >= KP_SELECTION_PLAYERS) kp_game_fail(g, "selection: player %lld", (long long)p);
	sel = g->selections[p];
	for (k = 0; k < KP_SLOTS; k++) sel[k] = 0;
	for (r = 0; r < count; r++) {
		int64_t n = kp_unit_from_ref(g, refs[r]);
		if (n != 0) {
			int present = 0;
			for (k = 0; k < KP_SLOTS; k++) {
				if (sel[k] == 0) break;
				if (sel[k] == n) {
					present = 1;
					break;
				}
			}
			if (!present && g->u.type[n] != NUCLEAR_MISSILE && put_in_row(g, n, p, added)) added++;
		}
	}
	if (added > 1) record_recent(g, p);
}

/* ExecSelectAdd 0x473070 (command 0x0a): units join the player's selection when they all fit (no Nuclear Missile
 * exclusion here). */
void kp_command_select_add(KpGame *g, int64_t p, const int64_t *refs, int count)
{
	int64_t *row;
	int c = 0, k, j;
	if (count > KP_SLOTS) return;
	if (p < 0 || p >= KP_SELECTION_PLAYERS) kp_game_fail(g, "selection: player %lld", (long long)p);
	row = g->selections[p];
	while (c < KP_SLOTS && row[c] != 0) c++;
	if (c + count > KP_SLOTS) return;
	for (k = 0; k < count; k++) {
		int64_t n = kp_unit_from_ref(g, refs[k]);
		int present = 0;
		if (n == 0) continue;
		for (j = 0; j < KP_SLOTS && row[j] != 0; j++) {
			if (row[j] == n) {
				present = 1;
				break;
			}
		}
		if (!present && put_in_row(g, n, p, c)) c++;
	}
	if (c > 1) record_recent(g, p);
}

/* RemoveUnitFromRow 0x49c9d0: the row's count after n left it. */
static int remove_from_row(KpGame *g, int64_t n, int64_t p)
{
	int64_t *row;
	int count = 0, at = -1, k;
	if (p > 7) return 0;
	row = g->selections[p];
	while (count < KP_SLOTS && row[count] != 0) {
		if (row[count] == n) at = count;
		count++;
	}
	if (at < 0 || count == 0) return count;
	for (k = at; k <= count - 2; k++) row[k] = row[k + 1];
	row[count - 1] = 0;
	return count - 1;
}

/* ExecSelectRemove 0x473170 (command 0x0b): units leave the player's selection (RemoveUnitFromSelectionRow 0x49c870;
 * the dashed circles of a team game's allied rows, 0x49c2b0, never show in a campaign). */
void kp_command_select_remove(KpGame *g, int64_t p, const int64_t *refs, int count)
{
	int r = 0, k;
	if (count > KP_SLOTS) return;
	if (p < 0 || p >= KP_SELECTION_PLAYERS) kp_game_fail(g, "selection: player %lld", (long long)p);
	for (k = 0; k < count; k++) {
		int64_t n = kp_unit_from_ref(g, refs[k]);
		if (n != 0) r = remove_from_row(g, n, p);
	}
	if (r > 1) record_recent(g, p);
}

int kp_selected_units(KpGame *g, int64_t p, int64_t *out)
{
	int count = 0, k;
	if (p < 0 || p >= KP_SELECTION_PLAYERS) kp_game_fail(g, "selection: player %lld", (long long)p);
	for (k = 0; k < KP_SLOTS; k++) {
		int64_t n = g->selections[p][k];
		if (n == 0) break;
		if (kp_unit_is_dead(g, n)) kp_game_fail(g, "selection: dead unit in a selection (0x49c9d0, 0x472100)");
		out[count++] = n;
	}
	return count;
}
