/* standard.snp's UDP provider (kp_snp_udp.h; phase N, stage N2, atlas U01). */
#include "kp_snp_udp.h"

#include <stdlib.h>
#include <string.h>

static uint32_t now_of(const KpSnpUdp *p) { return p->io.now(p->io.ud); }

static void send_on(KpSnpUdp *p, int socket, const KpNetAddr *to, const uint8_t *data, size_t size)
{
	if (p->io.send) p->io.send(p->io.ud, socket, to, data, size);
}

static KpNetAddr broadcast_list(void) { return kp_net_addr(KP_NET_BROADCAST, KP_NET_LIST_PORT); }

/* The 20-byte header: checksum (later), length, type, 0, program id, version, mode. */
static void put_header(const KpSnpUdp *p, uint8_t *d, uint32_t size, uint32_t type, uint32_t mode)
{
	kp_net_put16(d, 0);
	kp_net_put16(d + 2, size);
	kp_net_put16(d + 4, type);
	kp_net_put16(d + 6, 0);
	kp_net_put32(d + 8, p->program);
	kp_net_put32(d + 12, p->version);
	kp_net_put32(d + 16, mode);
}

/* BroadcastGameListRequest 0x1d009370's datagram to `to`. */
static void send_request(KpSnpUdp *p, const KpNetAddr *to)
{
	uint8_t d[KP_SNP_HEADER];
	put_header(p, d, KP_SNP_HEADER, KP_SNP_REQUEST, 0);
	kp_net_checksum(d, sizeof d);
	send_on(p, KP_NET_SOCKET_LIST, to, d, sizeof d);
}

/* BroadcastGameListRequest 0x1d009370, and Koprulu's host by its address after it. */
static void broadcast_request(KpSnpUdp *p)
{
	KpNetAddr to = broadcast_list();
	send_request(p, &to);
	if (p->direct_on) send_request(p, &p->direct);
}

int kp_snp_udp_direct(KpSnpUdp *p, const KpNetAddr *host)
{
	p->direct_on = 0;
	memset(&p->direct, 0, sizeof p->direct);
	if (!host) return 1;
	if (kp_net_addr_port(host) == 0 || kp_net_addr_port(host) == 0xffff) return 0;
	p->direct = kp_net_addr(kp_net_addr_ip(host), kp_net_addr_port(host));
	p->direct_on = 1;
	send_request(p, &p->direct);
	return 1;
}

void kp_snp_udp_init(KpSnpUdp *p, const KpNetIo *io, uint32_t program, uint32_t version, uint32_t max_players)
{
	memset(p, 0, sizeof *p);
	p->io = *io;
	p->program = program;
	p->version = version;
	p->max_players = max_players < 256 ? max_players : 256;
	broadcast_request(p); /* the advertisement thread's first act */
}

static void free_games(KpSnpUdp *p)
{
	while (p->games) {
		KpSnpGame *g = p->games;
		p->games = g->next;
		free(g);
	}
}

void kp_snp_udp_free(KpSnpUdp *p)
{
	free_games(p);
	while (p->queue) {
		KpSnpDatagram *d = p->queue;
		p->queue = d->next;
		free(d);
	}
	p->queue_tail = NULL;
	p->queued = 0;
	p->advert_size = 0;
}

/* SStrCopy(dst, src, KP_SNP_STRING) bounded by the datagram's end: the characters copied, or -1 when the string does
 * not end within the datagram (the original reads on). */
static int copy_string(char *dst, const uint8_t *src, size_t avail)
{
	size_t n = 0;
	while (n < avail && src[n]) n++;
	if (n == avail) return -1;
	if (n > KP_SNP_STRING - 1) n = KP_SNP_STRING - 1;
	memcpy(dst, src, n);
	dst[n] = 0;
	return (int)n;
}

/* OnGameAdvertisement 0x1d0087a0 (remove: a stop). */
static void on_advertisement(KpSnpUdp *p, KpNetAddr from, const uint8_t *d, size_t size, int remove)
{
	KpSnpGame **at = &p->games, *g;
	uint32_t id = 0;
	int removed = 0, others = 0, n;
	size_t off;
	/* the game's address: the advertiser's with port 6112; Koprulu's host by its address at its port + 1 */
	uint32_t game_port = p->direct_on && kp_net_addr_eq(&from, &p->direct) ? kp_net_addr_port(&from) + 1u
		: (uint32_t)KP_NET_GAME_PORT;
	from.b[2] = (uint8_t)(game_port >> 8);
	from.b[3] = (uint8_t)game_port;
	while (*at) {
		if (kp_net_addr_eq(&(*at)->addr, &from)) {
			g = *at;
			*at = g->next;
			id = g->id;
			removed = 1;
			free(g);
		} else {
			others++;
			at = &(*at)->next;
		}
	}
	if (!removed) {
		if (++p->game_counter == 0) p->game_counter = 1;
		id = p->game_counter;
	}
	if (kp_net_get32(d + 8) != p->program || remove) return;
	/* the original lists every advertiser: its ids are given above all the same */
	if (others >= KP_SNP_GAMES) {
		p->crowded++;
		return;
	}
	if (!(g = (KpSnpGame *)calloc(1, sizeof *g))) return;
	g->id = id;
	g->mode = kp_net_get32(d + 16);
	g->addr = from;
	g->tick = now_of(p);
	off = KP_SNP_HEADER;
	if ((n = copy_string(g->name, d + off, size - off)) < 0) goto malformed;
	off += (size_t)n + 1;
	if ((n = copy_string(g->stats, d + off, size - off)) < 0) goto malformed;
	off += (size_t)n + 1;
	g->extra_size = (uint32_t)(size - off);
	memcpy(g->extra, d + off, g->extra_size);
	g->program = kp_net_get32(d + 8);
	g->version = kp_net_get32(d + 12);
	g->next = p->games;
	p->games = g;
	return;
malformed:
	p->malformed++;
	free(g);
}

/* SendAdvertisement 0x1d009300: the stored advertisement to `to` (NULL: broadcast). */
static void send_advertisement(KpSnpUdp *p, const KpNetAddr *to)
{
	KpNetAddr b = broadcast_list();
	if (p->advert_size) send_on(p, KP_NET_SOCKET_LIST, to ? to : &b, p->advert, p->advert_size);
}

int kp_snp_udp_input(KpSnpUdp *p, int socket, const KpNetAddr *from, const uint8_t *data, size_t size)
{
	KpNetAddr src = *from;
	memset(src.b + 8, 0, 8);
	if (socket == KP_NET_SOCKET_LIST) {
		uint32_t type;
		if (size > KP_SNP_LIST_DATAGRAM || size < KP_SNP_HEADER) return 0;
		if (kp_net_get16(data + 2) != size || kp_net_sum(data, size) != 0) return 0;
		type = kp_net_get16(data + 4);
		if (type == KP_SNP_ADVERTISEMENT || type == KP_SNP_STOP) on_advertisement(p, src, data, size, type == KP_SNP_STOP);
		else if (type == KP_SNP_REQUEST) send_advertisement(p, &src);
		return 0;
	}
	if (size > KP_SNP_DATAGRAM) {
		p->oversized++;
		return 0;
	}
	if (p->queued >= KP_SNP_QUEUE) {
		p->overflowed++;
		return 0;
	} else {
		KpSnpDatagram *d = (KpSnpDatagram *)malloc(sizeof *d);
		if (!d) return 0;
		d->next = NULL;
		d->from = src;
		d->size = (uint16_t)size;
		memcpy(d->data, data, size);
		if (p->queue_tail) p->queue_tail->next = d;
		else p->queue = d;
		p->queue_tail = d;
		p->queued++;
	}
	return 1;
}

int kp_snp_udp_receive(KpSnpUdp *p, KpNetAddr *from, uint8_t *out, uint32_t *size)
{
	KpSnpDatagram *d = p->queue;
	if (!d) return 0;
	p->queue = d->next;
	if (!p->queue) p->queue_tail = NULL;
	p->queued--;
	*from = d->from;
	memcpy(out, d->data, d->size);
	*size = d->size;
	free(d);
	return 1;
}

void kp_snp_udp_send(KpSnpUdp *p, int count, const KpNetAddr *const *to, const uint8_t *data, size_t size)
{
	int k;
	if (count <= 0 || !to || !data || !size) return;
	for (k = count - 1; k >= 0; k--) send_on(p, KP_NET_SOCKET_GAME, to[k], data, size);
}

int kp_snp_udp_advertise(KpSnpUdp *p, const char *name, const char *stats, uint32_t mode, const uint8_t *extra,
	size_t extra_size)
{
	size_t n, s, size;
	if (!name || !stats) return 0;
	n = strlen(name);
	s = strlen(stats);
	if (n > KP_SNP_STRING - 1) n = KP_SNP_STRING - 1;
	if (s > KP_SNP_STRING - 1) s = KP_SNP_STRING - 1;
	size = KP_SNP_HEADER + n + 1 + s + 1 + extra_size;
	if (size > sizeof p->advert) return 0;
	memset(p->advert, 0, sizeof p->advert);
	put_header(p, p->advert, (uint32_t)size, KP_SNP_ADVERTISEMENT, mode);
	memcpy(p->advert + KP_SNP_HEADER, name, n);
	memcpy(p->advert + KP_SNP_HEADER + n + 1, stats, s);
	if (extra_size) memcpy(p->advert + KP_SNP_HEADER + n + 1 + s + 1, extra, extra_size);
	kp_net_checksum(p->advert, size);
	p->advert_size = (uint32_t)size;
	send_advertisement(p, NULL);
	return 1;
}

void kp_snp_udp_stop_advertising(KpSnpUdp *p)
{
	if (!p->advert_size) return;
	kp_net_put16(p->advert + 4, KP_SNP_STOP);
	kp_net_checksum(p->advert, p->advert_size);
	send_advertisement(p, NULL);
	p->advert_size = 0;
}

const KpSnpGame *kp_snp_udp_lock_games(KpSnpUdp *p)
{
	KpSnpGame **at = &p->games;
	uint32_t now = now_of(p);
	while (*at) {
		if (now - (*at)->tick > KP_SNP_GAME_LIFETIME) {
			KpSnpGame *g = *at;
			*at = g->next;
			free(g);
		} else {
			at = &(*at)->next;
		}
	}
	return p->games;
}

void kp_snp_udp_unlock_games(KpSnpUdp *p)
{
	uint32_t now = now_of(p);
	if (now - p->last_request > KP_SNP_REQUEST_GAP) {
		p->last_request = now;
		broadcast_request(p);
	}
}

int kp_snp_udp_game_info(KpSnpUdp *p, uint32_t id, const char *name, KpSnpGame *out)
{
	const KpSnpGame *g;
	memset(out, 0, sizeof *out);
	if (!name || (id == 0 && !*name)) return 0;
	for (g = p->games; g; g = g->next) {
		if ((id == 0 || g->id == id) && (!*name || kp_net_casecmp(name, g->name) == 0)) {
			*out = *g;
			out->next = NULL;
			return 1;
		}
	}
	return 0;
}
