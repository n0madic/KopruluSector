/* The network game's wait for overdue turns (net_record.cpp..okcancel.cpp, atlas P27 0x47a810..0x47aba0): while a turn
 * is late WaitForLaggingPlayers 0x47a810 shows rez\timeout.bin over the stopped game, the players without a turn in a
 * shuffled order and a countdown, after which Drop Players drops them (ResolveDesync 0x47a630). The network's game code
 * keeps the wait and its countdown (ui->net); the dialog follows it each frame. */
#include "kp_ui.h"

#include <stdio.h>
#include <string.h>

#include "kp_game.h"
#include "kp_gametype.h"
#include "kp_rng.h"

#define ID_DROP 2                     /* Drop Players */
#define ID_COUNTDOWN (-30)
#define ID_PLAYERS (-10)              /* the first of the players' rows */
#define FLAG_TILED_FRAME 0x20000000u
#define SLOTS 8

/* ShuffleDwordsWithGameSeed 0x47a8d0: the CRT generator reseeded with the game's initial seed (srand), then each pick
 * rand() % the entries left. */
static void shuffle(KpGame *g, int *slots, int n)
{
	int tmp[SLOTS], k, left;
	memcpy(tmp, slots, sizeof(int) * (size_t)n);
	kp_rng_crt_init(g->crt, g->rng->initial_seed);
	for (k = 0, left = n; k < n; k++, left--) {
		int r = (int)(kp_rng_crt_rand(g->crt) % (uint32_t)left);
		slots[k] = tmp[r];
		memmove(tmp + r, tmp + r + 1, sizeof(int) * (size_t)(left - r - 1));
	}
}

/* RefreshLaggingPlayerList 0x47aba0: from control -10 on, a row per lagging human (the force's leader's colour in a team
 * game), the controls after hidden. */
static void refresh_players(KpUi *ui, const int *lagging, int n)
{
	KpGame *g = ui->game;
	int slots[SLOTS], k = 0;
	int32_t c = kp_ui_find_control(ui, ui->timeout.dialog, ID_PLAYERS);
	memcpy(slots, lagging, sizeof(int) * (size_t)n);
	memcpy(ui->timeout.lagging, lagging, sizeof(int) * (size_t)n);
	ui->timeout.lagging_count = n;
	shuffle(g, slots, n);
	for (; c >= 0; c = ui->controls[c].next, k++) {
		KpUiControl *row = &ui->controls[c];
		int s;
		if (k >= n) {
			kp_ui_hide_control(ui, c);
			continue;
		}
		s = slots[k];
		row->draw = kp_ui_player_item_draw;
		row->text = ui->game->w->replay && ui->slot_names[s][0] ? ui->slot_names[s] : kp_ui_record_name(ui, s);
		row->user = (uint32_t)(kp_setup_team_game(&g->setup) && g->setup.force[s] >= 1 && g->setup.force[s] <= 4
			? g->setup.leader[g->setup.force[s]] : s);
		kp_ui_show_control(ui, c);
		kp_ui_mark_dirty(ui, c);
	}
}

/* TickTimeoutCountdown 0x47ab00's text and InitTimeoutDialog 0x47a960's: "%d:%02d" while the countdown runs, then
 * Drop Players enabled. */
static void countdown(KpUi *ui)
{
	int32_t d = ui->timeout.dialog, text = kp_ui_find_control(ui, d, ID_COUNTDOWN), drop = kp_ui_find_control(ui, d, ID_DROP);
	uint32_t s = ui->timeout.seconds;
	snprintf(ui->timeout.countdown, sizeof ui->timeout.countdown, "%u:%02u", s / 60, s % 60);
	if (text >= 0) {
		ui->controls[text].text = (const uint8_t *)ui->timeout.countdown;
		kp_ui_mark_dirty(ui, text);
		kp_ui_set_visible(ui, text, s != 0);
	}
	if (drop >= 0) kp_ui_set_enabled(ui, drop, s == 0);
}

/* The dialog's procedure 0x47aa10: code 10 keeps the dialog (TimeoutDialogStoreCase 0x47aa32), 0 InitTimeoutDialog
 * 0x47a960, 1 the destroy (0x47aa55), 2 Drop Players (TimeoutDialogActivateCase 0x47aa68). */
static int timeout_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	int32_t self = (int32_t)(d - ui->controls);
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_POST_LOAD: ui->timeout.dialog = self; break;
		case KP_UI_CREATE: {
			int32_t drop = kp_ui_find_control(ui, self, ID_DROP);
			int slots[SLOTS];
			uint32_t seconds = 0;
			int n = ui->net->game_lagging(ui->net->host, slots, &seconds);
			kp_ui_tile_frame(ui, self);
			kp_ui_border_colour(ui, self);
			d->flags |= FLAG_TILED_FRAME;
			refresh_players(ui, slots, n < 0 ? 0 : n);
			if (drop >= 0) ui->controls[drop].interact = kp_ui_menu_button_proc;
			countdown(ui);
			break;
		}
		case KP_UI_DESTROY:
			kp_ui_close_modeless(ui, 0);
			ui->timeout.dialog = -1;
			(void)kp_ui_modeless_default(ui, d, e);
			return 1;
		case KP_UI_CLOSE:
			if (ui->result == ID_DROP && ui->net->game_drop != NULL) ui->net->game_drop(ui->net->host);
			return 1;
		default: break;
		}
	}
	return kp_ui_modeless_default(ui, d, e);
}

void kp_ui_net_wait(KpUi *ui)
{
	const KpUiNet *net = ui->net;
	int slots[SLOTS], n;
	uint32_t seconds = 0;
	if (!ui->hud.live || net == NULL || net->game_lagging == NULL) return;
	n = net->game_lagging(net->host, slots, &seconds);
	if (n < 0) { /* the turn came: the dialog goes (WaitForLaggingPlayers' end) */
		if (ui->timeout.dialog >= 0) kp_ui_destroy_dialog(ui, ui->timeout.dialog);
		ui->timeout.dialog = -1;
		return;
	}
	if (ui->timeout.dialog < 0) { /* OpenTimeoutDialog 0x47aaa0: the menus' depth back to 0 */
		ui->timeout.seconds = seconds;
		(void)kp_ui_open_modeless(ui, "rez\\timeout.bin", timeout_proc);
		ui->menu_depth = 0;
		return;
	}
	/* the rows and the countdown drawn again only when they change (the countdown ticks once a second) */
	if (n != ui->timeout.lagging_count || memcmp(slots, ui->timeout.lagging, sizeof(int) * (size_t)n) != 0)
		refresh_players(ui, slots, n);
	if (seconds != ui->timeout.seconds) {
		ui->timeout.seconds = seconds;
		countdown(ui);
	}
}
