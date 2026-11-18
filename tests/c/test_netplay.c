/* test_netplay: Koprulu's game over the network (phase N, stage N4) between Koprulu seats. Whole stacks (KpNetGame over
 * kp_snet over the UDP provider, kp_net_lobby.h) on the in-process LAN host a melee game of the 1.17 install's map,
 * reach its start, and each seat then plays it in a session of its own over KP_TURNS_NET, a frame every FRAME_MS of
 * the LAN's clock as the original's loop at the fastest speed:
 *
 * - every seat's shared state hash (kp_shared_hash) is the same at every frame, with commands of each seat, also on a
 *   lossy LAN (the turns wait, resend and arrive);
 * - a seat that quits (EndGame 0x443db0) is seen leaving at one turn by every other seat, which plays on;
 * - a seat whose state differs (its first unit's hit points written, as net.py's poke) sends another sync value 0x37:
 *   ResolveDesync 0x47a630 drops it, and in a game of two each side drops the other, as 1.17 does;
 * - a seat whose cable is cut stops sending turns: the other waits (timeout.bin), its countdown runs out and Drop
 *   Players drops the silent seat; the game goes on.
 *
 * The original's side of the same turns (the 0x37 values, the waits, the drops) is test_lobby_trace's. Needs the 1.17
 * install (KP_TEST_ORIGINAL: the map, the templates); skipped without it. */
#include "kp_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_data.h"
#include "kp_game.h"
#include "kp_host.h"
#include "kp_ids.h"
#include "kp_kpf.h"
#include "kp_mission.h"
#include "kp_net_lobby.h"
#include "kp_netgame.h"
#include "kp_rep.h"
#include "kp_session.h"
#include "kp_start.h"

#define INSTALL_ENV "KP_TEST_ORIGINAL"
#define MAP_FILE "(2)Challenger.scm"
#define MAP_FILE_4 "(4)Blood Bath.scm"
#define MELEE "Melee"
#define SPEED_FASTEST 6
#define FRAME_MS 42          /* a frame at the fastest speed */
#define SETTLE_MS 3000       /* a lobby message's round: a few 250 ms passes and Storm's latency */
#define ROOM_MS 30000        /* the room to its start: the countdown (24 turns at 250 ms), the start information */
#define BEGIN_MS 10000       /* the first turns of every seat */
#define MAX_FRAMES 1200
#define SEATS 4
#define MAX_SECTIONS 64     /* kp_shared_hash's sections */
#define COMMAND_FRAME 40     /* the seats' commands queued at this frame */
#define MOVING_FRAME 60      /* their worker on its way by then (the turns' latency passed) */
#define LAST_COMMAND_FRAME 350 /* a last command of each seat (its selection emptied): the replay's last block, which
                               * a playback never feeds (FeedReplayCommands 0x4769b0) */
#define LOCKSTEP_FRAMES 400
#define CLICK_OFFSET 96      /* the right-click's point: this far right of the unit */
#define SELECT_COMMAND 0x09
#define RIGHT_CLICK_COMMAND 0x14
#define TIMEOUT_MS 60000     /* timeout.bin's countdown (45 s) and its ticks */
#define CHAT_LINE "gl hf"
#define ALL_STORM_IDS 0xffu  /* a chat line's recipients: every Storm ID (WORD 0x5264ba) */
#define REPLAYS_ENV "KP_NETPLAY_REPLAYS" /* a folder: each seat's replay written there (the emulator plays them) */

typedef struct {
	KpSession *session;
	KpDataMap map;
	int map_open;
	int begun;     /* kp_session_net_begin ran the first turn */
	int stopped;   /* the seat plays no more frames (it quit, or the test stopped it) */
	uint32_t next; /* the LAN time of its next frame */
	int64_t frames; /* its frames run */
	int poke_at;   /* the frame whose end writes the first unit's hit points, -1 none */
	int commands;  /* the seat queues its commands at COMMAND_FRAME */
	int64_t worker_order; /* its worker's order at MOVING_FRAME */
	uint64_t hash[MAX_FRAMES + 1]; /* the shared hash after each frame */
	uint64_t section[MAX_FRAMES + 1][MAX_SECTIONS]; /* and its sections' (their tags the play's) */
	int64_t turn[MAX_FRAMES + 1];  /* the turn counter after each frame */
} Seat;

/* A shared hash's sections: their tags and hashes */
typedef struct {
	int count;
	char tag[MAX_SECTIONS][5];
	uint64_t *hash;
} Sections;

static void take_section(void *ud, const char *tag, uint64_t hash, size_t bytes)
{
	Sections *s = (Sections *)ud;
	(void)bytes;
	if (s->count >= MAX_SECTIONS) return;
	memcpy(s->tag[s->count], tag, 4);
	s->tag[s->count][4] = 0;
	s->hash[s->count++] = hash;
}

/* The shared hash of g into *h, its sections' into section (MAX_SECTIONS) and their tags into tags (NULL: not kept) */
static int hash_of(KpGame *g, uint64_t *h, uint64_t *section, char (*tags)[5])
{
	Sections s;
	s.count = 0;
	s.hash = section;
	if (kp_shared_hash(g, take_section, &s, h) != 0) return -1;
	if (tags) memcpy(tags, s.tag, sizeof s.tag);
	return 0;
}

typedef struct {
	KpNetLobby l; /* first: the world's callbacks cast its seats back to the play */
	KpT *T;
	const KpData *data;
	uint8_t *map;
	size_t map_size;
	char map_name[64];
	int seats;
	Seat seat[SEATS];
	char tags[MAX_SECTIONS][5]; /* the shared hash's sections */
} Play;

static void play_free(void *ud)
{
	Play *p = (Play *)ud;
	int k;
	for (k = 0; k < SEATS; k++) {
		Seat *q = &p->seat[k];
		if (q->session) {
			kp_session_free(q->session);
			free(q->session);
		}
		if (q->map_open) kp_data_map_close(&q->map);
	}
	kp_net_lobby_free(&p->l);
	free(p->map);
	free(p);
}

static Play *play_of(KpNetSeat *s) { return (Play *)(void *)s->w; }

/* The seat's first unit (its player's first in the player's list) */
static int64_t first_unit(KpGame *g) { return g->player_units[g->local_player]; }

/* The seat's first worker in its player's list, 0 none */
static int64_t first_worker(KpGame *g)
{
	int64_t n;
	for (n = g->player_units[g->local_player]; n != 0; n = g->u.player_next[n])
		if (g->u.type[n] == KP_TYPE_SCV || g->u.type[n] == KP_TYPE_DRONE || g->u.type[n] == KP_TYPE_PROBE) return n;
	return 0;
}

/* The seat's commands at COMMAND_FRAME: its first worker selected, then a right-click on the ground to its right */
static void queue_commands(Play *p, int k)
{
	KpGame *g = p->seat[k].session->g;
	KpNetGame *ng = &p->l.seat[k].game;
	int64_t n = first_worker(g), ref;
	int32_t sprite;
	uint8_t select[4], click[10];
	kp_t_ok(p->T, n != 0, "seat %d has a worker", k);
	ref = kp_unit_ref(g, n);
	select[0] = SELECT_COMMAND;
	select[1] = 1;
	kp_le_put16(select + 2, (uint32_t)ref);
	sprite = kp_unit_sprite(g, n);
	click[0] = RIGHT_CLICK_COMMAND;
	kp_le_put16(click + 1, (uint32_t)(g->w->spr.x[sprite] + CLICK_OFFSET));
	kp_le_put16(click + 3, (uint32_t)g->w->spr.y[sprite]);
	kp_le_put16(click + 5, 0);
	kp_le_put16(click + 7, KP_TYPE_NONE);
	click[9] = 0;
	kp_t_ok(p->T, kp_netgame_command(ng, select, sizeof select) == 0, "seat %d's selection queued", k);
	kp_t_ok(p->T, kp_netgame_command(ng, click, sizeof click) == 0, "seat %d's right-click queued", k);
}

/* A seat's frame of the LAN's loop: the lobby's until its game starts; then, every FRAME_MS, RunGame 0x445310's first
 * turn polled until it ran, then GameLoop's messages and a frame (none while the turns are overdue) */
static void seat_frame(KpNetSeat *ns)
{
	Play *p = play_of(ns);
	int k = ns->index;
	Seat *q = &p->seat[k];
	KpSession *s = q->session;
	uint32_t now = kp_net_lobby_now(&p->l);
	if (!s) {
		kp_net_lobby_frame(&p->l.seat[k]);
		return;
	}
	if (q->stopped || (int32_t)(now - q->next) < 0) return;
	q->next = now + FRAME_MS;
	if (!q->begun) {
		int r = kp_session_net_begin(s);
		kp_t_ok(p->T, r >= 0, "seat %d's first turn: %s", k, s->error);
		q->begun = r == 1;
		return;
	}
	kp_session_net_messages(s);
	kp_t_ok(p->T, !s->failed, "seat %d's messages: %s", k, s->error);
	if (!kp_session_step(s, NULL)) {
		kp_t_ok(p->T, !s->failed, "seat %d's frame %lld: %s", k, (long long)q->frames, s->error);
		return;
	}
	q->frames++;
	if (q->frames == q->poke_at && first_unit(s->g)) kp_set_unit_hp(s->g, first_unit(s->g), 1);
	if (q->commands && q->frames == COMMAND_FRAME) {
		queue_commands(p, k);
		if (k == 0) kp_netgame_game_chat(&p->l.seat[k].game, CHAT_LINE, ALL_STORM_IDS);
	}
	if (q->commands && q->frames == LAST_COMMAND_FRAME) {
		static const uint8_t SELECT_NONE[2] = { SELECT_COMMAND, 0 };
		kp_t_ok(p->T, kp_netgame_command(&p->l.seat[k].game, SELECT_NONE, sizeof SELECT_NONE) == 0,
			"seat %d's last command queued", k);
	}
	if (q->frames == MOVING_FRAME && first_worker(s->g)) q->worker_order = s->g->u.order[first_worker(s->g)];
	if (q->frames <= MAX_FRAMES) {
		kp_t_ok(p->T, hash_of(s->g, &q->hash[q->frames], q->section[q->frames], p->tags) == 0, "seat %d's hash", k);
		q->turn[q->frames] = kp_scalar(s->g, KP_TURN);
	}
}

/* The melee template of the install */
static const KpTemplate *melee(KpT *T, const KpTemplates *t)
{
	int k;
	for (k = 0; k < t->count; k++)
		if (strcmp(t->list[k].name, MELEE) == 0) return &t->list[k];
	kp_t_fail(T, "no " MELEE " template");
}

/* The lobby: seat 0 hosts a melee game of the map at the fastest speed, the others join with the map; the host starts
 * once all are in */
static Play *play_lobby(KpT *T, const char *map_file, int seats, uint64_t seed, int lossy)
{
	const char *root = getenv(INSTALL_ENV);
	const KpTemplate *t;
	Play *p;
	KpDataMap dm;
	KpMapHeader mh;
	KpNetMap m;
	KpNetGameHeader h;
	KpError e;
	KpNetLobbySeat *host;
	uint8_t *chk;
	size_t chk_size;
	char path[1024], why[128];
	int k;
	if (!root || !root[0]) kp_t_skip(T, "no " INSTALL_ENV " (the install's maps)");
	p = (Play *)calloc(1, sizeof *p);
	kp_t_ok(T, p != NULL, "out of memory");
	kp_t_defer(T, play_free, p);
	p->T = T;
	p->seats = seats;
	p->data = kp_t_data_117(T);
	snprintf(path, sizeof path, "%s/maps/%s", root, map_file);
	snprintf(p->map_name, sizeof p->map_name, "%s", map_file);
	p->map = kp_read_file(path, &p->map_size);
	kp_t_ok(T, p->map != NULL, "cannot read %s", path);
	kp_t_ok(T, kp_data_map_open(&dm, &p->data->files, path, &e) == 0, "%s", e.msg);
	chk = kp_data_map_chk(&dm, &chk_size, &e);
	kp_data_map_close(&dm);
	kp_t_ok(T, chk != NULL, "%s", e.msg);
	k = kp_map_header(&mh, chk, chk_size, p->data->files.profile, 0, why, sizeof why);
	free(chk);
	kp_t_ok(T, k == 0, "the map's header: %s", why);
	kp_t_ok(T, kp_net_lobby_init(&p->l, T, kp_t_fail_text, T, seed, seats, lossy), "the LAN");
	p->l.templates = p->data->templates;
	p->l.n.frame = seat_frame;
	t = melee(T, &p->l.templates);
	for (k = 0; k < seats; k++) {
		p->l.seat[k].map_name = p->map_name;
		p->l.seat[k].map_size = p->map_size;
		p->seat[k].poke_at = -1;
	}
	/* the host: CreateGameFromEntry 0x45bfc0's header (kp_netui's u_create_game) */
	host = &p->l.seat[0];
	kp_t_ok(T, kp_net_lobby_seat_init(host, KP_NET_LOBBY_NAMES[0], &p->l.templates, p->map, 1, NULL), "the caps");
	snprintf(path, sizeof path, "maps\\%s", map_file);
	kp_netgame_map(&m, &mh, path, &host->game.hooks);
	kp_netgame_set_map(&host->game, &m);
	memset(&h, 0, sizeof h);
	snprintf(h.name, sizeof h.name, KP_NET_LOBBY_GAME);
	h.width = m.width;
	h.height = m.height;
	h.tileset = m.tileset;
	h.max_players = mh.players;
	h.speed = SPEED_FASTEST;
	h.type = t->record[KP_TEMPLATE_TYPE];
	h.sub = t->record[KP_TEMPLATE_SUB];
	h.subtype = (uint16_t)(t->record[KP_TEMPLATE_SUBTYPE] | t->record[KP_TEMPLATE_SUBTYPE + 1] << 8);
	memcpy(h.title, m.title, sizeof h.title);
	kp_t_ok(T, kp_netgame_create(&host->game, &h, "") && kp_netgame_enter(&host->game), "the host's game");
	host->game.in_chat_room = 1;
	host->phase = KP_NET_LOBBY_ROOM;
	for (k = 1; k < seats; k++) {
		KpNetLobbySeat *s = &p->l.seat[k];
		const KpNetGameHeader *g;
		kp_t_ok(T, kp_net_lobby_seat_init(s, KP_NET_LOBBY_NAMES[k], &p->l.templates, p->map, 1, NULL), "the caps");
		kp_net_lobby_advance(&p->l, KP_NET_LOBBY_LIST_MS);
		kp_netgame_enum(&s->game);
		g = kp_net_lobby_listed(&s->game);
		kp_t_ok(T, g && kp_net_lobby_enter(s, g), "seat %d joins", k);
	}
	kp_net_lobby_advance(&p->l, SETTLE_MS);
	kp_t_eq(T, kp_netgame_participants(&host->game), seats, "the room's players");
	kp_t_eq(T, kp_netgame_start_check(&host->game), 0, "the host may start");
	kp_t_ok(T, kp_netgame_start(&host->game), "the host starts");
	kp_net_lobby_run_to_start(&p->l, ROOM_MS);
	for (k = 0; k < seats; k++)
		kp_t_eq(T, p->l.seat[k].phase, KP_NET_LOBBY_STARTED, "seat %d started (leave reason %d)", k,
			p->l.seat[k].leave);
	return p;
}

/* Each seat's session (StartGame 0x443f40 after the lobby, as the product's start_network_game): the provider's caps
 * asked again, the map's melee mission, the lobby's setup, seed, speed and Storm ID, its turns the network game's */
static void play_start(Play *p)
{
	KpT *T = p->T;
	const KpData *d = p->data;
	const char *root = getenv(INSTALL_ENV);
	char path[1024], name[96];
	int k;
	snprintf(path, sizeof path, "%s/maps/%s", root, p->map_name);
	snprintf(name, sizeof name, "maps/%s", p->map_name);
	for (k = 0; k < p->seats; k++) {
		KpNetGame *ng = &p->l.seat[k].game;
		Seat *q = &p->seat[k];
		KpSessionStart st;
		KpMission m;
		KpError e;
		kp_t_eq(T, kp_netgame_game_caps(ng), 0, "seat %d's caps", k);
		kp_t_ok(T, kp_data_map_open(&q->map, &d->files, path, &e) == 0, "%s", e.msg);
		q->map_open = 1;
		kp_t_ok(T, kp_data_map_mission(&q->map, KP_CHK_MELEE, &m, &e) == 0, "%s", e.msg);
		q->session = (KpSession *)calloc(1, sizeof *q->session);
		kp_t_ok(T, q->session != NULL, "out of memory");
		kp_t_ok(T, kp_session_load_from(q->session, &d->shared, &d->dat, &m, name, &q->map.files) == 0,
			"seat %d's game does not load: %s", k, q->session->error);
		kp_session_start_defaults(&st);
		st.seed = ng->seed;
		st.speed = ng->header.speed + 1;
		st.has_setup = 1;
		kp_netgame_game_setup(ng, &st.setup);
		st.local_storm = ng->local_storm;
		st.net = ng;
		st.record = 1;
		kp_t_ok(T, kp_session_start(q->session, &st) == 0, "seat %d's game does not start: %s", k,
			q->session->error);
		kp_t_guard(T, q->session->g);
		q->next = kp_net_lobby_now(&p->l);
	}
}

/* The LAN's clock run for ms, the seats playing */
static void play_run(Play *p, uint32_t ms) { kp_net_lobby_advance(&p->l, ms); }

/* Until every playing seat's first turn ran */
static void play_begin(Play *p)
{
	uint32_t until = kp_net_lobby_now(&p->l) + BEGIN_MS;
	for (;;) {
		int k, waiting = 0;
		for (k = 0; k < p->seats; k++) waiting += !p->seat[k].begun;
		if (!waiting) return;
		kp_t_ok(p->T, (int32_t)(kp_net_lobby_now(&p->l) - until) < 0, "the first turns within %d ms", BEGIN_MS);
		play_run(p, FRAME_MS);
	}
}

/* Until seat k ran `frames` frames (within ms of the LAN's clock) */
static void play_until(Play *p, int k, int64_t frames, uint32_t ms)
{
	uint32_t until = kp_net_lobby_now(&p->l) + ms;
	while (p->seat[k].frames < frames) {
		kp_t_ok(p->T, (int32_t)(kp_net_lobby_now(&p->l) - until) < 0,
			"seat %d at frame %lld of %lld within %u ms (lagging %d)", k, (long long)p->seat[k].frames,
			(long long)frames, (unsigned)ms, p->l.seat[k].game.lagging);
		play_run(p, FRAME_MS);
	}
}

/* The tags of the sections whose hashes differ (a string of the test's) */
static const char *differing(Play *p, const uint64_t *a, const uint64_t *b)
{
	char out[MAX_SECTIONS * 5 + 1] = "";
	int k;
	for (k = 0; k < MAX_SECTIONS && p->tags[k][0]; k++)
		if (a[k] != b[k]) {
			strcat(out, " ");
			strcat(out, p->tags[k]);
		}
	return kp_t_strdup(p->T, out);
}

/* Seats a and b's shared hashes equal at every frame both ran from `from` (to `to`, -1 the last) */
static void same_hashes(Play *p, int a, int b, int64_t from, int64_t to)
{
	int64_t f, last = p->seat[a].frames < p->seat[b].frames ? p->seat[a].frames : p->seat[b].frames;
	if (last > MAX_FRAMES) last = MAX_FRAMES;
	if (to >= 0 && to < last) last = to;
	kp_t_ok(p->T, last >= from, "seats %d and %d ran frame %lld", a, b, (long long)from);
	for (f = from; f <= last; f++)
		if (p->seat[a].hash[f] != p->seat[b].hash[f])
			kp_t_fail(p->T, "seats %d and %d differ at frame %lld:%s", a, b, (long long)f,
				differing(p, p->seat[a].section[f], p->seat[b].section[f]));
}

static int dropped(const KpNetGame *ng, int storm) { return ng->drop_flags[storm] != 0; }

/* Seat k's game sees the player of Storm ID `storm` as gone (0x673818: KP_NET_LEFT) */
static int gone(Play *p, int k, int storm)
{
	return p->seat[k].session->g->net_state[storm] == KP_NET_LEFT;
}

static void session_free(void *ud)
{
	kp_session_free((KpSession *)ud);
	free(ud);
}

static void rep_free(void *ud)
{
	kp_rep_free((KpRep *)ud);
	free(ud);
}

/* A map file opened for a playback (closed by the cleanup once the session that reads it is freed) */
typedef struct {
	KpDataMap map;
	int open;
} PlaybackMap;

static void playback_map_free(void *ud)
{
	PlaybackMap *m = (PlaybackMap *)ud;
	if (m->open) kp_data_map_close(&m->map);
	free(m);
}

/* Seat k's replay (kp_session_replay_file, through its file; written to REPLAYS_ENV as <name>-<seat name>.rep when
 * set) played back by Koprulu as the recorded game (replay_as_game): its shared hash equal to the live game's at every
 * frame up to `last` (the record's last block is never fed), but for what a playback has otherwise by design, which is hashed with the live game's values: a
 * playback runs without the multiplayer byte 0x672664 and records no slot summary (RecordSlotSummary 0x47e290 is the
 * lobby's start's), and its turn counter counts its frames where a network game's counts its turns */
static void replay_is_the_game(Play *p, int k, const char *name, int64_t last)
{
	KpT *T = p->T;
	Seat *q = &p->seat[k];
	const char *dir = getenv(REPLAYS_ENV), *root = getenv(INSTALL_ENV);
	KpRep rep, *back = (KpRep *)calloc(1, sizeof *back);
	KpSession *s = (KpSession *)calloc(1, sizeof *s);
	PlaybackMap *map = (PlaybackMap *)calloc(1, sizeof *map);
	const KpGameSetup *live = &q->session->g->setup;
	uint64_t section[MAX_SECTIONS];
	KpSessionStart st;
	KpMission m;
	KpError e;
	uint8_t *file;
	size_t size;
	char path[1024], key[96];
	int64_t f;
	int mark = kp_t_mark(T);
	kp_t_ok(T, back && s && map, "out of memory");
	kp_t_defer(T, rep_free, back);
	kp_t_defer(T, playback_map_free, map);
	kp_t_defer(T, session_free, s); /* freed before the map it reads */
	memset(&rep, 0, sizeof rep);
	kp_t_ok(T, kp_session_replay_file(q->session, NULL, &rep, &e) == 0, "seat %d's replay: %s", k, e.msg);
	f = kp_rep_write(&rep, &file, &size, &e);
	kp_rep_free(&rep);
	kp_t_ok(T, f == 0, "seat %d's replay file: %s", k, e.msg);
	if (dir && dir[0]) {
		FILE *out;
		snprintf(path, sizeof path, "%s/%s-%s.rep", dir, name, KP_NET_LOBBY_NAMES[k]);
		out = fopen(path, "wb");
		kp_t_ok(T, out && fwrite(file, 1, size, out) == size && fclose(out) == 0, "cannot write %s", path);
		kp_t_log(T, "the replay written to %s (%lld frames)", path, (long long)q->frames);
	}
	f = kp_rep_read(back, file, size, NULL, &e);
	free(file);
	kp_t_ok(T, f == 0, "seat %d's replay reads back: %s", k, e.msg);
	snprintf(path, sizeof path, "%s/maps/%s", root, p->map_name);
	snprintf(key, sizeof key, "maps/%s", p->map_name);
	kp_t_ok(T, kp_data_map_open(&map->map, &p->data->files, path, &e) == 0, "%s", e.msg);
	map->open = 1;
	kp_t_ok(T, kp_data_map_mission(&map->map, KP_CHK_MELEE, &m, &e) == 0, "%s", e.msg);
	kp_t_ok(T, kp_session_load_from(s, &p->data->shared, &p->data->dat, &m, key, &map->map.files) == 0, "%s", s->error);
	kp_session_start_defaults(&st);
	kp_t_eq(T, kp_session_replay_start(&st, back), 0, "seat %d's replay plays", k);
	st.replay = back;
	st.replay_as_game = 1;
	kp_t_ok(T, kp_session_start(s, &st) == 0, "the playback does not start: %s", s->error);
	kp_t_guard(T, s->g);
	kp_t_ok(T, last <= q->frames && last <= MAX_FRAMES, "seat %d ran frame %lld", k, (long long)last);
	for (f = 1; f <= last; f++) {
		KpGame *g = s->g;
		KpGameSetup kept;
		int64_t turn;
		uint64_t h;
		int failed;
		kp_t_ok(T, kp_session_step(s, NULL), "the playback's frame %lld: %s", (long long)f, s->error);
		/* the playback's differences by design, hashed with the live game's values */
		kept = g->setup;
		turn = kp_scalar(g, KP_TURN);
		g->setup.multiplayer = live->multiplayer;
		memcpy(g->setup.race_summary, live->race_summary, sizeof live->race_summary);
		kp_scalar_set(g, KP_TURN, q->turn[f]);
		failed = hash_of(g, &h, section, NULL);
		g->setup = kept;
		kp_scalar_set(g, KP_TURN, turn);
		kp_t_ok(T, failed == 0, "the playback's hash");
		if (h != q->hash[f])
			kp_t_fail(T, "seat %d's replay differs from its game at frame %lld:%s", k, (long long)f,
				differing(p, q->section[f], section));
	}
	kp_t_release(T, mark);
}

static void play_in_lockstep(KpT *T, uint64_t seed, int lossy)
{
	Play *p = play_lobby(T, MAP_FILE, 2, seed, lossy);
	int k;
	play_start(p);
	for (k = 0; k < p->seats; k++) p->seat[k].commands = 1;
	play_begin(p);
	play_until(p, 0, LOCKSTEP_FRAMES, LOCKSTEP_FRAMES * FRAME_MS * 4);
	play_until(p, 1, LOCKSTEP_FRAMES, LOCKSTEP_FRAMES * FRAME_MS * 4);
	same_hashes(p, 0, 1, 1, -1);
	for (k = 0; k < p->seats; k++) {
		KpNetGame *ng = &p->l.seat[k].game;
		KpGame *g = p->seat[k].session->g;
		int other = 1 - k, storm = p->l.seat[other].game.local_storm;
		kp_t_ok(T, !dropped(ng, storm) && !ng->desync, "seat %d saw no desync", k);
		kp_t_eq(T, g->net_state[storm], KP_NET_ACTIVE, "seat %d's other player is active", k);
		kp_t_eq(T, p->seat[k].worker_order, KP_ORD_MOVE, "seat %d's worker moves (its right-click ran)", k);
		replay_is_the_game(p, k, lossy ? "lockstep-lossy" : "lockstep", LAST_COMMAND_FRAME);
	}
}

static void two_seats_play_in_lockstep(KpT *T) { play_in_lockstep(T, 21, 0); }
static void two_seats_play_in_lockstep_on_a_lossy_lan(KpT *T) { play_in_lockstep(T, 23, 1); }

/* Three seats of a four-player map; the third quits at frame 150: the others see it leave at one turn and play on */
static void a_leaver_is_seen_at_one_turn(KpT *T)
{
	Play *p = play_lobby(T, MAP_FILE_4, 3, 29, 0);
	int storm, k;
	play_start(p);
	play_begin(p);
	play_until(p, 2, 150, 150 * FRAME_MS * 4);
	storm = p->l.seat[2].game.local_storm;
	kp_netgame_game_end(&p->l.seat[2].game); /* the game menu's quit: EndGame 0x443db0 */
	p->seat[2].stopped = 1;
	play_until(p, 0, 400, 400 * FRAME_MS * 4);
	play_until(p, 1, 400, 400 * FRAME_MS * 4);
	same_hashes(p, 0, 1, 1, -1);
	for (k = 0; k < 2; k++) {
		kp_t_ok(T, gone(p, k, storm), "seat %d saw the third player leave", k);
		kp_t_ok(T, !dropped(&p->l.seat[k].game, storm), "seat %d did not drop it", k);
	}
}

/* Two seats; the second's first unit's hit points written at frame 100's end: its next 0x37 differs, and each side
 * drops the other (ResolveDesync 0x47a630 in a game of two); both play on alone */
static void a_desync_drops_each_side_in_a_game_of_two(KpT *T)
{
	Play *p = play_lobby(T, MAP_FILE, 2, 31, 0);
	int k;
	play_start(p);
	p->seat[1].poke_at = 100;
	play_begin(p);
	play_until(p, 0, 300, 300 * FRAME_MS * 8);
	play_until(p, 1, 300, 300 * FRAME_MS * 8);
	same_hashes(p, 0, 1, 1, 99); /* the poke changes seat 1 at frame 100's end */
	for (k = 0; k < 2; k++) {
		int other = p->l.seat[1 - k].game.local_storm;
		kp_t_ok(T, dropped(&p->l.seat[k].game, other), "seat %d dropped the other as out of sync", k);
		kp_t_ok(T, gone(p, k, other), "seat %d's game sees the other gone", k);
	}
}

/* Two seats; the second's cable cut at frame 100: the first waits (timeout.bin), its countdown ends, Drop Players
 * drops the silent seat, and the first plays on */
static void a_silent_seat_is_dropped_after_the_countdown(KpT *T)
{
	Play *p = play_lobby(T, MAP_FILE, 2, 37, 0);
	KpNetGame *ng;
	uint32_t until;
	int storm;
	int64_t at;
	play_start(p);
	play_begin(p);
	play_until(p, 1, 100, 100 * FRAME_MS * 4);
	kp_net_lan_cut(p->l.n.lan, p->l.seat[1].n.host, 1);
	p->seat[1].stopped = 1;
	ng = &p->l.seat[0].game;
	storm = p->l.seat[1].game.local_storm;
	until = kp_net_lobby_now(&p->l) + TIMEOUT_MS;
	while (!kp_netgame_timeout_drop(ng)) {
		kp_t_ok(T, (int32_t)(kp_net_lobby_now(&p->l) - until) < 0, "Drop Players within %d ms (lagging %d, %u s)",
			TIMEOUT_MS, ng->lagging, (unsigned)ng->timeout_seconds);
		play_run(p, FRAME_MS);
	}
	at = p->seat[0].frames;
	play_until(p, 0, at + 100, 100 * FRAME_MS * 4);
	kp_t_ok(T, gone(p, 0, storm), "the silent player is gone");
	kp_t_ok(T, !ng->lagging, "no more waiting");
}

static const KpTest TESTS[] = {
	{ "two_seats_play_in_lockstep", two_seats_play_in_lockstep, 0 },
	{ "two_seats_play_in_lockstep_on_a_lossy_lan", two_seats_play_in_lockstep_on_a_lossy_lan, 0 },
	{ "a_leaver_is_seen_at_one_turn", a_leaver_is_seen_at_one_turn, 0 },
	{ "a_desync_drops_each_side_in_a_game_of_two", a_desync_drops_each_side_in_a_game_of_two, 0 },
	{ "a_silent_seat_is_dropped_after_the_countdown", a_silent_seat_is_dropped_after_the_countdown, 0 },
};

KP_SUITE(kp_suite_netplay, "test_netplay", TESTS);
