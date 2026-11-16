/*
 * The seats of the development mode (koprulu --map PATH --seats N; phase M, stage M6, ADR 0025): a custom game's N
 * humans in one process, each seat a session of the same game over one turn hub (kp_hub.h). The window shows one seat
 * (the flow's session, with its console); the others run without an interface, stepped to the shown seat's frame after
 * each of its frames, their events dropped (as test_seats' seats). Then the seats' shared hashes (kp_shared_hash) are
 * compared: the first difference stops the game with a report of the frame, the walk's sections that differ and
 * kp_dump's categories that differ (some of those are the seat's own state: a hint where to look).
 */
#ifndef KP_SEATS_H
#define KP_SEATS_H

#include <stddef.h>
#include <stdint.h>

#include "kp_hub.h"
#include "kp_session.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_SEATS_REPORT 1024

typedef struct {
	int count;                        /* the seats, 0 outside the mode */
	int shown;                        /* the seat whose session is the flow's */
	KpTurnHub *hub;
	KpSession *session[KP_HUB_SEATS]; /* every seat's (the shown one's owned by the flow) */
	int64_t checked;                  /* the last frame whose hashes were compared, -1 none */
	int stopped;                      /* a seat failed or the seats diverged: nothing steps or compares again */
	char report[KP_SEATS_REPORT];
} KpSeats;

/* A lobby's setup (kp_setup_lobby's, not started) for n seats (2..KP_HUB_SEATS): its human (Storm ID 0) and its first
 * n - 1 computers made humans with the Storm IDs 1.. in slot order, named "Seat k", in a network game (the multiplayer
 * byte 0x672664). -1 with why when the setup has no n such slots. */
int kp_seats_setup(KpGameSetup *setup, int n, char *why, size_t why_size);
/* After the seats' sessions started over the hub (KpSessionStart's hub, seat and Storm ID): every seat sends its first
 * turns and runs its first (kp_hub_begin, kp_session_seat_begin). -1 with the report. */
int kp_seats_begin(KpSeats *s);
/* The hidden seats stepped to the shown seat's frame, then, when every seat stands at it, their shared hashes
 * compared: 0, or -1 with the report when a seat failed or the hashes differ (s->stopped). */
int kp_seats_frame(KpSeats *s);
/* The report of the first difference between two seats' games (the frame, the walk's sections, kp_dump's categories)
 * into out. */
void kp_seats_compare(KpGame *a, KpGame *b, int seat_a, int seat_b, char *out, size_t size);
/* The sessions but the shown seat's (the flow frees that one) and the hub freed; s zeroed. */
void kp_seats_free(KpSeats *s);

#ifdef __cplusplus
}
#endif

#endif
