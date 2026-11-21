/*
 * The multiplayer screens over the game's network code (phase N, stage N3): KpUiNet's game and lobby hooks
 * (kp_ui.h; the join and create screens, the chat room of kp_ui_lobby.c) answered by a KpNetGame (kp_netgame.h), and
 * KpNetGame's screen hooks (its lines, refreshes, leave reasons, countdown, key draws) forwarded to the chat room.
 * Names, lines and texts cross as UTF-8 on the screens' side and as the original's bytes on the network's.
 *
 * The owner (the product's flow, the screens' oracle) owns the provider: it fills KpUiNet's provider hooks itself
 * (providers, select_provider, reset_provider, shutdown), sets game once a provider's KpNetGame is initialized, and
 * points KpUiNet.host at its KpNetUi, which every hook here casts back.
 */
#ifndef KP_NETUI_H
#define KP_NETUI_H

#include <stddef.h>
#include <stdint.h>

#include "kp_netgame.h"
#include "kp_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_NETUI_TEXTS 0x100 /* network.tbl's strings the game asks (KP_NETTBL_*) */

/* A replay entry of the Create Game screen as a hosted replay's game takes it (CreateGameFromEntry 0x45bfc0's kind
 * 0x80, LoadReplayEntryInfo 0x45d250): its game header (the header's +0x14, its replay byte set), the 0x48 record its
 * start information carries (GetReplayGameSeedRecord 0x480540), its scenario's header and the file the transfer
 * sends. */
typedef struct {
	uint8_t game[KP_NETGAME_HEADER];
	uint8_t seed_record[KP_NETGAME_START_INFO];
	KpMapHeader map;
	char path[KP_TRANSFER_PATH];
} KpNetReplayEntry;

typedef struct KpNetUi {
	KpNetGame *game; /* the selected provider's game (NULL: none, every hook refuses) */
	KpUi *ui;        /* the screens the game's hooks forward to */
	/* The owner's: the map of the Create Game screen's entry index (its header and the path the transfer reads): 1, or
	 * 0 when it cannot be read */
	int (*entry_map)(void *owner, KpUi *ui, int index, KpMapHeader *h, char *path, size_t size);
	/* The owner's: the game header of the Create Game screen's saved game entry index (the game it resumes): 1, or 0
	 * when it cannot be read (NULL: none) */
	int (*entry_save)(void *owner, KpUi *ui, int index, KpNetGameHeader *h);
	/* The owner's: the replay of the Create Game screen's entry index: 1, or 0 when it cannot be played (NULL: none) */
	int (*entry_replay)(void *owner, KpUi *ui, int index, KpNetReplayEntry *out);
	/* The owner's KpNetGameHooks.find_save: this seat's copy of the saved game id (NULL: none) */
	int (*find_save)(void *owner, uint32_t id, KpNetGameSave *out);
	/* The owner's: the chat room's preview world (KpUiNet.lobby_preview, kp_preview.h) of the lobby's map at path under
	 * the game's template record: 1 with it in *out until preview_release, 0 when the map does not load (the screens
	 * report network.tbl 0x61); NULL: no preview world */
	int (*preview)(void *owner, const char *path, const uint8_t *template_record, KpUiPreview *out);
	void (*preview_release)(void *owner);
	/* time(NULL), the seed the host's start information carries (NULL: the system's) */
	uint32_t (*time)(void *owner);
	void *owner;
	char *texts[KP_NETUI_TEXTS]; /* network.tbl's strings as the original's bytes, converted at their first use */
} KpNetUi;

/* KpUiNet's game and lobby hooks (games .. lobby_password) and host over n; the provider hooks are left as they are. */
void kp_netui_hooks(KpNetUi *n, KpUiNet *net);
/* KpNetGameHooks over n: the clock (the screens' ui->now), time, network.tbl's texts, the errors (the screens'
 * deferred report), the chat room's hooks and the owner's saved games. */
KpNetGameHooks kp_netui_game_hooks(KpNetUi *n);
/* The texts freed (the game is the owner's). */
void kp_netui_free(KpNetUi *n);

/* The lobby as the chat room reads it (KpUiNet.lobby_view's answer over a game). */
void kp_netui_view(const KpNetGame *ng, KpUiLobby *out);

#ifdef __cplusplus
}
#endif

#endif
