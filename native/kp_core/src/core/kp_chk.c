/* A scenario read by the original's loader tables (kp_chk.h; converter/formats/chk.py load, the same checks in the same
 * order and with the same messages). */
#include "kp_chk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_kpf.h"
#include "kp_sha256.h"

#define CHUNK_HEADER 8
#define ROW_SIZE 19 /* chk.LOADER_ROW: tag[4], required, reads, vcod, u32 eq, u32 max, u32 multiple */
#define ROW_REQUIRED 4
#define ROW_READS 5
#define ROW_VCOD 6
#define ROW_EQ 7
#define ROW_MAX 11
#define ROW_MULTIPLE 15
#define NO_LIMIT 0xffffffffu
#define ERA_MAX_WITHOUT_EXPANSION 4

typedef struct {
	char tag[5];
	uint32_t header, size;
} Chunk;

/* The profile's CHKL: the descriptor of a version (its rows), the ignored tags, the VCOD digest. */
typedef struct {
	const uint8_t *rows, *ignored, *vcod;
	int row_count, ignored_count, expansion_required, found;
} Tables;

static int fail(char *err, size_t err_size, const char *what)
{
	snprintf(err, err_size, "%s", what);
	return -1;
}

const KpChkSection *kp_chk_section(const KpChk *c, const char *tag)
{
	int k;
	for (k = 0; k < c->count; k++)
		if (memcmp(c->sections[k].tag, tag, 4) == 0) return &c->sections[k];
	return NULL;
}

/* 0, or -1 when the table is malformed */
static int tables_of(const uint8_t *t, size_t n, uint32_t version, Tables *out)
{
	size_t pos = 1;
	int k, count = n ? t[0] : 0;
	memset(out, 0, sizeof *out);
	for (k = 0; k < count; k++) {
		int rows;
		if (pos + 4 > n) return -1;
		rows = t[pos + 3];
		if (pos + 4 + (size_t)rows * ROW_SIZE > n) return -1;
		if (kp_le_u16(t + pos) == version && !out->found) {
			out->found = 1;
			out->expansion_required = t[pos + 2];
			out->row_count = rows;
			out->rows = t + pos + 4;
		}
		pos += 4 + (size_t)rows * ROW_SIZE;
	}
	if (pos >= n) return -1;
	out->ignored_count = t[pos];
	out->ignored = t + pos + 1;
	pos += 1 + 4 * (size_t)out->ignored_count;
	if (pos + KP_SHA256_SIZE != n) return -1;
	out->vcod = t + pos;
	return 0;
}

static int row_reads(const Tables *t, const char *tag)
{
	int r;
	for (r = 0; r < t->row_count; r++)
		if (memcmp(t->rows + r * ROW_SIZE, tag, 4) == 0) return 1;
	return 0;
}

static int ignored(const Tables *t, const char *tag)
{
	int k;
	for (k = 0; k < t->ignored_count; k++)
		if (memcmp(t->ignored + 4 * k, tag, 4) == 0) return 1;
	return 0;
}

static int occurrences(const Chunk *chunks, int count, const uint8_t *tag)
{
	int k, n = 0;
	for (k = 0; k < count; k++) n += memcmp(chunks[k].tag, tag, 4) == 0;
	return n;
}

/* _check_payload */
static int check_payload(const Chunk *ch, const uint8_t *row, const uint8_t *body, const uint8_t *vcod, char *err,
	size_t err_size)
{
	uint32_t eq = kp_le_u32(row + ROW_EQ), max = kp_le_u32(row + ROW_MAX), multiple = kp_le_u32(row + ROW_MULTIPLE);
	if (eq != NO_LIMIT && ch->size != eq) {
		snprintf(err, err_size, "section '%s': %u bytes, the native loader requires exactly %u", ch->tag,
			(unsigned)ch->size, (unsigned)eq);
		return -1;
	}
	if (max != NO_LIMIT && ch->size > max) {
		snprintf(err, err_size, "section '%s': %u bytes, the native loader accepts at most %u", ch->tag,
			(unsigned)ch->size, (unsigned)max);
		return -1;
	}
	if (multiple && ch->size % multiple) {
		snprintf(err, err_size, "section '%s': %u bytes is not a multiple of %u", ch->tag, (unsigned)ch->size,
			(unsigned)multiple);
		return -1;
	}
	if (row[ROW_VCOD]) {
		/* equal bytes give equal digests; ComputeVcodHash 0x464560 could pass another block, which no map has */
		uint8_t digest[KP_SHA256_SIZE];
		kp_sha256(body, ch->size, digest);
		if (memcmp(digest, vcod, KP_SHA256_SIZE) != 0) {
			snprintf(err, err_size, "section '%s' differs from the executable's VCOD resource", ch->tag);
			return -1;
		}
	}
	return 0;
}

static int read_chunks(KpChk *c, const uint8_t *data, size_t size, const KpDefsProfile *profile, int expansion,
	const Chunk *chunks, int chunk_count, char *err, size_t err_size)
{
	const KpDefsTable *table = profile ? kp_defs_table(profile, "CHKL") : NULL;
	const Chunk *version_chunk = NULL;
	const KpChkSection *era;
	Tables t;
	int k, r;
	if (!table) return fail(err, err_size, "the profile has no CHK loader tables (CHKL)");
	for (k = 0; k < chunk_count && !version_chunk; k++)
		if (memcmp(chunks[k].tag, "VER ", 4) == 0) version_chunk = &chunks[k];
	if (!version_chunk) return fail(err, err_size, "missing required section 'VER '");
	if ((uint64_t)version_chunk->header + CHUNK_HEADER + version_chunk->size > size) {
		snprintf(err, err_size, "section 'VER ' at %#x: payload of %u bytes runs past the end",
			(unsigned)version_chunk->header, (unsigned)version_chunk->size);
		return -1;
	}
	if (version_chunk->size != 2) {
		snprintf(err, err_size, "section 'VER ': %u bytes, the native loader requires exactly 2",
			(unsigned)version_chunk->size);
		return -1;
	}
	c->version = (uint16_t)kp_le_u16(data + version_chunk->header + CHUNK_HEADER);
	if (tables_of(table->data, table->size, c->version, &t) != 0)
		return fail(err, err_size, "the profile's CHK loader tables are malformed");
	if (!t.found) {
		snprintf(err, err_size, "CHK version %u has no native loader descriptor; the original preflight rejects it",
			(unsigned)c->version);
		return -1;
	}
	if (t.expansion_required && !expansion) {
		snprintf(err, err_size, "CHK version %u requires Brood War", (unsigned)c->version);
		return -1;
	}
	for (k = 0; k < chunk_count; k++)
		if (!row_reads(&t, chunks[k].tag) && !ignored(&t, chunks[k].tag)) {
			snprintf(err, err_size, "section '%s' is neither read by the version %u loader nor allowlisted", chunks[k].tag,
				(unsigned)c->version);
			return -1;
		}
	for (r = 0; r < t.row_count; r++) {
		const uint8_t *row = t.rows + r * ROW_SIZE;
		if (row[ROW_REQUIRED] && occurrences(chunks, chunk_count, row) == 0) {
			snprintf(err, err_size, "missing required section '%.4s' (version %u)", (const char *)row,
				(unsigned)c->version);
			return -1;
		}
	}
	for (r = 0; r < t.row_count; r++) {
		const uint8_t *row = t.rows + r * ROW_SIZE;
		if (occurrences(chunks, chunk_count, row) > 1) {
			snprintf(err, err_size, "duplicate section '%.4s' (outside the supported corpus)", (const char *)row);
			return -1;
		}
	}
	/* 0x4c12e0: table order outer, chunk order inner; a tag two groups read keeps its first place */
	for (r = 0; r < t.row_count; r++) {
		const uint8_t *row = t.rows + r * ROW_SIZE;
		for (k = 0; k < chunk_count; k++) {
			const Chunk *ch = &chunks[k];
			const uint8_t *body;
			KpChkSection *s;
			if (memcmp(ch->tag, row, 4) != 0 || !row[ROW_READS]) continue;
			if ((uint64_t)ch->header + CHUNK_HEADER + ch->size > size) {
				snprintf(err, err_size, "section '%s' at %#x: payload of %u bytes runs past the end", ch->tag,
					(unsigned)ch->header, (unsigned)ch->size);
				return -1;
			}
			body = data + ch->header + CHUNK_HEADER;
			if (check_payload(ch, row, body, t.vcod, err, err_size) != 0) return -1;
			if ((s = (KpChkSection *)kp_chk_section(c, ch->tag)) == NULL) {
				if (c->count == KP_CHK_SECTIONS) return fail(err, err_size, "more sections than the loader tables read");
				s = &c->sections[c->count++];
				memcpy(s->tag, ch->tag, 5);
			}
			s->data = body;
			s->size = ch->size;
		}
	}
	if (!(era = kp_chk_section(c, "ERA ")) || era->size != 2) return fail(err, err_size, "the scenario has no ERA");
	if (kp_le_u16(era->data) > ERA_MAX_WITHOUT_EXPANSION && !expansion) {
		snprintf(err, err_size, "tileset %u requires Brood War", (unsigned)kp_le_u16(era->data));
		return -1;
	}
	return 0;
}

int kp_chk_read(KpChk *c, const uint8_t *data, size_t size, const KpDefsProfile *profile, int expansion, char *err,
	size_t err_size)
{
	uint64_t header = 0; /* in 64 bits: a 32-bit size_t would wrap past a chunk's 2 GB */
	int count = 0, rc;
	Chunk *chunks;
	memset(c, 0, sizeof *c);
	/* 0x4c1170: a header when 8 bytes remain; the next one at its payload + size */
	while (header + CHUNK_HEADER <= size) {
		int32_t s = (int32_t)kp_le_u32(data + header + 4);
		if (s < 0) {
			snprintf(err, err_size, "chunk '%.4s' at %#x has negative size %d (unsupported)", (const char *)data + header,
				(unsigned)header, (int)s);
			return -1;
		}
		count++;
		header += CHUNK_HEADER + (uint64_t)(uint32_t)s;
	}
	if (!(chunks = (Chunk *)malloc((size_t)(count ? count : 1) * sizeof *chunks))) return fail(err, err_size, "out of memory");
	for (header = 0, count = 0; header + CHUNK_HEADER <= size; count++) {
		Chunk *ch = &chunks[count];
		memcpy(ch->tag, data + header, 4);
		ch->tag[4] = '\0';
		ch->header = (uint32_t)header;
		ch->size = kp_le_u32(data + header + 4);
		header += CHUNK_HEADER + (uint64_t)ch->size;
	}
	rc = read_chunks(c, data, size, profile, expansion, chunks, count, err, err_size);
	free(chunks);
	if (rc != 0) memset(c, 0, sizeof *c);
	return rc;
}
