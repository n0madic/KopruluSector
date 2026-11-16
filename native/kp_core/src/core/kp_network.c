/* The network game's commands among the players (phase M stage M5, atlas P25 and P26): the alliances (ExecAlliance
 * 0x4731d0, command 0x0e), the shared vision (ExecSharedVision 0x473280, 0x0d) and a player leaving (ExecLeaveGame
 * 0x472da0, 0x57), with the allied targets' cleanup (ClearAlliedOrderTargets 0x4721c0) the alliance trigger shares.
 * Each runs in a replay as in the network game: the replay plays without the multiplayer byte 0x672664, and these
 * handlers do not test it. */
#include "kp_game_internal.h"
#include "kp_gametype.h"

#define LEFT_GAME_STRING 0x4b /* rez\network.tbl: "%s has left the game." */

void kp_clear_allied_targets(KpGame *g, int64_t p)
{
	KpUnits *u = &g->u;
	int64_t n;
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		int64_t target = u->order_unit[n];
		if (target == 0 || KP_DAT(g, orders, use_weapon_targeting, u->order[n]) == 0) continue;
		if (kp_alliance(g, p, u->owner[target]) == 0) continue;
		if (!kp_is_enemy(g, n, target)) u->order_unit[n] = 0; /* 0x420950 */
	}
}

void kp_command_alliance(KpGame *g, int64_t player, uint32_t stances)
{
	int64_t q;
	if (!kp_setup_alliances_allowed(&g->setup)) return;
	if (player < 0 || player >= KP_PLAYERS) kp_game_fail(g, "network: the alliances of player %lld", (long long)player);
	for (q = 0; q < KP_PLAYERS; q++) kp_set_alliance(g, player, q, (stances >> (2 * q)) & 3);
	kp_refresh_alliance_colors(g);
	kp_clear_allied_targets(g, player);
	/* then BuildAllyRecipientMask 0x442070 for the local player whose chat goes to its allies (0x529040 = 9): the
	 * interface's */
}

/* CanShareVisionWith 0x447600: in a replay always; an observer shares none, every player may share with an observer;
 * else when the game allows alliances. */
static int may_share_vision(KpGame *g, int64_t player, int64_t with)
{
	if (g->w->replay) return 1;
	if (kp_controller(g, player) == KP_CTRL_OBSERVER) return 0;
	if (kp_controller(g, with) == KP_CTRL_OBSERVER) return 1;
	return kp_setup_alliances_allowed(&g->setup);
}

void kp_command_vision(KpGame *g, int64_t player, int64_t mask)
{
	int64_t p;
	if (player < 0 || player >= 8) kp_game_fail(g, "network: the vision of player %lld", (long long)player);
	for (p = 7; p >= 0; p--) {
		if (kp_band(mask, (int64_t)1 << p) != 0 && !may_share_vision(g, player, p)) return;
	}
	g->players.shared_vision[player] = kp_bor(kp_bor(kp_band(mask, 0xff), kp_band(g->players.shared_vision[player], 0xff00)),
		(int64_t)1 << player);
}

void kp_command_leave(KpGame *g, int64_t player)
{
	if (!g->w->replay) return;
	kp_event(g, KP_EVENT_PLAYER_MESSAGE, LEFT_GAME_STRING, player, 0, NULL);
	kp_trigger_player_left(g, player, 0);
	kp_clear_selection_row(g, player);
}
