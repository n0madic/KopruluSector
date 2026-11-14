/* StarCraft 1.17 replays (kp_rep.h; docs/formats/rep.md). */
#include "kp_rep.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pklib/pklib.h"

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void wr32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

uint32_t kp_rep_crc(const uint8_t *data, size_t size) { return ~kp_crc32(data, size); }

/* --- PKWARE DCL through pklib, with the original's callbacks (0x413750, 0x413790) --- */

typedef struct {
	const uint8_t *in;
	size_t in_size, in_pos;
	uint8_t *out;
	size_t out_cap, out_n; /* out_n may pass out_cap: the write callback still counts what it drops */
} Pipe;

static unsigned int pipe_read(char *buf, unsigned int *size, void *param)
{
	Pipe *p = (Pipe *)param;
	size_t n = p->in_size - p->in_pos;
	if (n > *size) n = *size;
	memcpy(buf, p->in + p->in_pos, n);
	p->in_pos += n;
	return (unsigned int)n;
}

static void pipe_write(char *buf, unsigned int *size, void *param)
{
	Pipe *p = (Pipe *)param;
	if (p->out_n + *size <= p->out_cap) memcpy(p->out + p->out_n, buf, *size);
	p->out_n += *size;
}

size_t kp_rep_work_size(void) { return CMP_BUFFER_SIZE; }

size_t kp_rep_chunk(const uint8_t *in, size_t n, uint8_t *scratch, void *work, const uint8_t **out)
{
	Pipe p = {in, n, 0, scratch, n, 0};
	unsigned int type = CMP_BINARY, dict = CMP_IMPLODE_DICT_SIZE1;
	memset(work, 0, CMP_BUFFER_SIZE); /* implode's matcher reads window bytes it never wrote: same input, same output */
	if (implode(pipe_read, pipe_write, (char *)work, &p, &type, &dict) == CMP_NO_ERROR && p.out_n < n) {
		*out = scratch;
		return p.out_n;
	}
	*out = in;
	return n;
}

static int grow(uint8_t **buf, size_t *cap, size_t need)
{
	size_t c = *cap ? *cap : 1024;
	uint8_t *g;
	if (need <= *cap) return 0;
	while (c < need) c *= 2;
	g = (uint8_t *)realloc(*buf, c);
	if (!g) return -1;
	*buf = g;
	*cap = c;
	return 0;
}

static int append(uint8_t **buf, size_t *len, size_t *cap, const void *p, size_t n)
{
	if (grow(buf, cap, *len + n) != 0) return -1;
	if (n) memcpy(*buf + *len, p, n);
	*len += n;
	return 0;
}

static int append32(uint8_t **buf, size_t *len, size_t *cap, uint32_t v)
{
	uint8_t b[4];
	wr32(b, v);
	return append(buf, len, cap, b, 4);
}

int kp_rep_block_write(uint8_t **buf, size_t *len, size_t *cap, const uint8_t *in, size_t n)
{
	size_t chunks = (n + KP_REP_CHUNK - 1) / KP_REP_CHUNK, i;
	uint8_t *scratch = (uint8_t *)malloc(KP_REP_CHUNK);
	char *work = (char *)malloc(CMP_BUFFER_SIZE);
	int rc = -1;
	if (!scratch || !work) goto done;
	if (append32(buf, len, cap, kp_rep_crc(in, n)) || append32(buf, len, cap, (uint32_t)chunks)) goto done;
	for (i = 0; i < chunks; i++) {
		size_t k = n - i * KP_REP_CHUNK < KP_REP_CHUNK ? n - i * KP_REP_CHUNK : KP_REP_CHUNK;
		const uint8_t *out;
		size_t m = kp_rep_chunk(in + i * KP_REP_CHUNK, k, scratch, work, &out);
		if (append32(buf, len, cap, (uint32_t)m) || append(buf, len, cap, out, m)) goto done;
	}
	rc = 0;
done:
	free(scratch);
	free(work);
	return rc;
}

/* ReadCompressedBlock 0x413ae0: the chunk count is not tied to the size, each chunk's length is bounded by what is
 * left, a chunk whose length is not its expected size is exploded and must give at most what is left; the block
 * needs exactly n bytes and its checksum. */
int kp_rep_block_read(const uint8_t *data, size_t size, size_t *pos, uint8_t *out, size_t n)
{
	size_t p = *pos, consumed = 0;
	uint32_t want, chunks, i;
	char *work = NULL;
	int rc = -1;
	if (n == 0 || size < 8 || p > size - 8) return -1;
	want = rd32(data + p);
	chunks = rd32(data + p + 4);
	p += 8;
	for (i = 0; i < chunks; i++) {
		size_t len, expected, left = n - consumed;
		if (p > size - 4) goto done;
		len = rd32(data + p);
		p += 4;
		if (len > left || len > size - p) goto done;
		expected = n - (size_t)i * KP_REP_CHUNK;
		if ((size_t)i * KP_REP_CHUNK > n) expected = 0;
		if (expected > KP_REP_CHUNK) expected = KP_REP_CHUNK;
		if (len == expected) {
			memcpy(out + consumed, data + p, len);
			consumed += len;
		} else {
			Pipe pp;
			if (!work && !(work = (char *)malloc(EXP_BUFFER_SIZE))) goto done;
			pp.in = data + p;
			pp.in_size = len;
			pp.in_pos = 0;
			pp.out = out + consumed;
			pp.out_cap = left;
			pp.out_n = 0;
			if (explode(pipe_read, pipe_write, work, &pp) != CMP_NO_ERROR || pp.out_n == 0 || pp.out_n > left)
				goto done;
			consumed += pp.out_n;
		}
		p += len;
		if (consumed > n) goto done;
	}
	if (consumed != n || kp_rep_crc(out, n) != want) goto done;
	*pos = p;
	rc = 0;
done:
	free(work);
	return rc;
}

/* --- the tail --- */

#define TAIL_INTS (2 * KP_CARRY_PLAYERS + 5 + 2 * KP_CARRY_COUNTS + KP_CARRY_PSTATS * 12 + KP_CARRY_PLAYERS)
#define TAIL_PAYLOAD (4 + 4 + 32 + 4 + 8 * TAIL_INTS + 8 + 4 + 8 * 8 + 4 + 4)

/* KpCarry is int64_t fields only: its values in declaration order */
typedef char carry_is_int64s[sizeof(KpCarry) == 8 * (TAIL_INTS) ? 1 : -1];

static void put64(uint8_t *p, int64_t v)
{
	wr32(p, (uint32_t)(uint64_t)v);
	wr32(p + 4, (uint32_t)((uint64_t)v >> 32));
}

static int64_t get64(const uint8_t *p) { return (int64_t)((uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32); }

static int tail_codec(KpRepTail *t, uint8_t *b, int write)
{
	int64_t *carry = (int64_t *)(void *)&t->carry;
	size_t o = 0;
	int k;
#define U32(x) do { if (write) wr32(b + o, (uint32_t)(x)); else (x) = rd32(b + o); o += 4; } while (0)
#define I64(x) do { if (write) put64(b + o, (x)); else (x) = get64(b + o); o += 8; } while (0)
	U32(t->version);
	U32(t->sim_revision);
	if (write) memcpy(b + o, t->data_hash, 32);
	else memcpy(t->data_hash, b + o, 32);
	o += 32;
	U32(t->has_carry);
	for (k = 0; k < TAIL_INTS; k++) I64(carry[k]);
	I64(t->cheats);
	U32(t->objective_count);
	for (k = 0; k < 8; k++) I64(t->objectives[k]);
	U32(t->loads);
	U32(t->lost);
#undef U32
#undef I64
	return o == TAIL_PAYLOAD ? 0 : -1;
}

/* --- the file --- */

static int refuse(KpRepRefusal *why, KpRepRefusal r, KpError *err, const char *msg)
{
	if (why) *why = r;
	if (err) snprintf(err->msg, sizeof err->msg, "rep: %s", msg);
	return -1;
}

void kp_rep_free(KpRep *r)
{
	free(r->record);
	free(r->chk);
	free(r->tail);
	memset(r, 0, sizeof *r);
}

int kp_rep_read(KpRep *r, const uint8_t *data, size_t size, KpRepRefusal *why, KpError *err)
{
	size_t pos = 0;
	uint8_t word[4];
	int32_t used;
	uint32_t chk;
	memset(r, 0, sizeof *r);
	if (why) *why = KP_REP_OK;
	if (kp_rep_block_read(data, size, &pos, word, 4) != 0 || rd32(word) != KP_REP_MAGIC)
		return refuse(why, KP_REP_DAMAGED, err, "not a replay");
	if (kp_rep_block_read(data, size, &pos, r->header, KP_REP_HEADER_SIZE) != 0 || r->header[KP_REP_PRESENT] == 0)
		return refuse(why, KP_REP_DAMAGED, err, "bad header");
	if (kp_rep_block_read(data, size, &pos, word, 4) != 0) return refuse(why, KP_REP_DAMAGED, err, "bad record size");
	used = (int32_t)rd32(word);
	if (used > 0) {
		if ((uint32_t)used > KP_REP_MAX_RECORD) return refuse(why, KP_REP_DAMAGED, err, "record too large");
		r->record = (uint8_t *)malloc((size_t)used);
		if (!r->record) return refuse(why, KP_REP_NO_MEMORY, err, "out of memory");
		r->record_size = (size_t)used;
		if (kp_rep_block_read(data, size, &pos, r->record, r->record_size) != 0) {
			kp_rep_free(r);
			return refuse(why, KP_REP_DAMAGED, err, "bad command record");
		}
	}
	if (kp_rep_block_read(data, size, &pos, word, 4) != 0) {
		kp_rep_free(r);
		return refuse(why, KP_REP_DAMAGED, err, "bad map size");
	}
	chk = rd32(word);
	if (chk == 0 || chk > KP_REP_MAX_CHK) {
		kp_rep_free(r);
		return refuse(why, KP_REP_DAMAGED, err, "bad map size");
	}
	r->chk = (uint8_t *)malloc(chk);
	if (!r->chk) {
		kp_rep_free(r);
		return refuse(why, KP_REP_NO_MEMORY, err, "out of memory");
	}
	r->chk_size = chk;
	if (kp_rep_block_read(data, size, &pos, r->chk, chk) != 0) {
		kp_rep_free(r);
		return refuse(why, KP_REP_DAMAGED, err, "bad map");
	}
	/* Koprulu's tail: "KPRX", then a block of the payload; anything else stays unread */
	if (size - pos >= 4 && memcmp(data + pos, KP_REP_TAIL_MAGIC, 4) == 0) {
		uint8_t *payload = (uint8_t *)malloc(TAIL_PAYLOAD);
		size_t p = pos + 4;
		r->tail = (KpRepTail *)calloc(1, sizeof *r->tail);
		if (!payload || !r->tail) {
			free(payload);
			kp_rep_free(r);
			return refuse(why, KP_REP_NO_MEMORY, err, "out of memory");
		}
		if (kp_rep_block_read(data, size, &p, payload, TAIL_PAYLOAD) == 0 && tail_codec(r->tail, payload, 0) == 0 &&
			r->tail->version == KP_REP_TAIL_VERSION) {
			r->has_tail = 1;
			pos = p;
		} else {
			free(r->tail);
			r->tail = NULL;
		}
		free(payload);
	}
	r->extra_size = size - pos;
	return 0;
}

int kp_rep_write(const KpRep *r, uint8_t **out, size_t *size, KpError *err)
{
	uint8_t *b = NULL, word[4];
	size_t len = 0, cap = 0;
	int ok = 1;
	wr32(word, KP_REP_MAGIC);
	ok = ok && kp_rep_block_write(&b, &len, &cap, word, 4) == 0;
	ok = ok && kp_rep_block_write(&b, &len, &cap, r->header, KP_REP_HEADER_SIZE) == 0;
	wr32(word, (uint32_t)r->record_size);
	ok = ok && kp_rep_block_write(&b, &len, &cap, word, 4) == 0;
	if (ok && r->record_size) ok = kp_rep_block_write(&b, &len, &cap, r->record, r->record_size) == 0;
	wr32(word, (uint32_t)r->chk_size);
	ok = ok && kp_rep_block_write(&b, &len, &cap, word, 4) == 0;
	ok = ok && kp_rep_block_write(&b, &len, &cap, r->chk, r->chk_size) == 0;
	if (ok && r->tail) {
		uint8_t *payload = (uint8_t *)malloc(TAIL_PAYLOAD);
		KpRepTail t = *r->tail;
		ok = payload && tail_codec(&t, payload, 1) == 0 && append(&b, &len, &cap, KP_REP_TAIL_MAGIC, 4) == 0 &&
			kp_rep_block_write(&b, &len, &cap, payload, TAIL_PAYLOAD) == 0;
		free(payload);
	}
	if (!ok) {
		free(b);
		if (err) snprintf(err->msg, sizeof err->msg, "rep: out of memory");
		return -1;
	}
	*out = b;
	*size = len;
	return 0;
}

/* --- the header --- */

uint32_t kp_rep_frames(const KpRep *r) { return rd32(r->header + KP_REP_FRAMES); }
int kp_rep_campaign(const KpRep *r) { return r->header[KP_REP_CAMPAIGN] | r->header[KP_REP_CAMPAIGN + 1] << 8; }
uint32_t kp_rep_seed(const KpRep *r) { return rd32(r->header + KP_REP_SEED); }
int kp_rep_speed(const KpRep *r) { return r->header[KP_REP_SPEED]; }
int kp_rep_brood_war(const KpRep *r) { return r->header[KP_REP_BROOD_WAR]; }
void kp_rep_set_frames(KpRep *r, uint32_t frames) { wr32(r->header + KP_REP_FRAMES, frames); }
void kp_rep_set_seed(KpRep *r, uint32_t seed) { wr32(r->header + KP_REP_SEED, seed); }

/* The game header's bytes from +0x6d on in every 1.17 campaign replay: the game type 0x0a (Use Map Settings), subtype
 * 1, and the template's victory, resources, unit stats, fog 2, starting units, fixed positions 1, restrictions 3,
 * allies 1, teams, cheats 1 */
static const uint8_t UMS_TEMPLATE[] = { 0x0a, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 3, 1, 0, 1, 0 };
#define UMS_TEMPLATE_AT (KP_REP_GAME + 0x6d)

/* strlen up to max (strnlen is POSIX, not C99: the web build has none) */
static size_t text_len(const char *text, size_t max)
{
	size_t n = 0;
	while (n < max && text[n] != 0) n++; /* never past the terminator (memchr may read ahead before C11) */
	return n;
}

static void put_text(uint8_t *at, const char *text, size_t field)
{
	size_t n = text ? text_len(text, field - 1) : 0;
	memset(at, 0, field);
	if (n) memcpy(at, text, n);
}

void kp_rep_header(KpRep *r, const KpRepGame *game)
{
	uint8_t *h = r->header;
	int k;
	memset(h, 0, KP_REP_HEADER_SIZE);
	h[KP_REP_BROOD_WAR] = (uint8_t)(game->brood_war != 0);
	wr32(h + KP_REP_FRAMES, game->frames);
	h[KP_REP_CAMPAIGN] = (uint8_t)game->campaign;
	h[KP_REP_CAMPAIGN + 1] = (uint8_t)(game->campaign >> 8);
	wr32(h + KP_REP_SEED, game->seed);
	put_text(h + KP_REP_GAME_NAME, game->player_name, KP_REP_GAME_NAME_SIZE);
	h[KP_REP_MAP_SIZE] = (uint8_t)game->map_w;
	h[KP_REP_MAP_SIZE + 1] = (uint8_t)(game->map_w >> 8);
	h[KP_REP_MAP_SIZE + 2] = (uint8_t)game->map_h;
	h[KP_REP_MAP_SIZE + 3] = (uint8_t)(game->map_h >> 8);
	h[KP_REP_SLOTS - 1] = 1; /* the active players */
	h[KP_REP_SLOTS] = 1;
	h[KP_REP_SPEED] = (uint8_t)game->speed;
	h[KP_REP_GAME + 0x28] = 0x0a; /* the game type and subtype, again in the template */
	h[KP_REP_GAME + 0x2a] = 1;
	h[KP_REP_TILESET] = (uint8_t)game->tileset;
	h[KP_REP_TILESET + 1] = (uint8_t)(game->tileset >> 8);
	h[KP_REP_PRESENT] = 1;
	put_text(h + KP_REP_MAP_NAME, game->map_name, KP_REP_MAP_NAME_SIZE);
	memcpy(h + UMS_TEMPLATE_AT, UMS_TEMPLATE, sizeof UMS_TEMPLATE);
	for (k = 0; k < KP_REP_PLAYERS; k++) {
		KpRepPlayer p;
		if (k < 8) {
			p = game->players[k];
		} else {
			memset(&p, 0, sizeof p);
			p.storm = -1;
		}
		p.slot = (uint32_t)k;
		kp_rep_set_player(r, k, &p);
	}
	for (k = 0; k < 8; k++) wr32(h + KP_REP_COLORS + 4 * k, (uint32_t)k);
}

void kp_rep_player(const KpRep *r, int index, KpRepPlayer *p)
{
	const uint8_t *b = r->header + KP_REP_PLAYER_RECORDS + index * KP_REP_PLAYER_SIZE;
	p->slot = rd32(b);
	p->storm = (int32_t)rd32(b + 4);
	p->type = b[8];
	p->race = b[9];
	p->force = b[10];
	memcpy(p->name, b + 11, KP_REP_NAME);
	p->name[KP_REP_NAME - 1] = 0;
}

void kp_rep_set_player(KpRep *r, int index, const KpRepPlayer *p)
{
	uint8_t *b = r->header + KP_REP_PLAYER_RECORDS + index * KP_REP_PLAYER_SIZE;
	size_t n = text_len(p->name, KP_REP_NAME - 1);
	wr32(b, p->slot);
	wr32(b + 4, (uint32_t)p->storm);
	b[8] = p->type;
	b[9] = p->race;
	b[10] = p->force;
	memset(b + 11, 0, KP_REP_NAME);
	memcpy(b + 11, p->name, n);
}
