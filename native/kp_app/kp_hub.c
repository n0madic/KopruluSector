/* The turn hub (kp_hub.h; phase M, stage M6, ADR 0025). */
#include "kp_hub.h"

#include <stdlib.h>
#include <string.h>

#include "kp_record.h"
#include "kp_session.h"

#define LATENCY_COMMAND 0x55
#define MAX_TURNS_PER_SECOND 20

int kp_hub_frames_per_turn(int speed, int turns_per_second)
{
	uint32_t ms, n;
	if (speed < 0 || speed >= KP_CLOCK_SPEEDS) return 1;
	if (turns_per_second <= 0 || turns_per_second >= MAX_TURNS_PER_SECOND) turns_per_second = MAX_TURNS_PER_SECOND;
	ms = (uint32_t)KP_CLOCK_SPEED_MS[speed];
	n = 1000u / (ms * (uint32_t)turns_per_second);
	return n == 0 ? 1 : (int)n;
}

KpTurnHub *kp_hub_new(int seats, int frames_per_turn)
{
	KpTurnHub *h;
	if (seats < 1 || seats > KP_HUB_SEATS) return NULL;
	if (!(h = (KpTurnHub *)calloc(1, sizeof *h))) return NULL;
	h->seats = seats;
	h->frames_per_turn = frames_per_turn < 1 ? 1 : frames_per_turn;
	h->in_transit = KP_HUB_IN_TRANSIT;
	return h;
}

void kp_hub_free(KpTurnHub *h)
{
	int k;
	if (!h) return;
	for (k = 0; k < KP_HUB_SEATS; k++) free(h->list[k]);
	free(h);
}

int kp_hub_seat(KpTurnHub *h, int seat, int64_t record, int32_t storm)
{
	int k;
	if (h->started || seat < 0 || seat >= h->seats || storm < 0 || storm >= KP_HUB_SEATS) return -1;
	for (k = 0; k < h->seats; k++) {
		if (k != seat && h->assigned[k] && h->storm[k] == storm) return -1;
	}
	h->record[seat] = record;
	h->storm[seat] = storm;
	h->assigned[seat] = 1;
	return 0;
}

int kp_hub_target(const KpTurnHub *h, int seat) { return h->in_transit + h->extra[seat]; }

int64_t kp_hub_in_transit(const KpTurnHub *h, int seat) { return h->sent_count[seat] - h->received[seat]; }

/* SNetSendTurn 0x15027ee0 of the seat's outgoing turn (an empty one too) into its stream. */
static void send_turn(KpTurnHub *h, int seat)
{
	KpHubTurn *t = &h->sent[seat][h->sent_count[seat] % KP_HUB_RING];
	t->size = h->out[seat].size;
	memcpy(t->bytes, h->out[seat].bytes, h->out[seat].size);
	h->sent_count[seat]++;
	h->out[seat].size = 0;
}

/* SendTurns 0x476500: the turns in transit topped up to the target. */
static void send_turns(KpTurnHub *h, int seat)
{
	while (kp_hub_in_transit(h, seat) < kp_hub_target(h, seat)) send_turn(h, seat);
}

int kp_hub_begin(KpTurnHub *h)
{
	int k;
	for (k = 0; k < h->seats; k++) {
		if (!h->assigned[k]) return -1;
	}
	h->started = 1;
	for (k = 0; k < h->seats; k++) send_turns(h, k);
	return 0;
}

int kp_hub_submit(KpTurnHub *h, int seat, const uint8_t *bytes, size_t size)
{
	KpHubTurn *out;
	if (seat < 0 || seat >= h->seats || size == 0 || size > KP_HUB_MESSAGE) return -1;
	out = &h->out[seat];
	if (out->size + size > KP_HUB_MESSAGE) {
		/* the turn is full: sent first (0x476da0), unless the seat has too many in transit already */
		if (kp_hub_in_transit(h, seat) >= KP_HUB_TRANSIT_LIMIT - h->in_transit) {
			h->dropped++;
			return -1;
		}
		send_turn(h, seat);
	}
	memcpy(out->bytes + out->size, bytes, size);
	out->size = (uint16_t)(out->size + size);
	return 0;
}

typedef struct {
	KpTurnHub *h;
	int seat, count, failed;
	int64_t player;
} Split;

static void add_command(void *ud, const uint8_t *cmd, size_t n)
{
	Split *sp = (Split *)ud;
	KpTurnHub *h = sp->h;
	KpCommand *c;
	if (sp->failed) return;
	if (sp->count == h->list_cap[sp->seat]) {
		int cap = h->list_cap[sp->seat] ? 2 * h->list_cap[sp->seat] : 64;
		KpCommand *l = (KpCommand *)realloc(h->list[sp->seat], (size_t)cap * sizeof *l);
		if (!l) {
			sp->failed = 1;
			return;
		}
		h->list[sp->seat] = l;
		h->list_cap[sp->seat] = cap;
	}
	c = &h->list[sp->seat][sp->count++];
	c->player = sp->player;
	c->bytes = cmd;
	c->size = n;
	/* OnLatencyCommand 0x476d10 as the seat runs it: the extra turns of its next sends */
	if (cmd[0] == LATENCY_COMMAND && n >= 2) h->extra[sp->seat] = cmd[1] < KP_HUB_EXTRA_MAX ? cmd[1] : KP_HUB_EXTRA_MAX;
}

/* TurnManagerStep 0x476c00: the turn received, the turns sent, the turn run (its list); KP_HUB_WAIT when a seat's
 * turn is missing. */
static int turn_step(KpTurnHub *h, int seat, const KpCommand **list, int *count)
{
	Split sp;
	int64_t turn;
	int storm, k;
	/* ReceiveTurns: the turn every seat sent */
	turn = h->received[seat];
	for (k = 0; k < h->seats; k++) {
		if (h->sent_count[k] <= turn) return KP_HUB_WAIT;
	}
	h->received[seat]++;
	send_turns(h, seat);
	/* ExecuteGameTurn: the Storm IDs from 7 down to 0, each seat's turn as the dispatcher walks it */
	memset(&sp, 0, sizeof sp);
	sp.h = h;
	sp.seat = seat;
	for (storm = KP_HUB_SEATS - 1; storm >= 0; storm--) {
		for (k = 0; k < h->seats; k++) {
			const KpHubTurn *t;
			if (h->storm[k] != storm) continue;
			t = &h->sent[k][turn % KP_HUB_RING];
			sp.player = h->record[k];
			(void)kp_record_split(t->bytes, t->size, add_command, &sp);
		}
	}
	if (sp.failed) return KP_HUB_NO_MEMORY;
	*list = h->list[seat];
	*count = sp.count;
	return KP_HUB_TURN;
}

int kp_hub_first_turn(KpTurnHub *h, int seat, const KpCommand **list, int *count)
{
	*list = NULL;
	*count = 0;
	if (!h->started || seat < 0 || seat >= h->seats || h->received[seat] != 0) return KP_HUB_WAIT;
	return turn_step(h, seat, list, count);
}

int kp_hub_frame(KpTurnHub *h, int seat, const KpCommand **list, int *count)
{
	int got;
	*list = NULL;
	*count = 0;
	if (!h->started || seat < 0 || seat >= h->seats) return KP_HUB_WAIT;
	if (h->countdown[seat] > 1) { /* PollTurn: a frame of the turn without the turn manager */
		h->countdown[seat]--;
		return KP_HUB_BETWEEN;
	}
	got = turn_step(h, seat, list, count);
	/* the turn's frames counted down from its poll; a wait polls again at the next frame */
	h->countdown[seat] = got == KP_HUB_TURN ? h->frames_per_turn : 1;
	return got;
}
