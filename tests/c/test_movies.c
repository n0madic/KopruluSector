/*
 * The cinematics of the data played as they are (ADR 0018, stage D7; ADR 0019, stage C4): kp_smk's palette records
 * and sound (Smacker's PCM and Huffman DPCM) over a movie written here, the subtitles of an .srt, a movie in memory
 * (KpCineMovie: an SMK's, a WebM's) played by kp_cine's player, a run of an install's movies; with KP_TEST_ORIGINAL the
 * 28 Smacker movies of the 1.17 install decode to FFmpeg's frames and sound (FFMPEG_GOLDEN: SHA-256 of each frame's
 * indices and palette as FFmpeg's pal8, and of the sound as s16le).
 */
#include "kp_test.h"
#include "kp_test_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_cine.h"
#include "kp_defs.h"
#include "kp_kpf.h"
#include "kp_res.h"
#include "kp_sha256.h"
#include "kp_smk.h"

#define W 16
#define H 8
#define FRAMES 3
#define FILL 7 /* every pixel's index */
#define KPW_FIXTURE "tests/fixtures/kpf/movie.kpw"
#define KPW_HEADER (24 + 768) /* a KPW1's header and palette before its WebM */
#define FFMPEG_GOLDEN "tests/golden/oracle/cinematics/smk_ffmpeg.json" /* tools/oracle/export_smk_ffmpeg.py */

/* --- the movie --- */

static const uint8_t COLOUR_A[3] = { 10, 20, 63 }, COLOUR_B[3] = { 1, 33, 48 };
/* the packed track's left deltas (each a code of its low and high tree) and right ones (single-leaf trees: +2) */
static const uint8_t LEFT_BITS[2][7] = { { 0, 1, 1, 0, 1, 0, 0 }, { 0, 0, 1, 0, 1, 1, 0 } }; /* per frame */
#define PACKED_FRAMES 8 /* stereo samples of a packed chunk: 2 first ones, then 7 deltas a side */
#define LEFT0 0x1234
#define RIGHT0 (-3)
static const uint8_t PCM8[2][5] = { { 0x80, 0x81, 0x7f, 0x00, 0xff }, { 1, 2, 3, 4, 5 } };

/* The 4-byte padded palette record of a frame: frame 0 sets entry FILL to COLOUR_A; frame 1 sets entry 0 to COLOUR_B and
 * copies to FILL the palette's entry 0 before the record (black). */
static size_t palette_record(int frame, uint8_t *out)
{
	size_t n = 1;
	if (frame == 0) {
		out[n++] = 0x80 | (FILL - 1); /* skip FILL entries */
		out[n++] = COLOUR_A[0];
		out[n++] = COLOUR_A[1];
		out[n++] = COLOUR_A[2];
	} else {
		out[n++] = COLOUR_B[0];
		out[n++] = COLOUR_B[1];
		out[n++] = COLOUR_B[2];
		out[n++] = 0x80 | (FILL - 2); /* skip 1..FILL-1 */
		out[n++] = 0x40;              /* copy 1 entry ... */
		out[n++] = 0;                 /* ... of the old palette's entry 0 */
	}
	out[n++] = 0x80 | 0x7f;                   /* skip 128 */
	out[n++] = (uint8_t)(0x80 | (256 - 128 - FILL - 1 - 1)); /* the rest */
	while (n % 4) out[n++] = 0;
	out[0] = (uint8_t)(n / 4);
	return n;
}

/* A packed 16-bit stereo chunk (u32 unpacked size, the bits) of frame f */
static size_t packed_chunk(int f, uint8_t *out)
{
	KpTBits o;
	int i;
	memset(&o, 0, sizeof o);
	kp_t_bits(&o, 1, 1); /* data */
	kp_t_bits(&o, 1, 1); /* stereo */
	kp_t_bits(&o, 1, 1); /* 16 bits */
	/* left low: +1 or 0xff (with the high byte: -1); left high: 0 or 0xff; right low and high: one leaf each */
	kp_t_bits(&o, 1, 1); kp_t_bits_pair(&o, 0x01, 0xff); kp_t_bits(&o, 0, 1);
	kp_t_bits(&o, 1, 1); kp_t_bits_pair(&o, 0x00, 0xff); kp_t_bits(&o, 0, 1);
	kp_t_bits(&o, 1, 1); kp_t_bits_leaf(&o, 0x02); kp_t_bits(&o, 0, 1);
	kp_t_bits(&o, 1, 1); kp_t_bits_leaf(&o, 0x00); kp_t_bits(&o, 0, 1);
	{ /* the first samples, the right one first, each big endian in the stream's order */
		uint16_t r = (uint16_t)(int16_t)RIGHT0, l = (uint16_t)LEFT0;
		kp_t_bits(&o, (uint32_t)(r >> 8 | (r & 0xff) << 8), 16);
		kp_t_bits(&o, (uint32_t)(l >> 8 | (l & 0xff) << 8), 16);
	}
	for (i = 0; i < PACKED_FRAMES - 1; i++) {
		kp_t_bits(&o, LEFT_BITS[f % 2][i], 1); /* left low */
		kp_t_bits(&o, LEFT_BITS[f % 2][i], 1); /* left high: the same code, so +1 or -1 */
		/* the right trees are single leaves: no bits */
	}
	out[0] = (uint8_t)(PACKED_FRAMES * 4);
	out[1] = out[2] = out[3] = 0;
	memcpy(out + 4, o.bytes, kp_t_bits_bytes(&o));
	return 4 + kp_t_bits_bytes(&o);
}

/* The samples packed_chunk's frame f decodes to */
static void packed_expected(int f, int16_t *out)
{
	int16_t l = LEFT0, r = RIGHT0;
	int i;
	out[0] = l;
	out[1] = r;
	for (i = 0; i < PACKED_FRAMES - 1; i++) {
		l = (int16_t)(l + (LEFT_BITS[f % 2][i] ? -1 : 1));
		r = (int16_t)(r + 2);
		out[2 + 2 * i] = l;
		out[3 + 2 * i] = r;
	}
}

/* Frames 0 and 1's chunks: a palette record, track 0's packed chunk, track 1's PCM */
static size_t movie_chunks(void *ctx, int f, uint8_t *type, uint8_t *out)
{
	size_t at, n;
	(void)ctx;
	if (f >= 2) return 0;
	*type = 1 | 2 | 4;
	at = palette_record(f, out);
	n = packed_chunk(f, out + at + 4);
	kp_le_put32(out + at, (uint32_t)(4 + n));
	at += 4 + n;
	kp_le_put32(out + at, 4 + sizeof PCM8[f]);
	memcpy(out + at + 4, PCM8[f], sizeof PCM8[f]);
	return at + 4 + sizeof PCM8[f];
}

/* An SMK2 of W x H, FRAMES frames at the cinematics' rate, every pixel FILL, palette records in frames 0 and 1, a
 * packed 16-bit stereo track 0 and an 8-bit mono PCM track 1 in frames 0 and 1. */
static uint8_t *smk_movie(KpT *T, size_t *size)
{
	static const uint32_t TRACKS[2] = { 0xf0000000u | 22050, 0x40000000u | 11025 };
	return kp_t_smk(T, W, H, FRAMES, FILL, TRACKS, movie_chunks, NULL, size);
}

static uint8_t level(uint8_t v) { return (uint8_t)(v << 2 | v >> 4); }

static void smk_palette_records_and_sound(KpT *T)
{
	size_t size, n;
	uint8_t *data = smk_movie(T, &size), *pcm;
	KpSmk s;
	KpSmkSoundFormat f;
	int16_t want[2][PACKED_FRAMES * 2];
	int k;
	kp_t_eq(T, kp_smk_open(&s, data, size), 0, "the movie opens");
	kp_t_eq(T, kp_smk_decode(&s), 0, "frame 0");
	kp_t_eq(T, s.pixels[0], FILL, NULL);
	kp_t_ok(T, s.palette[3 * FILL] == level(COLOUR_A[0]) && s.palette[3 * FILL + 1] == level(COLOUR_A[1])
		&& s.palette[3 * FILL + 2] == level(COLOUR_A[2]), "a new entry's 6-bit levels as 8 bits");
	kp_t_eq(T, s.palette[3 * FILL + 2], 0xff, "63 is 255");
	kp_t_eq(T, s.palette[0], 0, "a skipped entry keeps its colour");
	kp_t_eq(T, kp_smk_decode(&s), 0, "frame 1");
	kp_t_ok(T, s.palette[0] == level(COLOUR_B[0]) && s.palette[1] == level(COLOUR_B[1]), "entry 0 set");
	kp_t_ok(T, s.palette[3 * FILL] == 0 && s.palette[3 * FILL + 2] == 0, "a copy reads the palette before the record");
	kp_t_eq(T, kp_smk_decode(&s), 0, "frame 2");
	kp_t_eq(T, s.palette[0], level(COLOUR_B[0]), "a frame without a record keeps the palette");
	kp_smk_rewind(&s);
	kp_t_eq(T, s.palette[0], 0, "a rewind starts from black");
	kp_t_eq(T, kp_smk_sound_format(&s, 0, &f), 0, NULL);
	kp_t_ok(T, f.rate == 22050 && f.channels == 2 && f.bits == 16 && f.packed, "track 0's format");
	kp_t_eq(T, kp_smk_sound(&s, 0, &f, &pcm, &n), 0, "track 0 decodes");
	kp_t_defer(T, free, pcm);
	kp_t_eq(T, (int64_t)n, 2 * PACKED_FRAMES * 4, "both frames' samples");
	packed_expected(0, want[0]);
	packed_expected(1, want[1]);
	for (k = 0; k < 4 * PACKED_FRAMES; k++) {
		int16_t got = (int16_t)(pcm[2 * k] | pcm[2 * k + 1] << 8);
		kp_t_eq(T, got, want[k / (2 * PACKED_FRAMES)][k % (2 * PACKED_FRAMES)], "sample %d (little endian)", k);
	}
	kp_t_eq(T, kp_smk_sound_format(&s, 1, &f), 0, NULL);
	kp_t_ok(T, f.rate == 11025 && f.channels == 1 && f.bits == 8 && !f.packed, "track 1's format");
	kp_t_eq(T, kp_smk_sound(&s, 1, &f, &pcm, &n), 0, "track 1 reads");
	kp_t_defer(T, free, pcm);
	kp_t_ok(T, n == 10 && memcmp(pcm, PCM8[0], 5) == 0 && memcmp(pcm + 5, PCM8[1], 5) == 0, "the PCM as stored");
	kp_t_eq(T, kp_smk_sound_format(&s, 2, &f), -1, "no track 2");
	{ /* a chunk whose bits run out */
		uint8_t *cut = (uint8_t *)malloc(size);
		KpSmk c;
		if (!cut) kp_t_fail(T, "out of memory");
		kp_t_defer(T, free, cut);
		memcpy(cut, data, size);
		kp_le_put32(cut + 76, 0xf0000000u | 22050); /* track 1 said packed: its PCM bytes are no Huffman stream */
		kp_t_eq(T, kp_smk_open(&c, cut, size), 0, NULL);
		kp_t_eq(T, kp_smk_sound_format(&c, 1, &f), 0, NULL);
		kp_t_eq(T, kp_smk_sound(&c, 1, &f, &pcm, &n), -1, "a malformed chunk refused");
		kp_smk_close(&c);
	}
	kp_smk_close(&s);
}

/* --- a movie in memory played --- */

static void smk_movie_plays(KpT *T)
{
	size_t size;
	uint8_t *data = smk_movie(T, &size), *copy = (uint8_t *)malloc(size);
	KpCineClip named;
	KpCineMovie *m;
	KpCinePlayer *p;
	KpVideoPicture pic;
	const KpCineClip *c;
	const uint8_t *rgba, *wav;
	size_t wav_size;
	KpError err;
	if (!copy) kp_t_fail(T, "out of memory");
	memcpy(copy, data, size);
	memset(&named, 0, sizeof named);
	strcpy(named.name, "smk\\test");
	named.id = 3;
	m = kp_cine_movie_smk(copy, size, &named, &err);
	kp_t_ok(T, m != NULL, "%s", err.msg);
	c = kp_cine_movie_clip(m);
	kp_t_eq_str(T, c->name, "smk\\test", NULL);
	kp_t_eq(T, c->id, 3, NULL);
	kp_t_ok(T, c->width == W && c->height == H && c->shown_w == W && c->shown_h == H, "its size, shown as coded");
	kp_t_ok(T, c->rate_num == 50000 && c->rate_den == 3333, "66.66 ms a frame: %u/%u", c->rate_num, c->rate_den);
	kp_t_eq(T, c->frames, FRAMES, NULL);
	wav = kp_cine_movie_sound(m, &wav_size);
	kp_t_ok(T, wav && wav_size == 44 + 2 * PACKED_FRAMES * 4 && memcmp(wav, "RIFF", 4) == 0 && memcmp(wav + 8, "WAVE", 4) == 0,
		"the first track as a WAV");
	kp_t_ok(T, kp_le_u32(wav + 24) == 22050 && kp_le_u16(wav + 22) == 2 && kp_le_u16(wav + 34) == 16, "its format");
	p = kp_cine_open_movie(m, &err);
	kp_t_ok(T, p != NULL, "%s", err.msg);
	kp_t_eq(T, kp_cine_advance(p, 0, &err), KP_CINE_FRAME, "frame 0 at 0");
	kp_t_eq(T, kp_cine_picture(p, &pic), 0, NULL);
	kp_t_ok(T, pic.srgb && pic.width == W && pic.height == H && pic.chroma_w == W, "G, B, R planes of the frame's size");
	kp_t_ok(T, pic.planes[2][0] == level(COLOUR_A[0]) && pic.planes[0][0] == level(COLOUR_A[1])
		&& pic.planes[1][0] == level(COLOUR_A[2]), "the palette's colour");
	rgba = kp_cine_rgba(p);
	kp_t_ok(T, rgba && rgba[0] == level(COLOUR_A[0]) && rgba[3] == 255, "the same as RGBA");
	kp_t_eq(T, kp_cine_advance(p, 66, &err), KP_CINE_IDLE, "frame 1 is due at 66.66 ms");
	kp_t_eq(T, kp_cine_advance(p, 67, &err), KP_CINE_FRAME, NULL);
	kp_t_eq(T, kp_cine_picture(p, &pic), 0, NULL);
	kp_t_eq(T, pic.planes[2][0], 0, "frame 1's copied black");
	kp_t_eq(T, kp_cine_advance(p, 1000, &err), KP_CINE_FRAME, "frame 2");
	kp_t_eq(T, kp_cine_decoded(p), FRAMES, NULL);
	kp_t_eq(T, kp_cine_advance(p, 1000, &err), KP_CINE_END, "the end once its time has passed");
	kp_cine_free(p);
	kp_cine_movie_free(m);
}

static void webm_movie_plays(KpT *T)
{
	size_t size;
	uint8_t *data;
	KpCineClip named;
	KpCineMovie *m;
	KpCinePlayer *p;
	KpError err;
	uint32_t n, pictures = 0;
	const KpCineClip *c;
	data = kp_t_read(T, KPW_FIXTURE, &size);
	memset(&named, 0, sizeof named);
	strcpy(named.name, "smk\\fixture");
	m = kp_cine_movie_webm(data + KPW_HEADER, size - KPW_HEADER, &named, &err); /* the KPW1's WebM: VP9, no sound */
	kp_t_ok(T, m != NULL, "%s", err.msg);
	c = kp_cine_movie_clip(m);
	kp_t_ok(T, c->width == kp_le_u32(data + 4) && c->height == kp_le_u32(data + 8), "the WebM's size");
	kp_t_ok(T, c->frames > 0 && c->rate_num > 0 && c->rate_den > 0, "frames and a rate");
	kp_t_eq(T, c->audio_size, 0, "no sound");
	kp_t_ok(T, kp_cine_movie_sound(m, &size) == NULL, NULL);
	p = kp_cine_open_movie(m, &err);
	kp_t_ok(T, p != NULL, "%s", err.msg);
	for (n = 0; n < c->frames; n++) {
		int rc = kp_cine_advance(p, kp_cine_frame_ms(c, n), &err);
		kp_t_ok(T, rc >= 0, "frame %u: %s", n, err.msg);
		pictures += rc == KP_CINE_FRAME;
	}
	kp_t_eq(T, pictures, c->frames, "every frame a picture");
	kp_t_eq(T, kp_cine_advance(p, kp_cine_frame_ms(c, c->frames), &err), KP_CINE_END, NULL);
	kp_cine_free(p);
	kp_cine_movie_free(m);
	kp_t_ok(T, kp_cine_movie_webm(data + KPW_HEADER, 100, &named, &err) == NULL, "a cut WebM refused");
}

/* --- subtitles --- */

static const char SRT[] =
	"\xef\xbb\xbf" "1\r\n00:00:02,500 --> 00:00:04,000\r\nSecond\r\n\r\n"
	"2\n00:00:01.000 --> 00:00:02,000  \n  First line  \nsecond line\n \t\n"
	"3\n00:00:05,000 --> 00:00:06,000\n   \n\n"
	"4\n00:00:02,500 --> 00:00:03,000\nSame start, later in the file\n";

#define FIRST_TEXT "  First line\nsecond line"

static void subtitles_of_an_srt(KpT *T)
{
	KpCineSubs *s = (KpCineSubs *)malloc(sizeof *s);
	KpError err;
	if (!s) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, s);
	kp_t_eq(T, kp_cine_subs_srt((const uint8_t *)SRT, sizeof SRT - 1, s, &err), 0, "%s", err.msg);
	kp_t_eq(T, s->count, 3, "the cue without text left out");
	kp_t_ok(T, s->cues[0].start_ms == 1000 && s->cues[0].end_ms == 2000, "by their start ('.' or ',')");
	kp_t_ok(T, s->cues[0].size == strlen(FIRST_TEXT) && memcmp(s->text + s->cues[0].at, FIRST_TEXT, s->cues[0].size) == 0,
		"a block ends at a line of blanks, its lines right-stripped: %.*s", (int)s->cues[0].size, s->text + s->cues[0].at);
	kp_t_ok(T, s->cues[1].start_ms == 2500 && memcmp(s->text + s->cues[1].at, "Second", 6) == 0, "CRLF lines");
	kp_t_ok(T, s->cues[2].start_ms == 2500 && memcmp(s->text + s->cues[2].at, "Same", 4) == 0, "equal starts in the file's order");
	{
		static const char *const BAD[] = { "1\n00:60:00,000 --> 00:61:00,000\nx\n", "1\n00:00:02,000 --> 00:00:01,000\nx\n",
			"x\n00:00:01,000 --> 00:00:02,000\nx\n", "1\n00:00:01 --> 00:00:02,000\nx\n", "1\n\xff\xfe --> \n",
			"1\n00:00:01,000 --> 00:00:02,000\nx\n\n  \nstray\n" /* a blank line splits: a block without its number */ };
		size_t k;
		for (k = 0; k < sizeof BAD / sizeof BAD[0]; k++)
			kp_t_eq(T, kp_cine_subs_srt((const uint8_t *)BAD[k], strlen(BAD[k]), s, &err), -1, "refused: %s", BAD[k]);
	}
	kp_t_eq(T, kp_cine_subs_srt((const uint8_t *)"1\n00:00:01,000 --> 00:00:02,000\n", 32, s, &err), 0, "%s", err.msg);
	kp_t_eq(T, s->count, 0, "times with no text");
}

/* --- the installs --- */

/* The profile's movies (CINM) */
static const KpDefsTable *cinematics_table(KpT *T, KpRes *res)
{
	const KpDefsTable *t = kp_defs_table(kp_res_profile(res), "CINM");
	if (!t) kp_t_fail(T, "no compiled CINM");
	return t;
}

/* Each 1.17 movie read from the install, checked against CINM, decoded: every frame's indices and palette (FFmpeg's
 * pal8: B, G, R, 255 an entry) and its sound as FFmpeg decodes them. */
static void original_smk_movies_decode_as_ffmpeg(KpT *T)
{
	KpRes *res = kp_t_install(T, "KP_TEST_ORIGINAL");
	const KpJson *g = kp_json_get(kp_t_golden(T, FFMPEG_GOLDEN), "movies");
	const KpDefsTable *table = cinematics_table(T, res);
	KpCineCatalog cat;
	int k;
	kp_t_eq(T, kp_cine_catalog(&cat, table->data, table->size), 0, "CINM reads");
	kp_t_eq(T, cat.count, 28, "the original's table");
	for (k = 0; k < cat.count; k++) {
		char name[96], err[256], hex[65];
		uint8_t *data, digest[32], pal[1024];
		size_t size, n, sound_size;
		KpResStatus st;
		KpSmk s;
		KpSha256 sha;
		KpSmkSoundFormat f;
		uint8_t *pcm;
		uint32_t frame;
		const KpJson *rec;
		if (!kp_t_mine(T, k + 1)) continue;
		snprintf(name, sizeof name, "smk\\%s.smk", cat.movies[k].name);
		data = kp_res_read(res, name, &size, &st, err, sizeof err);
		kp_t_ok(T, data != NULL, "%s: %s", name, err);
		kp_t_defer(T, free, data);
		kp_t_eq(T, kp_cine_catalog_check(&cat, k, size), 0, "%s: the reference copy's size", name);
		kp_sha256(data, size, digest);
		kp_t_ok(T, memcmp(digest, cat.movies[k].sha256, sizeof digest) == 0, "%s: the reference copy's SHA-256", name);
		rec = kp_json_get(g, cat.movies[k].name);
		kp_t_ok(T, rec != NULL, "%s in the golden", name);
		kp_t_eq(T, kp_smk_open(&s, data, size), 0, "%s opens", name);
		kp_sha256_init(&sha);
		n = 0;
		for (frame = 0; frame < s.frames; frame++) {
			int i;
			kp_t_eq(T, kp_smk_decode(&s), 0, "%s: frame %u", name, frame);
			for (i = 0; i < 256; i++) {
				pal[4 * i] = s.palette[3 * i + 2];
				pal[4 * i + 1] = s.palette[3 * i + 1];
				pal[4 * i + 2] = s.palette[3 * i];
				pal[4 * i + 3] = 255;
			}
			kp_sha256_update(&sha, s.pixels, (size_t)s.width * s.height);
			kp_sha256_update(&sha, pal, sizeof pal);
			n += (size_t)s.width * s.height + sizeof pal;
		}
		kp_sha256_final(&sha, digest);
		kp_sha256_hex(digest, hex);
		kp_t_eq(T, (int64_t)n, kp_json_int(kp_json_at(kp_json_get(rec, "video"), 0), -1), "%s: the frames' bytes", name);
		kp_t_eq_str(T, hex, kp_json_str(kp_json_at(kp_json_get(rec, "video"), 1), ""), "%s: the frames as FFmpeg's", name);
		kp_t_eq(T, kp_smk_sound_format(&s, 0, &f), 0, "%s has a sound", name);
		kp_t_eq(T, kp_smk_sound(&s, 0, &f, &pcm, &sound_size), 0, "%s: the sound decodes", name);
		kp_sha256(pcm, sound_size, digest);
		free(pcm);
		kp_sha256_hex(digest, hex);
		kp_t_eq(T, (int64_t)sound_size, kp_json_int(kp_json_at(kp_json_get(rec, "sound"), 0), -1), "%s: the sound's bytes", name);
		kp_t_eq_str(T, hex, kp_json_str(kp_json_at(kp_json_get(rec, "sound"), 1), ""), "%s: the sound as FFmpeg's", name);
		kp_smk_close(&s);
	}
}

/* --- a run of an install's movies: the host loads each one, its sound given in memory --- */

typedef struct {
	KpCineRun r;
	uint64_t now;
	int loads, fail_load, sounds, sound_stops, held_at_stop;
	const uint8_t *movie; /* the SMK each load gives */
	size_t movie_size, sound_size;
	char last_log[512];
} OriginalHost;

static uint64_t orig_now(void *ctx) { return ((OriginalHost *)ctx)->now; }
static void orig_begin(void *ctx) { (void)ctx; }
static void orig_end(void *ctx) { (void)ctx; }

static int orig_load(void *ctx, const KpCineClip *clip, KpCineMovie **movie, KpError *err)
{
	OriginalHost *h = (OriginalHost *)ctx;
	uint8_t *copy;
	h->loads++;
	if (h->fail_load) {
		snprintf(err->msg, sizeof err->msg, "cinematics: %s is not there", clip->name);
		return -1;
	}
	if (!(copy = (uint8_t *)malloc(h->movie_size))) {
		snprintf(err->msg, sizeof err->msg, "out of memory");
		return -1;
	}
	memcpy(copy, h->movie, h->movie_size);
	return (*movie = kp_cine_movie_smk(copy, h->movie_size, clip, err)) != NULL ? 0 : -1;
}

static int orig_sound(void *ctx, const KpCineClip *clip, const uint8_t *data, size_t size)
{
	OriginalHost *h = (OriginalHost *)ctx;
	(void)clip;
	h->sounds++;
	h->sound_size = data && memcmp(data, "RIFF", 4) == 0 ? size : 0;
	return 0; /* the wall clock's movie */
}

static int orig_sound_ms(void *ctx, int64_t *ms)
{
	(void)ctx;
	*ms = -1;
	return 0;
}

static void orig_sound_stop(void *ctx)
{
	OriginalHost *h = (OriginalHost *)ctx;
	h->sound_stops++;
	h->held_at_stop += h->r.movie != NULL; /* the sound's bytes are the movie's: it is stopped before they go */
}

static void orig_log(void *ctx, const char *line) { snprintf(((OriginalHost *)ctx)->last_log, 512, "%s", line); }

static int has_even(void *ctx, int k)
{
	(void)ctx;
	return k % 2 == 0;
}

static void run_plays_an_installs_movies(KpT *T)
{
	static const uint8_t TABLE_HEAD[] = { 3, 0, 1, 0 };
	uint8_t table[4 + 3 * (1 + 2 + 4 + 32) + 2];
	KpCineCatalog cat;
	OriginalHost *h = (OriginalHost *)calloc(1, sizeof *h);
	KpCineHost *c;
	KpVideoPicture pic;
	size_t at = sizeof TABLE_HEAD;
	int k, ids[1] = { 2 }, frames = 0;
	if (!h) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, h);
	memcpy(table, TABLE_HEAD, sizeof TABLE_HEAD);
	for (k = 0; k < 3; k++) { /* movies M0, M1, M2 */
		table[at++] = 2;
		table[at++] = 'M';
		table[at++] = (uint8_t)('0' + k);
		kp_le_put32(table + at, (uint32_t)k);
		memset(table + at + 4, k, 32);
		at += 4 + 32;
	}
	table[at++] = 2; /* M2 after its prelude M0 */
	table[at++] = 0;
	kp_t_eq(T, kp_cine_catalog(&cat, table, at), 0, "the table reads");
	kp_t_ok(T, cat.count == 3 && strcmp(cat.movies[2].name, "M2") == 0 && cat.prelude_count == 1, NULL);
	{
		KpCineCatalog cut;
		kp_t_eq(T, kp_cine_catalog(&cut, table, at - 1), -1, "a table cut short");
		table[at - 2] = 3;
		kp_t_eq(T, kp_cine_catalog(&cut, table, at), -1, "a prelude of a movie past the table's");
		table[at - 2] = 2;
	}
	kp_t_eq(T, kp_cine_catalog_check(&cat, 0, cat.movies[0].size), 0, "a copy of the reference's size");
	kp_t_eq(T, kp_cine_catalog_check(&cat, 0, cat.movies[0].size + 1), -1, "a copy of another size than the reference's");
	kp_t_eq(T, kp_cine_catalog_check(&cat, cat.count, cat.movies[0].size), -1, "a movie past the table");
	kp_cine_index_movies(&h->r.index, 0, &cat, has_even, NULL);
	kp_t_eq(T, h->r.index.count, 2, "the movies the install has");
	kp_t_eq_str(T, h->r.index.clips[1].name, "smk\\m2", "named in lower case");
	kp_t_eq(T, h->r.index.clips[1].id, 2, "numbered as the table");
	kp_t_eq(T, kp_cine_prelude(&h->r.index, 2), 0, NULL);
	h->movie = smk_movie(T, &h->movie_size);
	h->r.have = 1;
	h->now = 1;
	c = &h->r.host;
	c->ctx = h;
	c->now_ns = orig_now;
	c->begin = orig_begin;
	c->end = orig_end;
	c->load = orig_load;
	c->sound = orig_sound;
	c->sound_ms = orig_sound_ms;
	c->sound_stop = orig_sound_stop;
	c->log = orig_log;
	kp_t_eq(T, kp_cine_run_start(&h->r, ids, 1, 1), 1, "the prelude and its movie");
	kp_t_ok(T, h->loads == 1 && h->sounds == 1, "the prelude loaded, its sound given");
	kp_t_eq(T, (int64_t)h->sound_size, 44 + 2 * PACKED_FRAMES * 4, "a WAV in memory");
	for (k = 0; k < 400 && h->r.active; k++) {
		h->now += 10000000u;
		if (kp_cine_run_frame(&h->r, &pic)) {
			kp_cine_run_shown(&h->r);
			frames++;
			kp_t_ok(T, frames > 1 || (pic.srgb && pic.width == W && pic.planes[2][0] == level(COLOUR_A[0])), "frame 0's colour");
		}
	}
	kp_t_ok(T, !h->r.active && h->loads == 2 && frames == 2 * FRAMES, "both played: %d pictures", frames);
	kp_t_ok(T, h->sound_stops == 2 && h->held_at_stop == 2, "each sound stopped before its movie went");
	h->fail_load = 1;
	h->loads = 0;
	kp_t_eq(T, kp_cine_run_start(&h->r, ids, 1, 0), 0, "a movie that does not load: nothing plays");
	kp_t_ok(T, h->loads == 1 && strstr(h->last_log, "is not there") != NULL, "logged: %s", h->last_log);
}

static const KpTest TESTS[] = {
	{ "smk_palette_records_and_sound", smk_palette_records_and_sound, 0 },
	{ "smk_movie_plays", smk_movie_plays, 0 },
	{ "webm_movie_plays", webm_movie_plays, 0 },
	{ "subtitles_of_an_srt", subtitles_of_an_srt, 0 },
	{ "run_plays_an_installs_movies", run_plays_an_installs_movies, 0 },
	{ "original_smk_movies_decode_as_ffmpeg", original_smk_movies_decode_as_ffmpeg, 8 },
};

KP_SUITE(kp_suite_movies, "test_movies", TESTS);
