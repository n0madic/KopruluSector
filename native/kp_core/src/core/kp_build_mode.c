/* The local player's building placement mode (kp_game.h), ported from the atlas (P08, P16, P27, E01): StartPlacement
 * 0x47d3a0, ClosePlacement 0x47d580, the cancels through EndPlacement 0x47d300(0, 0, 1) (CancelTargetingUi 0x423120
 * when the selection empties, CancelCursorModes 0x448c20) and CanPlaceCurrent 0x47db60, by which the game closes the
 * mode when the selected builder can no longer place. The mode's cursor, prompt and preview bitmaps are the
 * interface's (kp_ui_placebox.c); the game keeps whether the mode is on, its order and type, the Psi fields it shows
 * for a Protoss builder, and the world's ghost images of the mode (the pools 0x59406c and 0x595074): a gas building's
 * on the Geysers and a Terran type's on the neutral add-ons it could take (0x47d190, 0x47d080, 0x47d530, 0x47d8e0).
 * The frontend starts and closes it, as an oracle run replays its "placement" events. */
#include "kp_game_internal.h"

#define DAT_ADDON 0x02
#define GROUP_TERRAN 0x02 /* units.dat staredit group flags 0x523c40 */

static int active(KpGame *g) { return kp_scalar(g, KP_PLACEMENT_ACTIVE) == 1; }

/* The interface's status unit (0x6ab4c4): the first of the local selection.
 * TODO: 0x6ab4c4 is the status panel's leader (RebuildStatusSelection 0x4a7cd0: a disabled first unit yields to the
 * next), which the game does not see; the placement's recheck and ghosts test the first unit */
static int64_t status_unit(KpGame *g) { return g->local_selection[0]; }

static int64_t place_w(KpGame *g, int64_t t) { return (int16_t)KP_DAT(g, units, staredit_placement_size, t * 2); }
static int64_t place_h(KpGame *g, int64_t t) { return (int16_t)KP_DAT(g, units, staredit_placement_size, t * 2 + 1); }
static int64_t addon_x(KpGame *g, int64_t t) { return (int16_t)KP_DAT(g, units, addon_position, t * 2); }
static int64_t addon_y(KpGame *g, int64_t t) { return (int16_t)KP_DAT(g, units, addon_position, t * 2 + 1); }

/* PlaceAddonPreview 0x436e70: on unit n's sprite the ghost of type t (its flingy's sprite's image), its footprint's
 * centre where t would stand to take n (or over the Geyser n); 1 when it was made. */
static int attach_ghost(KpGame *g, int64_t n, int64_t t)
{
	int64_t self = g->u.type[n];
	int64_t image = KP_DAT(g, sprites, image, KP_DAT(g, flingy, sprite, KP_DAT(g, units, graphics, t)));
	int64_t ox = kp_s16v(place_w(g, t) / 2 - place_w(g, self) / 2 - addon_x(g, self));
	int64_t oy = kp_s16v(place_h(g, t) / 2 - place_h(g, self) / 2 - addon_y(g, self));
	return kp_placement_ghost_create(g->w, kp_unit_sprite(g, n), (int32_t)image, (int32_t)ox, (int32_t)oy,
		(int32_t)place_w(g, t), (int32_t)place_h(g, t)) != KP_NONE;
}

/* AddGeyserGhosts 0x47d190: a gas building's ghost on every Geyser. */
static void geyser_ghosts(KpGame *g)
{
	int64_t n, t = kp_scalar(g, KP_PLACEMENT_TYPE);
	for (n = g->player_units[KP_PLAYER_NEUTRAL]; n != 0; n = g->u.player_next[n]) {
		if (g->u.type[n] == KP_TYPE_GEYSER && kp_gas_building(t)) attach_ghost(g, n, t);
	}
}

/* AddNeutralAddonGhosts 0x47d080: a Terran type's ghost by every live neutral add-on it may take whose place for it
 * the builder may use (ValidateAddonPlacement 0x413150). */
static void neutral_addon_ghosts(KpGame *g)
{
	int64_t n, t = kp_scalar(g, KP_PLACEMENT_TYPE), b = status_unit(g);
	if (b == 0) return;
	for (n = g->player_units[KP_PLAYER_NEUTRAL]; n != 0; n = g->u.player_next[n]) {
		int64_t a = g->u.type[n], s, tx, ty;
		if ((KP_DAT(g, units, special_ability_flags, a) & DAT_ADDON) == 0 || kp_unit_inactive(g, n)) continue;
		kp_scalar_set(g, KP_REQUIREMENT_ERROR, 0); /* 0x4b3850 */
		if (kp_required_parent_type(g, a) != t) continue;
		s = kp_unit_sprite(g, n);
		tx = kp_s16v(g->w->spr.x[s] - place_w(g, a) / 2 - addon_x(g, a)) / 32; /* toward zero */
		ty = kp_s16v(g->w->spr.y[s] - place_h(g, a) / 2 - addon_y(g, a)) / 32;
		if (kp_validate_addon_placement(g, b, g->u.owner[b], tx, ty, t) == 0) attach_ghost(g, n, t);
	}
}

/* The ghosts of the mode's type outside the add-on mode (0x47d3a0, 0x47d530). */
static void add_ghosts(KpGame *g)
{
	int64_t t = kp_scalar(g, KP_PLACEMENT_TYPE);
	if (kp_gas_building(t)) geyser_ghosts(g); /* 0x429320 */
	else if ((KP_DAT(g, units, staredit_group_flags, t) & GROUP_TERRAN) != 0) neutral_addon_ghosts(g);
}

static int addon_mode(KpGame *g) /* 0x6751f8: the mode places an add-on */
{
	return (KP_DAT(g, units, special_ability_flags, kp_scalar(g, KP_PLACEMENT_TYPE)) & DAT_ADDON) != 0;
}

void kp_placement_refresh_ghosts(KpGame *g)
{
	if (!active(g) || addon_mode(g)) return;
	kp_placement_ghosts_release(g->w);
	add_ghosts(g);
}

void kp_placement_geyser_ghost(KpGame *g, int64_t geyser)
{
	int64_t t = kp_scalar(g, KP_PLACEMENT_TYPE);
	if (active(g) && kp_gas_building(t)) attach_ghost(g, geyser, t);
}

void kp_placement_remove_ghosts(KpGame *g, int64_t n) { kp_sprite_release_placement_ghosts(g->w, kp_unit_sprite(g, n)); }

/* StartPlacement 0x47d3a0: the mode places type t with the order, with its ghosts outside the add-on mode; a Protoss
 * builder shows the Psi fields. */
void kp_placement_start(KpGame *g, int64_t order, int64_t t)
{
	int64_t n = status_unit(g);
	kp_scalar_set(g, KP_PLACEMENT_ACTIVE, 1);
	kp_scalar_set(g, KP_PLACEMENT_ORDER, kp_band(order, 0xff));
	kp_scalar_set(g, KP_PLACEMENT_TYPE, kp_band(t, 0xffff));
	if (!addon_mode(g)) add_ghosts(g);
	if (n != 0 && kp_unit_race(g, n) == KP_RACE_PROTOSS) kp_power_show_fields(g); /* 0x430320 */
}

/* ClosePlacement 0x47d580: the mode ends, the Psi fields hide (HideAllPsiFieldOverlays 0x42fda0) and the ghosts go. */
void kp_placement_close(KpGame *g)
{
	kp_scalar_set(g, KP_PLACEMENT_ACTIVE, 0);
	kp_power_hide_fields(g);
	kp_placement_ghosts_release(g->w); /* 0x40f490 */
}

/* EndPlacement 0x47d300(0, 0, 1): an active mode closes without a sound. */
void kp_placement_cancel(KpGame *g)
{
	if (active(g)) kp_placement_close(g);
}

/* CanPlaceCurrent 0x47db60 and the cancel: the mode closes when its builder may no longer place its type with its
 * order for the current player 0x508d44 (0x47da50). */
void kp_placement_recheck(KpGame *g)
{
	int64_t n = status_unit(g), player = kp_scalar(g, KP_COMMAND_PLAYER);
	if (!active(g)) return;
	if (player == KP_NIL) player = g->local_player; /* 0x508d44 before any command: the local player's */
	if (n != 0 && kp_may_place(g, kp_scalar(g, KP_PLACEMENT_ORDER), kp_scalar(g, KP_PLACEMENT_TYPE), player, n)) return;
	kp_placement_close(g);
	kp_event(g, KP_EVENT_CURSOR_MODES, 0, 0, 0, NULL);
}

void kp_cancel_cursor_modes(KpGame *g)
{
	kp_placement_cancel(g);
	kp_event(g, KP_EVENT_CURSOR_MODES, 1, 0, 0, NULL);
}
