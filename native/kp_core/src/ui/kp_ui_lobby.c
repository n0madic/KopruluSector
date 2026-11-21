/* The chat room, glue state 3 (gluchat.cpp, gluchatslot.cpp; atlas P17 0x4505f0..0x4510b0, P18 0x451140..0x453cc0,
 * E01 0x451680, 0x4517a0, 0x453770): rez\gluChat.bin over the network glue's lobby (KpUiNet's lobby hooks), its twelve
 * slot rows with their type and race drop-downs, the force headers, latency bars and download percents, the chat list
 * and line, the game's labels, the map preview and the start countdown. */
#include "kp_text.h"
#include "kp_ui.h"
#include "kp_ui_text.h"

#include <stdio.h>
#include <string.h>

#define STATE_CHAT 3
#define STATE_BATTLE_NET 4
#define STATE_JOIN 10
#define STATE_MODEM 20
#define STATE_MAIN 0
#define THEME_NONE 11
#define PROVIDER_BNET 0x424e4554u
#define PROVIDER_MODEM 0x4d4f444du
#define PROVIDER_MODEM_X 0x4d444d58u

#define ID_PANEL_OK 5
#define ID_PREVIEW 6
#define ID_START 7
#define ID_CANCEL 8
#define ID_SEND 9
#define ID_EDIT 10
#define ID_LIST 11
#define ID_PASSWORD_LABEL 12
#define ID_TYPE_LABEL 13
#define ID_TEMPLATE_LABEL 14
#define ID_MAP_LABEL 15
#define ID_PASSWORD 16
#define ID_GAME_NAME 17
#define ID_GAME_TYPE 18
#define ID_SUBTYPE 19
#define ID_MAP_NAME 20
#define ID_SIZE 21
#define ID_SPEED 22
#define ID_COUNT_LABEL 23
#define ID_COUNT 24
#define ID_OBSERVERS 25
#define ID_OBSERVER_FIRST 26
#define ID_OBSERVER_LAST 29
#define ID_ROW_FIRST 30
#define PROCS 84                /* the table 0x503a00's 0x150 bytes */
#define TYPE_STATIC 9
#define TYPE_COMBO 13
#define TYPE_BUTTON 14

#define R_START 0x22c           /* the game starts (PollLobby's 0x53) */
#define R_LEAVE 0x22d           /* the room closes (a leave reason, the join check's failure) */
#define SLIDE_DONE 0x405
#define PREVIEW_TIMER 1         /* LobbyMapPreviewTimer 0x450640 */
#define PREVIEW_PERIOD 1000
#define LATENCY_TIMER 0x1c      /* LobbyLatencyTimer 0x452a00 */
#define STATS_TIMER 0x1d        /* LobbyStatsTimer 0x452ab0 */
#define ROW_TIMER_PERIOD 400
#define COUNTDOWN_PERIOD 1000   /* StartLobbyCountdown 0x47a160's SetTimer */
#define COUNTDOWN_START 5
#define COUNTDOWN_SOUND 0xaa
#define CHAT_GRP "glue\\chatroom\\iChat.grp"
#define LATENCY_STEP 200        /* a bar per 200 ms */
#define LATENCY_BARS 6
#define FRAME_LATENCY 5         /* 5 up to two bars, 6 three, 7 four or more */
#define FRAME_HEADER 0x35
#define FRAME_HEADER_LIT 0x38
#define HEADER_W 0xb8
#define HEADER_H 0x12
#define HEADER_TEXT_X 4
#define TYPE_MENU_W 0xa4
#define RACE_MENU_W 0x64
#define MENU_H 0x19
#define OBSERVER_W 0xa0
#define OBSERVER_H 0x10
#define WUSER_SHOWN 0x100u      /* the row needs the control (the procedures' SHOW lets it through) */
#define ROW_JOINABLE 0x4000u    /* +0xa8: an open or closed row's force has no player */
#define FLAGS_NO_EVENTS 0xdfffffefu
#define FLAG_LIT KP_UI_CONSUMED /* a header's highlight 0x80000000 */
#define FORMAT_SLOT_TEXT 0x21
#define FORMAT_HEADER 0x11
#define CHAT_KEEP 63            /* AppendLobbyChatLine's lines kept before a new one */
#define CHAT_LINE 119           /* LobbyChatEditKeyFilter's characters */
#define CHAT_INDENT 3
#define SWAP_TARGETS 7          /* the slot-type menu's swap records: values KP_UI_SLOT_SWAP + 1..7 */
#define KEY_BACKSPACE 8
#define CHAT_COLOUR_OWN 2
#define CHAT_COLOUR_PLAYER 6
#define CHAT_COLOUR_SYSTEM 4
#define LIST_COPY 0x10

/* gluAll's strings */
#define STR_SIZE 0x20           /* "%ux%u" */
#define STR_NO_OPPONENT 0x72
#define STR_LEAVE_FIRST 0x74
#define STR_CONFIRM_BOOT 0x7e   /* "Performing this action will boot:\n\t%s\nContinue?" */
#define STR_SECONDS 0x82
#define STR_COMPUTER_SWAP 0x84
#define STR_HOME_TEAM 0xac      /* + side: 0xad "Home Team", 0xae "Away Team" */
#define STR_SWITCH_WITH 0xaf
#define STR_MOVE_TO 0xb0
#define STR_REPLAY 0xb2
#define STR_SECOND 0xca
#define STR_SPEED_FIRST 85
#define SPEEDS 7
/* network.tbl's */
#define NET_RACE_FIRST 7        /* "Zerg", "Terran", "Protoss", "Random" */
#define NET_FORCE_FIRST 0x12    /* + force: "Team 1".."Team 4" */

/* the slot types (the network glue's 0x6726c8 + 8) */
#define SLOT_COMPUTER_GAME 1
#define SLOT_HUMAN 2
#define SLOT_COMPUTER 5
#define SLOT_OPEN 6
#define SLOT_CLOSED 8
#define SLOT_NONE_9 9

static KpUiChat *ch(KpUi *ui) { return &ui->menus.chat; }
static const KpUiLobby *lb(KpUi *ui) { return &ui->menus.chat.view; }

static int32_t control(KpUi *ui, int16_t id)
{
	return ch(ui)->dialog >= 0 ? kp_ui_find_control(ui, ch(ui)->dialog, id) : -1;
}

/* The row's k-th control (its label 30 + 4i, the type and race drop-downs, the latency bars). */
static int32_t row_control(KpUi *ui, int row, int k)
{
	return control(ui, (int16_t)(ID_ROW_FIRST + 4 * row + k));
}

static int ascii_casecmp(const char *a, const char *b)
{
	/* _stricmp in the C locale */
	for (;; a++, b++) {
		int x = (unsigned char)*a, y = (unsigned char)*b;
		if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
		if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
		if (x != y || x == 0) return x - y;
	}
}

/* --- the lobby's predicates (the network glue's, over the view) --- */

static int slot_force(KpUi *ui, int s)
{
	/* GetSlotForce 0x443560 */
	return s >= 0 && s < KP_UI_LOBBY_PLAYERS ? lb(ui)->slots[s].force : 0;
}

static int local_force(KpUi *ui) { return slot_force(ui, lb(ui)->local_slot); }

static uint32_t deadline(KpUi *ui) { return lb(ui)->turn + 4 * lb(ui)->transit; }

static int force_has_player(KpUi *ui, int force)
{
	/* the opposite of ForceHasNoHumanOrComputerSlot 0x4b2d50 */
	int s;
	for (s = 0; s < KP_UI_LOBBY_PLAYERS; s++) {
		const KpUiLobbySlot *r = &lb(ui)->slots[s];
		if (r->force == force && (r->type == SLOT_HUMAN || r->type == SLOT_COMPUTER)) return 1;
	}
	return 0;
}

static int force_has_computer(KpUi *ui, int force)
{
	/* ForceHasComputer 0x4b2cf0 */
	int s;
	for (s = KP_UI_LOBBY_PLAYERS - 1; s >= 0; s--) {
		const KpUiLobbySlot *r = &lb(ui)->slots[s];
		if (r->force == force && (r->type == SLOT_COMPUTER_GAME || r->type == SLOT_COMPUTER)) return 1;
	}
	return 0;
}

static int available_type(int type) { return type >= 3 && type <= 7; } /* IsAvailableSlotType 0x4795e0 */

static int user_select(KpUi *ui, int s)
{
	/* IsUserSelectSlot 0x479610 */
	return s >= 0 && s < KP_UI_LOBBY_PLAYERS ? lb(ui)->user_select[s] : 0;
}

/* --- showing the rows' controls --- */

static void show_needed(KpUi *ui, int32_t c)
{
	/* ShowSlotControlUnlessSuppressed 0x452600 */
	if (c < 0) return;
	ui->controls[c].wuser |= WUSER_SHOWN;
	if (!ch(ui)->hide_rows) kp_ui_show_control(ui, c);
}

static void hide_unneeded(KpUi *ui, int32_t c)
{
	if (c < 0) return;
	ui->controls[c].wuser &= (uint16_t)~WUSER_SHOWN;
	kp_ui_hide_control(ui, c);
}

static void disable(KpUi *ui, int32_t c)
{
	if (c >= 0) kp_ui_set_enabled(ui, c, 0);
}

/* --- the map preview (0x4505f0..0x4507b0) --- */

static void show_lobby_controls(KpUi *ui, int over_preview, int show_button)
{
	/* ShowLobbyControls 0x4507b0: the labels or the minimap while the cursor is over Map Preview */
	static const int16_t LABELS[6] = { ID_TYPE_LABEL, ID_GAME_TYPE, ID_TEMPLATE_LABEL, ID_SUBTYPE, ID_MAP_LABEL,
		ID_MAP_NAME };
	KpUiChat *c = ch(ui);
	char password[KP_UI_GAME_NAME];
	int k;
	int32_t preview_child;
	if (!c->preview_state || !c->slid_in || !c->preview_shown) return;
	password[0] = 0;
	if (ui->net->lobby_password != NULL) ui->net->lobby_password(ui->net->host, password, sizeof password);
	if (password[0] != 0) {
		kp_ui_set_visible(ui, control(ui, ID_PASSWORD_LABEL), !over_preview);
		kp_ui_set_visible(ui, control(ui, ID_PASSWORD), !over_preview);
	}
	for (k = 0; k < 6; k++) {
		int32_t l = control(ui, LABELS[k]);
		if (l >= 0) kp_ui_set_visible(ui, l, !over_preview);
	}
	if (c->preview >= 0 && (preview_child = kp_ui_find_control(ui, c->preview, 1)) >= 0)
		kp_ui_set_visible(ui, preview_child, over_preview);
	if (control(ui, ID_PREVIEW) >= 0) kp_ui_set_visible(ui, control(ui, ID_PREVIEW), show_button);
}

static void preview_tick(KpUi *ui, int32_t owner)
{
	/* LobbyMapPreviewTimer 0x450640: the preview world once the map is the lobby's and every peer has it */
	KpUiChat *c = ch(ui);
	KpUiLobby view;
	if (c->preview_state == 1) return;
	memset(&view, 0, sizeof view);
	if (ui->net->lobby_view != NULL) ui->net->lobby_view(ui->net->host, &view);
	if (!view.map_ready || view.local_slot < 0 || view.local_slot >= KP_UI_LOBBY_PLAYERS || !view.transfer_done) return;
	c->preview_state = 1;
	/* InitGameWorldForLobbyPreview 0x443850 (under SetRngEnabled 0x4781c0(1): the preview world draws nothing of the
	 * game's generator here) and InitMinimap 0x46e260 under the preview; a map that does not load was reported (0x61,
	 * fatal: the glue exits) */
	if (ui->net->lobby_preview != NULL && c->preview >= 0) {
		KpUiPreview world;
		memset(&world, 0, sizeof world);
		if (ui->net->lobby_preview(ui->net->host, &world)) (void)kp_ui_minimap_preview_world(ui, c->preview, &world);
	}
	kp_ui_delete_timer(&ui->timers, owner, PREVIEW_TIMER);
}

static void open_preview(KpUi *ui)
{
	/* OpenLobbyMapPreview 0x450760 */
	KpUiChat *c = ch(ui);
	c->slid_in = 0;
	c->preview_shown = 1;
	c->preview_state = 1;
	c->preview = kp_ui_minimap_preview_open(ui);
	c->preview_state = 0;
	(void)kp_ui_set_timer(&ui->timers, c->dialog, PREVIEW_TIMER, PREVIEW_PERIOD, kp_ui_callback(ui, preview_tick), ui->now);
}

static void close_preview(KpUi *ui)
{
	/* CloseLobbyMapPreview 0x4505f0 */
	KpUiChat *c = ch(ui);
	if (c->preview_state == 0) {
		if (c->preview >= 0) kp_ui_minimap_preview_close(ui, c->preview);
		c->preview = -1;
		return;
	}
	if (c->preview_shown == 1) {
		show_lobby_controls(ui, 0, 1);
		c->preview_shown = 0;
	}
	/* ReleaseLobbyPreviewWorld 0x443d70: the world, then the minimap (FreeMinimap 0x46e1a0) */
	if (ui->net->lobby_preview_release != NULL) ui->net->lobby_preview_release(ui->net->host);
	if (c->preview >= 0) kp_ui_minimap_preview_close(ui, c->preview);
	c->preview = -1;
	c->preview_state = 0;
}

/* --- the game's labels (FormatLobbyGameDescription 0x450ef0, RefreshLobbyPassword 0x4510b0) --- */

static void set_text(KpUi *ui, int16_t id, const uint8_t *text)
{
	/* SetGlueControlText 0x457ed0 */
	int32_t c = control(ui, id);
	if (c >= 0) kp_ui_set_control_text(ui, c, text);
}

static void describe_game(KpUi *ui)
{
	KpUiChat *c = ch(ui);
	const KpUiLobby *v = lb(ui);
	uint32_t total = (uint32_t)v->max_players + (v->save_id != 0 ? v->save_players : 0);
	snprintf((char *)c->size, sizeof c->size, (const char *)kp_ui_tbl(ui, KP_UI_GLUALL, STR_SIZE), (unsigned)v->width,
		(unsigned)v->height);
	(void)kp_ui_game_texts(ui, v->type, v->sub, v->subtype, total, v->replay, c->type_name, c->label, c->subtype);
	set_text(ui, ID_GAME_NAME, (const uint8_t *)v->game_name);
	set_text(ui, ID_MAP_NAME, (const uint8_t *)v->title);
	set_text(ui, ID_GAME_TYPE, c->type_name);
	set_text(ui, ID_TEMPLATE_LABEL, c->label);
	set_text(ui, ID_SUBTYPE, c->subtype);
	set_text(ui, ID_SIZE, c->size);
	/* GetReplayGameSpeed 0x4805c0 in a replay: the view's speed is the replay's */
	if (v->speed < SPEEDS) set_text(ui, ID_SPEED, kp_ui_tbl(ui, KP_UI_GLUALL, STR_SPEED_FIRST + v->speed));
}

static void refresh_password(KpUi *ui)
{
	KpUiChat *c = ch(ui);
	int32_t label = control(ui, ID_PASSWORD_LABEL), value = control(ui, ID_PASSWORD);
	c->password[0] = 0;
	if (ui->net->lobby_password != NULL) ui->net->lobby_password(ui->net->host, (char *)c->password, sizeof c->password);
	if (c->password[0] != 0) {
		if (value >= 0) kp_ui_set_control_text(ui, value, c->password);
		if (label >= 0) {
			ui->controls[label].wuser = 1;
			kp_ui_show_control(ui, label);
		}
		if (value >= 0) {
			ui->controls[value].wuser = 1;
			kp_ui_show_control(ui, value);
		}
	} else {
		if (label >= 0) kp_ui_hide_control(ui, label);
		if (value >= 0) kp_ui_hide_control(ui, value);
	}
}

/* --- the chat list (AppendLobbyChatLine 0x451140) --- */

static int32_t list_text_width(KpUi *ui, int32_t list)
{
	/* GetListTextWidth 0x4c6860: a hidden scroll bar's width (and 3) given back to the text */
	const KpUiControl *c = &ui->controls[list];
	int32_t w = c->right - c->left + 1;
	if (c->list != NULL && c->list->bar >= 0) {
		const KpUiControl *b = &ui->controls[c->list->bar];
		if (!(b->flags & KP_UI_VISIBLE)) return w + (b->left - b->right) - 3;
	}
	return w;
}

static void append_line(KpUi *ui, const char *text, int kind)
{
	KpUiChat *c = ch(ui);
	int32_t list = c->list;
	uint8_t colour = kind == KP_UI_CHAT_OWN ? CHAT_COLOUR_OWN : kind == KP_UI_CHAT_PLAYER ? CHAT_COLOUR_PLAYER
		: kind == KP_UI_CHAT_SYSTEM || kind == KP_UI_CHAT_PLAIN ? CHAT_COLOUR_SYSTEM : CHAT_COLOUR_OWN;
	int32_t width;
	char copy[KP_UI_CHAT_TEXT];
	char *rest;
	int at;
	if (list < 0) return;
	for (at = kp_ui_list_count(ui, list); at >= CHAT_KEEP + 1; at--) kp_ui_list_delete(ui, list, 0);
	kp_ui_select_control_font(ui, &ui->controls[list]);
	width = list_text_width(ui, list) - ui->text.max_width;
	if (kp_ui_text_width(&ui->text, (const uint8_t *)text) < width) {
		at = kp_ui_list_add(ui, list, (const uint8_t *)text, (uint8_t)(colour | LIST_COPY));
		kp_ui_list_select(ui, list, at);
		kp_ui_mark_dirty(ui, list);
		return;
	}
	/* word-wrapped: a break at the last space past the third byte, the next piece indented by three spaces */
	(void)kp_text_copy(copy, sizeof copy, text);
	rest = copy;
	for (;;) {
		size_t n = kp_ui_text_fit_length(&ui->text, (const uint8_t *)rest, width), k;
		char saved, *next;
		if (rest[n] != 0 && n > CHAT_INDENT) {
			for (k = n; k > CHAT_INDENT; k--) {
				if (rest[k] == ' ') {
					n = k;
					break;
				}
			}
		}
		saved = rest[n];
		rest[n] = 0;
		at = kp_ui_list_add(ui, list, (const uint8_t *)rest, (uint8_t)(colour | LIST_COPY));
		if (at == KP_UI_LIST_NONE) return;
		kp_ui_list_select(ui, list, at);
		if (saved == 0) break;
		rest[n] = saved;
		next = rest + n;
		if (saved == ' ') while (*next == ' ') next++;
		/* the indent overwrites the piece's last bytes: a piece of three bytes or fewer (a glyph wider than the list)
		 * would step back before the copy or stay put, so the rest is dropped */
		if (next - rest <= CHAT_INDENT) break;
		rest = next - CHAT_INDENT;
		memset(rest, ' ', CHAT_INDENT);
	}
	kp_ui_mark_dirty(ui, list);
}

/* --- the slot rows (gluchatslot.cpp) --- */

static void mark_force_headers(KpUi *ui, int kind, int force)
{
	/* MarkForceHeaders 0x4529a0 */
	KpUiChat *c = ch(ui);
	int i;
	for (i = 0; i < KP_UI_CHAT_ROWS; i++) {
		KpUiChatRow *r = &c->rows[i];
		int32_t k = r->control;
		if (r->kind != kind || k < 0) continue;
		if (r->race == force) {
			ui->controls[k].flags |= FLAG_LIT;
		} else {
			if (!r->pending_race) continue;
			ui->controls[k].flags &= ~FLAG_LIT;
		}
		kp_ui_mark_dirty(ui, k);
	}
}

static void clear_header_pending(KpUi *ui, int force)
{
	/* ClearForceHeaderPending 0x4523c0 */
	KpUiChat *c = ch(ui);
	int i;
	for (i = 0; i < KP_UI_CHAT_ROWS; i++) {
		const KpUiChatRow *r = &c->rows[i];
		if (r->header_force == force && (r->kind == KP_UI_ROW_OPEN || r->kind == KP_UI_ROW_LOCAL)) return;
	}
	for (i = 0; i < KP_UI_CHAT_ROWS; i++) {
		KpUiChatRow *r = &c->rows[i];
		if (r->kind == KP_UI_ROW_TEAM && r->race == force) {
			r->pending_race = 0;
			if (r->control >= 0) {
				ui->controls[r->control].flags &= 0x7fffffefu;
				kp_ui_mark_dirty(ui, r->control);
			}
			return;
		}
	}
}

/* DiffLobbySlotRecord 0x452440: whether the row differs from slot s (or a full refresh), its pending requests expired */
static void diff_row(KpUi *ui, KpUiChatRow *r, int kind, int s, int *pending, int *changed)
{
	KpUiChat *c = ch(ui);
	const KpUiLobbySlot *slot = &lb(ui)->slots[s];
	uint32_t turn = lb(ui)->turn;
	*changed = c->full_refresh;
	switch (kind) {
	case KP_UI_ROW_REMOTE:
	case KP_UI_ROW_LOCAL:
		if (ascii_casecmp(slot->name, r->name) != 0) *changed = 1;
		/* fall through */
	case KP_UI_ROW_COMPUTER:
	case KP_UI_ROW_OPEN:
	case KP_UI_ROW_CLOSED:
		if ((uint8_t)r->header_force != (uint8_t)local_force(ui) && r->pending_race) {
			*changed = 1;
			r->pending_race = 0;
		}
		if (ascii_casecmp(slot->name, r->name) != 0) {
			*changed = 1;
			r->pending_swap = 0;
			if (lb(ui)->top_vs_bottom) c->tvb_refresh = 1;
		}
		if (r->pending_swap && r->deadline <= turn) {
			*changed = 1;
			r->pending_swap = 0;
			if (lb(ui)->top_vs_bottom) c->tvb_refresh = 1;
		}
		if (!r->pending_kind) {
			if (r->kind != kind) *changed = 1;
		} else if (r->kind == kind || r->before != kind || r->deadline <= turn) {
			*changed = 1;
			r->pending_kind = 0;
		}
		if (!r->pending_race) {
			if (slot->race != r->race) *changed = 1;
		} else if (slot->race == r->race || slot->race != r->previous_race || r->deadline <= turn) {
			*changed = 1;
			r->pending_race = 0;
		}
		break;
	default:
		break;
	}
	*pending = r->pending_kind || r->pending_swap || r->pending_race;
}

/* The row's +0x58 against the local player's force: a change marks the row changed. */
static int sync_local_force(KpUi *ui, KpUiChatRow *r, int header_force, int changed)
{
	int same = (uint8_t)local_force(ui) == (uint8_t)header_force;
	if (r->local_force != same) {
		r->local_force = (uint8_t)same;
		return 1;
	}
	return changed;
}

static void set_row(KpUi *ui, KpUiChatRow *r, int kind, int s, int header_force, int ums_force)
{
	r->kind = (uint8_t)kind;
	r->header_force = (int8_t)header_force;
	r->local_force = (uint8_t)((uint8_t)local_force(ui) == (uint8_t)header_force);
	r->ums_force = (uint8_t)ums_force;
	r->race = lb(ui)->slots[s].race;
}

/* BuildSwapMenuLabel 0x4526d0: the swap item's text for target setup slot t on side (1 top, 2 bottom), cached */
static const uint8_t *swap_label(KpUi *ui, int t, int side)
{
	KpUiChat *c = ch(ui);
	const KpUiLobby *v = lb(ui);
	int i, cursor = 0;
	uint8_t *out;
	if (t < 0 || t >= KP_UI_LOBBY_PLAYERS) return NULL;
	out = c->swap_labels[t];
	out[0] = 0;
	for (i = 0; i < KP_UI_CHAT_ROWS && cursor < KP_UI_LOBBY_PLAYERS; i++) {
		const KpUiChatRow *r = &c->rows[i];
		if (r->kind == KP_UI_ROW_REMOTE || r->kind == KP_UI_ROW_LOCAL || r->kind == KP_UI_ROW_COMPUTER
			|| r->kind == KP_UI_ROW_OPEN || r->kind == KP_UI_ROW_CLOSED) {
			while (cursor < KP_UI_LOBBY_PLAYERS && !available_type(v->setup_types[cursor])) cursor++;
			if (cursor == t) {
				const uint8_t *format = kp_ui_tbl(ui, KP_UI_GLUALL,
					r->kind == KP_UI_ROW_OPEN ? STR_MOVE_TO : STR_SWITCH_WITH);
				const char *arg = r->kind == KP_UI_ROW_OPEN ? (const char *)kp_ui_tbl(ui, KP_UI_GLUALL, STR_HOME_TEAM + side)
					: r->kind == KP_UI_ROW_COMPUTER ? (const char *)kp_ui_tbl(ui, KP_UI_GLUALL, STR_COMPUTER_SWAP) : r->name;
				if (r->kind == KP_UI_ROW_CLOSED) {
					cursor++;
					continue;
				}
				if (format == NULL) return NULL;
				snprintf((char *)out, KP_UI_CHAT_SWAP_LABEL, (const char *)format, arg != NULL ? arg : "");
				return out;
			}
			cursor++;
		}
	}
	return NULL;
}

/* The slot-type menu's records (0x503b68): action, value, then the six conditions f1..f6 */
static const uint16_t TYPE_MENU[13][8] = {
	{ KP_UI_SLOT_NAME, 0, 1, 1, 1, 1, 1, 1 },
	{ KP_UI_SLOT_PUBLIC, 198, 1, 0, 0, 0, 0, 0 },
	{ KP_UI_SLOT_SWAP, 1, 1, 1, 1, 1, 1, 1 }, { KP_UI_SLOT_SWAP, 2, 1, 1, 1, 1, 1, 1 },
	{ KP_UI_SLOT_SWAP, 3, 1, 1, 1, 1, 1, 1 }, { KP_UI_SLOT_SWAP, 4, 1, 1, 1, 1, 1, 1 },
	{ KP_UI_SLOT_SWAP, 5, 1, 1, 1, 1, 1, 1 }, { KP_UI_SLOT_SWAP, 6, 1, 1, 1, 1, 1, 1 },
	{ KP_UI_SLOT_SWAP, 7, 1, 1, 1, 1, 1, 1 },
	{ KP_UI_SLOT_OPEN, 133, 0, 1, 0, 0, 1, 1 },
	{ KP_UI_SLOT_CLOSE, 134, 0, 1, 0, 0, 1, 1 },
	{ KP_UI_SLOT_COMPUTER, 132, 0, 1, 0, 0, 0, 1 },
	{ KP_UI_SLOT_BAN, 135, 0, 1, 1, 1, 1, 1 },
};

static int type_menu_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e);
static int race_menu_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e);

/* The drop-down's preparation (BuildSlotTypeMenu and BuildLobbyRaceMenu's head): needed, enabled, closed, emptied */
static void menu_begin(KpUi *ui, int32_t menu)
{
	show_needed(ui, menu);
	kp_ui_set_enabled(ui, menu, 1);
	kp_ui_mark_dirty(ui, menu);
	(void)kp_ui_send_user(ui, menu, KP_UI_FOCUS_LOST, 0);
	ui->controls[menu].interact = kp_ui_combo_proc;
	kp_ui_list_redraw(ui, menu, 0);
	kp_ui_list_clear(ui, menu);
}

static void build_type_menu(KpUi *ui, int32_t menu, const char *name, int is_local, int restrict_, int force,
	int current)
{
	/* BuildSlotTypeMenu 0x4532b0 */
	const KpUiLobby *v = lb(ui);
	const KpUiChatRow *row;
	int8_t avail[KP_UI_LOBBY_PLAYERS], side[KP_UI_LOBBY_PLAYERS];
	int first[3] = { -1, -1, -1 };
	int swap_mode, i, k, cursor, swap_done = 0, first_open = 0, selection = KP_UI_LIST_NONE;
	if (menu < 0) return;
	row = &ch(ui)->rows[ui->controls[menu].user];
	menu_begin(ui, menu);
	swap_mode = v->top_vs_bottom && !v->save_open;
	if (swap_mode && !v->replay) {
		memset(avail, -1, sizeof avail);
		for (i = 0, k = 0; i < KP_UI_LOBBY_PLAYERS; i++)
			if (available_type(v->setup_types[i])) avail[k++] = (int8_t)i;
		for (i = 0; i < KP_UI_LOBBY_PLAYERS; i++) side[i] = (int8_t)(i < v->top_size ? 1 : 2);
		for (i = 0, cursor = 0; i < KP_UI_CHAT_ROWS && cursor != KP_UI_LOBBY_PLAYERS; i++) {
			switch (ch(ui)->rows[i].kind) {
			case KP_UI_ROW_REMOTE:
			case KP_UI_ROW_LOCAL:
			case KP_UI_ROW_COMPUTER:
			case KP_UI_ROW_CLOSED:
				do cursor++;
				while (cursor < KP_UI_LOBBY_PLAYERS && !available_type(v->setup_types[cursor]));
				break;
			case KP_UI_ROW_OPEN:
				while (cursor < KP_UI_LOBBY_PLAYERS && !available_type(v->setup_types[cursor])) cursor++;
				if (cursor < KP_UI_LOBBY_PLAYERS && first[side[cursor]] < 0) first[side[cursor]] = cursor;
				cursor++;
				break;
			default:
				break;
			}
		}
	}
	for (k = 0; k < 13; k++) {
		const uint16_t *m = TYPE_MENU[k];
		const uint8_t *text;
		int action = m[0], at, t = -1;
		uint32_t data = (uint32_t)action;
		if (action == KP_UI_SLOT_SWAP) {
			int s = row->slot;
			if (v->replay || !swap_mode || s < 0 || s >= KP_UI_LOBBY_PLAYERS) continue;
			if (v->slots[s].storm == 0 || row->kind != KP_UI_ROW_REMOTE || (t = avail[m[1]]) < 0) continue;
			if (side[s] == side[t] || swap_done) continue;
			first_open = first[side[t]] >= 0;
			if (first_open && t != first[side[t]]) continue;
		}
		if ((m[2] == 0 && is_local) || (m[3] == 0 && !is_local) || (m[4] != 0 && name == NULL)
			|| (restrict_ && m[5] == 0)) {
			continue;
		}
		if (action == KP_UI_SLOT_COMPUTER && (!v->computers || v->replay || (v->ums && current != KP_UI_SLOT_COMPUTER)))
			continue;
		if (v->team && m[6] == 0 && force_has_player(ui, force) && !force_has_computer(ui, force)) continue;
		if (m[7] == 0 && v->replay) continue;
		text = action == KP_UI_SLOT_NAME ? (const uint8_t *)name
			: action == KP_UI_SLOT_SWAP ? swap_label(ui, t, side[t]) : kp_ui_tbl(ui, KP_UI_GLUALL, m[1]);
		if (text == NULL) continue;
		at = kp_ui_list_add(ui, menu, text, 0);
		if (at == KP_UI_LIST_NONE) break;
		if (action == KP_UI_SLOT_SWAP) data = (uint32_t)(KP_UI_SLOT_SWAP + t);
		kp_ui_list_set_data(ui, menu, at, data);
		if (action == current) selection = at;
		if (first_open) swap_done = 1;
	}
	kp_ui_list_redraw(ui, menu, 1);
	kp_ui_list_select(ui, menu, selection);
	ui->controls[menu].interact = type_menu_proc;
	if (!v->host || kp_ui_list_count(ui, menu) == 0) disable(ui, menu);
}

static void build_race_menu(KpUi *ui, int32_t menu, int race)
{
	/* BuildLobbyRaceMenu 0x4538d0 over 0x503d0c */
	static const uint8_t RACES[4] = { 0, 1, 2, 6 };
	int k, selection = 0;
	if (menu < 0) return;
	menu_begin(ui, menu);
	for (k = 0; k < 4; k++) {
		int at = kp_ui_list_add(ui, menu, kp_ui_tbl(ui, KP_UI_NETWORK, (uint32_t)(NET_RACE_FIRST + k)), 0);
		if (at == KP_UI_LIST_NONE) break;
		kp_ui_list_set_data(ui, menu, at, RACES[k]);
		if (RACES[k] == race) selection = at;
	}
	kp_ui_list_redraw(ui, menu, 1);
	kp_ui_list_select(ui, menu, selection);
	ui->controls[menu].interact = race_menu_proc;
}

static void update_computer_row(KpUi *ui, int i, int s, int header_force, int ums_force)
{
	/* UpdateComputerSlotRow 0x452be0 */
	KpUiChatRow *r = &ch(ui)->rows[i];
	const KpUiLobby *v = lb(ui);
	int pending, changed;
	int32_t type_menu = row_control(ui, i, 1), race_menu = row_control(ui, i, 2);
	diff_row(ui, r, KP_UI_ROW_COMPUTER, s, &pending, &changed);
	changed = sync_local_force(ui, r, header_force, changed);
	if (!pending) {
		set_row(ui, r, KP_UI_ROW_COMPUTER, s, header_force, ums_force);
		r->name[0] = 0;
	}
	hide_unneeded(ui, row_control(ui, i, 0));
	if (!changed && !pending) return;
	build_type_menu(ui, type_menu, NULL, 0, 0, slot_force(ui, s), KP_UI_SLOT_COMPUTER);
	if (!v->host || v->ums || pending) disable(ui, type_menu);
	build_race_menu(ui, race_menu, r->race);
	if (!v->host || v->ums || pending) disable(ui, race_menu);
	if (v->replay) hide_unneeded(ui, race_menu);
	hide_unneeded(ui, row_control(ui, i, 3));
}

static void update_open_closed_row(KpUi *ui, int i, int s, int header_force, int ums_force, int open, int no_player)
{
	/* UpdateOpenClosedSlotRow 0x452d60 */
	KpUiChatRow *r = &ch(ui)->rows[i];
	const KpUiLobby *v = lb(ui);
	int kind = open ? KP_UI_ROW_OPEN : KP_UI_ROW_CLOSED, pending, changed;
	int32_t type_menu = row_control(ui, i, 1), race_menu = row_control(ui, i, 2);
	diff_row(ui, r, kind, s, &pending, &changed);
	changed = sync_local_force(ui, r, header_force, changed);
	if (!pending) {
		set_row(ui, r, kind, s, header_force, ums_force);
		r->name[0] = 0;
	}
	if (no_player != ((r->bits & ROW_JOINABLE) != 0)) {
		r->bits ^= ROW_JOINABLE;
		changed = 1;
	}
	hide_unneeded(ui, row_control(ui, i, 0));
	if (!open && v->team) clear_header_pending(ui, header_force);
	if (!changed && !pending) return;
	build_type_menu(ui, type_menu, NULL, 0, 0, slot_force(ui, s), open ? KP_UI_SLOT_OPEN : KP_UI_SLOT_CLOSE);
	if (!v->host || v->slots[s].type == SLOT_NONE_9 || pending) disable(ui, type_menu);
	if (!v->team || no_player) {
		disable(ui, race_menu);
		hide_unneeded(ui, race_menu);
	} else {
		build_race_menu(ui, race_menu, r->race);
		if (slot_force(ui, s) != local_force(ui) || pending) disable(ui, race_menu);
	}
	if (v->replay) hide_unneeded(ui, race_menu);
	hide_unneeded(ui, row_control(ui, i, 3));
}

static void update_human_row(KpUi *ui, int i, int s, int header_force, int ums_force)
{
	/* UpdateHumanSlotRow 0x452f70 */
	KpUiChat *c = ch(ui);
	KpUiChatRow *r = &c->rows[i];
	const KpUiLobby *v = lb(ui);
	int is_local = s == v->local_slot;
	int kind = is_local ? KP_UI_ROW_LOCAL : KP_UI_ROW_REMOTE, pending, changed;
	int32_t type_menu = row_control(ui, i, 1), race_menu = row_control(ui, i, 2);
	diff_row(ui, r, kind, s, &pending, &changed);
	changed = sync_local_force(ui, r, header_force, changed);
	if (!pending) set_row(ui, r, kind, s, header_force, ums_force);
	show_needed(ui, row_control(ui, i, 0));
	if (v->team) clear_header_pending(ui, header_force);
	if (changed || pending) {
		int locked_race = v->ums && !user_select(ui, s);
		(void)kp_text_copy(r->name, sizeof r->name, v->slots[s].name);
		build_type_menu(ui, type_menu, r->name, is_local, 0, slot_force(ui, s), KP_UI_SLOT_NAME);
		if (pending) disable(ui, type_menu);
		build_race_menu(ui, race_menu, r->race);
		if (locked_race || pending
			|| !(r->kind == KP_UI_ROW_LOCAL || (v->team && slot_force(ui, s) == local_force(ui)))) {
			disable(ui, race_menu);
		}
		if (v->replay) hide_unneeded(ui, race_menu);
	}
	if (v->ums && ums_force == c->local_force && c->header_deadline <= v->turn)
		mark_force_headers(ui, KP_UI_ROW_FORCE, ums_force);
	if (v->team && is_local && c->header_deadline <= v->turn) mark_force_headers(ui, KP_UI_ROW_TEAM, slot_force(ui, s));
}

static int header_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e);
static void header_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4]);

static void setup_header_control(KpUi *ui, int32_t k, const uint8_t *text)
{
	/* SetupForceHeaderControl 0x453a50 */
	KpUiChat *c = ch(ui);
	KpUiControl *h;
	if (k < 0) return;
	h = &ui->controls[k];
	h->wuser |= WUSER_SHOWN;
	if (!c->hide_rows) kp_ui_show_control(ui, k);
	if (!c->locked) kp_ui_set_enabled(ui, k, 1);
	h->right = (int16_t)(h->left + HEADER_W);
	h->bottom = (int16_t)(h->top + HEADER_H);
	if (c->header_deadline <= lb(ui)->turn) h->flags &= 0x7ffffffeu;
	h->flags |= 0x20000000u | KP_UI_EVENTS;
	h->interact = header_proc;
	h->draw = header_draw;
	kp_ui_set_control_text(ui, k, text);
	kp_ui_mark_dirty(ui, k);
}

static void setup_header_row(KpUi *ui, int i, int force, int computer_led)
{
	/* SetupForceHeaderRow 0x4531c0 */
	KpUiChatRow *r = &ch(ui)->rows[i];
	const KpUiLobby *v = lb(ui);
	int32_t k = row_control(ui, i, 0);
	const uint8_t *name = NULL;
	r->kind = KP_UI_ROW_TEAM;
	r->race = (uint8_t)force;
	if (v->team) name = kp_ui_tbl(ui, KP_UI_NETWORK, (uint32_t)(NET_FORCE_FIRST + force));
	if (v->top_vs_bottom) name = kp_ui_tbl(ui, KP_UI_GLUALL, (uint32_t)(STR_HOME_TEAM + force));
	if (name != NULL) (void)kp_text_copy(r->name, sizeof r->name, (const char *)name);
	setup_header_control(ui, k, (const uint8_t *)r->name);
	if (!computer_led) {
		r->pending_race = 1;
	} else {
		r->pending_race = 0;
		if (!v->top_vs_bottom && k >= 0) ui->controls[k].flags &= FLAGS_NO_EVENTS;
	}
	hide_unneeded(ui, row_control(ui, i, 1));
	hide_unneeded(ui, row_control(ui, i, 2));
}

/* The rows a layout left: unused, hidden */
static void clear_rows(KpUi *ui, int from)
{
	KpUiChat *c = ch(ui);
	int i, k;
	for (i = from; i < KP_UI_CHAT_ROWS; i++) {
		KpUiChatRow *r = &c->rows[i];
		r->kind = KP_UI_ROW_NONE;
		for (k = 0; k < 4; k++) hide_unneeded(ui, row_control(ui, i, k));
		r->storm = lb(ui)->slots[0].storm;
		r->slot = 0;
	}
}

static void layout_rows(KpUi *ui, int budget)
{
	/* LayoutLobbySlots 0x451b50 */
	const KpUiLobby *v = lb(ui);
	KpUiLobby *live = &ch(ui)->view;
	int s, i = 0, header = 0, joinable = 0;
	for (s = 0; s < KP_UI_LOBBY_PLAYERS && i < KP_UI_CHAT_ROWS; s++) {
		const KpUiLobbySlot *slot = &v->slots[s];
		int row = 1;
		if (v->replay) {
			if (slot->type == SLOT_HUMAN) {
				update_human_row(ui, i, s, 0, 0);
			} else if (slot->type == SLOT_CLOSED) {
				update_open_closed_row(ui, i, s, 0, 0, 0, joinable);
			} else {
				/* the network glue's slot becomes open, and the view the rows read with it */
				if (ui->net->lobby_open_slot != NULL) ui->net->lobby_open_slot(ui->net->host, s);
				live->slots[s].type = SLOT_OPEN;
				update_open_closed_row(ui, i, s, 0, 0, 1, joinable);
			}
		} else {
			int type = slot->type;
			if (slot->force != 0 && slot->force > header && (type == SLOT_COMPUTER_GAME || type == SLOT_HUMAN
				|| type == SLOT_COMPUTER || type == SLOT_OPEN || type == SLOT_CLOSED || type == SLOT_NONE_9)) {
				setup_header_row(ui, i, slot->force, type == SLOT_COMPUTER_GAME || type == SLOT_COMPUTER);
				header = slot->force;
				if (v->team) joinable = !force_has_player(ui, slot->force);
				ch(ui)->rows[i].slot = 8;
				ch(ui)->rows[i].storm = -1;
				if (++i >= KP_UI_CHAT_ROWS) break;
			}
			switch (type) {
			case SLOT_HUMAN: update_human_row(ui, i, s, header, 0); break;
			case SLOT_COMPUTER:
				if (v->computers) update_computer_row(ui, i, s, slot->force, 0);
				else row = 0;
				break;
			case SLOT_OPEN: update_open_closed_row(ui, i, s, header, 0, 1, joinable); break;
			case SLOT_CLOSED: update_open_closed_row(ui, i, s, header, 0, 0, joinable); break;
			default: row = 0; break;
			}
		}
		if (!row) continue;
		ch(ui)->rows[i].slot = s;
		ch(ui)->rows[i].storm = slot->storm;
		i++;
		if (--budget == 0) break;
	}
	clear_rows(ui, i);
}

static void layout_rows_by_force(KpUi *ui, int budget)
{
	/* LayoutLobbySlotsByForce 0x451e40 */
	KpUiChat *c = ch(ui);
	const KpUiLobby *v = lb(ui);
	int f, s, i = 0;
	c->local_force = local_force(ui);
	for (f = 1; f <= 4; f++) {
		int titled = 0;
		for (s = 0; s < KP_UI_LOBBY_PLAYERS; s++) {
			const KpUiLobbySlot *slot = &v->slots[s];
			int type = slot->type;
			if (slot->force != f) continue;
			if (!titled && (type == SLOT_HUMAN || type == SLOT_COMPUTER || type == SLOT_OPEN || type == SLOT_CLOSED
				|| type == SLOT_NONE_9)) {
				KpUiChatRow *r;
				if (i >= KP_UI_CHAT_ROWS) goto done;
				r = &c->rows[i];
				titled = 1;
				(void)kp_text_copy(r->name, sizeof r->name, v->force_names[f - 1]);
				r->race = (uint8_t)f;
				r->kind = KP_UI_ROW_FORCE;
				setup_header_control(ui, row_control(ui, i, 0), (const uint8_t *)r->name);
				hide_unneeded(ui, row_control(ui, i, 1));
				hide_unneeded(ui, row_control(ui, i, 2));
				r->slot = s;
				r->storm = slot->storm;
				i++;
			}
			if (i >= KP_UI_CHAT_ROWS) goto done;
			switch (type) {
			case SLOT_HUMAN:
			case SLOT_COMPUTER:
			case SLOT_OPEN:
				if (budget == 0) goto done;
				if (type == SLOT_HUMAN) update_human_row(ui, i, s, 0, f);
				else if (type == SLOT_COMPUTER) update_computer_row(ui, i, s, 0, f);
				else update_open_closed_row(ui, i, s, 0, f, 1, 0);
				budget--;
				break;
			case SLOT_CLOSED: update_open_closed_row(ui, i, s, 0, f, 0, 0); break;
			default: continue;
			}
			c->rows[i].slot = s;
			c->rows[i].storm = slot->storm;
			i++;
		}
	}
done:
	clear_rows(ui, i);
}

static void update_percents(KpUi *ui)
{
	/* UpdateDownloadPercentLabels 0x453cc0 */
	KpUiChat *c = ch(ui);
	int i;
	for (i = 0; i < KP_UI_CHAT_ROWS; i++) {
		KpUiChatRow *r = &c->rows[i];
		int32_t k = row_control(ui, i, 0);
		uint8_t p;
		if ((r->kind != KP_UI_ROW_REMOTE && r->kind != KP_UI_ROW_LOCAL) || k < 0) continue;
		p = r->storm >= 0 && r->storm < KP_UI_LOBBY_PLAYERS ? c->percents[r->storm] : 0;
		if (p != 0 && p != 100) {
			show_needed(ui, k);
			if (r->percent != p || ui->controls[k].text != r->percent_text) {
				r->percent = p;
				snprintf((char *)r->percent_text, sizeof r->percent_text, "%3u", (unsigned)p);
				kp_ui_set_control_text(ui, k, r->percent_text);
			}
		} else { /* no ladder stats (LobbyStatsTimer's Battle.net "/stats") */
			hide_unneeded(ui, k);
		}
	}
}

/* ResetComboBox 0x4c5eb0 of the open drop-down (0x6cf7a8) among the rows of slot a or b (-1: every row's) */
static void close_drop_downs(KpUi *ui, int a, int b)
{
	KpUiChat *c = ch(ui);
	int i, k;
	for (i = 0; i < KP_UI_CHAT_ROWS; i++) {
		if (a >= 0 && c->rows[i].slot != a && c->rows[i].slot != b) continue;
		for (k = 1; k <= 2; k++) {
			int32_t m = row_control(ui, i, k);
			if (m >= 0 && (ui->controls[m].flags & KP_UI_DROPPED)) (void)kp_ui_send_user(ui, m, KP_UI_FOCUS_LOST, 0);
		}
	}
}

static void lock_rows(KpUi *ui)
{
	/* LockLobbySlotControls 0x451ad0: the open drop-down closed, the rows disabled, the row timers gone */
	KpUiChat *c = ch(ui);
	int i, k;
	close_drop_downs(ui, -1, -1);
	for (i = 0; i < KP_UI_CHAT_ROWS; i++)
		for (k = 0; k < 4; k++) disable(ui, row_control(ui, i, k));
	kp_ui_delete_timer(&ui->timers, c->dialog, LATENCY_TIMER);
	kp_ui_delete_timer(&ui->timers, c->dialog, STATS_TIMER);
	c->locked = 1;
}

static void refresh_rows(KpUi *ui)
{
	/* RefreshLobbySlots 0x451da0 */
	KpUiChat *c = ch(ui);
	const KpUiLobby *v;
	int budget;
	if (ui->net->lobby_view != NULL) ui->net->lobby_view(ui->net->host, &c->view);
	v = lb(ui);
	budget = v->save_open || v->replay ? 8 : v->max_players;
	if (v->ums && !v->replay) layout_rows_by_force(ui, 8);
	else layout_rows(ui, budget);
	if (c->tvb_refresh) {
		c->tvb_refresh = 0;
		c->full_refresh = 1;
		layout_rows(ui, budget);
	}
	update_percents(ui);
	if (v->save_open || c->locked) lock_rows(ui);
	c->full_refresh = 0;
}

/* --- the rows' timers and procedures --- */

static void latency_tick(KpUi *ui, int32_t owner)
{
	/* LobbyLatencyTimer 0x452a00: a remote human's latency from Storm's caps */
	KpUiChat *c = ch(ui);
	int i;
	(void)owner;
	for (i = 0; i < KP_UI_CHAT_ROWS; i++) {
		KpUiChatRow *r = &c->rows[i];
		int32_t bars = row_control(ui, i, 3);
		uint32_t ms = 0;
		if (bars < 0) continue;
		if (r->kind != KP_UI_ROW_REMOTE) {
			hide_unneeded(ui, bars);
			continue;
		}
		if (ui->net->lobby_latency == NULL || !ui->net->lobby_latency(ui->net->host, r->storm, &ms)) continue;
		r->latency = ms;
		if (ms != 0) show_needed(ui, bars);
		else hide_unneeded(ui, bars);
		kp_ui_mark_dirty(ui, bars);
	}
}

static void stats_tick(KpUi *ui, int32_t owner)
{
	/* LobbyStatsTimer 0x452ab0: Battle.net's ladder "/stats" (excluded, outside phase N); nothing without it */
	(void)ui;
	(void)owner;
}

static void latency_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawSlotLatencyBars 0x452620 */
	const KpUiChatRow *r = &ch(ui)->rows[c->user];
	const KpUiFile *f = kp_ui_file(ui->grps, ui->grp_count, CHAT_GRP);
	uint32_t n = r->latency / LATENCY_STEP, k;
	int32_t left = c->left;
	(void)x;
	(void)y;
	(void)clip;
	if (r->latency == 0 || f == NULL) return;
	if (n > LATENCY_BARS) n = LATENCY_BARS;
	for (k = 0; k < n; k++) {
		const uint8_t *px;
		int32_t w, h;
		int16_t rect[4];
		if (kp_ui_dialog_grp_frame(f->data, f->size, FRAME_LATENCY + (n <= 2 ? 0 : n == 3 ? 1 : 2), &px, &w, &h) != 0)
			return;
		rect[0] = (int16_t)left;
		rect[1] = c->top;
		rect[2] = c->right;
		rect[3] = c->bottom;
		kp_ui_blit_clipped(ui, px, w, h, 0, 0, rect, KP_UI_TRANSPARENT);
		left += w;
	}
}

static void slot_text_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawSlotNameText 0x453ae0: a human row's download percent */
	const KpUiChatRow *r = &ch(ui)->rows[c->user];
	(void)x;
	(void)y;
	(void)clip;
	if ((r->kind == KP_UI_ROW_REMOTE || r->kind == KP_UI_ROW_LOCAL) && c->text != NULL)
		kp_ui_draw_control_text(ui, c, 0, 0, NULL, FORMAT_SLOT_TEXT);
}

static void header_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawForceHeader 0x453b50 */
	int frame = (c->flags & (FLAG_LIT | KP_UI_HOVER)) ? FRAME_HEADER_LIT : FRAME_HEADER;
	(void)x;
	(void)y;
	if (!(c->flags & KP_UI_DISABLED))
		kp_ui_three_part_bar(ui, c->left, c->top, c->right - c->left + 1, ui->theme.row_w, frame, clip, KP_UI_TRANSPARENT);
	if (c->text != NULL) kp_ui_draw_control_text(ui, c, HEADER_TEXT_X, 0, NULL, FORMAT_HEADER);
}

static int needed_show(const KpUiControl *c, const KpUiEvent *e)
{
	/* a SHOW the row does not need: answered, the control stays hidden */
	return e->type == KP_UI_USER && e->code == KP_UI_SHOW && !(c->wuser & WUSER_SHOWN);
}

static int slot_row_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* SlotRowInteract 0x453280 */
	if (e->type != KP_UI_USER) return kp_ui_type_interact(TYPE_STATIC)(ui, c, e);
	if (needed_show(c, e)) return 1;
	return kp_ui_type_interact(-1)(ui, c, e);
}

static int header_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* ForceHeaderInteract 0x453bc0: a click on a header asks to join its force */
	KpUiChat *ch_ = ch(ui);
	KpUiChatRow *r = &ch_->rows[c->user];
	if (e->type == KP_UI_LEFT_DOWN) {
		if (r->kind == KP_UI_ROW_TEAM) {
			if (lb(ui)->team) {
				if (ui->net->lobby_force != NULL) ui->net->lobby_force(ui->net->host, KP_UI_ROW_TEAM, r->race);
				mark_force_headers(ui, KP_UI_ROW_TEAM, r->race);
				ch_->header_deadline = deadline(ui);
			}
		} else if (r->kind == KP_UI_ROW_FORCE) {
			if (ui->net->lobby_force != NULL) ui->net->lobby_force(ui->net->host, KP_UI_ROW_FORCE, r->race);
			mark_force_headers(ui, KP_UI_ROW_FORCE, r->race);
			ch_->header_deadline = deadline(ui);
		}
		return 1;
	}
	if (e->type != KP_UI_USER) return 0;
	if (needed_show(c, e)) return 1;
	return kp_ui_type_interact(-1)(ui, c, e);
}

/* MarkPendingSlotTypeChange 0x453c80 / MarkPendingSlotSwap 0x453b10 */
static void mark_pending_kind(KpUi *ui, KpUiChatRow *r, int kind)
{
	r->before = r->kind;
	r->pending_kind = 1;
	r->kind = (uint8_t)kind;
	r->deadline = deadline(ui);
	if (lb(ui)->top_vs_bottom) ch(ui)->tvb_refresh = 1;
}

static void mark_pending_swap(KpUi *ui, KpUiChatRow *r)
{
	r->pending_swap = 1;
	r->deadline = deadline(ui);
	if (lb(ui)->top_vs_bottom) ch(ui)->tvb_refresh = 1;
}

/* The kind a slot action's row shows at once (OnSlotTypeMenuSelect's cases) */
static int action_kind(int action)
{
	switch (action) {
	case KP_UI_SLOT_OPEN: return KP_UI_ROW_OPEN;
	case KP_UI_SLOT_CLOSE: return KP_UI_ROW_CLOSED;
	case KP_UI_SLOT_BAN: return KP_UI_ROW_OPEN;
	case KP_UI_SLOT_COMPUTER: return KP_UI_ROW_COMPUTER;
	default: return KP_UI_ROW_NONE;
	}
}

static int slot_request(KpUi *ui, int row, int action, int target)
{
	/* The request of OnSlotTypeMenuSelect 0x4537c0's case, then its pending mark */
	KpUiChatRow *r = &ch(ui)->rows[row];
	if (ui->net->lobby_slot == NULL || !ui->net->lobby_slot(ui->net->host, action, r->slot, target)) return 0;
	if (action == KP_UI_SLOT_SWAP) mark_pending_swap(ui, r);
	else if (action_kind(action) != KP_UI_ROW_NONE) mark_pending_kind(ui, r, action_kind(action));
	return 1;
}

static void confirm_done(KpUi *ui, int32_t result)
{
	/* ConfirmLobbySlotAction 0x451640's OK: the action goes on (the popup's run is modal in the original) */
	KpUiChat *c = ch(ui);
	int row = c->confirm_row, action = c->confirm_action;
	c->confirm_row = -1;
	if (result != 1 || row < 0 || row >= KP_UI_CHAT_ROWS || c->dialog < 0) return;
	if (slot_request(ui, row, action, -1)) c->refresh = 1;
}

static int slot_menu_select(KpUi *ui, KpUiControl *menu, int index)
{
	/* OnSlotTypeMenuSelect 0x4537c0 */
	KpUiChat *c = ch(ui);
	int row = (int)menu->user;
	uint32_t value = kp_ui_list_data(ui, (int32_t)(menu - ui->controls), index);
	char names[KP_UI_POPUP_TEXT];
	if (value > KP_UI_SLOT_SWAP + SWAP_TARGETS) return 0;
	if (value == KP_UI_SLOT_NAME) return 1;
	if (value == KP_UI_SLOT_PUBLIC)
		return ui->net->lobby_slot != NULL && ui->net->lobby_slot(ui->net->host, KP_UI_SLOT_PUBLIC, -1, -1);
	if (value == KP_UI_SLOT_SWAP) return 0;
	if (value > KP_UI_SLOT_SWAP) return slot_request(ui, row, KP_UI_SLOT_SWAP, (int)value - KP_UI_SLOT_SWAP);
	/* Ban, Close, Computer and Open check the slot, then ask before they boot a player */
	names[0] = 0;
	switch (ui->net->lobby_slot_check != NULL
		? ui->net->lobby_slot_check(ui->net->host, (int)value, c->rows[row].slot, names, sizeof names) : 1) {
	case 0: return 0;
	case 2: {
		char text[KP_UI_POPUP_TEXT];
		const uint8_t *format = kp_ui_tbl(ui, KP_UI_GLUALL, STR_CONFIRM_BOOT);
		snprintf(text, sizeof text, format != NULL ? (const char *)format : "%s", names);
		c->confirm_row = row;
		c->confirm_action = (int)value;
		kp_ui_menus_ok_cancel_popup(ui, (const uint8_t *)text, confirm_done);
		return 0;
	}
	default: return slot_request(ui, row, (int)value, -1);
	}
}

static int type_menu_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* SlotTypeDropdownProc 0x453770 */
	if (e->type != KP_UI_USER) return kp_ui_combo_proc(ui, c, e);
	if (e->code == KP_UI_VALUE_CHANGED && (uint8_t)e->param != KP_UI_LIST_NONE) {
		if (slot_menu_select(ui, c, (int)(uint8_t)e->param)) ch(ui)->refresh = 1;
		return 1;
	}
	if (needed_show(c, e)) return 1;
	return kp_ui_combo_proc(ui, c, e);
}

static int race_menu_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* LobbyRaceMenuInteract 0x4539a0, RequestLobbyRaceChange 0x453a00 */
	int32_t self = (int32_t)(c - ui->controls);
	if (e->type != KP_UI_USER) return kp_ui_combo_proc(ui, c, e);
	if (e->code == KP_UI_VALUE_CHANGED) {
		int index = (int)(uint8_t)e->param;
		if (index != kp_ui_list_selection(ui, self) && index != KP_UI_LIST_NONE) {
			KpUiChatRow *r = &ch(ui)->rows[c->user];
			int race = (int)kp_ui_list_data(ui, self, index);
			kp_ui_set_enabled(ui, self, 0);
			if (ui->net->lobby_race != NULL && ui->net->lobby_race(ui->net->host, r->slot, race)) {
				r->previous_race = r->race;
				r->race = (uint8_t)race;
				r->pending_race = 1;
				r->deadline = deadline(ui);
				ch(ui)->refresh = 1;
			}
		}
		return 1;
	}
	if (needed_show(c, e)) return 1;
	return kp_ui_combo_proc(ui, c, e);
}

static void init_rows(KpUi *ui, int32_t dialog)
{
	/* InitLobbySlots 0x4520c0: the rows' records and controls (before the dialog's run), the row timers */
	KpUiChat *c = ch(ui);
	int i, k;
	memset(c->rows, 0, sizeof c->rows);
	memset(c->percents, 0, sizeof c->percents);
	c->header_deadline = 0;
	c->hide_rows = 1;
	c->locked = 0;
	c->dialog = dialog;
	for (i = 0; i < KP_UI_CHAT_ROWS; i++) {
		KpUiChatRow *r = &c->rows[i];
		int32_t label = row_control(ui, i, 0), menu = row_control(ui, i, 1), race = row_control(ui, i, 2),
			bars = row_control(ui, i, 3);
		r->race = 0xff;
		r->control = label;
		if (label >= 0) {
			KpUiControl *l = &ui->controls[label];
			l->user = (uint32_t)i;
			show_needed(ui, label);
			l->flags = (l->flags & FLAGS_NO_EVENTS) | 0x400u;
			l->draw = slot_text_draw;
			l->interact = slot_row_proc;
			kp_ui_mark_dirty(ui, label);
		}
		for (k = 0; k < 2; k++) {
			int32_t m = k == 0 ? menu : race;
			KpUiControl *d;
			if (m < 0) continue;
			d = &ui->controls[m];
			d->right = (int16_t)(d->left + (k == 0 ? TYPE_MENU_W : RACE_MENU_W));
			d->bottom = (int16_t)(d->top + MENU_H);
			d->flags |= KP_UI_EVENTS;
			d->user = (uint32_t)i;
			d->type = TYPE_COMBO;
			d->interact = k == 0 ? type_menu_proc : race_menu_proc;
			d->draw = kp_ui_combo_draw;
			kp_ui_mark_dirty(ui, m);
			hide_unneeded(ui, m);
		}
		if (bars >= 0) {
			KpUiControl *b = &ui->controls[bars];
			const KpUiFile *f = kp_ui_file(ui->grps, ui->grp_count, CHAT_GRP);
			int32_t w, h;
			b->user = (uint32_t)i;
			if (f != NULL && kp_ui_grp_frame_size(f->data, f->size, FRAME_LATENCY, &w, &h) == 0) {
				b->right = (int16_t)(b->left + LATENCY_BARS * w);
				b->bottom = (int16_t)(b->top + h);
			}
			b->draw = latency_draw;
			hide_unneeded(ui, bars);
		}
	}
	k = kp_ui_find_control(ui, dialog, ID_OBSERVERS);
	if (k >= 0) {
		ui->controls[k].interact = slot_row_proc;
		hide_unneeded(ui, k);
	}
	for (i = ID_OBSERVER_FIRST; i <= ID_OBSERVER_LAST; i++) {
		int32_t o = kp_ui_find_control(ui, dialog, (int16_t)i);
		KpUiControl *d;
		if (o < 0) continue;
		d = &ui->controls[o];
		d->right = (int16_t)(d->left + OBSERVER_W);
		d->bottom = (int16_t)(d->top + OBSERVER_H);
		d->wuser &= (uint16_t)~WUSER_SHOWN;
		d->interact = type_menu_proc;
		d->type = TYPE_COMBO;
		d->draw = kp_ui_combo_draw;
		kp_ui_hide_control(ui, o);
	}
	(void)kp_ui_set_timer(&ui->timers, dialog, LATENCY_TIMER, ROW_TIMER_PERIOD, kp_ui_callback(ui, latency_tick), ui->now);
	(void)kp_ui_set_timer(&ui->timers, dialog, STATS_TIMER, ROW_TIMER_PERIOD, kp_ui_callback(ui, stats_tick), ui->now);
}

static void destroy_rows(KpUi *ui)
{
	/* DestroyLobbySlots 0x4518d0 */
	KpUiChat *c = ch(ui);
	memset(c->rows, 0, sizeof c->rows);
	c->header_deadline = 0;
	memset(c->percents, 0, sizeof c->percents);
	kp_ui_delete_timer(&ui->timers, c->dialog, LATENCY_TIMER);
	kp_ui_delete_timer(&ui->timers, c->dialog, STATS_TIMER);
}

/* --- the screen's procedures --- */

static int host_only_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* HostOnlyControlInteract 0x450d50: Start and its panel stay hidden on a joiner */
	if (e->type == KP_UI_USER && e->code == KP_UI_SHOW && !ch(ui)->host_controls) return 1;
	return c->type == TYPE_BUTTON ? kp_ui_flc_button_proc(ui, c, e) : kp_ui_type_interact(c->type)(ui, c, e);
}

static int chat_edit_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* LobbyChatEditInteract 0x451310, LobbyChatEditKeyFilter 0x451360 */
	int32_t self = (int32_t)(c - ui->controls);
	if (e->type == KP_UI_USER && e->code == KP_UI_POST_LOAD) {
		c->flags |= 0x20400u;
		ch(ui)->edit = self;
		kp_ui_edit_set_colour(ui, self, 3);
	} else if (e->type == KP_UI_CHAR) {
		const uint8_t *text;
		kp_ui_select_control_font(ui, c);
		text = kp_ui_edit_buffer(ui, self);
		if (text != NULL && kp_text_chars(text) >= CHAT_LINE && e->key != KEY_BACKSPACE) return 1;
		if (text != NULL && text[0] == 0 && e->key == ' ') return 1;
		(void)kp_ui_edit_proc(ui, c, e);
		return 1;
	}
	return kp_ui_edit_proc(ui, c, e);
}

static int chat_list_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* LobbyChatListInteract 0x4513d0 */
	int32_t self = (int32_t)(c - ui->controls);
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE:
			ch(ui)->list = self;
			kp_ui_list_select(ui, self, KP_UI_LIST_NONE);
			kp_ui_list_redraw(ui, self, 1);
			break;
		case KP_UI_DESTROY:
			kp_ui_list_clear(ui, self);
			break;
		case KP_UI_POST_LOAD:
			c->flags |= 0x10020400u;
			break;
		case KP_UI_VALUE_CHANGED:
			(void)kp_ui_list_proc(ui, c, e);
			return 1;
		default:
			break;
		}
	}
	return kp_ui_list_proc(ui, c, e);
}

static int info_label_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* GameInfoLabelInteract 0x4508f0: the value labels in text colour 6 */
	if (e->type == KP_UI_USER && e->code == KP_UI_POST_LOAD) c->flags |= KP_UI_PRESSED;
	return kp_ui_type_interact(TYPE_STATIC)(ui, c, e);
}

/* LeaveLobby 0x450980 after its popup: the dialog closed with the result */
static void room_left(KpUi *ui, int32_t popup_result)
{
	KpUiChat *c = ch(ui);
	(void)popup_result;
	ui->result = c->leave_result;
	if (!c->destroyed) (void)kp_ui_send_user(ui, c->dialog, KP_UI_CLOSE, 0);
}

static void leave_room(KpUi *ui, uint16_t text, int32_t result)
{
	/* LeaveLobby 0x450980: ShowOkPopup 0x460230's run is modal, the room closes once it ends */
	KpUiChat *c = ch(ui);
	c->polling = 0;
	c->leave_result = result;
	if (text != 0) {
		kp_ui_menus_ok_popup(ui, kp_ui_tbl(ui, KP_UI_GLUALL, text), room_left);
		return;
	}
	kp_ui_menus_close_popup(ui);
	room_left(ui, 0);
}

static void close_with(KpUi *ui, int32_t result)
{
	/* the idle callback cleared, the popup closed, the dialog closed with the result */
	KpUiChat *c = ch(ui);
	c->polling = 0;
	kp_ui_menus_close_popup(ui);
	ui->result = result;
	if (!c->destroyed) (void)kp_ui_send_user(ui, c->dialog, KP_UI_CLOSE, 0);
}

static int command(KpUi *ui, KpUiControl *d, int32_t id)
{
	/* LobbyCommand 0x4509d0 */
	KpUiChat *c = ch(ui);
	int32_t self = (int32_t)(d - ui->controls);
	switch (id) {
	case ID_START: {
		int refusal = ui->net->lobby_start_check != NULL ? ui->net->lobby_start_check(ui->net->host) : 0;
		if (refusal != 0) {
			kp_ui_menus_ok_popup(ui, kp_ui_tbl(ui, KP_UI_GLUALL, (uint32_t)refusal), NULL);
			return 1;
		}
		if (ui->net->lobby_start != NULL) (void)ui->net->lobby_start(ui->net->host);
		disable(ui, control(ui, ID_START));
		show_lobby_controls(ui, 0, 0);
		c->preview_shown = 0;
		return 1;
	}
	case ID_CANCEL:
		disable(ui, control(ui, ID_CANCEL));
		if (ui->net->lobby_abort != NULL) ui->net->lobby_abort(ui->net->host);
		/* fall through */
	case R_LEAVE:
		if (ui->net->lobby_cancel != NULL) ui->net->lobby_cancel(ui->net->host);
		show_lobby_controls(ui, 0, 1);
		c->preview_shown = 0;
		c->polling = 0;
		kp_ui_glue_slide_out(ui, self);
		return 1;
	case ID_SEND: {
		uint8_t *text = c->edit >= 0 ? kp_ui_edit_buffer(ui, c->edit) : NULL;
		if (text != NULL && text[0] != 0 && ui->net->lobby_chat != NULL) ui->net->lobby_chat(ui->net->host, (const char *)text);
		if (c->edit >= 0) kp_ui_edit_clear(ui, c->edit);
		return 1;
	}
	default:
		c->polling = 0;
		kp_ui_glue_slide_out(ui, self);
		return 1;
	}
}

static void chat_done(KpUi *ui, int32_t r)
{
	/* RunLobbyScreen 0x451470's dispatch: the game's start keeps the state; Cancel or a leave goes back by the
	 * provider (the replay flag cleared, FreeReplayMapData 0x4808d0); anything else the main menu */
	uint32_t provider = ui->menus.conn.provider; /* GetProviderId 0x475a10 */
	int next = STATE_CHAT, action = KP_UI_MENU_STATE;
	ch(ui)->dialog = -1;
	ch(ui)->edit = ch(ui)->list = -1;
	if (r == R_START) {
		/* PollLobby 0x474f00's step 3: a Use Map Settings game's race ready room, else the game at once */
		int room = ui->net->ready_room != NULL ? ui->net->ready_room(ui->net->host) : 0;
		if (room != 0) next = room;
		action = KP_UI_MENU_NETWORK;
	} else if (r == ID_CANCEL || r == R_LEAVE) {
		next = provider == PROVIDER_BNET ? STATE_BATTLE_NET
			: provider == PROVIDER_MODEM || provider == PROVIDER_MODEM_X ? STATE_MODEM : STATE_JOIN;
		ui->menus.create.replay_read = 0;
	} else {
		next = STATE_MAIN;
	}
	kp_ui_menus_end(ui, next, action);
}

static int chat_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* LobbyDialogInteract 0x450b30 */
	KpUiChat *c = ch(ui);
	int32_t self = (int32_t)(d - ui->controls);
	KpUiInteract base = kp_ui_type_interact(0);
	switch (e->type) {
	case KP_UI_MOUSE_MOVE: {
		int32_t p = control(ui, ID_PREVIEW);
		if (p >= 0) {
			const KpUiControl *b = &ui->controls[p];
			int32_t x = e->x - d->left, y = e->y - d->top;
			show_lobby_controls(ui, x >= b->left && x <= b->right && y >= b->top && y <= b->bottom, 1);
		}
		return base(ui, d, e);
	}
	case KP_UI_CHAR: /* '/': SNetGetLeaguePlayerName (Battle.net's, nothing here) */
		return base(ui, d, e);
	case KP_UI_WHEEL_UP:
	case KP_UI_WHEEL_DOWN:
		if (c->list >= 0 && ui->controls[c->list].list != NULL && ui->controls[c->list].list->bar >= 0) {
			(void)kp_ui_send_user(ui, ui->controls[c->list].list->bar,
				e->type == KP_UI_WHEEL_UP ? KP_UI_SCROLL_UP : KP_UI_SCROLL_DOWN, 0);
		}
		return 1;
	case KP_UI_USER:
		break;
	default:
		return base(ui, d, e);
	}
	switch (e->code) {
	case KP_UI_CREATE: {
		static const uint16_t SLIDES[10] = { 1, 3, 2, 2, 3, 0, 4, 3, 5, 2 }; /* 0x503b50 */
		/* InitLobbyInfo 0x450910 */
		c->host_controls = lb(ui)->host;
		if (!c->host_controls) {
			kp_ui_hide_control(ui, control(ui, ID_PANEL_OK));
			kp_ui_hide_control(ui, control(ui, ID_START));
		}
		describe_game(ui);
		kp_ui_glue_slide_in(ui, self, SLIDES, 5, 0);
		kp_ui_glue_activate(ui, self, THEME_NONE);
		(void)base(ui, d, e);
		/* LobbyJoinCheck 0x450ea0 */
		if (ui->net->lobby_enter != NULL && !ui->net->lobby_enter(ui->net->host)) close_with(ui, R_LEAVE);
		else c->polling = 1;
		return 1;
	}
	case KP_UI_DESTROY:
		c->polling = 0;
		destroy_rows(ui);
		if (ui->net->lobby_release != NULL) ui->net->lobby_release(ui->net->host);
		c->destroyed = 1;
		close_preview(ui);
		break;
	case KP_UI_CLOSE:
		return command(ui, d, ui->result);
	case KP_UI_POST_LOAD: {
		KpUiInteract procs[PROCS];
		int k;
		memset(procs, 0, sizeof procs); /* 0x503a00 */
		procs[ID_PANEL_OK - 1] = host_only_proc;
		procs[ID_START - 1] = host_only_proc;
		procs[ID_PREVIEW - 1] = procs[ID_CANCEL - 1] = procs[ID_SEND - 1] = kp_ui_text_button_proc;
		procs[ID_EDIT - 1] = chat_edit_proc;
		procs[ID_LIST - 1] = chat_list_proc;
		for (k = ID_PASSWORD_LABEL; k <= ID_MAP_LABEL; k++) procs[k - 1] = kp_ui_type_interact(TYPE_STATIC);
		for (k = ID_PASSWORD; k <= ID_SPEED; k++) procs[k - 1] = info_label_proc;
		procs[ID_COUNT_LABEL - 1] = procs[ID_COUNT - 1] = kp_ui_type_interact(TYPE_STATIC);
		kp_ui_glue_init_dialog(ui, self, procs, PROCS, THEME_NONE);
		c->destroyed = 0;
		if (!lb(ui)->replay && lb(ui)->save_id == 0 && !lb(ui)->ums) open_preview(ui);
		else kp_ui_hide_control(ui, control(ui, ID_PREVIEW));
		break;
	}
	case SLIDE_DONE:
		refresh_password(ui);
		c->hide_rows = 0; /* EnableLobbySlotDisplay 0x451ab0 */
		c->full_refresh = 1;
		kp_ui_hide_control(ui, control(ui, ID_COUNT_LABEL));
		kp_ui_hide_control(ui, control(ui, ID_COUNT));
		c->slid_in = 1;
		return 1;
	default:
		break;
	}
	return base(ui, d, e);
}

/* The step between LoadDialog and the run (RunLobbyScreen's InitLobbySlots 0x4520c0). */
static void chat_prepare(KpUi *ui, int32_t dialog)
{
	KpUiChat *c = ch(ui);
	if (ui->net->lobby_view != NULL) ui->net->lobby_view(ui->net->host, &c->view);
	init_rows(ui, dialog);
}

int32_t kp_ui_lobby_open(KpUi *ui)
{
	/* RunLobbyScreen 0x451470 */
	KpUiChat *c = ch(ui);
	int32_t d;
	if (ui->net == NULL) return -1;
	c->refresh = 0;
	c->leave = 0;
	c->disable_headers = 1;
	c->password_refresh = 0;
	c->dialog = c->edit = c->list = c->preview = -1;
	c->confirm_row = -1;
	c->polling = c->destroyed = c->slid_in = c->preview_shown = c->preview_state = 0;
	c->full_refresh = c->tvb_refresh = 0;
	memset(c->swap_labels, 0, sizeof c->swap_labels);
	memset(&c->view, 0, sizeof c->view);
	d = kp_ui_menus_open(ui, "rez\\gluChat.bin", chat_proc, chat_done, chat_prepare);
	return d;
}

/* LobbyUpdate 0x450d80: the room's idle callback, which every pass of a modal loop runs for each open dialog
 * (RunModalDialog 0x4c35c0), the room's own or a popup's over it. 1 when the pass took a message that ended nothing,
 * so another pass may take the next. */
static int lobby_pass(KpUi *ui)
{
	KpUiChat *c = ch(ui);
	int r;
	r = ui->net->lobby_poll != NULL ? ui->net->lobby_poll(ui->net->host) : 0;
	if (r == KP_UI_LOBBY_REFRESH) {
		refresh_rows(ui);
		c->refresh = 0;
		return 0;
	}
	if (r == KP_UI_LOBBY_START) {
		close_with(ui, R_START);
		return 0;
	}
	if (c->leave) {
		if (ui->net->lobby_abort != NULL) ui->net->lobby_abort(ui->net->host);
		show_lobby_controls(ui, 0, 1);
		c->preview_shown = 0;
		leave_room(ui, c->leave_text, R_LEAVE);
		return 0;
	}
	if (c->refresh) {
		refresh_rows(ui);
		c->refresh = 0;
	}
	if (c->password_refresh) {
		char password[KP_UI_GAME_NAME];
		password[0] = 0;
		if (ui->net->lobby_password != NULL) ui->net->lobby_password(ui->net->host, password, sizeof password);
		if (password[0] == 0) {
			refresh_password(ui);
			c->password_refresh = 0;
		}
	}
	return r == KP_UI_LOBBY_MESSAGE;
}

/* The chat room's dialog is the topmost modal (the original's nested popups run their own loop). The passes the
 * original's modal loop makes between two of Koprulu's frames: another while the last took a message, so the lobby's
 * messages (the map transfer's chunks, several in flight) go as fast as they arrive, not one a frame (the transfer's
 * pace followed the slower seat's frame rate). */
void kp_ui_lobby_idle(KpUi *ui)
{
	KpUiChat *c = ch(ui);
	int pass;
	for (pass = 0; pass < KP_UI_LOBBY_PASSES; pass++) {
		if (!c->polling || c->dialog < 0 || !lobby_pass(ui)) return;
	}
}

void kp_ui_lobby_line(KpUi *ui, int kind, const char *text)
{
	if (ch(ui)->dialog >= 0 && text != NULL) append_line(ui, text, kind);
}

void kp_ui_lobby_refresh(KpUi *ui) { ch(ui)->refresh = 1; }

void kp_ui_lobby_password_refresh(KpUi *ui) { ch(ui)->password_refresh = 1; }

void kp_ui_lobby_leave(KpUi *ui, int reason)
{
	/* SetLobbyLeaveReason 0x4506b0's table 0x450738 */
	static const uint16_t TEXTS[10] = { 0x74, 0x75, 0x76, 0x77, 0x78, 0x7a, 0x7d, 0x79, 0x7b, 0x7c };
	KpUiChat *c = ch(ui);
	c->leave = 1;
	c->leave_text = reason >= 0 && reason < 10 ? TEXTS[reason] : STR_LEAVE_FIRST;
}

void kp_ui_lobby_download(KpUi *ui, int storm, int pct)
{
	/* SetSlotDownloadPercent 0x4523a0 */
	KpUiChat *c = ch(ui);
	if (storm < 0 || storm >= KP_UI_LOBBY_PLAYERS || c->percents[storm] == (uint8_t)pct) return;
	c->percents[storm] = (uint8_t)pct;
	if (c->dialog >= 0) update_percents(ui);
}

static void countdown_show(KpUi *ui, int seconds)
{
	/* ShowLobbyCountdown 0x4bb340 in the chat room: ShowLobbyCountdownText 0x451680 */
	KpUiChat *c = ch(ui);
	int i;
	if (c->dialog < 0) return;
	if (c->preview_shown == 1) {
		show_lobby_controls(ui, 0, 0);
		c->preview_shown = 0;
	}
	(void)kp_ui_play_sound(ui, COUNTDOWN_SOUND);
	if (c->disable_headers) { /* DisableLobbyHeaderRows 0x451890 */
		for (i = 0; i < KP_UI_CHAT_ROWS; i++) {
			if (c->rows[i].kind == KP_UI_ROW_TEAM || c->rows[i].kind == KP_UI_ROW_FORCE || c->rows[i].kind == KP_UI_ROW_3) {
				disable(ui, row_control(ui, i, 0));
				disable(ui, row_control(ui, i, 1));
			}
		}
		c->disable_headers = 0;
	}
	if (seconds == 3) disable(ui, control(ui, ID_CANCEL));
	kp_ui_show_control(ui, control(ui, ID_COUNT_LABEL));
	kp_ui_show_control(ui, control(ui, ID_COUNT));
	if (seconds == 1) c->disable_headers = 1;
	snprintf((char *)c->countdown_text, sizeof c->countdown_text, "%d %s", seconds,
		(const char *)kp_ui_tbl(ui, KP_UI_GLUALL, seconds == 1 ? STR_SECOND : STR_SECONDS));
	if (control(ui, ID_COUNT) >= 0) kp_ui_set_control_text(ui, control(ui, ID_COUNT), c->countdown_text);
}

static void countdown_hide(KpUi *ui)
{
	/* HideLobbyCountdown 0x4bb320 in the chat room: HideLobbyCountdownText 0x4517a0 */
	if (ch(ui)->dialog < 0) return;
	kp_ui_hide_control(ui, control(ui, ID_COUNT_LABEL));
	kp_ui_hide_control(ui, control(ui, ID_COUNT));
}

void kp_ui_lobby_countdown(KpUi *ui, int seconds)
{
	KpUiChat *c = ch(ui);
	if (seconds < 0) { /* StopLobbyCountdown 0x47a210 */
		if (!c->countdown_timer) return;
		countdown_hide(ui);
		c->countdown_timer = 0;
		c->countdown = 0;
		return;
	}
	/* StartLobbyCountdown 0x47a160 */
	c->countdown = seconds;
	c->countdown_timer = 1;
	c->countdown_due = ui->now + COUNTDOWN_PERIOD;
	countdown_show(ui, seconds);
}

void kp_ui_lobby_tick(KpUi *ui)
{
	/* LobbyCountdownTimerProc 0x47a1a0, about once a second while the Win32 timer lives */
	KpUiChat *c = ch(ui);
	if (!c->countdown_timer || (int32_t)(ui->now - c->countdown_due) < 0) return;
	c->countdown_due = ui->now + COUNTDOWN_PERIOD;
	if (c->dialog >= 0 && ui->menus.state == STATE_CHAT) {
		if (c->countdown == 0) {
			kp_ui_lobby_countdown(ui, -1);
		} else {
			c->countdown--;
			countdown_show(ui, c->countdown);
		}
		return;
	}
	c->countdown_timer = 0;
	c->countdown = 0;
}

void kp_ui_lobby_swap(KpUi *ui, int a, int b)
{
	/* OnLobbySwapSlots 0x471150's ResetComboBox 0x4c5eb0: an open drop-down of either slot's row closes */
	if (ch(ui)->dialog >= 0 && a >= 0) close_drop_downs(ui, a, b);
}

void kp_ui_lobby_lock(KpUi *ui)
{
	/* LockLobbySlotsThunk 0x451770 */
	if (ch(ui)->dialog >= 0) lock_rows(ui);
}
