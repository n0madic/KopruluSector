/* The game's network layer over SNet: the multiplayer lobby (kp_netgame.h; phase N, stage N3; atlas P17, P24..P27, P41). */
#include "kp_netgame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_gametype.h"
#include "kp_ids.h"
#include "kp_net.h"
#include "kp_record.h"

/* The pending lobby messages other than 0x3e and 0x3f, in FlushLobbyMessages 0x470460's order (0x3e and 0x3f flush
 * between DOWNLOAD and RACE) */
enum { PEND_START, PEND_DOWNLOAD, PEND_RACE, PEND_TEAM, PEND_FORCE, PEND_ACTION, PEND_SWAP, PEND_START_INFO,
	PEND_PUBLIC, PEND_JOIN };

/* Storm's error codes the game's reports ignore (ReportStormError 0x474b80, SendNetMessage 0x474780) */
#define ERROR_BAD_PLAYER 0x8510006au
#define ERROR_NO_DATA 0x8510006bu
#define ERROR_NOT_OWNER 0x120u
#define ERROR_CANCELLED 0x4c7u
#define ERROR_VERSION 0x85100079u /* SNetJoinGame: an incompatible version */
#define DROP_KICK 3u              /* RejectChatRoomPlayer 0x471dd0's and OnPlayerJoined 0x4778e0's drop reason */
#define LEAVE_ERROR 3u            /* LeaveStormGame 0x477e50's code of a fatal error */
#define TURN_MAX_IN_TRANSIT 16u   /* QueueTurnCommand 0x476c80: in game, a full turn is sent only below 16 - L */
#define STATUS_PRESENT 0x10000u
#define STATUS_HAS_TURN 0x20000u
#define STATUS_RESPONDING 0x40000u
#define CAPS_SIZE 0x24u
#define NO_SLOT 8
#define RACE_RANDOM 6            /* a lobby slot's Random race (a map's User Select becomes it) */
#define NONE_BYTE 0xff
#define MODE_FULL 2u             /* the game mode's "full" bit (SetGameJoinable 0x476ef0) */
#define MODE_STARTED 8u          /* StartLobbyGame 0x4baed0: Storm refuses further joins */
#define TEMPLATE_TYPE_LADDER_FIRST 0x11 /* IsLadderTemplate 0x447ed0 */
#define TEMPLATE_TYPE_LADDER_LAST 0x16
#define TEMPLATE_TYPE_TOP_VS_BOTTOM 0x0f
#define VICTORY_ONE_ON_ONE 7     /* IsVictory7 0x447d80 */
#define GLUE_READY_TERRAN 7      /* PollLobby 0x474f00's ready rooms */
#define GLUE_READY_ZERG 8
#define GLUE_READY_PROTOSS 9
#define BRIEFING_IDLE 0          /* the ready room's state 0x672986 */
#define BRIEFING_ARMED 1
#define BRIEFING_SENT 2
#define CHAT_JOINED_SIZE 5
#define CHAT_LINE_FORMAT 0x91    /* OnChatPlayerJoined 0x471c80's buffer */
#define LINE_SIZE 152            /* AddLobbyChatMessage 0x451560's buffer */
#define TRANSFER_HEADER 3        /* 'O', WORD length */
#define LOBBY_COMMAND_FIRST 5    /* ExecuteLobbyCommands 0x476650: c - 5 > 0x56 stops */
#define LOBBY_COMMAND_SPAN 0x56

/* --- small helpers --- */

static uint32_t now(const KpNetGame *ng) { return ng->hooks.now(ng->hooks.ud); }

/* SStrCopy: at most size - 1 bytes, NUL-terminated */
static void copy(char *dst, size_t size, const char *src)
{
	size_t n = 0;
	if (!size) return;
	if (src)
		while (n + 1 < size && src[n]) n++;
	if (n) memcpy(dst, src, n);
	dst[n] = 0;
}

/* SStrNCat: appended within size bytes in all */
static void append(char *dst, size_t size, const char *src)
{
	size_t n = strlen(dst);
	if (n < size) copy(dst + n, size - n, src);
}

/* rez\network.tbl string `index`, "%s" when the hooks have none */
static const char *text_of(const KpNetGameHooks *h, int index)
{
	const char *t = h->text ? h->text(h->ud, index) : NULL;
	return t ? t : "%s";
}

/* sprintf(out, size, format, arg) for the table's one-argument formats: "%s" the argument, "%%" a percent, any other
 * '%' kept as it is (a table string never reaches a variadic formatter) */
static void format1(char *out, size_t size, const char *format, const char *arg)
{
	size_t n = 0;
	const char *f;
	if (!size) return;
	for (f = format; *f && n + 1 < size; f++) {
		if (f[0] == '%' && f[1] == 's') {
			const char *a = arg ? arg : "";
			while (*a && n + 1 < size) out[n++] = *a++;
			f++;
		} else if (f[0] == '%' && f[1] == '%') {
			out[n++] = '%';
			f++;
		} else {
			out[n++] = *f;
		}
	}
	out[n] = 0;
}

/* DisplayTimedMessage 0x46ac90 in game */
static void timed(KpNetGame *ng, const char *text)
{
	if (ng->hooks.timed) ng->hooks.timed(ng->hooks.ud, text);
}

/* The in-game messages (0x4ef2b8): their types, the text's offset, its longest with the terminator */
enum { GAME_CHAT, GAME_CHAT_FROM, GAME_MESSAGE_TYPES = 3 };
#define GAME_CHAT_TEXT 2
#define GAME_CHAT_MAX 0x50
#define GAME_LINE 256 /* the received whisper's line (HandleChatMessage 0x475b40) */

static const uint8_t *template_record(const KpNetGame *ng) { return ng->header.template_record; }
static int tbyte(const KpNetGame *ng, int at) { return template_record(ng)[at]; }

int kp_netgame_ums(const KpNetGame *ng) { return kp_template_ums(template_record(ng)); }
/* IsTeamGame 0x4b2d90: the multiplayer byte (always set in a network game) and the template's teams */
int kp_netgame_team_game(const KpNetGame *ng) { return tbyte(ng, KP_TEMPLATE_TEAMS) != 0; }
static int top_vs_bottom(const KpNetGame *ng) { return tbyte(ng, KP_TEMPLATE_TYPE) == TEMPLATE_TYPE_TOP_VS_BOTTOM; }
/* GetTeamCount 0x4b2bc0 */
static int team_count(const KpNetGame *ng) { return top_vs_bottom(ng) ? 2 : tbyte(ng, KP_TEMPLATE_TEAMS); }
/* GetTeamSize 0x4b2b10 (0 for a count the original reports as error 0x5d) */
static int team_size(const KpNetGame *ng)
{
	switch (tbyte(ng, KP_TEMPLATE_TEAMS)) {
	case 2: return 4;
	case 3: return 3;
	case 4: return 2;
	default: return 0;
	}
}
static int saving(const KpNetGame *ng) { return ng->save_open != 0; }

/* --- reports (net_error.cpp) --- */

static void reset_turn_state(KpNetGame *ng);

/* LeaveStormGame 0x477e50 (its one-second Sleep left out: nothing waits on it) */
static void leave_storm(KpNetGame *ng, uint32_t code)
{
	reset_turn_state(ng);
	if (ng->config.replay) code = LEAVE_ERROR;
	ng->net.leave_game(ng->net.ud, code);
}

/* ReportNetError 0x474a00 */
static void report(KpNetGame *ng, int index, const char *detail, int fatal)
{
	if (ng->error_latch) return;
	if (fatal) {
		leave_storm(ng, LEAVE_ERROR);
		ng->error_latch = 1;
	}
	if (ng->hooks.error) ng->hooks.error(ng->hooks.ud, index, fatal ? detail : NULL, fatal);
}

static int ignored_error(uint32_t e) { return e == ERROR_BAD_PLAYER || e == ERROR_NOT_OWNER || e == ERROR_CANCELLED; }

/* ReportStormError 0x474b80 of Storm's error e (none latched, an ignored one left out): SErrGetErrorStr's text is
 * Storm's error code */
static void report_error(KpNetGame *ng, int index, uint32_t e, int fatal)
{
	char detail[32];
	if (ng->error_latch || ignored_error(e)) return;
	snprintf(detail, sizeof detail, "Storm error 0x%08x", (unsigned)e);
	report(ng, index, detail, fatal);
}

/* ReportStormError 0x474b80 of the last SNet call's error */
static void report_storm(KpNetGame *ng, int index, int fatal)
{
	if (!ng->error_latch) report_error(ng, index, ng->net.error(ng->net.ud), fatal);
}

/* SetLobbyLeaveReason 0x4506b0 */
static void set_leave(KpNetGame *ng, int reason)
{
	ng->leave_reason = reason;
	if (ng->hooks.leave) ng->hooks.leave(ng->hooks.ud, reason);
}

static void refresh(KpNetGame *ng)
{
	if (ng->hooks.refresh) ng->hooks.refresh(ng->hooks.ud);
}

/* --- SNet wrappers (net_chrm.cpp's transport, P25 group 5) --- */

/* SendNetMessage 0x474780 */
static void send_message(KpNetGame *ng, int target, const uint8_t *data, uint32_t size)
{
	if (!ng->net.send_message(ng->net.ud, target, data, size)) report_storm(ng, KP_NETTBL_SEND_FAILED, 1);
}

/* ReceiveNetMessage 0x4745e0 */
static int receive_message(KpNetGame *ng, int *sender, const uint8_t **data, uint32_t *size)
{
	uint32_t e;
	if (ng->net.receive_message(ng->net.ud, sender, data, size)) return 1;
	e = ng->net.error(ng->net.ud);
	if (e != ERROR_NO_DATA) report_error(ng, KP_NETTBL_NETWORK_ERROR, e, 1);
	return 0;
}

/* SendTurn 0x474830 */
static void send_turn(KpNetGame *ng, const uint8_t *data, uint32_t size)
{
	if (!ng->net.send_turn(ng->net.ud, data, size)) report_storm(ng, KP_NETTBL_SEND_FAILED, 1);
}

/* GetNetPlayerName 0x477e30 */
static void net_player_name(KpNetGame *ng, int id, char *out, uint32_t size)
{
	if (!ng->net.player_name(ng->net.ud, id, out, size)) out[0] = 0;
	out[size - 1] = 0;
}

/* NetPlayerExists 0x478070 */
static int net_player_exists(KpNetGame *ng, int id)
{
	char name[KP_TRANSFER_PATH];
	return ng->net.player_name(ng->net.ud, id, name, sizeof name) != 0;
}

/* DropNetPlayer 0x477060 */
static void drop_player(KpNetGame *ng, int id, uint32_t reason)
{
	if (id >= 0 && id < KP_NETGAME_PLAYERS) ng->net.drop_player(ng->net.ud, id, reason);
}

/* SetGameJoinable 0x476ef0 */
static void set_joinable(KpNetGame *ng, int open)
{
	uint32_t mode = 0, size = 0;
	if (!ng->net.game_info(ng->net.ud, KP_SNET_INFO_MODE, &mode, sizeof mode, &size)) {
		report_storm(ng, KP_NETTBL_NETWORK_ERROR, 1);
		return;
	}
	mode = open ? mode & ~MODE_FULL : mode | MODE_FULL;
	if (!ng->net.set_game_mode(ng->net.ud, mode, 0)) report_storm(ng, KP_NETTBL_NETWORK_ERROR, 1);
}

/* --- the network player table (net_player.cpp) --- */

int kp_netgame_player_active(const KpNetGame *ng, int storm)
{
	return storm >= 0 && storm < KP_NETGAME_PLAYERS && ng->players[storm].state == 1;
}

const char *kp_netgame_player_name(const KpNetGame *ng, int storm)
{
	if (storm < 0 || storm >= KP_NETGAME_PLAYERS || ng->players[storm].state == 0) return NULL;
	return ng->players[storm].name;
}

/* AddNetPlayerRecord 0x478120 */
static void add_net_player(KpNetGame *ng, int id, uint16_t flags, uint16_t w4, uint16_t version)
{
	KpNetPlayer *p;
	if (id < 0 || id >= KP_NETGAME_PLAYERS || !net_player_exists(ng, id)) return;
	p = &ng->players[id];
	p->w4 = w4;
	p->state = 1;
	p->flags = flags;
	p->version = version;
	net_player_name(ng, id, p->name, sizeof p->name);
}

/* --- the slots (net_record.cpp, P27) --- */

/* RebuildStormSlotMaps 0x4793b0 */
static void rebuild_storm_maps(KpNetGame *ng, int local)
{
	int s;
	ng->local_slot = ng->local_index = -1;
	memset(ng->storm_slot, NO_SLOT, sizeof ng->storm_slot);
	memset(ng->storm_index, NO_SLOT, sizeof ng->storm_index);
	for (s = 7; s >= 0; s--) {
		int id = ng->slots[s].storm;
		if (ng->slots[s].type != KP_CTRL_HUMAN) continue;
		/* the original indexes its maps with the record's Storm ID unchecked */
		if (id >= 0 && id < KP_NETGAME_PLAYERS) ng->storm_slot[id] = ng->storm_index[id] = (uint8_t)s;
		if (id == local) ng->local_slot = ng->local_index = s;
	}
}

/* RestoreSlotFromMapSetup 0x479c40 */
static void restore_slot(KpNetGame *ng, int s) { ng->slots[s] = ng->setup[s]; }

int kp_netgame_open_slots(const KpNetGame *ng)
{
	int s, n = 0;
	for (s = 0; s < 8; s++) n += ng->slots[s].type == KP_CTRL_OPEN;
	return n;
}

static int lobby_computer(int type) { return type == KP_CTRL_LOBBY_COMPUTER; }
static int any_computer(int type) { return type == KP_CTRL_COMPUTER || type == KP_CTRL_LOBBY_COMPUTER; }
/* IsMapPlayerSlotType 0x4795e0: rescuable, either computer, open, neutral */
static int player_type(int type) { return type >= KP_CTRL_RESCUE && type <= KP_CTRL_NEUTRAL; }

/* ForceHasComputer 0x4b2cf0 (types 1 and 5) and ForceHasComputerSlot 0x443600 (type 5) */
static int force_has_computer(const KpNetGame *ng, int f)
{
	int s;
	for (s = 7; s >= 0; s--)
		if (ng->slots[s].force == f && any_computer(ng->slots[s].type)) return 1;
	return 0;
}
static int force_has_computer_slot(const KpNetGame *ng, int f)
{
	int s;
	for (s = 7; s >= 0; s--)
		if (ng->slots[s].force == f && lobby_computer(ng->slots[s].type)) return 1;
	return 0;
}

/* FirstOpenSlotOfForce 0x4b2ad0: types 6 and 9 */
static int first_open_slot_of_force(const KpNetGame *ng, int f)
{
	int s;
	for (s = 0; s < 8; s++)
		if (ng->slots[s].force == f && (ng->slots[s].type == KP_CTRL_OPEN || ng->slots[s].type == KP_CTRL_OBSERVER))
			return s;
	return NO_SLOT;
}

/* FirstStrictOpenSlotOfForce 0x4433d0: type 6 only */
static int first_strict_open_slot_of_force(const KpNetGame *ng, int f)
{
	int s;
	for (s = 0; s < 8; s++)
		if (ng->slots[s].force == f && ng->slots[s].type == KP_CTRL_OPEN) return s;
	return NO_SLOT;
}

/* NextOpenSlotOfForce 0x443500 */
static int next_open_slot_of_force(const KpNetGame *ng, int f, int previous)
{
	int start = (uint8_t)(previous + 1), k;
	for (k = 0; k < 8; k++) {
		int s = (start + k) % 8;
		if (ng->slots[s].force == f && ng->slots[s].type == KP_CTRL_OPEN) return s;
	}
	return NO_SLOT;
}

/* MostOpenForce 0x443480 and FirstSlotOfMostOpenForce 0x4435c0 (NONE_BYTE: no force has an open slot) */
static int first_slot_of_most_open_force(const KpNetGame *ng)
{
	int f, s, best = 0, best_open = 0;
	for (f = 1; f <= 4; f++) {
		int open = 0;
		for (s = 7; s >= 0; s--) open += ng->slots[s].force == f && ng->slots[s].type == KP_CTRL_OPEN;
		if (open > best_open) {
			best = f;
			best_open = open;
		}
	}
	if (!best) return NONE_BYTE;
	return first_strict_open_slot_of_force(ng, best);
}

static int count_type_in_force(const KpNetGame *ng, int f, int type)
{
	int s, n = 0;
	for (s = 7; s >= 0; s--) n += ng->slots[s].force == f && ng->slots[s].type == type;
	return n;
}

/* ChooseTeamForJoin 0x4b2990 */
static int choose_team_for_join(const KpNetGame *ng)
{
	int t, s, best = 0, best_open = 0;
	int last = top_vs_bottom(ng) ? 2 : tbyte(ng, KP_TEMPLATE_TEAMS);
	for (t = 1; t <= last; t++) {
		int open = 0;
		for (s = 7; s >= 0; s--)
			open += ng->slots[s].force == t &&
				(ng->slots[s].type == KP_CTRL_OPEN || ng->slots[s].type == KP_CTRL_OBSERVER);
		if (open == best_open && best != 0 &&
			count_type_in_force(ng, t, KP_CTRL_HUMAN) < count_type_in_force(ng, best, KP_CTRL_HUMAN)) {
			best = t;
			best_open = open;
		}
		if (open > best_open) {
			best = t;
			best_open = open;
		}
	}
	return best;
}

/* FindFreeJoinSlot 0x4794e0 */
static int find_free_join_slot(const KpNetGame *ng)
{
	int s, t;
	if (!kp_netgame_team_game(ng)) {
		for (s = 0; s < 8; s++)
			if (ng->slots[s].type == KP_CTRL_OPEN) return s;
		return NO_SLOT;
	}
	t = choose_team_for_join(ng);
	return t ? first_open_slot_of_force(ng, t) : NO_SLOT;
}

/* CountPlayersInForce 0x4b2b90 (humans and in-game computers) */
static int count_players_in_force(const KpNetGame *ng, int f)
{
	return count_type_in_force(ng, f, KP_CTRL_HUMAN) + count_type_in_force(ng, f, KP_CTRL_COMPUTER);
}

/* CanChangeLobbySlotForce 0x4517d0 */
static int can_change_force(const KpNetGame *ng, int a, int b)
{
	int f = ng->slots[a].force;
	return ng->lobby_state == KP_LOBBY_IN || !kp_netgame_team_game(ng) || f == ng->slots[b].force ||
		count_players_in_force(ng, f) != 1;
}

/* LobbyMoveSlotToForce 0x479630 */
static void move_slot(KpNetGame *ng, int s, int d)
{
	uint8_t s_race = ng->slots[s].race, s_force = ng->slots[s].force;
	uint8_t d_race = ng->slots[d].race, d_force = ng->slots[d].force;
	ng->slots[d] = ng->slots[s];
	ng->slots[d].race = d_race;
	ng->slots[d].force = d_force;
	ng->slots[d].slot = d;
	ng->slots[s] = ng->setup[s];
	ng->slots[s].race = s_race;
	ng->slots[s].force = s_force;
	ng->slots[s].slot = s;
	rebuild_storm_maps(ng, ng->local_storm);
}

/* LobbyCloseSlot 0x478c50 and LobbyOpenSlot 0x479760 */
static void close_or_open_slot(KpNetGame *ng, int s, int requester, uint8_t type)
{
	int f, j;
	uint8_t race;
	if (ng->slots[s].type == type || requester == s) return;
	f = ng->slots[s].force;
	if (kp_netgame_team_game(ng) && force_has_computer(ng, f)) {
		for (j = 0; j < 8; j++) {
			if (ng->slots[j].force != f) continue;
			race = ng->slots[j].race;
			ng->slots[j] = ng->setup[j];
			ng->slots[j].type = type;
			ng->slots[j].race = race;
		}
		return;
	}
	if (ng->slots[s].type == KP_CTRL_HUMAN) return;
	race = ng->slots[s].race;
	restore_slot(ng, s);
	ng->slots[s].type = type;
	ng->slots[s].race = race;
}

/* LobbySetComputerSlot 0x478d30 */
static void set_computer_slot(KpNetGame *ng, int s, int requester)
{
	int f, j, t = ng->slots[s].type;
	if (t == KP_CTRL_LOBBY_COMPUTER || t == KP_CTRL_RESCUE || t == KP_CTRL_RESCUE_ACTIVE || t == KP_CTRL_NEUTRAL) return;
	f = ng->slots[s].force;
	if (kp_netgame_team_game(ng)) {
		if (requester >= 0 && requester < 8 && ng->slots[requester].force == f) return;
		for (j = 0; j < 8; j++)
			if (ng->slots[j].force == f && ng->slots[j].type == KP_CTRL_HUMAN) return;
		for (j = 0; j < 8; j++) {
			if (ng->slots[j].force != f) continue;
			ng->slots[j] = ng->setup[j];
			ng->slots[j].type = KP_CTRL_LOBBY_COMPUTER;
		}
		return;
	}
	if (requester == s || t == KP_CTRL_HUMAN) return;
	restore_slot(ng, s);
	ng->slots[s].type = KP_CTRL_LOBBY_COMPUTER;
}

/* ResetMapSetupTypesForJoin 0x47a000 */
static void reset_setup_types_for_join(KpNetGame *ng)
{
	int s;
	for (s = KP_NETGAME_SLOTS - 1; s >= 0; s--) {
		uint8_t t = ng->setup[s].type;
		if (t == KP_CTRL_LOBBY_COMPUTER || t == KP_CTRL_RESCUE || t == KP_CTRL_RESCUE_ACTIVE || t == KP_CTRL_NEUTRAL)
			ng->setup[s].type = KP_CTRL_OPEN;
		ng->setup[s].force = 0;
	}
}

/* ApplyTeamSetupToMapSlots 0x478a30. Top vs Bottom gives a record of type 1, 2 or 8 the force of a stack DWORD the
 * original never wrote; it is 0 here (the one call path leaves no such record outside an unusual map). */
static void apply_team_setup(KpNetGame *ng)
{
	int k = 0, t, n, c, marked = 0, s;
	uint8_t list[8] = {0};
	if (ng->config.replay || !(kp_netgame_team_game(ng) || top_vs_bottom(ng))) return;
	if (kp_netgame_team_game(ng)) {
		for (t = 0; t < team_count(ng); t++)
			for (n = 0; n < team_size(ng); n++) {
				if (k >= 8) continue;
				ng->setup[k].type = KP_CTRL_OPEN;
				ng->setup[k].race = ng->setup[0].race;
				ng->setup[k].force = (uint8_t)(t + 1);
				k++;
			}
	}
	if (!top_vs_bottom(ng)) return;
	c = (int16_t)(template_record(ng)[KP_TEMPLATE_SUBTYPE] | template_record(ng)[KP_TEMPLATE_SUBTYPE + 1] << 8);
	for (s = 0; s < 8 && marked < c; s++)
		if (player_type(ng->setup[s].type)) {
			list[s] = 1;
			marked++;
		}
	if (s < 8 || c <= 0)
		for (; s < 8; s++)
			if (player_type(ng->setup[s].type)) list[s] = 2;
	for (s = 0; s < 8; s++) {
		if (ng->setup[s].type == 0) continue;
		ng->setup[s].type = KP_CTRL_OPEN;
		ng->setup[s].force = list[s];
		if (s) ng->setup[s].race = ng->setup[0].race;
	}
}

/* CanSetSlotRace 0x447500 */
int kp_netgame_can_set_race(const KpNetGame *ng, int target, int race, int requester)
{
	const KpNetSlot *t, *q;
	if ((unsigned)requester >= 8 || (unsigned)target >= 8) return 0;
	if (race != 0 && race != 1 && race != 2 && race != RACE_RANDOM) return 0;
	if (kp_netgame_ums(ng) && !ng->user_select[target]) return 0;
	if (saving(ng)) return 0;
	t = &ng->slots[target];
	q = &ng->slots[requester];
	if (q->storm == 0) {
		if (kp_netgame_team_game(ng) && t->force != q->force && !force_has_computer(ng, t->force)) return 0;
		if (kp_netgame_team_game(ng)) return 1;
		return target == requester || t->type == KP_CTRL_LOBBY_COMPUTER;
	}
	if (kp_netgame_team_game(ng)) return t->force == q->force;
	return target == requester;
}

/* CountLobbyParticipants 0x451a00 */
int kp_netgame_participants(const KpNetGame *ng)
{
	int s, max = 0, computers = 0, humans = 0;
	int grouped = kp_netgame_team_game(ng) || top_vs_bottom(ng);
	if (top_vs_bottom(ng) && saving(ng)) return 2;
	for (s = 0; s < 8; s++) {
		int t = ng->slots[s].type, f = ng->slots[s].force;
		int *count = any_computer(t) ? &computers : t == KP_CTRL_HUMAN ? &humans : NULL;
		if (!count) continue;
		if (!grouped) {
			(*count)++;
		} else if (f > max) {
			(*count)++;
			max = f;
		}
	}
	return humans >= 1 ? computers + humans : 0;
}

/* --- the saved slots (net_chat_cmds.cpp) --- */

/* AssignSavedSlot 0x471a40 */
static int assign_saved_slot(KpNetGame *ng, int slot, uint8_t saved, int force)
{
	int i;
	if (slot < 0 || slot >= 8) return 0;
	if (!force) {
		for (i = 7; i >= 0; i--)
			if (ng->saved_slots[i] == saved) return 0;
		if (ng->saved_slots[slot] != NO_SLOT) return 0;
	}
	ng->saved_slots[slot] = saved;
	return 1;
}

static void clear_saved_slot(KpNetGame *ng, int slot)
{
	if (slot >= 0 && slot < 8) ng->saved_slots[slot] = NO_SLOT;
}

/* RebuildLobbySlotsFromSave 0x479840's step 6, RebuildSlotsFromSaveSummary 0x479ce0's step 7: the setup records from
 * the slots (their type, race and force; no name) */
static void setup_from_slots(KpNetGame *ng)
{
	int i;
	memset(ng->setup, 0, sizeof ng->setup);
	for (i = KP_NETGAME_SLOTS - 1; i >= 0; i--) {
		ng->setup[i].slot = i;
		ng->setup[i].storm = -1;
		ng->setup[i].type = ng->slots[i].type;
		ng->setup[i].race = ng->slots[i].race;
		ng->setup[i].force = ng->slots[i].force;
	}
}

static int saved_player(int type)
{
	return type == KP_CTRL_COMPUTER || type == KP_CTRL_RESCUE || type == KP_CTRL_RESCUE_ACTIVE || type == KP_CTRL_NEUTRAL;
}

/* RebuildSlotsFromSaveSummary 0x479ce0: a team or Use Map Settings save's slots from the lobby the saved game started
 * with (0x526494, 0x5264a0), its forces, and its players that had lost (closed) or remain (their saved slots) */
static void rebuild_from_summary(KpNetGame *ng)
{
	KpNetSlot old[8];
	int s, k, j, t, team = kp_netgame_team_game(ng), ums = kp_netgame_ums(ng);
	memcpy(old, ng->slots, sizeof old);
	memset(ng->slots, 0, sizeof ng->slots);
	for (s = 0; s < KP_NETGAME_SLOTS; s++) {
		uint8_t type = ng->save.types[s];
		ng->slots[s].race = ng->save.races[s];
		ng->slots[s].slot = s;
		ng->slots[s].storm = -1;
		if (type == KP_CTRL_HUMAN) type = KP_CTRL_OPEN;
		else if (type == KP_CTRL_OPEN) type = KP_CTRL_CLOSED;
		ng->slots[s].type = type;
	}
	if (team)
		for (t = 1; t <= team_count(ng); t++) {
			int base = (uint8_t)((t - 1) * team_size(ng)); /* a byte product */
			for (j = base; j < base + team_size(ng) && j < 8; j++) ng->slots[j].force = (uint8_t)t;
		}
	if (ums)
		for (s = 7; s >= 0; s--) ng->slots[s].force = ng->forces[s];
	for (k = 0; k < 8; k++) {
		if (old[k].type != KP_CTRL_DEFEATED_COMPUTER) continue;
		if (team)
			for (j = 0; j < 8; j++)
				if (ng->slots[j].force == old[k].force) ng->slots[j].type = KP_CTRL_CLOSED;
		if (ums) ng->slots[k].type = KP_CTRL_CLOSED;
	}
	for (k = 0; k < 8; k++)
		if (old[k].type == KP_CTRL_DEFEATED_HUMAN && ums) ng->slots[k].type = KP_CTRL_CLOSED;
	for (k = 0; k < 8; k++) {
		if (!saved_player(old[k].type)) continue;
		if (team)
			for (j = 0; j < 8; j++)
				if (ng->slots[j].force == old[k].force) (void)assign_saved_slot(ng, j, (uint8_t)k, 1);
		if (ums) (void)assign_saved_slot(ng, k, (uint8_t)k, 1);
	}
	setup_from_slots(ng);
}

/* RebuildLobbySlotsFromSave 0x479840 (the host): the saved player table's humans become open slots and its computers
 * lobby computers, the open slots first, then the computers, rescuables and neutrals, each with its saved slot */
static void rebuild_from_save(KpNetGame *ng)
{
	KpNetSlot old[8];
	int k, n = 0;
	memcpy(ng->slots, ng->save.players, sizeof ng->slots); /* 0x499c50 reads the table into the host's slots */
	if (kp_netgame_team_game(ng) || kp_netgame_ums(ng)) {
		rebuild_from_summary(ng);
		return;
	}
	for (k = 7; k >= 0; k--) {
		if (ng->slots[k].type == KP_CTRL_HUMAN) ng->slots[k].type = KP_CTRL_OPEN;
		else if (ng->slots[k].type == KP_CTRL_COMPUTER) ng->slots[k].type = KP_CTRL_LOBBY_COMPUTER;
	}
	memcpy(old, ng->slots, sizeof old);
	memset(ng->slots, 0, sizeof ng->slots);
	for (k = 0; k < 8; k++)
		if (old[k].type == KP_CTRL_OPEN) ng->slots[n++] = old[k];
	for (k = 0; k < 8; k++) {
		int t = old[k].type;
		if (t != KP_CTRL_LOBBY_COMPUTER && t != KP_CTRL_RESCUE && t != KP_CTRL_RESCUE_ACTIVE && t != KP_CTRL_NEUTRAL)
			continue;
		ng->slots[n] = old[k];
		(void)assign_saved_slot(ng, n, (uint8_t)k, 1);
		n++;
	}
	for (k = KP_NETGAME_SLOTS - 1; k >= 0; k--) {
		ng->slots[k].storm = -1;
		ng->slots[k].slot = k;
		ng->slots[k].force = 0;
	}
	setup_from_slots(ng);
}

/* ReadSaveHeaderAndGameBlock 0x499c50's globals the lobby reads: the save's game header (0x6728d0), its forces, force
 * names and map size (the core block's), the RNG state (GetSavedRngState 0x499860) */
static void load_save(KpNetGame *ng, const KpNetGameSave *save)
{
	ng->save = *save;
	ng->save_open = 1;
	ng->save_rng = save->rng_state;
	ng->header = save->header;
	memcpy(ng->forces, save->forces, sizeof ng->forces);
	memcpy(ng->force_names, save->force_names, sizeof ng->force_names);
	ng->tileset = save->tileset;
	ng->width = save->width;
	ng->height = save->height;
}

/* --- the turns (net_mgr.cpp) --- */

/* ResetTurnState 0x4765a0 */
static void reset_turn_state(KpNetGame *ng)
{
	ng->extra_latency = 0;
	ng->in_game = 0;
	ng->turn = 0;
	ng->out_size = 0;
	memset(ng->turn_size, 0, sizeof ng->turn_size);
}

/* SetLobbyState 0x470910 */
static void set_lobby_state(KpNetGame *ng, uint8_t state)
{
	ng->lobby_state = state;
	if (state == KP_LOBBY_ENTERED) ng->countdown = 0;
}

/* QueueSyncCommand 0x47a300: the current ring record's 0x37 into the outgoing turn */
static int queue_turn_command(KpNetGame *ng, const uint8_t *data, uint32_t size);

static void queue_sync(KpNetGame *ng)
{
	uint8_t cmd[KP_SYNC_SIZE];
	kp_sync_command(&ng->sync, cmd);
	(void)queue_turn_command(ng, cmd, KP_SYNC_SIZE);
}

/* The outgoing turn sent (SendTurn 0x474830), an empty one as the keep-alive; in game the next turn starts with its
 * sync command (SendTurns 0x476500, FlushTurn 0x476da0) */
static void send_out(KpNetGame *ng)
{
	if (ng->out_size == 0) ng->out[ng->out_size++] = KP_LOBBY_KEEP_ALIVE;
	send_turn(ng, ng->out, ng->out_size);
	ng->out_size = 0;
	if (ng->in_game) queue_sync(ng);
}

/* SendTurns 0x476500 */
static void send_turns(KpNetGame *ng)
{
	uint32_t n = 0, target;
	if (!ng->net.turns_in_transit(ng->net.ud, &n)) {
		report_storm(ng, KP_NETTBL_NETWORK_ERROR, 1);
		return;
	}
	target = ng->transit + (ng->in_game ? ng->extra_latency : 0);
	while (n < target) {
		n++;
		send_out(ng);
	}
}

/* QueueTurnCommand 0x476c80: 1 queued, 0 dropped */
static int queue_turn_command(KpNetGame *ng, const uint8_t *data, uint32_t size)
{
	if (ng->out_size + size > ng->max_message) {
		uint32_t n = 0;
		if (!ng->in_game) return 0;
		if (!ng->net.turns_in_transit(ng->net.ud, &n)) {
			report_storm(ng, KP_NETTBL_NETWORK_ERROR, 1);
			return 0;
		}
		if (n >= TURN_MAX_IN_TRANSIT - ng->transit) return 0;
		send_out(ng); /* FlushTurn 0x476da0 */
		if (ng->out_size + size > sizeof ng->out) return 0; /* Koprulu's bound: the original copies past its buffer */
	}
	memcpy(ng->out + ng->out_size, data, size);
	ng->out_size += size;
	return 1;
}

static void handle_left_lobby(KpNetGame *ng, int id, uint32_t reason);
static void handle_left_game(KpNetGame *ng, KpGame *g, int id, uint32_t reason);

/* ProcessLeaveReasons 0x477da0: g the game in game (mode 3), NULL in the glue */
static void process_leave_reasons(KpNetGame *ng, KpGame *g)
{
	int id;
	for (id = 0; id < KP_NETGAME_PLAYERS; id++) {
		uint32_t reason = ng->leave_reasons[id];
		if (!reason) continue;
		if (g) handle_left_game(ng, g, id, reason);
		else if (!ng->in_game) handle_left_lobby(ng, id, reason);
		ng->leave_reasons[id] = 0;
	}
}

/* ReceiveTurns 0x4746a0 and 0x475ac0 (the leaves handled with the game RNG enabled) */
static int receive_turns(KpNetGame *ng, KpGame *g)
{
	uint32_t t = now(ng), e;
	if (ng->net.receive_turns(ng->net.ud, 0, KP_NETGAME_PLAYERS, ng->turn_data, ng->turn_size, ng->status)) {
		if (g) {
			uint32_t was = kp_rng_game_set_enabled(g->rng, 1);
			process_leave_reasons(ng, g);
			kp_rng_game_set_enabled(g->rng, was);
		} else {
			process_leave_reasons(ng, NULL);
		}
		ng->last_turn = t;
		ng->received = 1;
		return 1;
	}
	e = ng->net.error(ng->net.ud);
	if (e != ERROR_NO_DATA) report_error(ng, KP_NETTBL_NETWORK_ERROR, e, 1);
	ng->received = 0;
	return t - ng->last_turn < KP_NETGAME_ALIVE_MS;
}

static void execute_lobby_commands(KpNetGame *ng);

/* TurnManagerStep 0x476c00 before the game loop */
static int turn_step(KpNetGame *ng)
{
	receive_turns(ng, NULL);
	if (!ng->received) return 0;
	send_turns(ng);
	execute_lobby_commands(ng);
	ng->turn++;
	return 1;
}

/* ResolveDesync 0x47a630 */
static void resolve_desync(KpNetGame *ng)
{
	int id, present = 0, out = 0;
	for (id = 0; id < KP_NETGAME_PLAYERS; id++) {
		if (!(ng->status[id] & STATUS_PRESENT)) continue;
		present++;
		out += !(ng->status[id] & STATUS_RESPONDING);
	}
	if (present - out < out) {
		ng->drop_vote = 2;
		ng->net.disconnect_all(ng->net.ud, KP_NETGAME_DROP_DESYNC);
		return;
	}
	if (present - out == out) ng->drop_vote = 1;
	for (id = 0; id < KP_NETGAME_PLAYERS; id++)
		if ((ng->status[id] & STATUS_PRESENT) && !(ng->status[id] & STATUS_RESPONDING))
			drop_player(ng, id, KP_NETGAME_DROP_DESYNC);
}

/* --- the game's turns (net_mgr.cpp, net_misc.cpp; stage N4) --- */

#define LEAVE_COMMAND 0x57
#define LATENCY_COMMAND 0x55
#define LATENCY_MAX 2 /* Extra High */

/* The turn's list with one more command of `record`; 0, or -1 out of memory */
static int add_command(KpNetGame *ng, int64_t record, const uint8_t *cmd, size_t n)
{
	KpCommand *c;
	if (ng->list_count == ng->list_cap) {
		int cap = ng->list_cap ? 2 * ng->list_cap : 64;
		KpCommand *l = (KpCommand *)realloc(ng->list, (size_t)cap * sizeof *l);
		if (!l) return -1;
		ng->list = l;
		ng->list_cap = cap;
	}
	c = &ng->list[ng->list_count++];
	c->player = record;
	c->bytes = cmd;
	c->size = n;
	return 0;
}

/* HandlePlayerLeftGame 0x477a60 outside a replay: the leaver's record marked left (MarkNetPlayerLeft 0x4780c0, the
 * game's copy too), its {0x57, reason} recorded under its Storm ID (the turn's list: its record's command, which a live
 * game does not run), the game's side (kp_network_player_left) and the drop flag. The player count 0x5264c1 is
 * SNet's, which the interface asks. */
static void handle_left_game(KpNetGame *ng, KpGame *g, int id, uint32_t reason)
{
	int r = ng->storm_index[id];
	ng->players[id].state = 2;
	g->net_state[id] = KP_NET_LEFT;
	if (r < KP_NETGAME_PLAYERS) {
		ng->leave_cmd[id][0] = LEAVE_COMMAND;
		ng->leave_cmd[id][1] = (uint8_t)reason;
		if (add_command(ng, r, ng->leave_cmd[id], 2) != 0) ng->list_failed = 1;
	}
	if (kp_network_player_left(g, id, reason)) ng->drop_flags[id] = 1;
}

void kp_netgame_game_begin(KpNetGame *ng, KpGame *g)
{
	uint8_t state[KP_NETGAME_PLAYERS];
	int id;
	/* StartGame 0x443f40's steps 8 and 11: each Storm ID's record and the player it plays as (a team's leader) */
	for (id = 0; id < KP_NETGAME_PLAYERS; id++) {
		int64_t r = kp_setup_storm_slot(&g->setup, id);
		ng->storm_index[id] = r < 0 ? NO_SLOT : (uint8_t)r;
		ng->storm_slot[id] = r < 0 ? NO_SLOT : (uint8_t)kp_setup_record_player(&g->setup, r);
		state[id] = ng->players[id].state;
	}
	kp_set_net_state(g, state);
	if (!saving(ng)) { /* ClearDropVote 0x4775f0 */
		ng->drop_vote = 0;
		memset(ng->drop_flags, 0, sizeof ng->drop_flags);
	}
	ng->save_open = 0; /* CloseSaveFile 0x498a50: RunGameSession 0x467ba0 closes the save after StartGame */
	ng->desync = 0;
	ng->lagging = 0;
	ng->timeout_shown = 0;
	/* RunGame 0x445310's step 7: SetGameLoopActive 0x476e00 (the ring reset, its first 0x37), the turns from 0, the
	 * first turns sent */
	ng->in_game = 1;
	kp_sync_reset(&ng->sync, g, 1);
	queue_sync(ng);
	ng->turn = 0;
	send_turns(ng);
}

typedef struct {
	KpNetGame *ng;
	int id, count, failed;
	int64_t record, player;
} TurnSplit;

#define PLAYER_FLAG_SET 0x38
#define PLAYER_FLAG_CLEAR 0x39
#define PLAYER_FLAG 0x2

/* The local seat's record 0x508d54 (8 = none) */
static int64_t local_record(const KpNetGame *ng)
{
	return ng->local_storm >= 0 && ng->local_storm < KP_NETGAME_PLAYERS ? ng->storm_index[ng->local_storm] : NO_SLOT;
}

/* ExecuteTurnCommands 0x475d30's walk of a sender's turn: the keep-alive 0x05 and the sync 0x37 counted (0x672988),
 * the sync checked (a mismatch marks the sender out of sync); the latency 0x55's extra turns (OnLatencyCommand
 * 0x476d10: 0x672990, the network's side of the command, the game shows its line); ExecPlayerFlag 0x473b60's 0x38
 * and 0x39, which set and clear the flag 0x2 of the sender's network player (0x67381a) when the sender is another
 * seat and still active.
 * Koprulu's guards, not the original's, for a peer's bytes: a Storm ID without a record (8, the original's tables
 * read past their end) runs no game command, and a chat line 0x5c names its sender's record (the original shows the
 * text as the record byte 1 names; no seat sends its chat through the turns, kp_netgame_game_messages records it) */
static void split_command(void *ud, const uint8_t *cmd, size_t n)
{
	TurnSplit *t = (TurnSplit *)ud;
	KpNetGame *ng = t->ng;
	if (cmd[0] == LATENCY_COMMAND && n >= 2) ng->extra_latency = cmd[1] < LATENCY_MAX ? cmd[1] : LATENCY_MAX;
	if (cmd[0] == KP_LOBBY_KEEP_ALIVE) {
		t->count++;
	} else if (cmd[0] == KP_SYNC_ID) {
		if (!ng->config.replay && !kp_sync_check(&ng->sync, cmd, t->player)) {
			ng->desync = 1;
			ng->status[t->id] = STATUS_PRESENT;
		}
		t->count++;
	} else if (cmd[0] == PLAYER_FLAG_SET || cmd[0] == PLAYER_FLAG_CLEAR) {
		KpNetPlayer *p = &ng->players[t->id];
		if (t->record != local_record(ng) && p->state == 1) {
			if (cmd[0] == PLAYER_FLAG_SET) p->flags |= PLAYER_FLAG;
			else p->flags &= (uint16_t)~PLAYER_FLAG;
		}
	}
	if (t->record >= KP_NETGAME_PLAYERS) return;
	if (cmd[0] == KP_NETGAME_CHAT_COMMAND && cmd[1] != t->record) return;
	if (add_command(ng, t->record, cmd, n) != 0) t->failed = 1;
}

/* ExecuteGameTurn 0x4768a0's commands: the Storm IDs with a turn from 7 down to 0, each as its record (0x508d48) and
 * player (0x508d44); outside a replay a turn whose keep-alive and sync commands are not exactly one marks its sender out
 * of sync */
static int game_commands(KpNetGame *ng)
{
	TurnSplit t;
	int id;
	ng->desync = 0;
	for (id = KP_NETGAME_PLAYERS - 1; id >= 0; id--) {
		if (!(ng->status[id] & STATUS_HAS_TURN)) continue;
		memset(&t, 0, sizeof t);
		t.ng = ng;
		t.id = id;
		t.record = ng->storm_index[id];
		t.player = ng->storm_slot[id];
		if (ng->turn_size[id]) (void)kp_record_split(ng->turn_data[id], ng->turn_size[id], split_command, &t);
		if (t.failed) return -1;
		if (!ng->config.replay && t.count != 1) {
			ng->desync = 1;
			ng->status[id] = STATUS_PRESENT;
		}
	}
	return 0;
}

#define TIMEOUT_SECONDS 45
#define TIMEOUT_MEMORY_MS 120000 /* a dialog closed this recently gives the next one what it had left, less 2 s */
#define TIMEOUT_REOPEN_LOSS 2
#define TIMEOUT_TICK_MS 1000

/* OpenTimeoutDialog 0x47aaa0, then WaitForLaggingPlayers' t0 */
static void open_timeout(KpNetGame *ng)
{
	uint32_t t = now(ng);
	if (!ng->timeout_shown || t - ng->timeout_closed > TIMEOUT_MEMORY_MS)
		ng->timeout_seconds = TIMEOUT_SECONDS;
	else
		ng->timeout_seconds = ng->timeout_seconds > TIMEOUT_REOPEN_LOSS ? ng->timeout_seconds - TIMEOUT_REOPEN_LOSS : 0;
	ng->timeout_shown = 1;
	ng->lagging = 1;
	ng->lag_clock = now(ng);
}

/* A pass of the wait without a turn: TickTimeoutCountdown 0x47ab00 a second after the last tick */
static void tick_timeout(KpNetGame *ng)
{
	uint32_t t = now(ng);
	if (t - ng->lag_clock <= TIMEOUT_TICK_MS) return;
	if (ng->timeout_seconds) ng->timeout_seconds--;
	ng->lag_clock = t;
}

int kp_netgame_timeout_drop(KpNetGame *ng)
{
	if (!ng->lagging || ng->timeout_seconds) return 0;
	resolve_desync(ng);
	return 1;
}

int kp_netgame_lagging_slots(const KpNetGame *ng, int slots[KP_NETGAME_PLAYERS])
{
	int s, n = 0;
	for (s = 0; s < KP_NETGAME_PLAYERS; s++) {
		int id = ng->slots[s].storm;
		if (ng->slots[s].type != KP_CTRL_HUMAN || id < 0 || id >= KP_NETGAME_PLAYERS) continue;
		if ((ng->status[id] & STATUS_PRESENT) && !(ng->status[id] & STATUS_HAS_TURN)) slots[n++] = s;
	}
	return n;
}

int kp_netgame_game_results(const KpNetGame *ng, KpGame *g, uint8_t out[KP_NETGAME_PLAYERS])
{
	/* the committed results 0x5349e0 of each Storm ID's game player (0x672878): 1 left 4, 2 defeat 2, 3 victory 1, 5
	 * draw 3, the others 0 */
	static const uint8_t BY_STATE[6] = { 0, KP_NETGAME_DISCONNECT, KP_NETGAME_LOSS, KP_NETGAME_WIN, 0, KP_NETGAME_DRAW };
	int player[KP_NETGAME_PLAYERS], i, active = 0, others = 0, other = 0, local = ng->local_storm, local_result;
	const uint8_t *t = g->setup.template_record;
	for (i = 0; i < KP_NETGAME_PLAYERS; i++) {
		int64_t slot = kp_setup_storm_slot(&g->setup, i);
		player[i] = slot >= 0 ? (int)kp_setup_record_player(&g->setup, slot) : KP_NETGAME_PLAYERS;
		out[i] = 0;
		if (player[i] < 0 || player[i] >= KP_NETGAME_PLAYERS) continue;
		active++;
		if (g->results[player[i]] >= 0 && g->results[player[i]] < 6) out[i] = BY_STATE[g->results[player[i]]];
	}
	if (local < 0 || local >= KP_NETGAME_PLAYERS) return KP_NETGAME_LOSS;
	if (out[local] == 0) out[local] = KP_NETGAME_LOSS;
	local_result = out[local];
	/* one player, Use Map Settings outside the server templates (type 0x80 with a sub-byte) and the types 0x11..0x16,
	 * a replay: a draw for everyone, the local result kept */
	if (active == 1 || (kp_setup_ums(&g->setup) && !(t[0] == 0x80 && t[1] != 0) && (uint8_t)(t[0] - 0x11) > 5)
		|| g->w->replay) {
		for (i = 0; i < KP_NETGAME_PLAYERS; i++) {
			if (player[i] < KP_NETGAME_PLAYERS) out[i] = KP_NETGAME_DRAW;
		}
		return local_result;
	}
	/* ResolveDesync's vote (0x529041, the flags 0x529042): 2 this seat in the minority, 1 even */
	for (i = 0; i < KP_NETGAME_PLAYERS; i++) {
		if (ng->drop_flags[i]) out[i] = ng->drop_vote == 2 ? 0 : KP_NETGAME_DISCONNECT;
	}
	if (ng->drop_vote == 2) out[local] = KP_NETGAME_DISCONNECT;
	else if (ng->drop_vote == 1) out[local] = 0;
	/* the last other human standing without a result wins (no computers in the game) */
	for (i = 0; i < KP_NETGAME_PLAYERS; i++) {
		if (i != local && kp_netgame_player_active(ng, i)) {
			others++;
			other = i;
		}
	}
	if (others == 1) {
		int k, computers = 0;
		for (k = 0; k < KP_NETGAME_PLAYERS; k++) {
			computers += g->setup.type[k] == KP_CTRL_COMPUTER || g->setup.type[k] == KP_CTRL_LOBBY_COMPUTER;
		}
		if (computers == 0 && player[other] < KP_NETGAME_PLAYERS && g->results[player[other]] == 0) {
			out[other] = KP_NETGAME_WIN;
			g->results[player[other]] = 3;
		}
	}
	return out[local];
}

void kp_netgame_game_end(KpNetGame *ng)
{
	ng->lagging = 0;
	leave_storm(ng, KP_NETGAME_LEAVE_CODE);
}

int kp_netgame_game_turn(KpNetGame *ng, KpGame *g, const KpCommand **list, int *count)
{
	int alive;
	*list = NULL;
	*count = 0;
	ng->list_count = 0;
	ng->list_failed = 0;
	alive = receive_turns(ng, g);
	if (!ng->received) {
		/* WaitForLaggingPlayers 0x47a810: the turns are overdue (2.5 s without one); it waits until one arrives */
		if (ng->lagging) tick_timeout(ng);
		else if (!alive) open_timeout(ng);
		return KP_NETGAME_WAIT;
	}
	if (ng->lagging) { /* the dialog closed */
		ng->lagging = 0;
		ng->timeout_closed = now(ng);
	}
	send_turns(ng);
	if (ng->list_failed || game_commands(ng) != 0) return KP_NETGAME_NO_MEMORY;
	*list = ng->list;
	*count = ng->list_count;
	return KP_NETGAME_TURN;
}

void kp_netgame_game_turn_end(KpNetGame *ng, KpGame *g, int poll)
{
	if (ng->desync) resolve_desync(ng);
	ng->turn++;
	kp_sync_advance(&ng->sync, g);
	if (poll) kp_sync_capture_minimap(&ng->sync, g);
}

void kp_netgame_game_draw(KpNetGame *ng, KpGame *g, int64_t screen_tile_y)
{
	kp_sync_capture_sprites(&ng->sync, g, kp_sync_view_row(screen_tile_y));
}

int kp_netgame_command(KpNetGame *ng, const uint8_t *bytes, size_t size)
{
	if (size == 0 || size > ng->max_message) return -1;
	return queue_turn_command(ng, bytes, (uint32_t)size) ? 0 : -1;
}


/* HandleChatMessage 0x475b40: the bytes the message used, 0 refused */
static uint32_t game_chat(KpNetGame *ng, KpGame *g, int sender, const uint8_t *m, uint32_t n, int type,
	void (*record)(void *ud, const uint8_t *cmd, size_t size), void *ud)
{
	const char *text = (const char *)m + GAME_CHAT_TEXT;
	uint32_t len;
	int r;
	if (type != GAME_CHAT && type != GAME_CHAT_FROM) return 0; /* 0x4be940 */
	if (!kp_netgame_player_active(ng, sender) || n < GAME_CHAT_TEXT) return 0;
	len = kp_netgame_net_string(text, GAME_CHAT_MAX, n - GAME_CHAT_TEXT, 0, 0);
	if (!len) return 0;
	r = ng->storm_index[sender];
	if (!(ng->chat_mask & (1u << sender)) || r >= KP_NETGAME_PLAYERS) return len + GAME_CHAT_TEXT;
	if (type == GAME_CHAT) {
		/* the original's command is a stack buffer: the bytes after the text's terminator are what it held */
		uint8_t cmd[KP_NETGAME_CHAT_COMMAND_SIZE];
		memset(cmd, 0, sizeof cmd);
		cmd[0] = KP_NETGAME_CHAT_COMMAND;
		cmd[1] = (uint8_t)r;
		copy((char *)cmd + GAME_CHAT_TEXT, GAME_CHAT_MAX, text);
		if (record) record(ud, cmd, sizeof cmd);
		kp_chat_display(g, (const char *)cmd + GAME_CHAT_TEXT, r, 0);
	} else {
		char line[GAME_LINE];
		format1(line, sizeof line, text_of(&ng->hooks, KP_NETTBL_FROM), kp_netgame_player_name(ng, sender));
		append(line, sizeof line, " ");
		append(line, sizeof line, text);
		timed(ng, line);
	}
	return len + GAME_CHAT_TEXT;
}

void kp_netgame_game_messages(KpNetGame *ng, KpGame *g, void (*record)(void *ud, const uint8_t *cmd, size_t size),
	void *ud)
{
	int sender = -1;
	const uint8_t *m = NULL;
	uint32_t n = 0;
	while (receive_message(ng, &sender, &m, &n)) {
		while (m && n != 0 && m[0] < GAME_MESSAGE_TYPES) {
			uint32_t used = game_chat(ng, g, sender, m, n, m[0], record, ud);
			if (used == 0 || used > n) break;
			m += used;
			n -= used;
		}
	}
}

/* --- the lobby messages (net_chat_cmds.cpp, P25 group 1) --- */

static void pend(KpNetGame *ng, int kind, const uint8_t *data, uint8_t size)
{
	memcpy(ng->pending[kind], data, size);
	ng->pending_size[kind] = size;
}

/* QueueSlotUpdate 0x4715d0 */
static void queue_slot_update(KpNetGame *ng, int s)
{
	const KpNetSlot *r = &ng->slots[s];
	int i = r->slot;
	uint8_t *m;
	if (i < 0 || i >= 8) return; /* the original indexes its buffers with +0 unchecked */
	m = ng->pending_slot[i];
	m[0] = KP_LOBBY_SLOT;
	m[1] = (uint8_t)i;
	m[2] = (uint8_t)r->storm;
	m[3] = r->type;
	m[4] = r->race;
	m[5] = r->force;
	ng->pending_slots |= (uint8_t)(1u << i);
}

/* QueuePlayerInfo 0x471630 */
static void queue_player_info(KpNetGame *ng, int s)
{
	int id = ng->slots[s].storm;
	const KpNetPlayer *p;
	uint8_t *m;
	if ((uint32_t)id >= KP_NETGAME_PLAYERS) return;
	p = &ng->players[id];
	m = ng->pending_info[id];
	memset(m, 0, 8);
	m[0] = KP_LOBBY_PLAYER_INFO;
	m[1] = (uint8_t)id;
	if (p->state == 1) {
		kp_net_put16(m + 2, p->flags);
		kp_net_put16(m + 4, p->w4);
		kp_net_put16(m + 6, p->version);
	}
	ng->pending_infos |= (uint8_t)(1u << id);
}

/* QueueDownloadStatus 0x471590 */
static void queue_download(KpNetGame *ng, uint8_t pct)
{
	uint8_t m[2];
	if (pct == ng->last_download) return;
	ng->last_download = pct;
	m[0] = KP_LOBBY_DOWNLOAD;
	m[1] = pct;
	pend(ng, PEND_DOWNLOAD, m, sizeof m);
}

/* FlushLobbyMessages 0x470460 */
static void flush_lobby(KpNetGame *ng)
{
	int k, i;
	if (ng->lobby_state == KP_LOBBY_START_INFO) ng->lobby_state = KP_LOBBY_WAITING;
	else if (ng->lobby_state > KP_LOBBY_LOCKED) return;
	for (k = PEND_START; k <= PEND_JOIN; k++) {
		if (k == PEND_RACE) {
			for (i = 7; i >= 0; i--)
				if (ng->pending_slots & (1u << i)) queue_turn_command(ng, ng->pending_slot[i], 6);
			for (i = 7; i >= 0; i--)
				if (ng->pending_infos & (1u << i)) queue_turn_command(ng, ng->pending_info[i], 8);
			ng->pending_slots = ng->pending_infos = 0;
		}
		if (ng->pending_size[k]) queue_turn_command(ng, ng->pending[k], ng->pending_size[k]);
		ng->pending_size[k] = 0;
	}
}

/* DiscardLobbyMessages 0x470730 */
static void discard_lobby(KpNetGame *ng)
{
	memset(ng->pending_size, 0, sizeof ng->pending_size);
	ng->pending_slots = ng->pending_infos = 0;
}

/* BuildJoinRequest 0x4714b0 */
static void on_join_request(KpNetGame *ng, const uint8_t *m, int sender, int local);
static void build_join_request(KpNetGame *ng, uint8_t save_slot, uint8_t save_record, uint32_t rng, int local)
{
	uint8_t m[KP_NETGAME_JOIN_SIZE];
	m[0] = KP_LOBBY_JOIN;
	m[1] = save_slot;
	m[2] = save_record;
	kp_net_put32(m + 3, rng);
	kp_net_put16(m + 7, 0); /* GetLocalNetFlags 0x477fb0 */
	kp_net_put16(m + 9, 1);
	kp_net_put16(m + 0xb, KP_NETGAME_LOBBY_VERSION);
	m[0xd] = 0; /* GetSpawnFlag 0x4635c0: Koprulu is never a spawned copy */
	if (ng->hooks.key_draws) ng->hooks.key_draws(ng->hooks.ud, 1);
	kp_net_put32(m + 0xe, ng->config.key_hash);
	if (local) on_join_request(ng, m, ng->local_storm, 1);
	else pend(ng, PEND_JOIN, m, sizeof m);
}

/* BuildStartInfoMessage 0x471410 */
static void build_start_info(KpNetGame *ng)
{
	uint8_t m[KP_NETGAME_START_INFO];
	int s;
	m[0] = KP_LOBBY_START_INFO_MSG;
	kp_net_put32(m + 1, ng->hooks.time ? ng->hooks.time(ng->hooks.ud) : 0);
	if (ng->config.replay) memcpy(m, ng->config.replay_seed, sizeof m);
	for (s = 7; s >= 0; s--) m[5 + s] = ng->saved_slots[s];
	pend(ng, PEND_START_INFO, m, sizeof m);
}

/* StartLobbyCountdown 0x47a160 and StopLobbyCountdown 0x47a210 */
static void start_countdown(KpNetGame *ng)
{
	ng->countdown_shown = 1;
	if (ng->hooks.countdown) ng->hooks.countdown(ng->hooks.ud, KP_NETGAME_COUNTDOWN_SECONDS);
}
static void stop_countdown(KpNetGame *ng)
{
	if (!ng->countdown_shown) return;
	ng->countdown_shown = 0;
	if (ng->hooks.countdown) ng->hooks.countdown(ng->hooks.ud, -1);
}

/* LobbyCountdownTick 0x470930 */
static void countdown_tick(KpNetGame *ng)
{
	uint32_t c = ng->countdown;
	if (!c) return;
	ng->countdown = --c;
	if (c == 2 * ng->transit + 4) {
		if (ng->hooks.lock) ng->hooks.lock(ng->hooks.ud);
		ng->lobby_state = KP_LOBBY_LOCKED;
	}
	if (c == ng->transit + 2) {
		if (ng->host) {
			ng->lobby_state = KP_LOBBY_START_INFO;
			build_start_info(ng);
		} else {
			ng->lobby_state = KP_LOBBY_WAITING;
		}
	}
	if (c == 0) ng->lobby_state = KP_LOBBY_START;
}

/* --- the lobby messages' handlers --- */

/* SetGameJoinable over CountOpenPlayerSlots, the host's step after a slot change */
static void update_joinable(KpNetGame *ng) { set_joinable(ng, (uint8_t)kp_netgame_open_slots(ng) != 0); }

/* RejectChatRoomPlayer 0x471dd0 */
static void reject(KpNetGame *ng, int id, uint8_t reason)
{
	uint8_t m[2];
	int k;
	m[0] = KP_CHAT_LEAVE;
	m[1] = reason;
	for (k = 0; k < 3; k++) send_message(ng, id, m, sizeof m);
	drop_player(ng, id, DROP_KICK);
}

/* OnLobbyJoinRequest 0x470af0 */
static void on_join_request(KpNetGame *ng, const uint8_t *m, int sender, int local)
{
	int chosen, s;
	KpNetSlot *r;
	ng->last_download = NONE_BYTE;
	if (!ng->host) return;
	if (ng->lobby_state != KP_LOBBY_IN && !local) {
		reject(ng, sender, KP_LEAVE_UNABLE);
		return;
	}
	if (kp_net_get16(m + 0xb) != KP_NETGAME_LOBBY_VERSION) {
		reject(ng, sender, KP_LEAVE_VERSION);
		return;
	}
	if (m[0xd] && kp_net_get32(m + 0xe) != ng->config.key_hash) {
		reject(ng, sender, KP_LEAVE_SPAWN);
		return;
	}
	if (saving(ng) && (m[1] >= 8 || m[2] >= 8 || kp_net_get32(m + 3) != ng->save_rng)) {
		reject(ng, sender, KP_LEAVE_NO_SAVE);
		return;
	}
	if (kp_netgame_ums(ng) && !saving(ng)) chosen = first_slot_of_most_open_force(ng);
	else if (saving(ng) && (kp_netgame_team_game(ng) || kp_netgame_ums(ng))) chosen = m[1];
	else chosen = find_free_join_slot(ng);
	for (s = 7; s >= 0; s--)
		if (ng->slots[s].storm == sender) {
			clear_saved_slot(ng, s);
			ng->slots[s].type = KP_CTRL_OPEN;
			chosen = s;
		}
	if (chosen >= 8 || ng->slots[chosen].type != KP_CTRL_OPEN ||
		(saving(ng) && !assign_saved_slot(ng, chosen, m[2], 0))) {
		reject(ng, sender, KP_LEAVE_CLOSED);
		return;
	}
	r = &ng->slots[chosen];
	r->slot = chosen;
	r->storm = sender;
	r->type = KP_CTRL_HUMAN;
	net_player_name(ng, sender, r->name, sizeof r->name);
	if (saving(ng)) r->race = ng->save.races[m[1]];
	rebuild_storm_maps(ng, ng->local_storm);
	refresh(ng);
	add_net_player(ng, sender, (uint16_t)((kp_net_get16(m + 7) & 0xff03) | (m[0xd] ? 0x100 : 0)),
		(uint16_t)kp_net_get16(m + 9), (uint16_t)kp_net_get16(m + 0xb));
	if (!local) {
		uint8_t notice[CHAT_JOINED_SIZE];
		for (s = 7; s >= 0; s--) queue_slot_update(ng, s);
		for (s = 7; s >= 0; s--) queue_player_info(ng, s);
		notice[0] = KP_CHAT_JOINED; /* BroadcastJoinNotice 0x471f60 */
		kp_net_put32(notice + 1, (uint32_t)sender);
		send_message(ng, -1, notice, sizeof notice);
	} else {
		ng->lobby_state = KP_LOBBY_IN;
	}
	if (ng->offer) {
		kp_transfer_add_peer(&ng->transfer, ng->offer, sender);
		kp_transfer_clear_pending(ng->offer, 1u << sender);
	}
	update_joinable(ng);
}

/* OnLobbySlotUpdate 0x470db0 */
static void on_slot_update(KpNetGame *ng, const uint8_t *m, int sender)
{
	KpNetSlot *r;
	int id;
	if (sender != 0 || m[1] >= 8 || !(m[2] < 8 || m[2] == NONE_BYTE) || ng->host) return;
	id = m[2] == NONE_BYTE ? -1 : m[2];
	if (m[3] == KP_CTRL_HUMAN && !net_player_exists(ng, id)) return;
	if (ng->lobby_state == KP_LOBBY_REQUESTED) ng->lobby_state = KP_LOBBY_IN;
	else if (ng->lobby_state < KP_LOBBY_REQUESTED) return;
	r = &ng->slots[m[1]];
	memset(r, 0, sizeof *r);
	r->slot = m[1];
	r->storm = id;
	r->type = m[3];
	r->race = m[4];
	r->force = m[5];
	if (m[3] == KP_CTRL_HUMAN) net_player_name(ng, id, r->name, sizeof r->name);
	rebuild_storm_maps(ng, ng->local_storm);
	refresh(ng);
}

/* OnLobbyPlayerInfo 0x470ee0 */
static void on_player_info(KpNetGame *ng, const uint8_t *m, int sender)
{
	int id = m[1];
	uint16_t flags;
	if (ng->lobby_state < KP_LOBBY_IN || sender != 0 || id >= 8 || ng->host) return;
	flags = (uint16_t)(kp_net_get16(m + 2) & 0xff03);
	if (ng->players[id].state == 1) flags |= ng->players[id].flags;
	add_net_player(ng, id, flags, (uint16_t)kp_net_get16(m + 4), (uint16_t)kp_net_get16(m + 6));
}

/* OnLobbyRaceChange 0x470f60 */
static void on_race_change(KpNetGame *ng, const uint8_t *m, int sender)
{
	int q, target, f, j;
	if (!ng->host || ng->lobby_state < KP_LOBBY_IN) return;
	q = ng->storm_slot[sender];
	target = m[1] == 8 ? (uint8_t)q : m[1];
	if (!kp_netgame_can_set_race(ng, target, m[2], q)) return;
	f = ng->slots[target].force;
	if (kp_netgame_team_game(ng) && force_has_computer(ng, f)) {
		for (j = 0; j < 8; j++) {
			if (ng->slots[j].force != f) continue;
			ng->slots[j].race = m[2];
			queue_slot_update(ng, j);
		}
	} else {
		ng->slots[target].race = m[2];
		queue_slot_update(ng, target);
	}
	refresh(ng);
}

/* OnLobbySlotAction 0x471020 */
static void on_slot_action(KpNetGame *ng, const uint8_t *m, int sender)
{
	int s = m[1], h, j, f;
	if (!ng->host || ng->lobby_state < KP_LOBBY_IN || s >= 8 || m[2] >= 3 || sender != 0) return;
	h = ng->storm_slot[0];
	if (h >= 8) return;
	if (m[2] == KP_SLOT_COMPUTER) set_computer_slot(ng, s, h);
	else close_or_open_slot(ng, s, h, m[2] == KP_SLOT_OPEN ? KP_CTRL_OPEN : KP_CTRL_CLOSED);
	f = ng->slots[s].force;
	if (kp_netgame_team_game(ng)) {
		for (j = 7; j >= 0; j--)
			if (ng->slots[j].force == f) queue_slot_update(ng, j);
	} else {
		queue_slot_update(ng, s);
	}
	refresh(ng);
	update_joinable(ng);
}

/* OnLobbySwapSlots 0x471150 */
static void on_swap(KpNetGame *ng, const uint8_t *m, int sender)
{
	int a = m[1], b = m[2];
	KpNetSlot t;
	if ((a == ng->local_index || b == ng->local_index) && ng->hooks.swap) ng->hooks.swap(ng->hooks.ud, a, b);
	if (!ng->host || sender != 0 || ng->lobby_state < KP_LOBBY_IN || a < 1 || a > 7 || b < 1 || b > 7 || a == b) return;
	t = ng->slots[a];
	ng->slots[a] = ng->slots[b];
	ng->slots[a].slot = a;
	ng->slots[a].force = t.force;
	t.slot = b;
	t.force = ng->slots[b].force;
	ng->slots[b] = t;
	rebuild_storm_maps(ng, ng->local_storm);
	queue_slot_update(ng, b);
	queue_slot_update(ng, a);
	refresh(ng);
}

/* OnLobbyStartGame 0x4712d0 (IsLadderMapAllowed 0x477e90 is Battle.net's: always allowed) */
static void on_start_game(KpNetGame *ng, int sender)
{
	uint32_t c;
	if (ng->lobby_state != KP_LOBBY_IN) {
		set_leave(ng, KP_LEAVE_UNABLE);
		return;
	}
	if (sender != 0) return;
	if (ng->config.replay) memcpy(ng->replay_slots, ng->slots, sizeof ng->slots);
	ng->lobby_state = KP_LOBBY_STARTING;
	c = 2 * ng->transit + 4;
	ng->countdown = c > KP_NETGAME_COUNTDOWN ? c : KP_NETGAME_COUNTDOWN;
	start_countdown(ng);
}

/* OnLobbyTeamChange 0x471340 */
static void on_team_change(KpNetGame *ng, const uint8_t *m, int sender)
{
	int t = m[1], s, d;
	if (!ng->host || ng->lobby_state < KP_LOBBY_IN || !kp_netgame_team_game(ng)) return;
	if (t == 0 || t > team_count(ng) || force_has_computer(ng, t)) return;
	s = ng->storm_slot[sender];
	if (s >= 8 || ng->slots[s].force == t || ng->slots[s].type != KP_CTRL_HUMAN) return;
	d = first_open_slot_of_force(ng, t);
	if (d >= 8 || !can_change_force(ng, s, d)) return;
	move_slot(ng, s, d);
	queue_slot_update(ng, s);
	queue_slot_update(ng, d);
	refresh(ng);
}

/* OnLobbyForceChangeRequest 0x470a30 */
static void on_force_change(KpNetGame *ng, const uint8_t *m, int sender)
{
	int f = m[1], s, target;
	if (!ng->host || ng->lobby_state < KP_LOBBY_IN || !kp_netgame_ums(ng)) return;
	if (f < 1 || f > 4 || force_has_computer_slot(ng, f)) return;
	s = ng->storm_slot[sender];
	if (s >= 8 || ng->slots[s].type != KP_CTRL_HUMAN) return;
	target = ng->slots[s].force == f ? next_open_slot_of_force(ng, f, s) : first_strict_open_slot_of_force(ng, f);
	if (target >= 8) return;
	move_slot(ng, s, target);
	queue_slot_update(ng, s);
	queue_slot_update(ng, target);
	refresh(ng);
}

/* OnLobbyStartInfo 0x4709d0 */
static void on_start_info(KpNetGame *ng, const uint8_t *m, int sender)
{
	int s;
	if (sender != 0 || ng->lobby_state != KP_LOBBY_WAITING) return;
	if (!ng->config.replay) memcpy(ng->start_info, m, KP_NETGAME_START_INFO);
	ng->seed = kp_net_get32(m + 1);
	for (s = 7; s >= 0; s--) {
		uint8_t v = m[5 + s];
		if (v > NO_SLOT) {
			report(ng, KP_NETTBL_LAUNCH, NULL, 1);
			return;
		}
		assign_saved_slot(ng, s, v, 1);
	}
}

/* MakeGamePublic 0x477860 */
static void make_public(KpNetGame *ng)
{
	uint32_t mode = 0, size = 0;
	if (!ng->host) return;
	if (!ng->net.game_info(ng->net.ud, KP_SNET_INFO_MODE, &mode, sizeof mode, &size)) {
		report_storm(ng, KP_NETTBL_NETWORK_ERROR, 1);
		return;
	}
	if (!ng->net.set_game_mode(ng->net.ud, mode, 1)) report_storm(ng, KP_NETTBL_NETWORK_ERROR, 1);
	update_joinable(ng);
}

/* The lobby commands' lengths (ExecuteLobbyCommands 0x476650), 0 stops the player */
static uint32_t lobby_command_size(uint8_t c)
{
	switch (c) {
	case KP_LOBBY_KEEP_ALIVE: case KP_LOBBY_START_GAME: case KP_LOBBY_READY: case KP_LOBBY_PUBLIC: return 1;
	case KP_LOBBY_SQUELCH: case KP_LOBBY_UNSQUELCH: case KP_LOBBY_DOWNLOAD: case KP_LOBBY_TEAM: case KP_LOBBY_FORCE:
		return 2;
	case KP_LOBBY_RACE: case KP_LOBBY_SLOT_ACTION: case KP_LOBBY_SWAP: return 3;
	case KP_LOBBY_SLOT: return 6;
	case KP_LOBBY_PLAYER_INFO: return 8;
	case KP_LOBBY_START_INFO_MSG: return KP_NETGAME_START_INFO;
	case KP_LOBBY_JOIN: return KP_NETGAME_JOIN_SIZE;
	default: return 0;
	}
}

/* ExecuteLobbyCommands 0x476650 */
static void execute_lobby_commands(KpNetGame *ng)
{
	int id;
	for (id = KP_NETGAME_PLAYERS - 1; id >= 0; id--) {
		const uint8_t *d = ng->turn_data[id];
		uint32_t size = ng->turn_size[id], at = 0;
		if (!(ng->status[id] & STATUS_HAS_TURN) || !size || !d) continue;
		while (at < size) {
			const uint8_t *m = d + at;
			uint32_t n;
			if ((uint8_t)(m[0] - LOBBY_COMMAND_FIRST) > LOBBY_COMMAND_SPAN) break;
			n = lobby_command_size(m[0]);
			if (!n || size - at < n) break;
			switch (m[0]) {
			case KP_LOBBY_START_GAME: on_start_game(ng, id); break;
			case KP_LOBBY_DOWNLOAD:
				if (ng->hooks.download) ng->hooks.download(ng->hooks.ud, id, m[1] > 100 ? 0 : m[1]);
				break;
			case KP_LOBBY_SLOT: on_slot_update(ng, m, id); break;
			case KP_LOBBY_PLAYER_INFO: on_player_info(ng, m, id); break;
			case KP_LOBBY_JOIN: on_join_request(ng, m, id, 0); break;
			case KP_LOBBY_RACE: on_race_change(ng, m, id); break;
			case KP_LOBBY_TEAM: on_team_change(ng, m, id); break;
			case KP_LOBBY_FORCE: on_force_change(ng, m, id); break;
			case KP_LOBBY_SLOT_ACTION: on_slot_action(ng, m, id); break;
			case KP_LOBBY_SWAP: on_swap(ng, m, id); break;
			case KP_LOBBY_START_INFO_MSG: on_start_info(ng, m, id); break;
			case KP_LOBBY_READY: ng->ready[id] = 1; break;
			case KP_LOBBY_PUBLIC: /* OnLobbyMakePublic 0x470d80 */
				if (id == 0) {
					make_public(ng);
					if (ng->hooks.password_refresh) ng->hooks.password_refresh(ng->hooks.ud);
				}
				break;
			default: break; /* 5, and the voice squelch 0x3a/0x3b's no-op */
			}
			at += n;
		}
	}
}

/* --- the chat room's direct messages (net_chrm.cpp, P25 group 2) --- */

/* AddLobbyChatMessage 0x451560 */
static void add_chat(KpNetGame *ng, int sender, const char *text)
{
	char line[LINE_SIZE];
	int kind;
	line[0] = 0;
	if (sender == ng->local_storm || (sender >= 0 && sender < KP_NETGAME_PLAYERS)) {
		kind = sender == ng->local_storm ? KP_LINE_OWN : KP_LINE_PLAYER;
		copy(line, sizeof line, kp_netgame_player_name(ng, sender));
		append(line, sizeof line, ": ");
		{
			char t[KP_NETGAME_CHAT];
			copy(t, sizeof t, text);
			append(line, sizeof line, t);
		}
	} else if (sender == 8 || sender == -1) {
		kind = sender == 8 ? KP_LINE_SYSTEM : KP_LINE_PLAIN;
		copy(line, KP_NETGAME_CHAT, text);
	} else {
		return;
	}
	if (ng->hooks.line) ng->hooks.line(ng->hooks.ud, kind, line);
}

/* OnChatPlayerJoined 0x471c80 */
static void on_chat_joined(KpNetGame *ng, const uint8_t *m, int sender)
{
	uint32_t id = kp_net_get32(m + 1);
	char name[KP_NETGAME_NAME], line[CHAT_LINE_FORMAT];
	if (sender != 0 || id >= KP_NETGAME_PLAYERS || (int)id == ng->local_storm) return;
	if (!ng->net.player_name(ng->net.ud, (int)id, name, sizeof name)) return;
	name[sizeof name - 1] = 0;
	if (!name[0]) return;
	format1(line, sizeof line, text_of(&ng->hooks, KP_NETTBL_JOINED), name);
	add_chat(ng, 8, line);
}

/* OnChatGameInfo 0x471ae0 */
static void on_game_info(KpNetGame *ng, const uint8_t *m, int sender)
{
	int i, t, n, k;
	if (ng->host || sender != 0) return;
	ng->tileset = (uint16_t)kp_net_get16(m + 1);
	ng->width = (uint16_t)kp_net_get16(m + 3);
	ng->height = (uint16_t)kp_net_get16(m + 5);
	memset(ng->setup, 0, sizeof ng->setup);
	for (i = KP_NETGAME_SLOTS - 1; i >= 0; i--) {
		ng->setup[i].slot = i;
		ng->setup[i].storm = -1;
		ng->setup[i].type = m[7 + i];
		ng->setup[i].race = m[0x13 + i];
		ng->setup[i].force = m[0x2b + i];
	}
	if (kp_netgame_team_game(ng))
		for (t = 0, k = 0; t < team_count(ng); t++)
			for (n = 0; n < team_size(ng); n++, k++)
				if (k < 8) ng->setup[k].force = (uint8_t)(t + 1);
	memcpy(ng->setup_types, m + 0x1f, KP_NETGAME_SLOTS);
	memcpy(ng->forces, m + 0x2b, KP_NETGAME_SLOTS);
	memcpy(ng->user_select, m + 0x37, sizeof ng->user_select);
	/* OnChatGameInfo 0x471ae0: 0x526490 and GetSavedLocalPlayer 0x499850 when a save is open */
	if (saving(ng)) build_join_request(ng, ng->save.local_slot, (uint8_t)ng->save.local_player, ng->save_rng, 0);
	else build_join_request(ng, 0, 0, ng->save_rng, 0);
	set_lobby_state(ng, KP_LOBBY_REQUESTED);
}

/* OnChatForceNames 0x471a90 */
static void on_force_names(KpNetGame *ng, const uint8_t *m, int sender)
{
	int f;
	if (ng->host || sender != 0 || !kp_netgame_ums(ng)) return;
	for (f = 4; f >= 1; f--) {
		char name[KP_NETGAME_FORCE_NAME + 1];
		memcpy(name, m + 1 + KP_NETGAME_FORCE_NAME * (f - 1), KP_NETGAME_FORCE_NAME);
		name[KP_NETGAME_FORCE_NAME] = 0;
		copy(ng->force_names[f - 1], KP_NETGAME_FORCE_NAME, name); /* SetForceName 0x443650 */
	}
	if (ng->hooks.force_names) ng->hooks.force_names(ng->hooks.ud);
}

/* OnChatWhisper 0x471d30 */
static void on_whisper(KpNetGame *ng, const uint8_t *m, int sender)
{
	char line[KP_NETGAME_LINE];
	if (!kp_netgame_player_active(ng, sender) || !(ng->chat_mask & (1u << sender))) return;
	format1(line, sizeof line, text_of(&ng->hooks, KP_NETTBL_FROM), kp_netgame_player_name(ng, sender));
	append(line, sizeof line, " ");
	append(line, sizeof line, (const char *)m + 1);
	add_chat(ng, -1, line);
}

/* ReceiveChatRoomMessage 0x471860 */
static int receive_chat_message(KpNetGame *ng)
{
	int sender = -1;
	const uint8_t *m = NULL;
	uint32_t n = 0, w;
	if (!receive_message(ng, &sender, &m, &n)) return KP_CHAT_NONE;
	if (!n || !m || (uint8_t)(m[0] - KP_CHAT_JOINED) > 7) return KP_CHAT_MALFORMED;
	switch (m[0]) {
	case KP_CHAT_JOINED:
		if (n != CHAT_JOINED_SIZE) return KP_CHAT_MALFORMED;
		on_chat_joined(ng, m, sender);
		return KP_CHAT_JOINED;
	case KP_CHAT_GAME_INFO:
		if (n != KP_CHAT_GAME_INFO_SIZE) return KP_CHAT_MALFORMED;
		on_game_info(ng, m, sender);
		return KP_CHAT_GAME_INFO;
	case KP_CHAT_FORCE_NAMES:
		if (n != KP_CHAT_FORCE_NAMES_SIZE) return KP_CHAT_MALFORMED;
		on_force_names(ng, m, sender);
		return KP_CHAT_FORCE_NAMES;
	case KP_CHAT_TEXT:
		if (!kp_netgame_net_string((const char *)m + 1, KP_NETGAME_CHAT, n - 1, 0, 0)) return KP_CHAT_MALFORMED;
		if (kp_netgame_player_active(ng, sender) && (ng->chat_mask & (1u << sender)))
			add_chat(ng, sender, (const char *)m + 1);
		return KP_CHAT_TEXT;
	case KP_CHAT_WHISPER:
		if (!kp_netgame_net_string((const char *)m + 1, KP_NETGAME_CHAT, n - 1, 0, 0)) return KP_CHAT_MALFORMED;
		on_whisper(ng, m, sender);
		return KP_CHAT_TEXT;
	case KP_CHAT_LEAVE:
		if (n != 2) return KP_CHAT_MALFORMED;
		if (sender == 0) set_leave(ng, m[1]);
		return KP_CHAT_LEAVE;
	case KP_CHAT_TRANSFER:
		w = n >= TRANSFER_HEADER ? kp_net_get16(m + 1) : 0;
		if (n < TRANSFER_HEADER || w > n - TRANSFER_HEADER) ng->refused++;
		else kp_transfer_dispatch(&ng->transfer, m + TRANSFER_HEADER, w, sender);
		return KP_CHAT_TRANSFER;
	default:
		return KP_CHAT_INFO_END;
	}
}

/* --- chat lines and slash commands (P24) --- */

/* SendWhisperText 0x471f80 */
static void send_whisper_text(KpNetGame *ng, const char *text, int id)
{
	uint8_t m[KP_NETGAME_CHAT + 1];
	char line[KP_NETGAME_LINE], t[KP_NETGAME_CHAT];
	size_t n;
	if (!kp_netgame_player_active(ng, id)) return;
	n = strlen(text) + 1;
	if (n > KP_NETGAME_CHAT) return;
	m[0] = KP_CHAT_WHISPER;
	copy((char *)m + 1, KP_NETGAME_CHAT, text);
	send_message(ng, id, m, (uint32_t)n + 1);
	copy(t, sizeof t, text);
	format1(line, sizeof line, text_of(&ng->hooks, KP_NETTBL_TO), kp_netgame_player_name(ng, id));
	append(line, sizeof line, " ");
	append(line, sizeof line, t);
	add_chat(ng, -1, line);
}

/* SendWhisper 0x4701f0: in the chat room a 'M' message, in game the in-game message type 1 (not recorded) */
static void send_whisper(KpNetGame *ng, const char *text, int id)
{
	uint8_t m[GAME_CHAT_TEXT + GAME_CHAT_MAX];
	char line[KP_NETGAME_LINE];
	size_t n;
	if (!text[0] || !kp_netgame_player_active(ng, id)) return;
	if (!ng->in_game) {
		if (ng->in_chat_room) send_whisper_text(ng, text, id);
		return;
	}
	n = strlen(text) + 1;
	if (n > GAME_CHAT_MAX) return;
	m[0] = GAME_CHAT_FROM;
	m[1] = 0; /* never written by the original */
	copy((char *)m + GAME_CHAT_TEXT, GAME_CHAT_MAX, text);
	send_message(ng, id, m, (uint32_t)(n + GAME_CHAT_TEXT));
	format1(line, sizeof line, text_of(&ng->hooks, KP_NETTBL_TO), kp_netgame_player_name(ng, id));
	append(line, sizeof line, " ");
	append(line, sizeof line, text);
	timed(ng, line);
}

/* MatchSlashCommandPlayer 0x470320: the text after `keyword name` (and the space when required), the slot in *slot */
static const char *match_player(const KpNetGame *ng, const char *text, const char *keyword, int require_space, int *slot)
{
	size_t n = strlen(keyword);
	const char *rest;
	int i;
	if (kp_net_ncasecmp(text, keyword, n) != 0) return NULL;
	rest = text + n;
	for (i = 0; i < 8; i++) {
		const KpNetSlot *r = &ng->slots[i];
		size_t len = kp_net_text_len(r->name, sizeof r->name);
		if (r->type != KP_CTRL_HUMAN || !len || kp_net_ncasecmp(rest, r->name, len) != 0) continue;
		if (!require_space) {
			*slot = i;
			return rest + len;
		}
		if (rest[len] == ' ') {
			*slot = i;
			return rest + len + 1;
		}
	}
	return NULL;
}

/* The ignore mask's line (HandleChatSlashCommand's steps 1 and 2) */
static void mask_line(KpNetGame *ng, int index, int slot)
{
	char line[KP_NETGAME_LINE];
	format1(line, sizeof line, text_of(&ng->hooks, index), ng->slots[slot].name);
	if (ng->in_game) timed(ng, line);
	else if (ng->in_chat_room) add_chat(ng, -1, line);
}

/* HandleChatSlashCommand 0x46fd90: 0 for a line that is no command, else 1 */
static int slash_command(KpNetGame *ng, const char *text)
{
	static const char *const IGNORE[] = { "squelch ", "ignore " };
	static const char *const ACCEPT[] = { "unsquelch ", "unignore " };
	static const char *const WHISPER[] = { "whisper ", "msg ", "w ", "m " };
	const char *t, *rest;
	int slot = NO_SLOT, k, id;
	if (text[0] != '/') return 0;
	t = text + 1;
	for (k = 0; k < 2; k++)
		if (match_player(ng, t, IGNORE[k], 0, &slot)) {
			id = ng->slots[slot].storm;
			if (id >= 0 && id < 16) ng->chat_mask &= (uint16_t)~(1u << id);
			mask_line(ng, KP_NETTBL_IGNORING, slot);
			return 1;
		}
	for (k = 0; k < 2; k++)
		if (match_player(ng, t, ACCEPT[k], 0, &slot)) {
			id = ng->slots[slot].storm;
			if (id >= 0 && id < 16) ng->chat_mask |= (uint16_t)(1u << id);
			mask_line(ng, KP_NETTBL_ACCEPTING, slot);
			return 1;
		}
	/* MatchReplyTarget 0x4703d0: the reply name is Storm ordinal 146's, which the UDP provider never sets, so no slot
	 * matches and nothing is sent (kp_netgame.h) */
	if (kp_net_ncasecmp(t, "reply ", 6) == 0 || kp_net_ncasecmp(t, "r ", 2) == 0) return 1;
	for (k = 0; k < 4; k++)
		if ((rest = match_player(ng, t, WHISPER[k], 1, &slot)) != NULL) {
			send_whisper(ng, rest, ng->slots[slot].storm);
			return 1;
		}
	return 1;
}

/* SendInGameChat 0x473be0 */
void kp_netgame_game_chat(KpNetGame *ng, const char *text, uint16_t recipients)
{
	uint8_t m[GAME_CHAT_TEXT + GAME_CHAT_MAX];
	size_t n;
	int id;
	if (slash_command(ng, text)) return;
	n = strlen(text);
	if (!recipients || !n) return;
	/* the original sends n + 3 bytes whatever it copied; the box keeps a line under 78 characters */
	if (n + 1 > GAME_CHAT_MAX) n = GAME_CHAT_MAX - 1;
	m[0] = GAME_CHAT;
	m[1] = 0;
	copy((char *)m + GAME_CHAT_TEXT, GAME_CHAT_MAX, text);
	for (id = 0; id < KP_NETGAME_PLAYERS; id++)
		if ((recipients & (1u << id)) && kp_netgame_player_active(ng, id))
			send_message(ng, id, m, (uint32_t)(n + GAME_CHAT_TEXT + 1));
}

void kp_netgame_chat(KpNetGame *ng, const char *text)
{
	uint8_t m[KP_NETGAME_CHAT + 1];
	size_t n;
	if (slash_command(ng, text)) return;
	n = strlen(text) + 1;
	if (n > KP_NETGAME_CHAT) return;
	m[0] = KP_CHAT_TEXT;
	copy((char *)m + 1, KP_NETGAME_CHAT, text);
	send_message(ng, -1, m, (uint32_t)n + 1);
}

/* --- the map transfer's carrier --- */

/* SendMapDataChunk 0x471800: the packet as the chat-room message 'O' */
static void transfer_send(void *ud, int storm, const uint8_t *packet, size_t size)
{
	KpNetGame *ng = (KpNetGame *)ud;
	uint8_t m[KP_NETGAME_TURN_MAX];
	if (size + TRANSFER_HEADER > sizeof m) return; /* the session's capacity keeps it below */
	m[0] = KP_CHAT_TRANSFER;
	kp_net_put16(m + 1, (uint16_t)size);
	memcpy(m + TRANSFER_HEADER, packet, size);
	send_message(ng, storm, m, (uint32_t)(size + TRANSFER_HEADER));
}

static uint8_t *transfer_read(void *ud, const char *path, size_t *size)
{
	KpNetGame *ng = (KpNetGame *)ud;
	return ng->config.transfer.read ? ng->config.transfer.read(ng->config.transfer.ud, path, size) : NULL;
}

static int transfer_find(void *ud, const char *name, uint32_t size, uint32_t crc, char *out, size_t out_size)
{
	KpNetGame *ng = (KpNetGame *)ud;
	return ng->config.transfer.find ? ng->config.transfer.find(ng->config.transfer.ud, name, size, crc, out, out_size)
		: 0;
}

static int transfer_save(void *ud, const char *name, const uint8_t *data, size_t size, char *out, size_t out_size)
{
	KpNetGame *ng = (KpNetGame *)ud;
	return ng->config.transfer.save ? ng->config.transfer.save(ng->config.transfer.ud, name, data, size, out, out_size)
		: 0;
}

/* OpenMapTransferSession 0x477090 */
static int open_transfer(KpNetGame *ng)
{
	KpTransferIo io;
	io.send = transfer_send;
	io.read = transfer_read;
	io.find = transfer_find;
	io.save = transfer_save;
	io.ud = ng;
	ng->offer = NULL;
	kp_transfer_begin(&ng->transfer, &io, ng->local_storm, ng->max_message - 4);
	if (ng->host && !saving(ng)) {
		ng->offer = kp_transfer_offer(&ng->transfer, ng->map_path);
		if (!ng->offer) return 0;
	}
	return 1;
}

/* --- Storm's events (net_glue.cpp, net_misc.cpp) --- */

static int name_banned(const KpNetGame *ng, const char *name)
{
	int k;
	for (k = 0; k < ng->ban_count; k++)
		if (kp_net_casecmp(ng->bans[k], name) == 0) return 1;
	return 0;
}

/* SendForceNames 0x471e60 */
static void send_force_names(KpNetGame *ng, int id)
{
	uint8_t m[KP_CHAT_FORCE_NAMES_SIZE];
	int f;
	memset(m, 0, sizeof m);
	m[0] = KP_CHAT_FORCE_NAMES;
	for (f = 4; f >= 1; f--)
		copy((char *)m + 1 + KP_NETGAME_FORCE_NAME * (f - 1), KP_NETGAME_FORCE_NAME, ng->force_names[f - 1]);
	send_message(ng, id, m, sizeof m);
}

/* SendGameInfo 0x471ea0 */
static void send_game_info(KpNetGame *ng, int id)
{
	uint8_t m[KP_CHAT_GAME_INFO_SIZE], end = KP_CHAT_INFO_END;
	int i;
	m[0] = KP_CHAT_GAME_INFO;
	kp_net_put16(m + 1, ng->tileset);
	kp_net_put16(m + 3, ng->width);
	kp_net_put16(m + 5, ng->height);
	for (i = 0; i < KP_NETGAME_SLOTS; i++) {
		m[7 + i] = ng->setup[i].type;
		m[0x13 + i] = ng->setup[i].race;
	}
	memcpy(m + 0x1f, ng->setup_types, KP_NETGAME_SLOTS);
	memcpy(m + 0x2b, ng->forces, KP_NETGAME_SLOTS);
	memcpy(m + 0x37, ng->user_select, sizeof ng->user_select);
	send_message(ng, id, m, sizeof m);
	send_message(ng, id, &end, 1);
}

/* OnPlayerJoined 0x4778e0 */
static void on_player_joined(KpNetGame *ng, int id)
{
	char name[KP_NETGAME_NAME];
	if (id < 0 || id >= KP_NETGAME_PLAYERS) return;
	ng->leave_reasons[id] = 0;
	if (ng->in_game) {
		ng->net.drop_player(ng->net.ud, id, DROP_KICK);
		return;
	}
	if (!ng->host) return;
	if (ng->lobby_state != KP_LOBBY_IN || ng->start_pressed) {
		reject(ng, id, KP_LEAVE_UNABLE);
		return;
	}
	if (id == 0) {
		set_leave(ng, KP_LEAVE_UNABLE);
		return;
	}
	net_player_name(ng, id, name, sizeof name);
	if (name_banned(ng, name)) {
		reject(ng, id, KP_LEAVE_BANNED);
		return;
	}
	if (ng->offer) kp_transfer_set_pending(ng->offer, 1u << id);
	if (kp_netgame_ums(ng)) send_force_names(ng, id);
	send_game_info(ng, id);
}

/* HandlePlayerLeftLobby 0x477c20 */
static void handle_left_lobby(KpNetGame *ng, int id, uint32_t reason)
{
	int slot;
	uint8_t type, race, force;
	ng->players[id].state = 2; /* MarkNetPlayerLeft 0x4780c0 */
	if (ng->host && ng->offer) {
		kp_transfer_clear_pending(ng->offer, 1u << id);
		kp_transfer_remove_peer(ng->offer, id);
	}
	kp_transfer_abort_peer(&ng->transfer, id);
	slot = ng->storm_slot[id];
	if (slot >= 8) {
		if (id == 0) set_leave(ng, KP_LEAVE_HOST_LEFT);
		return;
	}
	if (ng->in_chat_room) {
		const char *name = kp_netgame_player_name(ng, id);
		if (id == 0) {
			set_leave(ng, KP_LEAVE_HOST_LEFT);
			return;
		}
		if (name) {
			char line[0x80];
			format1(line, sizeof line,
				text_of(&ng->hooks, reason == KP_NETGAME_DROP_DESYNC ? KP_NETTBL_DROPPED : KP_NETTBL_LEFT), name);
			add_chat(ng, 8, line);
		}
	}
	type = ng->slots[slot].type;
	race = ng->slots[slot].race;
	force = ng->slots[slot].force;
	restore_slot(ng, slot);
	if (type == KP_CTRL_CLOSED || type == KP_CTRL_LOBBY_COMPUTER) ng->slots[slot].type = type;
	ng->slots[slot].race = race;
	ng->slots[slot].force = force;
	ng->storm_slot[id] = NO_SLOT;
	if (!ng->in_chat_room) return;
	refresh(ng);
	if (saving(ng)) clear_saved_slot(ng, slot);
	if (ng->host) update_joinable(ng);
}

/* ReceiveTemplateRecord 0x448000 */
static void receive_template(KpNetGame *ng, const KpSnetEvent *e)
{
	const KpTemplate *t = ng->config.templates
		? kp_templates_find(ng->config.templates, ng->requested_key[0], ng->requested_key[1],
			ng->requested_key[2] | ng->requested_key[3] << 8)
		: NULL;
	if (t && e->size == KP_TEMPLATE_SIZE && e->data && memcmp(e->data, t->record, KP_TEMPLATE_SIZE) == 0)
		memcpy(ng->header.template_record, t->record, KP_TEMPLATE_SIZE);
	else
		ng->header.template_record[0] = NONE_BYTE;
}

void kp_netgame_event(KpNetGame *ng, const KpSnetEvent *e)
{
	switch (e->id) {
	case KP_SNET_EVENT_TEMPLATE: receive_template(ng, e); break;
	case KP_SNET_EVENT_PLAYER: on_player_joined(ng, e->player); break;
	case KP_SNET_EVENT_REMOVED: /* OnPlayerLeft 0x477a00 */
		if (e->player >= 0 && e->player < KP_NETGAME_PLAYERS) {
			uint32_t r = e->data && e->size >= 4 ? kp_net_get32(e->data) : 0;
			ng->leave_reasons[e->player] = r ? r : 3;
		}
		break;
	default: break; /* OnServerMessage 0x46fd40: Battle.net's text, never over UDP */
	}
}

/* --- the stat string (P26) --- */

uint32_t kp_netgame_net_string(const char *s, uint32_t max, uint32_t available, int formatting, int controls)
{
	uint32_t n = max < available ? max : available, k;
	for (k = 0; k < n; k++) {
		int b = (signed char)s[k];
		if (b == 0) return k + 1;
		if (!controls && ((b >= 1 && b <= 8) || (b >= 14 && b <= 17) || (b >= 21 && b <= 25) || (b >= 27 && b <= 31)))
			return 0;
		if (!formatting && ((b >= 9 && b <= 13) || (b >= 18 && b <= 20) || b == 26)) return 0;
	}
	return 0;
}

/* AppendStatLine 0x46f690 within the remaining bytes: CR becomes LF, then the line's CR */
static size_t stat_line(char *out, size_t remaining, const char *src)
{
	size_t n = 0;
	for (; *src && n + 2 < remaining; src++) out[n++] = *src == '\r' ? '\n' : *src;
	if (n + 1 < remaining) out[n++] = '\r';
	return n;
}

/* A string field of the header: its bytes up to its NUL, the last one forced to 0 */
static void header_string(char *dst, const uint8_t *src, size_t size)
{
	memcpy(dst, src, size);
	dst[size - 1] = 0;
}

void kp_netgame_header_read(const uint8_t *raw, KpNetGameHeader *h)
{
	memset(h, 0, sizeof *h);
	h->id = kp_net_get32(raw);
	header_string(h->name, raw + 4, sizeof h->name);
	h->save_id = kp_net_get32(raw + 0x1c);
	h->width = (uint16_t)kp_net_get16(raw + 0x20);
	h->height = (uint16_t)kp_net_get16(raw + 0x22);
	h->players = raw[0x24];
	h->max_players = raw[0x25];
	h->speed = raw[0x26];
	h->map_class = raw[0x27];
	h->type = raw[0x28];
	h->sub = raw[0x29];
	h->subtype = (uint16_t)kp_net_get16(raw + 0x2a);
	h->key_hash = kp_net_get32(raw + 0x2c);
	h->tileset = (uint16_t)kp_net_get16(raw + 0x30);
	h->replay = raw[0x32];
	h->save_players = raw[0x33];
	header_string(h->creator, raw + 0x34, sizeof h->creator);
	header_string(h->title, raw + 0x4d, sizeof h->title);
	memcpy(h->template_record, raw + 0x6d, sizeof h->template_record);
}

void kp_netgame_header_write(const KpNetGameHeader *h, uint8_t *raw)
{
	memset(raw, 0, KP_NETGAME_HEADER);
	kp_net_put32(raw, h->id);
	copy((char *)raw + 4, sizeof h->name, h->name);
	kp_net_put32(raw + 0x1c, h->save_id);
	kp_net_put16(raw + 0x20, h->width);
	kp_net_put16(raw + 0x22, h->height);
	raw[0x24] = h->players;
	raw[0x25] = h->max_players;
	raw[0x26] = h->speed;
	raw[0x27] = h->map_class;
	raw[0x28] = h->type;
	raw[0x29] = h->sub;
	kp_net_put16(raw + 0x2a, h->subtype);
	kp_net_put32(raw + 0x2c, h->key_hash);
	kp_net_put16(raw + 0x30, h->tileset);
	raw[0x32] = h->replay;
	raw[0x33] = h->save_players;
	copy((char *)raw + 0x34, sizeof h->creator, h->creator);
	copy((char *)raw + 0x4d, sizeof h->title, h->title);
	memcpy(raw + 0x6d, h->template_record, sizeof h->template_record);
}

void kp_netgame_stat_build(const KpNetGameHeader *h, char *out, size_t size)
{
	uint32_t v[11];
	static const uint32_t DEFAULTS[11] = { 0, 0x88, 0x18, 4, 0, 0, 0, 0, 0, 0, 0 };
	size_t at = 0;
	int k;
	if (!size) return;
	v[0] = h->save_id;
	v[1] = (uint32_t)(((h->width >> 1) & 0xf0) | ((h->height >> 5) & 0xff));
	v[2] = (uint32_t)(((h->players << 4) & 0xff) | h->max_players);
	v[3] = h->speed;
	v[4] = h->map_class;
	v[5] = h->type;
	v[6] = h->sub;
	v[7] = h->subtype;
	v[8] = h->key_hash;
	v[9] = h->tileset;
	v[10] = h->replay;
	for (k = 0; k < 11; k++) {
		int n = v[k] == DEFAULTS[k] ? snprintf(out + at, size - at, ",") : snprintf(out + at, size - at, "%x,", (unsigned)v[k]);
		/* FormatBounded 0x4cb860: a truncated write takes the rest of the buffer */
		at = n < 0 || (size_t)n >= size - at ? size - 1 : at + (size_t)n;
	}
	at += stat_line(out + at, size - at, h->creator);
	at += stat_line(out + at, size - at, h->title);
	out[at] = 0;
}

/* ParseHexField 0x46f910 */
static const char *hex_field(const char *s, uint32_t *out, uint32_t def)
{
	if (!s) {
		*out = def;
		return NULL;
	}
	if (*s == ',') {
		*out = def;
		return s + 1;
	}
	for (*out = 0;; s++) {
		int c = kp_net_lower((unsigned char)*s);
		if (c >= '0' && c <= '9') *out = *out * 16 + (uint32_t)(c - '0');
		else if (c >= 'a' && c <= 'f') *out = *out * 16 + (uint32_t)(c - 'a' + 10);
		else return c == ',' ? s + 1 : NULL;
	}
}

/* A name up to its '\r' (at most max characters) into out: the position after the '\r', NULL when malformed */
static const char *stat_name(const char *s, char *out, size_t max)
{
	size_t k;
	if (!s) return NULL;
	for (k = 0; s[k] != '\r'; k++) {
		if (k >= max || !s[k]) return NULL;
		out[k] = s[k];
	}
	out[k] = 0;
	return s + k + 1;
}

int kp_netgame_stat_parse(const char *stats, KpNetGameHeader *h)
{
	const char *s = stats;
	uint32_t v;
	if (!kp_netgame_net_string(stats, KP_NETGAME_STATS, KP_NETGAME_STATS, 1, 1)) return 0;
	s = hex_field(s, &h->save_id, 0);
	s = hex_field(s, &v, 0x88);
	h->width = (uint16_t)(((v >> 4) & 0xf) << 5);
	h->height = (uint16_t)((v & 0xf) << 5);
	s = hex_field(s, &v, 0x18);
	h->max_players = (uint8_t)(v & 0xf);
	h->players = (uint8_t)((v >> 4) & 0xf);
	s = hex_field(s, &v, 4);
	h->speed = (uint8_t)v;
	s = hex_field(s, &v, 0);
	h->map_class = (uint8_t)v;
	s = hex_field(s, &v, 0);
	h->type = (uint8_t)v;
	s = hex_field(s, &v, 0);
	h->sub = (uint8_t)v;
	s = hex_field(s, &v, 0);
	h->subtype = (uint16_t)v;
	s = hex_field(s, &h->key_hash, 0);
	s = hex_field(s, &v, 0);
	h->tileset = (uint16_t)v;
	s = hex_field(s, &v, 0);
	h->replay = (uint8_t)v;
	s = stat_name(s, h->creator, KP_NETGAME_NAME - 1);
	s = stat_name(s, h->title, KP_NETGAME_TITLE - 1);
	if (!s || *s) return 0;
	/* ValidateGameStat 0x46f8b0 */
	return h->width <= 0x100 && h->height <= 0x100 && h->players <= h->max_players && h->max_players <= 8 &&
		h->speed <= 6 && h->creator[0] && h->title[0] &&
		kp_netgame_net_string(h->title, KP_NETGAME_TITLE, KP_NETGAME_TITLE, 0, 1) && h->map_class < 7 &&
		h->tileset < 8;
}

/* --- the map (LoadMapHeader 0x46b5c0) --- */

void kp_netgame_map(KpNetMap *m, const KpMapHeader *h, const char *path, const KpNetGameHooks *hooks)
{
	int p, f;
	memset(m, 0, sizeof *m);
	m->tileset = h->tileset;
	m->width = h->width;
	m->height = h->height;
	for (p = KP_NETGAME_SLOTS - 1; p >= 0; p--) {
		KpNetSlot *r = &m->setup[p];
		r->slot = p;
		r->storm = -1;
		if (p >= KP_MAP_HEADER_SLOTS) continue;
		r->type = h->owners[p];
		r->race = h->races[p];
		if (r->race == KP_MAP_RACE_USER_SELECT) {
			r->race = RACE_RANDOM;
			m->user_select[p] = 1;
		}
	}
	/* LoadForceChunk 0x443320 and ApplyForcesToSlots 0x4432d0 */
	for (p = 0; p < KP_MAP_HEADER_SLOTS; p++) {
		m->forces[p] = (uint8_t)(h->forces[p] + 1);
		if (player_type(m->setup[p].type)) m->setup[p].force = m->forces[p];
		else m->forces[p] = 0;
	}
	memcpy(m->forces + KP_MAP_HEADER_SLOTS, h->force_flags, KP_MAP_HEADER_FORCES);
	for (f = 4; f >= 1; f--) {
		if (h->force_named[f - 1]) copy(m->force_names[f - 1], KP_NETGAME_FORCE_NAME, h->force_names[f - 1]);
		else copy(m->force_names[f - 1], KP_NETGAME_FORCE_NAME, hooks->text ? hooks->text(hooks->ud, KP_NETTBL_FORCE + f) : "");
	}
	copy(m->title, sizeof m->title, h->title);
	copy(m->path, sizeof m->path, path);
}

void kp_netgame_set_map(KpNetGame *ng, const KpNetMap *m)
{
	ng->tileset = m->tileset;
	ng->width = m->width;
	ng->height = m->height;
	memcpy(ng->setup, m->setup, sizeof ng->setup);
	memcpy(ng->forces, m->forces, sizeof ng->forces);
	memcpy(ng->force_names, m->force_names, sizeof ng->force_names);
	memcpy(ng->user_select, m->user_select, sizeof ng->user_select);
	copy(ng->map_path, sizeof ng->map_path, m->path);
	memcpy(ng->slots, ng->setup, sizeof ng->slots); /* CopyMapSetupToSlots 0x479bf0 */
}

/* --- the session (net_glue.cpp) --- */

int kp_netgame_init(KpNetGame *ng, const KpNetCalls *net, const KpNetGameHooks *hooks, const KpNetGameConfig *config)
{
	KpSnetCaps caps;
	memset(ng, 0, sizeof *ng);
	ng->net = *net;
	ng->hooks = *hooks;
	ng->config = *config;
	ng->local_storm = ng->local_slot = ng->local_index = -1;
	ng->leave_reason = -1;
	memset(ng->storm_slot, NO_SLOT, sizeof ng->storm_slot);
	memset(ng->storm_index, NO_SLOT, sizeof ng->storm_index);
	memset(ng->saved_slots, NO_SLOT, sizeof ng->saved_slots);
	memset(&caps, 0, sizeof caps);
	caps.size = CAPS_SIZE;
	if (!ng->net.provider_caps(ng->net.ud, &caps)) {
		report_storm(ng, KP_NETTBL_PROVIDER_CAPS, 0);
		return -1;
	}
	if (caps.maxmsg < KP_NETGAME_MIN_MESSAGE) {
		report(ng, KP_NETTBL_PROVIDER_CAPS, "caps.maxmessagesize", 0);
		return -1;
	}
	ng->max_message = caps.maxmsg < KP_NETGAME_TURN_MAX ? caps.maxmsg : KP_NETGAME_TURN_MAX;
	ng->transit = caps.turns_in_transit < 2 ? 2 : caps.turns_in_transit > 8 ? 8 : caps.turns_in_transit;
	ng->turns_per_second = caps.turns_per_sec;
	return 0;
}

int kp_netgame_game_caps(KpNetGame *ng)
{
	KpSnetCaps caps;
	memset(&caps, 0, sizeof caps);
	caps.size = CAPS_SIZE;
	if (!ng->net.provider_caps(ng->net.ud, &caps)) {
		report_storm(ng, KP_NETTBL_NETWORK_ERROR, 1);
		return -1;
	}
	ng->turns_per_second = caps.turns_per_sec;
	return 0;
}

void kp_netgame_free(KpNetGame *ng)
{
	kp_transfer_reset(&ng->transfer);
	free(ng->list);
	ng->list = NULL;
	ng->list_cap = ng->list_count = 0;
}

/* TemplateNetFlags 0x4476f0 */
static uint32_t template_net_flags(const KpNetGame *ng, const uint8_t *t)
{
	uint32_t f = 0, subtype = (uint32_t)(t[2] | t[3] << 8);
	if (t[0] == 9 && subtype == 2) f = 0x10;
	else if (t[0] == 0x80 && t[1] != 0) f = (uint32_t)t[1] << 24 | 0x800020u | (subtype == 2 ? 0x10u : 0);
	if (ng->config.replay) f |= 0x80;
	return f;
}

int kp_netgame_create(KpNetGame *ng, const KpNetGameHeader *h, const char *password)
{
	static const uint32_t LADDER[4] = { 0, 1, 3, 5 };
	KpNetGameHeader g = *h;
	const KpTemplate *t = ng->config.templates ? kp_templates_find(ng->config.templates, h->type, h->sub, h->subtype)
		: NULL;
	char stats[KP_NETGAME_STATS];
	uint32_t ladder;
	if (!t) {
		report(ng, KP_NETTBL_INVALID_OPTIONS, "CreateGame_MultiPlayer()", 0);
		return 0;
	}
	memcpy(g.template_record, t->record, KP_TEMPLATE_SIZE);
	g.players = 1;
	if (kp_net_text_len(ng->config.player_name, KP_NETGAME_NAME) >= KP_NETGAME_NAME) return 0;
	copy(g.creator, sizeof g.creator, ng->config.player_name);
	if (g.template_record[KP_TEMPLATE_TEAMS] && !g.save_id) g.max_players = 8;
	ladder = g.template_record[KP_TEMPLATE_TOURNAMENT] < 4 ? LADDER[g.template_record[KP_TEMPLATE_TOURNAMENT]] : 0;
	/* TODO: a ladder template (types 0x11..0x16) takes its ladder type from FindTemplatePath 0x447b10's entry +0x124;
	 * the retail templates.lst has none */
	if (g.template_record[KP_TEMPLATE_VICTORY] == VICTORY_ONE_ON_ONE && !g.save_id && g.max_players > 2)
		g.max_players = 2;
	if (kp_template_ums(g.template_record) && !g.save_id) g.max_players = (uint8_t)kp_netgame_open_slots(ng);
	if (ng->hooks.key_draws) ng->hooks.key_draws(ng->hooks.ud, 1);
	g.key_hash = ng->config.key_hash;
	kp_netgame_stat_build(&g, stats, sizeof stats);
	{
		/* the template record as the advertised game's type (PackTemplateKey 0x4476d0) */
		const uint8_t *r = g.template_record;
		uint32_t key = (uint32_t)(r[0] | r[1] << 8 | (uint32_t)(r[2] | r[3] << 8) << 16);
		int id = -1;
		if (!ng->net.create_game(ng->net.ud, g.name, password, stats, key, ladder, template_net_flags(ng, r), r,
				KP_TEMPLATE_SIZE, g.max_players, ng->config.player_name, "", &id))
			return 0;
		ng->local_storm = id;
	}
	ng->host = 1;
	ng->header = g;
	return 1;
}

/* OnEnumGame 0x475380 */
static int on_enum_game(void *ud, const KpSnetGame *g)
{
	KpNetGame *ng = (KpNetGame *)ud;
	KpNetGameHeader h;
	int at, k;
	memset(&h, 0, sizeof h);
	if (!g->stats || !kp_netgame_stat_parse(g->stats, &h)) return 1;
	copy(h.name, sizeof h.name, g->name ? g->name : "");
	h.id = g->id;
	h.players = (uint8_t)g->players;
	h.max_players = (uint8_t)g->max_players;
	if (ng->listed_count >= KP_NETGAME_GAMES) {
		ng->refused++;
		return 1;
	}
	for (at = 0; at < ng->listed_count; at++)
		if (kp_net_casecmp(ng->listed[at].name, h.name) >= 0) break;
	for (k = ng->listed_count; k > at; k--) ng->listed[k] = ng->listed[k - 1];
	ng->listed[at] = h;
	ng->listed_count++;
	return 1;
}

void kp_netgame_enum(KpNetGame *ng)
{
	uint32_t hint = 0;
	ng->listed_count = 0;
	ng->net.enum_games(ng->net.ud, on_enum_game, ng, &hint);
}

int kp_netgame_join(KpNetGame *ng, const KpNetGameHeader *listed)
{
	uint8_t received[KP_TEMPLATE_SIZE];
	int id = -1;
	ng->requested_key[0] = listed->type;
	ng->requested_key[1] = listed->sub;
	kp_net_put16(ng->requested_key + 2, listed->subtype);
	if (!ng->net.join_game(ng->net.ud, listed->id, "", "", ng->config.player_name, "", &id)) {
		report_storm(ng, ng->net.error(ng->net.ud) == ERROR_VERSION ? KP_NETTBL_JOIN_VERSION : KP_NETTBL_JOIN_FAILED, 0);
		return 0;
	}
	ng->local_storm = id;
	memcpy(received, ng->header.template_record, sizeof received);
	ng->header = *listed;
	memcpy(ng->header.template_record, received, sizeof received);
	ng->config.replay = listed->replay;
	ng->host = 0;
	return 1;
}

int kp_netgame_enter(KpNetGame *ng)
{
	ng->start_pressed = 0;
	if (!ng->host && ng->local_storm == 0) {
		set_leave(ng, KP_LEAVE_UNABLE);
		return 0;
	}
	memset(ng->saved_slots, NO_SLOT, sizeof ng->saved_slots); /* ResetSavedSlots 0x4719d0 */
	if (ng->header.save_id != 0) {
		KpNetGameSave save;
		memset(&save, 0, sizeof save);
		if (!ng->hooks.find_save || !ng->hooks.find_save(ng->hooks.ud, ng->header.save_id, &save)) {
			kp_netgame_abort(ng);
			report(ng, KP_NETTBL_NO_SAVE, NULL, 0);
			return 0;
		}
		load_save(ng, &save);
		if (ng->host) rebuild_from_save(ng);
	}
	memset(ng->slots, 0, sizeof ng->slots);
	rebuild_storm_maps(ng, -1);
	memset(ng->players, 0, sizeof ng->players);
	ng->ban_count = 0;
	if (ng->host && !saving(ng)) {
		int s;
		if (!kp_netgame_ums(ng)) reset_setup_types_for_join(ng);
		for (s = KP_NETGAME_SLOTS - 1; s >= 0; s--) ng->setup_types[s] = ng->setup[s].type; /* SaveSetupSlotTypes */
		apply_team_setup(ng);
	}
	if (ng->host) memcpy(ng->slots, ng->setup, sizeof ng->slots);
	ng->local_index = ng->local_slot = -1;
	ng->chat_mask = 0xffff;
	memset(ng->leave_reasons, 0, sizeof ng->leave_reasons);
	reset_turn_state(ng);
	set_lobby_state(ng, KP_LOBBY_IDLE);
	if (!saving(ng)) memset(ng->results, 0, sizeof ng->results);
	if (!open_transfer(ng)) {
		kp_netgame_abort(ng);
		report(ng, KP_NETTBL_NO_MAP, NULL, 0);
		return 0;
	}
	ng->last_download = NONE_BYTE;
	ng->poll_clock = now(ng); /* ResetLobbyPollClock 0x46f460 */
	send_turns(ng);
	set_lobby_state(ng, KP_LOBBY_ENTERED);
	if (ng->host) {
		set_lobby_state(ng, KP_LOBBY_REQUESTED);
		build_join_request(ng, saving(ng) ? ng->save.local_slot : 0,
			saving(ng) ? (uint8_t)ng->save.local_player : 0, ng->save_rng, 1);
	}
	return 1;
}

/* PollLobbyNetwork 0x46f3c0 */
static int poll_network(KpNetGame *ng)
{
	uint32_t t = now(ng);
	if (t - ng->poll_clock < KP_NETGAME_POLL_MS) return 0;
	if (!turn_step(ng)) {
		if (t - ng->poll_clock >= KP_NETGAME_STALL_MS) resolve_desync(ng);
		return 0;
	}
	if (ng->lobby_state == KP_LOBBY_IN) {
		uint8_t pct = 0;
		if (!kp_transfer_progress(&ng->transfer, &pct)) set_leave(ng, KP_LEAVE_WRITE_MAP);
		else queue_download(ng, pct);
	}
	flush_lobby(ng);
	ng->poll_clock = t;
	send_turns(ng);
	refresh(ng);
	countdown_tick(ng);
	return ng->lobby_state == KP_LOBBY_START;
}

int kp_netgame_poll(KpNetGame *ng)
{
	if (!poll_network(ng)) return receive_chat_message(ng);
	discard_lobby(ng);
	return KP_CHAT_START;
}

int kp_netgame_ready_room(const KpNetGame *ng)
{
	int race;
	if (!kp_netgame_ums(ng) || saving(ng) || ng->config.replay) return 0;
	race = ng->local_slot >= 0 && ng->local_slot < 8 ? ng->slots[ng->local_slot].race : -1;
	return race == 0 ? GLUE_READY_ZERG : race == 2 ? GLUE_READY_PROTOSS : GLUE_READY_TERRAN;
}

const char *kp_netgame_map_path(const KpNetGame *ng)
{
	return !ng->host && ng->transfer.map_ready ? ng->transfer.map_path : ng->map_path;
}

void kp_netgame_game_setup(const KpNetGame *ng, KpGameSetup *s)
{
	int k;
	memset(s, 0, sizeof *s);
	memcpy(s->template_record, ng->header.template_record, KP_TEMPLATE_SIZE);
	s->multiplayer = 1;
	s->slots = ng->header.max_players;
	for (k = 0; k < KP_SETUP_SLOTS && k < KP_NETGAME_SLOTS; k++) {
		const KpNetSlot *p = &ng->slots[k];
		s->storm[k] = p->storm;
		s->type[k] = p->type;
		s->race[k] = p->race;
		s->force[k] = p->force;
		snprintf(s->name[k], sizeof s->name[k], "%.*s", KP_PLAYER_NAME - 1, p->name);
	}
	for (k = 0; k < 8; k++) {
		s->user_select[k] = ng->user_select[k];
		s->color[k] = k;
	}
}

int kp_netgame_resumes(const KpNetGame *ng) { return saving(ng) && ng->header.save_id != 0; }

void kp_netgame_save_record(KpNetGame *ng, const KpGame *g, uint32_t save_id, KpNetGameSave *out)
{
	const KpGameSetup *s = &g->setup;
	int k, players = 0, computers = 0;
	memset(out, 0, sizeof *out);
	/* WriteSaveTagAndHeader 0x49a8e0's steps 2 and 3 on the live header */
	for (k = 0; k < KP_NETGAME_PLAYERS; k++) players += ng->players[k].state == 1; /* SNetGetNumPlayers' total */
	for (k = 0; k < 8; k++) computers += s->type[k] == KP_CTRL_COMPUTER;
	ng->header.id = 0;
	ng->header.save_id = save_id;
	ng->header.players = ng->header.max_players = (uint8_t)players;
	ng->header.save_players = (uint8_t)computers;
	out->header = ng->header;
	out->rng_state = g->rng ? (uint32_t)g->rng->state : 0;
	for (k = 0; k < KP_NETGAME_SLOTS; k++) {
		KpNetSlot *p = &out->players[k];
		p->slot = k;
		p->storm = (int32_t)s->storm[k];
		p->type = (uint8_t)s->type[k];
		p->race = (uint8_t)s->race[k];
		p->force = (uint8_t)s->force[k];
		copy(p->name, sizeof p->name, s->name[k]);
	}
	/* RecordSlotSummary 0x47e290 at the start: the lobby's local slot and slot types, the races after the random ones */
	out->local_slot = (uint8_t)ng->local_slot;
	for (k = 0; k < 8; k++) {
		out->types[k] = ng->slots[k].type;
		out->races[k] = (uint8_t)s->race_summary[k];
	}
	memcpy(out->forces, ng->forces, sizeof out->forces);
	memcpy(out->force_names, ng->force_names, sizeof out->force_names);
	out->tileset = ng->tileset;
	out->width = ng->width;
	out->height = ng->height;
	out->local_player = (uint32_t)g->local_record;
}

/* ForceHasHuman 0x4b2d20 / ForceHasComputer 0x4b2cf0 over a setup's slots 0..7 (a lobby computer counts) */
static int setup_force_has(const KpGameSetup *s, int64_t f, int computer)
{
	int k;
	for (k = 7; k >= 0; k--) {
		if (s->force[k] != f) continue;
		if (computer ? s->type[k] == KP_CTRL_COMPUTER || s->type[k] == KP_CTRL_LOBBY_COMPUTER : s->type[k] == KP_CTRL_HUMAN)
			return 1;
	}
	return 0;
}

/* GetForceLeaderSlot 0x4b2930: of slots 7..0 of force f played by a human or a computer, the lowest Storm ID
 * (unsigned: a computer's -1 is the highest; the lower slot on a tie), -1 none */
static int force_leader_slot(const KpGameSetup *s, int64_t f)
{
	int k, best = -1;
	uint32_t low = 0;
	for (k = 7; k >= 0; k--) {
		uint32_t storm = (uint32_t)s->storm[k];
		if (s->force[k] != f || (s->type[k] != KP_CTRL_HUMAN && s->type[k] != KP_CTRL_COMPUTER)) continue;
		if (best < 0 || storm <= low) {
			best = k;
			low = storm;
		}
	}
	return best;
}

/* A setup's record k from a lobby slot (0x6726c8's 36 bytes) */
static void setup_record(KpGameSetup *s, int k, const KpNetSlot *p)
{
	s->storm[k] = p->storm;
	s->type[k] = p->type;
	s->race[k] = p->race;
	s->force[k] = p->force;
	snprintf(s->name[k], sizeof s->name[k], "%.*s", KP_PLAYER_NAME - 1, p->name);
}

static void swap_records(KpGameSetup *s, int a, int b)
{
	int64_t storm = s->storm[a], type = s->type[a], race = s->race[a], force = s->force[a];
	char name[KP_SETUP_NAME];
	memcpy(name, s->name[a], sizeof name);
	s->storm[a] = s->storm[b];
	s->type[a] = s->type[b];
	s->race[a] = s->race[b];
	s->force[a] = s->force[b];
	memcpy(s->name[a], s->name[b], sizeof name);
	s->storm[b] = storm;
	s->type[b] = type;
	s->race[b] = race;
	s->force[b] = force;
	memcpy(s->name[b], name, sizeof name);
}

void kp_netgame_restore_slots(const KpNetGame *ng, KpGameSetup *s)
{
	KpNetSlot old[8];
	int k;
	int64_t f;
	if (!s->multiplayer) return;
	/* steps 1 and 2: the lobby's slots as the game's types, then every record emptied */
	memcpy(old, ng->slots, sizeof old);
	for (k = 7; k >= 0; k--) {
		if (old[k].type == KP_CTRL_LOBBY_COMPUTER) old[k].type = KP_CTRL_COMPUTER;
		else if (old[k].type == KP_CTRL_OPEN || old[k].type == KP_CTRL_CLOSED) old[k].type = KP_CTRL_NONE;
	}
	for (k = 0; k < KP_SETUP_SLOTS; k++) {
		s->storm[k] = s->type[k] = s->race[k] = s->force[k] = 0;
		memset(s->name[k], 0, sizeof s->name[k]);
	}
	/* step 3: each lobby slot into its saved record (the lower slot wins a record two name; 8 writes record 8) */
	for (k = 7; k >= 0; k--) {
		int t = old[k].type, d = ng->saved_slots[k];
		if (t != KP_CTRL_HUMAN && t != KP_CTRL_COMPUTER && t != KP_CTRL_RESCUE && t != KP_CTRL_RESCUE_ACTIVE
			&& t != KP_CTRL_NEUTRAL)
			continue;
		if (d < KP_SETUP_SLOTS) setup_record(s, d, &old[k]);
	}
	/* step 4 */
	for (k = KP_SETUP_SLOTS - 1; k >= 0; k--)
		if (s->type[k] != KP_CTRL_HUMAN) s->storm[k] = -1;
	if (!kp_setup_team_game(s)) return;
	/* step 7: a force whose saved leader slot has no human, the force's human with the lowest Storm ID moves there */
	for (f = 1; f <= 4; f++) {
		int64_t leader = s->leader[f];
		int h;
		if (setup_force_has(s, f, 1) || !setup_force_has(s, f, 0)) continue;
		if (leader < 0 || leader >= KP_SETUP_SLOTS || s->type[leader] == KP_CTRL_HUMAN) continue;
		if ((h = force_leader_slot(s, f)) >= 0) swap_records(s, (int)leader, h);
	}
}

/* AllPlayersReady 0x475c80 */
static int all_ready(KpNetGame *ng)
{
	int id;
	if (ng->briefing_state != BRIEFING_SENT) return 0;
	for (id = KP_NETGAME_PLAYERS - 1; id >= 0; id--)
		if ((ng->status[id] & STATUS_PRESENT) && !ng->ready[id]) return 0;
	ng->briefing_state = BRIEFING_IDLE;
	return 1;
}

void kp_netgame_briefing_arm(KpNetGame *ng)
{
	ng->briefing_state = BRIEFING_ARMED;
	memset(ng->ready, 0, sizeof ng->ready);
	send_turns(ng);
}

void kp_netgame_briefing_start(KpNetGame *ng, int abort)
{
	uint8_t m = KP_LOBBY_READY;
	if (abort) leave_storm(ng, LEAVE_ERROR);
	if (ng->briefing_state != BRIEFING_ARMED) return;
	if (abort) {
		ng->briefing_state = BRIEFING_IDLE;
		return;
	}
	queue_turn_command(ng, &m, 1);
	ng->briefing_state = BRIEFING_SENT;
}

int kp_netgame_briefing_poll(KpNetGame *ng)
{
	uint32_t t = now(ng);
	if (t - ng->briefing_clock < KP_NETGAME_POLL_MS) return 0;
	turn_step(ng);
	if (ng->received) ng->briefing_clock = t;
	return all_ready(ng);
}

void kp_netgame_abort(KpNetGame *ng)
{
	if (ng->offer) {
		kp_transfer_cancel(&ng->transfer, ng->offer);
		ng->offer = NULL;
	}
	kp_transfer_reset(&ng->transfer);
	flush_lobby(ng);
	discard_lobby(ng);
	leave_storm(ng, KP_NETGAME_LEAVE_CODE);
	stop_countdown(ng);
	ng->host = 0;
	ng->local_index = ng->local_slot = ng->local_storm = -1;
	memset(ng->slots, 0, sizeof ng->slots);
	ng->save_open = 0; /* CloseSaveFile 0x498a50 */
}

void kp_netgame_cancel(KpNetGame *ng)
{
	ng->lobby_state = KP_LOBBY_IDLE;
	stop_countdown(ng);
}

void kp_netgame_release(KpNetGame *ng)
{
	if (ng->offer) {
		kp_transfer_cancel(&ng->transfer, ng->offer);
		ng->offer = NULL;
	}
	kp_transfer_reset(&ng->transfer);
	ng->ban_count = 0;
}

/* --- the chat room's requests (uimulti.cpp, P41) --- */

int kp_netgame_request_force(KpNetGame *ng, int force)
{
	uint8_t m[2];
	if (force_has_computer_slot(ng, force)) return 0;
	m[0] = KP_LOBBY_FORCE;
	m[1] = (uint8_t)force;
	pend(ng, PEND_FORCE, m, sizeof m);
	return 1;
}

int kp_netgame_request_race(KpNetGame *ng, int slot, int race)
{
	uint8_t m[3];
	/* RequestLobbyRaceChange 0x453a00's own test, then QueueRaceChange 0x4716d0 */
	if (!kp_netgame_can_set_race(ng, slot, race, ng->local_slot)) return 0;
	m[0] = KP_LOBBY_RACE;
	m[1] = slot == ng->local_slot ? 8 : (uint8_t)slot;
	m[2] = (uint8_t)race;
	pend(ng, PEND_RACE, m, sizeof m);
	return 1;
}

int kp_netgame_request_team(KpNetGame *ng, int team)
{
	uint8_t m[2];
	if (force_has_computer(ng, team)) return 0;
	/* QueueTeamChange 0x4717b0: nothing for the local slot's own force */
	if (ng->local_slot >= 0 && ng->local_slot < KP_NETGAME_SLOTS && ng->slots[ng->local_slot].force == team) return 1;
	m[0] = KP_LOBBY_TEAM;
	m[1] = (uint8_t)team;
	pend(ng, PEND_TEAM, m, sizeof m);
	return 1;
}

static void queue_slot_action(KpNetGame *ng, int slot, int action)
{
	uint8_t m[3];
	m[0] = KP_LOBBY_SLOT_ACTION;
	m[1] = (uint8_t)slot;
	m[2] = (uint8_t)action;
	pend(ng, PEND_ACTION, m, sizeof m);
}

/* A booted human's name after the others' ("\n\t" between) */
static void add_name(char *names, size_t size, const char *name)
{
	size_t n;
	if (!names || !size) return;
	n = strlen(names);
	if (n && n + 2 < size) {
		memcpy(names + n, "\n\t", 2);
		names[n += 2] = 0;
	}
	if (n + 1 < size) copy(names + n, size - n, name);
}

int kp_netgame_slot_check(const KpNetGame *ng, int slot, int action, char *names, size_t size)
{
	const KpNetSlot *r;
	int j, boots = 0;
	if (names && size) names[0] = 0;
	if (slot < 0 || slot >= 8 || action < KP_SLOT_COMPUTER || action > KP_SLOT_BAN) return 0;
	r = &ng->slots[slot];
	if (action == KP_SLOT_BAN) { /* BanLobbySlot 0x4bb000 */
		if (r->type != KP_CTRL_HUMAN || slot == ng->local_slot) return 0;
		add_name(names, size, r->name);
		return 2;
	}
	if (action == KP_SLOT_COMPUTER) { /* SetLobbySlotComputer 0x4bb0e0 */
		int local_force = ng->local_slot >= 0 && ng->local_slot < 8 ? ng->slots[ng->local_slot].force : -1;
		if (r->type == KP_CTRL_LOBBY_COMPUTER) return 0;
		if (kp_netgame_team_game(ng) ? r->force == local_force : slot == ng->local_slot) return 0;
		if (kp_netgame_team_game(ng)) {
			for (j = 0; j < 8; j++)
				if (ng->slots[j].type == KP_CTRL_HUMAN && ng->slots[j].force == r->force) {
					add_name(names, size, ng->slots[j].name);
					boots++;
				}
			return boots ? 2 : 1;
		}
	} else if (r->type == (action == KP_SLOT_CLOSE ? KP_CTRL_CLOSED : KP_CTRL_OPEN) || slot == ng->local_slot) {
		return 0; /* CloseLobbySlot 0x4bb080, OpenLobbySlot 0x4bb290 */
	}
	if (r->type != KP_CTRL_HUMAN) return 1;
	add_name(names, size, r->name);
	return 2;
}

int kp_netgame_ban(KpNetGame *ng, int slot)
{
	const KpNetSlot *r;
	if (!kp_netgame_slot_check(ng, slot, KP_SLOT_BAN, NULL, 0)) return 0;
	r = &ng->slots[slot];
	/* AddIgnoredName 0x46fb10 (24 characters); Koprulu's bound drops the oldest name */
	if (ng->ban_count == KP_NETGAME_BANS) {
		memmove(ng->bans[0], ng->bans[1], sizeof ng->bans[0] * (KP_NETGAME_BANS - 1));
		ng->ban_count--;
	}
	copy(ng->bans[ng->ban_count++], KP_NETGAME_NAME, r->name);
	reject(ng, r->storm, KP_LEAVE_BANNED);
	queue_slot_action(ng, slot, KP_SLOT_OPEN);
	return 1;
}

int kp_netgame_slot_action(KpNetGame *ng, int slot, int action)
{
	int j;
	const KpNetSlot *r;
	if (action == KP_SLOT_BAN || !kp_netgame_slot_check(ng, slot, action, NULL, 0)) return 0;
	r = &ng->slots[slot];
	if (action == KP_SLOT_COMPUTER && kp_netgame_team_game(ng)) {
		int ids[8];
		for (j = 0; j < 8; j++)
			ids[j] = ng->slots[j].type == KP_CTRL_HUMAN && ng->slots[j].force == r->force ? ng->slots[j].storm : NO_SLOT;
		for (j = 0; j < 8; j++)
			if (ids[j] != NO_SLOT) reject(ng, ids[j], KP_LEAVE_BOOTED);
	} else if (r->type == KP_CTRL_HUMAN) {
		reject(ng, r->storm, KP_LEAVE_BOOTED);
	}
	queue_slot_action(ng, slot, action);
	return 1;
}

int kp_netgame_request_swap(KpNetGame *ng, int a, int b)
{
	uint8_t m[3];
	if (a < 0 || a >= KP_NETGAME_PLAYERS || b < 0 || b >= KP_NETGAME_PLAYERS || a == b) return 0;
	m[0] = KP_LOBBY_SWAP;
	m[1] = (uint8_t)a;
	m[2] = (uint8_t)b;
	pend(ng, PEND_SWAP, m, sizeof m);
	return 1;
}

int kp_netgame_request_public(KpNetGame *ng)
{
	uint8_t m = KP_LOBBY_PUBLIC;
	pend(ng, PEND_PUBLIC, &m, 1);
	return 1;
}

/* LobbyCommand 0x4509d0's Start checks: 0x451940(1) counts the humans of force 1 */
int kp_netgame_start_check(const KpNetGame *ng)
{
	int type = tbyte(ng, KP_TEMPLATE_TYPE);
	if (!ng->config.replay) {
		if (kp_netgame_participants(ng) < 2) return 0x72;
		if (type >= TEMPLATE_TYPE_LADDER_FIRST && type <= TEMPLATE_TYPE_LADDER_LAST &&
			count_type_in_force(ng, 1, KP_CTRL_HUMAN) < 2)
			return 0xba;
	}
	if (ng->offer && !kp_transfer_all_have(ng->offer)) return 0x73;
	return 0;
}

int kp_netgame_start(KpNetGame *ng)
{
	uint32_t mode = 0, size = 0;
	uint8_t m = KP_LOBBY_START_GAME;
	if (ng->offer && !kp_transfer_all_have(ng->offer)) return 0;
	if (!ng->net.game_info(ng->net.ud, KP_SNET_INFO_MODE, &mode, sizeof mode, &size)) {
		report_storm(ng, KP_NETTBL_NETWORK_ERROR, 1);
		return 0;
	}
	if (!ng->net.set_game_mode(ng->net.ud, mode | MODE_STARTED, 0)) report_storm(ng, KP_NETTBL_NETWORK_ERROR, 1);
	ng->start_pressed = 1;
	pend(ng, PEND_START, &m, 1);
	return 1;
}

/* --- kp_snet's calls --- */

static KpSnet *snet(void *ud) { return (KpSnet *)ud; }
static int c_create(void *ud, const char *name, const char *password, const char *stats, uint32_t game_type,
	uint32_t ladder_type, uint32_t mode, const void *tmpl, uint32_t tmpl_size, uint32_t max_players,
	const char *player_name, const char *player_desc, int *player_id)
{
	return kp_snet_create_game(snet(ud), name, password, stats, game_type, ladder_type, mode, tmpl, tmpl_size,
		max_players, player_name, player_desc, player_id);
}
static int c_join(void *ud, uint32_t game_id, const char *name, const char *password, const char *player_name,
	const char *player_desc, int *player_id)
{
	return kp_snet_join_game(snet(ud), game_id, name, password, player_name, player_desc, player_id);
}
static int c_leave(void *ud, uint32_t reason) { return kp_snet_leave_game(snet(ud), reason); }
static int c_enum(void *ud, int (*fn)(void *fn_ud, const KpSnetGame *g), void *fn_ud, uint32_t *hint)
{
	return kp_snet_enum_games(snet(ud), fn, fn_ud, hint);
}
static int c_send_turn(void *ud, const void *data, uint32_t size) { return kp_snet_send_turn(snet(ud), data, size); }
static int c_receive_turns(void *ud, int first, int count, const uint8_t **data, uint32_t *sizes, uint32_t *status)
{
	return kp_snet_receive_turns(snet(ud), first, count, data, sizes, status);
}
static int c_in_transit(void *ud, uint32_t *turns) { return kp_snet_turns_in_transit(snet(ud), turns); }
static int c_send_message(void *ud, int target, const void *data, uint32_t size)
{
	return kp_snet_send_message(snet(ud), target, data, size);
}
static int c_receive_message(void *ud, int *sender, const uint8_t **data, uint32_t *size)
{
	return kp_snet_receive_message(snet(ud), sender, data, size);
}
static int c_game_info(void *ud, int type, void *buf, uint32_t size, uint32_t *written)
{
	return kp_snet_game_info(snet(ud), type, buf, size, written);
}
static int c_set_mode(void *ud, uint32_t mode, int make_private) { return kp_snet_set_game_mode(snet(ud), mode, make_private); }
static int c_player_name(void *ud, int id, char *buf, uint32_t size) { return kp_snet_player_name(snet(ud), id, buf, size); }
static int c_provider_caps(void *ud, KpSnetCaps *caps) { return kp_snet_provider_caps(snet(ud), caps); }
static int c_player_caps(void *ud, int id, KpSnetCaps *caps) { return kp_snet_player_caps(snet(ud), id, caps); }
static int c_drop(void *ud, int id, uint32_t reason) { return kp_snet_drop_player(snet(ud), id, reason); }
static int c_disconnect(void *ud, uint32_t reason) { return kp_snet_disconnect_all(snet(ud), reason); }
static uint32_t c_error(void *ud) { return kp_snet_error(snet(ud)); }

KpNetCalls kp_netgame_snet_calls(KpSnet *s)
{
	KpNetCalls c;
	c.create_game = c_create;
	c.join_game = c_join;
	c.leave_game = c_leave;
	c.enum_games = c_enum;
	c.send_turn = c_send_turn;
	c.receive_turns = c_receive_turns;
	c.turns_in_transit = c_in_transit;
	c.send_message = c_send_message;
	c.receive_message = c_receive_message;
	c.game_info = c_game_info;
	c.set_game_mode = c_set_mode;
	c.player_name = c_player_name;
	c.provider_caps = c_provider_caps;
	c.player_caps = c_player_caps;
	c.drop_player = c_drop;
	c.disconnect_all = c_disconnect;
	c.error = c_error;
	c.ud = s;
	return c;
}
