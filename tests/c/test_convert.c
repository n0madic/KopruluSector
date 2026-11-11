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

#include "kp_fs.h"
#include "kp_icon.h"
#include "kp_kpf.h"
#include "kp_mpq.h"
#include "kp_ogg.h"
#include "kp_repack.h"
#include "kp_res.h"
#include "kp_scenario.h"
#include "kp_sha256.h"
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
} Profile;

static const KpDefsArchive ARCHIVES[] = { { "A.mpq", 0, "core" } };
static const char ONE[] = "one, in the core", CHK[] = "a mission's map", MOVIE[] = "a movie's bytes";

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

/* The test profile (its manifest: a core file, a mission's map and speech, a track; its movies' catalog: "test") and
 * its install, A.mpq and Game.exe, in dir. The speech is long enough that its Ogg is smaller; the track so short that
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
	kp_le_put32(p->cinm + 9, (uint32_t)strlen(MOVIE));
	kp_sha256(MOVIE, strlen(MOVIE), p->cinm + 13);
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
	kp_t_ok(T, kp_mpq_add(m, "smk\\test.smk", MOVIE, strlen(MOVIE), KP_MPQ_STORE, err, sizeof err) == 0, "%s", err);
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

/* An install's repack in groups, read back through kp_res alone: every file, the speech as Ogg Vorbis whose header
 * the listing answers, the short track as its WAV, the movie in the cinematics' group. A second run, the single
 * repack (one koprulu.mpq and the cinematics' group), takes the Oggs from the cache. */
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
	kp_t_eq(T, rep.encoded, 2, "both encoded (the track's Ogg is the larger)");
	kp_t_eq(T, rep.missing + rep.damaged, 0, "every file there");
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
	b = read_back(T, r, "smk\\test.smk", &size, &st);
	kp_t_ok(T, size == strlen(MOVIE) && memcmp(b, MOVIE, size) == 0, "the movie");
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
		{ "smk/blizzard.smk", "koprulu-cinematics.mpq", "smk/" },
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
#else
#define SKIPPED(name) \
	static void name(KpT *T) { kp_t_skip(T, "built without KP_ENCODERS"); }
SKIPPED(ogg_from_wav_decodes)
SKIPPED(ogg_serial_is_the_names_crc)
SKIPPED(repack_reads_back)
SKIPPED(repack_needs_an_install)
SKIPPED(repack_groups_by_name)
SKIPPED(scenario_rewrites_the_base)
SKIPPED(scenario_refuses_a_bad_spec)
SKIPPED(icon_reads_the_group)
SKIPPED(icon_resize_premultiplies)
SKIPPED(icon_png_holds_the_rows)
SKIPPED(icon_writes_every_platforms_files)
SKIPPED(fs_lists_writes_removes)
SKIPPED(work_runs_every_job_once)
#endif

static const KpTest TESTS[] = {
	{ "ogg_from_wav_decodes", ogg_from_wav_decodes, 0 },
	{ "ogg_serial_is_the_names_crc", ogg_serial_is_the_names_crc, 0 },
	{ "repack_reads_back", repack_reads_back, 0 },
	{ "repack_needs_an_install", repack_needs_an_install, 0 },
	{ "repack_groups_by_name", repack_groups_by_name, 0 },
	{ "scenario_rewrites_the_base", scenario_rewrites_the_base, 0 },
	{ "scenario_refuses_a_bad_spec", scenario_refuses_a_bad_spec, 0 },
	{ "icon_reads_the_group", icon_reads_the_group, 0 },
	{ "icon_resize_premultiplies", icon_resize_premultiplies, 0 },
	{ "icon_png_holds_the_rows", icon_png_holds_the_rows, 0 },
	{ "icon_writes_every_platforms_files", icon_writes_every_platforms_files, 0 },
	{ "fs_lists_writes_removes", fs_lists_writes_removes, 0 },
	{ "work_runs_every_job_once", work_runs_every_job_once, 0 },
};

KP_SUITE(kp_suite_convert, "test_convert", TESTS);
