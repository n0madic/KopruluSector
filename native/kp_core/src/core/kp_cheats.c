/* The cheat command (atlas P25, P03): ExecCheat 0x472530 and ApplyCheatFlags 0x40d9d0 with its one-shot cheats.
 *
 * The interface recognizes a typed cheat (ProcessCheatText 0x40dee0) and sends the whole new cheat word as command
 * 0x12 (SendCheatCommand 0x473d40); the game runs it at its turn. The word's other bits only persist for their
 * consumers (KP_CHEAT_* in kp_ids.h). A single-player game is never a multiplayer one (0x672664 = 0), so the
 * multiplayer exits of these functions do not apply. */
#include "kp_game_internal.h"

#define CHEAT_MONEY 10000        /* show me the money: minerals and gas */
#define CHEAT_RESOURCE_GIFT 500  /* whats mine is mine (minerals), breathe deep (gas) */
#define CHEAT_UPGRADES 60        /* something for nothing: upgrades 0..59 */
#define STRING_CHEAT_ENABLED 0x41 /* rez\network.tbl */
#define STRING_CHEAT_DISABLED 0x42

static int human(KpGame *g, int64_t p)
{
	return kp_controller(g, p) == KP_CTRL_HUMAN;
}

/* ApplyCheatFlags 0x40d9d0: commit the new word, run its one-shot bits and clear them, then the message. Bit 0x4000's
 * toggle only redraws the game layer (0x445880), which the presentation does every frame. */
static void apply_cheat_flags(KpGame *g, int64_t word)
{
	int64_t old = kp_cheats(g), p, u;
	int changed = word != old;
	kp_scalar_set(g, KP_CHEATS, word);
	if (kp_band(kp_bxor(word, old), KP_CHEAT_BLACK_SHEEP_WALL) != 0) kp_apply_vision_cheats(g);
	if (kp_band(word, KP_CHEAT_SHOW_ME_THE_MONEY) != 0) {
		word = kp_band(word, kp_bnot(KP_CHEAT_SHOW_ME_THE_MONEY));
		for (p = 0; p < KP_PLAYERS; p++) {
			if (!human(g, p)) continue;
			g->players.minerals[p] = kp_u32v(g->players.minerals[p] + CHEAT_MONEY);
			g->players.gas[p] = kp_u32v(g->players.gas[p] + CHEAT_MONEY);
		}
	}
	if (kp_band(word, KP_CHEAT_WHATS_MINE) != 0) {
		word = kp_band(word, kp_bnot(KP_CHEAT_WHATS_MINE));
		for (p = 0; p < KP_PLAYERS; p++) {
			if (human(g, p)) g->players.minerals[p] = kp_u32v(g->players.minerals[p] + CHEAT_RESOURCE_GIFT);
		}
	}
	if (kp_band(word, KP_CHEAT_BREATHE_DEEP) != 0) {
		word = kp_band(word, kp_bnot(KP_CHEAT_BREATHE_DEEP));
		for (p = 0; p < KP_PLAYERS; p++) {
			if (human(g, p)) g->players.gas[p] = kp_u32v(g->players.gas[p] + CHEAT_RESOURCE_GIFT);
		}
	}
	if (kp_band(word, KP_CHEAT_SOMETHING_FOR_NOTHING) != 0) {
		word = kp_band(word, kp_bnot(KP_CHEAT_SOMETHING_FOR_NOTHING));
		for (p = 0; p < KP_PLAYERS; p++) {
			if (!human(g, p)) continue;
			for (u = 0; u < CHEAT_UPGRADES; u++) {
				int64_t level = kp_band(kp_upgrade_level(g, p, u), 0xff);
				if (level >= KP_DAT(g, upgrades, max_repeats, u)) continue;
				kp_set_upgrade_level(g, p, u, level + 1);
				if (g->player_units[p] != 0) kp_apply_upgrade_to_units(g, p, u); /* 0x43b0b0 from the list's head */
			}
		}
	}
	kp_scalar_set(g, KP_CHEATS, word);
	if (changed) {
		kp_event(g, KP_EVENT_NETWORK_TEXT, kp_u32v(word) < kp_u32v(old) ? STRING_CHEAT_DISABLED : STRING_CHEAT_ENABLED,
			0, 0, NULL);
	}
	g->interface_refresh++; /* RefreshStatusUi 0x4a7ed0, for the interface */
}

/* ExecCheat 0x472530: when the game's template allows cheats (AreCheatsEnabled 0x447640, byte 0x67294e; 1 in every
 * campaign game, as the emulator's dumps show), apply the word. */
void kp_exec_cheat(KpGame *g, int64_t word)
{
	apply_cheat_flags(g, kp_u32v(word));
}
