/* The replay's command record (kp_record.h). */
#include "kp_record.h"

#include <stdlib.h>
#include <string.h>

/* Not recorded (RecordCommand 0x478340's table 0x4ef2fc) */
static const uint8_t EXCLUDED[] = { 0x37, 0x05, 0x06, 0x07, 0x08, 0x10, 0x11, 0x38, 0x39, 0x3a, 0x3b, 0x55 };
/* 0x4ef048, read by 0x4720c0: the length with the id byte; -1 unknown (ids 9-0xb: 2 + 2 * count) */
static const int8_t LENGTHS[0x60] = {
	/* 0x00 */ -1, -1, -1, -1, -1, 1, 33, 33, 1, 0, 0, 0, 8, 3, 5, 2,
	/* 0x10 */ 1, 1, 5, 3, 10, 11, -1, -1, 1, 1, 2, 1, 1, 1, 2, 3,
	/* 0x20 */ 3, 2, 2, 3, 1, 2, 2, 1, 2, 3, 1, 2, 2, 2, 1, 5,
	/* 0x30 */ 2, 1, 2, 1, 1, 3, 1, 1, 1, 1, 2, 2, -1, -1, -1, -1,
	/* 0x40 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	/* 0x50 */ -1, -1, -1, -1, -1, 2, 10, 2, 5, -1, 1, -1, 82, -1, -1, -1,
};

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int kp_record_command_length(const uint8_t *b, size_t size)
{
	if (size == 0) return -1;
	if (b[0] >= 9 && b[0] <= 0xb) return size >= 2 ? 2 + 2 * b[1] : -1;
	return b[0] < sizeof LENGTHS ? LENGTHS[b[0]] : -1;
}

int kp_record_init(KpRecorder *r)
{
	memset(r, 0, sizeof *r);
	r->buf = (uint8_t *)malloc(KP_RECORD_START);
	if (!r->buf) return -1;
	r->cap = KP_RECORD_START;
	r->recording = 1;
	r->open_frame = -1;
	r->buffered_frame = -1;
	return 0;
}

void kp_record_free(KpRecorder *r)
{
	free(r->buf);
	free(r->exec);
	free(r->entries);
	r->buf = r->exec = NULL;
	r->entries = NULL;
	r->used = r->cap = r->exec_size = r->exec_cap = 0;
	r->entries_cap = 0;
}

/* GrowRecordBuffer 0x4785c0 */
static int grow(KpRecorder *r, size_t need)
{
	while (r->used + need >= r->cap) {
		uint8_t *g = (uint8_t *)realloc(r->buf, r->cap + KP_RECORD_GROW);
		if (!g) return -1;
		r->buf = g;
		r->cap += KP_RECORD_GROW;
		if (need <= KP_RECORD_SCRATCH) break; /* the original grows once per command */
	}
	return 0;
}

/* StartRecordBlock 0x4787e0: closes the open block (an empty one is removed) and opens one for `frame`. */
static void start_block(KpRecorder *r, int64_t frame)
{
	if (r->open) {
		size_t payload = r->used - r->open;
		if (payload == 0) r->used -= 5;
		else r->buf[r->open - 1] = (uint8_t)payload;
	}
	r->buf[r->used] = (uint8_t)frame;
	r->buf[r->used + 1] = (uint8_t)(frame >> 8);
	r->buf[r->used + 2] = (uint8_t)(frame >> 16);
	r->buf[r->used + 3] = (uint8_t)(frame >> 24);
	r->buf[r->used + 4] = 0;
	r->used += 5;
	r->open = r->used;
	r->open_frame = frame;
}

int kp_record_excluded(uint8_t id)
{
	size_t k;
	for (k = 0; k < sizeof EXCLUDED; k++)
		if (id == EXCLUDED[k]) return 1;
	return 0;
}

void kp_record_command(KpRecorder *r, int64_t frame, uint8_t storm, const uint8_t *cmd, size_t size)
{
	if (!r->recording || !r->buf || size == 0 || size >= 0x100 || kp_record_excluded(cmd[0])) return;
	if (r->used + KP_RECORD_SCRATCH >= r->cap && grow(r, KP_RECORD_SCRATCH) != 0) return;
	if (r->open && (r->used - r->open) + 1 + size >= 0xff) start_block(r, frame);
	if (r->open_frame != frame) start_block(r, frame);
	if (r->used + size + 1 <= r->cap) {
		r->buf[r->used++] = storm;
		memcpy(r->buf + r->used, cmd, size);
		r->used += size;
	}
}

void kp_record_close(KpRecorder *r)
{
	if (r->open) {
		r->buf[r->open - 1] = (uint8_t)(r->used - r->open);
		r->open = 0; /* the frame is kept: later commands of this frame go on without a header */
	}
}

const uint8_t *kp_record_bytes(KpRecorder *r, size_t *size)
{
	kp_record_close(r);
	*size = r->used;
	return r->buf;
}

int kp_record_restore(KpRecorder *r, const uint8_t *bytes, size_t size, int64_t open_frame)
{
	size_t cap = KP_RECORD_START;
	uint8_t *b;
	while (size + KP_RECORD_SCRATCH >= cap) cap += KP_RECORD_GROW;
	b = (uint8_t *)malloc(cap);
	if (!b) return -1;
	if (size) memcpy(b, bytes, size);
	free(r->buf);
	r->buf = b;
	r->cap = cap;
	r->used = size;
	r->open = 0;
	r->open_frame = open_frame;
	return 0;
}

int kp_record_load(KpRecorder *r, const uint8_t *bytes, size_t size)
{
	if (kp_record_restore(r, bytes, size, -1) != 0) return -1;
	r->recording = 0;
	r->playing = 1;
	r->cursor = 0;
	r->buffered_frame = -1;
	r->count = 0;
	return 0;
}

/* The buffer record of FeedReplayCommands (0x4782d0): the static buffers, or a heap copy once a read outgrew them */
typedef struct {
	uint8_t *data;
	uint32_t *lengths;
	size_t cap;
	int owned;
} Scratch;

/* GrowCommandBuffer 0x478710: copies the old capacity's bytes and lengths. */
static int grow_scratch(Scratch *s, size_t cap)
{
	uint8_t *d = (uint8_t *)malloc(cap);
	uint32_t *l = (uint32_t *)malloc(cap * sizeof *l);
	if (!d || !l) {
		free(d);
		free(l);
		return -1;
	}
	memcpy(d, s->data, s->cap);
	memcpy(l, s->lengths, s->cap * sizeof *l);
	if (s->owned) {
		free(s->data);
		free(s->lengths);
	}
	s->data = d;
	s->lengths = l;
	s->cap = cap;
	s->owned = 1;
	return 0;
}

/* ReadRecordedFrame 0x478480: the next frame's commands into s and r->ids; -1 at the end of the data (the count is
 * not stored) or when the data ends inside a block. An id 0x4720c0 does not know ends the data too (the original
 * would copy 0xffffffff bytes). */
static int64_t read_frame(KpRecorder *r, Scratch *s)
{
	int64_t frame;
	size_t stored = 0;
	int i = 0;
	if (!r->playing || r->cursor + 5 > r->used) return -1;
	frame = rd32(r->buf + r->cursor);
	while (r->cursor + 5 <= r->used && rd32(r->buf + r->cursor) == (uint32_t)frame) {
		size_t size = r->buf[r->cursor + 4], sum = 0;
		r->cursor += 5;
		if (stored + size >= s->cap && grow_scratch(s, stored + size) != 0) return -1;
		while (sum < size) {
			int len;
			if (r->cursor + 1 > r->used || i >= KP_RECORD_MAX_ENTRIES) return -1;
			r->ids[i] = r->buf[r->cursor++];
			len = kp_record_command_length(r->buf + r->cursor, r->used - r->cursor);
			if (len < 0 || r->cursor + (size_t)len > r->used || stored + (size_t)len > s->cap) return -1;
			memcpy(s->data + stored, r->buf + r->cursor, (size_t)len);
			stored += (size_t)len;
			s->lengths[i++] = (uint32_t)len;
			r->cursor += (size_t)len;
			sum += (size_t)len + 1;
		}
	}
	if (r->cursor + 5 > r->used) return -1;
	r->count = i;
	return frame;
}

int kp_record_feed(KpRecorder *r, int64_t f, uint32_t frames, const KpRecordEntry **out, int *count)
{
	Scratch s;
	size_t offset = 0;
	int i, n = 0;
	*count = 0;
	*out = r->entries;
	if (f >= (int64_t)frames) return 1;
	if (f < r->buffered_frame) return 0;
	s.data = r->data;
	s.lengths = r->lengths;
	s.cap = KP_RECORD_SCRATCH;
	s.owned = 0;
	if (f > r->buffered_frame) {
		r->buffered_frame = read_frame(r, &s);
		if (r->buffered_frame == -1 || f < r->buffered_frame) goto done;
	}
	/* the frame's commands, from whichever buffers the record now has: the static ones still hold a frame read
	 * early whose heap copy was freed, with what an earlier frame left after the part written before the growth */
	r->exec_size = 0;
	if (r->count > r->entries_cap) {
		KpRecordEntry *e = (KpRecordEntry *)realloc(r->entries, (size_t)r->count * sizeof *e);
		if (!e) goto done;
		r->entries = e;
		r->entries_cap = r->count;
	}
	for (i = 0; i < r->count; i++) {
		size_t len;
		if ((size_t)i >= s.cap) break; /* past the buffers' lengths (more than 0x200 entries: not modelled) */
		len = s.lengths[i];
		if (offset + len > s.cap) break; /* the original reads on past its buffer */
		if (r->exec_size + len > r->exec_cap) {
			size_t cap = r->exec_cap ? r->exec_cap : 1024;
			uint8_t *g;
			while (cap < r->exec_size + len) cap *= 2;
			if (!(g = (uint8_t *)realloc(r->exec, cap))) break;
			r->exec = g;
			r->exec_cap = cap;
		}
		memcpy(r->exec + r->exec_size, s.data + offset, len);
		r->exec_size += len;
		offset += len;
		r->entries[n].storm = r->ids[i];
		r->entries[n].size = len;
		n++;
	}
	offset = 0;
	for (i = 0; i < n; i++) {
		r->entries[i].bytes = r->exec + offset;
		offset += r->entries[i].size;
	}
	*out = r->entries;
	*count = n;
	r->count = 0;
done:
	if (s.owned) {
		free(s.data);
		free(s.lengths);
	}
	return 0;
}

/* ExecuteTurnCommands 0x475d30's lengths: -1 stops the buffer, 0 for 6/7 (a name) and 9-0xb (a count) */
static int dispatch_length(const uint8_t *b, size_t remaining)
{
	uint8_t c = b[0];
	if (c < 5 || c > 0x5c) return -1;
	if (c == 6 || c == 7) { /* the name: ValidateNetString 0x476e20(max 0x1c, no formatting, no controls) */
		size_t k, max = remaining > 5 ? remaining - 5 : 0;
		if (max > 0x1c) max = 0x1c;
		for (k = 0; k < max; k++) {
			if (b[5 + k] == 0) return (int)(5 + k + 1);
			if (b[5 + k] < 0x20) return -1; /* every control code 1-31 fails, which stops the buffer */
		}
		return -1;
	}
	if (c >= 9 && c <= 0xb) return remaining >= 2 ? 2 + 2 * b[1] : -1;
	if (c == 0x37) return 7;
	if (c == 0x24) return -1;
	return LENGTHS[c];
}

int kp_record_split(const uint8_t *b, size_t size, void (*fn)(void *ud, const uint8_t *cmd, size_t n), void *ud)
{
	size_t at = 0;
	int n = 0;
	while (at < size) {
		int len = dispatch_length(b + at, size - at);
		if (len <= 0 || (size_t)len > size - at) break;
		fn(ud, b + at, (size_t)len);
		at += (size_t)len;
		n++;
	}
	return n;
}
