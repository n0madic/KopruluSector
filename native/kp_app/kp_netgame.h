/*
 * The game's network layer over Storm's SNet (phase N; ADR 0026; atlas P24..P27, P14, P39, P41): what StarCraft.exe
 * does with SNet. Stage N3: the multiplayer lobby (the chat room's state, its turn stream of lobby messages 0x3c..0x48
 * and 0x5b, the chat room's direct messages 'I'..'P', the map transfer (kp_transfer.h), Storm's events, the leave and
 * drop handling before the game, the countdown and the start information 0x48 with the game's seed). Stage N4 adds the
 * game's turns. Every byte it sends is the original's: Koprulu plays with Koprulu and with StarCraft 1.17.
 *
 * A pure state machine over a table of SNet calls (KpNetCalls: kp_snet's, or a test's that answers from a recording),
 * the clock and the screens' hooks (KpNetGameHooks). It owns no thread and waits for nothing; the flow polls it from
 * the chat room's loop (kp_netgame_poll) and routes SNet's events to it (kp_netgame_event), which SNet dispatches
 * inside its receive calls.
 *
 * Where it differs from the original, only for inputs the original mishandles or for state no caller reads:
 *   - /reply and /r whisper to no one when no slot's name matches the reply name (the original whispers to whatever
 *     its stack held); over UDP the reply name (Storm ordinal 146's) stays empty, so they never match;
 *   - a chat-room message 'O' whose length WORD runs past the message is refused (the original reads past it);
 *   - the ban list holds at most KP_NETGAME_BANS names (the original's list grows without bound);
 *   - Battle.net's branches (the server chat commands, the ladder checks, the /astat line) are not ported;
 *   - the RNG state a join request carries (GetSavedRngState 0x499860) is the one of the save this game's lobbies read
 *     last; the original's is the last save header any screen probed (the Create Game list's saved games included),
 *     so a seat that only looked at a saved game there sends 0 where the original sends that save's.
 */
#ifndef KP_NETGAME_H
#define KP_NETGAME_H

#include <stddef.h>
#include <stdint.h>

#include "kp_mission.h"
#include "kp_snet.h"
#include "kp_template.h"
#include "kp_transfer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_NETGAME_SLOTS 12
#define KP_NETGAME_PLAYERS 8       /* Storm IDs 0..7 */
#define KP_NETGAME_NAME 25         /* a player's name, its NUL included (0x672690, a slot's +0xb) */
#define KP_NETGAME_GAME_NAME 24    /* the game header's name (+4) */
#define KP_NETGAME_TITLE 32        /* the map's title (+0x4d) */
#define KP_NETGAME_FORCE_NAME 30
#define KP_NETGAME_FORCES 4
#define KP_NETGAME_STATS 128       /* the stat string's buffer */
#define KP_NETGAME_TURN_MAX 0x200  /* the outgoing turn's capacity bound (0x67265c) */
#define KP_NETGAME_MIN_MESSAGE 0x80 /* InitNetworkProvider 0x4757e0 refuses a provider below it */
#define KP_NETGAME_POLL_MS 250     /* PollLobbyNetwork 0x46f3c0: one lobby step at most every 250 ms */
#define KP_NETGAME_STALL_MS 20000  /* and ResolveDesync after 20 s without a turn */
#define KP_NETGAME_ALIVE_MS 2500   /* ReceiveTurns 0x475ac0: alive this long after the last turn */
#define KP_NETGAME_COUNTDOWN 24    /* OnLobbyStartGame 0x4712d0: max(2L + 4, 24) turns */
#define KP_NETGAME_COUNTDOWN_SECONDS 5 /* StartLobbyCountdown 0x47a160's display */
#define KP_NETGAME_LOBBY_VERSION 5 /* the join request's version WORD */
#define KP_NETGAME_CHAT 0x78       /* a chat line or whisper, its NUL included */
#define KP_NETGAME_LINE 0x104      /* a formatted chat-room line */
#define KP_NETGAME_BANS 64         /* Koprulu's bound of the ban list */
#define KP_NETGAME_GAMES 64        /* the listed games kept (Storm's UDP list keeps KP_SNP_GAMES) */
#define KP_NETGAME_DROP_DESYNC 0x40000006u /* ResolveDesync's drop reason */
#define KP_NETGAME_LEAVE_CODE 0x40000001u  /* AbortNetworkGame's SNetLeaveGame code */

/* The lobby state byte 0x672600 */
enum {
	KP_LOBBY_IDLE = 1,     /* game creation, CancelLobby */
	KP_LOBBY_ENTERED,      /* InitJoinedLobby */
	KP_LOBBY_REQUESTED,    /* the join request sent */
	KP_LOBBY_IN,           /* a slot assigned: the lobby proper */
	KP_LOBBY_STARTING,     /* the start received */
	KP_LOBBY_LOCKED,       /* the slots locked */
	KP_LOBBY_START_INFO,   /* the host builds 0x48 */
	KP_LOBBY_WAITING,      /* waiting for the start */
	KP_LOBBY_START         /* start the game */
};

/* The lobby messages of the turn stream */
enum {
	KP_LOBBY_KEEP_ALIVE = 0x05,
	KP_LOBBY_SQUELCH = 0x3a, KP_LOBBY_UNSQUELCH = 0x3b,
	KP_LOBBY_START_GAME = 0x3c, KP_LOBBY_DOWNLOAD = 0x3d, KP_LOBBY_SLOT = 0x3e, KP_LOBBY_PLAYER_INFO = 0x3f,
	KP_LOBBY_JOIN = 0x40, KP_LOBBY_RACE = 0x41, KP_LOBBY_TEAM = 0x42, KP_LOBBY_FORCE = 0x43, KP_LOBBY_SLOT_ACTION = 0x44,
	KP_LOBBY_SWAP = 0x45, KP_LOBBY_START_INFO_MSG = 0x48, KP_LOBBY_READY = 0x54, KP_LOBBY_PUBLIC = 0x5b
};
/* The chat room's direct messages (ReceiveChatRoomMessage 0x471860; its results are the type byte, 0x51 none, 0x52
 * malformed, kp_netgame_poll's 0x53 the start) */
enum {
	KP_CHAT_JOINED = 'I', KP_CHAT_GAME_INFO = 'J', KP_CHAT_FORCE_NAMES = 'K', KP_CHAT_TEXT = 'L', KP_CHAT_WHISPER = 'M',
	KP_CHAT_LEAVE = 'N', KP_CHAT_TRANSFER = 'O', KP_CHAT_INFO_END = 'P', KP_CHAT_NONE = 0x51, KP_CHAT_MALFORMED = 0x52,
	KP_CHAT_START = 0x53
};
#define KP_NETGAME_START_INFO 13   /* the start information 0x48 */
#define KP_NETGAME_JOIN_SIZE 18    /* the join request 0x40 */
#define KP_NETGAME_PENDING 10      /* the pending lobby messages other than 0x3e and 0x3f */
#define KP_CHAT_GAME_INFO_SIZE 63
#define KP_CHAT_FORCE_NAMES_SIZE 121

/* SetLobbyLeaveReason 0x4506b0's reasons (gluAll 0x74..0x7d) */
enum {
	KP_LEAVE_BOOTED, KP_LEAVE_BANNED, KP_LEAVE_CLOSED, KP_LEAVE_HOST_LEFT, KP_LEAVE_UNABLE, KP_LEAVE_NO_SAVE,
	KP_LEAVE_WRITE_MAP, KP_LEAVE_VERSION, KP_LEAVE_SPAWN, KP_LEAVE_MAP_AUTH
};
/* The slot actions of 0x44, and the ban's check (kp_netgame_slot_check) */
enum { KP_SLOT_COMPUTER, KP_SLOT_OPEN, KP_SLOT_CLOSE, KP_SLOT_BAN };
/* The kinds of a chat-room line (AddLobbyChatMessage 0x451560: own, another player's, a system line, plain text) */
enum { KP_LINE_OWN, KP_LINE_PLAYER, KP_LINE_SYSTEM, KP_LINE_PLAIN };
/* The rez\network.tbl strings the lines and errors use (kp_netgame_hooks.text) */
enum {
	KP_NETTBL_FORCE = 0x16,      /* + force: "Force 1".. */
	KP_NETTBL_JOINED = 0x4a, KP_NETTBL_LEFT = 0x4b, KP_NETTBL_DROPPED = 0x4c, KP_NETTBL_IGNORING = 0x4d,
	KP_NETTBL_ACCEPTING = 0x4e, KP_NETTBL_TO = 0x4f, KP_NETTBL_FROM = 0x50,
	KP_NETTBL_NETWORK_ERROR = 0x51, KP_NETTBL_PROVIDER_CAPS = 0x55, KP_NETTBL_JOIN_FAILED = 0x59,
	KP_NETTBL_JOIN_VERSION = 0x5a, KP_NETTBL_SEND_FAILED = 0x5b, KP_NETTBL_INVALID_OPTIONS = 0x5d,
	KP_NETTBL_NO_SAVE = 0x5f, KP_NETTBL_LAUNCH = 0x60, KP_NETTBL_NO_MAP = 0x64
};

/* A player slot or a setup record (36 bytes at 0x6726c8 and 0x673d48) */
typedef struct {
	int32_t slot;  /* +0 */
	int32_t storm; /* +4: -1 none */
	uint8_t type, race, force; /* +8, +9, +0xa (KP_CTRL_*: the lobby's 5 computer, 6 open, 8 closed) */
	char name[KP_NETGAME_NAME]; /* +0xb */
} KpNetSlot;

/* A record of the network player table 0x673818 (state 0 none, 1 active, 2 left) */
typedef struct {
	uint8_t state;
	uint16_t flags, w4, version; /* +2, +4, +6: the join request's net flags, its WORD 1 and version 5 */
	char name[KP_NETGAME_NAME];
} KpNetPlayer;

/* The game header 0x6728d0 (KP_NETGAME_HEADER bytes); +0x1c..+0x50 is the stat record the stat string carries */
#define KP_NETGAME_HEADER 0x8d
typedef struct {
	uint32_t id;
	char name[KP_NETGAME_GAME_NAME];
	uint32_t save_id;
	uint16_t width, height;
	uint8_t players, max_players, speed, map_class;
	uint8_t type, sub;
	uint16_t subtype;
	uint32_t key_hash;
	uint16_t tileset;
	uint8_t replay;
	uint8_t save_players; /* +0x33: a saved game's computers (WriteSaveTagAndHeader 0x49a8e0), not in the stat string */
	char creator[KP_NETGAME_NAME];
	char title[KP_NETGAME_TITLE];
	uint8_t template_record[32];
} KpNetGameHeader;

/* What LoadMapHeader 0x46b5c0 leaves for a lobby (the Create Game screen's map): the setup records, the forces
 * 0x534890 (8 slots, then the 4 force flags), the force names, the User Select slots and the map's path */
typedef struct {
	uint16_t tileset, width, height;
	KpNetSlot setup[KP_NETGAME_SLOTS];
	uint8_t forces[KP_NETGAME_SLOTS];
	char force_names[KP_NETGAME_FORCES][KP_NETGAME_FORCE_NAME];
	uint8_t user_select[8];
	char title[KP_NETGAME_TITLE];
	char path[KP_TRANSFER_PATH];
} KpNetMap;

/* The SNet calls the game makes (kp_snet.h's signatures over ud). */
typedef struct {
	int (*create_game)(void *ud, const char *name, const char *password, const char *stats, uint32_t game_type,
		uint32_t ladder_type, uint32_t mode, const void *tmpl, uint32_t tmpl_size, uint32_t max_players,
		const char *player_name, const char *player_desc, int *player_id);
	int (*join_game)(void *ud, uint32_t game_id, const char *name, const char *password, const char *player_name,
		const char *player_desc, int *player_id);
	int (*leave_game)(void *ud, uint32_t reason);
	int (*enum_games)(void *ud, int (*fn)(void *fn_ud, const KpSnetGame *g), void *fn_ud, uint32_t *hint);
	int (*send_turn)(void *ud, const void *data, uint32_t size);
	int (*receive_turns)(void *ud, int first, int count, const uint8_t **data, uint32_t *sizes, uint32_t *status);
	int (*turns_in_transit)(void *ud, uint32_t *turns);
	int (*send_message)(void *ud, int target, const void *data, uint32_t size);
	int (*receive_message)(void *ud, int *sender, const uint8_t **data, uint32_t *size);
	int (*game_info)(void *ud, int type, void *buf, uint32_t size, uint32_t *written);
	int (*set_game_mode)(void *ud, uint32_t mode, int make_private);
	int (*player_name)(void *ud, int id, char *buf, uint32_t size);
	int (*provider_caps)(void *ud, KpSnetCaps *caps);
	int (*player_caps)(void *ud, int id, KpSnetCaps *caps); /* the chat room's latency bars' (NULL: none) */
	int (*drop_player)(void *ud, int id, uint32_t reason);
	int (*disconnect_all)(void *ud, uint32_t reason);
	uint32_t (*error)(void *ud);
	void *ud;
} KpNetCalls;

/* kp_snet's calls over s. */
KpNetCalls kp_netgame_snet_calls(KpSnet *s);

/* A saved network game as ReadSaveHeaderAndGameBlock 0x499c50 leaves it for the lobby that resumes it: its game header,
 * the save header's RNG state, the player table (which the host's lobby rebuilds its slots from) and the fields of the
 * core block 0x5263d0 the lobby reads. kp_netsave.h reads the original's files into it. */
typedef struct {
	KpNetGameHeader header;   /* the save header's +0xc, copied to 0x6728d0 (+0x1c: the save's ID) */
	uint32_t rng_state;       /* the save header's +4 (GetSavedRngState 0x499860): the joins' identity token */
	KpNetSlot players[KP_NETGAME_SLOTS]; /* 0x6726c8 when the game was saved */
	uint8_t local_slot;       /* 0x526490 (RecordSlotSummary 0x47e290's local player) */
	uint8_t types[KP_NETGAME_SLOTS], races[KP_NETGAME_SLOTS]; /* 0x526494, 0x5264a0: the lobby's at the game's start */
	uint8_t forces[KP_NETGAME_SLOTS]; /* 0x534890: the slots' forces, then the 4 force flags */
	char force_names[KP_NETGAME_FORCES][KP_NETGAME_FORCE_NAME]; /* 0x53489c */
	uint16_t tileset, width, height; /* 0x5264bc, 0x5264b4, 0x5264b6 */
	uint32_t local_player;    /* the DWORD after the core block (GetSavedLocalPlayer 0x499850): this seat's record */
} KpNetGameSave;

/* The clock, the strings and the screens' side of the chat room. Every hook may be NULL except now. */
typedef struct {
	uint32_t (*now)(void *ud);  /* GetTickCount */
	uint32_t (*time)(void *ud); /* time(NULL): the seed the host's 0x48 carries (NULL: 0) */
	/* rez\network.tbl string `index` (KP_NETTBL_*), NULL none: the lines and errors are formatted from it */
	const char *(*text)(void *ud, int index);
	void (*refresh)(void *ud);          /* RequestLobbyRefresh 0x451790: the rows changed */
	void (*password_refresh)(void *ud); /* RequestLobbyPasswordRefresh 0x451780 */
	/* AppendLobbyChatLine 0x451140: a chat-room line of a kind (KP_LINE_*) */
	void (*line)(void *ud, int kind, const char *text);
	/* SetLobbyLeaveReason 0x4506b0: the room closes with this reason (KP_LEAVE_*) at its next pass */
	void (*leave)(void *ud, int reason);
	void (*download)(void *ud, int storm, int pct); /* SetSlotDownloadPercent 0x4523a0 */
	/* StartLobbyCountdown 0x47a160 (seconds 5) and StopLobbyCountdown 0x47a210 (seconds -1): the display's count */
	void (*countdown)(void *ud, int seconds);
	void (*swap)(void *ud, int a, int b);  /* OnLobbySwapSlots 0x471150 step 1: close the slot combo of a or b */
	void (*lock)(void *ud);                /* LockLobbySlotsThunk 0x451770: the countdown locks the slots' rows */
	void (*force_names)(void *ud);         /* 'K' arrived: the force headers changed */
	/* ReportNetError 0x474a00: network.tbl `index` with an optional detail (Storm's error text), fatal: leave and close
	 * the screens */
	void (*error)(void *ud, int index, const char *detail, int fatal);
	/* DecodeSpawnResources 0x4635d0's decodes GetSpawnKeyHash 0x4634e0 runs (CreateMultiplayerGame, BuildJoinRequest
	 * 0x4714b0): each reseeds the glue's CRT generator and draws from it */
	void (*key_draws)(void *ud, int decodes);
	/* FindMultiplayerSaveById 0x4995e0, LoadSavedGameByName 0x498bd0 and ReadSaveHeaderAndGameBlock 0x499c50: this
	 * seat's copy of the saved game `id` read into out; 1, or 0 when none is found or it does not read (NULL: this
	 * seat has no saves, as a seat without the file) */
	int (*find_save)(void *ud, uint32_t id, KpNetGameSave *out);
	void *ud;
} KpNetGameHooks;

typedef struct {
	char player_name[KP_NETGAME_NAME]; /* SetLocalPlayerName 0x475750: the profile's name */
	/* GetSpawnKeyHash 0x4634e0: SStrHash of the CD key font.gid holds; the join request and the stat string carry it,
	 * only a spawned copy compares it. TODO: compute it from the install's font.gid (0 meanwhile, as an install
	 * without one). */
	uint32_t key_hash;
	const KpTemplates *templates; /* FindTemplateRecord 0x447b50, ReceiveTemplateRecord 0x448000 */
	KpTransferIo transfer;        /* the map transfer's files */
	int replay;                   /* the replay flag 0x67cc90 of the hosted or joined game */
	uint8_t replay_seed[KP_NETGAME_START_INFO]; /* GetReplayGameSeedRecord 0x480540: the replay header's 0x48 record */
} KpNetGameConfig;

typedef struct {
	KpNetCalls net;
	KpNetGameHooks hooks;
	KpNetGameConfig config;

	/* the provider (InitNetworkProvider 0x4757e0) */
	uint32_t max_message; /* 0x67265c: min(caps.maxmsg, 0x200) */
	uint32_t transit;     /* 0x672660: the turns-in-transit target L */

	/* the session (net_glue) */
	int host;             /* 0x672970: this seat created the game */
	int start_pressed;    /* 0x672964 */
	int local_storm;      /* 0x508d58 */
	int local_slot, local_index; /* 0x508d50, 0x508d54 */
	KpNetGameHeader header; /* 0x6728d0 */
	uint8_t requested_key[4]; /* 0x6728f8..0x6728fb: the joined game's template key */
	int error_latch;      /* 0x6728b8: a fatal network error was reported */
	int in_chat_room;     /* the glue state 0x66a274 is 3: the chat room shows (its lines, its refreshes) */
	int in_game;          /* the mode word 0x6662b8 is 3 (stage N4); 0: glue mode 4 */
	int leave_reason;     /* SetLobbyLeaveReason 0x4506b0's pending reason (0x667c20, 0x667c24), -1 none */
	int save_open;        /* 0x6a5e34: the lobby resumes `save` (InitJoinedLobby 0x474c60 read it; CloseSaveFile
	                       * 0x498a50 at AbortNetworkGame) */
	KpNetGameSave save;
	uint32_t save_rng;    /* 0x6a5e40 (GetSavedRngState 0x499860): the RNG state of the save read last, kept after its
	                       * lobby; every join request carries it */
	uint32_t seed;        /* SetInitialSeed 0x4781f0 from the host's 0x48 */
	uint8_t start_info[KP_NETGAME_START_INFO]; /* SetReplayGameSeedRecord 0x4807f0: the 0x48 the replay records */
	uint8_t drop_vote;    /* 0x529041: ResolveDesync's 1 (even) or 2 (this seat in the minority) */
	uint32_t refused;     /* inputs refused by Koprulu's bounds (the header's list), for the tests' and fuzzers' view */

	/* the slots (net_record) */
	KpNetSlot slots[KP_NETGAME_SLOTS];  /* 0x6726c8 */
	KpNetSlot setup[KP_NETGAME_SLOTS];  /* 0x673d48 */
	uint8_t setup_types[KP_NETGAME_SLOTS]; /* 0x672610 */
	uint8_t forces[KP_NETGAME_SLOTS];   /* 0x534890: the slot forces, then the 4 force flags */
	char force_names[KP_NETGAME_FORCES][KP_NETGAME_FORCE_NAME]; /* 0x53489c */
	uint8_t user_select[8];             /* 0x673f08 */
	uint16_t tileset, width, height;    /* 0x5264bc, 0x5264b4, 0x5264b6 */
	uint8_t storm_slot[KP_NETGAME_PLAYERS], storm_index[KP_NETGAME_PLAYERS]; /* 0x672878, 0x672898 */
	uint8_t saved_slots[8];             /* 0x672608 */
	KpNetPlayer players[KP_NETGAME_PLAYERS]; /* 0x673818 */
	uint32_t leave_reasons[KP_NETGAME_PLAYERS]; /* 0x6737f4 */
	uint8_t results[8];                 /* 0x5349e0: cleared for a new game */
	KpNetSlot replay_slots[KP_NETGAME_SLOTS]; /* 0x66fd50: a replay's slots when its start arrived */
	char map_path[KP_TRANSFER_PATH];    /* 0x52701c */

	/* the lobby (net_chat_cmds) */
	uint8_t lobby_state;  /* 0x672600 */
	uint32_t countdown;   /* 0x672604 */
	uint8_t last_download; /* 0x672601 */
	uint32_t poll_clock;  /* 0x672590 */
	int countdown_shown;  /* 0x673f14: the display's timer exists */
	/* the pending lobby messages (0x672598..0x6725fc): the slot updates and player records by bit, the others by
	 * their length (0 none) */
	uint8_t pending_slot[8][6], pending_info[8][8];
	uint8_t pending_slots, pending_infos;
	uint8_t pending[KP_NETGAME_PENDING][KP_NETGAME_JOIN_SIZE];
	uint8_t pending_size[KP_NETGAME_PENDING];
	uint8_t ready[KP_NETGAME_PLAYERS];  /* 0x672bdc: the 0x54 of each Storm ID (the race ready room) */
	uint8_t briefing_state; /* 0x672986: 0 idle, 1 armed, 2 the local 0x54 queued */
	uint32_t briefing_clock; /* 0x6737ec: the ready room's last turn */
	uint16_t chat_mask;   /* 0x5264b8: the Storm IDs whose lines show */
	int ban_count;
	char bans[KP_NETGAME_BANS][KP_NETGAME_NAME]; /* 0x508d2c, the oldest first */

	/* the turns (net_mgr) */
	uint8_t out[KP_NETGAME_TURN_MAX]; /* 0x6729d8 */
	uint32_t out_size;    /* 0x6729d4 */
	uint32_t turn;        /* 0x672658 */
	int received;         /* 0x672665 */
	uint32_t last_turn;   /* 0x6737e8 */
	uint32_t status[KP_NETGAME_PLAYERS]; /* 0x672670 */
	const uint8_t *turn_data[KP_NETGAME_PLAYERS]; /* 0x672994 */
	uint32_t turn_size[KP_NETGAME_PLAYERS];       /* 0x6729b4 */
	uint32_t extra_latency; /* 0x672990 (stage N4) */

	/* the map transfer */
	KpTransfer transfer;
	KpTransferOffer *offer; /* 0x672960 */

	int listed_count;
	KpNetGameHeader listed[KP_NETGAME_GAMES]; /* 0x508d5c, sorted by name: each with Storm's live player counts */
} KpNetGame;

/* The game's state for a provider whose SNet is `net` (InitNetworkProvider 0x4757e0's steps 4..: the caps read, L
 * set). 0, or -1 when the caps fail or the maximum message is below 0x80 (network.tbl 0x55 reported). */
int kp_netgame_init(KpNetGame *ng, const KpNetCalls *net, const KpNetGameHooks *hooks, const KpNetGameConfig *config);
/* Everything freed (the transfers). */
void kp_netgame_free(KpNetGame *ng);

/* The game header's KP_NETGAME_HEADER bytes (a save's, a replay's) into h, its strings cut at their NUL or their
 * field's end; and h written back (its strings' unused bytes 0). */
void kp_netgame_header_read(const uint8_t *raw, KpNetGameHeader *h);
void kp_netgame_header_write(const KpNetGameHeader *h, uint8_t *raw);
/* The stat string (BuildGameStatString 0x46f470) of a header into out (KP_NETGAME_STATS). */
void kp_netgame_stat_build(const KpNetGameHeader *h, char *out, size_t size);
/* ParseGameStatString 0x46f6f0 into the header's stat record: 1 well formed and valid, else 0. */
int kp_netgame_stat_parse(const char *stats, KpNetGameHeader *h);
/* ValidateNetString 0x476e20: the length with the NUL, 0 refused. */
uint32_t kp_netgame_net_string(const char *s, uint32_t max, uint32_t available, int formatting, int controls);

/* LoadMapHeader 0x46b5c0 and LoadForceChunk 0x443320 over a map's header: the setup records, forces, force names
 * (the map's, or network.tbl's "Force n" through the hooks' text) and User Select slots of the map at path. */
void kp_netgame_map(KpNetMap *m, const KpMapHeader *h, const char *path, const KpNetGameHooks *hooks);
/* The map as the lobby's (LoadMapHeader's globals): its setup records copied to the slots (CopyMapSetupToSlots). */
void kp_netgame_set_map(KpNetGame *ng, const KpNetMap *m);

/* CreateMultiplayerGame 0x475040 of a header the Create Game screen filled (name, size, speed, class, template key,
 * tileset, title; the map set first): 1 created, 0 not (reported, or Storm's error left for the screen). */
int kp_netgame_create(KpNetGame *ng, const KpNetGameHeader *h, const char *password);
/* EnumNetworkGames 0x475570: the listed games refreshed (ng->listed, sorted by name, the newest first among equal; a
 * game whose stat string is malformed or invalid is not listed). */
void kp_netgame_enum(KpNetGame *ng);
/* JoinNetworkGame 0x475610 of a listed game: 1 joined, 0 not (reported). */
int kp_netgame_join(KpNetGame *ng, const KpNetGameHeader *listed);
/* InitJoinedLobby 0x474c60: the chat room opens; 1, or 0 (aborted, reported or a leave reason set). A game that resumes
 * a save (its header's save ID) needs this seat's copy (the hooks' find_save; network.tbl 0x5f "Save Game File Not
 * Found." without it): its header becomes the game's and the host's slots are the save's players (RebuildLobbySlotsFromSave
 * 0x479840, RebuildSlotsFromSaveSummary 0x479ce0). */
int kp_netgame_enter(KpNetGame *ng);
/* PollLobby 0x474f00: one pass of the chat room. KP_CHAT_START when the game starts (the lobby's messages dropped),
 * otherwise ReceiveChatRoomMessage 0x471860's result. */
int kp_netgame_poll(KpNetGame *ng);
/* AbortNetworkGame 0x474e60: leave the game, the lobby reset. */
void kp_netgame_abort(KpNetGame *ng);
/* CancelLobby 0x4baf60. */
void kp_netgame_cancel(KpNetGame *ng);
/* ReleaseLobbySession 0x474c30: the chat room closed. */
void kp_netgame_release(KpNetGame *ng);
/* SNet's event handlers (InitNetworkProvider's registrations: 1 ReceiveTemplateRecord, 2 OnPlayerJoined, 3 OnPlayerLeft,
 * 4 OnServerMessage). */
void kp_netgame_event(KpNetGame *ng, const KpSnetEvent *e);

/* The chat room's requests (uimulti.cpp 0x4bae80..0x4bb2f0, LobbyCommand 0x4509d0): 1 done or queued, 0 refused. */
int kp_netgame_request_race(KpNetGame *ng, int slot, int race);  /* RequestLobbyRaceChange 0x453a00 */
int kp_netgame_request_team(KpNetGame *ng, int team);            /* RequestTeamChange 0x4baeb0 */
int kp_netgame_request_force(KpNetGame *ng, int force);          /* RequestForceChange 0x4bae80 */
/* BanLobbySlot 0x4bb000, CloseLobbySlot 0x4bb080, SetLobbySlotComputer 0x4bb0e0, OpenLobbySlot 0x4bb290; the
 * confirmation popup (ConfirmLobbySlotAction 0x451640) is the screen's, before the call. */
int kp_netgame_ban(KpNetGame *ng, int slot);
int kp_netgame_slot_action(KpNetGame *ng, int slot, int action);
/* Their checks before the confirmation (KP_SLOT_*; the ban's KP_SLOT_BAN): 0 refused, 1 to do, 2 to do once the player
 * confirms the boot of the names written to names (ConfirmLobbySlotAction's "%s": the booted humans' names separated
 * by "\n\t", the slots' order; names may be NULL). */
int kp_netgame_slot_check(const KpNetGame *ng, int slot, int action, char *names, size_t size);
int kp_netgame_request_swap(KpNetGame *ng, int a, int b);       /* RequestSlotSwap 0x4bb2f0 */
int kp_netgame_request_public(KpNetGame *ng);                   /* RequestMakePublic 0x4baf90 */
/* LobbyCommand's Start checks: 0 ready, else the gluAll string of the refusal (0x72 no opponent, 0xba not enough
 * players, 0x73 the map still being distributed). */
int kp_netgame_start_check(const KpNetGame *ng);
int kp_netgame_start(KpNetGame *ng);                            /* StartLobbyGame 0x4baed0 */
/* SendChatRoomText 0x471e10: a chat line or a slash command. */
void kp_netgame_chat(KpNetGame *ng, const char *text);
/* PollLobby 0x474f00's step 3 after KP_CHAT_START: the race ready room (glue state 8 Zerg, 9 Protoss, 7 another race)
 * of a Use Map Settings game that resumes no save and plays no replay, 0 when the game starts at once. */
int kp_netgame_ready_room(const KpNetGame *ng);
/* The game the lobby started as StartGame 0x443f40 finds it (ADR 0024's KpGameSetup, `started` 0): the template record,
 * the multiplayer byte, the header's player slots and the player records 0x6726c8 (Storm ID, type, race, force, name)
 * with the User Select slots; the start resolves the random races and slots from the 0x48 seed. */
void kp_netgame_game_setup(const KpNetGame *ng, KpGameSetup *s);
/* GetCurrentMapPath 0x46b0b0: the map the game plays, the host's own or the copy a joiner found or received
 * (SetCurrentMapPath 0x46b8d0 from the transfer), "" none yet. */
const char *kp_netgame_map_path(const KpNetGame *ng);
/* The race ready room's handshake (net_mgr.cpp; the briefing's screen glurdy.cpp drives it): ArmBriefingReady
 * 0x475d10 when the room has loaded (the ready bytes cleared, turns sent); SendBriefingStart 0x475cc0 when the
 * briefing ends or the player presses Start (0x54 queued) or cancels (abort: Storm left); PollBriefingTurn 0x476b20
 * each pass of the room: a turn step at most every 250 ms, 1 when every present player's 0x54 arrived (AllPlayersReady
 * 0x475c80: the room then starts the game). */
void kp_netgame_briefing_arm(KpNetGame *ng);
void kp_netgame_briefing_start(KpNetGame *ng, int abort);
int kp_netgame_briefing_poll(KpNetGame *ng);

/* Queries the screens make. */
int kp_netgame_open_slots(const KpNetGame *ng);         /* CountOpenPlayerSlots 0x479550 */
int kp_netgame_participants(const KpNetGame *ng);       /* CountLobbyParticipants 0x451a00 */
const char *kp_netgame_player_name(const KpNetGame *ng, int storm); /* GetNetPlayerRecordName 0x478050 (NULL none) */
int kp_netgame_player_active(const KpNetGame *ng, int storm);      /* IsNetPlayerActive 0x4780a0 */
int kp_netgame_ums(const KpNetGame *ng);                /* IsUseMapSettings 0x447db0 of the game's template */
int kp_netgame_team_game(const KpNetGame *ng);          /* IsTeamGame 0x4b2d90 */
int kp_netgame_can_set_race(const KpNetGame *ng, int target, int race, int requester); /* CanSetSlotRace 0x447500 */

#ifdef __cplusplus
}
#endif

#endif
