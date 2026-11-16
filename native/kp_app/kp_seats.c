/* The seats of the development mode (kp_seats.h; phase M, stage M6, ADR 0025). */
#include "kp_seats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_ids.h"

#define SECTIONS_MAX 64   /* the walk's sections (KP_SAVE_SECTIONS_MAX) */
#define CATEGORIES_MAX 64 /* kp_dump's categories */
#define CATEGORY_NAME 32
#define FIELDS_SHOWN 12   /* the snapshot's differing fields a report names */

int kp_seats_setup(KpGameSetup *setup, int n, char *why, size_t why_size)
{
	int p, seats = 0, human = -1;
	if (n < 2 || n > KP_HUB_SEATS) {
		snprintf(why, why_size, "seats: %d seats, 2 to %d", n, KP_HUB_SEATS);
		return -1;
	}
	for (p = 0; p < 8; p++) {
		if (setup->type[p] == KP_CTRL_HUMAN && setup->storm[p] == 0) human = p;
	}
	if (human < 0) {
		snprintf(why, why_size, "seats: the lobby has no player of Storm ID 0");
		return -1;
	}
	seats = 1;
	for (p = 0; p < 8 && seats < n; p++) {
		if (setup->type[p] != KP_CTRL_LOBBY_COMPUTER) continue;
		setup->type[p] = KP_CTRL_HUMAN;
		setup->storm[p] = seats;
		snprintf(setup->name[p], sizeof setup->name[p], "Seat %d", seats);
		seats++;
	}
	if (seats < n) {
		snprintf(why, why_size, "seats: the map's lobby has room for %d seats, not %d", seats, n);
		return -1;
	}
	setup->multiplayer = 1;
	return 0;
}

static int stop(KpSeats *s, const char *fmt, int seat, const char *what)
{
	s->stopped = 1;
	snprintf(s->report, sizeof s->report, fmt, seat, what);
	return -1;
}

int kp_seats_begin(KpSeats *s)
{
	int k;
	s->checked = -1;
	if (kp_hub_begin(s->hub) != 0) return stop(s, "seats: seat %d%s has no Storm ID", 0, "");
	for (k = 0; k < s->count; k++) {
		if (kp_session_seat_begin(s->session[k]) != 0) return stop(s, "seats: seat %d's first turn: %s", k, s->session[k]->error);
	}
	return 0;
}

/* The game's events of a hidden seat with no frame of its own dropped. */
static void drop_events(KpSession *ss)
{
	KpEvent e;
	while (kp_event_take(ss->g, &e)) {
	}
}

typedef struct {
	int count;
	const char *tag[SECTIONS_MAX];
	uint64_t hash[SECTIONS_MAX];
	char category[CATEGORIES_MAX][CATEGORY_NAME]; /* copied: the dump's names live in its buffers */
	uint32_t crc[CATEGORIES_MAX];
	int categories;
} Hashes;

static void each_section(void *ud, const char *tag, uint64_t hash, size_t bytes)
{
	Hashes *h = (Hashes *)ud;
	(void)bytes;
	if (h->count == SECTIONS_MAX) return;
	h->tag[h->count] = tag;
	h->hash[h->count++] = hash;
}

static void each_category(void *ud, const char *category, uint32_t crc)
{
	Hashes *h = (Hashes *)ud;
	if (h->categories == CATEGORIES_MAX) return;
	snprintf(h->category[h->categories], CATEGORY_NAME, "%s", category);
	h->crc[h->categories++] = crc;
}

static void append(char *out, size_t size, const char *text)
{
	size_t n = strlen(out);
	if (n + 1 < size) snprintf(out + n, size - n, "%s", text);
}

/* The snapshot's fields that differ but the seat's (in a section the shared hash walks in a canonical order, WRLD's,
 * a slot's difference may be none of the state's: a hint). */
static void field_diff(KpGame *a, KpGame *b, char *out, size_t size)
{
	KpSnapshotField *fa = NULL, *fb = NULL;
	size_t na = 0, nb = 0, bytes = kp_snapshot_size(a), k;
	uint8_t *sa = (uint8_t *)malloc(bytes), *sb = (uint8_t *)malloc(bytes);
	KpError err;
	int shown = 0;
	append(out, size, "; fields");
	if (!sa || !sb || kp_snapshot_size(b) != bytes || kp_snapshot(a, sa, bytes, &err) != 0 || kp_snapshot(b, sb, bytes, &err) != 0
		|| kp_snapshot_fields(a, &fa, &na) != 0 || kp_snapshot_fields(b, &fb, &nb) != 0 || na != nb) {
		append(out, size, " (no snapshots)");
	} else {
		for (k = 0; k < na; k++) {
			char name[160];
			if (fa[k].seat || memcmp(sa + fa[k].offset, sb + fb[k].offset, fa[k].bytes) == 0) continue;
			if (shown++ == FIELDS_SHOWN) {
				append(out, size, " ...");
				break;
			}
			{ /* its first element that differs */
				size_t e = fa[k].elem ? fa[k].elem : 1, at = 0;
				while (at + e <= fa[k].bytes && memcmp(sa + fa[k].offset + at, sb + fb[k].offset + at, e) == 0) at += e;
				snprintf(name, sizeof name, " %s/%s[%lu]", fa[k].tag ? fa[k].tag : "-", fa[k].name, (unsigned long)(at / e));
			}
			append(out, size, name);
		}
	}
	free(fa);
	free(fb);
	free(sa);
	free(sb);
}

void kp_seats_compare(KpGame *a, KpGame *b, int seat_a, int seat_b, char *out, size_t size)
{
	Hashes ha, hb;
	int k, any = 0;
	memset(&ha, 0, sizeof ha);
	memset(&hb, 0, sizeof hb);
	snprintf(out, size, "seats %d and %d differ at frame %lld: sections", seat_a, seat_b,
		(long long)kp_scalar(a, KP_FRAME));
	if (kp_shared_hash(a, each_section, &ha, NULL) != 0 || kp_shared_hash(b, each_section, &hb, NULL) != 0) {
		append(out, size, " (out of memory)");
		return;
	}
	for (k = 0; k < ha.count && k < hb.count; k++) {
		if (ha.hash[k] == hb.hash[k] && strcmp(ha.tag[k], hb.tag[k]) == 0) continue;
		append(out, size, " ");
		append(out, size, ha.tag[k]);
		any = 1;
	}
	if (ha.count != hb.count) append(out, size, " (their counts)");
	else if (!any) append(out, size, " (none)");
	append(out, size, "; dump categories");
	any = 0;
	if (kp_dump_crcs(a, 0, each_category, &ha) != 0 || kp_dump_crcs(b, 0, each_category, &hb) != 0) {
		append(out, size, " (unreadable)");
		return;
	}
	for (k = 0; k < ha.categories && k < hb.categories; k++) {
		if (ha.crc[k] == hb.crc[k] && strcmp(ha.category[k], hb.category[k]) == 0) continue;
		append(out, size, " ");
		append(out, size, ha.category[k]);
		any = 1;
	}
	if (!any) append(out, size, " (none)");
	field_diff(a, b, out, size);
}

typedef struct {
	KpSeats *s;
	int seat;
	uint64_t hash;
	int failed;
} Hash;

/* A seat's shared hash under its session's failure handler. */
static void seat_hash(void *ud)
{
	Hash *h = (Hash *)ud;
	if (kp_shared_hash(h->s->session[h->seat]->g, NULL, NULL, &h->hash) != 0) h->failed = 1;
}

typedef struct {
	KpSeats *s;
	int seat;
} Compare;

/* The report of seat 0 against the seat whose hash differs from it (the pair known to differ). */
static void seat_compare(void *ud)
{
	Compare *c = (Compare *)ud;
	KpSeats *s = c->s;
	kp_seats_compare(s->session[0]->g, s->session[c->seat]->g, 0, c->seat, s->report, sizeof s->report);
}

int kp_seats_frame(KpSeats *s)
{
	KpSession *shown;
	int64_t frame;
	int k, progress, steps[KP_HUB_SEATS];
	uint64_t want = 0;
	if (s->stopped || s->count == 0) return s->stopped ? -1 : 0;
	shown = s->session[s->shown];
	if (shown->failed) return stop(s, "seats: the shown seat %d stopped: %s", s->shown, shown->error);
	frame = kp_scalar(shown->g, KP_FRAME);
	/* each hidden seat to the shown seat's frame; a seat waiting for another's turn steps again once that one did */
	memset(steps, 0, sizeof steps);
	do {
		progress = 0;
		for (k = 0; k < s->count; k++) {
			KpSession *ss = s->session[k];
			if (k == s->shown) continue;
			while (kp_scalar(ss->g, KP_FRAME) < frame && kp_session_step(ss, NULL)) {
				progress = 1;
				steps[k]++;
			}
			if (ss->failed) return stop(s, "seats: seat %d stopped: %s", k, ss->error);
		}
	} while (progress);
	/* each hidden seat's frame, as its own machine runs one each pass of its loop, a step or none */
	for (k = 0; k < s->count; k++) {
		KpSession *ss = s->session[k];
		if (k == s->shown) continue;
		if (s->hidden) s->hidden(s->hidden_ud, k, steps[k]);
		else drop_events(ss);
		if (ss->failed) return stop(s, "seats: seat %d's frame: %s", k, ss->error);
	}
	for (k = 0; k < s->count; k++) {
		if (kp_scalar(s->session[k]->g, KP_FRAME) != frame) return 0; /* the shown seat waits for this one's turn */
	}
	if (frame == s->checked) return 0;
	s->checked = frame;
	for (k = 0; k < s->count; k++) {
		Hash h;
		memset(&h, 0, sizeof h);
		h.s = s;
		h.seat = k;
		if (kp_session_guard(s->session[k], seat_hash, &h) != 0 || h.failed) {
			return stop(s, "seats: seat %d's shared hash: %s", k, h.failed ? "out of memory" : s->session[k]->error);
		}
		if (k == 0) {
			want = h.hash;
		} else if (h.hash != want) {
			Compare c;
			c.s = s;
			c.seat = k;
			s->stopped = 1;
			if (kp_session_guard(s->session[c.seat], seat_compare, &c) != 0) {
				snprintf(s->report, sizeof s->report, "seats: seat %d differs at frame %lld", c.seat, (long long)frame);
			}
			return -1;
		}
	}
	return 0;
}

void kp_seats_free(KpSeats *s)
{
	int k;
	for (k = 0; k < s->count; k++) {
		if (k == s->shown || !s->session[k]) continue;
		kp_session_free(s->session[k]);
		free(s->session[k]);
	}
	kp_hub_free(s->hub);
	memset(s, 0, sizeof *s);
}
