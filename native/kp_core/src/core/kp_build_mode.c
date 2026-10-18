/* The local player's building placement mode (kp_game.h), ported from the atlas (P08, P16, P27, E01): StartPlacement
 * 0x47d3a0, ClosePlacement 0x47d580, the cancels through EndPlacement 0x47d300(0, 0, 1) (CancelTargetingUi 0x423120
 * when the selection empties, CancelCursorModes 0x448c20) and CanPlaceCurrent 0x47db60, by which the game closes the
 * mode when the selected builder can no longer place. The mode is the interface's: its cursor, prompt, preview bitmaps
 * and the ghost images of gas buildings and add-ons (the pools 0x59406c and 0x595074; 0x47d080, 0x47d190, 0x47d530,
 * 0x47d8e0) are the presentation's; the game keeps whether the mode is on, its order and type, and the Psi fields it
 * shows for a Protoss builder. The frontend starts and closes it, as an oracle run replays its "placement" events. */
#include "kp_game_internal.h"


static int active(KpGame *g) { return kp_scalar(g, KP_PLACEMENT_ACTIVE) == 1; }

/* The interface's status unit (0x6ab4c4): the first of the local selection. */
static int64_t status_unit(KpGame *g) { return g->local_selection[0]; }

/* StartPlacement 0x47d3a0: the mode places type t with the order; a Protoss builder shows the Psi fields. */
void kp_placement_start(KpGame *g, int64_t order, int64_t t)
{
	int64_t n = status_unit(g);
	kp_scalar_set(g, KP_PLACEMENT_ACTIVE, 1);
	kp_scalar_set(g, KP_PLACEMENT_ORDER, kp_band(order, 0xff));
	kp_scalar_set(g, KP_PLACEMENT_TYPE, kp_band(t, 0xffff));
	if (n != 0 && kp_unit_race(g, n) == KP_RACE_PROTOSS) kp_power_show_fields(g); /* 0x430320 */
}

/* ClosePlacement 0x47d580: the mode ends and the Psi fields hide (HideAllPsiFieldOverlays 0x42fda0). */
void kp_placement_close(KpGame *g)
{
	kp_scalar_set(g, KP_PLACEMENT_ACTIVE, 0);
	kp_power_hide_fields(g);
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
}
