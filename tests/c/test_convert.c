/*
 * kp_convert's own code (ADR 0019, stage C1): a WAV as Ogg Vorbis (decoded back), the repack of a test profile's install
 * read back through kp_res, the groups' names, the test scenarios' CHK writer, the icon from an executable's resources,
 * the files and the workers. Built with KP_ENCODERS only (the host's tools, CMakeLists.txt); elsewhere its tests skip.
 */
#include "kp_test.h"

#ifdef KP_ENCODERS
#include <math.h>
#include <stdio.h>

#include <vorbis/codec.h>

#include "kp_cine.h"
#include "kp_fs.h"
#include "kp_icon.h"
#include "kp_json.h"
#include "kp_kpf.h"
#include "kp_movie.h"
#include "kp_mpq.h"
#include "kp_ogg.h"
#include "kp_repack.h"
#include "kp_res.h"
#include "kp_scenario.h"
#include "kp_sha256.h"
#include "kp_test_app.h"
#include "kp_webm.h"
#include "kp_webm_write.h"
#include "kp_work.h"
#include "zlib.h"

#define SOUND_RATE 22050
#define SOUND_FRAMES (SOUND_RATE + 37) /* not a multiple of the 1024 samples fed at a time: the stream's end is flushed */
#define SOUND_SNR_FLOOR 30.0           /* 34.0 dB measured */
#define WAV_HEADER 44

/* --- WAV files --- */

static float sound_sample(uint32_t i)
{
	return (float)(0.4 * sin(2.0 * 3.14159265358979 * 440.0 * i / SOUND_RATE)
		+ 0.1 * sin(2.0 * 3.14159265358979 * 1250.0 * i / SOUND_RATE));
}

/* A PCM WAV of frames samples a channel (format 1; 3 is IEEE float, which the encoder refuses), the sound's samples on
 * every channel: malloc'd, released at the end. */
static uint8_t *wav_new(KpT *T, uint32_t frames, int channels, int bits, int format, size_t *size)
{
	uint32_t block = (uint32_t)(channels * bits / 8), data = frames * block, i;
	uint8_t *w = (uint8_t *)malloc(WAV_HEADER + (size_t)data);
	int c;
	kp_t_ok(T, w != NULL, "memory");
	kp_t_defer(T, free, w);
	memcpy(w, "RIFF", 4);
	kp_le_put32(w + 4, 36 + data);
	memcpy(w + 8, "WAVEfmt ", 8);
	kp_le_put32(w + 16, 16);
	kp_le_put16(w + 20, (uint32_t)format);
	kp_le_put16(w + 22, (uint32_t)channels);
	kp_le_put32(w + 24, SOUND_RATE);
	kp_le_put32(w + 28, SOUND_RATE * block);
	kp_le_put16(w + 32, block);
	kp_le_put16(w + 34, (uint32_t)bits);
	memcpy(w + 36, "data", 4);
	kp_le_put32(w + 40, data);
	for (i = 0; i < frames; i++)
		for (c = 0; c < channels; c++) {
			uint8_t *s = w + WAV_HEADER + (size_t)i * block + (size_t)c * (size_t)(bits / 8);
			if (bits == 8) s[0] = (uint8_t)(128 + (int)lrintf(sound_sample(i) * 127.0f));
			else kp_le_put16(s, (uint32_t)(int32_t)lrintf(sound_sample(i) * 32767.0f));
		}
	*size = WAV_HEADER + (size_t)data;
	return w;
}

/* An Ogg Vorbis stream decoded: its channels, its serial and its first channel's samples against the sound's (the SNR,
 * in dB); every sample comes back, no more, and the last page ends the stream at their count. */
static double ogg_decode(KpT *T, const uint8_t *ogg, size_t size, int channels, uint32_t serial)
{
	ogg_sync_state oy;
	ogg_stream_state os;
	ogg_page og;
	ogg_packet op;
	vorbis_info vi;
	vorbis_comment vc;
	vorbis_dsp_state vd;
	vorbis_block vb;
	int headers = 0, last_page = 0;
	int64_t granule = -1;
	uint32_t decoded = 0;
	double signal = 0, noise = 0;
	char *buf;
	ogg_sync_init(&oy);
	buf = ogg_sync_buffer(&oy, (long)size);
	memcpy(buf, ogg, size);
	ogg_sync_wrote(&oy, (long)size);
	vorbis_info_init(&vi);
	vorbis_comment_init(&vc);
	ogg_stream_init(&os, (int)serial);
	while (ogg_sync_pageout(&oy, &og) == 1) {
		kp_t_eq(T, ogg_page_serialno(&og), (int)serial, "the stream's serial");
		kp_t_ok(T, !last_page, "no page after the last");
		last_page = ogg_page_eos(&og);
		granule = ogg_page_granulepos(&og);
		ogg_stream_pagein(&os, &og);
		while (ogg_stream_packetout(&os, &op) == 1) {
			float **pcm;
			int n;
			if (headers < 3) {
				kp_t_eq(T, vorbis_synthesis_headerin(&vi, &vc, &op), 0, "header %d", headers);
				if (++headers == 3) {
					kp_t_eq(T, vi.channels, channels, "channels");
					kp_t_eq(T, vi.rate, SOUND_RATE, "rate");
					vorbis_synthesis_init(&vd, &vi);
					vorbis_block_init(&vd, &vb);
				}
				continue;
			}
			kp_t_eq(T, vorbis_synthesis(&vb, &op), 0, "a packet decodes");
			vorbis_synthesis_blockin(&vd, &vb);
			while ((n = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
				int i;
				for (i = 0; i < n; i++, decoded++) {
					double want = decoded < SOUND_FRAMES ? sound_sample(decoded) : 0.0, d = pcm[0][i] - want;
					signal += want * want;
					noise += d * d;
				}
				vorbis_synthesis_read(&vd, n);
			}
		}
	}
	kp_t_eq(T, headers, 3, "the three headers");
	vorbis_block_clear(&vb);
	vorbis_dsp_clear(&vd);
	vorbis_comment_clear(&vc);
	vorbis_info_clear(&vi);
	ogg_stream_clear(&os);
	ogg_sync_clear(&oy);
	kp_t_ok(T, last_page, "the last page ends the stream");
	kp_t_eq(T, granule, SOUND_FRAMES, "the last page's granule is the sample count");
	kp_t_eq(T, decoded, SOUND_FRAMES, "every sample decodes back, no more");
	return 10.0 * log10(signal / (noise > 0 ? noise : 1e-30));
}

/* A WAV as Ogg Vorbis at the repack's quality: 16-bit mono and 8-bit stereo decode back to their sound, the same WAV
 * gives the same bytes (the repack's cache relies on it), and a WAV of float samples is refused. */
static void ogg_from_wav_decodes(KpT *T)
{
	static const struct {
		int channels, bits;
		double floor; /* 8-bit samples add their own noise */
	} CASES[] = { { 1, 16, SOUND_SNR_FLOOR }, { 2, 8, 25.0 } };
	uint32_t serial = kp_ogg_serial("sound/misc/test.wav");
	char err[256];
	size_t k;
	for (k = 0; k < sizeof CASES / sizeof CASES[0]; k++) {
		size_t wav_size = 0, a_size = 0, b_size = 0;
		const uint8_t *wav = wav_new(T, SOUND_FRAMES, CASES[k].channels, CASES[k].bits, 1, &wav_size);
		uint8_t *a = kp_ogg_from_wav(wav, wav_size, KP_OGG_QUALITY, serial, &a_size, err, sizeof err);
		uint8_t *b = kp_ogg_from_wav(wav, wav_size, KP_OGG_QUALITY, serial, &b_size, err, sizeof err);
		double snr;
		kp_t_ok(T, a != NULL && b != NULL, "%d-bit, %d channels: %s", CASES[k].bits, CASES[k].channels, err);
		kp_t_defer(T, free, a);
		kp_t_defer(T, free, b);
		kp_t_ok(T, a_size == b_size && memcmp(a, b, a_size) == 0, "the same samples give the same bytes");
		kp_t_ok(T, a_size < wav_size, "smaller than the WAV (%lu bytes, %lu)", (unsigned long)a_size, (unsigned long)wav_size);
		snr = ogg_decode(T, a, a_size, CASES[k].channels, serial);
		kp_t_ok(T, snr >= CASES[k].floor, "%d-bit: SNR %.1f dB at least %.1f", CASES[k].bits, snr, CASES[k].floor);
	}
	{
		size_t wav_size = 0, size = 0;
		const uint8_t *wav = wav_new(T, 100, 1, 32, 3, &wav_size);
		err[0] = 0;
		kp_t_ok(T, kp_ogg_from_wav(wav, wav_size, KP_OGG_QUALITY, serial, &size, err, sizeof err) == NULL && err[0],
			"float samples refused: %s", err);
	}
}

/* The serial is the CRC-32 of the normalized name with the top bit clear (oggenc's -s takes a non-negative one). */
static void ogg_serial_is_the_names_crc(KpT *T)
{
	static const char *const NAMES[] = { "sound/misc/buzz.wav", "campaign/terran/terran01/staredit/wav/x.wav", "" };
	size_t k;
	for (k = 0; k < sizeof NAMES / sizeof NAMES[0]; k++) {
		uint32_t crc = (uint32_t)crc32(0, (const Bytef *)NAMES[k], (uInt)strlen(NAMES[k]));
		kp_t_eq(T, kp_ogg_serial(NAMES[k]), crc & 0x7fffffffu, "%s", NAMES[k]);
	}
}

/* --- the repack of a test profile's install --- */

#define FILES 4

typedef struct {
	KpDefsProfile profile;
	KpDefsInput files[FILES];
	KpDefsTable tables[1];
	uint8_t cinm[4 + 1 + 4 + 4 + KP_SHA256_SIZE];
	char hex[FILES][2 * KP_SHA256_SIZE + 1];
	char log[4096];
	const uint8_t *movie; /* the install's smk\test.smk */
	size_t movie_size;
} Profile;

static const KpDefsArchive ARCHIVES[] = { { "A.mpq", 0, "core" } };
static const char ONE[] = "one, in the core", CHK[] = "a mission's map";
#define TEST_MOVIE_W 16 /* the install's movie: 16 x 8, 3 frames of one colour, no sound */
#define TEST_MOVIE_H 8
#define TEST_MOVIE_FRAMES 3
#define TEST_MOVIE_FILL 7
#define TEST_MOVIE_WEBM "sd/smk/test.webm" /* where the repack keeps it (Remastered's SD set's name) */

static void profile_log(void *ctx, const char *line)
{
	Profile *p = (Profile *)ctx;
	size_t n = strlen(p->log);
	snprintf(p->log + n, sizeof p->log - n, "%s\n", line);
}

static void res_close(void *p) { kp_res_close((KpRes *)p); }

static KpRes *open_res(KpT *T, Profile *p, const char *dir)
{
	KpResConfig c;
	char err[512];
	KpRes *r;
	memset(&c, 0, sizeof c);
	c.profile = &p->profile;
	c.log = profile_log;
	c.log_ctx = p;
	r = kp_res_open(&dir, 1, &c, err, sizeof err);
	kp_t_ok(T, r != NULL, "%s: %s", dir, err);
	kp_t_defer(T, res_close, r);
	return r;
}

static void repack_log(void *ctx, const char *line)
{
	profile_log(ctx, line);
}

/* The test profile (its manifest: a core file, a mission's map and speech, a track; its movies' catalog: "test", a
 * Smacker movie) and its install, A.mpq and Game.exe, in dir. The speech is long enough that its Ogg is smaller; the track so short that
 * its Ogg is not. */
static void test_install(KpT *T, Profile *p, const char *dir, const uint8_t *speech, size_t speech_size,
	const uint8_t *track, size_t track_size)
{
	static const char *const NAMES[FILES] = { "a/one.dat", "campaign/terran/terran01/staredit/scenario.chk",
		"campaign/terran/terran01/staredit/wav/x.wav", "music/t.wav" };
	static const char *const STORED[FILES] = { "a\\one.dat", "campaign\\terran\\terran01\\staredit\\scenario.chk",
		"campaign\\terran\\terran01\\staredit\\wav\\x.wav", "music\\t.wav" };
	const uint8_t *data[FILES];
	size_t sizes[FILES];
	uint8_t digest[KP_SHA256_SIZE];
	char path[KP_T_PATH], err[512];
	KpMpq *m;
	FILE *f;
	int k;
	data[0] = (const uint8_t *)ONE;
	sizes[0] = strlen(ONE);
	data[1] = (const uint8_t *)CHK;
	sizes[1] = strlen(CHK);
	data[2] = speech;
	sizes[2] = speech_size;
	data[3] = track;
	sizes[3] = track_size;
	memset(p, 0, sizeof *p);
	p->movie = kp_t_smk(T, TEST_MOVIE_W, TEST_MOVIE_H, TEST_MOVIE_FRAMES, TEST_MOVIE_FILL, NULL, NULL, NULL, &p->movie_size);
	for (k = 0; k < FILES; k++) { /* sorted by name */
		kp_sha256(data[k], sizes[k], digest);
		kp_sha256_hex(digest, p->hex[k]);
		p->files[k].name = NAMES[k];
		p->files[k].size = (uint32_t)sizes[k];
		p->files[k].sha256 = p->hex[k];
	}
	/* CINM: one movie, no preludes */
	kp_le_put16(p->cinm, 1);
	kp_le_put16(p->cinm + 2, 0);
	p->cinm[4] = 4;
	memcpy(p->cinm + 5, "test", 4);
	kp_le_put32(p->cinm + 9, (uint32_t)p->movie_size);
	kp_sha256(p->movie, p->movie_size, p->cinm + 13);
	memcpy(p->tables[0].fourcc, "CINM", 5);
	p->tables[0].data = p->cinm;
	p->tables[0].size = (uint32_t)sizeof p->cinm;
	p->profile.id = "test-1";
	p->profile.executable = "Game.exe";
	p->profile.archives = ARCHIVES;
	p->profile.archive_count = 1;
	p->profile.files = p->files;
	p->profile.file_count = FILES;
	p->profile.tables = p->tables;
	p->profile.table_count = 1;
	snprintf(path, sizeof path, "%s/Game.exe", dir);
	kp_t_ok(T, (f = fopen(path, "wb")) != NULL && fwrite("MZ", 1, 2, f) == 2 && fclose(f) == 0, "cannot write %s", path);
	snprintf(path, sizeof path, "%s/A.mpq", dir);
	kp_t_ok(T, (m = kp_mpq_create(path, 16, err, sizeof err)) != NULL, "%s", err);
	for (k = 0; k < FILES; k++)
		kp_t_ok(T, kp_mpq_add(m, STORED[k], data[k], sizes[k], KP_MPQ_ZLIB, err, sizeof err) == 0, "%s", err);
	kp_t_ok(T, kp_mpq_add(m, "smk\\test.smk", p->movie, p->movie_size, KP_MPQ_STORE, err, sizeof err) == 0, "%s", err);
	kp_t_eq(T, kp_mpq_close(m), 0, "A.mpq written");
}

/* A file read through kp_res: its bytes (released at the end) and status. */
static const uint8_t *read_back(KpT *T, KpRes *r, const char *name, size_t *size, KpResStatus *st)
{
	char err[512];
	uint8_t *b = kp_res_read(r, name, size, st, err, sizeof err);
	kp_t_ok(T, b != NULL, "%s: %s", name, err);
	kp_t_defer(T, free, b);
	return b;
}

/* The test install's movie as the repack keeps it: a WebM the player takes, of the SMK's size, rate and frames. */
static void test_movie_is_its_webm(KpT *T, const uint8_t *webm, size_t size)
{
	KpCineClip named;
	KpCineMovie *m;
	KpError e;
	memset(&named, 0, sizeof named);
	snprintf(named.name, sizeof named.name, "smk\\test");
	kp_t_ok(T, size > 4 && memcmp(webm, "\x1a\x45\xdf\xa3", 4) == 0, "a WebM (EBML) of %lu bytes", (unsigned long)size);
	kp_t_ok(T, (m = kp_cine_movie_webm(webm, size, &named, &e)) != NULL, "the player takes it: %s", e.msg);
	kp_t_ok(T, kp_cine_movie_clip(m)->width == TEST_MOVIE_W && kp_cine_movie_clip(m)->height == TEST_MOVIE_H
		&& kp_cine_movie_clip(m)->frames == TEST_MOVIE_FRAMES && kp_cine_movie_clip(m)->rate_num == 50000
		&& kp_cine_movie_clip(m)->rate_den == 3333 && kp_cine_movie_clip(m)->audio_size == 0,
		"%ux%u, %u frames at %u/%u, no sound", kp_cine_movie_clip(m)->width, kp_cine_movie_clip(m)->height,
		kp_cine_movie_clip(m)->frames, kp_cine_movie_clip(m)->rate_num, kp_cine_movie_clip(m)->rate_den);
	kp_cine_movie_free(m);
}

/* An install's repack in groups, read back through kp_res alone: every file, the speech as Ogg Vorbis whose header
 * the listing answers, the short track as its WAV, the movie transcoded into WebM in the cinematics' group (status
 * converted). A second run, the single repack (one koprulu.mpq and the cinematics' group), takes the Oggs and the
 * movie from the cache. */
static void repack_reads_back(KpT *T)
{
	static const char *const GROUP_FILES[] = { "koprulu-cinematics.mpq", "koprulu-core.mpq", "koprulu-music.mpq",
		"koprulu-terran-terran01.mpq" };
	const char *root = kp_t_temp_dir(T, NULL);
	char install[KP_T_PATH], out[KP_T_PATH], cache[KP_T_PATH], single[KP_T_PATH], err[512], **names;
	size_t speech_size = 0, track_size = 0, size = 0, got = 0;
	const uint8_t *speech = wav_new(T, SOUND_FRAMES, 1, 16, 1, &speech_size), *track = wav_new(T, 10, 1, 16, 1, &track_size), *b;
	uint64_t total = 0;
	KpRepackConfig c;
	KpRepackReport rep;
	KpResStatus st;
	Profile p;
	KpRes *r;
	uint8_t *head;
	int count = 0, k;
	snprintf(install, sizeof install, "%s/install", root);
	snprintf(out, sizeof out, "%s/out", root);
	snprintf(cache, sizeof cache, "%s/cache", root);
	snprintf(single, sizeof single, "%s/single", root);
	kp_t_eq(T, kp_t_mkdir(install), 0, "%s", install);
	test_install(T, &p, install, speech, speech_size, track, track_size);
	r = open_res(T, &p, install);
	memset(&c, 0, sizeof c);
	c.out = out;
	c.cache = cache;
	c.threads = 2;
	c.log = repack_log;
	c.log_ctx = &p;
	kp_t_eq(T, kp_repack_write(r, &c, &rep, err, sizeof err), 0, "%s", err);
	kp_t_eq(T, rep.group_count, 4, "the core, the mission's, the music's, the cinematics' groups");
	kp_t_eq(T, rep.ogg, 1, "the speech as Ogg Vorbis, the short track as its WAV");
	kp_t_eq(T, rep.encoded, 3, "both sounds encoded (the track's Ogg is the larger), and the movie");
	kp_t_eq(T, rep.missing + rep.damaged, 0, "every file there");
	kp_t_ok(T, rep.movies == 1 && rep.movie_from == p.movie_size && rep.movie_bytes > 0, "the movie as WebM");
	kp_t_eq(T, rep.statuses[KP_RES_CONVERTED], 1, "the movie converted");
	kp_t_ok(T, (names = kp_fs_list(out, ".mpq", &count, err, sizeof err)) != NULL, "%s", err);
	kp_t_eq(T, count, 4, "four archives");
	for (k = 0; k < count; k++) {
		kp_t_eq_str(T, names[k], GROUP_FILES[k], "archive %d", k);
		free(names[k]);
	}
	free(names);

	r = open_res(T, &p, out);
	kp_t_eq(T, kp_res_install(r), KP_RES_SOURCE_NONE, "the repack alone");
	b = read_back(T, r, "a/one.dat", &size, &st);
	kp_t_ok(T, size == strlen(ONE) && memcmp(b, ONE, size) == 0 && st == KP_RES_REPACKED, "one.dat: status %s",
		kp_res_status_name(st));
	b = read_back(T, r, "campaign\\terran\\terran01\\staredit\\scenario.chk", &size, &st);
	kp_t_ok(T, size == strlen(CHK) && memcmp(b, CHK, size) == 0, "the map");
	b = read_back(T, r, "music\\t.wav", &size, &st);
	kp_t_ok(T, size == track_size && memcmp(b, track, size) == 0 && st == KP_RES_REPACKED, "the track as its WAV");
	{ /* the movie by its WebM name, its group open: read whole, not by a range */
		char file[64];
		uint64_t offset;
		uint32_t ranged;
		uint8_t *webm = kp_res_raw_read(r, TEST_MOVIE_WEBM, &size, err, sizeof err);
		kp_t_ok(T, webm != NULL, "%s: %s", TEST_MOVIE_WEBM, err);
		kp_t_defer(T, free, webm);
		test_movie_is_its_webm(T, webm, size);
		kp_t_eq(T, kp_res_raw_range(r, TEST_MOVIE_WEBM, file, sizeof file, &offset, &ranged), 0, "its group is open");
		kp_t_eq(T, kp_res_exists(r, "smk\\test.smk"), 0, "no SMK in the repack");
	}
	b = read_back(T, r, "campaign\\terran\\terran01\\staredit\\wav\\x.wav", &size, &st);
	kp_t_ok(T, size > 4 && memcmp(b, "OggS", 4) == 0 && size < speech_size, "the speech as Ogg Vorbis (%lu bytes)",
		(unsigned long)size);
	kp_t_eq(T, kp_le_u32(b + 14), kp_ogg_serial("campaign/terran/terran01/staredit/wav/x.wav"), "its serial");
	head = kp_res_head(r, "campaign\\terran\\terran01\\staredit\\wav\\x.wav", KP_REPACK_WAV_HEAD, &got, &total, err, sizeof err);
	kp_t_ok(T, head != NULL, "%s", err);
	kp_t_defer(T, free, head);
	kp_t_ok(T, got == WAV_HEADER && memcmp(head, speech, WAV_HEADER) == 0, "the WAV's header: %lu bytes", (unsigned long)got);
	kp_t_eq(T, (int64_t)total, (int64_t)speech_size, "the WAV's size");

	/* into a folder of its own: Windows removes no file a reader holds open, and the repack read above is still open */
	r = open_res(T, &p, install);
	c.out = single;
	c.single = 1;
	kp_t_eq(T, kp_repack_write(r, &c, &rep, err, sizeof err), 0, "%s", err);
	kp_t_ok(T, rep.ogg == 1 && rep.encoded == 0, "the Oggs from the cache (%d encoded)", rep.encoded);
	kp_t_eq(T, rep.movies, 1, "the movie (from the cache)");
	kp_t_eq(T, rep.group_count, 2, "koprulu.mpq and the cinematics' group");
	kp_t_ok(T, strcmp(rep.groups[0].file, "koprulu.mpq") == 0 || strcmp(rep.groups[1].file, "koprulu.mpq") == 0, "%s, %s",
		rep.groups[0].file, rep.groups[1].file);
	r = open_res(T, &p, single);
	b = read_back(T, r, "music\\t.wav", &size, &st);
	kp_t_ok(T, size == track_size && st == KP_RES_REPACKED, "the single repack's track");
}

/* A repack is made from an install, not from a repack. */
static void repack_needs_an_install(KpT *T)
{
	const char *root = kp_t_temp_dir(T, NULL);
	char install[KP_T_PATH], out[KP_T_PATH], again[KP_T_PATH], err[512];
	size_t speech_size = 0, track_size = 0;
	const uint8_t *speech = wav_new(T, 10, 1, 16, 1, &speech_size), *track = wav_new(T, 10, 1, 16, 1, &track_size);
	KpRepackConfig c;
	KpRepackReport rep;
	Profile p;
	snprintf(install, sizeof install, "%s/install", root);
	snprintf(out, sizeof out, "%s/out", root);
	snprintf(again, sizeof again, "%s/again", root);
	kp_t_eq(T, kp_t_mkdir(install), 0, "%s", install);
	test_install(T, &p, install, speech, speech_size, track, track_size);
	memset(&c, 0, sizeof c);
	c.out = out;
	c.wav = 1;
	c.threads = 1;
	kp_t_eq(T, kp_repack_write(open_res(T, &p, install), &c, &rep, err, sizeof err), 0, "%s", err);
	kp_t_eq(T, rep.ogg, 0, "--wav keeps the WAVs");
	c.out = again;
	kp_t_eq(T, kp_repack_write(open_res(T, &p, out), &c, &rep, err, sizeof err), -1, "a repack of a repack");
	kp_t_ok(T, strstr(err, "from an install") != NULL, "%s", err);
}

/* The movies' group is read by ranges (the web, ADR 0019 stage C4): the core's listing places each movie in its group's
 * file, stored as it is, so that while the group is not open the bytes to fetch are named; once it is added the movie
 * is read whole. A stored file has a place in its archive, a compressed one none (kp_mpq_stored_at). */
static void repack_movies_read_by_ranges(KpT *T)
{
	const char *root = kp_t_temp_dir(T, NULL);
	char install[KP_T_PATH], out[KP_T_PATH], core[KP_T_PATH], path[KP_T_PATH], file[64], err[512];
	size_t speech_size = 0, track_size = 0, size = 0, n = 0, refused = 0, got = 0;
	const uint8_t *speech = wav_new(T, 10, 1, 16, 1, &speech_size), *track = wav_new(T, 10, 1, 16, 1, &track_size), *b;
	uint8_t *webm, *again;
	uint64_t offset = 0;
	uint32_t ranged = 0;
	KpRepackConfig c;
	KpRepackReport rep;
	Profile p;
	KpRes *r;
	KpMpq *m;
	snprintf(install, sizeof install, "%s/install", root);
	snprintf(out, sizeof out, "%s/out", root);
	snprintf(core, sizeof core, "%s/core", root);
	kp_t_eq(T, kp_t_mkdir(install), 0, "%s", install);
	kp_t_eq(T, kp_t_mkdir(core), 0, "%s", core);
	test_install(T, &p, install, speech, speech_size, track, track_size);

	snprintf(path, sizeof path, "%s/A.mpq", install);
	kp_t_ok(T, (m = kp_mpq_open(path, 0, err, sizeof err)) != NULL, "%s", err);
	kp_t_eq(T, kp_mpq_stored_at(m, "smk\\test.smk", &offset), 0, "the stored movie's place");
	kp_t_eq(T, kp_mpq_stored_at(m, "a\\one.dat", &offset), -1, "a compressed file has none");
	kp_t_eq(T, kp_mpq_stored_at(m, "a\\none.dat", &offset), -1, "nor a file the archive lacks");
	kp_t_eq(T, kp_mpq_stored_at(m, "smk\\test.smk", &offset), 0, NULL);
	kp_mpq_close(m);
	b = kp_t_read(T, path, &n);
	kp_t_ok(T, offset + p.movie_size <= n && memcmp(b + offset, p.movie, p.movie_size) == 0, "the bytes at %llu are the movie",
		(unsigned long long)offset);

	memset(&c, 0, sizeof c);
	c.out = out;
	c.wav = 1;
	c.threads = 1;
	kp_t_eq(T, kp_repack_write(open_res(T, &p, install), &c, &rep, err, sizeof err), 0, "%s", err);
	r = open_res(T, &p, out);
	webm = kp_res_raw_read(r, TEST_MOVIE_WEBM, &size, err, sizeof err);
	kp_t_ok(T, webm != NULL, "%s: %s", TEST_MOVIE_WEBM, err);
	kp_t_defer(T, free, webm);

	/* the core alone: the movie listed, its range named, not read */
	snprintf(path, sizeof path, "%s/koprulu-core.mpq", out);
	b = kp_t_read(T, path, &n);
	snprintf(path, sizeof path, "%s/koprulu-core.mpq", core);
	kp_t_eq(T, kp_fs_write(path, b, n, err, sizeof err), 0, "%s", err);
	r = open_res(T, &p, core);
	kp_t_ok(T, kp_res_raw_held(r, TEST_MOVIE_WEBM), "the movie is listed");
	kp_t_eq(T, kp_res_raw_range(r, TEST_MOVIE_WEBM, file, sizeof file, &offset, &ranged), 1, "its range");
	kp_t_eq_str(T, file, "koprulu-cinematics.mpq", "in the cinematics' group");
	kp_t_eq(T, (int64_t)ranged, (int64_t)size, "the movie's size");
	snprintf(path, sizeof path, "%s/koprulu-cinematics.mpq", out);
	b = kp_t_read(T, path, &n);
	kp_t_ok(T, offset + ranged <= n && memcmp(b + offset, webm, size) == 0, "the group's bytes at %llu are the movie",
		(unsigned long long)offset);
	kp_t_ok(T, kp_res_raw_read(r, TEST_MOVIE_WEBM, &refused, err, sizeof err) == NULL, "not read while its group is not open");
	kp_t_eq(T, kp_res_raw_range(r, "sd/smk/none.webm", file, sizeof file, &offset, &ranged), 0, "a movie not listed");
	kp_t_eq(T, kp_res_raw_range(r, "a/one.dat", file, sizeof file, &offset, &ranged), 0, "a core file is no range");

	/* the group added (the web, once fetched): read whole */
	snprintf(path, sizeof path, "%s/koprulu-cinematics.mpq", core);
	kp_t_eq(T, kp_fs_write(path, b, n, err, sizeof err), 0, "%s", err);
	kp_t_eq(T, kp_res_add_repack(r, path, err, sizeof err), 0, "%s", err);
	kp_t_eq(T, kp_res_raw_range(r, TEST_MOVIE_WEBM, file, sizeof file, &offset, &ranged), 0, "its group open");
	again = kp_res_raw_read(r, TEST_MOVIE_WEBM, &got, err, sizeof err);
	kp_t_ok(T, again != NULL, "%s", err);
	kp_t_defer(T, free, again);
	kp_t_ok(T, got == size && memcmp(again, webm, got) == 0, "the movie from its group");
}

/* A name's group: a mission directory's own, the music's, the cinematics', the core's. */
static void repack_groups_by_name(KpT *T)
{
	static const struct {
		const char *key, *file, *prefix;
	} CASES[] = {
		{ "campaign/terran/terran01/staredit/scenario.chk", "koprulu-terran-terran01.mpq", "campaign/terran/terran01/" },
		{ "campaign/expprotoss/protoss01/staredit/wav/a.wav", "koprulu-expprotoss-protoss01.mpq",
			"campaign/expprotoss/protoss01/" },
		{ "campaign/staredit/x.wav", "koprulu-core.mpq", "" }, /* no mission directory between */
		{ "campaign/terran/readme.txt", "koprulu-core.mpq", "" },
		{ "music/terran1.wav", "koprulu-music.mpq", "music/" },
		{ "sd/smk/blizzard.webm", "koprulu-cinematics.mpq", "sd/smk/" },
		{ "hd2/smk/blizzard.webm", "koprulu-cinematics-hd2.mpq", "hd2/smk/" },
		{ "smk/blizzard.smk", "koprulu-core.mpq", "" }, /* a 1.17 movie goes in as WebM, by Remastered's SD name */
		{ "arr/units.dat", "koprulu-core.mpq", "" },
		{ "", "koprulu-core.mpq", "" },
	};
	char file[64], prefix[256];
	size_t k;
	for (k = 0; k < sizeof CASES / sizeof CASES[0]; k++) {
		kp_repack_group(CASES[k].key, file, sizeof file, prefix, sizeof prefix);
		kp_t_eq_str(T, file, CASES[k].file, "%s", CASES[k].key);
		kp_t_eq_str(T, prefix, CASES[k].prefix, "%s", CASES[k].key);
	}
}

/* --- the test scenarios' CHK --- */

/* A CHK chunk appended: tag, size, payload. */
static void chunk(KpBuf *b, const char *tag, const void *payload, size_t size)
{
	uint8_t head[8];
	memcpy(head, tag, 4);
	kp_le_put32(head + 4, (uint32_t)size);
	kp_buf_add(b, head, 8);
	if (size) kp_buf_add(b, payload, size);
}

/* The CHK's first chunk of a tag: its payload and size, or NULL. */
static const uint8_t *find_chunk(const uint8_t *chk, size_t size, const char *tag, size_t *n)
{
	size_t at;
	for (at = 0; at + 8 <= size; at += 8 + (size_t)kp_le_u32(chk + at + 4))
		if (memcmp(chk + at, tag, 4) == 0) {
			*n = kp_le_u32(chk + at + 4);
			return chk + at + 8;
		}
	return NULL;
}

/* The spec's chunks over a base map: its players' controllers, its units (a building and its add-on related both
 * ways), THG2's sprites kept and its doodad units dropped, the briefing emptied, its triggers with their texts appended
 * to STR (string before WAV), an AI script's id, a location, a tech researched; the chunks it leaves are the base's. */
static void scenario_rewrites_the_base(KpT *T)
{
	static const char SPEC[] = "{\"base\": \"terran/terran01\", \"players\": {\"0\": \"human\", \"1\": \"computer\"},"
		"\"units\": [{\"type\": 0, \"x\": 100, \"y\": 200, \"owner\": 0},"
		"{\"type\": 106, \"x\": 300, \"y\": 400, \"owner\": 1, \"hp\": 3, \"related\": 2},"
		"{\"type\": 107, \"x\": 340, \"y\": 400, \"owner\": 1, \"related\": 1, \"state\": [\"burrowed\"]}],"
		"\"doodad_units\": [{\"type\": 203, \"x\": 64, \"y\": 96, \"owner\": 11, \"flags\": 0}],"
		"\"triggers\": [{\"owners\": [0, 18], \"conditions\": [{\"type\": \"always\"}],"
		"\"actions\": [{\"type\": \"transmission\", \"wav\": \"x.wav\", \"string\": \"Hello\", \"time\": 5},"
		"{\"type\": \"run_ai_script\", \"script\": \"TMCx\"}, {\"type\": \"preserve_trigger\"}]}],"
		"\"locations\": {\"2\": {\"rect\": [1, 2, 3, 4], \"name\": 1}}, \"researched\": {\"0\": [3]}}";
	static const uint8_t STR[] = { 2, 0, 6, 0, 8, 0, 'a', 0, 'b', 0 };
	static const uint8_t VER[] = { 205, 0 };
	uint8_t thg2[20], mrgn[40], ptec[2 * 12 * 24 + 2 * 24 + 12 * 24], unit[36];
	const uint8_t *u, *t, *s, *m, *pt;
	char err[256];
	size_t size = 0, n = 0;
	KpJsonDoc doc;
	KpJson *spec;
	KpBuf base;
	uint8_t *chk;
	memset(thg2, 0, sizeof thg2);
	kp_le_put16(thg2 + 8, 0x1000); /* a sprite */
	kp_le_put16(thg2 + 10, 77);    /* a doodad unit (flags 0) */
	memset(mrgn, 0, sizeof mrgn);
	memset(ptec, 0, sizeof ptec);
	memset(unit, 0xee, sizeof unit);
	kp_buf_init(&base);
	chunk(&base, "VER ", VER, sizeof VER);
	chunk(&base, "OWNR", "\x05\x05\x05\x05\x05\x05\x05\x05\x00\x00\x00\x07", 12);
	chunk(&base, "UNIT", unit, sizeof unit);
	chunk(&base, "THG2", thg2, sizeof thg2);
	chunk(&base, "MBRF", "briefing", 8);
	chunk(&base, "TRIG", "old", 3);
	chunk(&base, "STR ", STR, sizeof STR);
	chunk(&base, "MRGN", mrgn, sizeof mrgn);
	chunk(&base, "PTEC", ptec, sizeof ptec);
	kp_json_doc_init(&doc);
	spec = kp_json_parse(&doc, SPEC, strlen(SPEC), err, sizeof err);
	kp_t_ok(T, spec != NULL, "%s", err);
	chk = kp_scenario_chk((const uint8_t *)base.data, base.size, spec, &size, err, sizeof err);
	kp_buf_free(&base);
	kp_json_doc_free(&doc);
	kp_t_ok(T, chk != NULL, "%s", err);
	kp_t_defer(T, free, chk);

	kp_t_ok(T, find_chunk(chk, size, "VER ", &n) != NULL && n == 2, "VER kept");
	u = find_chunk(chk, size, "OWNR", &n);
	kp_t_ok(T, u && n == 12 && u[0] == 6 && u[1] == 5 && u[2] == 0 && u[11] == 0, "the players' controllers");
	u = find_chunk(chk, size, "UNIT", &n);
	kp_t_ok(T, u && n == 3 * 36, "three units");
	kp_t_ok(T, kp_le_u32(u) == 1 && kp_le_u16(u + 4) == 100 && kp_le_u16(u + 6) == 200 && kp_le_u16(u + 8) == 0
		&& u[16] == 0 && u[17] == 100 && kp_le_u32(u + 32) == 0, "the Marine");
	kp_t_ok(T, kp_le_u16(u + 36 + 10) == 0x400 && u[36 + 17] == 3 && kp_le_u16(u + 36 + 14) == 0x2
		&& kp_le_u32(u + 36 + 32) == 3, "the Command Center: an add-on's relation to unit 3, its hp valid");
	kp_t_ok(T, kp_le_u32(u + 72 + 32) == 2 && kp_le_u16(u + 72 + 12) == 2 && kp_le_u16(u + 72 + 26) == 2,
		"the Comsat Station: related to unit 2, burrowed");
	t = find_chunk(chk, size, "THG2", &n);
	kp_t_ok(T, t && n == 20 && kp_le_u16(t + 8) == 0x1000 && kp_le_u16(t + 10) == 203 && t[16] == 11,
		"the sprite kept, the doodad unit the spec's");
	kp_t_ok(T, find_chunk(chk, size, "MBRF", &n) != NULL && n == 0, "no briefing");
	t = find_chunk(chk, size, "TRIG", &n);
	kp_t_ok(T, t && n == 2400, "one trigger");
	kp_t_eq(T, t[15], 22, "the condition: always");
	kp_t_ok(T, t[320 + 26] == 7 && kp_le_u32(t + 320 + 4) == 3 && kp_le_u32(t + 320 + 8) == 4
		&& kp_le_u32(t + 320 + 12) == 5, "the transmission: string 3, its WAV 4, its time");
	kp_t_ok(T, t[352 + 26] == 15 && memcmp(t + 352 + 20, "TMCx", 4) == 0, "the AI script's id");
	kp_t_eq(T, t[384 + 26], 3, "preserve trigger");
	kp_t_ok(T, t[2372] == 1 && t[2372 + 18] == 1 && t[2372 + 1] == 0, "its owners");
	s = find_chunk(chk, size, "STR ", &n);
	kp_t_ok(T, s && kp_le_u16(s) == 4, "the texts appended");
	kp_t_ok(T, kp_le_u16(s + 2) + 4 < n && strcmp((const char *)s + kp_le_u16(s + 2), "a") == 0, "string 1 moved");
	kp_t_ok(T, kp_le_u16(s + 6) < n && strcmp((const char *)s + kp_le_u16(s + 6), "Hello") == 0, "string 3");
	kp_t_ok(T, kp_le_u16(s + 8) < n && strcmp((const char *)s + kp_le_u16(s + 8), "x.wav") == 0, "string 4");
	m = find_chunk(chk, size, "MRGN", &n);
	kp_t_ok(T, m && n == 40 && kp_le_u32(m) == 0 && kp_le_u32(m + 20) == 1 && kp_le_u32(m + 32) == 4
		&& kp_le_u16(m + 36) == 1, "location 2");
	pt = find_chunk(chk, size, "PTEC", &n);
	kp_t_ok(T, pt && n == sizeof ptec && pt[3] == 1 && pt[12 * 24 + 3] == 1 && pt[4] == 0, "tech 3 researched");
}

/* A spec that does not hold is refused with its reason. */
static void scenario_refuses_a_bad_spec(KpT *T)
{
	static const struct {
		const char *spec, *reason;
	} CASES[] = {
		{ "{\"players\": {\"0\": \"robot\"}}", "unknown controller" },
		{ "{\"units\": [{\"type\": 0, \"x\": 1, \"y\": 1, \"owner\": 0, \"related\": 0}]}", "not another spec unit" },
		{ "{\"units\": [{\"type\": 0, \"x\": 1, \"owner\": 0}]}", "no integer \"y\"" },
		{ "{\"triggers\": [{\"owners\": [0], \"actions\": [{\"type\": \"fly\"}]}]}", "unknown action type" },
		{ "{\"triggers\": [{\"owners\": [0], \"conditions\": [{\"type\": \"always\", \"speed\": 2}]}]}", "unknown field" },
		{ "{\"triggers\": [{\"conditions\": []}]}", "no owners" },
	};
	static const uint8_t BASE[] = { 'U', 'N', 'I', 'T', 0, 0, 0, 0, 'T', 'R', 'I', 'G', 0, 0, 0, 0 };
	size_t k;
	for (k = 0; k < sizeof CASES / sizeof CASES[0]; k++) {
		char err[256];
		size_t size = 0;
		KpJsonDoc doc;
		KpJson *spec;
		uint8_t *chk;
		kp_json_doc_init(&doc);
		spec = kp_json_parse(&doc, CASES[k].spec, strlen(CASES[k].spec), err, sizeof err);
		kp_t_ok(T, spec != NULL, "%s", err);
		err[0] = 0;
		chk = kp_scenario_chk(BASE, sizeof BASE, spec, &size, err, sizeof err);
		kp_json_doc_free(&doc);
		free(chk);
		kp_t_ok(T, chk == NULL && strstr(err, CASES[k].reason) != NULL, "%s: %s", CASES[k].spec, err);
	}
}

/* --- the icon --- */

#define ICON 4                 /* the test executable's icon: 4 x 4 pixels */
#define RSRC_RVA 0x1000
#define RSRC_FILE 0x200
#define DIB_AT 0xa0            /* in the resource section */
#define DIB_SIZE (40 + ICON * ICON * 4 + ICON * 4)
#define GROUP_AT (DIB_AT + DIB_SIZE)
#define RSRC_SIZE (GROUP_AT + 6 + 14)

/* A resource directory of one entry (id: target) at at. */
static void rsrc_dir(uint8_t *r, size_t at, uint32_t id, uint32_t target)
{
	kp_le_put16(r + at + 14, 1);
	kp_le_put32(r + at + 16, id);
	kp_le_put32(r + at + 20, target);
}

/* A PE32 whose resources hold icon group 102 of one 32-bit 4 x 4 DIB: pixel (x, y) is (16x, 16y, 200, 60y + 15). */
static uint8_t *test_executable(KpT *T, size_t *size)
{
	uint8_t *exe = (uint8_t *)calloc(RSRC_FILE + RSRC_SIZE, 1), *r, *dib, *g;
	size_t pe = 0x40, opt = pe + 24, table = opt + 224;
	int x, y;
	kp_t_ok(T, exe != NULL, "memory");
	kp_t_defer(T, free, exe);
	exe[0] = 'M';
	exe[1] = 'Z';
	kp_le_put32(exe + 0x3c, (uint32_t)pe);
	memcpy(exe + pe, "PE\0\0", 4);
	kp_le_put16(exe + pe + 4, 0x14c);
	kp_le_put16(exe + pe + 6, 1);   /* one section */
	kp_le_put16(exe + pe + 20, 224); /* the optional header's size */
	kp_le_put16(exe + opt, 0x10b);
	kp_le_put32(exe + opt + 96 + 16, RSRC_RVA); /* the resource table's directory */
	kp_le_put32(exe + opt + 96 + 20, RSRC_SIZE);
	memcpy(exe + table, ".rsrc", 5);
	kp_le_put32(exe + table + 8, RSRC_SIZE);
	kp_le_put32(exe + table + 12, RSRC_RVA);
	kp_le_put32(exe + table + 16, RSRC_SIZE);
	kp_le_put32(exe + table + 20, RSRC_FILE);
	r = exe + RSRC_FILE;
	/* the root: RT_ICON (3) and RT_GROUP_ICON (14); each a directory of one id, of one language, of one data entry */
	kp_le_put16(r + 14, 2);
	kp_le_put32(r + 16, 3);
	kp_le_put32(r + 20, 0x80000000u | 0x20);
	kp_le_put32(r + 24, 14);
	kp_le_put32(r + 28, 0x80000000u | 0x50);
	rsrc_dir(r, 0x20, 1, 0x80000000u | 0x38);
	rsrc_dir(r, 0x38, 0x409, 0x80);
	rsrc_dir(r, 0x50, 102, 0x80000000u | 0x68);
	rsrc_dir(r, 0x68, 0x409, 0x90);
	kp_le_put32(r + 0x80, RSRC_RVA + DIB_AT);
	kp_le_put32(r + 0x84, DIB_SIZE);
	kp_le_put32(r + 0x90, RSRC_RVA + GROUP_AT);
	kp_le_put32(r + 0x94, 6 + 14);
	dib = r + DIB_AT;
	kp_le_put32(dib, 40);
	kp_le_put32(dib + 4, ICON);
	kp_le_put32(dib + 8, 2 * ICON);
	kp_le_put16(dib + 12, 1);
	kp_le_put16(dib + 14, 32);
	for (y = 0; y < ICON; y++) /* bottom up, BGRA */
		for (x = 0; x < ICON; x++) {
			uint8_t *p = dib + 40 + 4 * ((ICON - 1 - y) * ICON + x);
			p[0] = 200;
			p[1] = (uint8_t)(16 * y);
			p[2] = (uint8_t)(16 * x);
			p[3] = (uint8_t)(60 * y + 15);
		}
	g = r + GROUP_AT;
	kp_le_put16(g + 2, 1);
	kp_le_put16(g + 4, 1);
	g[6] = g[7] = ICON;
	kp_le_put16(g + 10, 1);
	kp_le_put16(g + 12, 32);
	kp_le_put32(g + 14, DIB_SIZE);
	kp_le_put16(g + 18, 1);
	*size = RSRC_FILE + RSRC_SIZE;
	return exe;
}

static void free_images(KpIconImage *images, int count)
{
	int k;
	for (k = 0; k < count; k++) free(images[k].rgba);
}

/* The executable's icon group read: its DIB top down as RGBA. Not a PE, or no such group, is refused. */
static void icon_reads_the_group(KpT *T)
{
	KpIconImage images[KP_ICON_IMAGES];
	size_t size = 0;
	const uint8_t *exe = test_executable(T, &size);
	char err[256];
	int count = kp_icon_read(exe, size, KP_ICON_GROUP, images, KP_ICON_IMAGES, err, sizeof err), x, y, ok = 1;
	kp_t_eq(T, count, 1, "%s", err);
	kp_t_eq(T, images[0].size, ICON, "its size");
	for (y = 0; y < ICON; y++)
		for (x = 0; x < ICON; x++) {
			const uint8_t *p = images[0].rgba + 4 * (y * ICON + x);
			ok = ok && p[0] == 16 * x && p[1] == 16 * y && p[2] == 200 && p[3] == 60 * y + 15;
		}
	free_images(images, count);
	kp_t_ok(T, ok, "the pixels top down, RGBA");
	kp_t_eq(T, kp_icon_read(exe, size, 106, images, KP_ICON_IMAGES, err, sizeof err), -1, "no group 106");
	kp_t_ok(T, strstr(err, "not found") != NULL, "%s", err);
	kp_t_eq(T, kp_icon_read((const uint8_t *)"MZ", 2, KP_ICON_GROUP, images, KP_ICON_IMAGES, err, sizeof err), -1, "a stub");
	kp_t_eq(T, kp_icon_read(exe, RSRC_FILE + DIB_AT + 20, KP_ICON_GROUP, images, KP_ICON_IMAGES, err, sizeof err), -1,
		"cut short");
}

/* The resampling weighs premultiplied colour: a transparent pixel gives none of its colour to its opaque neighbour's;
 * an opaque single colour stays itself at any size. */
static void icon_resize_premultiplies(KpT *T)
{
	static const uint8_t TWO[2 * 2 * 4] = { 255, 0, 0, 255, 0, 255, 0, 0, 255, 0, 0, 255, 0, 255, 0, 0 };
	uint8_t flat[4 * 4 * 4], *out;
	int i, ok = 1;
	out = kp_icon_resize(TWO, 2, 4);
	kp_t_ok(T, out != NULL, "memory");
	for (i = 0; i < 16; i++) ok = ok && (out[4 * i + 3] == 0 || (out[4 * i] == 255 && out[4 * i + 1] == 0));
	kp_t_ok(T, ok && out[3] == 255 && out[4 * 3 + 3] == 0 && out[4 * 1 + 3] > 0 && out[4 * 1 + 3] < 255,
		"red where there is alpha: alphas %d %d %d %d", out[3], out[7], out[11], out[15]);
	free(out);
	for (i = 0; i < 16; i++) {
		flat[4 * i] = 12;
		flat[4 * i + 1] = 34;
		flat[4 * i + 2] = 56;
		flat[4 * i + 3] = 255;
	}
	out = kp_icon_resize(flat, 4, 7);
	kp_t_ok(T, out != NULL, "memory");
	for (i = 0, ok = 1; i < 49; i++) ok = ok && out[4 * i] == 12 && out[4 * i + 1] == 34 && out[4 * i + 2] == 56 && out[4 * i + 3] == 255;
	free(out);
	kp_t_ok(T, ok, "a flat colour stays");
}

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* A PNG: the signature, IHDR (size, 8-bit RGB or RGBA) with its CRC, IDAT inflating to filter 0 and the rows, IEND. */
static void icon_png_holds_the_rows(KpT *T)
{
	uint8_t pixels[3 * 5 * 3], rows[3 * (1 + 5 * 3)];
	uLongf rows_size = sizeof rows;
	size_t size = 0, k, idat;
	uint8_t *png;
	for (k = 0; k < sizeof pixels; k++) pixels[k] = (uint8_t)(k * 7);
	png = kp_icon_png(pixels, 5, 3, 3, &size);
	kp_t_ok(T, png != NULL, "memory");
	kp_t_defer(T, free, png);
	kp_t_ok(T, size > 8 + 25 + 12 + 12 && memcmp(png, "\x89PNG\r\n\x1a\n", 8) == 0, "the signature");
	kp_t_ok(T, be32(png + 8) == 13 && memcmp(png + 12, "IHDR", 4) == 0 && be32(png + 16) == 5 && be32(png + 20) == 3
		&& png[24] == 8 && png[25] == 2, "IHDR: 5 x 3, 8-bit RGB");
	kp_t_eq(T, be32(png + 29), (uint32_t)crc32(0, png + 12, 17), "IHDR's CRC");
	idat = 33;
	kp_t_ok(T, memcmp(png + idat + 4, "IDAT", 4) == 0, "IDAT");
	kp_t_eq(T, uncompress(rows, &rows_size, png + idat + 8, be32(png + idat)), Z_OK, "IDAT inflates");
	kp_t_eq(T, (int64_t)rows_size, (int64_t)sizeof rows, "its rows");
	for (k = 0; k < 3; k++) {
		kp_t_eq(T, rows[k * 16], 0, "row %d's filter", (int)k);
		kp_t_ok(T, memcmp(rows + k * 16 + 1, pixels + k * 15, 15) == 0, "row %d", (int)k);
	}
	kp_t_ok(T, memcmp(png + size - 12, "\0\0\0\0IEND\xae\x42\x60\x82", 12) == 0, "IEND");
	kp_t_ok(T, kp_icon_png(pixels, 5, 3, 2, &size) == NULL, "two channels refused");
}

/* Every platform's files: each original's PNG and 23 more (the real executable's four originals make 27); the ICO's and
 * ICNS's directories; iOS's flattened; a second run writes none. */
static void icon_writes_every_platforms_files(KpT *T)
{
	KpIconImage images[KP_ICON_IMAGES];
	const char *dir = kp_t_temp_dir(T, NULL);
	char err[256], path[KP_T_PATH];
	size_t size = 0, n = 0;
	const uint8_t *exe = test_executable(T, &size), *b;
	int count = kp_icon_read(exe, size, KP_ICON_GROUP, images, KP_ICON_IMAGES, err, sizeof err), files = 0, written;
	kp_t_eq(T, count, 1, "%s", err);
	written = kp_icon_write_set(images, count, dir, &files, err, sizeof err);
	kp_t_eq(T, written, 1 + 23, "%s", err);
	kp_t_eq(T, files, 1 + 23, "the files");
	written = kp_icon_write_set(images, count, dir, &files, err, sizeof err);
	free_images(images, count);
	kp_t_eq(T, written, 0, "the same files again: none written");
	snprintf(path, sizeof path, "%s/icon.ico", dir);
	b = kp_t_read(T, path, &n);
	kp_t_ok(T, n > 6 + 6 * 16 && kp_le_u16(b + 2) == 1 && kp_le_u16(b + 4) == 6 && b[6] == 16 && b[6 + 5 * 16] == 0
		&& kp_le_u32(b + 6 + 5 * 16 + 12) + kp_le_u32(b + 6 + 5 * 16 + 8) == n, "icon.ico: six PNGs, 256 the last");
	snprintf(path, sizeof path, "%s/icon.icns", dir);
	b = kp_t_read(T, path, &n);
	kp_t_ok(T, n > 16 && memcmp(b, "icns", 4) == 0 && be32(b + 4) == n && memcmp(b + 8, "icp4", 4) == 0, "icon.icns");
	snprintf(path, sizeof path, "%s/ios/AppIcon60x60@2x.png", dir);
	b = kp_t_read(T, path, &n);
	kp_t_ok(T, n > 26 && be32(b + 16) == 120 && b[25] == 2, "iOS's: 120 pixels, RGB");
	snprintf(path, sizeof path, "%s/android/res/mipmap-xxxhdpi/ic_launcher_foreground.png", dir);
	b = kp_t_read(T, path, &n);
	kp_t_ok(T, n > 26 && be32(b + 16) == 432 && b[25] == 6, "the adaptive foreground: 432 pixels, RGBA");
}

/* --- files and workers --- */

/* A folder's files by suffix, sorted; a file written only when its bytes change; the files of a prefix removed. */
static void fs_lists_writes_removes(KpT *T)
{
	static const char *const NAMES[] = { "b.json", "a.json", "c.txt", "koprulu-x.mpq", "koprulu.mpq", "other.mpq" };
	const char *dir = kp_t_temp_dir(T, NULL);
	char path[KP_T_PATH], err[256], **names;
	size_t size = 0, k;
	int count = 0;
	uint8_t *b;
	for (k = 0; k < sizeof NAMES / sizeof NAMES[0]; k++) {
		snprintf(path, sizeof path, "%s/%s", dir, NAMES[k]);
		kp_t_eq(T, kp_fs_write(path, NAMES[k], strlen(NAMES[k]), err, sizeof err), 0, "%s", err);
	}
	snprintf(path, sizeof path, "%s/sub/deeper", dir);
	kp_t_eq(T, kp_fs_mkdirs(path, err, sizeof err), 0, "%s", err);
	kp_t_eq(T, kp_fs_mkdirs(path, err, sizeof err), 0, "made again");
	snprintf(path, sizeof path, "%s/sub.json", dir); /* a folder whose name has the suffix is no file */
	kp_t_eq(T, kp_fs_mkdirs(path, err, sizeof err), 0, "%s", err);
	names = kp_fs_list(dir, ".json", &count, err, sizeof err);
	kp_t_ok(T, names != NULL, "%s", err);
	kp_t_ok(T, count == 2 && strcmp(names[0], "a.json") == 0 && strcmp(names[1], "b.json") == 0, "%d files", count);
	for (k = 0; k < (size_t)count; k++) free(names[k]);
	free(names);
	snprintf(path, sizeof path, "%s/a.json", dir);
	kp_t_eq(T, kp_fs_write_if_changed(path, "a.json", 6, err, sizeof err), 0, "the same bytes");
	kp_t_eq(T, kp_fs_write_if_changed(path, "{}", 2, err, sizeof err), 1, "new bytes");
	b = kp_fs_read(path, &size, err, sizeof err);
	kp_t_ok(T, b && size == 2 && memcmp(b, "{}", 3) == 0, "read back, NUL terminated");
	free(b);
	snprintf(path, sizeof path, "%s/none", dir);
	kp_t_ok(T, kp_fs_read(path, &size, err, sizeof err) == NULL && err[0] == 0, "no such file: no reason");
	kp_t_eq(T, kp_fs_remove_matching(dir, "koprulu", ".mpq", err, sizeof err), 2, "koprulu*.mpq removed");
	names = kp_fs_list(dir, ".mpq", &count, err, sizeof err);
	kp_t_ok(T, names && count == 1 && strcmp(names[0], "other.mpq") == 0, "the other archive stays");
	for (k = 0; k < (size_t)count; k++) free(names[k]);
	free(names);
}

typedef struct {
	int runs[100];
} Jobs;

static void count_job(void *ctx, int job) { ((Jobs *)ctx)->runs[job]++; }

/* Every job runs once, on one thread or several. */
static void work_runs_every_job_once(KpT *T)
{
	static const int THREADS[] = { 1, 3, 8 };
	size_t k;
	int i;
	for (k = 0; k < sizeof THREADS / sizeof THREADS[0]; k++) {
		Jobs jobs;
		memset(&jobs, 0, sizeof jobs);
		kp_work_run(count_job, &jobs, 100, THREADS[k]);
		for (i = 0; i < 100; i++) kp_t_eq(T, jobs.runs[i], 1, "job %d on %d threads", i, THREADS[k]);
	}
	kp_t_ok(T, kp_work_processors() >= 1, "processors");
}

/* --- the movies' WebM (stage C4) --- */

#define MOVIE_W 32
#define MOVIE_H 16
#define MOVIE_PLANE (MOVIE_W * MOVIE_H)
#define MOVIE_FRAMES 6
#define MOVIE_RATE_NUM 50000 /* the 1.17 movies' rate */
#define MOVIE_RATE_DEN 3333
#define MOVIE_FRAME_NS 66660000u /* 1e9 * 3333 / 50000 */
#define MOVIE_SERIAL 77
#define MOVIES_GOLDEN "tests/golden/cinematics/converter.json" /* converter cinematics' SD pack, before stage C4 */
#define MOVIES_REPACK "KP_TEST_MOVIES" /* a repack whose cinematics group's movies are compared, all of them */
#define MOVIES_ORIGINAL "KP_TEST_ORIGINAL"
#define VORBIS_SLACK 1000 /* a movie's Vorbis bytes from oggenc's at most a thousandth of them */

/* Frame n's G, B, R planes: a level that moves with n. */
static int movie_frame(void *ctx, uint32_t n, const uint8_t *planes[3], int strides[3])
{
	uint8_t *p = (uint8_t *)ctx + (size_t)n * 3 * MOVIE_PLANE;
	int k, i;
	for (k = 0; k < 3; k++) {
		for (i = 0; i < MOVIE_PLANE; i++) p[k * MOVIE_PLANE + i] = (uint8_t)(40 * k + 7 * (i % MOVIE_W) + 20 * n);
		planes[k] = p + (size_t)k * MOVIE_PLANE;
		strides[k] = MOVIE_W;
	}
	return 0;
}

static void movie_video_free(void *v)
{
	kp_movie_video_free((KpMovieVideo *)v);
	free(v);
}

static void vorbis_free(void *p)
{
	kp_vorbis_packets_free((KpVorbisPackets *)p);
	free(p);
}

static void streams_free(void *s)
{
	kp_webm_streams_free((KpWebmStreams *)s);
	free(s);
}

static KpWebmStreams *streams_of(KpT *T, const uint8_t *webm, size_t size)
{
	KpWebmStreams *st = (KpWebmStreams *)calloc(1, sizeof *st);
	char err[200];
	kp_t_ok(T, st != NULL, "memory");
	kp_t_ok(T, kp_webm_streams(webm, size, MOVIE_SERIAL, st, err, sizeof err) == 0, "the WebM reads back: %s", err);
	kp_t_defer(T, streams_free, st);
	return st;
}

/* A movie written as WebM reads back as the player takes it: the VP9 packets and their key frames, the frame's
 * duration (the rate 50000/3333 again), the Vorbis sound decoded to exactly its samples (its last block's
 * DiscardPadding trimmed), the same bytes twice; without sound, none. */
static void webm_reads_back(KpT *T)
{
	uint8_t *frames = (uint8_t *)malloc((size_t)MOVIE_FRAMES * 3 * MOVIE_PLANE);
	KpMovieVideo *v = (KpMovieVideo *)calloc(1, sizeof *v);
	KpVorbisPackets *sound = (KpVorbisPackets *)calloc(1, sizeof *sound);
	KpWebmMovie m;
	KpWebmStreams *st;
	KpCineClip named;
	KpCineMovie *movie;
	KpError e;
	size_t wav_size = 0, a_size = 0, b_size = 0, silent_size = 0;
	const uint8_t *wav;
	uint8_t *a, *b, *silent;
	char err[256];
	uint32_t n;
	kp_t_ok(T, frames && v && sound, "memory");
	kp_t_defer(T, free, frames);
	kp_t_defer(T, movie_video_free, v);
	kp_t_defer(T, vorbis_free, sound);
	kp_t_ok(T, kp_movie_vp9(MOVIE_W, MOVIE_H, MOVIE_RATE_NUM, MOVIE_RATE_DEN, MOVIE_FRAMES, movie_frame, frames, v, err,
		sizeof err) == 0, "%s", err);
	wav = wav_new(T, SOUND_FRAMES, 2, 16, 1, &wav_size);
	kp_t_ok(T, kp_vorbis_from_wav(wav, wav_size, KP_OGG_QUALITY, sound, err, sizeof err) == 0, "%s", err);
	kp_t_eq(T, sound->granules[sound->count - 1], SOUND_FRAMES, "the last packet's granule is the sound's length");
	memset(&m, 0, sizeof m);
	m.width = MOVIE_W;
	m.height = MOVIE_H;
	m.shown_w = 2 * MOVIE_W;
	m.frame_ns = MOVIE_FRAME_NS;
	m.packets = v->packets;
	m.sizes = v->sizes;
	m.frames = v->count;
	m.sound = sound;
	a = kp_webm_write(&m, &a_size, err, sizeof err);
	b = kp_webm_write(&m, &b_size, err, sizeof err);
	kp_t_ok(T, a && b, "%s", err);
	kp_t_defer(T, free, a);
	kp_t_defer(T, free, b);
	kp_t_ok(T, a_size == b_size && memcmp(a, b, a_size) == 0, "the same movie gives the same bytes");
	st = streams_of(T, a, a_size);
	kp_t_eq(T, st->width, MOVIE_W, "width");
	kp_t_eq(T, st->height, MOVIE_H, "height");
	kp_t_eq(T, st->frame_ns, MOVIE_FRAME_NS, "the frame's duration");
	kp_t_eq(T, st->count, MOVIE_FRAMES, "a packet a frame");
	kp_t_ok(T, st->packets_size == v->size && memcmp(st->packets, v->packets, v->size) == 0, "the packets as written");
	for (n = 0; n < st->count; n++) kp_t_eq(T, kp_le_u32(st->sizes + 4u * n), v->sizes[n], "frame %u's size and key bit", n);
	kp_t_ok(T, st->ogg != NULL, "the sound");
	ogg_decode(T, st->ogg, st->ogg_size, 2, MOVIE_SERIAL); /* every sample, no more: the padding trimmed */
	memset(&named, 0, sizeof named);
	snprintf(named.name, sizeof named.name, "smk\\test");
	movie = kp_cine_movie_webm(a, a_size, &named, &e);
	kp_t_ok(T, movie != NULL, "the player takes it: %s", e.msg);
	kp_t_eq(T, kp_cine_movie_clip(movie)->rate_num, MOVIE_RATE_NUM, "its rate");
	kp_t_eq(T, kp_cine_movie_clip(movie)->rate_den, MOVIE_RATE_DEN, "its rate");
	kp_t_eq(T, kp_cine_movie_clip(movie)->frames, MOVIE_FRAMES, "its frames");
	kp_cine_movie_free(movie);
	m.sound = NULL;
	silent = kp_webm_write(&m, &silent_size, err, sizeof err);
	kp_t_ok(T, silent != NULL, "%s", err);
	kp_t_defer(T, free, silent);
	st = streams_of(T, silent, silent_size);
	kp_t_ok(T, st->ogg == NULL && st->count == MOVIE_FRAMES, "no sound, the frames");
	v->sizes[0] &= ~KP_WEBM_KEY;
	m.sizes = v->sizes;
	kp_t_ok(T, kp_webm_write(&m, &silent_size, err, sizeof err) == NULL, "a first frame that is no key frame refused");
}

/* The SHA-256 of bytes, in hexadecimal */
static void hex_digest(const uint8_t *data, size_t size, char hex[2 * KP_SHA256_SIZE + 1])
{
	uint8_t d[KP_SHA256_SIZE];
	kp_sha256(data, size, d);
	kp_sha256_hex(d, hex);
}

/* An Ogg stream's packets: their count, their bytes but the second's (the comment header: the encoder's vendor), the
 * last page's granule. */
static void ogg_packets(KpT *T, const uint8_t *ogg, size_t size, int *count, uint64_t *bytes, int64_t *granule)
{
	size_t at = 0, current = 0;
	*count = 0;
	*bytes = 0;
	*granule = -1;
	while (at < size) {
		size_t segments, body = 0, k, data;
		kp_t_ok(T, at + 27 <= size && memcmp(ogg + at, "OggS", 4) == 0, "an Ogg page at %lu", (unsigned long)at);
		segments = ogg[at + 26];
		*granule = (int64_t)((uint64_t)kp_le_u32(ogg + at + 6) | (uint64_t)kp_le_u32(ogg + at + 10) << 32);
		data = at + 27 + segments;
		for (k = 0; k < segments; k++) {
			current += ogg[at + 27 + k];
			body += ogg[at + 27 + k];
			if (ogg[at + 27 + k] < 255) { /* a packet ends */
				if (*count != 1) *bytes += current;
				(*count)++;
				current = 0;
			}
		}
		at = data + body;
	}
}

/* A movie's WebM against its golden entry: its size and rate (the player's: KpCineClip), its VP9 packets and frame
 * table; its Vorbis packets' count and the sound's length, their bytes within 1 / VORBIS_SLACK of oggenc's (libvorbisenc
 * built here rounds a few floats otherwise than Homebrew's oggenc: stage C0's 3 bytes in paryes00.wav), *vorbis_apart
 * counting how far. */
static void movie_as_golden(KpT *T, const char *name, const uint8_t *webm, size_t size, const KpJson *g,
	int64_t *vorbis_apart)
{
	KpWebmStreams *st = streams_of(T, webm, size);
	KpCineClip named;
	KpCineMovie *movie;
	KpError e;
	char hex[2 * KP_SHA256_SIZE + 1];
	memset(&named, 0, sizeof named);
	snprintf(named.name, sizeof named.name, "%s", name);
	movie = kp_cine_movie_webm(webm, size, &named, &e);
	kp_t_ok(T, movie != NULL, "%s: the player takes it: %s", name, e.msg);
	kp_t_eq(T, kp_cine_movie_clip(movie)->rate_num, kp_json_int(kp_json_at(kp_json_get(g, "rate"), 0), -1), "%s: rate", name);
	kp_t_eq(T, kp_cine_movie_clip(movie)->rate_den, kp_json_int(kp_json_at(kp_json_get(g, "rate"), 1), -1), "%s: rate", name);
	kp_cine_movie_free(movie);
	kp_t_eq(T, st->width, kp_json_field_int(g, "width", -1), "%s: width", name);
	kp_t_eq(T, st->height, kp_json_field_int(g, "height", -1), "%s: height", name);
	kp_t_eq(T, st->count, kp_json_field_int(g, "frames", -1), "%s: frames", name);
	kp_t_eq(T, st->packets_size, kp_json_field_int(g, "video_size", -1), "%s: the video's bytes", name);
	hex_digest(st->packets, (size_t)st->packets_size, hex);
	kp_t_ok(T, strcmp(hex, kp_json_str(kp_json_get(g, "video"), "")) == 0, "%s: the VP9 packets are the converter's", name);
	hex_digest(st->sizes, 4u * st->count, hex);
	kp_t_ok(T, strcmp(hex, kp_json_str(kp_json_get(g, "sizes"), "")) == 0, "%s: the frame table is the converter's", name);
	if (kp_json_get(g, "samples")) {
		int count;
		uint64_t bytes, want = (uint64_t)kp_json_field_int(g, "vorbis_bytes", -1);
		int64_t granule;
		kp_t_ok(T, st->ogg != NULL, "%s: its sound", name);
		ogg_packets(T, st->ogg, st->ogg_size, &count, &bytes, &granule);
		kp_t_eq(T, count, kp_json_field_int(g, "vorbis_packets", -1), "%s: the Vorbis packets", name);
		kp_t_eq(T, granule, kp_json_field_int(g, "samples", -1), "%s: the sound's samples", name);
		kp_t_ok(T, (bytes > want ? bytes - want : want - bytes) * VORBIS_SLACK <= want,
			"%s: %llu bytes of Vorbis, oggenc's %llu", name, (unsigned long long)bytes, (unsigned long long)want);
		*vorbis_apart += (int64_t)bytes - (int64_t)want;
	} else {
		kp_t_ok(T, st->ogg == NULL, "%s: no sound", name);
	}
}

/* The movie names of the profile's catalog in lower case ("blizzard"), k-th. */
static void catalog_lower(const KpCineCatalog *c, int k, char *out, size_t size)
{
	size_t i;
	for (i = 0; c->movies[k].name[i] && i + 1 < size; i++)
		out[i] = (char)(c->movies[k].name[i] >= 'A' && c->movies[k].name[i] <= 'Z' ? c->movies[k].name[i] - 'A' + 'a' : c->movies[k].name[i]);
	out[i] = 0;
}

/* Acceptance of stage C4: the 1.17 movies kp_convert transcodes (kp_movie_smk_webm) equal what the Python converter
 * wrote (MOVIES_GOLDEN, recorded before it was deleted): the same VP9 packets and key frames, the same Vorbis packets
 * (all but the comment header) and length. With KP_TEST_MOVIES, a repack's cinematics group, all 28 (the repack wrote
 * them: tools/ci.sh); else two transcoded here from KP_TEST_ORIGINAL (a quick 320-wide one and a 640-wide briefing). */
static void movies_equal_the_converters(KpT *T)
{
	static const char *const QUICK[] = { "blizzard", "preprotoss_3" };
	const char *repack = getenv(MOVIES_REPACK);
	KpRes *res = kp_t_install(T, repack && repack[0] ? MOVIES_REPACK : MOVIES_ORIGINAL);
	const KpJson *golden = kp_json_get(kp_t_golden(T, MOVIES_GOLDEN), "movies");
	const KpDefsTable *cinm = kp_defs_table(kp_res_profile(res), "CINM");
	KpCineCatalog *c = (KpCineCatalog *)malloc(sizeof *c);
	int k, compared = 0;
	int64_t vorbis_apart = 0;
	kp_t_ok(T, c != NULL, "memory");
	kp_t_defer(T, free, c);
	kp_t_ok(T, cinm && kp_cine_catalog(c, cinm->data, cinm->size) == 0, "the profile's movies");
	kp_t_eq(T, kp_json_len(golden), c->count, "a golden entry a movie");
	for (k = 0; k < c->count; k++) {
		char lower[KP_CINE_NAME], name[KP_CINE_NAME + 8], path[96], err[400];
		const KpJson *g;
		uint8_t *webm;
		size_t size = 0, q;
		catalog_lower(c, k, lower, sizeof lower);
		snprintf(name, sizeof name, "smk\\%s", lower);
		kp_t_ok(T, (g = kp_json_get(golden, name)) != NULL, "%s in the golden", name);
		if (repack && repack[0]) {
			snprintf(path, sizeof path, KP_CINE_WEBM_FORMAT, KP_CINE_WEBM_FOLDER[0], lower);
			webm = kp_res_raw_read(res, path, &size, err, sizeof err);
			kp_t_ok(T, webm != NULL, "%s in the repack: %s", path, err);
		} else {
			KpResStatus status;
			uint8_t *smk;
			KpMovieInfo info;
			for (q = 0; q < sizeof QUICK / sizeof QUICK[0] && strcmp(QUICK[q], lower) != 0; q++) {}
			if (q == sizeof QUICK / sizeof QUICK[0]) continue;
			snprintf(path, sizeof path, "smk\\%s.smk", c->movies[k].name);
			smk = kp_res_read(res, path, &size, &status, err, sizeof err);
			kp_t_ok(T, smk != NULL, "%s: %s", path, err);
			webm = kp_movie_smk_webm(smk, size, name, &size, &info, err, sizeof err);
			free(smk);
			kp_t_ok(T, webm != NULL, "%s", err);
		}
		kp_t_defer(T, free, webm);
		movie_as_golden(T, name, webm, size, g, &vorbis_apart);
		compared++;
	}
	kp_t_log(T, "%d movies as the converter's, their Vorbis %+lld bytes from oggenc's", compared, (long long)vorbis_apart);
}

#else
#define SKIPPED(name) \
	static void name(KpT *T) { kp_t_skip(T, "built without KP_ENCODERS"); }
SKIPPED(ogg_from_wav_decodes)
SKIPPED(ogg_serial_is_the_names_crc)
SKIPPED(repack_reads_back)
SKIPPED(repack_needs_an_install)
SKIPPED(repack_movies_read_by_ranges)
SKIPPED(repack_groups_by_name)
SKIPPED(scenario_rewrites_the_base)
SKIPPED(scenario_refuses_a_bad_spec)
SKIPPED(icon_reads_the_group)
SKIPPED(icon_resize_premultiplies)
SKIPPED(icon_png_holds_the_rows)
SKIPPED(icon_writes_every_platforms_files)
SKIPPED(fs_lists_writes_removes)
SKIPPED(work_runs_every_job_once)
SKIPPED(webm_reads_back)
SKIPPED(movies_equal_the_converters)
#endif

static const KpTest TESTS[] = {
	{ "ogg_from_wav_decodes", ogg_from_wav_decodes, 0 },
	{ "ogg_serial_is_the_names_crc", ogg_serial_is_the_names_crc, 0 },
	{ "repack_reads_back", repack_reads_back, 0 },
	{ "repack_needs_an_install", repack_needs_an_install, 0 },
	{ "repack_movies_read_by_ranges", repack_movies_read_by_ranges, 0 },
	{ "repack_groups_by_name", repack_groups_by_name, 0 },
	{ "scenario_rewrites_the_base", scenario_rewrites_the_base, 0 },
	{ "scenario_refuses_a_bad_spec", scenario_refuses_a_bad_spec, 0 },
	{ "icon_reads_the_group", icon_reads_the_group, 0 },
	{ "icon_resize_premultiplies", icon_resize_premultiplies, 0 },
	{ "icon_png_holds_the_rows", icon_png_holds_the_rows, 0 },
	{ "icon_writes_every_platforms_files", icon_writes_every_platforms_files, 0 },
	{ "fs_lists_writes_removes", fs_lists_writes_removes, 0 },
	{ "work_runs_every_job_once", work_runs_every_job_once, 0 },
	{ "webm_reads_back", webm_reads_back, 0 },
	{ "movies_equal_the_converters", movies_equal_the_converters, 0 },
};

KP_SUITE(kp_suite_convert, "test_convert", TESTS);
