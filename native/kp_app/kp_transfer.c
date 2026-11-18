/* The lobby's map transfer (kp_transfer.h; phase N, stage N3, atlas P13 and P14). */
#include "kp_transfer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_kpf.h"
#include "kp_net.h"

uint32_t kp_transfer_crc(const uint8_t *data, size_t size) { return ~kp_crc32(data, size); }

int kp_transfer_extension_ok(const char *name)
{
	const char *dot = strrchr(name, '.');
	if (!dot) return 0;
	dot++;
	return kp_net_casecmp(dot, "scx") == 0 || kp_net_casecmp(dot, "scm") == 0 || kp_net_casecmp(dot, "rep") == 0;
}

/* IsInvalidFileName 0x408300's reserved names (0x5025b8), in its order */
static const char *const RESERVED[] = { "nul", "con", "prn", "aux", "conin$", "conout$", "clock$", "com1", "com2",
	"com3", "com4", "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7",
	"lpt8", "lpt9" };
/* rez\network.tbl string 17, as shipped */
static const char FORBIDDEN[] = "*\\/:><|&+^?\"";

int kp_transfer_name_valid(const char *name, size_t limit)
{
	size_t n, len, k;
	const char *dot;
	/* 0x476e20(name, limit, limit, 0, 0): a NUL within limit bytes, no byte 1..31 */
	for (n = 0; n < limit && name[n]; n++)
		if ((unsigned char)name[n] < 0x20) return 0;
	if (n == limit || n == 0) return 0;
	/* the forbidden characters anywhere, ':' included (no drive prefix is skipped: the header's list) */
	if (strpbrk(name, FORBIDDEN)) return 0;
	dot = strchr(name, '.');
	len = dot ? (size_t)(dot - name) : n;
	for (k = 0; k < sizeof RESERVED / sizeof RESERVED[0]; k++) {
		size_t r = strlen(RESERVED[k]);
		if (kp_net_ncasecmp(name, RESERVED[k], len > r ? len : r) == 0) return 0;
	}
	return 1;
}

static void send_packet(KpTransfer *t, int storm, const uint8_t *p, size_t size)
{
	if (t->open && t->io.send) t->io.send(t->io.ud, storm, p, size);
}

static void free_offer(KpTransferOffer *o) { free(o); }

static void free_active(KpTransferActive *a)
{
	if (!a) return;
	free(a->buffer);
	free(a);
}

static void remove_active(KpTransfer *t, int k)
{
	free_active(t->active[k]);
	memmove(t->active + k, t->active + k + 1, (size_t)(t->active_count - k - 1) * sizeof t->active[0]);
	t->active_count--;
}

static void remove_offer(KpTransfer *t, const KpTransferOffer *o)
{
	int k;
	for (k = 0; k < t->offer_count; k++) {
		if (t->offers[k] != o) continue;
		free_offer(t->offers[k]);
		memmove(t->offers + k, t->offers + k + 1, (size_t)(t->offer_count - k - 1) * sizeof t->offers[0]);
		t->offer_count--;
		return;
	}
}

static void free_all(KpTransfer *t)
{
	while (t->offer_count > 0) remove_offer(t, t->offers[0]);
	while (t->active_count > 0) remove_active(t, 0);
}

void kp_transfer_begin(KpTransfer *t, const KpTransferIo *io, int local, uint32_t capacity)
{
	free_all(t);
	memset(t, 0, sizeof *t);
	t->io = *io;
	t->open = 1;
	t->local = (uint8_t)local;
	t->next_id = 0;
	t->ack_chunk = capacity - 9;
}

void kp_transfer_reset(KpTransfer *t)
{
	free_all(t);
	t->open = 0;
}

/* A path's file name: after its last '\\' (the original's), or the host's '/' */
static const char *file_name(const char *path)
{
	const char *name = path, *c;
	for (c = path; *c; c++)
		if (*c == '\\' || *c == '/') name = c + 1;
	return name;
}

KpTransferOffer *kp_transfer_offer(KpTransfer *t, const char *path)
{
	const char *name = file_name(path);
	uint8_t *data;
	size_t size = 0;
	KpTransferOffer *o;
	if (strlen(name) >= KP_TRANSFER_NAME) return NULL;
	if (!kp_transfer_extension_ok(path) || t->next_id == 0xff || t->offer_count >= KP_TRANSFER_OFFERS) return NULL;
	if (!t->io.read || !(data = t->io.read(t->io.ud, path, &size))) return NULL;
	if (size == 0 || size > KP_TRANSFER_MAX_SIZE) {
		free(data);
		return NULL;
	}
	if (!(o = (KpTransferOffer *)calloc(1, sizeof *o))) {
		free(data);
		return NULL;
	}
	o->crc = kp_transfer_crc(data, size);
	o->size = (uint32_t)size;
	free(data);
	o->id = t->next_id++;
	snprintf(o->path, sizeof o->path, "%s", path);
	memmove(t->offers + 1, t->offers, (size_t)t->offer_count * sizeof t->offers[0]);
	t->offers[0] = o;
	t->offer_count++;
	return o;
}

/* SendTransferCancel 0x441890 */
static void send_cancel(KpTransfer *t, int storm, uint8_t id)
{
	uint8_t p[2] = { KP_TRANSFER_CANCEL, id };
	send_packet(t, storm, p, sizeof p);
}

void kp_transfer_cancel(KpTransfer *t, KpTransferOffer *o)
{
	int k;
	if (!o) return;
	for (k = 0; k < o->peer_count; k++) send_cancel(t, o->peers[k].storm, o->id);
	remove_offer(t, o);
}

/* SendFileOffer 0x4416a0 */
static void send_offer(KpTransfer *t, int storm, const KpTransferOffer *o)
{
	uint8_t p[44];
	const char *name = file_name(o->path);
	size_t n;
	n = strlen(name);
	if (n + 1 > KP_TRANSFER_NAME) return;
	p[0] = KP_TRANSFER_OFFER;
	p[1] = o->id;
	kp_net_put32(p + 2, o->size);
	kp_net_put32(p + 6, o->crc);
	memcpy(p + 10, name, n + 1);
	send_packet(t, storm, p, n + 11);
}

void kp_transfer_add_peer(KpTransfer *t, KpTransferOffer *o, int storm)
{
	int k;
	for (k = 0; k < o->peer_count; k++)
		if (o->peers[k].storm == (uint8_t)storm) return;
	if (o->peer_count >= KP_TRANSFER_PEERS) return; /* Storm IDs 0..7: never reached */
	memmove(o->peers + 1, o->peers, (size_t)o->peer_count * sizeof o->peers[0]);
	o->peers[0].storm = (uint8_t)storm;
	o->peers[0].state = KP_TRANSFER_OFFER_SENT;
	o->peers[0].count = 0;
	o->peer_count++;
	send_offer(t, storm, o);
}

void kp_transfer_remove_peer(KpTransferOffer *o, int storm)
{
	int k;
	for (k = 0; k < o->peer_count; k++) {
		if (o->peers[k].storm != (uint8_t)storm) continue;
		memmove(o->peers + k, o->peers + k + 1, (size_t)(o->peer_count - k - 1) * sizeof o->peers[0]);
		o->peer_count--;
		return;
	}
}

void kp_transfer_set_pending(KpTransferOffer *o, uint32_t bits) { o->pending |= bits; }
void kp_transfer_clear_pending(KpTransferOffer *o, uint32_t bits) { o->pending &= ~bits; }

int kp_transfer_all_have(const KpTransferOffer *o)
{
	int k;
	if (o->pending) return 0;
	for (k = 0; k < o->peer_count; k++)
		if (o->peers[k].state != KP_TRANSFER_EXISTS || o->peers[k].count != o->size) return 0;
	return 1;
}

int kp_transfer_progress(const KpTransfer *t, uint8_t *pct)
{
	uint32_t s = 0, r = 0;
	int k, failed = 0;
	for (k = 0; k < t->active_count; k++) {
		s += t->active[k]->size;
		r += t->active[k]->received;
		if (t->active[k]->state == KP_TRANSFER_FAILED) failed = 1;
	}
	/* 100 * received overflows 32 bits from 43 MB on (16 transfers of up to 16 MB), so the product is 64-bit */
	*pct = (uint8_t)(s ? (uint64_t)r * 100u / s : 0u);
	return !failed;
}

/* SendTransferStatus 0x440e30 */
static void send_status(KpTransfer *t, const KpTransferActive *a)
{
	uint8_t p[8];
	p[0] = KP_TRANSFER_STATUS;
	p[1] = a->id;
	p[2] = a->origin;
	p[3] = a->state;
	kp_net_put32(p + 4, a->received);
	send_packet(t, a->origin, p, sizeof p);
}

void kp_transfer_abort_peer(KpTransfer *t, int storm)
{
	int k;
	for (k = 0; k < t->active_count; k++) {
		if (t->active[k]->origin == (uint8_t)storm) {
			remove_active(t, k);
			break;
		}
	}
	for (k = 0; k < t->active_count; k++) {
		KpTransferActive *a = t->active[k];
		if ((a->state & KP_TRANSFER_RECEIVING) && a->uploader == (uint8_t)storm) a->state &= (uint8_t)~KP_TRANSFER_RECEIVING;
		if ((a->state & KP_TRANSFER_UPLOADING) && a->target == (uint8_t)storm) a->state &= (uint8_t)~KP_TRANSFER_UPLOADING;
		send_status(t, a);
	}
}

static KpTransferOffer *find_offer(KpTransfer *t, uint8_t id)
{
	int k;
	for (k = 0; k < t->offer_count; k++)
		if (t->offers[k]->id == id) return t->offers[k];
	return NULL;
}

static int find_active(const KpTransfer *t, uint8_t id, uint8_t origin)
{
	int k;
	for (k = 0; k < t->active_count; k++)
		if (t->active[k]->id == id && t->active[k]->origin == origin) return k;
	return -1;
}

/* SendUploadRedirect 0x441850 */
static void send_upload(KpTransfer *t, int uploader, uint8_t id, const KpTransferPeer *receiver)
{
	uint8_t p[7];
	p[0] = KP_TRANSFER_UPLOAD;
	p[1] = id;
	p[2] = receiver->storm;
	kp_net_put32(p + 3, receiver->count);
	send_packet(t, uploader, p, sizeof p);
}

/* RedirectDuplicateDownloads 0x440d40 */
static void pair_downloads(KpTransfer *t, KpTransferOffer *o)
{
	for (;;) {
		KpTransferPeer *r = NULL, *u = NULL;
		int k;
		for (k = 0; k < o->peer_count && !(r && u); k++) {
			KpTransferPeer *p = &o->peers[k];
			if (p->state != KP_TRANSFER_EXISTS) continue;
			if (p->count != o->size) {
				if (!r) r = p;
			} else if (!u) {
				u = p;
			}
		}
		if (!r || !u) return;
		send_upload(t, u->storm, o->id, r);
		r->state = 4;
		u->state = 2;
	}
}

/* HandleTransferStatusPacket 0x440db0 */
static int on_status(KpTransfer *t, const uint8_t *p, size_t size, int sender)
{
	KpTransferOffer *o;
	int k;
	if (size != 8 || p[2] != t->local) return 0;
	if (!(o = find_offer(t, p[1]))) return 0;
	for (k = 0; k < o->peer_count; k++) {
		if (o->peers[k].storm != (uint8_t)sender) continue;
		o->peers[k].state = p[3];
		o->peers[k].count = kp_net_get32(p + 4);
		pair_downloads(t, o);
		return 1;
	}
	return 0;
}

static void set_map_path(KpTransfer *t, const char *path)
{
	snprintf(t->map_path, sizeof t->map_path, "%s", path);
	t->map_ready = 1;
}

/* FindLocalCopy 0x441080 */
static int find_copy(KpTransfer *t, const KpTransferActive *a, char *out, size_t out_size)
{
	if (a->origin == t->local) {
		const KpTransferOffer *o = find_offer(t, a->id);
		if (!o) return 0;
		snprintf(out, out_size, "%s", o->path);
		return 1;
	}
	if (!t->io.find || !t->io.find(t->io.ud, a->name, a->size, a->crc, out, out_size)) return 0;
	set_map_path(t, out);
	return 1;
}

/* HandleFileOfferPacket 0x441150 */
static int on_offer(KpTransfer *t, const uint8_t *p, size_t size, int sender)
{
	KpTransferActive *a;
	char name[KP_TRANSFER_NAME], path[KP_TRANSFER_PATH];
	uint32_t file_size;
	size_t limit, n;
	if (size < 10) return 0;
	if ((file_size = kp_net_get32(p + 2)) == 0) return 0;
	/* ValidateNetString(packet + 10, 0x20, [length - 0x4a]): a NUL within min(32, length - 74 unsigned), no byte
	 * 1..31; Koprulu also within the packet */
	limit = (size_t)(uint32_t)(size - 0x4a);
	if (limit > KP_TRANSFER_NAME) limit = KP_TRANSFER_NAME;
	if (limit > size - 10) limit = size - 10;
	for (n = 0; n < limit && p[10 + n]; n++)
		if (p[10 + n] < 0x20) return 0;
	if (n == limit) return 0;
	memcpy(name, p + 10, n + 1);
	if (!kp_transfer_extension_ok(name) || !kp_transfer_name_valid(name, n + 1)) return 0;
	if (file_size > KP_TRANSFER_MAX_SIZE || t->active_count >= KP_TRANSFER_MAX_ACTIVE) {
		t->refused++;
		return 0;
	}
	if (!(a = (KpTransferActive *)calloc(1, sizeof *a))) return 0;
	if (!(a->buffer = (uint8_t *)calloc(1, file_size))) {
		free(a);
		return 0;
	}
	a->id = p[1];
	a->origin = (uint8_t)sender;
	a->size = file_size;
	a->crc = kp_net_get32(p + 6);
	memcpy(a->name, name, n + 1);
	a->state = KP_TRANSFER_EXISTS;
	memmove(t->active + 1, t->active, (size_t)t->active_count * sizeof t->active[0]);
	t->active[0] = a;
	t->active_count++;
	if (find_copy(t, a, path, sizeof path)) {
		size_t got = 0;
		uint8_t *data = t->io.read ? t->io.read(t->io.ud, path, &got) : NULL;
		if (data && got != a->size) {
			/* only this seat's own file can differ from the offer's size (an offer from its own Storm ID) */
			t->refused++;
			free(data);
			data = NULL;
		}
		if (!data) {
			a->state = KP_TRANSFER_FAILED;
			return 0;
		}
		memcpy(a->buffer, data, got);
		free(data);
		a->received = (uint32_t)got;
	}
	send_status(t, a);
	return 1;
}

/* SendFileChunk 0x441720: the data bytes sent */
static uint32_t send_chunk(KpTransfer *t, KpTransferActive *a, uint32_t max)
{
	uint8_t p[9 + KP_TRANSFER_CHUNK];
	uint32_t n = max < KP_TRANSFER_CHUNK ? max : KP_TRANSFER_CHUNK, offset = a->next;
	if (!t->open || !t->io.send) return 0;
	if (offset >= a->size) return 0;
	if (offset + n > a->size) n = a->size - offset;
	if (offset + n > a->received) {
		a->state &= (uint8_t)~KP_TRANSFER_UPLOADING;
		offset = a->received;
		n = 0;
	}
	p[0] = KP_TRANSFER_DATA;
	p[1] = a->id;
	p[2] = a->origin;
	kp_net_put32(p + 3, offset);
	kp_net_put16(p + 7, n);
	if (n) memcpy(p + 9, a->buffer + offset, n);
	send_packet(t, a->target, p, n + 9);
	return n;
}

/* HandleUploadRequestPacket 0x4415e0 */
static int on_upload(KpTransfer *t, const uint8_t *p, size_t size, int sender)
{
	KpTransferActive *a;
	int k, i;
	if (size != 7) return 0;
	if ((k = find_active(t, p[1], (uint8_t)sender)) < 0) return 0;
	if (p[2] >= KP_TRANSFER_PEERS) {
		t->refused++;
		return 0;
	}
	a = t->active[k];
	if (a->state & KP_TRANSFER_UPLOADING) return 0;
	a->state |= KP_TRANSFER_UPLOADING;
	a->next = kp_net_get32(p + 3);
	a->target = p[2];
	for (i = 0; i < KP_TRANSFER_START_CHUNKS; i++) a->next += send_chunk(t, a, KP_TRANSFER_START_BYTES);
	return 1;
}

/* HandleTransferCancelPacket 0x441660 */
static int on_cancel(KpTransfer *t, const uint8_t *p, size_t size, int sender)
{
	int k;
	if (size != 2) return 0;
	if ((k = find_active(t, p[1], (uint8_t)sender)) < 0) return 0;
	remove_active(t, k);
	return 1;
}

/* SendUploadStop 0x441820 (also the inline stop of 0x4413b0) */
static void send_stop(KpTransfer *t, int storm, const KpTransferActive *a)
{
	uint8_t p[3] = { KP_TRANSFER_STOP, a->id, a->origin };
	send_packet(t, storm, p, sizeof p);
}

/* SendChunkAck 0x4417e0 */
static void send_ack(KpTransfer *t, const KpTransferActive *a)
{
	uint8_t p[7];
	p[0] = KP_TRANSFER_ACK;
	p[1] = a->id;
	p[2] = a->origin;
	kp_net_put32(p + 3, a->received);
	send_packet(t, a->uploader, p, sizeof p);
}

/* HandleFileDataPacket 0x4413b0 */
static int on_data(KpTransfer *t, const uint8_t *p, size_t size, int sender)
{
	KpTransferActive *a;
	uint32_t offset, n;
	int k;
	if (size < 9) return 0;
	n = kp_net_get16(p + 7);
	if (size != n + 9) return 0;
	if ((k = find_active(t, p[1], p[2])) < 0) return 0;
	a = t->active[k];
	offset = kp_net_get32(p + 3);
	if (offset == a->size || offset + n > a->size || offset + n < offset) {
		send_stop(t, sender, a);
		send_status(t, a);
		return 0;
	}
	if (offset != a->received) {
		send_stop(t, sender, a);
		send_status(t, a);
		return 0;
	}
	a->state |= KP_TRANSFER_RECEIVING;
	a->uploader = (uint8_t)sender;
	if (n) memcpy(a->buffer + offset, p + 9, n);
	a->received += n;
	send_ack(t, a);
	if (a->received == a->size) {
		char path[KP_TRANSFER_PATH];
		if (kp_transfer_crc(a->buffer, a->size) != a->crc) {
			a->state &= (uint8_t)~KP_TRANSFER_RECEIVING;
			a->received = 0;
			send_status(t, a);
			return 1;
		}
		/* SaveDownloadedFile 0x4418c0: the name was checked on the offer (kp_transfer_name_valid) */
		if (!t->io.save || !t->io.save(t->io.ud, a->name, a->buffer, a->size, path, sizeof path)) {
			a->state = KP_TRANSFER_FAILED;
			return 0;
		}
		set_map_path(t, path);
		a->state &= (uint8_t)~KP_TRANSFER_RECEIVING;
		send_status(t, a);
	}
	return 1;
}

/* HandleChunkAckPacket 0x441520 */
static int on_ack(KpTransfer *t, const uint8_t *p, size_t size)
{
	KpTransferActive *a;
	int k;
	if (size != 7) return 0;
	if ((k = find_active(t, p[1], p[2])) < 0) return 0;
	a = t->active[k];
	if (!(a->state & KP_TRANSFER_UPLOADING)) return 0;
	if (kp_net_get32(p + 3) < a->size) {
		a->next += send_chunk(t, a, t->ack_chunk);
		return 1;
	}
	a->state &= (uint8_t)~KP_TRANSFER_UPLOADING;
	send_status(t, a);
	return 1;
}

/* HandleSendStopPacket 0x4415a0 */
static int on_stop(KpTransfer *t, const uint8_t *p, size_t size)
{
	KpTransferActive *a;
	int k;
	if (size != 3) return 0;
	if ((k = find_active(t, p[1], p[2])) < 0) return 0;
	a = t->active[k];
	a->state &= (uint8_t)~KP_TRANSFER_UPLOADING;
	send_status(t, a);
	return 1;
}

int kp_transfer_dispatch(KpTransfer *t, const uint8_t *packet, size_t size, int sender)
{
	if (!packet || size == 0) return 0;
	switch (packet[0]) {
	case KP_TRANSFER_STATUS: return on_status(t, packet, size, sender);
	case KP_TRANSFER_OFFER: return on_offer(t, packet, size, sender);
	case KP_TRANSFER_UPLOAD: return on_upload(t, packet, size, sender);
	case KP_TRANSFER_CANCEL: return on_cancel(t, packet, size, sender);
	case KP_TRANSFER_DATA: return on_data(t, packet, size, sender);
	case KP_TRANSFER_ACK: return on_ack(t, packet, size);
	case KP_TRANSFER_STOP: return on_stop(t, packet, size);
	default: return 0;
	}
}
