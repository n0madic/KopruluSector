/* The turn source (kp_turns.h; phase T, stage T1, ADR 0022). */
#include "kp_turns.h"

#include <stdlib.h>
#include <string.h>

int kp_turns_replay(KpTurns *t, const KpRep *rep)
{
	int k;
	if (!(t->feed = (KpRecorder *)malloc(sizeof *t->feed))) return -1;
	if (kp_record_init(t->feed) != 0) {
		free(t->feed);
		t->feed = NULL;
		return -1;
	}
	if (kp_record_load(t->feed, rep->record, rep->record_size) != 0) {
		kp_record_free(t->feed);
		free(t->feed);
		t->feed = NULL;
		return -1;
	}
	t->kind = KP_TURNS_REPLAY;
	t->frames = kp_rep_frames(rep);
	for (k = 0; k < KP_TURNS_SLOTS; k++) {
		KpRepPlayer p;
		kp_rep_player(rep, k, &p);
		t->feed_storm[k] = (uint32_t)p.storm;
		t->feed_slot[k] = p.slot;
	}
	return 0;
}

void kp_turns_seats(KpTurns *t, KpTurnHub *hub, int seat)
{
	t->kind = KP_TURNS_SEATS;
	t->hub = hub;
	t->seat = seat;
}

void kp_turns_net(KpTurns *t, KpNetGame *net)
{
	t->kind = KP_TURNS_NET;
	t->net = net;
}

void kp_turns_net_begin(KpTurns *t, int64_t speed)
{
	int id;
	for (id = 0; id < KP_NETGAME_PLAYERS; id++) {
		int r = t->net->storm_index[id];
		if (r < KP_TURNS_SLOTS) t->storm[r] = id;
	}
	t->frames_per_turn = kp_hub_frames_per_turn((int)speed, (int)t->net->turns_per_second);
	t->countdown = 0;
}

void kp_turns_begin(KpTurns *t, int64_t local_player)
{
	int p;
	for (p = 0; p < KP_TURNS_SLOTS; p++) t->storm[p] = -1;
	if (t->kind == KP_TURNS_LOCAL && local_player >= 0 && local_player < KP_TURNS_SLOTS) t->storm[local_player] = 0;
	if (t->kind == KP_TURNS_SEATS) { /* every seat's commands under its Storm ID */
		for (p = 0; p < t->hub->seats; p++) {
			int64_t r = t->hub->record[p];
			if (r >= 0 && r < KP_TURNS_SLOTS) t->storm[r] = t->hub->storm[p];
		}
	}
	t->feed_player = local_player;
}

void kp_turns_free(KpTurns *t)
{
	if (t->feed) kp_record_free(t->feed);
	free(t->feed);
	free(t->list);
	memset(t, 0, sizeof *t);
}

int kp_turns_playing(const KpTurns *t) { return t->kind == KP_TURNS_REPLAY; }
int kp_turns_ended(const KpTurns *t) { return t->ended; }
uint32_t kp_turns_frames(const KpTurns *t) { return t->kind == KP_TURNS_REPLAY ? t->frames : 0; }

/* The replay filter of ExecuteTurnCommands 0x475d30 for the live commands of a playback (table 0x47636c): the
 * keep-alive, restart, pause and resume, the sync and the four the network's (0x37-0x3b), latency and the replay
 * speed. */
static int live_in_replay(uint8_t id)
{
	return id == 0x05 || id == 0x08 || id == 0x10 || id == 0x11 || (id >= 0x37 && id <= 0x3b) || id == 0x55 || id == 0x56;
}

int kp_turns_submit(KpTurns *t, const uint8_t *bytes, size_t size, int64_t player)
{
	KpQueuedCommand *c;
	if (t->kind == KP_TURNS_SEATS) return kp_hub_submit(t->hub, t->seat, bytes, size);
	if (t->kind == KP_TURNS_NET) return kp_netgame_command(t->net, bytes, size);
	if (t->pending_count >= KP_TURNS_PENDING || size == 0 || size > KP_TURNS_COMMAND_BYTES) return -1;
	if (player < 0 || player >= KP_TURNS_SLOTS) return -1; /* the dispatcher's players: a seat's, 0..7 */
	if (t->kind == KP_TURNS_REPLAY && !live_in_replay(bytes[0])) return -1;
	c = &t->pending[t->pending_count++];
	c->player = player;
	c->size = (uint16_t)size;
	memcpy(c->bytes, bytes, size);
	return 0;
}

/* The frame's list with room for n more commands; -1 out of memory. */
static int list_room(KpTurns *t, int count, int n)
{
	KpCommand *l;
	int cap;
	if (count + n <= t->list_cap) return 0;
	cap = t->list_cap ? t->list_cap : KP_TURNS_PENDING;
	while (cap < count + n) cap *= 2;
	if (!(l = (KpCommand *)realloc(t->list, (size_t)cap * sizeof *l))) return -1;
	t->list = l;
	t->list_cap = cap;
	return 0;
}

typedef struct {
	KpTurns *t;
	int count, failed;
	int64_t player;
} Split;

static void add_command(void *ud, const uint8_t *cmd, size_t n)
{
	Split *sp = (Split *)ud;
	KpCommand *c;
	if (sp->failed || list_room(sp->t, sp->count, 1) != 0) {
		sp->failed = 1;
		return;
	}
	c = &sp->t->list[sp->count++];
	c->player = sp->player;
	c->bytes = cmd;
	c->size = n;
}

/* FeedReplayCommands 0x4769b0's part of a frame: the commands recorded for it, each buffer's player the one whose
 * record has its Storm ID (kept from the buffer before when none has), walked as ExecuteTurnCommands 0x475d30 walks
 * them; 1 at the end of the record (SetReplayPaused 0x480e00), -1 out of memory. */
static int feed(KpTurns *t, int64_t frame, Split *sp)
{
	const KpRecordEntry *e;
	int n, k, j;
	if (kp_record_feed(t->feed, frame, t->frames, &e, &n)) return 1;
	for (k = 0; k < n; k++) {
		for (j = 0; j < KP_TURNS_SLOTS; j++) {
			if (t->feed_storm[j] == e[k].storm && t->feed_slot[j] < KP_TURNS_SLOTS) { /* checked at the start too */
				t->feed_player = t->feed_slot[j];
				break;
			}
		}
		sp->player = t->feed_player;
		(void)kp_record_split(e[k].bytes, e[k].size, add_command, sp);
	}
	return sp->failed ? -1 : 0;
}

int kp_turns_frame(KpTurns *t, int64_t frame, const KpCommand **list, int *count)
{
	Split sp;
	int status = KP_TURNS_READY, k;
	memset(&sp, 0, sizeof sp);
	sp.t = t;
	*list = NULL;
	*count = 0;
	if (t->kind == KP_TURNS_SEATS) {
		(void)frame;
		switch (kp_hub_frame(t->hub, t->seat, list, count)) {
		case KP_HUB_TURN: return KP_TURNS_READY;
		case KP_HUB_BETWEEN: return KP_TURNS_BETWEEN;
		case KP_HUB_WAIT: return KP_TURNS_WAIT;
		default: return KP_TURNS_NO_MEMORY;
		}
	}
	if (t->kind == KP_TURNS_REPLAY) {
		int fed = feed(t, frame, &sp);
		if (fed < 0) return KP_TURNS_NO_MEMORY;
		if (fed > 0 && !t->ended) {
			t->ended = 1;
			status = KP_TURNS_ENDED;
		}
	}
	if (list_room(t, sp.count, t->pending_count) != 0) return KP_TURNS_NO_MEMORY;
	for (k = 0; k < t->pending_count; k++) {
		const KpQueuedCommand *q = &t->pending[k];
		KpCommand *c = &t->list[sp.count++];
		c->player = q->player;
		c->bytes = q->bytes;
		c->size = q->size;
	}
	*list = t->list;
	*count = sp.count;
	return status;
}

void kp_turns_done(KpTurns *t) { t->pending_count = 0; }

typedef struct {
	KpRecorder *rec;
	int64_t frame;
	uint8_t storm;
} Record;

static void record_command(void *ud, const uint8_t *cmd, size_t n)
{
	const Record *r = (const Record *)ud;
	kp_record_command(r->rec, r->frame, r->storm, cmd, n);
}

void kp_turns_record(const KpTurns *t, KpRecorder *rec, int64_t frame, const KpCommand *list, int count)
{
	Record r;
	int k;
	r.rec = rec;
	r.frame = frame;
	for (k = 0; k < count; k++) {
		int64_t p = list[k].player;
		if (p < 0 || p >= KP_TURNS_SLOTS || t->storm[p] < 0) continue;
		r.storm = (uint8_t)t->storm[p];
		(void)kp_record_split(list[k].bytes, list[k].size, record_command, &r);
	}
}
