/*
 * The repack (ADR 0018, stage D8; in C since ADR 0019, stage C1): the files the game reads from an install, in their
 * original formats, written into koprulu*.mpq archives with StormLib, each with a "(koprulu)" listing the runtime
 * checks every file against (kp_res.c).
 *
 * The files are the input manifest's (kp_defs.h) in its order (a Remastered rebuild's palette is kept for the files
 * after it: the order kp_res reads them in), then a 1.17 install's movies (the profile's CINM, each checked against its
 * reference copy's size and SHA-256), then the names a run read unlisted (its --res-log). From a Remastered install the
 * copies and rebuilds keep kp_res's statuses and no movies go in (Remastered's are WebM, which the runtime reads from the
 * install). A file the source lacks (a 1.17 install without its CDs' archives: music, movies, speech), or holds a
 * damaged copy of (one StormLib cannot read whole), is left out and reported; a 1.17 copy kp_res refuses (it differs
 * from the manifest) stops the repack.
 *
 * A WAV goes in as Ogg Vorbis when that is smaller (kp_ogg.h, the CRC-32 of its normalized name as the stream serial;
 * encoded once into the cache): the listing keeps the WAV's digest, size and header up to its data chunk, which the
 * runtime answers for the WAV's header (kp_res_head). A WAV the original's parser refuses, whose header is longer than
 * KP_REPACK_WAV_HEAD, or which the encoder does not take, stays a WAV.
 *
 * Groups, for a build that fetches them when it needs them (the web): koprulu-core.mpq, what the program reads when it
 * starts; a group per mission directory, its CHK and speech (koprulu-<campaign>-<mission>.mpq); koprulu-music.mpq;
 * koprulu-cinematics.mpq. The core's listing names each group with the prefix of its names. A single repack is one
 * koprulu.mpq instead (the desktops), the cinematics still apart in their group, which its listing names.
 *
 * The listing (a line each):
 *     koprulu repack 2
 *     profile <id>
 *     source <1.17|remastered>
 *     group <file> <prefix>                          the core's: a group and the normalized prefix of its names
 *     <sha256> <size> <status> <name>
 *     <sha256> <size> <status> ogg <wav sha256> <wav size> <wav header, hex> <name>
 * The digest and size are the stored bytes'; <status> the source's (kp_res_status_name: exact, derived, differs,
 * equivalent, colour-equal, approximation, unlisted); <name> the normalized name with '\' separators.
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
	int wav;                /* the WAVs as they are (no Ogg Vorbis) */
	const char *cache;      /* the folder each WAV's Ogg Vorbis is kept in (<WAV SHA-256>-q3-s<serial>.ogg), NULL none */
	const char *const *extra; /* names a run read unlisted (kp_res's log), the manifest's left out */
	int extra_count;
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
} KpRepackReport;

/* The group file of a normalized name and the prefix of its group's names into out and prefix: a mission directory's
 * "campaign/<campaign>/<mission>/" (koprulu-<campaign>-<mission>.mpq), "music/" (koprulu-music.mpq), "smk/"
 * (koprulu-cinematics.mpq); the core's is koprulu-core.mpq and "". */
void kp_repack_group(const char *key, char *out, size_t size, char *prefix, size_t prefix_size);

/* The repack of an install's files (res holds one install and no repack) into c->out: 0, or -1 with the reason (the
 * report holds what was written up to then). */
int kp_repack_write(KpRes *res, const KpRepackConfig *c, KpRepackReport *report, char *err, size_t err_size);

#ifdef __cplusplus
}
#endif

#endif
