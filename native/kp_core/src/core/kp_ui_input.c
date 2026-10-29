/* The in-game input (plan phase 9C; atlas P13 0x43f330..0x4401c0, P16 0x448c20..0x448f20, P41 0x4bbdd0..0x4bce70,
 * P25 0x474130): the events of the window procedure through the dialogs to the input procedures of the cursor mode,
 * the selection by click and by box, and the selection commands the interface queues for the game. The interface
 * changes the game's local selection as the original's does (SelectUnitsLocally 0x49cf80) and otherwise acts on the
 * game through its queued commands. */
#include "kp_ui.h"

#include <stdio.h>
#include <string.h>

#include "kp_game.h"
#include "kp_ids.h"

#define CLICK_SLOP 4           /* FinishDragSelection 0x4bc140: a box within 4 pixels is a click */
#define SLOTS 12
#define QUERY_MAX 1700
#define PICK_REACH 32          /* GetUnitsClickableAtPoint 0x422150 */
#define CURSOR_DRAG 9
#define CURSOR_SCROLL_FIRST 11 /* the edge-scroll cursors: clicks are ignored */
#define DAT_BUILDING 0x01
#define DAT_SUBUNIT 0x10
#define DAT_SPRITE_HIT 0x100   /* units.dat: the sprite's clickable images are hit-tested, not the primary image */
#define STATUS_DISABLED 0x400
#define STATUS_GROUNDED_BUILDING 0x02
#define STATUS_CLOAKED 0x300
#define STATUS_BURROWED 0x10
#define STATUS_CLOAK_BIT 0x100
#define STATUS_FREE_CLOAK 0x800
#define STATUS_HALLUCINATION 0x40000000
#define CMD_SELECT 0x09
#define CMD_SELECT_ADD 0x0a
#define CMD_SELECT_REMOVE 0x0b
#define CMD_RIGHT_CLICK 0x14
#define TYPE_NONE 228
#define ORDER_CONSTRUCTING 33
#define ORDER_NUKE_TRACK 129
#define ORDER_ARCHON_SUMMON 106
#define CMD_TARGETED 0x15
#define CMD_BUILD 0x0c
#define ORDER_PLACE_PROTOSS 0x47 /* EndPlacement 0x47d300: a Protoss building's own error */
#define STRING_NOT_POWERED 0x35f
#define SOUND_PLACEMENT_ERROR 0x8d
#define SOUND_WORKER_ERROR 0x9f
#define WEAPON_NONE 130
#define TECH_NONE 44
#define ORDER_NONE 189
#define TYPE_INFESTED_TERRAN 50
#define DAT_INVINCIBLE 0x20000000
#define ERR_NONE 0xffff
#define ERR_OUT_OF_RANGE 0x378
#define ERR_TOO_CLOSE 0x379
#define STRING_CANNOT_ATTACK 0x36f
#define STRING_HARVEST_ERROR 0x37d /* + race */
#define SOUND_TARGET_ERROR 0x8a    /* + race */
#define TYPE_FIRST_BEACON 0xc2
#define TYPE_LAST_BEACON 0xc7
#define DAT_UNSELECTABLE_TARGET 0x800
#define DAT_RESOURCE_CONTAINER 0x2000
#define DAT_ORGANIC 0x10000
#define DAT_MECHANICAL 0x40000000
#define STATUS_COMPLETED_FLAG 0x01
#define STATUS_IN_AIR 0x04
#define STATUS_INVINCIBLE 0x04000000
#define ORDER_REPAIR 34
#define ORDER_REPAIR_OBSCURED 35
#define ORDER_HARVEST 79
#define ORDER_HARVEST_OBSCURED 80
#define ORDER_PICKUP 94
#define ORDER_HEAL_MOVE 177
#define TYPE_FIRST_TERRAN_BUILDING 106
#define TYPE_LAST_TERRAN_BUILDING 127
#define ERR_NOT_TERRAN 0x374
#define ERR_NOT_MECHANICAL 0x373
#define ERR_NOT_RESOURCE 0x37c
#define ERR_NOT_YOURS 0x380
#define ERR_CANNOT_LOAD 0x37a
#define ERR_TRANSPORT_FULL 0x37b
#define ERR_NOT_ORGANIC 0x52d
#define ERR_IN_STASIS 900
#define ERR_INVINCIBLE_BUILDING 0x36d
#define SOUND_NO_ENERGY 0x9c       /* + race */
#define STRING_NO_ENERGY 0x360     /* + race */
#define ERR_NO_ENERGY_SPELL 0x36c
#define UNIQUE_SHIFT 11        /* a packed unit reference: index + 1 | the unit's uniqueness << 11 (0x47bd60) */
#define CMD_GROUP 0x13
#define GROUP_ASSIGN 0
#define GROUP_SELECT 1
#define GROUP_ADD 2
#define GROUP_NONE 0xff
#define DOUBLE_TAP_MS 500
#define RECENT_FIRST 10        /* the automatic groups 10..17 (SelectRecentGroupOfUnit 0x464030) */
#define ACC_ADD_FIRST 39891    /* GameCommandHandler 0x4487a0's command ids */
#define ACC_ASSIGN_FIRST 39901
#define ACC_CENTRE_FIRST 39921
#define ACC_CENTRE_STATUS 40036
#define ACC_SPEED_UP 40056
#define ACC_SPEED_DOWN 40058
#define ACC_SAVE_SCREEN 40060
#define ACC_JUMP_SCREEN 40063
#define ACC_QUIT 40043         /* Ctrl+X, Alt+X: OpenQuitMenu 0x47b360 */
#define ACC_QUIT_MISSION 40045 /* Ctrl+Q, Alt+Q: OpenQuitMissionMenu 0x47b380 */
#define ACC_OPTIONS 40047      /* Alt+O: OpenOptionsMenu 0x47b340 */
#define ACC_HELP 40052         /* F1, Alt+H: OpenHelpMenu 0x47b320 */
#define ACC_GAME_MENU 40059    /* Alt+M: OpenGameMenu 0x47b300 */
#define ACC_SAVE 40054         /* Alt+S: OpenSaveGameDialog 0x49b0b0 */
#define ACC_LOAD 40055         /* Alt+L: OpenLoadGameDialog 0x49af50 */
#define ACC_LEADERBOARD 40053  /* Ctrl+R: ToggleLeaderboardExpanded 0x4a6260 */
#define ACC_MUSIC 40038        /* ToggleMusic 0x49ec00 */
#define ACC_SOUND 40039        /* ToggleSound 0x49ec40 */
#define CMD_SPEED 0x0f
#define SPEED_FASTEST 6
#define TILE 32

static KpGame *game(KpUi *ui) { return ui->game; }
static KpUiInput *in(KpUi *ui) { return &ui->input; }

/* --- the commands --- */

/* QueueGameCommand 0x485bd0: the interface's command for the game's next turns. */
void kp_ui_queue_command(KpUi *ui, const uint8_t *bytes, size_t size)
{
	KpUiInput *p = in(ui);
	if (p->command_count == KP_UI_COMMANDS || p->command_used + size > KP_UI_COMMAND_BYTES) {
		snprintf(ui->error, sizeof ui->error, "ui: more than %d queued commands", KP_UI_COMMANDS);
		return;
	}
	memcpy(p->commands + p->command_used, bytes, size);
	p->command_size[p->command_count++] = (uint16_t)size;
	p->command_used += size;
}

size_t kp_ui_take_command(KpUi *ui, uint8_t *out, size_t capacity)
{
	KpUiInput *p = in(ui);
	size_t at = 0, size;
	int k;
	if (p->command_first >= p->command_count) {
		p->command_first = p->command_count = 0;
		p->command_used = 0;
		return 0;
	}
	for (k = 0; k < p->command_first; k++) at += p->command_size[k];
	size = p->command_size[p->command_first++];
	if (size > capacity) return 0;
	memcpy(out, p->commands + at, size);
	return size;
}

uint16_t kp_ui_unit_ref(struct KpGame *g, int64_t n)
{
	return (uint16_t)((n & 0x7ff) | (g->u.uniqueness[n] & 0x1f) << UNIQUE_SHIFT);
}

/* SendSelectionCommands 0x474130: the new local selection against the network's mirror, as one select command or
 * a remove and an add. */
static void send_selection(KpUi *ui, const int64_t *units, int n)
{
	KpGame *g = game(ui);
	KpUiInput *p = in(ui);
	uint8_t add[2 + 2 * SLOTS], remove[2 + 2 * SLOTS], all[2 + 2 * SLOTS];
	int adds = 0, removes = 0, k, j;
	uint32_t mask = (uint32_t)(g->w->replay ? g->w->local_vision : g->local_vision) & 0xff;
	p->double_group = GROUP_NONE;
	for (k = 0; k < n; k++) {
		int found = 0;
		for (j = 0; j < SLOTS && p->mirror[j] != 0; j++) found |= p->mirror[j] == units[k];
		if (!found && (g->w->spr.visibility[kp_unit_sprite(g, units[k])] & mask)) {
			uint16_t r = kp_ui_unit_ref(g, units[k]);
			add[2 + 2 * adds] = (uint8_t)r;
			add[3 + 2 * adds] = (uint8_t)(r >> 8);
			adds++;
		}
	}
	for (j = 0; j < SLOTS && p->mirror[j] != 0; j++) {
		int found = 0;
		for (k = 0; k < n; k++) found |= units[k] == p->mirror[j];
		if (!found) {
			uint16_t r = kp_ui_unit_ref(g, p->mirror[j]);
			remove[2 + 2 * removes] = (uint8_t)r;
			remove[3 + 2 * removes] = (uint8_t)(r >> 8);
			removes++;
		}
	}
	memset(p->mirror, 0, sizeof p->mirror);
	for (k = 0; k < n; k++) p->mirror[k] = units[k];
	if (adds + removes >= n) {
		all[0] = CMD_SELECT;
		all[1] = (uint8_t)n;
		for (k = 0; k < n; k++) { /* the last unit first */
			uint16_t r = kp_ui_unit_ref(g, units[n - 1 - k]);
			all[2 + 2 * k] = (uint8_t)r;
			all[3 + 2 * k] = (uint8_t)(r >> 8);
		}
		kp_ui_queue_command(ui, all, 2 + 2 * (size_t)n);
		return;
	}
	if (removes) {
		remove[0] = CMD_SELECT_REMOVE;
		remove[1] = (uint8_t)removes;
		kp_ui_queue_command(ui, remove, 2 + 2 * (size_t)removes);
	}
	if (adds) {
		add[0] = CMD_SELECT_ADD;
		add[1] = (uint8_t)adds;
		kp_ui_queue_command(ui, add, 2 + 2 * (size_t)adds);
	}
}

void kp_ui_selection_sent(KpUi *ui, const int64_t *units, int n) { send_selection(ui, units, n); }

void kp_ui_selection_gone(KpUi *ui, int64_t unit)
{
	/* 0x472100: the last unit of the mirror takes the gone one's place */
	int64_t *m = in(ui)->mirror;
	int n = 0, k;
	while (n < SLOTS && m[n] != 0) n++;
	for (k = 0; k < n; k++) {
		if (m[k] == unit) {
			m[k] = m[n - 1];
			m[n - 1] = 0;
			return;
		}
	}
}

/* SelectUnitsLocally 0x49cf80: the game's local selection, the selection's voice (phase 9D) and the network's. */
static void select_locally(KpUi *ui, const int64_t *units, int n, int sound, int send)
{
	kp_select_locally(game(ui), units, n);
	if (sound) kp_ui_selection_sound(ui, units, n);
	if (send) send_selection(ui, units, n);
}

/* --- the control groups (group.cpp, atlas P22, P25) --- */

static int group_kept(KpGame *g, int64_t u, int n)
{
	return u != 0 && !(g->w->spr.flags[kp_unit_sprite(g, u)] & KP_SF_HIDDEN) && g->u.owner[u] == g->local_player
		&& (kp_multi_selectable(g, u) || n <= 1);
}

int kp_ui_group_held(const KpUi *ui, int group)
{
	KpGame *g = ui->game;
	const int64_t *row;
	int n = 0, k;
	if (g == NULL || group < 0 || group >= KP_GROUPS) return 0;
	row = g->groups[g->local_player][group];
	while (n < SLOTS && row[n] != 0) n++;
	for (k = 0; k < n; k++) {
		if (group_kept(g, kp_unit_from_ref(g, row[k]), n)) return 1; /* select_group would keep it */
	}
	return 0;
}

/* CenterViewOnControlGroup 0x463d60: the view moves to the mean of the group's units. */
static void centre_on_group(KpUi *ui, int group)
{
	KpGame *g = game(ui);
	const int64_t *row = g->groups[g->local_player][group];
	int n = 0, m = 0, k;
	int32_t sx = 0, sy = 0;
	if (group >= 10) return;
	while (n < SLOTS && row[n] != 0) n++;
	for (k = 0; k < n; k++) {
		int64_t u = kp_unit_from_ref(g, row[k]);
		int32_t s;
		if (u == 0 || kp_unit_is_dead(g, u) || (g->w->spr.flags[kp_unit_sprite(g, u)] & KP_SF_HIDDEN)
			|| !(kp_multi_selectable(g, u) || n <= 1)) {
			continue;
		}
		s = kp_unit_sprite(g, u);
		sx += (int16_t)g->w->spr.x[s];
		sy += (int16_t)g->w->spr.y[s];
		m++;
	}
	if (m == 0) return;
	/* CenterViewOnControlGroup 0x463d60: the mean at the view's centre (320, 200 of the original's 640 x 400) */
	kp_ui_set_screen_position(ui, (sx / m - ui->view_w / 2) & ~7, (sy / m - ui->view_h / 2) & ~7);
}

void kp_ui_centre_on_unit(KpUi *ui, int64_t unit)
{
	/* CenterScreenOnUnit 0x422b10 by tiles (0x4457f0): the unit's tile at the view's centre (10, 6 of 640 x 400) */
	KpGame *g = ui->game;
	int32_t s;
	if (g == NULL || unit == 0 || (s = kp_unit_sprite(g, unit)) == KP_NONE) return;
	kp_ui_set_screen_position(ui, ((int16_t)g->w->spr.x[s] / TILE - ui->view_w / TILE / 2) * TILE,
		((int16_t)g->w->spr.y[s] / TILE - ui->view_h / TILE / 2) * TILE);
}

/* SendControlGroupCommand 0x473df0 with the double tap. */
static void send_group(KpUi *ui, uint8_t action, uint8_t group, const int64_t *list, int count)
{
	KpUiInput *p = in(ui);
	uint8_t cmd[3];
	cmd[0] = CMD_GROUP;
	cmd[1] = action;
	cmd[2] = group;
	kp_ui_queue_command(ui, cmd, 3);
	if (action != GROUP_SELECT) {
		p->double_group = GROUP_NONE;
		return;
	}
	memset(p->mirror, 0, sizeof p->mirror);
	memcpy(p->mirror, list, (size_t)count * sizeof list[0]);
	if (group == p->double_group && ui->now - p->double_tick < DOUBLE_TAP_MS) {
		centre_on_group(ui, group);
		p->double_group = GROUP_NONE;
	} else {
		p->double_group = group;
		p->double_tick = ui->now;
	}
}

static void cancel_modes(KpUi *ui); /* CancelTargetingUi 0x423120 */

/* FlashRallyPoint 0x41b250 behind its callers' check: a lone selected unit of the local player's that is a rally
 * producer (IsRallyProducer 0x429250) flashes the unit or fog ghost at its rally point or moves the marker there
 * (0x47fc10), unless the point is unset or the unit's own position. */
static void flash_rally(KpUi *ui, int64_t n)
{
	KpGame *g = game(ui);
	int64_t x, y;
	if (n == 0 || g->u.owner[n] != g->local_player || !kp_rally_producer(g->u.type[n])) return;
	x = g->u.rally_x[n];
	y = g->u.rally_y[n];
	if ((x == 0 && y == 0) || (x == g->u.x[n] && y == g->u.y[n])) return;
	kp_mark_target(g, x, y, 0);
}

/* SelectControlGroupLocal 0x463ee0 (also the end of SelectRecentGroupOfUnit 0x464030). */
static void select_group(KpUi *ui, int group)
{
	KpGame *g = game(ui);
	const int64_t *row = &g->groups[0][0][0] + ((int64_t)g->local_player * KP_GROUPS + group) * KP_SLOTS;
	int64_t list[SLOTS], last = 0;
	int n = 0, kept = 0, k;
	memset(list, 0, sizeof list);
	while (n < SLOTS && row[n] != 0) n++;
	for (k = 0; k < n; k++) {
		int64_t u = kp_unit_from_ref(g, row[k]);
		if (group_kept(g, u, n)) {
			list[kept++] = u;
			last = u;
		} else {
			last = 0;
		}
	}
	if (kept == 0) return;
	if (kept == 1) flash_rally(ui, last); /* the group's last unit, 0 when it was not kept */
	cancel_modes(ui);
	select_locally(ui, list, kept, 1, 0);
	send_group(ui, GROUP_SELECT, (uint8_t)group, list, kept);
}

/* SelectRecentGroupOfUnit 0x464030: the automatic group most recently recalled that holds the unit. */
static int recall_group_of(KpUi *ui, int64_t unit)
{
	KpGame *g = game(ui);
	int64_t p = g->local_player, ref = kp_ui_unit_ref(g, unit);
	int best = -1, a, k;
	uint16_t best_stamp = 0;
	for (a = KP_RECENT - 1; a >= 0; a--) {
		const int64_t *row = &g->groups[0][0][0] + (p * KP_GROUPS + RECENT_FIRST + a) * KP_SLOTS;
		for (k = 0; k < SLOTS; k++) {
			uint16_t stamp = (uint16_t)g->recent_stamps[p][a];
			/* TODO: with no best yet the original compares the word before this slot's stamps */
			if (row[k] != 0 && row[k] == ref && (best < 0 || stamp > best_stamp)) {
				best = a;
				best_stamp = stamp;
			}
		}
	}
	if (best < 0) return 0;
	select_group(ui, RECENT_FIRST + best);
	return 1;
}

/* --- the units under the mouse --- */

static uint32_t unit_flags(KpGame *g, int64_t n) { return (uint32_t)KP_DAT(g, units, special_ability_flags, g->u.type[n]); }

/* IsUnselectableType 0x4bc730: the types 14, 85, 105, 202 and 205-208. */
static int unselectable(int64_t t)
{
	return t == 14 || t == 85 || t == 105 || t == 202 || (t >= 205 && t <= 208);
}

static int multi_selectable(KpGame *g, int64_t n) { return kp_multi_selectable(g, n); } /* 0x4291b0 */

/* The sprite's visibility for the local player (FilterSelectableUnits 0x4bc5b0 step 2, PickUnitAtPoint 0x4bcd00). */
static int visible(KpGame *g, int64_t n)
{
	KpImages *w = g->w;
	uint32_t vision = (uint32_t)g->local_vision, sprite = (uint32_t)w->spr.visibility[kp_unit_sprite(g, n)];
	if (w->replay) {
		/* TODO: a replay's vision (0x67cc84, 0x67cc88) */
		return 0;
	}
	if (!(sprite & vision & 0xff)) return 0;
	return !(g->u.status[n] & STATUS_CLOAKED) || (g->u.visibility[n] & vision);
}

/* GetSpriteClickableBounds 0x4160e0 with IsPointInSpriteBounds 0x416980. */
static int in_sprite_bounds(KpImages *w, int32_t s, int32_t x, int32_t y)
{
	int32_t i, l = 0, t = 0, r = 0, b = 0, any = 0;
	for (i = w->spr.images[s].head; i != KP_NONE; i = w->img.next[i]) {
		int32_t rect[6], il, it, ir, ib;
		if (!(w->img.flags[i] & KP_F_CLICKABLE)) continue;
		kp_images_frame_rect(w, i, -1, rect);
		il = (int16_t)w->img.x[i];
		it = (int16_t)w->img.y[i];
		ir = il + rect[2] - 1;
		ib = it + rect[3] - 1;
		if (!any || il < l) l = il;
		if (!any || it < t) t = it;
		if (!any || ir > r) r = ir;
		if (!any || ib > b) b = ib;
		any = 1;
	}
	/* TODO: a sprite without a clickable image reads the original's uninitialized rectangle */
	return any && l <= x && x < r && t <= y && y < b;
}

/* ImageHitTest 0x40fdc0. */
static int image_hit(KpImages *w, int32_t i, int32_t x, int32_t y)
{
	int32_t rect[6], lx = x - (int16_t)w->img.x[i], ly = y - (int16_t)w->img.y[i], entry;
	kp_images_frame_rect(w, i, -1, rect);
	if (w->img.flags[i] & KP_F_MIRROR) lx = rect[4] - 2 * rect[0] - lx;
	entry = kp_images_grp_of(w, w->img.id[i]);
	if (entry < 0) return 0;
	return kp_grpmeta_pixel_opaque(w->grp, (uint32_t)entry, (uint32_t)w->img.frame[i], lx, ly);
}

/* SpriteHitTest 0x4166d0. */
static int sprite_hit(KpImages *w, int32_t s, int32_t x, int32_t y)
{
	int32_t i;
	for (i = w->spr.images[s].head; i != KP_NONE; i = w->img.next[i]) {
		if ((w->img.flags[i] & KP_F_CLICKABLE) && image_hit(w, i, x, y)) return 1;
	}
	return 0;
}

/* UnitPixelHitTest 0x4bc400 (subunit: SubunitPixelHitTest 0x4bc460, without the fallback). */
static int unit_hit(KpGame *g, int64_t n, int32_t x, int32_t y, int subunit)
{
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n);
	if (in_sprite_bounds(w, s, x, y)) {
		int hit = (unit_flags(g, n) & DAT_SPRITE_HIT) ? sprite_hit(w, s, x, y) : image_hit(w, w->spr.main[s], x, y);
		if (hit) return 1;
	}
	return !subunit && g->u.subunit[n] != 0 && unit_hit(g, g->u.subunit[n], x, y, 1);
}

static int32_t placement_area(KpGame *g, int64_t t)
{
	return (int32_t)(int16_t)KP_DAT(g, units, staredit_placement_size, t * 2)
		* (int32_t)(int16_t)KP_DAT(g, units, staredit_placement_size, t * 2 + 1);
}

/* PickUnitAtPoint 0x4bcd00 over GetUnitsClickableAtPoint 0x422150: the unit a click at map (x, y) takes. */
static int64_t pick_unit(KpUi *ui, int32_t x, int32_t y)
{
	KpGame *g = game(ui);
	KpImages *w = g->w;
	int count, k;
	int64_t best = 0, *found;
	int32_t area = INT32_MAX;
	found = kp_query_rect(g, (int16_t)(x - PICK_REACH), (int16_t)(y - PICK_REACH),
		(int16_t)(x + PICK_REACH + g->spatial.max_w), (int16_t)(y + PICK_REACH + g->spatial.max_h), &count);
	for (k = 0; k < count; k++) {
		int64_t u = found[k];
		int32_t a;
		if (!in_sprite_bounds(w, kp_unit_sprite(g, u), x, y)) continue;
		if (unit_flags(g, u) & DAT_SUBUNIT) u = g->u.subunit[u];
		if (!visible(g, u) || unselectable(g->u.type[u])) continue;
		if (best == 0) {
			a = placement_area(g, g->u.type[u]);
		} else if (kp_sprite_depth_key(w, kp_unit_sprite(g, best)) < kp_sprite_depth_key(w, kp_unit_sprite(g, u))) {
			if (unit_hit(g, u, x, y, 0)) {
				int32_t s = kp_unit_sprite(g, u);
				a = (int32_t)((uint32_t)(w->spr.width[s] & 0xff) * (uint32_t)(w->spr.height[s] & 0xff));
				best = u;
				area = a;
				continue;
			}
			a = placement_area(g, g->u.type[u]);
			if (area <= a) continue;
		} else {
			if (unit_hit(g, best, x, y, 0)) continue;
			a = placement_area(g, g->u.type[u]);
			if (area <= a) continue;
		}
		best = u;
		area = a;
	}
	kp_query_release(g, found);
	return best;
}

int64_t kp_ui_pick_unit(KpUi *ui, int32_t x, int32_t y) { return pick_unit(ui, x, y); }

/* ReplaceSelectionEntryByDepth 0x4bbdd0. */
static void replace_by_depth(KpGame *g, int64_t *list, int count, int64_t n, int64_t excluded)
{
	uint32_t key = kp_sprite_depth_key(g->w, kp_unit_sprite(g, n));
	int k;
	for (k = 0; k < count; k++) {
		if (list[k] == excluded) continue;
		if (key > kp_sprite_depth_key(g->w, kp_unit_sprite(g, list[k])) || n == excluded) {
			list[k] = n;
			return;
		}
	}
}

/* FilterSelectableUnits 0x4bc5b0. */
static int filter_selectable(KpUi *ui, const int64_t *list, int count, int64_t *out, int64_t exemplar)
{
	KpGame *g = game(ui);
	KpUnits *u = &g->u;
	int n = 0, k, j;
	int64_t last = 0;
	if (exemplar) out[n++] = exemplar;
	for (k = 0; k < count; k++) {
		int64_t v = list[k];
		int dup = 0;
		if (v == exemplar) continue;
		if (u->subunit[v] != 0) {
			if (unit_flags(g, v) & DAT_SUBUNIT) v = u->subunit[v];
			for (j = 0; j < n; j++) dup |= out[j] == v;
			if (dup) continue;
		}
		if (!visible(g, v) || unselectable(u->type[v])) continue;
		if (!multi_selectable(g, v) || g->w->replay || u->owner[v] != g->local_player) {
			last = v;
			continue;
		}
		if (exemplar) {
			if (u->owner[v] != u->owner[exemplar] || u->type[v] != u->type[exemplar]) continue;
			if ((u->status[v] ^ u->status[exemplar]) & STATUS_BURROWED) continue;
			/* a unit cloaked for free (IsCloakingForFree 0x4a04e0: the Arbiter's field, not burrowed) matches either */
			if (!((u->status[v] & STATUS_FREE_CLOAK) && !(u->status[v] & STATUS_BURROWED))
				&& ((u->status[v] ^ u->status[exemplar]) & STATUS_CLOAK_BIT)) continue;
			if ((u->status[v] ^ u->status[exemplar]) & STATUS_HALLUCINATION) continue;
		}
		if (n < SLOTS) out[n++] = v;
		else replace_by_depth(g, out, SLOTS, v, exemplar);
	}
	if (n == 0 && last != 0) {
		out[0] = last;
		return 1;
	}
	return n;
}

/* MergeIntoSelection 0x4bc4c0. */
static int merge_into_selection(KpUi *ui, int64_t *dest, const int64_t *src, int count, int64_t exemplar)
{
	KpGame *g = game(ui);
	int c = 0, k, j, n;
	while (c < SLOTS && dest[c] != 0) c++;
	if (c >= SLOTS) return SLOTS;
	if (c >= 1 && !(multi_selectable(g, src[0]) && multi_selectable(g, dest[0]) && !g->w->replay
			&& g->u.owner[src[0]] == g->local_player && g->u.owner[dest[0]] == g->local_player)) {
		return c;
	}
	n = c;
	for (k = 0; k < count; k++) {
		int dup = 0;
		for (j = 0; j < c; j++) dup |= dest[j] == src[k];
		if (dup) continue;
		if (n < SLOTS) dest[n++] = src[k];
		else replace_by_depth(g, dest + c, n - c, src[k], exemplar);
	}
	return n;
}

/* SelectUnitsWithGroupRecall 0x4bce70. */
static void select_with_recall(KpUi *ui, const int64_t *units, int n)
{
	kp_ui_select_units(ui, units, n, 1);
}

void kp_ui_select_units(KpUi *ui, const int64_t *units, int n, int sound)
{
	if (n == 1 && in(ui)->keys[2] && recall_group_of(ui, units[0])) return;
	select_locally(ui, units, n, sound, 1);
}

static int screen_units(KpUi *ui, int64_t *out, int64_t exemplar)
{
	KpGame *g = game(ui);
	KpUiInput *p = in(ui);
	int count, n;
	int64_t *found = kp_query_rect(g, p->screen_x, p->screen_y, p->screen_x + ui->view_w, p->screen_y + ui->view_h, &count);
	n = filter_selectable(ui, found, count, out, exemplar);
	kp_query_release(g, found);
	return n;
}

/* ClickSelectAtPoint 0x4bbe70. */
static void click_select(KpUi *ui, int32_t x, int32_t y)
{
	KpGame *g = game(ui);
	KpUiInput *p = in(ui);
	int64_t u = pick_unit(ui, x, y), buf[SLOTS + 1], copy[SLOTS * 2];
	int type_select, n, c, k;
	if (u == 0) return;
	type_select = p->keys[1] || ((g->w->spr.flags[kp_unit_sprite(g, u)] & KP_SF_SELECTED) && p->double_click);
	memset(buf, 0, sizeof buf);
	if (!type_select && (!p->keys[0] || g->local_selection[0] == 0)) {
		buf[0] = u;
		select_with_recall(ui, buf, 1);
	} else if (p->keys[0] && type_select) {
		memset(copy, 0, sizeof copy);
		for (k = 0; k < SLOTS; k++) copy[k] = g->local_selection[k];
		n = screen_units(ui, buf, u);
		if (n == 0) return;
		select_with_recall(ui, copy, merge_into_selection(ui, copy, buf, n, u));
		return;
	} else if (p->keys[0]) {
		for (c = 0; c < SLOTS && g->local_selection[c] != 0; c++) buf[c] = g->local_selection[c];
		if (g->w->spr.flags[kp_unit_sprite(g, u)] & KP_SF_SELECTED) {
			int at = g->w->spr.selection[kp_unit_sprite(g, u)];
			for (k = at; k < c - 1; k++) buf[k] = buf[k + 1];
			buf[--c] = 0;
			if (c == 1 && p->keys[2] && recall_group_of(ui, buf[0])) return;
			select_locally(ui, buf, c, 0, 1);
			return;
		}
		if (c >= SLOTS || !multi_selectable(g, buf[0]) || g->w->replay || g->u.owner[buf[0]] != g->local_player
			|| !multi_selectable(g, u) || g->u.owner[u] != g->local_player) {
			return;
		}
		buf[c] = u;
		select_with_recall(ui, buf, c + 1);
		return;
	} else {
		n = screen_units(ui, buf, u);
		if (n == 0) return;
		select_with_recall(ui, buf, n);
		if (n != 1) return;
	}
	flash_rally(ui, u);
}

/* BoxSelectUnits 0x4bc2d0. */
static void box_select(KpUi *ui, int32_t l, int32_t t, int32_t r, int32_t b)
{
	KpGame *g = game(ui);
	KpUiInput *p = in(ui);
	int64_t buf[SLOTS], copy[SLOTS * 2], *found;
	int count, n, k;
	found = kp_query_overlap(g, l, t, r, b, &count);
	n = filter_selectable(ui, found, count, buf, 0);
	kp_query_release(g, found);
	if (n == 0) return;
	if (p->keys[0] && g->local_selection[0] != 0) {
		memset(copy, 0, sizeof copy);
		for (k = 0; k < SLOTS; k++) copy[k] = g->local_selection[k];
		select_with_recall(ui, copy, merge_into_selection(ui, copy, buf, n, 0));
		return;
	}
	select_with_recall(ui, buf, n);
	if (n == 1) flash_rally(ui, buf[0]);
}

/* --- the drag box --- */

/* IsPointOverConsole 0x43e890: the console art's opaque pixels.
 * TODO: the HUD mask's rows (ComputeConsoleMaskRows 0x43e9c0) and Storm's trans mask (0x6d0c44) */
static int over_console(KpUi *ui, int32_t x, int32_t y)
{
	const KpUiSurface *c = &ui->console;
	if (in(ui)->over_console >= 0) return in(ui)->over_console;
	if (c->pixels == NULL || x < 0 || y < 0 || x >= c->w || y >= c->h) return 0;
	return c->pixels[(size_t)y * (size_t)c->w + (size_t)x] != 0;
}

static void reset_box(KpUi *ui) { in(ui)->box = 0; } /* ResetDragBox 0x4bbe30 (its dirty cells are the view's) */

void kp_ui_cancel_drag_box(KpUi *ui)
{
	if (!in(ui)->box) return;
	in(ui)->box = 0;
	kp_ui_install_game_input(ui);
}

void kp_ui_set_active(KpUi *ui, int active)
{
	KpUiInput *p = in(ui);
	ui->inactive = (uint8_t)(active == 0);
	memset(p->vk, 0, sizeof p->vk);
	memset(p->keys, 0, sizeof p->keys); /* Shift, Ctrl and Alt are the bytes 0x665e68.. of the same array */
}

void kp_ui_capture_changed(KpUi *ui)
{
	KpUiInput *p = in(ui);
	if ((p->mouse_state & KP_UI_MOUSE_BUTTONS) == 0) return;
	p->mouse_state &= (uint16_t)~KP_UI_MOUSE_BUTTONS;
	if (ui->game != NULL) kp_ui_install_game_input(ui);
	else kp_ui_reset_input(ui);
}

void kp_ui_game_view_update(KpUi *ui)
{
	kp_ui_placement_update(ui);
	if (in(ui)->box && in(ui)->mouse_state == 0) kp_ui_cancel_drag_box(ui);
}

/* UpdateDragBox 0x4bc220. */
static void update_box(KpUi *ui, KpUiEvent *e)
{
	KpUiInput *p = in(ui);
	p->box = 1;
	p->box_left = e->x < p->anchor_x ? e->x : p->anchor_x;
	p->box_right = e->x > p->anchor_x ? e->x : p->anchor_x;
	p->box_top = e->y < p->anchor_y ? e->y : p->anchor_y;
	p->box_bottom = e->y > p->anchor_y ? e->y : p->anchor_y;
}

static uint16_t pointer_cursor(KpUi *ui);

/* FinishDragSelection 0x4bc140. */
static void finish_box(KpUi *ui, KpUiEvent *e)
{
	KpGame *g = game(ui);
	KpUiInput *p = in(ui);
	int32_t l, t, r, b;
	update_box(ui, e);
	kp_ui_install_game_input(ui); /* its reset (ResetDragBox 0x4bbe30) ends the box */
	kp_ui_cursor_set(ui, pointer_cursor(ui)); /* GetPointerCursorId 0x43e6d0, SetCursorId 0x43e9a0 */
	l = p->box_left + (int16_t)p->screen_x;
	r = p->box_right + (int16_t)p->screen_x;
	t = p->box_top + (int16_t)p->screen_y;
	b = p->box_bottom + (int16_t)p->screen_y;
	p->box = 0;
	if (t >= g->map_h_px) return;
	if (b >= g->map_h_px) b = (int32_t)g->map_h_px - 1;
	if (r - l <= CLICK_SLOP && b - t <= CLICK_SLOP) click_select(ui, l, t);
	else box_select(ui, l, t, r, b);
}

/* BeginDragSelection 0x4bcb80: the normal mode's left button. */
static void begin_box(KpUi *ui, KpUiEvent *e)
{
	KpUiInput *p = in(ui);
	if (over_console(ui, e->x, e->y)) return;
	if (p->targeting) kp_ui_cancel_targeting(ui); /* 0x4b2900 */
	kp_ui_end_placement(ui);                       /* EndPlacement 0x47d300(1) */
	p->reset = reset_box;
	p->proc[KP_UI_LEFT_UP] = finish_box;
	p->proc[KP_UI_RIGHT_UP] = finish_box;
	p->proc[KP_UI_MOUSE_MOVE] = update_box;
	kp_ui_cursor_set(ui, CURSOR_DRAG);
	p->anchor_x = e->x;
	p->anchor_y = e->y;
	if (e->type == KP_UI_LEFT_DOUBLE) p->double_click = 1;
	else if (e->type != KP_UI_MOUSE_MOVE && e->type != KP_UI_LEFT_UP) p->double_click = 0;
}

/* --- the right click --- */

/* CanUnitTakeRightClick 0x47f4b0. */
static int takes_right_click(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], order = u->order[n];
	if (kp_unit_disabled(g, n)) return 0;
	if ((u->status[n] & STATUS_BURROWED) && t != KP_TYPE_LURKER) return 0;
	if (t == KP_TYPE_SCV && order == ORDER_CONSTRUCTING) return 0;
	if ((t == 1 || t == 16 || t == 100 || t == 99 || t == 104) && order == ORDER_NUKE_TRACK) return 0;
	if (t == KP_TYPE_ARCHON && order == ORDER_ARCHON_SUMMON) return 0;
	return kp_right_click_action(g, n) != 0;
}

static int beacon(int64_t t) { return t >= TYPE_FIRST_BEACON && t <= TYPE_LAST_BEACON; } /* IsBeaconType 0x421620 */

/* CheckRightClickFeedback 0x47f9d0: 1 for the acknowledgement, else the error of the lowest selected unit that cannot
 * act on the target (its race's sound and its line) or nothing. */
static int right_click_feedback(KpUi *ui, int64_t target)
{
	KpGame *g = game(ui);
	int64_t status = ui->hud.status_unit;
	int32_t sound = -1, error = -1;
	int k;
	for (k = SLOTS - 1; k >= 0; k--) {
		int64_t u = ui->hud.selection[k], c, race;
		int unattackable = 0;
		if (u == 0) continue;
		c = kp_right_click_action(g, u);
		if ((g->u.status[u] & STATUS_HALLUCINATION) && (c == 4 || c == 5)) c = 1;
		if (c > 5 || c == 2) return 1;
		if (c == 0) continue;
		race = kp_unit_race(g, u);
		if (target != 0) {
			int64_t tt = g->u.type[target];
			unattackable = beacon(tt) || (KP_DAT(g, units, special_ability_flags, tt) & DAT_UNSELECTABLE_TARGET);
			if ((c == 4 || c == 5) && !unattackable && tt == KP_TYPE_GEYSER) {
				sound = (int32_t)race + 1;
				error = (int32_t)(STRING_HARVEST_ERROR + race);
				continue;
			}
		}
		if (c == 1 || c == 4 || c == 5) {
			if (target == 0 || (c != 1 && (KP_DAT(g, units, special_ability_flags, g->u.type[target]) & DAT_UNSELECTABLE_TARGET))
				|| !kp_is_enemy(g, status, target) || unattackable || kp_can_attack(g, u, target, 1)) {
				return 1;
			}
			error = STRING_CANNOT_ATTACK;
		} else { /* 3: a burrowed Lurker */
			if (target == 0 || unattackable) continue;
			if (!kp_can_attack(g, u, target, 1)) error = STRING_CANNOT_ATTACK;
			else if (kp_target_out_of_range(g, u, target)) error = ERR_OUT_OF_RANGE;
			else if (kp_target_inside_min_range(g, u, target)) error = ERR_TOO_CLOSE;
			else return 1;
		}
		sound = (int32_t)(SOUND_TARGET_ERROR + race);
	}
	if (sound != -1) (void)kp_ui_sound_at_unit(ui, (uint32_t)sound, 0, 1, 0);
	if ((uint16_t)error != 0xffff) kp_ui_error_line(ui, kp_ui_tbl(ui, KP_UI_STAT_TXT, (uint32_t)error));
	return 0;
}

/* RightClickInGame 0x47f300: the normal mode's right button. */
static void right_click(KpUi *ui, KpUiEvent *e)
{
	KpGame *g = game(ui);
	KpUiInput *p = in(ui);
	int64_t status = ui->hud.status_unit, u, type = TYPE_NONE;
	int32_t mx, my, f;
	uint8_t cmd[10];
	int k, ok = 0;
	if (over_console(ui, e->x, e->y)) return;
	if (p->mirror[0] == 0 || g->u.owner[p->mirror[0]] != g->local_player) return; /* 0x4720f0 */
	if (status == 0 || g->w->replay || g->u.owner[status] != g->local_player) return;
	for (k = 0; k < SLOTS; k++) ok |= ui->hud.selection[k] != 0 && takes_right_click(g, ui->hud.selection[k]);
	if (!ok) return;
	mx = e->x + (int16_t)p->screen_x;
	my = e->y + (int16_t)p->screen_y;
	u = pick_unit(ui, mx, my);
	if (ui->hud.selection_count == 1 && u == status
		&& !((g->u.status[u] & STATUS_GROUNDED_BUILDING) && kp_rally_producer(g->u.type[u]))) {
		return;
	}
	if (u == 0) {
		ok = 0;
		for (k = 0; k < SLOTS; k++) {
			int64_t c;
			if (ui->hud.selection[k] == 0) continue;
			c = kp_right_click_action(g, ui->hud.selection[k]);
			ok |= c == 1 || c == 2 || c == 4 || c == 5 || c == 6;
		}
		if (!ok) return;
	}
	f = kp_fog_at_point(g->w, mx, my);
	if (f != KP_NONE) type = g->w->fog.kind[f];
	kp_mark_target(g, mx, my, u); /* the target's flash (selection timer 31) or the ground marker (0x417ad0) */
	cmd[0] = CMD_RIGHT_CLICK;
	kp_le_put16(cmd + 1, (uint32_t)mx);
	kp_le_put16(cmd + 3, (uint32_t)my);
	kp_le_put16(cmd + 5, u ? kp_ui_unit_ref(g, u) : 0);
	kp_le_put16(cmd + 7, (uint32_t)type);
	cmd[9] = p->keys[0];
	kp_ui_queue_command(ui, cmd, sizeof cmd); /* SendRightClickCommand 0x4740a0 */
	if (right_click_feedback(ui, u)) kp_ui_yes_sound(ui, status);
}

/* --- the targeting and the placement --- */

static void restore_input_after_click(KpUi *ui);

/* IsTypeAttackable 0x420a90. */
static int type_attackable(KpGame *g, uint16_t type)
{
	return type < TYPE_NONE && !(KP_DAT(g, units, special_ability_flags, type) & DAT_INVINCIBLE);
}

/* CanTypeBeAttackedByUnit 0x41db90. */
static int type_attackable_by(KpGame *g, int64_t u, uint16_t type)
{
	if (type >= TYPE_NONE || (KP_DAT(g, units, special_ability_flags, type) & DAT_INVINCIBLE)) return 0;
	if (type == KP_TYPE_REAVER || type == KP_TYPE_WARBRINGER || type == KP_TYPE_CARRIER || type == KP_TYPE_GANTRITHOR) return 1;
	return kp_ground_weapon_with_subunit(g, u) != WEAPON_NONE;
}

/* ValidateOrderTarget 0x429870's errors (the interface's use: only the error). */
static void order_target_error(KpGame *g, int64_t u, uint8_t order, int64_t target, int64_t *err)
{
	KpUnits *units = &g->u;
	switch (order) {
	case ORDER_REPAIR:
		if (target == 0 || kp_unit_race(g, target) != KP_RACE_TERRAN) *err = ERR_NOT_TERRAN;
		else if (!(KP_DAT(g, units, special_ability_flags, units->type[target]) & DAT_MECHANICAL)
			|| !(units->status[target] & STATUS_COMPLETED_FLAG)) *err = ERR_NOT_MECHANICAL;
		break;
	case ORDER_HARVEST:
		if (target == 0) break;
		if (units->type[target] == KP_TYPE_GEYSER) *err = STRING_HARVEST_ERROR + kp_unit_race(g, u);
		else if (kp_mineral_field(units->type[target])) break;
		else if (!(KP_DAT(g, units, special_ability_flags, units->type[target]) & DAT_RESOURCE_CONTAINER)) *err = ERR_NOT_RESOURCE;
		else if (units->owner[target] != units->owner[u]) *err = ERR_NOT_YOURS;
		break;
	case ORDER_PICKUP:
		if (target == 0 || units->owner[target] != units->owner[u]) *err = ERR_CANNOT_LOAD;
		else if (!kp_can_load(g, target, u)
			&& KP_DAT(g, units, space_required, units->type[target]) <= KP_DAT(g, units, space_provided, units->type[u])) {
			*err = ERR_TRANSPORT_FULL;
		} else if (!kp_can_load(g, target, u)) {
			*err = ERR_CANNOT_LOAD;
		}
		break;
	case ORDER_HEAL_MOVE:
		if (target == 0 || !kp_can_use_tech(g, KP_TECH_HEALING, units->owner[u], u)) break;
		if ((units->status[target] & (STATUS_GROUNDED_BUILDING | STATUS_IN_AIR))
			|| !(KP_DAT(g, units, special_ability_flags, units->type[target]) & DAT_ORGANIC)) *err = ERR_NOT_ORGANIC;
		break;
	default:
		break;
	}
}

/* ValidateObscuredOrderTarget 0x4297f0's errors. */
static void obscured_target_error(KpGame *g, int64_t u, uint8_t order, uint16_t type, int64_t *err)
{
	static const uint16_t GAS[3] = { KP_TYPE_EXTRACTOR, KP_TYPE_REFINERY, KP_TYPE_ASSIMILATOR }; /* 0x429c80 */
	int64_t race = kp_unit_race(g, u);
	if (order == ORDER_REPAIR_OBSCURED) {
		if (type < TYPE_FIRST_TERRAN_BUILDING || type > TYPE_LAST_TERRAN_BUILDING) *err = ERR_NOT_TERRAN;
	} else if (order == ORDER_HARVEST_OBSCURED) {
		if (type == KP_TYPE_GEYSER) *err = STRING_HARVEST_ERROR + race;
		else if (!kp_resource_type(type) && !(race >= 0 && race < 3 && type == GAS[race])) *err = ERR_NOT_RESOURCE;
	}
}

/* GetTargetErrorString 0x4bceb0. */
static int64_t target_error(KpGame *g, int64_t target, int64_t weapon, int64_t err)
{
	if (target != 0 && (g->u.status[target] & STATUS_INVINCIBLE)) {
		if (g->u.stasis_timer[target] != 0) return ERR_IN_STASIS;
		return (g->u.status[target] & STATUS_GROUNDED_BUILDING) ? ERR_INVINCIBLE_BUILDING : ERR_NO_ENERGY_SPELL;
	}
	if ((uint16_t)err == ERR_NONE) err = STRING_CANNOT_ATTACK;
	if (weapon != WEAPON_NONE) err = KP_DAT(g, weapons, target_error_message, weapon);
	return err;
}

/* ConfirmTargetedOrder 0x4bc820: the targeted order for the console's selection, or the error that blocks it. */
static void confirm_targeted(KpUi *ui, int32_t x, int32_t y, int64_t target, uint16_t type)
{
	KpGame *g = game(ui);
	KpUiInput *p = in(ui);
	uint8_t order, cmd[11];
	int64_t w, weapon, tech, cost, err = ERR_NONE, status = ui->hud.status_unit;
	int valid = 0, energy_ok = 0, spell_ok = 0, k;
	if (!p->targeting) return;
	order = kp_ui_finish_targeting(ui, target, type);
	w = KP_DAT(g, orders, use_weapon_targeting, order);
	weapon = KP_DAT(g, orders, targeting, order);
	tech = KP_DAT(g, orders, technology_energy, order);
	cost = kp_order_energy(g, order);
	if (KP_DAT(g, orders, obscured_order, order) == ORDER_NONE) type = TYPE_NONE;
	for (k = 0; k < SLOTS; k++) {
		int64_t u = ui->hud.selection[k];
		if (u == 0 || kp_unit_disabled(g, u)) continue;
		if (w && weapon == WEAPON_NONE) {
			if (target == 0) {
				valid |= type == TYPE_INFESTED_TERRAN || type_attackable_by(g, u, type);
			} else if (kp_right_click_action(g, u) == 3) { /* a burrowed Lurker */
				if (kp_target_out_of_range(g, u, target)) err = ERR_OUT_OF_RANGE;
				else if (kp_target_inside_min_range(g, u, target)) err = ERR_TOO_CLOSE;
				else valid = 1;
			} else {
				valid |= kp_can_attack(g, u, target, 1);
			}
		} else if (w) {
			if (kp_weapon_can_target(g, u, weapon, target) || type_attackable(g, type)) {
				valid = 1;
				if (!kp_needs_energy(g, u, cost)) energy_ok = 1;
			}
		} else if (tech != TECH_NONE) {
			int64_t e = 0;
			if (kp_validate_spell_target(g, u, tech, target, type, x, y, &e)) {
				spell_ok = 1;
				if (!kp_needs_energy(g, u, cost)) energy_ok = 1;
			} else if (e) {
				err = e;
			}
		}
		else if (!w) {
			if (type == TYPE_NONE) order_target_error(g, u, order, target, &err);
			else obscured_target_error(g, u, order, type, &err);
		}
	}
	if (w && (!valid || err != ERR_NONE)) {
		err = target_error(g, target, weapon, err);
		kp_ui_error_sound(ui, g->u.owner[status], (uint32_t)(SOUND_TARGET_ERROR + kp_unit_race(g, status)), (uint32_t)err);
		if (weapon != WEAPON_NONE) return;
	} else if (!w && cost != 0 && !spell_ok) {
		kp_ui_error_buzz(ui, status, (uint32_t)(err == ERR_NONE ? ERR_NO_ENERGY_SPELL : err), g->local_player);
		return;
	} else if (cost != 0 && !energy_ok) {
		int64_t race = kp_unit_race(g, status);
		kp_ui_error_sound(ui, g->u.owner[status], (uint32_t)(SOUND_NO_ENERGY + race), (uint32_t)(STRING_NO_ENERGY + race));
		return;
	} else if (err != ERR_NONE) {
		kp_ui_error_buzz(ui, status, (uint32_t)err, g->u.owner[status]);
		return;
	} else {
		kp_ui_yes_sound(ui, status);
	}
	cmd[0] = CMD_TARGETED;
	kp_le_put16(cmd + 1, (uint32_t)x);
	kp_le_put16(cmd + 3, (uint32_t)y);
	kp_le_put16(cmd + 5, target ? kp_ui_unit_ref(g, target) : 0);
	kp_le_put16(cmd + 7, type);
	cmd[9] = order;
	cmd[10] = p->keys[0];
	kp_ui_queue_command(ui, cmd, sizeof cmd); /* SendTargetedOrderCommand 0x473c70 */
}

void kp_ui_minimap_target(KpUi *ui, int32_t x, int32_t y)
{
	restore_input_after_click(ui);
	confirm_targeted(ui, x, y, 0, TYPE_NONE);
	kp_move_placement_cursor(game(ui), x, y); /* 0x417ad0: the ground marker */
}

void kp_ui_minimap_right_click(KpUi *ui, int32_t x, int32_t y)
{
	KpGame *g = game(ui);
	int64_t status = ui->hud.status_unit;
	uint8_t cmd[10];
	int k, takes = 0, classes = 0;
	if (status == 0 || g->u.owner[status] != g->local_player) return;
	for (k = 0; k < SLOTS; k++) {
		int64_t n = ui->hud.selection[k], c;
		if (n == 0) continue;
		takes |= takes_right_click(g, n);
		c = kp_right_click_action(g, n);
		classes |= c == 1 || c == 2 || c == 4 || c == 5 || c == 6;
	}
	if (!takes || !classes) return;
	cmd[0] = CMD_RIGHT_CLICK;
	kp_le_put16(cmd + 1, (uint32_t)x);
	kp_le_put16(cmd + 3, (uint32_t)y);
	kp_le_put16(cmd + 5, 0);
	kp_le_put16(cmd + 7, TYPE_NONE);
	cmd[9] = in(ui)->keys[0];
	kp_ui_queue_command(ui, cmd, sizeof cmd);
	kp_ui_yes_sound(ui, status);
	kp_move_placement_cursor(g, x, y);
}

/* TargetingLeftClick 0x445d70. */
static void targeting_left(KpUi *ui, KpUiEvent *e)
{
	KpGame *g = game(ui);
	KpUiInput *p = in(ui);
	int32_t mx, my, f;
	int64_t u;
	uint16_t type = TYPE_NONE;
	if (over_console(ui, e->x, e->y)) return;
	mx = e->x + (int16_t)p->screen_x;
	my = e->y + (int16_t)p->screen_y;
	u = pick_unit(ui, mx, my);
	f = kp_fog_at_point(g->w, mx, my);
	if (f != KP_NONE) type = (uint16_t)g->w->fog.kind[f];
	kp_mark_target(g, mx, my, u); /* 0x47fc10: the fog ghost's or the unit's flash, or the marker */
	confirm_targeted(ui, mx, my, u, type);
	restore_input_after_click(ui);
}

static void targeting_right(KpUi *ui, KpUiEvent *e)
{
	(void)e;
	if (in(ui)->targeting) kp_ui_cancel_targeting(ui); /* BtnActCancelTargeting 0x4a04b0 */
}

/* ClosePlacement 0x47d580 after EndPlacement 0x47d300. */
static void placement_left(KpUi *ui, KpUiEvent *e);

/* ClosePlacement 0x47d580's interface part: add-on mode 0x6751f8 = 0, the command card's set, the input handlers and
 * the minimap's cursor. */
static void placement_ui_closed(KpUi *ui)
{
	kp_ui_placement_free(ui); /* FreePlacementBitmaps 0x47ce20, layers 4 and 3 off */
	in(ui)->placement_parent = 0;
	kp_ui_card_override(ui, KP_UI_SET_NONE);
	kp_ui_install_game_input(ui);
	kp_ui_set_textbox_prompt(ui, NULL); /* ClosePlacement 0x47d580 */
	in(ui)->minimap_cursor = 0;
}

static void close_placement(KpUi *ui)
{
	kp_placement_close(game(ui));
	placement_ui_closed(ui);
}

void kp_ui_cursor_modes_ended(KpUi *ui, int targeting)
{
	if (in(ui)->left == placement_left) placement_ui_closed(ui);
	if (targeting && in(ui)->targeting) kp_ui_cancel_targeting(ui); /* 0x423120's own targeting part */
}

void kp_ui_end_placement(KpUi *ui)
{
	if (ui->game != NULL && kp_scalar(ui->game, KP_PLACEMENT_ACTIVE) == 1) close_placement(ui);
}

/* PlacementLeftClick 0x47ce60 with UpdatePlacementCursor 0x47d7e0 (ScreenToPlacementTile 0x47d490, CheckPlacementAt
 * 0x47db80, the preview drawn). */
static void placement_left(KpUi *ui, KpUiEvent *e)
{
	KpGame *g = game(ui);
	int64_t status = ui->hud.status_unit, tx, ty, error;
	uint16_t type = (uint16_t)kp_scalar(g, KP_PLACEMENT_TYPE);
	uint8_t order = (uint8_t)kp_scalar(g, KP_PLACEMENT_ORDER), cmd[8];
	if (over_console(ui, e->x, e->y)) return;
	kp_ui_placement_update(ui); /* outside the game view the last tile and error stay */
	tx = ui->placement.tile_x;
	ty = ui->placement.tile_y;
	error = ui->placement.error;
	if (error != 0) { /* EndPlacement 0x47d300: the placement stays */
		if (order == ORDER_PLACE_PROTOSS) {
			kp_ui_error_line(ui, kp_ui_tbl(ui, KP_UI_STAT_TXT, STRING_NOT_POWERED));
			kp_ui_worker_voice(ui, status, SOUND_PLACEMENT_ERROR, -1);
		} else {
			static const uint16_t STRINGS[12] = { 0, 0x355, 0x354, 0x356, 0x358, 0x35c, 0x35d, 0x35e, 0x35b, 0x35a, 0x359,
				0x357 }; /* 0x508d90 */
			kp_ui_worker_voice(ui, status, SOUND_WORKER_ERROR, error < 12 ? STRINGS[error] : 0);
		}
		return;
	}
	kp_ui_placed_sound(ui, type);
	close_placement(ui);
	cmd[0] = CMD_BUILD;
	cmd[1] = order;
	kp_le_put16(cmd + 2, (uint32_t)tx);
	kp_le_put16(cmd + 4, (uint32_t)ty);
	kp_le_put16(cmd + 6, type);
	kp_ui_queue_command(ui, cmd, sizeof cmd); /* SendBuildCommand 0x474010 */
	restore_input_after_click(ui);
}

/* CancelPlacementRightClick 0x448760: EndPlacement 0x47d300(0, 0, 1). */
static void placement_right(KpUi *ui, KpUiEvent *e)
{
	(void)e;
	if (kp_scalar(game(ui), KP_PLACEMENT_ACTIVE) == 1) close_placement(ui);
}

static void install_after_click(KpUi *ui, KpUiEvent *e)
{
	(void)e;
	kp_ui_install_game_input(ui);
}

/* RestoreInputAfterClick 0x448c50(0). */
static void restore_input_after_click(KpUi *ui)
{
	KpUiInput *p = in(ui);
	if (p->mouse_state == 0) {
		kp_ui_install_game_input(ui);
		return;
	}
	p->proc[KP_UI_LEFT_UP] = install_after_click;
	p->proc[KP_UI_RIGHT_UP] = install_after_click;
	p->proc[KP_UI_MIDDLE_UP] = install_after_click;
	p->proc[KP_UI_MOUSE_MOVE] = NULL;
	kp_ui_cursor_set(ui, 0);
}

/* CancelTargetingUi 0x423120. */
static void cancel_modes(KpUi *ui)
{
	if (kp_scalar(game(ui), KP_PLACEMENT_ACTIVE) == 1) close_placement(ui);
	if (in(ui)->targeting) kp_ui_cancel_targeting(ui);
}

/* The character procedure 0x448780: a digit recalls its group; Escape skips the transmissions (its `je 0x4bda80`
 * into SkipLocalTriggerWaits, paused or not: a paused game's Escape is the text box's, 0x4b3960). */
static void game_char(KpUi *ui, KpUiEvent *e)
{
	int c = e->key & 0xff;
	if ((unsigned)(c - '0') <= 9) select_group(ui, c - '0');
	if (c == 27) kp_trigger_skip(game(ui));
}

/* ToggleMusic 0x49ec00 and ToggleSound 0x49ec40: a volume goes to 0 and comes back. */
static void toggle_volumes(KpUi *ui, int id)
{
	KpUiSound *s = &ui->sound;
	if (id == ACC_MUSIC) {
		if (s->music != 0) {
			s->music_saved = s->music;
			s->music = 0;
		} else {
			s->music = s->music_saved;
		}
		kp_ui_apply_volumes(ui); /* ApplyVolumeOptions 0x49ea60 */
	} else if (id == ACC_SOUND && s->device) {
		if (s->effects == 0) {
			s->effects = s->effects_saved;
		} else {
			s->effects_saved = s->effects;
			s->effects = 0;
			kp_ui_stop_sounds(ui); /* StopAllSounds 0x49eca0 */
		}
		/* (0x4c3760's controls' volume: a store nothing reads) */
	}
}

/* GameCommandHandler 0x4487a0: the volume toggles, the menus, then (unless the game is paused, IsGamePaused
 * 0x47cb60) the groups, the screen and the speed. */
static void game_command(KpUi *ui, KpUiEvent *e)
{
	KpGame *g = game(ui);
	KpUiInput *p = in(ui);
	int id = e->key, k;
	toggle_volumes(ui, id);
	if (id == ACC_GAME_MENU) {
		kp_ui_game_menu(ui, KP_UI_GAME_MENU);
	} else if (id == ACC_HELP) {
		kp_ui_game_menu(ui, KP_UI_GAME_MENU_HELP);
	} else if (id == ACC_OPTIONS) {
		kp_ui_game_menu(ui, KP_UI_GAME_MENU_OPTIONS);
	} else if (id == ACC_QUIT_MISSION) {
		kp_ui_game_menu(ui, KP_UI_GAME_MENU_QUIT_MISSION);
	} else if (id == ACC_QUIT) {
		kp_ui_game_menu(ui, KP_UI_GAME_MENU_QUIT);
	} else if (id == ACC_SAVE) {
		kp_ui_game_menu(ui, KP_UI_GAME_MENU_SAVE);
	} else if (id == ACC_LOAD) {
		kp_ui_game_menu(ui, KP_UI_GAME_MENU_LOAD);
	} else if (id == ACC_LEADERBOARD) {
		kp_ui_toggle_leaderboard(ui);
	} else if (kp_scalar(g, KP_PAUSED) != 0) {
		/* the rest waits for the game */
	} else if (id >= ACC_ADD_FIRST && id < ACC_ADD_FIRST + 10) {
		send_group(ui, GROUP_ADD, (uint8_t)((id - ACC_ADD_FIRST + 1) % 10), NULL, 0);
	} else if (id >= ACC_ASSIGN_FIRST && id < ACC_ASSIGN_FIRST + 10) {
		send_group(ui, GROUP_ASSIGN, (uint8_t)((id - ACC_ASSIGN_FIRST + 1) % 10), NULL, 0);
	} else if (id >= ACC_CENTRE_FIRST && id < ACC_CENTRE_FIRST + 10) {
		k = (id - ACC_CENTRE_FIRST + 1) % 10;
		centre_on_group(ui, k);
		select_group(ui, k);
	} else if (id == ACC_CENTRE_STATUS && ui->hud.status_unit != 0) {
		kp_ui_centre_on_unit(ui, ui->hud.status_unit);
	} else if (id == ACC_SPEED_UP || id == ACC_SPEED_DOWN) {
		/* 0x474380 (single player only; Koprulu has no other): the speed changes when the command runs */
		int64_t speed = kp_scalar(g, KP_SPEED);
		if (id == ACC_SPEED_UP ? speed < SPEED_FASTEST : speed > 0) {
			uint8_t cmd[2] = { CMD_SPEED, (uint8_t)(speed + (id == ACC_SPEED_UP ? 1 : -1)) };
			kp_ui_queue_command(ui, cmd, sizeof cmd);
		}
	} else if (id >= ACC_SAVE_SCREEN && id < ACC_SAVE_SCREEN + 3) {
		p->scroll.saved[id - ACC_SAVE_SCREEN][0] = (int16_t)p->screen_x;
		p->scroll.saved[id - ACC_SAVE_SCREEN][1] = (int16_t)p->screen_y;
	} else if (id >= ACC_JUMP_SCREEN && id < ACC_JUMP_SCREEN + 3) {
		kp_ui_set_screen_position(ui, p->scroll.saved[id - ACC_JUMP_SCREEN][0], p->scroll.saved[id - ACC_JUMP_SCREEN][1]);
	}
}

/* --- the cursor --- */

#define CARD_RECT_LEFT 496       /* 0x5033d0 */
#define CARD_RECT_TOP 354
#define CARD_RECT_RIGHT 639
#define CARD_RECT_BOTTOM 479
#define MINIMAP_RECT_LEFT 6      /* 0x508bb0 */
#define MINIMAP_RECT_TOP 348
#define MINIMAP_RECT_RIGHT 133
#define MINIMAP_RECT_BOTTOM 475
#define OWNER_NEUTRAL 11
#define VK_PRIOR 0x21
#define VK_DOWN 0x28
#define VK_NUMPAD0 0x60
#define VK_NUMPAD9 0x69
#define VK_NUMLOCK 0x90

/* IsEnemyOfLocalPlayer 0x43e740. */
static int enemy_of_local(KpGame *g, int64_t u)
{
	return kp_alliance(g, g->local_player, g->u.owner[u]) == 0 && !g->w->replay;
}

/* GetUnitHoverCursorId 0x43ea90. */
static uint16_t hover_cursor(KpUi *ui)
{
	KpGame *g = game(ui);
	KpUiInput *p = in(ui);
	int64_t u;
	if (kp_scalar(g, KP_PLACEMENT_ACTIVE) == 1) return 0;
	u = pick_unit(ui, (int16_t)p->screen_x + ui->mouse_x, (int16_t)p->screen_y + ui->mouse_y);
	if (p->targeting) {
		if (u == 0) return 5;
		if (g->u.owner[u] == OWNER_NEUTRAL) return 2;
		return enemy_of_local(g, u) ? 3 : 4;
	}
	if (u == 0) return 0;
	if (g->u.owner[u] == OWNER_NEUTRAL) return 8;
	return enemy_of_local(g, u) ? 7 : 6;
}

/* The game view's rectangle 0x666430 = {0, 0, 639, 399} for the original's view, the view's here; PtInRect leaves its
 * right and bottom edges outside. */
static int in_game_view(const KpUi *ui, int32_t x, int32_t y)
{
	return x >= 0 && x < ui->view_w - 1 && y >= 0 && y < ui->view_h - 1;
}

/* GetPointerCursorId 0x43e6d0: the minimap's rectangle 0x508bb0 (PtInRect: right and bottom outside) is the image's
 * as LayoutMinimap 0x46d850 placed it for the map's size (the original's {6, 348, 133, 475} before a minimap); a point
 * in it has the original's coordinates only on a panel (the minimap dialog covers the rectangle). */
static uint16_t pointer_cursor(KpUi *ui)
{
	int32_t x = ui->mouse_x, y = ui->mouse_y;
	const int16_t *r = ui->minimap.clip;
	int laid_out = r[2] > r[0];
	int32_t left = laid_out ? r[0] : MINIMAP_RECT_LEFT, top = laid_out ? r[1] : MINIMAP_RECT_TOP;
	int32_t right = laid_out ? r[2] : MINIMAP_RECT_RIGHT, bottom = laid_out ? r[3] : MINIMAP_RECT_BOTTOM;
	if (in(ui)->over_console < 0 && x >= left && x < right && y >= top && y < bottom) { /* not the game view's point */
		return in(ui)->minimap_cursor;
	}
	if (over_console(ui, x, y)) return 0;
	if (in_game_view(ui, x, y)) return in(ui)->box ? CURSOR_DRAG : hover_cursor(ui);
	return 0;
}

/* The cursor: the edge scroll's (edge: the cursor's position checked) or else the pointer's, then AnimateCursor. */
static void update_cursor(KpUi *ui, int edge, int32_t cursor_x, int32_t cursor_y)
{
	if (!ui->cursor_locked) {
		uint16_t id = edge ? kp_ui_edge_scroll(ui, cursor_x, cursor_y, NULL, NULL) : 0;
		if (id == 0) id = pointer_cursor(ui);
		if (id != ui->cursor) kp_ui_cursor_set(ui, id);
	}
	kp_ui_cursor_animate(ui);
}

void kp_ui_cursor_update(KpUi *ui, int32_t cursor_x, int32_t cursor_y) { update_cursor(ui, 1, cursor_x, cursor_y); }

void kp_ui_pointer_update(KpUi *ui) { update_cursor(ui, 0, 0, 0); }

int kp_ui_touch_target(KpUi *ui, int32_t x, int32_t y)
{
	KpGame *g = game(ui);
	KpUiInput *p = in(ui);
	int64_t u, s;
	if (g == NULL) return KP_UI_TOUCH_SELECT;
	if (p->targeting) return KP_UI_TOUCH_AIM;
	if (kp_scalar(g, KP_PLACEMENT_ACTIVE) == 1) return kp_ui_placement_at(ui, x, y) ? KP_UI_TOUCH_GHOST : KP_UI_TOUCH_PLACE;
	u = pick_unit(ui, x + (int16_t)p->screen_x, y + (int16_t)p->screen_y);
	if (u != 0 && g->u.owner[u] == g->local_player) return KP_UI_TOUCH_SELECT;
	s = g->local_selection[0];
	return s != 0 && g->u.owner[s] == g->local_player && !g->w->replay ? KP_UI_TOUCH_ORDER : KP_UI_TOUCH_SELECT;
}

/* --- the procedures --- */

/* GameIdleThunk 0x448f10: TickSoundCooldowns 0x446ff0. */
static void idle(KpUi *ui, KpUiEvent *e)
{
	(void)e;
	kp_ui_tick_sounds(ui);
}

/* GameLeftClick 0x448c90. */
static void game_left(KpUi *ui, KpUiEvent *e)
{
	if (ui->cursor < CURSOR_SCROLL_FIRST && in(ui)->left) in(ui)->left(ui, e);
}

/* CancelCommandMode 0x446050: the targeting ends (0x4b2900), else the placement (EndPlacement 0x47d300(0, 0, 1)), else
 * the command card goes back to the selection's own set (0x4a11e0(228)). */
static void cancel_command_mode(KpUi *ui)
{
	if (in(ui)->targeting) kp_ui_cancel_targeting(ui);
	else if (kp_scalar(game(ui), KP_PLACEMENT_ACTIVE) == 1) close_placement(ui);
	else kp_ui_card_override(ui, KP_UI_SET_NONE);
}

/* GameRightClick 0x448eb0: at the mouse (0x6d3154) in the command card's rectangle 0x5033d0 (a point of the original's
 * coordinates: on a panel, the card dialog covering the rectangle; the console art off the panels has the logical
 * screen's) or outside the game view the mode ends; elsewhere the mode's right click. */
static void game_right(KpUi *ui, KpUiEvent *e)
{
	int32_t x = ui->mouse_x, y = ui->mouse_y;
	if (ui->cursor >= CURSOR_SCROLL_FIRST) return;
	if ((in(ui)->over_console < 0 && x >= CARD_RECT_LEFT && x < CARD_RECT_RIGHT && y >= CARD_RECT_TOP
		&& y < CARD_RECT_BOTTOM) || !in_game_view(ui, x, y)) {
		cancel_command_mode(ui);
		return;
	}
	if (in(ui)->right) in(ui)->right(ui, e);
}

void kp_ui_reset_input(KpUi *ui)
{
	/* ResetInputHandlers 0x43f330: the reset callback once, the 19 procedures cleared, the default accelerators and
	 * WM_COMMAND handler (the cursor's clip is the host's) */
	KpUiInput *p = in(ui);
	if (p->reset) {
		void (*reset)(KpUi *) = p->reset;
		p->reset = NULL;
		reset(ui);
	}
	memset(p->proc, 0, sizeof p->proc);
	kp_ui_set_accelerators(ui, KP_UI_ACCEL_DEFAULT);
	ui->keys.handler = KP_UI_HANDLER_DEFAULT;
}

void kp_ui_install_game_input(KpUi *ui)
{
	KpUiInput *p = in(ui);
	kp_ui_reset_input(ui);
	p->proc[KP_UI_LEFT_DOWN] = game_left;
	p->proc[KP_UI_LEFT_DOUBLE] = game_left;
	p->proc[KP_UI_RIGHT_DOWN] = game_right;
	p->proc[KP_UI_RIGHT_DOUBLE] = game_right;
	p->proc[KP_UI_CHAR] = game_char;
	p->proc[KP_UI_COMMAND] = game_command;
	kp_ui_set_accelerators(ui, KP_UI_ACCEL_GAME); /* SetGameAccelerators 0x43f140 */
	ui->keys.handler = KP_UI_HANDLER_GAME;
	p->proc[KP_UI_MIDDLE_DOWN] = kp_ui_middle_down;
	p->proc[KP_UI_MIDDLE_UP] = kp_ui_middle_up;
	p->proc[KP_UI_IDLE] = idle; /* GameIdleThunk 0x448f10 */
	if (ui->game != NULL && kp_scalar(ui->game, KP_PLACEMENT_ACTIVE) == 1) {
		p->left = placement_left;
		p->right = placement_right;
	} else if (p->targeting) {
		p->left = targeting_left;
		p->right = targeting_right;
	} else {
		p->left = begin_box;
		p->right = right_click;
	}
}

/* --- the dispatch --- */

static int mouse_event(uint16_t type)
{
	switch (type) {
	case KP_UI_MOUSE_MOVE: case KP_UI_LEFT_DOWN: case KP_UI_LEFT_UP: case KP_UI_LEFT_DOUBLE: case KP_UI_RIGHT_DOWN:
	case KP_UI_RIGHT_UP: case KP_UI_RIGHT_DOUBLE: case KP_UI_MIDDLE_DOWN: case KP_UI_MIDDLE_UP: case KP_UI_MIDDLE_DOUBLE:
		return 1;
	default:
		return 0;
	}
}

int kp_ui_dispatch(KpUi *ui, KpUiEvent *e)
{
	int32_t d, next;
	int r = 0;
	/* a point the layout answered for (over_console 0 or 1: on the game view or the console art, on none of its
	 * panels) keeps the logical screen's coordinates, which may lie inside a console dialog's original rectangle: the
	 * console's dialogs, which a layout places away from their original rectangles, see it outside themselves (their
	 * hover and tooltips end as when the cursor leaves them) */
	KpUiEvent outside = *e;
	int off_panels = in(ui)->over_console >= 0 && mouse_event(e->type);
	outside.x = outside.y = -1;
	/* (DestroyPendingDialogs 0x4c36b0 has nothing to do here: kp_ui_close_dialog only unlinks a dialog, initializing
	 * or not, and its destruction is the modal run's or the host's, never inside its own procedure) */
	if (e->type < KP_UI_EVENT_TYPES && ui->capture[e->type] >= 0) {
		KpUiControl *c = &ui->controls[ui->capture[e->type]];
		r = c->interact ? c->interact(ui, c, e) : 0;
	}
	if (!r) {
		for (d = ui->open; d >= 0; d = next) {
			KpUiControl *c = &ui->controls[d];
			KpUiEvent *seen = off_panels && kp_ui_is_console_dialog(ui, d) ? &outside : e;
			next = c->next;
			if (c->interact && (r = c->interact(ui, c, seen)) != 0) break;
		}
	}
	(void)kp_ui_press_slots(ui);
	return r;
}

void kp_ui_input_event(KpUi *ui, KpUiEvent *e)
{
	switch (e->type) {
	case KP_UI_KEY_DOWN: case KP_UI_KEY_REPEAT: /* MainWindowProc 0x43f600's key bytes */
		in(ui)->vk[e->key & 0xff] = 1;
		if ((e->key & 0xff) == VK_NUMLOCK) {
			memset(in(ui)->vk + VK_NUMPAD0, 0, VK_NUMPAD9 - VK_NUMPAD0 + 1);
			memset(in(ui)->vk + VK_PRIOR, 0, VK_DOWN - VK_PRIOR + 1);
		}
		break;
	case KP_UI_KEY_UP:
		in(ui)->vk[e->key & 0xff] = 0;
		break;
	case KP_UI_LEFT_DOWN: case KP_UI_LEFT_UP: case KP_UI_LEFT_DOUBLE: case KP_UI_RIGHT_DOWN: case KP_UI_RIGHT_UP:
	case KP_UI_RIGHT_DOUBLE: case KP_UI_MIDDLE_DOWN: case KP_UI_MIDDLE_UP: case KP_UI_MIDDLE_DOUBLE: case KP_UI_MOUSE_MOVE:
	case KP_UI_IDLE: /* the pump's idle event carries the mouse's position too (0x4401c0) */
		ui->mouse_x = e->x;
		ui->mouse_y = e->y;
		break;
	default:
		break;
	}
	if (kp_ui_dispatch(ui, e)) return;
	if (e->type == KP_UI_COMMAND && ui->keys.handler == KP_UI_HANDLER_DEFAULT) { /* DefaultCommandHandler 0x448f20 */
		toggle_volumes(ui, e->key);
		return;
	}
	if (e->type < KP_UI_EVENT_TYPES && in(ui)->proc[e->type]) in(ui)->proc[e->type](ui, e);
}

void kp_ui_set_keys(KpUi *ui, uint8_t shift, uint8_t ctrl, uint8_t alt, uint16_t mouse_state)
{
	KpUiInput *p = in(ui);
	p->keys[0] = shift;
	p->keys[1] = ctrl;
	p->keys[2] = alt;
	p->mouse_state = mouse_state;
}

void kp_ui_set_over_console(KpUi *ui, int over) { in(ui)->over_console = (int8_t)over; }

void kp_ui_set_view(KpUi *ui, int32_t w, int32_t h)
{
	ui->view_w = w > TILE ? w : TILE;
	ui->view_h = h > TILE ? h : TILE;
}

void kp_ui_set_screen(KpUi *ui, int32_t x, int32_t y)
{
	in(ui)->screen_x = x;
	in(ui)->screen_y = y;
}
