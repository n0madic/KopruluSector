/*
 * The product's network play (phase N, stage N3): the providers the connection screen lists (Local Area Network
 * (UDP/IP)), and for the chosen one Storm's stack (kp_snet over kp_snp_udp) on the system's sockets (kp_net_sock), the
 * game's network code (KpNetGame) and the screens' adapter (KpNetUi): kp_ui's KpUiNet answered as StarCraft 1.17's
 * glue answers its screens, so a Koprulu seat lists, hosts and joins the lobbies of Koprulu and of the original.
 *
 * Nothing runs a thread: the flow pumps the sockets and Storm's timers each frame (kp_netplay_pump), and a join waits
 * inside SNetJoinGame as the original's glue does (the sockets read meanwhile). The web has no UDP sockets: no
 * provider there until the relay of stage N5. TODO: pump Storm on a thread of its own while a mission loads (the
 * plan's native network thread), so a long load does not miss Storm's timeouts.
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
	int (*find_save)(void *ctx, uint32_t id, KpNetGameSave *out);
	void (*log)(void *ctx, const char *line); /* a line for the log, NULL none */
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
} KpNetPlay;

/* p over ui: its hooks set as ui->net, no provider chosen yet. */
void kp_netplay_init(KpNetPlay *p, KpUi *ui, const KpNetPlayConfig *config);
/* A frame's network: what arrived on the sockets handed to the provider, Storm's pass when it is due. */
void kp_netplay_pump(KpNetPlay *p);
/* AbortNetworkGame 0x474e60: the game left (the chat room's start the flow cannot play yet), the provider kept. */
void kp_netplay_leave(KpNetPlay *p);
/* The provider's stack and the texts freed (ui->net is the caller's to clear). */
void kp_netplay_free(KpNetPlay *p);

#ifdef __cplusplus
}
#endif

#endif
