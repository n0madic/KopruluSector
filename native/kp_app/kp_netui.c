/* The multiplayer screens over the game's network code (kp_netui.h). */
#include "kp_netui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kp_ids.h"
#include "kp_text.h"

#define TEMPLATE_TOP_VS_BOTTOM 0x0f /* IsTopVsBottom 0x447e70: the record's game type */
#define CREATE_FAILED 1u            /* create_game's answer when Storm left no error (the screen's generic message) */

static KpNetUi *nu(void *host) { return (KpNetUi *)host; }

static uint16_t record16(const uint8_t *r, int at) { return (uint16_t)(r[at] | r[at + 1] << 8); }

static void utf8(char *out, size_t size, const char *legacy) { (void)kp_text_from_legacy(legacy, out, size); }

/* --- the game's hooks: the screens' side --- */

static uint32_t g_now(void *ud) { return nu(ud)->ui->now; }

static uint32_t g_time(void *ud)
{
	KpNetUi *n = nu(ud);
	return n->time ? n->time(n->owner) : (uint32_t)time(NULL);
}

static const char *g_text(void *ud, int index)
{
	KpNetUi *n = nu(ud);
	const uint8_t *s;
	size_t size;
	if (index < 1 || index >= KP_NETUI_TEXTS) return NULL;
	if (n->texts[index]) return n->texts[index];
	if ((s = kp_ui_tbl(n->ui, KP_UI_NETWORK, (uint32_t)index)) == NULL) return NULL;
	size = strlen((const char *)s) + 1; /* a legacy byte per UTF-8 character at most */
	if ((n->texts[index] = (char *)malloc(size)) == NULL) return NULL;
	(void)kp_text_to_legacy((const char *)s, n->texts[index], size);
	return n->texts[index];
}

static void g_refresh(void *ud) { kp_ui_lobby_refresh(nu(ud)->ui); }
static void g_password_refresh(void *ud) { kp_ui_lobby_password_refresh(nu(ud)->ui); }

static void g_line(void *ud, int kind, const char *text)
{
	char line[KP_UI_CHAT_TEXT];
	utf8(line, sizeof line, text);
	/* KP_LINE_* are the chat room's KP_UI_CHAT_* */
	kp_ui_lobby_line(nu(ud)->ui, kind, line);
}

static void g_leave(void *ud, int reason) { kp_ui_lobby_leave(nu(ud)->ui, reason); }

static void g_timed(void *ud, const char *text)
{
	char line[KP_UI_CHAT_TEXT];
	utf8(line, sizeof line, text);
	kp_ui_timed_message(nu(ud)->ui, (const uint8_t *)line, 0);
}
static void g_download(void *ud, int storm, int pct) { kp_ui_lobby_download(nu(ud)->ui, storm, pct); }
static void g_countdown(void *ud, int seconds) { kp_ui_lobby_countdown(nu(ud)->ui, seconds); }
static void g_swap(void *ud, int a, int b) { kp_ui_lobby_swap(nu(ud)->ui, a, b); }
static void g_lock(void *ud) { kp_ui_lobby_lock(nu(ud)->ui); }
static void g_key_draws(void *ud, int decodes) { kp_ui_menus_key_draws(nu(ud)->ui, decodes); }

static int g_find_save(void *ud, uint32_t id, KpNetGameSave *out)
{
	KpNetUi *n = nu(ud);
	return n->find_save != NULL && n->find_save(n->owner, id, out);
}

static void g_error(void *ud, int index, const char *detail, int fatal)
{
	/* TODO: ShowNetErrorMessage 0x474a70's popup over the active screen with Storm's text (detail), and a fatal
	 * report's leave (LeaveNetworkGame 0x4bb760); the deferred report of the next screen meanwhile */
	(void)detail;
	(void)fatal;
	kp_ui_menus_net_error(nu(ud)->ui, (uint16_t)index);
}

KpNetGameHooks kp_netui_game_hooks(KpNetUi *n)
{
	KpNetGameHooks h;
	memset(&h, 0, sizeof h);
	h.now = g_now;
	h.time = g_time;
	h.text = g_text;
	h.refresh = g_refresh;
	h.password_refresh = g_password_refresh;
	h.line = g_line;
	h.leave = g_leave;
	h.download = g_download;
	h.countdown = g_countdown;
	h.swap = g_swap;
	h.lock = g_lock;
	h.force_names = g_refresh; /* OnChatForceNames 0x471a90: the headers' names read again */
	h.error = g_error;
	h.key_draws = g_key_draws;
	h.find_save = g_find_save;
	h.timed = g_timed;
	h.ud = n;
	return h;
}

/* --- the screens' hooks --- */

static void game_of(const KpNetGameHeader *h, KpUiGame *g)
{
	memset(g, 0, sizeof *g);
	g->id = h->id;
	utf8(g->name, sizeof g->name, h->name);
	g->width = h->width;
	g->height = h->height;
	g->players = h->players;
	g->max_players = h->max_players;
	g->speed = h->speed;
	g->type = h->type;
	g->sub = h->sub;
	g->subtype = h->subtype;
	g->replay = h->replay;
	utf8(g->title, sizeof g->title, h->title);
}

static int u_games(void *host, KpUiGame *out, int max)
{
	KpNetGame *ng = nu(host)->game;
	int k;
	if (!ng) return 0;
	kp_netgame_enum(ng);
	for (k = 0; k < ng->listed_count && k < max; k++) game_of(&ng->listed[k], &out[k]);
	return k;
}

static int u_join(void *host, const KpUiGame *game)
{
	KpNetGame *ng = nu(host)->game;
	int k;
	if (!ng) return 0;
	for (k = 0; k < ng->listed_count; k++)
		if (ng->listed[k].id == game->id) return kp_netgame_join(ng, &ng->listed[k]);
	return 0;
}

static void u_clear_games(void *host, int unhost)
{
	KpNetGame *ng = nu(host)->game;
	if (!ng) return;
	ng->listed_count = 0;
	if (unhost) ng->host = 0;
}

/* CreateMultiplayerGame 0x475040 of a header: 0 created, else Storm's error */
static uint32_t create(KpNetGame *ng, const KpNetGameHeader *h)
{
	uint32_t err;
	if (kp_netgame_create(ng, h, NULL)) return 0;
	err = ng->net.error ? ng->net.error(ng->net.ud) : 0;
	return err ? err : CREATE_FAILED;
}

static uint32_t u_create_game(void *host, KpUi *ui, int index, uint8_t speed)
{
	KpNetUi *n = nu(host);
	KpNetGame *ng = n->game;
	const KpUiCreate *c = &ui->menus.create;
	const KpUiMapEntry *e;
	KpMapHeader mh;
	KpNetMap m;
	KpNetGameHeader h;
	char path[KP_TRANSFER_PATH];
	if (!ng || index < 0 || index >= ui->menus.entry_count) return CREATE_FAILED;
	if (ui->menus.entries[index].kind == KP_UI_ENTRY_SAVE) {
		/* CreateGameFromEntry 0x45bfc0's saved game: its header the game's, the player's name its name (the map, the
		 * slots and the screen's choices are the save's: the lobby reads them from it) */
		if (!n->entry_save || !n->entry_save(n->owner, ui, index, &h)) return CREATE_FAILED;
		snprintf(h.name, sizeof h.name, "%s", ng->config.player_name);
		return create(ng, &h);
	}
	if (!n->entry_map || !n->entry_map(n->owner, ui, index, &mh, path, sizeof path)) return CREATE_FAILED;
	e = &ui->menus.entries[index];
	kp_netgame_map(&m, &mh, path, &ng->hooks);
	kp_netgame_set_map(ng, &m);
	/* CreateGameFromEntry 0x45bfc0's header: the player's name as the game's, the map's size, tileset, slots and
	 * title, the speed, the map's class and the screen's template key */
	memset(&h, 0, sizeof h);
	snprintf(h.name, sizeof h.name, "%s", ng->config.player_name);
	h.width = m.width;
	h.height = m.height;
	h.tileset = m.tileset;
	h.max_players = e->players;
	h.speed = speed;
	h.map_class = (uint8_t)e->map_class;
	h.type = c->record[KP_TEMPLATE_TYPE];
	h.sub = c->record[KP_TEMPLATE_SUB];
	h.subtype = record16(c->record, KP_TEMPLATE_SUBTYPE);
	memcpy(h.title, m.title, sizeof h.title);
	/* TODO: stage N4 hosts a replay's game (CreateGameFromEntry's kind 0x80: the replay's header, the lobby that plays
	 * it) */
	return create(ng, &h);
}

static void u_player_name(void *host, const char *name)
{
	KpNetGame *ng = nu(host)->game;
	if (ng) (void)kp_text_to_legacy_or(name, ng->config.player_name, sizeof ng->config.player_name, KP_TEXT_LEGACY_DEFAULT);
}

static int u_lobby_enter(void *host)
{
	KpNetGame *ng = nu(host)->game;
	if (!ng || !kp_netgame_enter(ng)) return 0;
	ng->in_chat_room = 1;
	return 1;
}

static int u_lobby_poll(void *host)
{
	KpNetGame *ng = nu(host)->game;
	int r;
	if (!ng) return 0;
	r = kp_netgame_poll(ng);
	return r == KP_UI_LOBBY_START || r == KP_UI_LOBBY_REFRESH ? r : 0;
}

void kp_netui_view(const KpNetGame *ng, KpUiLobby *out)
{
	const uint8_t *r = ng->header.template_record;
	int k;
	memset(out, 0, sizeof *out);
	for (k = 0; k < KP_UI_LOBBY_SLOTS && k < KP_NETGAME_SLOTS; k++) {
		const KpNetSlot *s = &ng->slots[k];
		out->slots[k].storm = s->storm;
		out->slots[k].type = s->type;
		out->slots[k].race = s->race;
		out->slots[k].force = s->force;
		utf8(out->slots[k].name, sizeof out->slots[k].name, s->name);
		out->setup_types[k] = ng->setup[k].type;
		out->forces[k] = ng->forces[k];
	}
	for (k = 0; k < KP_NETGAME_FORCES; k++) utf8(out->force_names[k], sizeof out->force_names[k], ng->force_names[k]);
	memcpy(out->user_select, ng->user_select, sizeof out->user_select);
	utf8(out->game_name, sizeof out->game_name, ng->header.name);
	utf8(out->title, sizeof out->title, ng->header.title);
	out->width = ng->header.width;
	out->height = ng->header.height;
	out->max_players = ng->header.max_players;
	out->speed = ng->header.speed;
	out->type = ng->header.type;
	out->sub = ng->header.sub;
	out->subtype = ng->header.subtype;
	out->save_id = ng->header.save_id;
	out->save_players = ng->header.save_players;
	out->host = ng->host;
	out->local_slot = ng->local_slot;
	out->local_storm = ng->local_storm;
	out->replay = ng->config.replay;
	out->save_open = ng->save_open;
	out->ums = kp_netgame_ums(ng);
	out->team = kp_netgame_team_game(ng);
	out->top_vs_bottom = r[KP_TEMPLATE_TYPE] == TEMPLATE_TOP_VS_BOTTOM;
	out->computers = r[KP_TEMPLATE_RESTRICTION] == 1 || r[KP_TEMPLATE_RESTRICTION] == 3; /* IsRestrictionMode1Or3 */
	out->top_size = record16(r, KP_TEMPLATE_SUBTYPE); /* GetTemplateSubtype 0x447c90 */
	out->lobby_state = ng->lobby_state;
	out->turn = ng->turn;
	out->transit = ng->transit;
	out->map_ready = ng->host || ng->transfer.map_ready;
	out->transfer_done = ng->offer == NULL || kp_transfer_all_have(ng->offer);
}

static void u_lobby_view(void *host, KpUiLobby *out)
{
	const KpNetGame *ng = nu(host)->game;
	if (ng) kp_netui_view(ng, out);
	else memset(out, 0, sizeof *out);
}

static void u_lobby_abort(void *host)
{
	if (nu(host)->game) kp_netgame_abort(nu(host)->game);
}

static void u_lobby_cancel(void *host)
{
	if (nu(host)->game) kp_netgame_cancel(nu(host)->game);
}

static void u_lobby_release(void *host)
{
	KpNetGame *ng = nu(host)->game;
	if (!ng) return;
	ng->in_chat_room = 0;
	kp_netgame_release(ng);
}

static int u_lobby_start_check(void *host)
{
	return nu(host)->game ? kp_netgame_start_check(nu(host)->game) : 0;
}

static int u_lobby_start(void *host) { return nu(host)->game ? kp_netgame_start(nu(host)->game) : 0; }

static void u_lobby_chat(void *host, const char *text)
{
	char legacy[KP_NETGAME_LINE];
	if (!nu(host)->game) return;
	(void)kp_text_to_legacy_or(text, legacy, sizeof legacy, KP_TEXT_LEGACY_DEFAULT);
	kp_netgame_chat(nu(host)->game, legacy);
}

static void u_game_chat(void *host, const char *text, uint16_t recipients)
{
	char legacy[KP_NETGAME_LINE];
	if (!nu(host)->game) return;
	(void)kp_text_to_legacy_or(text, legacy, sizeof legacy, KP_TEXT_LEGACY_DEFAULT);
	kp_netgame_game_chat(nu(host)->game, legacy, recipients);
}

static int u_game_latency(void *host) { return nu(host)->game ? (int)nu(host)->game->extra_latency : 0; }

static int u_game_lagging(void *host, int slots[8], uint32_t *seconds)
{
	KpNetGame *ng = nu(host)->game;
	if (!ng || !ng->lagging) return -1;
	*seconds = ng->timeout_seconds;
	return kp_netgame_lagging_slots(ng, slots);
}

static void u_game_drop(void *host)
{
	if (nu(host)->game) (void)kp_netgame_timeout_drop(nu(host)->game);
}

/* The slot menus' actions as the game's (KP_SLOT_*), -1 for the others */
static int slot_kind(int action)
{
	switch (action) {
	case KP_UI_SLOT_OPEN: return KP_SLOT_OPEN;
	case KP_UI_SLOT_CLOSE: return KP_SLOT_CLOSE;
	case KP_UI_SLOT_COMPUTER: return KP_SLOT_COMPUTER;
	case KP_UI_SLOT_BAN: return KP_SLOT_BAN;
	default: return -1;
	}
}

static int u_lobby_slot(void *host, int action, int slot, int target)
{
	KpNetGame *ng = nu(host)->game;
	int kind = slot_kind(action);
	if (!ng) return 0;
	if (action == KP_UI_SLOT_PUBLIC) return kp_netgame_request_public(ng);
	if (action == KP_UI_SLOT_SWAP) return kp_netgame_request_swap(ng, slot, target);
	if (kind == KP_SLOT_BAN) return kp_netgame_ban(ng, slot);
	return kind >= 0 && kp_netgame_slot_action(ng, slot, kind);
}

static int u_lobby_slot_check(void *host, int action, int slot, char *names, size_t size)
{
	KpNetGame *ng = nu(host)->game;
	char legacy[8 * KP_NETGAME_NAME + 16];
	int kind = slot_kind(action), r;
	if (size) names[0] = 0;
	if (!ng || kind < 0) return 0;
	r = kp_netgame_slot_check(ng, slot, kind, legacy, sizeof legacy);
	if (r == 2 && size) utf8(names, size, legacy);
	return r;
}

static int u_lobby_race(void *host, int slot, int race)
{
	return nu(host)->game ? kp_netgame_request_race(nu(host)->game, slot, race) : 0;
}

static void u_lobby_force(void *host, int kind, int force)
{
	KpNetGame *ng = nu(host)->game;
	if (!ng) return;
	if (kind == KP_UI_ROW_TEAM) (void)kp_netgame_request_team(ng, force);
	else if (kind == KP_UI_ROW_FORCE) (void)kp_netgame_request_force(ng, force);
}

static int u_lobby_latency(void *host, int storm, uint32_t *ms)
{
	KpNetGame *ng = nu(host)->game;
	KpSnetCaps caps;
	if (!ng || !ng->net.player_caps) return 0;
	memset(&caps, 0, sizeof caps);
	caps.size = sizeof caps;
	if (!ng->net.player_caps(ng->net.ud, storm, &caps)) return 0;
	*ms = caps.latency;
	return 1;
}

static void u_lobby_password(void *host, char *out, size_t size)
{
	KpNetGame *ng = nu(host)->game;
	char legacy[KP_UI_GAME_NAME];
	uint32_t written = 0;
	if (size) out[0] = 0;
	if (!ng || !ng->net.game_info || !size) return;
	memset(legacy, 0, sizeof legacy);
	if (!ng->net.game_info(ng->net.ud, KP_SNET_INFO_PASSWORD, legacy, sizeof legacy - 1, &written)) return;
	utf8(out, size, legacy);
}

static int u_ready_room(void *host) { return nu(host)->game ? kp_netgame_ready_room(nu(host)->game) : 0; }

static void u_briefing_arm(void *host)
{
	if (nu(host)->game) kp_netgame_briefing_arm(nu(host)->game);
}

static void u_briefing_start(void *host, int abort)
{
	if (nu(host)->game) kp_netgame_briefing_start(nu(host)->game, abort);
}

static int u_briefing_poll(void *host) { return nu(host)->game ? kp_netgame_briefing_poll(nu(host)->game) : 0; }

void kp_netui_hooks(KpNetUi *n, KpUiNet *net)
{
	net->player_name = u_player_name;
	net->games = u_games;
	net->join = u_join;
	net->clear_games = u_clear_games;
	net->create_game = u_create_game;
	net->lobby_enter = u_lobby_enter;
	net->lobby_poll = u_lobby_poll;
	net->lobby_view = u_lobby_view;
	net->lobby_abort = u_lobby_abort;
	net->lobby_cancel = u_lobby_cancel;
	net->lobby_release = u_lobby_release;
	net->lobby_start_check = u_lobby_start_check;
	net->lobby_start = u_lobby_start;
	net->lobby_chat = u_lobby_chat;
	net->game_chat = u_game_chat;
	net->game_latency = u_game_latency;
	net->game_lagging = u_game_lagging;
	net->game_drop = u_game_drop;
	net->lobby_slot = u_lobby_slot;
	net->lobby_slot_check = u_lobby_slot_check;
	net->lobby_race = u_lobby_race;
	net->lobby_force = u_lobby_force;
	net->lobby_latency = u_lobby_latency;
	net->lobby_password = u_lobby_password;
	net->ready_room = u_ready_room;
	net->briefing_arm = u_briefing_arm;
	net->briefing_start = u_briefing_start;
	net->briefing_poll = u_briefing_poll;
	net->host = n;
}

void kp_netui_free(KpNetUi *n)
{
	int k;
	for (k = 0; k < KP_NETUI_TEXTS; k++) {
		free(n->texts[k]);
		n->texts[k] = NULL;
	}
}
