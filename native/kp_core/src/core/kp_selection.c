/* Unit selection (kp_game.h). */
#include "kp_game_internal.h"

#define RECENT_FIRST 10
#define SPRITE_SELECTED 0x08
#define SPRITE_CIRCLE 0x01
#define SPRITE_HIDDEN 0x20
#define CIRCLE_FIRST 0x231
#define DAT_SUBUNIT 0x10
#define DAT_INVINCIBLE 0x20000000
#define COLOR_OWN 0
#define COLOR_ALLY 1
#define COLOR_ENEMY 2
#define DAT_BUILDING 0x1
#define DAT_POWERUP 0x800
#define CURSOR_ANIMATION 2
#define TARGET_FLASH 31

static int single_only(int64_t t)
{
	return t == KP_TYPE_SPIDER_MINE || t == KP_TYPE_EGG || t == KP_TYPE_RHYNADON || t == KP_TYPE_BENGALAAS
		|| t == KP_TYPE_SCANTID || t == KP_TYPE_KAKARU || t == KP_TYPE_RAGNASAUR || t == KP_TYPE_URSADON
		|| t == KP_TYPE_SPELL_DISRUPTION_WEB || t == KP_TYPE_SPELL_DARK_SWARM;
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
	if (!g->w->replay && u->owner[n] == g->local_player && kp_band(u->status[n], 1) != 0 && t == KP_TYPE_PYLON) {
		kp_power_show_fields(g); /* 0x430320 */
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
	if (!g->w->replay && u->owner[n] == g->local_player && kp_band(u->status[n], 1) != 0 && u->type[n] == KP_TYPE_PYLON) {
		kp_power_hide_fields(g); /* 0x42fda0 */
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
	return !(t >= KP_TYPE_FIRST_TRAP && t <= KP_TYPE_LAST_TRAP) && !single_only(t);
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
			if (!present && g->u.type[n] != KP_TYPE_NUCLEAR_MISSILE && put_in_row(g, n, p, added)) added++;
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

/* The group's twelve references: group 18 is the next slot's group 0, as the original's array runs on. */
static int64_t *group_row(KpGame *g, int64_t p, int64_t group)
{
	int64_t flat = p * KP_GROUPS + group;
	if (p < 0 || group < 0 || flat >= (int64_t)KP_SELECTION_PLAYERS * KP_GROUPS) {
		kp_game_fail(g, "selection: group %lld of player %lld", (long long)group, (long long)p);
	}
	return &g->groups[0][0][0] + flat * KP_SLOTS;
}

static int group_length(const int64_t *row)
{
	int n = 0;
	while (n < KP_SLOTS && row[n] != 0) n++;
	return n;
}

/* AssignControlGroup 0x4643c0: the player's selection replaces the group or joins it (only multi-selectable units
 * join a nonempty group, none twice; a group led by a unit that is not multi-selectable takes none). */
static void assign_control_group(KpGame *g, int64_t p, int64_t group, int replace)
{
	int64_t *row = group_row(g, p, group), player = kp_scalar(g, KP_COMMAND_PLAYER);
	int n = 0, k, j;
	if (!replace) {
		int64_t first = kp_unit_from_ref(g, row[0]);
		if (first != 0 && !kp_multi_selectable(g, first)) return;
		n = group_length(row);
	} else {
		for (k = 0; k < KP_SLOTS; k++) row[k] = 0;
	}
	for (k = 0; k < KP_SLOTS; k++) {
		int64_t u = g->selections[p][k], ref;
		int present = 0;
		if (u == 0 || g->u.owner[u] != player) return;
		ref = kp_unit_ref(g, u);
		if (ref == 0) continue;
		if (!replace) {
			for (j = 0; j < n; j++) present = present || row[j] == ref;
			if (present || (n != 0 && !kp_multi_selectable(g, u))) continue;
		}
		row[n++] = ref;
		if (n >= KP_SLOTS) return;
	}
}

/* ExecuteSelectGroup 0x464200: the group's units the player still owns and sees become the selection (a group of
 * several keeps its multi-selectable ones, compacted from its end); groups 10.. stamp their turn, the others are
 * recorded as a recent group. */
static void select_control_group(KpGame *g, int64_t p, int64_t group)
{
	int64_t *row = group_row(g, p, group), player = kp_scalar(g, KP_COMMAND_PLAYER), *sel = g->selections[p];
	int n = group_length(row), i = 0, k;
	if (n == 0) return;
	for (k = 0; k < KP_SLOTS; k++) sel[k] = 0; /* 0x49c7d0 */
	while (i < n) {
		int64_t u = kp_unit_from_ref(g, row[i]);
		if (u == 0 || g->u.owner[u] != player || kp_band(g->w->spr.flags[kp_unit_sprite(g, u)], SPRITE_HIDDEN) != 0
			|| (!kp_multi_selectable(g, u) && n > 1)) {
			n--;
			row[i] = row[n];
			row[n] = 0;
		} else {
			sel[i++] = u; /* 0x49cbc0 */
		}
	}
	if (group >= RECENT_FIRST) g->recent_stamps[p][group - RECENT_FIRST] = kp_mod(kp_scalar(g, KP_TURN), 65536);
	else record_recent(g, p);
}

/* ExecControlGroup 0x472790 (command 0x13): assign (0), select (1) or add to (2) group 0..18. */
void kp_command_control_group(KpGame *g, int64_t p, int64_t action, int64_t group)
{
	if (group > KP_GROUPS) return;
	if (p < 0 || p >= KP_SELECTION_PLAYERS) kp_game_fail(g, "selection: player %lld", (long long)p);
	if (action == 0) assign_control_group(g, p, group, 1);
	else if (action == 1) select_control_group(g, p, group);
	else if (action == 2) assign_control_group(g, p, group, 0);
}

/* The units of the local selection (0x6ab4c0). */
int kp_local_selection_count(KpGame *g)
{
	int k = 0;
	while (k < KP_SLOTS && g->local_selection[k] != 0) k++;
	return k;
}
