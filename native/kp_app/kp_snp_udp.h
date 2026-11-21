/*
 * standard.snp's "Local Area Network (UDP/IP)" provider 'UDPN' (phase N, stage N2; atlas U01, the "UDP provider"
 * section of docs/formats/storm-udp.md) as a pure state machine: the game list on socket A (port 6111) and Storm's
 * datagrams on socket B (port 6112). Its threads are the transport's: what arrives is given to kp_snp_udp_input, what
 * it sends goes to KpNetIo.send. The slots Storm calls (SnpBind's table at 0x1d01b2e8) are the functions below.
 *
 * Where it differs from the original, only for inputs the original mishandles (none of them changes a datagram a
 * 1.17 peer sends or accepts):
 *   - a datagram on port 6112 above 504 bytes is dropped and counted; the original's receiving thread ends for good;
 *   - an advertisement whose strings do not end within it is ignored after the sender's entry is removed; the
 *     original reads past it or asks Storm for a negative allocation, which is fatal;
 *   - the receive queue keeps at most KP_SNP_QUEUE datagrams, the game list KP_SNP_GAMES advertisers (the
 *     original's have no limit: forged addresses grow them until its memory runs out); an advertiser beyond them still
 *     takes its id, so the games listed keep the original's ids;
 *   - names and stat strings are cut at 127 characters for the advertisement's length too (the original counts their
 *     whole length while it copies 127, a datagram its receivers misread); StarCraft's are far shorter;
 *   - the shutdown sends neither its wake datagrams (they woke its own blocked threads) nor the stop it fails to send.
 *
 * Koprulu's addition (phase N, stage N5): a host by its address (kp_snp_udp_direct), for a network that does not carry
 * the broadcasts (the internet, iOS without Apple's multicast entitlement). Each game-list request also goes to that
 * address, a datagram the original sends to 255.255.255.255:6111; the original's receiving thread answers a request to
 * whoever sent it (AdvertReceiveThread 0x1d0089d0), so a 1.17 host lists its game too. The game of an advertisement from
 * that address is at its port + 1 (6112 for the default 6111: the original's), where a Koprulu host on --lan-port
 * PORT binds socket B.
 */
#ifndef KP_SNP_UDP_H
#define KP_SNP_UDP_H

#include <stddef.h>
#include <stdint.h>

#include "kp_net.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_SNP_UDP_ID 0x5544504eu /* 'UDPN' */
#define KP_SNP_DATAGRAM 504       /* the most a port-6112 thread reads (recvfrom's buffer) */
#define KP_SNP_LIST_DATAGRAM 532  /* the port-6111 thread's buffer and the advertisement's, 0x214 */
#define KP_SNP_HEADER 20          /* a game-list datagram's header */
#define KP_SNP_STRING 128         /* SStrCopy's limit for the name and the stat string (127 characters) */
#define KP_SNP_GAME_LIFETIME 10000 /* ms: ExpireGames 0x1d009430 at each lock of the list */
#define KP_SNP_REQUEST_GAP 400     /* ms: UdpUnlockGameList's least gap between requests (strictly more) */
#define KP_SNP_ENUM_HINT 500       /* UdpUnlockGameList's hint, SNetEnumGamesEx's *out */
#define KP_SNP_QUEUE 4096          /* the received datagrams kept for Storm, at most */
#define KP_SNP_GAMES 256           /* the listed games, at most */

enum { KP_SNP_ADVERTISEMENT = 0, KP_SNP_STOP = 1, KP_SNP_REQUEST = 2 };

/* A listed game, the record Storm copies (0x13C bytes in the original): its id (local to this receiver), mode bits,
 * the advertiser's address with port 6112, the receipt time, name, stat string, the bytes after them (Storm's
 * 12-byte player record {u32 12, u32 players, u32 max players}) and the advertised program id and version. The game
 * type is always 0 over UDP. */
typedef struct KpSnpGame {
	struct KpSnpGame *next;
	uint32_t id, mode;
	KpNetAddr addr;
	uint32_t tick;
	char name[KP_SNP_STRING], stats[KP_SNP_STRING];
	uint8_t extra[KP_SNP_LIST_DATAGRAM];
	uint32_t extra_size;
	uint32_t program, version;
} KpSnpGame;

typedef struct KpSnpDatagram {
	struct KpSnpDatagram *next;
	KpNetAddr from;
	uint16_t size;
	uint8_t data[KP_SNP_DATAGRAM];
} KpSnpDatagram;

typedef struct {
	KpNetIo io;
	uint32_t program, version; /* program data +0xC and +0x10: 'STAR' or 'SEXP', 0xD5 */
	uint32_t max_players;      /* min(program data +0x18, 256) */
	uint8_t advert[KP_SNP_LIST_DATAGRAM];
	uint32_t advert_size;      /* 0: not advertising */
	KpSnpGame *games;          /* most recently heard first */
	uint32_t game_counter;     /* 0x1d01c77c */
	uint32_t last_request;     /* 0x1d01c7d0, from 0 */
	KpSnpDatagram *queue, *queue_tail;
	int queued;
	long oversized, overflowed, crowded, malformed; /* datagrams dropped where the original fails (above) */
	KpNetAddr direct;          /* Koprulu's host by its address (its socket A), when direct_on */
	int direct_on;
} KpSnpUdp;

/* UdpInitialize 0x1d007fa0 (the sockets are the transport's): the provider's program and version, its maximum players,
 * and the advertisement thread's first game-list request. */
void kp_snp_udp_init(KpSnpUdp *p, const KpNetIo *io, uint32_t program, uint32_t version, uint32_t max_players);
/* UdpDestroy 0x1d007b40: everything freed, nothing sent. */
void kp_snp_udp_free(KpSnpUdp *p);
/* A datagram that arrived on socket A or B from `from` (its bytes 8-15 are zeroed as the threads do): A's are
 * answered or filed at once (AdvertReceiveThread 0x1d0089d0), B's queued for Storm (GameReceiveThread 0x1d008b80).
 * Returns 1 when Storm's pump should be signalled (a datagram was queued). */
int kp_snp_udp_input(KpSnpUdp *p, int socket, const KpNetAddr *from, const uint8_t *data, size_t size);

/* --- the slots Storm calls --- */

/* UdpReceive 0x1d0082b0: the oldest queued datagram into *from and out (KP_SNP_DATAGRAM bytes), its size in *size;
 * 0 when the queue is empty. */
int kp_snp_udp_receive(KpSnpUdp *p, KpNetAddr *from, uint8_t *out, uint32_t *size);
/* UdpSend 0x1d008400: one datagram from socket B to each address, the last first. */
void kp_snp_udp_send(KpSnpUdp *p, int count, const KpNetAddr *const *to, const uint8_t *data, size_t size);
/* UdpStartAdvertisingGame 0x1d0077c0: the advertisement built (name, stat string, the extra bytes) and broadcast; 0
 * for a NULL string. */
int kp_snp_udp_advertise(KpSnpUdp *p, const char *name, const char *stats, uint32_t mode, const uint8_t *extra,
	size_t extra_size);
/* UdpStopAdvertisingGame 0x1d007a50: the stop broadcast and the advertisement dropped (nothing without one). */
void kp_snp_udp_stop_advertising(KpSnpUdp *p);
/* UdpLockGameList 0x1d008270: the entries older than KP_SNP_GAME_LIFETIME dropped; the list's head. */
const KpSnpGame *kp_snp_udp_lock_games(KpSnpUdp *p);
/* UdpUnlockGameList 0x1d007930: a game-list request broadcast when more than KP_SNP_REQUEST_GAP ms passed since the
 * last one sent here. */
void kp_snp_udp_unlock_games(KpSnpUdp *p);
/* Koprulu's host by its address (above): `host` is its socket A (a port of 1..65534), NULL none. A game-list request
 * goes to it at once and with every request after; 0 for a port that has no port above it. */
int kp_snp_udp_direct(KpSnpUdp *p, const KpNetAddr *host);
/* UdpGetGameInfo 0x1d007ed0: the first listed game with that id (0: any) and that name (empty: any, else compared
 * without case) into *out; 0 when there is none. */
int kp_snp_udp_game_info(KpSnpUdp *p, uint32_t id, const char *name, KpSnpGame *out);

#ifdef __cplusplus
}
#endif

#endif
