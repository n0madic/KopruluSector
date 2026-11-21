/*
 * The fuzz targets' entries (phase T, stage T2; ADR 0022): the untrusted inputs a player can be handed, each fed to the
 * functions that read it. libFuzzer's targets (tests/fuzz/kp_fuzz_main.c, the KP_FUZZ build) and the suites' replay
 * of the kept inputs (tests/c/test_fuzz.c, every build) call the same entries.
 *
 *   rep       a replay file: kp_rep_read, kp_session_replay_start, then the turn source's feed of every frame; a file
 *             read is written back and must read again;
 *   chk       a scenario: kp_chk_read with and without the expansion, a custom map's melee and use-map-settings
 *             loads (kp_chk_load), and kp_mission_from_chk and kp_mission_from_map's two loads when the game's data
 *             is present;
 *   save      the first byte's low bit picks the form: 0 a save file (its KPF's CRCs made right, kp_save_check, then
 *             kp_save_load into a recorded session), 1 edits of the game's own native save (8 MB, past libFuzzer's
 *             inputs) by its sections, {u8 section, u24 offset, u8 n, n bytes}, its hash table then made right by
 *             kp_save_seal and the save given to kp_load;
 *   commands  the first byte picks the scenario; then chunks {u8 player | steps << 3, u8 n, n bytes} whose bytes
 *             kp_record_split walks into the player's commands, the steps after each chunk run by the session; each
 *             input in a single player's game and in a network game's (the game's multiplayer). A network game
 *             refuses no seat's commands (every seat runs them all, so a refusal stops the game for every seat):
 *             there a refusal is a finding;
 *   turns     a network game's turn hub (kp_hub.h; phase M, stage M6) of 1 to 8 seats: the seats' commands, first
 *             turns and frames in the input's order, each turn's list the same on every seat and by the Storm IDs from
 *             7 down, no seat past QueueTurnCommand's bound of turns in transit nor its stream's ring;
 *   udp       the UDP provider (kp_snp_udp.h; phase N, stage N2) alone: datagrams from peers on both sockets, its clock,
 *             Storm's calls (the queue taken, advertisements, sends, the game list), its sends well-formed and its
 *             queue and list within their bounds (kp_fuzz_net.c tells the ops);
 *   snet      Storm's SNet (kp_snet.h) over it: the same datagrams and clock, the pump's passes, turns, messages and
 *             every export StarCraft calls, a game hosted or joined, the events' handlers calling back in; its packets
 *             well-formed and its records within their bound.
 *   lobby     the multiplayer lobby (kp_netgame.h; phase N, stage N3) of one seat with the screens' pass, over an SNet
 *             that answers from the input: the receive calls' turns, messages and events, the players' names, the
 *             game's mode, failing calls, the clock, a saved game's file (kp_netsave.h) the lobby resumes and the
 *             screens' actions; its sends within the provider's message size and its state within its bounds
 *             (kp_fuzz_lobby.c tells the ops);
 *   transfer  the map transfer (kp_transfer.h) alone: other seats' packets, the offer's peers, cancels and resets; its
 *             packets well-formed, its saves only of names it may save, its transfers within their bounds.
 *
 * save and commands need the game's data (kp_oracle_data("."), run from the source tree's root); the games start once
 * a process and each input begins from their snapshot. A crash or a sanitizer's report is a finding. A game the
 * game's own checks stop (kp_game_fail under the session's guard) is a refusal: counted, not a finding.
 */
#ifndef KP_FUZZ_H
#define KP_FUZZ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { KP_FUZZ_REP, KP_FUZZ_CHK, KP_FUZZ_SAVE, KP_FUZZ_COMMANDS, KP_FUZZ_TURNS, KP_FUZZ_UDP, KP_FUZZ_SNET, KP_FUZZ_LOBBY,
	KP_FUZZ_TRANSFER, KP_FUZZ_TARGETS };

/* The target's name ("rep", ...), and the target of a name (-1 none). */
const char *kp_fuzz_name(int target);
int kp_fuzz_target(const char *name);
/* Whether the target needs the game's data. */
int kp_fuzz_needs_data(int target);
/* Starts what the target needs (the data, its games and their snapshots): 0, or -1 with the reason in why. */
int kp_fuzz_init(int target, char *why, size_t why_size);
/* One input: 0, or -1 with the reason in why when a property the target checks failed (a finding). */
int kp_fuzz_run(int target, const uint8_t *data, size_t size, char *why, size_t why_size);

#define KP_FUZZ_REASONS 16     /* the refusals' reasons told apart, at most (the rest counted as others) */
#define KP_FUZZ_REASON_SIZE 96
typedef struct {
	long inputs;
	long refused; /* games stopped by the game's own checks (kp_game_fail) */
	long loaded;  /* saves the loads took */
	int reasons;  /* the refusals by their reason, its digits as '#' */
	char reason[KP_FUZZ_REASONS][KP_FUZZ_REASON_SIZE];
	long reason_count[KP_FUZZ_REASONS];
	long reason_others; /* refusals of a reason past the first KP_FUZZ_REASONS */
} KpFuzzStats;
const KpFuzzStats *kp_fuzz_stats(void);

/* The seeds into dir/<target>/: the synthetic ones (a replay and a CHK of made-up bytes, commands' plain orders, the
 * network and lobby targets' sessions on the in-process LAN; the committed tests/fuzz/seeds) and, with_data and the data
 * present, the ones made from it (the campaign's and the scenarios' CHKs, the games' saves and replays, commands on the
 * games' own units: Blizzard's bytes, never committed).
 * The save target's game gives a save file and a native save at each of KP_FUZZ_SAVE_SEED_POINTS points. Their
 * count, or -1 with the reason (a seed not written, or the data present and its games not starting). */
#define KP_FUZZ_SAVE_SEED_POINTS 3
int kp_fuzz_write_seeds(const char *dir, int with_data, char *why, size_t why_size);

/* Releases the games and zeroes the counts (a process's end under LeakSanitizer, a test's). */
void kp_fuzz_free(void);

#ifdef __cplusplus
}
#endif

#endif
