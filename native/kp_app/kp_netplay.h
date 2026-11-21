/*
 * The product's network play (phase N, stage N3): the providers the connection screen lists (Local Area Network
 * (UDP/IP)), and for the chosen one Storm's stack (kp_snet over kp_snp_udp) on the system's sockets (kp_net_sock), the
 * game's network code (KpNetGame) and the screens' adapter (KpNetUi): kp_ui's KpUiNet answered as StarCraft 1.17's
 * glue answers its screens, so a Koprulu seat lists, hosts and joins the lobbies of Koprulu and of the original.
 *
 * Nothing runs a thread but a host name's lookup: the flow pumps the sockets and Storm's timers each frame
 * (kp_netplay_pump), and a join waits inside SNetJoinGame as the original's glue does (the sockets read meanwhile).
 * TODO: pump Storm on a thread of its own while a mission loads (the plan's native network thread), so a long load does
 * not miss Storm's timeouts. Not needed yet (stage N6): a melee map's load holds the pump about 0.2 s on a Mac and
 * 0.3 s on a Galaxy Z Fold7, well inside Storm's one second.
 *
 * The lobby takes one message a pass (ReceiveChatRoomMessage 0x471860, as the original), and the flow makes one pass
 * a frame where the original's modal loop makes hundreds a second: a map's transfer goes as fast as the slower seat
 * draws (stage N6: 68 KB in under 15 s between a Mac and the Fold7 at 60 frames a second, about 55 s to the iOS
 * Simulator).
 *
 * A host by its address (stage N5): the join screen's Address (and --lan-host) gives the provider a host to ask for its
 * game besides the broadcast (kp_snp_udp_direct), for a game over the internet or a network without broadcasts; the
 * host forwards UDP ports 6111 and 6112 (its --lan-port and the next one) to itself. A name is looked up off the main
 * thread (kp_net_lookup_*) and its answer taken by kp_netplay_pump: the provider asks the host once it has its address,
 * and the Address popup waits for it (KpUiNet.set_address's -1, kp_ui_menus_address_found).
 *
 * TODO: the web has no UDP sockets and no network play (the user's decision of 2026-11-21: no relay for now; a relay
 * or WebRTC later, docs/phase-network.md).
 */
#ifndef KP_NETPLAY_H
#define KP_NETPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "kp_net_sock.h"
#include "kp_netgame.h"
#include "kp_netui.h"
#include "kp_snet.h"
#include "kp_snp_udp.h"
#include "kp_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_NETPLAY_UDP_NAME "Local Area Network (UDP/IP)" /* the UDP provider's name (standard.snp's, atlas U01) */

typedef struct {
	KpNetSockConfig sock;         /* the sockets: port 6111 on the LAN, or a map of Koprulu's games on one machine */
	const KpTemplates *templates; /* the game types (templates.lst) */
	KpTransferIo transfer;        /* the map transfer's files (kp_flow_transfer_io) */
	/* the Create Game screen's map of an entry index (KpNetUi.entry_map's, over ctx) */
	int (*entry_map)(void *ctx, KpUi *ui, int index, KpMapHeader *h, char *path, size_t size);
	/* its saved game's header (KpNetUi.entry_save's) and this seat's copy of a saved game (KpNetUi.find_save's), over
	 * ctx; NULL none */
	int (*entry_save)(void *ctx, KpUi *ui, int index, KpNetGameHeader *h);
	/* its replay entry's (KpNetUi.entry_replay's, over ctx); NULL none */
	int (*entry_replay)(void *ctx, KpUi *ui, int index, KpNetReplayEntry *out);
	int (*find_save)(void *ctx, uint32_t id, KpNetGameSave *out);
	/* the chat room's preview world (KpNetUi.preview's and preview_release's, over ctx); NULL none */
	int (*preview)(void *ctx, const char *path, const uint8_t *template_record, KpUiPreview *out);
	void (*preview_release)(void *ctx);
	void (*log)(void *ctx, const char *line); /* a line for the log, NULL none */
	/* the host by its address at the start ("" none: kp_net.h's form) and the player's new one, to keep (NULL none) */
	char host[KP_NET_HOST];
	void (*host_changed)(void *ctx, const char *text);
	/* the LAN provider's sockets opened (1) and closed (0): Android's Wi-Fi multicast lock, without which a phone
	 * may drop the broadcasts of the game list (NULL none) */
	void (*lan_lock)(void *ctx, int on);
	void *ctx;
} KpNetPlayConfig;

typedef struct KpNetPlay {
	KpNetPlayConfig config;
	KpNetUi nu;      /* nu.owner is the KpNetPlay */
	KpUiNet net;     /* the screens' hooks (ui->net) */
	KpNetSock *sock; /* the chosen provider's stack, NULL none */
	KpSnpUdp snp;
	int snp_on;
	KpSnet *snet;
	KpNetGame game;
	int game_on;
	int signalled;   /* a datagram arrived since Storm's last pass */
	uint32_t wake;   /* Storm's next timer pass, KP_SNET_INFINITE none */
	char host[KP_NET_HOST]; /* the host by its address ("" none) */
	KpNetLookup *host_lookup; /* host's address being looked up for the provider, NULL none */
	KpNetLookup *asked;       /* the Address popup's address (asked_text) being looked up, NULL none */
	char asked_text[KP_NET_HOST];
	int locked;      /* lan_lock(1) called */
	int sock_failed; /* the sockets failed: read no more until the provider is chosen again (logged, told once) */
} KpNetPlay;

/* p over ui: its hooks set as ui->net, no provider chosen yet. */
void kp_netplay_init(KpNetPlay *p, KpUi *ui, const KpNetPlayConfig *config);
/* A frame's network: a host's address that a lookup found given to the provider (and the Address popup told), what
 * arrived on the sockets handed to the provider, Storm's pass when it is due. */
void kp_netplay_pump(KpNetPlay *p);
/* AbortNetworkGame 0x474e60: the game left (the chat room's start the flow cannot play yet), the provider kept. */
void kp_netplay_leave(KpNetPlay *p);
/* The provider's stack, the lookups and the texts freed (ui->net is the caller's to clear). */
void kp_netplay_free(KpNetPlay *p);

#ifdef __cplusplus
}
#endif

#endif
