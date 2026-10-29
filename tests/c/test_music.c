/*
 * The music container's track index (native/kp_app/kp_music.c): the tracks' places read from the file's head and the
 * tracks' small headers, never from their bytes, on a container built here and on the converted packs/music.kpd.
 */
#include "kp_test.h"

#include "kp_music.h"

#define MUSIC "generated/packs/music.kpd"

typedef struct {
	const uint8_t *data;
	size_t size;
	size_t max_read;
	int reads;
	int fail_read; /* this read (from 1) fails; 0 none */
} Memory;

static int read_memory(void *ctx, uint64_t offset, uint8_t *out, size_t size)
{
	Memory *m = (Memory *)ctx;
	if (m->fail_read != 0 && m->reads + 1 == m->fail_read) return -1;
	if (offset > m->size || size > m->size - offset) return -1;
	memcpy(out, m->data + offset, size);
	m->reads++;
	if (size > m->max_read) m->max_read = size;
	return 0;
}

static void put16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
	put16(p, v);
	put16(p + 2, v >> 16);
}

/* A MUSC container of one TRKS section: tracks[i] is {flags, size}; the OGG bytes are 'a' + i. */
static size_t build(uint8_t *out, size_t cap, const uint32_t (*tracks)[2], int count, uint32_t size_override)
{
	size_t at = 1, k, j, total;
	uint8_t *s = out + 176;
	memset(out, 0, cap);
	s[0] = (uint8_t)count;
	for (k = 0; k < (size_t)count; k++) {
		s[at] = (uint8_t)tracks[k][0];
		put32(s + at + 1, k == 0 && size_override ? size_override : tracks[k][1]);
		for (j = 0; j < tracks[k][1]; j++) s[at + 5 + j] = (uint8_t)('a' + k);
		at += 5 + tracks[k][1];
	}
	total = 176 + at;
	memcpy(out, "KPRFMUSC", 8);
	put16(out + 8, 1);
	put16(out + 10, 128);
	put16(out + 12, 48);
	put16(out + 14, 1);
	put32(out + 104, 128);
	put32(out + 108, (uint32_t)total);
	memcpy(out + 128, "TRKS", 4);
	put32(out + 128 + 8, 176);
	put32(out + 128 + 12, (uint32_t)at);
	return total;
}

static void index_follows_the_tracks(KpT *T)
{
	static const uint32_t TRACKS[3][2] = { { 0, 0 }, { 1, 5 }, { 0, 3 } };
	uint8_t file[256];
	size_t size = build(file, sizeof file, TRACKS, 3, 0);
	Memory m = { file, size, 0, 0, 0 };
	KpMusicIndex ix;
	KpError err;
	kp_t_eq(T, kp_music_index(&ix, file, size, size, read_memory, &m, &err), 0, "%s", err.msg);
	kp_t_eq(T, ix.count, 3, "three tracks");
	kp_t_eq(T, ix.tracks[0].size, 0, "an empty track");
	kp_t_eq(T, ix.tracks[1].offset, 176 + 1 + 5 + 5, "the second track's bytes follow its header");
	kp_t_eq(T, ix.tracks[1].size, 5, NULL);
	kp_t_eq(T, ix.tracks[1].loops, 1, "the loop flag");
	kp_t_eq(T, ix.tracks[2].offset, 176 + 1 + 5 + 10 + 5, NULL);
	kp_t_eq(T, ix.tracks[2].loops, 0, NULL);
	kp_t_eq(T, file[ix.tracks[2].offset], 'c', "the offset points at the OGG");
	kp_t_eq(T, m.reads, 4, "the count and each track's header");
	kp_t_ok(T, m.max_read <= 5, "no track bytes read");
}

static void index_refuses_a_track_past_its_section(KpT *T)
{
	static const uint32_t TRACKS[2][2] = { { 0, 4 }, { 0, 4 } };
	uint8_t file[256];
	size_t size = build(file, sizeof file, TRACKS, 2, 1000);
	Memory m = { file, size, 0, 0, 0 };
	KpMusicIndex ix;
	KpError err;
	kp_t_eq(T, kp_music_index(&ix, file, size, size, read_memory, &m, &err), -1, "a track longer than the section");
	kp_t_eq(T, ix.count, 0, NULL);
	file[128] = 'X'; /* no TRKS section */
	kp_t_eq(T, kp_music_index(&ix, file, size, size, read_memory, &m, &err), -1, "no TRKS");
}

/* A table cut short, more tracks than the index holds and a header that cannot be read: refused, with no tracks (a
 * partial index would leave the later tracks silent without a word). */
static void index_refuses_a_partial_table(KpT *T)
{
	static const uint32_t TRACKS[3][2] = { { 0, 2 }, { 0, 2 }, { 0, 2 } };
	uint8_t file[256];
	size_t size = build(file, sizeof file, TRACKS, 3, 0);
	Memory m = { file, size, 0, 0, 0 };
	KpMusicIndex ix;
	KpError err;
	kp_t_eq(T, kp_music_index(&ix, file, size, size, read_memory, &m, &err), 0, "%s", err.msg);
	kp_t_eq(T, ix.count, 3, NULL);
	m.fail_read = 3; /* the count, the first header, then the second's */
	m.reads = 0;
	kp_t_eq(T, kp_music_index(&ix, file, size, size, read_memory, &m, &err), -1, "a header that cannot be read");
	kp_t_eq(T, ix.count, 0, NULL);
	m.fail_read = 0;
	put32(file + 128 + 12, 15 + 3); /* TRKS ends inside the third header (at 15) */
	kp_t_eq(T, kp_music_index(&ix, file, size, size, read_memory, &m, &err), -1, "the table cut short");
	kp_t_ok(T, ix.count == 0 && strstr(err.msg, "truncated") != NULL, "%s", err.msg);
	put32(file + 128 + 12, (uint32_t)(size - 176));
	file[176] = KP_MUSIC_TRACKS + 1;
	kp_t_eq(T, kp_music_index(&ix, file, size, size, read_memory, &m, &err), -1, "more tracks than the index holds");
	kp_t_eq(T, ix.count, 0, NULL);
}

static void index_matches_the_converted_music(KpT *T)
{
	size_t size, k;
	const uint8_t *file;
	Memory m;
	KpMusicIndex ix;
	KpError err;
	uint64_t bytes = 0;
	if (!kp_t_has(T, MUSIC)) return;
	file = kp_t_read(T, MUSIC, &size);
	m.data = file;
	m.size = size;
	m.max_read = 0;
	m.reads = 0;
	m.fail_read = 0;
	kp_t_eq(T, kp_music_index(&ix, file, size < KP_MUSIC_HEAD ? size : KP_MUSIC_HEAD, size, read_memory, &m, &err), 0, "%s",
		err.msg);
	kp_t_ok(T, ix.count > 0, "tracks indexed");
	kp_t_ok(T, m.max_read <= 5, "the index reads headers only (%d bytes at most)", (int)m.max_read);
	for (k = 0; k < (size_t)ix.count; k++) {
		const KpMusicTrack *t = &ix.tracks[k];
		kp_t_ok(T, (uint64_t)t->offset + t->size <= size, "track %d inside the file", (int)k);
		if (t->size > 0) kp_t_ok(T, memcmp(file + t->offset, "OggS", 4) == 0, "track %d starts an OGG stream", (int)k);
		bytes += t->size;
	}
	kp_t_ok(T, bytes > 0, "the tracks hold the music");
}

static const KpTest TESTS[] = {
	{ "index_follows_the_tracks", index_follows_the_tracks, 0 },
	{ "index_refuses_a_track_past_its_section", index_refuses_a_track_past_its_section, 0 },
	{ "index_refuses_a_partial_table", index_refuses_a_partial_table, 0 },
	{ "index_matches_the_converted_music", index_matches_the_converted_music, 0 },
};

KP_SUITE(kp_suite_music, "test_music", TESTS);
