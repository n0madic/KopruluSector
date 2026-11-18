/* kp_net_lobby: the network tests' lobby seats (phase N, stage N3): KpNetGame with the screens' side the chat room
 * needs (the clock, the test's own network.tbl lines, the map's files), the pass the chat room and the race ready room
 * make each frame as the screens do, and the host's and the joiners' steps over a kp_net_world LAN. The test map and
 * templates are the test's own: a map of 4 open slots with made-up bytes, a melee template of made-up values and the
 * Use Map Settings record. test_netgame plays its lobbies with it; the lobby fuzzer replays a seat's recorded session
 * through the same pass and writes its seeds from such lobbies. */
#ifndef KP_NET_LOBBY_H
#define KP_NET_LOBBY_H

#include <stddef.h>
#include <stdint.h>

#include "kp_net_world.h"
#include "kp_netgame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_NET_LOBBY_MAP_NAME "test.scm"
#define KP_NET_LOBBY_MAP_PATH "maps\\" KP_NET_LOBBY_MAP_NAME
#define KP_NET_LOBBY_DOWNLOADS "maps\\download\\"
#define KP_NET_LOBBY_MAP_SIZE 3000  /* 24 chunks of 128 bytes: more than the transfer's window of 8 */
#define KP_NET_LOBBY_MAP_SLOTS 4
#define KP_NET_LOBBY_GAME "Test Game"
#define KP_NET_LOBBY_TITLE "Test Map"
#define KP_NET_LOBBY_TIME 0x5f3759dfu /* the host's time(NULL): the seed its 0x48 carries */
#define KP_NET_LOBBY_LINES 32
#define KP_NET_LOBBY_LIST_MS 1500     /* the host's advertisement listed (kp_snet takes a connect again after 1000 ms) */
#define KP_NET_LOBBY_MAP_SEED 0x4b504c42u /* the test map's bytes */
#define KP_NET_LOBBY_SAVE_BANNER "Test saved game\r\n" /* a save file's banner (the test's own) */
#define KP_NET_LOBBY_SAVE_ID 0x2f6a51c3u  /* a resumed game's ID (the test's own) */
#define KP_NET_LOBBY_SAVE_RNG 0x1234abcdu /* its RNG state */

/* The test's templates (KpNetLobby's) */
enum { KP_NET_LOBBY_MELEE, KP_NET_LOBBY_UMS, KP_NET_LOBBY_TEMPLATES };
/* The phases of a seat's screens */
enum { KP_NET_LOBBY_MENU, KP_NET_LOBBY_ROOM, KP_NET_LOBBY_READY_ROOM, KP_NET_LOBBY_STARTED, KP_NET_LOBBY_LEFT };

typedef struct {
	KpNetSeat n; /* first: the world's callbacks cast it back */
	KpNetGame game;
	int inited, phase;
	int leave;       /* the room's leave reason (KP_LEAVE_*), -1 none */
	int errors;      /* network.tbl errors reported */
	int has_map;     /* the seat has the host's map in its maps folder */
	int press_start; /* the ready room's Start pressed, its 0x54 not queued yet */
	int bad_saves;   /* downloads saved under a name the transfer must refuse (kp_transfer_name_valid) */
	const uint8_t *map;    /* the host's map bytes (KP_NET_LOBBY_MAP_SIZE, or map_size) */
	const char *map_name;  /* the map's file name in the seat's maps folder, NULL the test map's; with map_size set by
	                        * the caller (a real map's), kept by kp_net_lobby_seat_init */
	size_t map_size;
	const KpNetGameSave *save; /* the save in the seat's maps folder (its find_save's), NULL none; set by the caller,
	                            * kept by kp_net_lobby_seat_init */
	const uint32_t *clock; /* a clock of the caller's, NULL: the world's */
	uint8_t *download;
	size_t download_size;
	char lines[KP_NET_LOBBY_LINES][KP_NETGAME_LINE];
	int line_count;
} KpNetLobbySeat;

typedef struct {
	KpNetWorld n; /* its seats are seat[k].n */
	KpNetLobbySeat seat[KP_NET_WORLD_SEATS];
	KpTemplates templates;
	uint8_t map[KP_NET_LOBBY_MAP_SIZE];
} KpNetLobby;

/* The players' names: seat k's */
extern const char *const KP_NET_LOBBY_NAMES[KP_NET_WORLD_SEATS];

void kp_net_lobby_templates(KpTemplates *t);
/* The test map's bytes of a seed (made-up), and its lobby map (KpNetMap at KP_NET_LOBBY_MAP_PATH): 4 open slots of
 * User Select races, forces 1 and 2 named "Left Team" and "Right Team". */
void kp_net_lobby_map_bytes(uint8_t *map, size_t size, uint32_t seed);
void kp_net_lobby_map(KpNetMap *m, const KpNetGameHooks *hooks);
/* The Create Game screen's header of the test game under a template record. */
void kp_net_lobby_header(KpNetGameHeader *h, const uint8_t *template_record);

/* A seat's KpNetGame over calls (NULL: the seat's kp_snet), the seat's player named, its screens' hooks and files: 1,
 * or 0 when the provider's caps refuse it. A seat already inited is freed first. */
int kp_net_lobby_seat_init(KpNetLobbySeat *s, const char *name, const KpTemplates *templates, const uint8_t *map,
	int has_map, const KpNetCalls *calls);
void kp_net_lobby_seat_free(KpNetLobbySeat *s);
/* The screens' pass of a frame: the chat room's poll (its start: the room released, the ready room armed or the game
 * started; its leave reason: the game aborted), or the ready room's (its Start, its poll). */
void kp_net_lobby_frame(KpNetLobbySeat *s);
/* The host's steps on a seat inited with its map: the map set, the game created (the seat's save resumed when it has
 * one: the save's header under the test game's name, the template index unused), the chat room entered: 1, or 0. */
int kp_net_lobby_create(KpNetLobbySeat *s, const KpTemplates *templates, int template_index);
/* The joiner's: the game list's row of the test game (NULL none), then the join (the world's in_join set around it, as
 * Storm's handshake pumps the seat alone) and the chat room entered: 1, or 0 when Storm refused the join. */
const KpNetGameHeader *kp_net_lobby_listed(const KpNetGame *ng);
int kp_net_lobby_enter(KpNetLobbySeat *s, const KpNetGameHeader *listed);

/* A lobby of no seat on the system's sockets (the live checks): the caller adds each seat with kp_net_world_add over
 * its socket's io. */
void kp_net_lobby_init_sockets(KpNetLobby *l, struct KpT *T, void (*fail)(void *ud, const char *why), void *fail_ud);
/* A lobby of `seats` seats on a LAN of a seed (lossy: 10% of the datagrams lost, 5..35 ms late): 1, or 0 failed (the
 * world's fail callback told). */
int kp_net_lobby_init(KpNetLobby *l, struct KpT *T, void (*fail)(void *ud, const char *why), void *fail_ud,
	uint64_t seed, int seats, int lossy);
void kp_net_lobby_free(KpNetLobby *l);
uint32_t kp_net_lobby_now(const KpNetLobby *l);
void kp_net_lobby_advance(KpNetLobby *l, uint32_t ms);
/* Seat k hosts the test game under a template / joins it after the list's wait: 1, or 0 (refused, not listed). */
int kp_net_lobby_host(KpNetLobby *l, int k, int template_index, const KpNetCalls *calls);
int kp_net_lobby_join(KpNetLobby *l, int k, int has_map, const KpNetCalls *calls);
/* Until no seat waits in the chat room or the ready room, at most ms. */
void kp_net_lobby_run_to_start(KpNetLobby *l, uint32_t ms);
/* A resumed game's save of the test map under a template (KP_NET_LOBBY_SAVE_ID, KP_NET_LOBBY_SAVE_RNG, the test
 * game's name, forces 1 and 2 named as the map's): the local seat's slot and record `local`, no players yet; a player
 * record k (and the saved lobby's type and race of slot k); a melee save of three humans (records 0 Alpha, 2 Beta,
 * 4 Gamma), a computer (1) and a rescuable (3). */
void kp_net_lobby_save_init(KpNetGameSave *s, const KpTemplates *t, int template_index, int local);
void kp_net_lobby_save_player(KpNetGameSave *s, int k, uint8_t type, uint8_t race, uint8_t force, const char *name);
void kp_net_lobby_melee_save(KpNetGameSave *s, const KpTemplates *t, int local);
/* A save's file as the original writes one (kp_netsave.h): the test's banner, the tag, the header block (the version
 * `version`, the RNG state, the game header, the display name `name`), the player table, a core block holding the
 * fields the lobby reads (its Brood War byte `expansion`) and the local player. NULL out of memory; the caller frees
 * it. */
uint8_t *kp_net_lobby_save_file(const KpNetGameSave *s, int version, const char *name, int expansion, size_t *size);

/* The slot of seat k's player as seat `view` sees it, -1 none. */
int kp_net_lobby_slot_of(const KpNetLobby *l, int view, int k);

#ifdef __cplusplus
}
#endif

#endif
