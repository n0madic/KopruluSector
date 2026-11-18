/* test_net_live: the mixed lobby (phase N, stage N3.5): Koprulu's lobby with the emulated 1.17's over the network
 * oracle's loopback bridge (tools/research/sc_emulator net --bridge BASE --realtime). tools/net_live.py runs the
 * emulator and these tests together and compares the starts: each test writes Koprulu's start (the seed, the start
 * information, the game header, the twelve player records, the map it plays) to KOPRULU_NET_LIVE_OUT, and the script
 * compares it with each emulated player's (net.py's setup.json, taken at StartGame 0x443f40).
 *
 * - lobby_join: Koprulu joins the emulated host's game ("Alpha") without the map, downloads it from the host (the
 *   original's upload, Koprulu's download), and plays the lobby to the start the host's Start gives.
 * - lobby_host: Koprulu hosts a melee game of the map (offered as "!<name>") under its player's name ("Beta"), waits
 *   for its room to fill (KOPRULU_NET_LIVE_PLAYERS players, Koprulu included; default 2), sends the map to the
 *   emulated players without it (Koprulu's upload, the original's download) and starts.
 *
 * After the start (stage N4) Koprulu plays the game KOPRULU_NET_LIVE_FRAMES frames over the network with the emulated
 * players (KP_TURNS_NET, a frame every frame time of the header's speed, as the product's loop), recording it: no
 * desync on its side (no sender out of sync, nobody dropped), then its replay written beside KOPRULU_NET_LIVE_OUT
 * (REPLAY_FILE: the emulator plays it, tools/net_live.py) and the game left by the menu's quit (EndGame 0x443db0).
 * Without the variable (or 0) it leaves Storm's game at the start, and the emulated players play on alone.
 * Both tests need the emulator running and the 1.17 data (the templates, the map's header): CI skips them without
 * KOPRULU_NET_BRIDGE. KOPRULU_NET_LIVE_MAP names the map file both sides play (the host's; the joiner checks its
 * download against it, and plays it). */
#include "kp_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_data.h"
#include "kp_mission.h"
#include "kp_net_lobby.h"
#include "kp_net_sock.h"
#include "kp_net_world.h"
#include "kp_netgame.h"
#include "kp_rep.h"
#include "kp_session.h"
#include "kp_start.h"
#include "kp_transfer.h"

#define BRIDGE_ENV "KOPRULU_NET_BRIDGE"        /* BASE[:HOSTS]: the bridge's first port and its emulated hosts */
#define MAP_ENV "KOPRULU_NET_LIVE_MAP"         /* the map file both sides play */
#define OUT_ENV "KOPRULU_NET_LIVE_OUT"         /* Koprulu's start, as JSON */
#define PLAYERS_ENV "KOPRULU_NET_LIVE_PLAYERS" /* the hosted room's players before the Start, Koprulu included */
#define FRAMES_ENV "KOPRULU_NET_LIVE_FRAMES"   /* the game's frames Koprulu plays after the start (0: none) */
#define REPLAY_FILE "koprulu.rep"              /* Koprulu's replay of the game, beside OUT_ENV's file */
#define NAME "Beta"                            /* Koprulu's player, and its hosted game's name */
#define HOST_GAME "Alpha"                      /* the emulated host's game: its player's name */
#define DEFAULT_PLAYERS 2
#define MAX_PLAYERS 8
#define LIST_MS 600000      /* the emulated host's game listed within (the emulator boots in real time) */
#define LIST_POLL_MS 1000   /* StarCraft's join screen reads the game list once a second */
#define ROOM_MS 900000      /* the room filled, the map sent, the countdown run */
#define START_WAIT_MS 2000  /* the host's Start this long after its room filled, as the emulator's driver waits */
#define LEAVE_MS 1500       /* after the start: Storm's leave sent before the sockets close */
#define BEGIN_MS 30000      /* the game's first turns (the emulated players load the map in real time) */
#define LAG_MS 120000       /* the game's frames within their time and this much more (the emulator's waits) */
#define SPEED_FASTEST 6
#define MELEE "Melee"
#define EXPANSION_EXT ".scx"
/* The hosted map's name: the file's with '!' first, as the emulator's driver names its maps. The 1.17 install holds
 * the map under its own name, where a joiner's FindLocalCopy 0x441080 would find it and download nothing. */
#define OFFERED_PREFIX "!"

typedef struct {
	KpNetLobby l;      /* its seat 0 is Koprulu's, on the system's sockets */
	KpNetSock *sock;
	const uint8_t *map; /* the map file's bytes (MAP_ENV), NULL none */
	size_t map_size;
	char map_file[KP_TRANSFER_PATH];
	char map_name[KP_TRANSFER_NAME]; /* its base name */
	char offered[KP_TRANSFER_NAME];  /* the hosted map's name (OFFERED_PREFIX and the base name) */
	/* the game after the start */
	KpSession *session;
	KpDataMap game_map;
	int map_open;
	int begun;          /* RunGame 0x445310's first turn ran */
	uint32_t next;      /* the clock of the next frame */
	uint32_t frame_ms;  /* a frame at the header's speed */
	int64_t frames;     /* the frames run */
} Live;

static void live_free(void *p)
{
	Live *v = (Live *)p;
	if (v->session) {
		kp_session_free(v->session);
		free(v->session);
	}
	if (v->map_open) kp_data_map_close(&v->game_map);
	kp_net_lobby_free(&v->l);
	kp_net_sock_close(v->sock);
	free(v);
}

static uint32_t now(const Live *v) { return kp_net_lobby_now(&v->l); }
static int before(const Live *v, uint32_t until) { return (int32_t)(now(v) - until) < 0; }

/* Koprulu's seat on the bridge with the 1.17 templates, and the map file when MAP_ENV names one (need_map: it must) */
static Live *live_open(KpT *T, int need_map)
{
	const char *bridge = getenv(BRIDGE_ENV), *map = getenv(MAP_ENV), *base;
	const KpData *data;
	Live *v;
	KpNetIo io;
	char error[128] = "";
	if (!bridge || !*bridge) kp_t_skip(T, "no " BRIDGE_ENV " (the emulator's --bridge base port: tools/net_live.py)");
	kp_t_ok(T, !need_map || (map && *map), "no " MAP_ENV " (the map file the game plays)");
	data = kp_t_data_117(T);
	v = (Live *)calloc(1, sizeof *v);
	kp_t_ok(T, v != NULL, "out of memory");
	kp_t_defer(T, live_free, v);
	kp_net_lobby_init_sockets(&v->l, T, kp_t_fail_text, T);
	v->l.templates = data->templates;
	v->sock = kp_net_world_bridge_open(bridge, error, sizeof error);
	kp_t_ok(T, v->sock != NULL, "the seat's sockets: %s", error);
	io = kp_net_sock_io(v->sock);
	v->l.seat[0].n.sock = v->sock;
	kp_t_ok(T, kp_net_world_add(&v->l.n, &v->l.seat[0].n, &io), "the seat's SNet: %s", v->l.n.why);
	if (map && *map) {
		v->map = kp_t_read(T, map, &v->map_size);
		for (base = map + strlen(map); base > map && base[-1] != '/' && base[-1] != '\\'; base--) {
		}
		kp_t_ok(T, strlen(OFFERED_PREFIX) + strlen(base) < sizeof v->offered, "the map's name %s is too long", base);
		snprintf(v->map_file, sizeof v->map_file, "%s", map);
		snprintf(v->map_name, sizeof v->map_name, "%s", base);
		snprintf(v->offered, sizeof v->offered, OFFERED_PREFIX "%s", base);
	}
	return v;
}

/* The game list's row of a game by its name, NULL none */
static const KpNetGameHeader *listed(const KpNetGame *ng, const char *name)
{
	int k;
	for (k = 0; k < ng->listed_count; k++)
		if (strcmp(ng->listed[k].name, name) == 0) return &ng->listed[k];
	return NULL;
}

/* The lobby until the start: the ready room's Start pressed once; a host (players > 0) presses Start START_WAIT_MS
 * after its room held `players` players, once its checks pass (the map distributed) */
static void run_to_start(KpT *T, Live *v, int players)
{
	KpNetLobbySeat *s = &v->l.seat[0];
	uint32_t until = now(v) + ROOM_MS, full = 0;
	int pressed = 0, started = 0, was_full = 0;
	while (s->phase == KP_NET_LOBBY_ROOM || s->phase == KP_NET_LOBBY_READY_ROOM) {
		kp_t_ok(T, before(v, until), "no start in %d s: lobby state %d, %d participants", ROOM_MS / 1000,
			s->game.lobby_state, kp_netgame_participants(&s->game));
		kp_net_lobby_advance(&v->l, KP_NETGAME_POLL_MS);
		if (s->phase == KP_NET_LOBBY_READY_ROOM && !pressed) {
			s->press_start = 1;
			pressed = 1;
		}
		if (players <= 0 || started || s->phase != KP_NET_LOBBY_ROOM) continue;
		if (kp_netgame_participants(&s->game) < players) {
			was_full = 0;
			continue;
		}
		if (!was_full) {
			kp_t_log(T, "the room holds %d players", players);
			full = now(v);
			was_full = 1;
		}
		if ((int32_t)(now(v) - full) >= START_WAIT_MS && kp_netgame_start_check(&s->game) == 0) {
			kp_t_ok(T, kp_netgame_start(&s->game), "the Start refused");
			kp_t_log(T, "Start pressed");
			started = 1;
		}
	}
	kp_t_ok(T, s->phase == KP_NET_LOBBY_STARTED, "the room closed: leave reason %d, %d network errors", s->leave,
		s->errors);
}

/* A JSON string of a field's bytes (at most max): the bytes outside printable ASCII as \u00XX, as Latin-1 reads them */
static void put_string(FILE *f, const char *s, size_t max)
{
	size_t k;
	fputc('"', f);
	for (k = 0; k < max && s[k]; k++) {
		unsigned c = (unsigned char)s[k];
		if (c < 0x20 || c >= 0x7f || c == '"' || c == '\\') fprintf(f, "\\u%04x", c);
		else fputc((int)c, f);
	}
	fputc('"', f);
}

static void put_hex(FILE *f, const uint8_t *p, size_t n)
{
	size_t k;
	fputc('"', f);
	for (k = 0; k < n; k++) fprintf(f, "%02x", p[k]);
	fputc('"', f);
}

/* Koprulu's start at OUT_ENV (none without it) */
static void write_start(KpT *T, const Live *v, const char *role)
{
	const char *path = getenv(OUT_ENV);
	const KpNetLobbySeat *s = &v->l.seat[0];
	const KpNetGame *ng = &s->game;
	const char *map_path = kp_netgame_map_path(ng);
	uint8_t raw[KP_NETGAME_HEADER];
	FILE *f;
	int k;
	if (!path || !*path) return;
	f = fopen(path, "w");
	kp_t_ok(T, f != NULL, "cannot write %s", path);
	kp_netgame_header_write(&ng->header, raw);
	fprintf(f, "{\"role\": \"%s\", \"name\": \"%s\", \"seed\": %lu, \"local_storm\": %d, \"start_info\": ", role, NAME,
		(unsigned long)ng->seed, ng->local_storm);
	put_hex(f, ng->start_info, sizeof ng->start_info);
	fprintf(f, ", \"game_header\": ");
	put_hex(f, raw, sizeof raw);
	fprintf(f, ", \"map_path\": ");
	put_string(f, map_path ? map_path : "", KP_TRANSFER_PATH);
	fprintf(f, ", \"download_size\": %lu, \"download_crc\": %lu, \"players\": [", (unsigned long)s->download_size,
		(unsigned long)(s->download ? kp_transfer_crc(s->download, s->download_size) : 0));
	for (k = 0; k < KP_NETGAME_SLOTS; k++) {
		const KpNetSlot *p = &ng->slots[k];
		fprintf(f, "%s{\"player\": %ld, \"storm\": %ld, \"type\": %u, \"race\": %u, \"force\": %u, \"name\": ",
			k ? ", " : "", (long)p->slot, (long)p->storm, (unsigned)p->type, (unsigned)p->race, (unsigned)p->force);
		put_string(f, p->name, sizeof p->name);
		fputc('}', f);
	}
	fprintf(f, "]}\n");
	kp_t_ok(T, fclose(f) == 0, "cannot write %s", path);
	kp_t_log(T, "the start written to %s", path);
}

/* The seat's loop on the sockets' clock: the lobby's frames until the game's session exists; then, every frame time,
 * RunGame 0x445310's first turn polled until it ran, then GameLoop's messages and a frame (none while the turns are
 * overdue) */
static void live_frame(KpNetSeat *ns)
{
	Live *v = (Live *)(void *)ns->w;
	KpSession *s = v->session;
	KpT *T = v->l.n.T;
	if (!s) {
		kp_net_lobby_frame(&v->l.seat[0]);
		return;
	}
	if (!before(v, v->next)) {
		v->next = now(v) + v->frame_ms;
		if (!v->begun) {
			int r = kp_session_net_begin(s);
			kp_t_ok(T, r >= 0, "the game's first turn: %s", s->error);
			v->begun = r == 1;
			return;
		}
		kp_session_net_messages(s);
		if (kp_session_step(s, NULL)) v->frames++;
		kp_t_ok(T, !s->failed, "the game at frame %lld: %s", (long long)v->frames, s->error);
	}
}

/* Koprulu's replay of the game beside OUT_ENV's file (none without it) */
static void write_replay(KpT *T, Live *v)
{
	const char *out = getenv(OUT_ENV);
	char path[KP_TRANSFER_PATH + 32];
	KpRep rep;
	KpError e;
	uint8_t *file;
	size_t size, n;
	FILE *f;
	int rc;
	if (!out || !*out) return;
	for (n = strlen(out); n > 0 && out[n - 1] != '/' && out[n - 1] != '\\'; n--) {
	}
	kp_t_ok(T, n + strlen(REPLAY_FILE) < sizeof path, "the path %s is too long", out);
	snprintf(path, sizeof path, "%.*s" REPLAY_FILE, (int)n, out);
	memset(&rep, 0, sizeof rep);
	kp_t_ok(T, kp_session_replay_file(v->session, NULL, &rep, &e) == 0, "the replay: %s", e.msg);
	rc = kp_rep_write(&rep, &file, &size, &e);
	kp_rep_free(&rep);
	kp_t_ok(T, rc == 0, "the replay's file: %s", e.msg);
	f = fopen(path, "wb");
	rc = f && fwrite(file, 1, size, f) == size;
	if (f && fclose(f) != 0) rc = 0;
	free(file);
	kp_t_ok(T, rc, "cannot write %s", path);
	kp_t_log(T, "the replay written to %s", path);
}

/* After the start (FRAMES_ENV): the game's session (StartGame 0x443f40 after the lobby: the provider's caps again, the
 * map's mission for the template, the lobby's setup, seed, speed and Storm ID, its turns the network game's, recorded),
 * its frames played on the sockets' clock; no sender out of sync and nobody dropped; the replay written. 0 without
 * FRAMES_ENV. */
static int play_game(KpT *T, Live *v)
{
	const char *env = getenv(FRAMES_ENV);
	const KpData *data = kp_t_data_117(T);
	KpNetGame *ng = &v->l.seat[0].game;
	KpSessionStart st;
	KpMission m;
	KpError e;
	char name[KP_TRANSFER_NAME + 8];
	int64_t frames = env && *env ? atoll(env) : 0;
	uint32_t until;
	int id;
	if (frames <= 0) return 0;
	kp_t_ok(T, v->map != NULL, "the game needs " MAP_ENV);
	kp_t_eq(T, kp_netgame_game_caps(ng), 0, "the game's caps");
	kp_t_ok(T, kp_data_map_open(&v->game_map, &data->files, v->map_file, &e) == 0, "%s", e.msg);
	v->map_open = 1;
	kp_t_ok(T, kp_data_map_mission(&v->game_map, kp_netgame_ums(ng) ? KP_CHK_UMS : KP_CHK_MELEE, &m, &e) == 0, "%s",
		e.msg);
	v->session = (KpSession *)calloc(1, sizeof *v->session);
	kp_t_ok(T, v->session != NULL, "out of memory");
	snprintf(name, sizeof name, "maps/%s", v->map_name);
	kp_t_ok(T, kp_session_load_from(v->session, &data->shared, &data->dat, &m, name, &v->game_map.files) == 0,
		"the game does not load: %s", v->session->error);
	kp_session_start_defaults(&st);
	st.seed = ng->seed;
	st.speed = ng->header.speed + 1;
	st.has_setup = 1;
	kp_netgame_game_setup(ng, &st.setup);
	st.local_storm = ng->local_storm;
	st.net = ng;
	st.record = 1;
	kp_t_ok(T, kp_session_start(v->session, &st) == 0, "the game does not start: %s", v->session->error);
	kp_t_guard(T, v->session->g);
	kp_t_ok(T, ng->header.speed < KP_CLOCK_SPEEDS, "the speed %u", (unsigned)ng->header.speed);
	v->frame_ms = (uint32_t)KP_CLOCK_SPEED_MS[ng->header.speed];
	v->next = now(v);
	v->l.n.frame = live_frame;
	until = now(v) + BEGIN_MS + (uint32_t)frames * v->frame_ms + LAG_MS;
	while (v->frames < frames) {
		kp_t_ok(T, before(v, until), "frame %lld of %lld in time (first turn %s, waiting %d)", (long long)v->frames,
			(long long)frames, v->begun ? "ran" : "not run", ng->lagging);
		kp_net_lobby_advance(&v->l, v->frame_ms);
	}
	kp_t_log(T, "the game played to frame %lld (turn %lld)", (long long)kp_scalar(v->session->g, KP_FRAME),
		(long long)kp_scalar(v->session->g, KP_TURN));
	kp_t_ok(T, !ng->desync, "a sender out of sync");
	for (id = 0; id < KP_NETGAME_PLAYERS; id++) kp_t_ok(T, !ng->drop_flags[id], "Storm ID %d dropped", id);
	write_replay(T, v);
	return 1;
}

/* After the start: Storm's game left, its leave sent (the game's quit, EndGame 0x443db0, after a game played; else
 * AbortNetworkGame 0x474e60) */
static void leave(KpT *T, Live *v)
{
	if (play_game(T, v)) kp_netgame_game_end(&v->l.seat[0].game);
	else kp_netgame_abort(&v->l.seat[0].game);
	kp_net_lobby_advance(&v->l, LEAVE_MS);
}

static void lobby_join(KpT *T)
{
	Live *v = live_open(T, 0);
	KpNetLobbySeat *s = &v->l.seat[0];
	KpNetGameHeader game;
	const KpNetGameHeader *g = NULL;
	uint32_t until;
	kp_t_ok(T, kp_net_lobby_seat_init(s, NAME, &v->l.templates, NULL, 0, NULL), "the provider's caps refused");
	until = now(v) + LIST_MS;
	while (!(g = listed(&s->game, HOST_GAME)) && before(v, until)) {
		kp_net_lobby_advance(&v->l, LIST_POLL_MS);
		kp_netgame_enum(&s->game);
	}
	kp_t_ok(T, g != NULL, "\"%s\" not listed in %d s", HOST_GAME, LIST_MS / 1000);
	game = *g;
	kp_t_log(T, "listed: \"%s\", %d of %d players, template type %u", game.name, game.players, game.max_players,
		(unsigned)game.type);
	kp_net_lobby_advance(&v->l, KP_NET_LOBBY_LIST_MS); /* the host takes a connect again after max(4L, 1000) ms */
	kp_t_ok(T, kp_net_lobby_enter(s, &game), "the join refused: leave reason %d, %d network errors", s->leave,
		s->errors);
	kp_t_log(T, "joined as Storm ID %d, slot %d", s->game.local_storm, s->game.local_slot);
	run_to_start(T, v, 0);
	kp_t_log(T, "started: seed %#lx, map %s", (unsigned long)s->game.seed, kp_netgame_map_path(&s->game));
	write_start(T, v, "join");
	if (v->map && s->download)
		kp_t_ok(T, s->download_size == v->map_size && memcmp(s->download, v->map, v->map_size) == 0,
			"the downloaded map (%lu bytes) differs from %s (%lu bytes)", (unsigned long)s->download_size, v->map_file,
			(unsigned long)v->map_size);
	leave(T, v);
}

static void lobby_host(KpT *T)
{
	Live *v = live_open(T, 1);
	KpNetLobbySeat *s = &v->l.seat[0];
	const KpData *data = kp_t_data_117(T);
	const char *env = getenv(PLAYERS_ENV);
	const KpTemplate *melee = NULL;
	KpDataMap map;
	KpMapHeader mh;
	KpNetMap m;
	KpNetGameHeader h;
	KpError e;
	uint8_t *chk;
	size_t chk_size;
	char path[KP_TRANSFER_PATH], why[128];
	int k, players = env && *env ? atoi(env) : DEFAULT_PLAYERS, expansion;
	kp_t_ok(T, players >= 2 && players <= MAX_PLAYERS, PLAYERS_ENV " %d: 2 to %d", players, MAX_PLAYERS);
	expansion = strstr(v->map_name, EXPANSION_EXT) != NULL;
	kp_t_ok(T, kp_data_map_open(&map, &data->files, v->map_file, &e) == 0, "%s", e.msg);
	chk = kp_data_map_chk(&map, &chk_size, &e);
	kp_data_map_close(&map);
	kp_t_ok(T, chk != NULL, "%s", e.msg);
	k = kp_map_header(&mh, chk, chk_size, data->files.profile, expansion, why, sizeof why);
	free(chk);
	kp_t_ok(T, k == 0, "the map's header: %s", why);
	for (k = 0; k < v->l.templates.count && !melee; k++)
		if (strcmp(v->l.templates.list[k].name, MELEE) == 0) melee = &v->l.templates.list[k];
	kp_t_ok(T, melee != NULL, "no " MELEE " template");
	s->map_name = v->offered;
	s->map_size = v->map_size;
	kp_t_ok(T, kp_net_lobby_seat_init(s, NAME, &v->l.templates, v->map, 1, NULL), "the provider's caps refused");
	snprintf(path, sizeof path, "maps\\%s", v->offered);
	kp_netgame_map(&m, &mh, path, &s->game.hooks);
	kp_netgame_set_map(&s->game, &m);
	/* CreateGameFromEntry 0x45bfc0's header (kp_netui's u_create_game): the player's name as the game's, the map's
	 * size, tileset, slots and title, the speed, the template's key; a melee game reads no map class */
	memset(&h, 0, sizeof h);
	snprintf(h.name, sizeof h.name, "%s", NAME);
	h.width = m.width;
	h.height = m.height;
	h.tileset = m.tileset;
	h.max_players = mh.players;
	h.speed = SPEED_FASTEST;
	h.type = melee->record[KP_TEMPLATE_TYPE];
	h.sub = melee->record[KP_TEMPLATE_SUB];
	h.subtype = (uint16_t)(melee->record[KP_TEMPLATE_SUBTYPE] | melee->record[KP_TEMPLATE_SUBTYPE + 1] << 8);
	memcpy(h.title, m.title, sizeof h.title);
	kp_t_ok(T, kp_netgame_create(&s->game, &h, "") && kp_netgame_enter(&s->game), "the create refused: %d errors",
		s->errors);
	s->game.in_chat_room = 1;
	s->phase = KP_NET_LOBBY_ROOM;
	kp_t_log(T, "hosting \"%s\" (%s, %d slots), waiting for %d players", h.name, v->offered, mh.players, players);
	run_to_start(T, v, players);
	kp_t_log(T, "started: seed %#lx", (unsigned long)s->game.seed);
	write_start(T, v, "host");
	leave(T, v);
}

static const KpTest TESTS[] = {
	{ "lobby_join", lobby_join, 0 },
	{ "lobby_host", lobby_host, 0 },
};

KP_SUITE(kp_suite_net_live, "test_net_live", TESTS);
