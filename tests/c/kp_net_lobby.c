/* kp_net_lobby: the network tests' lobby seats (kp_net_lobby.h). */
#include "kp_net_lobby.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_ids.h"
#include "kp_net.h"
#include "kp_netsave.h"
#include "kp_rep.h"
#include "kp_start.h"
#include "kp_transfer.h"

#define MAP_SIDE 64
#define SPEED_FASTEST 6
#define LOSSY_DELAY 5   /* ms */
#define LOSSY_JITTER 30 /* ms */
#define LOSSY_LOSS 100  /* per mille */

const char *const KP_NET_LOBBY_NAMES[KP_NET_WORLD_SEATS] = { "Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta",
	"Eta", "Theta" };

/* --- the screens' side --- */

static uint32_t h_now(void *ud)
{
	const KpNetLobbySeat *s = (const KpNetLobbySeat *)ud;
	return s->clock ? *s->clock : kp_net_world_now(s->n.w);
}

static uint32_t h_time(void *ud)
{
	(void)ud;
	return KP_NET_LOBBY_TIME;
}

/* The test's own lines for the table's formats */
static const char *h_text(void *ud, int index)
{
	(void)ud;
	switch (index) {
	case KP_NETTBL_JOINED: return "%s joined";
	case KP_NETTBL_LEFT: return "%s left";
	case KP_NETTBL_DROPPED: return "%s dropped";
	case KP_NETTBL_IGNORING: return "Ignoring %s";
	case KP_NETTBL_ACCEPTING: return "Accepting %s";
	case KP_NETTBL_TO: return "To %s:";
	case KP_NETTBL_FROM: return "From %s:";
	default: return NULL;
	}
}

static void h_line(void *ud, int kind, const char *text)
{
	KpNetLobbySeat *s = (KpNetLobbySeat *)ud;
	(void)kind;
	if (s->line_count < KP_NET_LOBBY_LINES) snprintf(s->lines[s->line_count++], KP_NETGAME_LINE, "%s", text);
}

static int h_find_save(void *ud, uint32_t id, KpNetGameSave *out)
{
	const KpNetLobbySeat *s = (const KpNetLobbySeat *)ud;
	if (!s->save || s->save->header.save_id != id) return 0;
	*out = *s->save;
	return 1;
}

static void h_leave(void *ud, int reason) { ((KpNetLobbySeat *)ud)->leave = reason; }

static void h_error(void *ud, int index, const char *detail, int fatal, int in_game)
{
	(void)in_game;
	(void)index;
	(void)detail;
	(void)fatal;
	((KpNetLobbySeat *)ud)->errors++;
}

/* The seat's map: its name and size (the test map's unless the caller set a real map's) */
static const char *map_name(const KpNetLobbySeat *s) { return s->map_name ? s->map_name : KP_NET_LOBBY_MAP_NAME; }
static size_t map_size(const KpNetLobbySeat *s) { return s->map_name ? s->map_size : KP_NET_LOBBY_MAP_SIZE; }

/* Its path in the maps folder (maps\<name>) */
static void map_path(const KpNetLobbySeat *s, char *out, size_t size) { snprintf(out, size, "maps\\%s", map_name(s)); }

static uint8_t *io_read(void *ud, const char *path, size_t *size)
{
	KpNetLobbySeat *s = (KpNetLobbySeat *)ud;
	uint8_t *copy;
	char own[KP_TRANSFER_PATH];
	map_path(s, own, sizeof own);
	if (strcmp(path, own) != 0 || !s->has_map || !s->map) return NULL;
	if (!(copy = (uint8_t *)malloc(map_size(s) ? map_size(s) : 1))) return NULL;
	memcpy(copy, s->map, map_size(s));
	*size = map_size(s);
	return copy;
}

static int io_find(void *ud, const char *name, uint32_t size, uint32_t crc, char *out, size_t out_size)
{
	KpNetLobbySeat *s = (KpNetLobbySeat *)ud;
	if (!s->has_map || !s->map || strcmp(name, map_name(s)) != 0 || size != map_size(s) ||
		crc != kp_transfer_crc(s->map, map_size(s)))
		return 0;
	map_path(s, out, out_size);
	return 1;
}

static int io_save(void *ud, const char *name, const uint8_t *data, size_t size, char *out, size_t out_size)
{
	KpNetLobbySeat *s = (KpNetLobbySeat *)ud;
	if (!kp_transfer_name_valid(name, KP_TRANSFER_NAME) || !kp_transfer_extension_ok(name) || size > KP_TRANSFER_MAX_SIZE)
		s->bad_saves++;
	free(s->download);
	s->download_size = 0;
	if (!(s->download = (uint8_t *)malloc(size ? size : 1))) return 0;
	memcpy(s->download, data, size);
	s->download_size = size;
	snprintf(out, out_size, KP_NET_LOBBY_DOWNLOADS "%s", name);
	return 1;
}

/* --- the test's map and templates --- */

void kp_net_lobby_templates(KpTemplates *t)
{
	KpTemplate *m = &t->list[KP_NET_LOBBY_MELEE], *u = &t->list[KP_NET_LOBBY_UMS];
	memset(t, 0, sizeof *t);
	snprintf(m->name, sizeof m->name, "Test Melee");
	m->record[KP_TEMPLATE_TYPE] = 2;
	m->record[KP_TEMPLATE_SUBTYPE] = 1;
	m->record[KP_TEMPLATE_VICTORY] = 1;
	m->record[KP_TEMPLATE_RESOURCES] = 1;
	m->record[KP_TEMPLATE_STARTING_UNITS] = 1;
	m->record[KP_TEMPLATE_ALLIES] = 1;
	snprintf(u->name, sizeof u->name, "Test Map Settings");
	memcpy(u->record, KP_TEMPLATE_UMS, KP_TEMPLATE_SIZE);
	t->count = KP_NET_LOBBY_TEMPLATES;
}

void kp_net_lobby_map_bytes(uint8_t *map, size_t size, uint32_t seed)
{
	uint32_t x = seed | 1u;
	size_t k;
	for (k = 0; k < size; k++) { /* xorshift */
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		map[k] = (uint8_t)x;
	}
}

void kp_net_lobby_map(KpNetMap *m, const KpNetGameHooks *hooks)
{
	KpMapHeader h;
	int p;
	memset(&h, 0, sizeof h);
	h.width = h.height = MAP_SIDE;
	for (p = 0; p < KP_NET_LOBBY_MAP_SLOTS; p++) {
		h.owners[p] = KP_CTRL_OPEN;
		h.races[p] = KP_MAP_RACE_USER_SELECT;
		h.user_select[p] = 1;
		h.forces[p] = (uint8_t)(p / 2);
	}
	h.force_flags[0] = h.force_flags[1] = 1;
	h.force_named[0] = h.force_named[1] = 1;
	snprintf(h.force_names[0], sizeof h.force_names[0], "Left Team");
	snprintf(h.force_names[1], sizeof h.force_names[1], "Right Team");
	snprintf(h.title, sizeof h.title, KP_NET_LOBBY_TITLE);
	h.players = h.humans = KP_NET_LOBBY_MAP_SLOTS;
	kp_netgame_map(m, &h, KP_NET_LOBBY_MAP_PATH, hooks);
}

void kp_net_lobby_header(KpNetGameHeader *h, const uint8_t *r)
{
	memset(h, 0, sizeof *h);
	snprintf(h->name, sizeof h->name, KP_NET_LOBBY_GAME);
	snprintf(h->title, sizeof h->title, KP_NET_LOBBY_TITLE);
	h->width = h->height = MAP_SIDE;
	h->max_players = KP_NET_LOBBY_MAP_SLOTS;
	h->speed = SPEED_FASTEST;
	h->type = r[KP_TEMPLATE_TYPE];
	h->sub = r[KP_TEMPLATE_SUB];
	h->subtype = (uint16_t)(r[KP_TEMPLATE_SUBTYPE] | r[KP_TEMPLATE_SUBTYPE + 1] << 8);
}

/* --- a seat --- */

int kp_net_lobby_seat_init(KpNetLobbySeat *s, const char *name, const KpTemplates *templates, const uint8_t *map,
	int has_map, const KpNetCalls *calls)
{
	KpNetCalls own;
	KpNetGameHooks h;
	KpNetGameConfig c;
	kp_net_lobby_seat_free(s);
	if (!calls) {
		own = kp_netgame_snet_calls(s->n.net);
		calls = &own;
	}
	memset(&h, 0, sizeof h);
	h.now = h_now;
	h.time = h_time;
	h.text = h_text;
	h.line = h_line;
	h.leave = h_leave;
	h.error = h_error;
	h.find_save = h_find_save;
	h.ud = s;
	memset(&c, 0, sizeof c);
	snprintf(c.player_name, sizeof c.player_name, "%s", name);
	c.templates = templates;
	c.transfer.read = io_read;
	c.transfer.find = io_find;
	c.transfer.save = io_save;
	c.transfer.ud = s;
	s->map = map;
	s->has_map = has_map;
	s->leave = -1;
	s->errors = 0;
	s->bad_saves = 0;
	s->press_start = 0;
	s->phase = KP_NET_LOBBY_MENU;
	s->line_count = 0;
	if (kp_netgame_init(&s->game, calls, &h, &c) != 0) return 0;
	s->inited = 1;
	return 1;
}

void kp_net_lobby_seat_free(KpNetLobbySeat *s)
{
	if (s->inited) kp_netgame_free(&s->game);
	s->inited = 0;
	free(s->download);
	s->download = NULL;
	s->download_size = 0;
}

void kp_net_lobby_frame(KpNetLobbySeat *s)
{
	if (s->phase == KP_NET_LOBBY_ROOM) {
		if (s->leave >= 0) { /* the room closes: Storm left */
			s->game.in_chat_room = 0;
			kp_netgame_abort(&s->game);
			kp_netgame_release(&s->game);
			s->phase = KP_NET_LOBBY_LEFT;
			return;
		}
		if (kp_netgame_poll(&s->game) != KP_CHAT_START) return;
		s->game.in_chat_room = 0;
		kp_netgame_release(&s->game);
		if (kp_netgame_ready_room(&s->game)) {
			kp_netgame_briefing_arm(&s->game);
			s->phase = KP_NET_LOBBY_READY_ROOM;
		} else {
			s->phase = KP_NET_LOBBY_STARTED;
		}
	} else if (s->phase == KP_NET_LOBBY_READY_ROOM) {
		if (s->press_start) {
			kp_netgame_briefing_start(&s->game, 0);
			s->press_start = 0;
		}
		if (kp_netgame_briefing_poll(&s->game)) s->phase = KP_NET_LOBBY_STARTED;
	}
}

int kp_net_lobby_create(KpNetLobbySeat *s, const KpTemplates *templates, int template_index)
{
	KpNetMap m;
	KpNetGameHeader h;
	kp_net_lobby_map(&m, &s->game.hooks);
	kp_netgame_set_map(&s->game, &m);
	if (s->save) {
		h = s->save->header;
		snprintf(h.name, sizeof h.name, KP_NET_LOBBY_GAME);
	} else {
		kp_net_lobby_header(&h, templates->list[template_index].record);
	}
	if (!kp_netgame_create(&s->game, &h, "") || !kp_netgame_enter(&s->game)) return 0;
	s->game.in_chat_room = 1;
	s->phase = KP_NET_LOBBY_ROOM;
	return 1;
}

const KpNetGameHeader *kp_net_lobby_listed(const KpNetGame *ng)
{
	int k;
	for (k = 0; k < ng->listed_count; k++)
		if (strcmp(ng->listed[k].name, KP_NET_LOBBY_GAME) == 0) return &ng->listed[k];
	return NULL;
}

int kp_net_lobby_enter(KpNetLobbySeat *s, const KpNetGameHeader *listed)
{
	KpNetWorld *w = s->n.w;
	KpNetGameHeader h = *listed;
	int ok;
	if (w) w->in_join = s->n.index;
	ok = kp_netgame_join(&s->game, &h);
	if (w) {
		w->in_join = -1;
		kp_net_world_pump(w);
	}
	if (!ok || !kp_netgame_enter(&s->game)) return 0;
	s->game.in_chat_room = 1;
	s->phase = KP_NET_LOBBY_ROOM;
	return 1;
}

/* --- a lobby --- */

static void frame(KpNetSeat *s) { kp_net_lobby_frame((KpNetLobbySeat *)s); }

static void event(KpNetSeat *s, const KpSnetEvent *e)
{
	KpNetLobbySeat *st = (KpNetLobbySeat *)s;
	if (st->inited) kp_netgame_event(&st->game, e);
}

int kp_net_lobby_init(KpNetLobby *l, struct KpT *T, void (*fail)(void *ud, const char *why), void *fail_ud,
	uint64_t seed, int seats, int lossy)
{
	int k;
	memset(l, 0, sizeof *l);
	kp_net_world_init(&l->n, T, fail, fail_ud);
	l->n.frame = frame;
	l->n.event = event;
	kp_net_lobby_templates(&l->templates);
	kp_net_lobby_map_bytes(l->map, sizeof l->map, KP_NET_LOBBY_MAP_SEED);
	if (!kp_net_world_lan(&l->n, seed)) return 0;
	if (lossy) kp_net_lan_link(l->n.lan, LOSSY_DELAY, LOSSY_JITTER, LOSSY_LOSS);
	for (k = 0; k < seats; k++) {
		l->seat[k].leave = -1;
		if (!kp_net_world_add_lan(&l->n, &l->seat[k].n)) return 0;
	}
	return 1;
}

void kp_net_lobby_init_sockets(KpNetLobby *l, struct KpT *T, void (*fail)(void *ud, const char *why), void *fail_ud)
{
	int k;
	memset(l, 0, sizeof *l);
	kp_net_world_init(&l->n, T, fail, fail_ud);
	l->n.frame = frame;
	l->n.event = event;
	kp_net_lobby_templates(&l->templates);
	kp_net_lobby_map_bytes(l->map, sizeof l->map, KP_NET_LOBBY_MAP_SEED);
	for (k = 0; k < KP_NET_WORLD_SEATS; k++) l->seat[k].leave = -1;
}

void kp_net_lobby_free(KpNetLobby *l)
{
	int k;
	for (k = 0; k < KP_NET_WORLD_SEATS; k++) kp_net_lobby_seat_free(&l->seat[k]);
	kp_net_world_release(&l->n);
}

uint32_t kp_net_lobby_now(const KpNetLobby *l) { return kp_net_world_now(&l->n); }

void kp_net_lobby_advance(KpNetLobby *l, uint32_t ms) { kp_net_world_advance(&l->n, kp_net_lobby_now(l) + ms); }

int kp_net_lobby_host(KpNetLobby *l, int k, int template_index, const KpNetCalls *calls)
{
	KpNetLobbySeat *s = &l->seat[k];
	return kp_net_lobby_seat_init(s, KP_NET_LOBBY_NAMES[k], &l->templates, l->map, 1, calls) &&
		kp_net_lobby_create(s, &l->templates, template_index);
}

int kp_net_lobby_join(KpNetLobby *l, int k, int has_map, const KpNetCalls *calls)
{
	KpNetLobbySeat *s = &l->seat[k];
	const KpNetGameHeader *g;
	if (!kp_net_lobby_seat_init(s, KP_NET_LOBBY_NAMES[k], &l->templates, l->map, has_map, calls)) return 0;
	kp_net_lobby_advance(l, KP_NET_LOBBY_LIST_MS);
	kp_netgame_enum(&s->game);
	g = kp_net_lobby_listed(&s->game);
	return g && kp_net_lobby_enter(s, g);
}

void kp_net_lobby_run_to_start(KpNetLobby *l, uint32_t ms)
{
	uint32_t until = kp_net_lobby_now(l) + ms;
	for (;;) {
		int k, waiting = 0;
		for (k = 0; k < l->n.count; k++)
			waiting += l->seat[k].phase == KP_NET_LOBBY_ROOM || l->seat[k].phase == KP_NET_LOBBY_READY_ROOM;
		if (!waiting || (int32_t)(kp_net_lobby_now(l) - until) >= 0) return;
		kp_net_lobby_advance(l, KP_NETGAME_POLL_MS);
	}
}

/* --- a resumed game's save --- */

#define RACE_ZERG 0
#define RACE_TERRAN 1
#define RACE_PROTOSS 2
#define SAVE_TILESET 4

void kp_net_lobby_save_init(KpNetGameSave *s, const KpTemplates *t, int template_index, int local)
{
	int k;
	memset(s, 0, sizeof *s);
	kp_net_lobby_header(&s->header, t->list[template_index].record);
	memcpy(s->header.template_record, t->list[template_index].record, KP_TEMPLATE_SIZE);
	s->header.save_id = KP_NET_LOBBY_SAVE_ID;
	s->rng_state = KP_NET_LOBBY_SAVE_RNG;
	s->local_slot = (uint8_t)local;
	s->local_player = (uint32_t)local;
	s->width = s->height = MAP_SIDE;
	s->tileset = SAVE_TILESET;
	for (k = 0; k < KP_NETGAME_SLOTS; k++) {
		s->players[k].slot = k;
		s->players[k].storm = -1;
	}
	snprintf(s->force_names[0], sizeof s->force_names[0], "Left Team");
	snprintf(s->force_names[1], sizeof s->force_names[1], "Right Team");
}

void kp_net_lobby_save_player(KpNetGameSave *s, int k, uint8_t type, uint8_t race, uint8_t force, const char *name)
{
	KpNetSlot *p = &s->players[k];
	p->type = type;
	p->race = race;
	p->force = force;
	snprintf(p->name, sizeof p->name, "%s", name);
	s->types[k] = type;
	s->races[k] = race;
}

void kp_net_lobby_melee_save(KpNetGameSave *s, const KpTemplates *t, int local)
{
	kp_net_lobby_save_init(s, t, KP_NET_LOBBY_MELEE, local);
	kp_net_lobby_save_player(s, 0, KP_CTRL_HUMAN, RACE_TERRAN, 0, "Alpha");
	kp_net_lobby_save_player(s, 1, KP_CTRL_COMPUTER, RACE_ZERG, 0, "Computer");
	kp_net_lobby_save_player(s, 2, KP_CTRL_HUMAN, RACE_PROTOSS, 0, "Beta");
	kp_net_lobby_save_player(s, 3, KP_CTRL_RESCUE, RACE_TERRAN, 0, "Rescue");
	kp_net_lobby_save_player(s, 4, KP_CTRL_HUMAN, RACE_ZERG, 0, "Gamma");
	s->header.save_players = 1;
}

/* --- a save's file --- */

#define SAVE_TAG 0x00690000u /* the tag's high WORD 0x69 */
#define SAVE_RNG 4
#define SAVE_GAME 0xc
#define SAVE_NAME 0x99
#define SAVE_RECORD 36
/* the core block's fields (their addresses less 0x5263d0, atlas P33) */
#define CORE_LOCAL_SLOT 0xc0
#define CORE_TYPES 0xc4
#define CORE_RACES 0xd0
#define CORE_WIDTH 0xe4
#define CORE_HEIGHT 0xe6
#define CORE_TILESET 0xec
#define CORE_FORCES 0xe4c0
#define CORE_FORCE_NAMES 0xe4cc
#define CORE_EXPANSION 0x10350

uint8_t *kp_net_lobby_save_file(const KpNetGameSave *s, int version, const char *name, int expansion, size_t *size)
{
	uint8_t header[KP_NETSAVE_HEADER], players[KP_NETSAVE_PLAYERS], tag[4], local[4];
	uint8_t *core = (uint8_t *)calloc(1, KP_NETSAVE_CORE), *buf = NULL;
	size_t len = 0, cap = 0, banner = strlen(KP_NET_LOBBY_SAVE_BANNER);
	int k, ok;
	if (!core) return NULL;
	memset(header, 0, sizeof header);
	kp_net_put16(header, (uint32_t)version);
	kp_net_put32(header + SAVE_RNG, s->rng_state);
	kp_netgame_header_write(&s->header, header + SAVE_GAME);
	snprintf((char *)header + SAVE_NAME, KP_NETSAVE_NAME, "%s", name);
	memset(players, 0, sizeof players);
	for (k = 0; k < KP_NETGAME_SLOTS; k++) {
		uint8_t *r = players + k * SAVE_RECORD;
		const KpNetSlot *p = &s->players[k];
		kp_net_put32(r, (uint32_t)p->slot);
		kp_net_put32(r + 4, (uint32_t)p->storm);
		r[8] = p->type;
		r[9] = p->race;
		r[0xa] = p->force;
		memcpy(r + 0xb, p->name, sizeof p->name);
	}
	core[CORE_LOCAL_SLOT] = s->local_slot;
	memcpy(core + CORE_TYPES, s->types, sizeof s->types);
	memcpy(core + CORE_RACES, s->races, sizeof s->races);
	kp_net_put16(core + CORE_WIDTH, s->width);
	kp_net_put16(core + CORE_HEIGHT, s->height);
	kp_net_put16(core + CORE_TILESET, s->tileset);
	memcpy(core + CORE_FORCES, s->forces, sizeof s->forces);
	for (k = 0; k < KP_NETGAME_FORCES; k++)
		memcpy(core + CORE_FORCE_NAMES + k * KP_NETGAME_FORCE_NAME, s->force_names[k], KP_NETGAME_FORCE_NAME);
	core[CORE_EXPANSION] = (uint8_t)expansion;
	kp_net_put32(tag, SAVE_TAG);
	kp_net_put32(local, s->local_player);
	ok = (buf = (uint8_t *)malloc(banner + 1 + sizeof tag)) != NULL;
	if (ok) {
		cap = len = banner + 1 + sizeof tag;
		memcpy(buf, KP_NET_LOBBY_SAVE_BANNER, banner);
		buf[banner] = 0x1a;
		memcpy(buf + banner + 1, tag, sizeof tag);
		ok = kp_rep_block_write(&buf, &len, &cap, header, sizeof header) == 0 &&
			kp_rep_block_write(&buf, &len, &cap, players, sizeof players) == 0 &&
			kp_rep_block_write(&buf, &len, &cap, core, KP_NETSAVE_CORE) == 0;
	}
	free(core);
	if (ok) { /* the raw DWORD of the local player */
		uint8_t *grown = (uint8_t *)realloc(buf, len + sizeof local);
		ok = grown != NULL;
		if (ok) {
			buf = grown;
			memcpy(buf + len, local, sizeof local);
			len += sizeof local;
		}
	}
	if (!ok) {
		free(buf);
		return NULL;
	}
	*size = len;
	return buf;
}

int kp_net_lobby_slot_of(const KpNetLobby *l, int view, int k)
{
	const KpNetGame *ng = &l->seat[view].game;
	int s, storm = l->seat[k].game.local_storm;
	if (storm < 0) return -1;
	for (s = 0; s < 8; s++)
		if (ng->slots[s].type == KP_CTRL_HUMAN && ng->slots[s].storm == storm) return s;
	return -1;
}
