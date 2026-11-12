/*
 * The repack (ADR 0018, stage D8; in C since ADR 0019, stage C1): the files the game reads from an install, in their
 * original formats, written into koprulu*.mpq archives with StormLib, each with a "(koprulu)" listing the runtime
 * checks every file against (kp_res.c).
 *
 * The files are the input manifest's (kp_defs.h) in its order (a Remastered rebuild's palette is kept for the files
 * after it: the order kp_res reads them in), then a 1.17 install's movies (the profile's CINM, each checked against its
 * reference copy's size and SHA-256), then the names a run read unlisted (its --res-log). From a Remastered install the
 * copies and rebuilds keep kp_res's statuses. A file the source lacks (a 1.17 install without its CDs' archives:
 * music, movies, speech), or holds a damaged copy of (one StormLib cannot read whole), is left out and reported; a 1.17
 * copy kp_res refuses (it differs from the manifest) stops the repack.
 *
 * The movies (ADR 0019, stage C4) go in under Remastered's names, which the runtime plays them by from either source
 * (kp_cine.h KP_CINE_WEBM_FORMAT): a 1.17 install's transcoded into WebM (kp_movie.h; encoded once into the cache as
 * <SMK SHA-256>-KP_MOVIE_SETTINGS.webm, listed "converted") as the SD set; a Remastered install's SD and HD2 sets as
 * they are, with the base language's subtitles the install holds (KP_LOCALE_SUBTITLES; a language's own go with its
 * text). A movie that does not transcode stops the repack.
 *
 * A WAV goes in as Ogg Vorbis when that is smaller (kp_ogg.h, the CRC-32 of its normalized name as the stream serial;
 * encoded once into the cache): the listing keeps the WAV's digest, size and header up to its data chunk, which the
 * runtime answers for the WAV's header (kp_res_head). A WAV the original's parser refuses, whose header is longer than
 * KP_REPACK_WAV_HEAD, or which the encoder does not take, stays a WAV.
 *
 * A language's speech (ADR 0019, stage C2) goes in under Remastered's own names (kp_res_raw_read): for each campaign
 * mission its map (the TIME of its lines, KP_HOST_SPEECH_MAP) and its lines (a WAV string's file under the language's
 * assets, .ogg for .wav), and the voices of sfxdata's sounds (kp_ui_data_speech_path); a file the install lacks or
 * keeps empty (Remastered's silent stand-ins) is left out, the runtime plays the base's there. A line or a voice goes
 * in as Ogg Vorbis mono at 22,050 Hz (kp_ogg_speech) when that is smaller; a voice keeps its WAV header in the
 * listing, as the base's sounds do.
 *
 * Groups, for a build that fetches them when it needs them (the web): koprulu-core.mpq, what the program reads when it
 * starts (the base language's subtitles too); a group per mission directory, its CHK and speech
 * (koprulu-<campaign>-<mission>.mpq); koprulu-music.mpq; the movies' sets, koprulu-cinematics.mpq (SD) and
 * koprulu-cinematics-hd2.mpq; a language's speech and text, koprulu-locale-<locale>.mpq. The core's listing names each
 * group with the prefix of its names. A single repack is one koprulu.mpq instead (the desktops), the movies' sets and
 * each language still apart in their groups, which its listing names (a bundle takes the sets and languages it
 * ships). The movies' groups are read by ranges (the web fetches a movie alone): their files stored as they are, the
 * core's listing places each.
 *
 * The listing (a line each):
 *     koprulu repack 4
 *     profile <id>
 *     source <1.17|remastered>
 *     group <file> <prefix>                          the core's: a group and the normalized prefix of its names
 *     in <file> <offset> <sha256> <size> <status> <name>   the core's: a file of a group read by ranges, its bytes there
 *     <sha256> <size> <status> <name>
 *     <sha256> <size> <status> ogg <wav sha256> <wav size> <wav header, hex> <name>
 * The digest and size are the stored bytes'; <status> the source's (kp_res_status_name: exact, derived, differs,
 * equivalent, colour-equal, approximation, unlisted; remastered: a Remastered file by its own name, which no manifest
 * lists; converted: the converter's of the install's, by a Remastered name); <name> the normalized name with '\'
 * separators; <offset> where the group's file holds the bytes.
 */
#ifndef KP_REPACK_H
#define KP_REPACK_H

#include <stddef.h>
#include <stdint.h>

#include "kp_res.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_REPACK_WAV_HEAD 4096 /* kp_files.c WAV_HEAD: the bytes of a WAV's header the runtime reads first */
#define KP_REPACK_GROUPS 128
#define KP_REPACK_NAMES 16      /* the missing and damaged names a report keeps */
#define KP_REPACK_NAME 256

typedef struct {
	const char *out;        /* the folder; the koprulu*.mpq there before are removed */
	int single;             /* one koprulu.mpq and the cinematics' group, instead of the groups */
	int wav;                /* the sounds as they are (no Ogg Vorbis; a 1.17 movie is WebM all the same) */
	const char *cache;      /* the folder the encoded files are kept in (a WAV's Ogg Vorbis, a 1.17 movie's WebM), NULL none */
	const char *const *extra; /* names a run read unlisted (kp_res's log), the manifest's left out */
	int extra_count;
	const char *const *locales; /* the languages whose speech goes in ("ruRU"; the install's: kp_res_raw_read) */
	int locale_count;
	int threads;            /* the encoder's threads (0: the processors) */
	void (*log)(void *ctx, const char *line); /* warnings (a WAV kept as it is), NULL none */
	void *log_ctx;
} KpRepackConfig;

typedef struct {
	int group_count;
	struct {
		char file[64];
		int files;
		uint64_t bytes; /* stored */
	} groups[KP_REPACK_GROUPS];
	int statuses[KP_RES_STATUS_COUNT];
	int missing, damaged;
	char missing_names[KP_REPACK_NAMES][KP_REPACK_NAME], damaged_names[KP_REPACK_NAMES][KP_REPACK_NAME];
	int ogg, encoded;
	uint64_t wav_bytes, ogg_bytes; /* the WAVs the Oggs replace, and the Oggs */
	int speech;                         /* a language's lines and voices re-encoded */
	uint64_t speech_from, speech_bytes; /* the files the re-encoded ones replace, and they */
	int movies;                         /* a 1.17 install's movies as WebM */
	uint64_t movie_from, movie_bytes;   /* their SMKs, and they */
	int raw_absent;                     /* Remastered names the install lacks or keeps empty (a language's: the base's) */
} KpRepackReport;

/* The group file of a normalized name and the prefix of its group's names into out and prefix: a mission directory's
 * "campaign/<campaign>/<mission>/" (koprulu-<campaign>-<mission>.mpq), "music/" (koprulu-music.mpq), "sd/smk/"
 * (koprulu-cinematics.mpq), "hd2/smk/" (koprulu-cinematics-hd2.mpq), a language's but the base's "locales/<locale>/"
 * (koprulu-locale-<locale>.mpq, the locale in lower case); the core's is koprulu-core.mpq and "". */
void kp_repack_group(const char *key, char *out, size_t size, char *prefix, size_t prefix_size);

/* The repack of an install's files (res holds one install and no repack) into c->out: 0, or -1 with the reason (the
 * report holds what was written up to then). */
int kp_repack_write(KpRes *res, const KpRepackConfig *c, KpRepackReport *report, char *err, size_t err_size);

#ifdef __cplusplus
}
#endif

#endif
