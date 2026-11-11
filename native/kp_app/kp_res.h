/*
 * The original data read at run time (ADR 0018, stage D1): files by their MPQ path ("arr\\units.dat", any case, '\\'
 * or '/') from the sources the player names, each checked against the profile's input manifest (kp_defs.h):
 *
 *   - a repack (koprulu.mpq, or koprulu-core.mpq and the groups its listing names): the converter's archive of the
 *     files the game reads, in their original formats (Remastered's rebuilt 1.17 files among them, a sound as Ogg
 *     Vorbis where that is smaller); its "(koprulu)" file names the profile and each file's size and SHA-256. It is
 *     read before the install; a file that differs from its listing, or a repack of another profile, falls back to
 *     the install;
 *   - an install: 1.17 (StarCraft.exe and the profile's archives, the first copy of a name in the profile's order;
 *     a copy that differs from the manifest is refused) or StarCraft: Remastered (.build.info, CASC: the converter's
 *     ScrSource rules, converter/casc/source.py: its prefixes, the moved files, the tilesets' 1.17 prefix, .vx4 from
 *     .vx4ex; a copy no rule brings to the manifest's digest is read as it is, "differs").
 *
 * Nothing is cached on disk. A KpRes is used from one thread; a stream opens handles of its own for its thread.
 */
#ifndef KP_RES_H
#define KP_RES_H

#include <stddef.h>
#include <stdint.h>

#include "kp_defs.h"
#include "kp_files.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	KP_RES_SOURCE_NONE,
	KP_RES_SOURCE_REPACK, /* koprulu.mpq or koprulu-core.mpq */
	KP_RES_SOURCE_MPQ,    /* a 1.17 install */
	KP_RES_SOURCE_CASC,   /* a StarCraft: Remastered install */
} KpResSourceKind;

/* What a read found (the converter's statuses, converter/casc/source.py) */
typedef enum {
	KP_RES_MISSING,    /* no source has it */
	KP_RES_ABSENT,     /* the manifest lists it as absent from the reference install */
	KP_RES_UNREADABLE, /* the manifest lists it as unreadable in the reference install */
	KP_RES_REPACKED,   /* from a repack, equal to its listing */
	KP_RES_EXACT,      /* from an install, equal to the manifest */
	KP_RES_DERIVED,    /* rebuilt from Remastered's copy to the manifest's digest */
	KP_RES_DIFFERS,    /* Remastered's copy, which no rule brings to the manifest's digest */
	KP_RES_UNLISTED,   /* a name the manifest does not hold, read unchecked */
	KP_RES_REFUSED,    /* a 1.17 copy that differs from the manifest */
	KP_RES_ERROR,      /* a source could not read it */
	KP_RES_STATUS_COUNT
} KpResStatus;

typedef struct KpRes KpRes;
typedef struct KpResStream KpResStream;

typedef struct {
	const KpDefsProfile *profile; /* NULL: kp_defs_bw_1_17_0_1 */
	void (*log)(void *ctx, const char *line); /* NULL: no log */
	void *log_ctx;
} KpResConfig;

/* What a folder holds. */
KpResSourceKind kp_res_detect(const char *dir, const KpDefsProfile *profile);
const char *kp_res_source_name(KpResSourceKind kind);
const char *kp_res_status_name(KpResStatus status);

/* The sources of the folders (kp_res_detect each; one install, the first; any number of repacks), or NULL with the
 * reason: a folder that holds no data, an install whose archives cannot be opened. */
KpRes *kp_res_open(const char *const *dirs, int count, const KpResConfig *config, char *err, size_t err_size);
void kp_res_close(KpRes *r);
/* A repack's archive added (the web's groups, fetched before a mission): 0, or -1 with the reason. */
int kp_res_add_repack(KpRes *r, const char *path, char *err, size_t err_size);
KpResSourceKind kp_res_install(const KpRes *r);
int kp_res_repacks(const KpRes *r);

/* A malloc'd copy of a file's bytes (NUL terminated past size) and its status, or NULL: err empty when no source has
 * it (missing, absent, unreadable), the reason otherwise (a refused copy, a failed read). */
uint8_t *kp_res_read(KpRes *r, const char *name, size_t *size, KpResStatus *status, char *err, size_t err_size);
int kp_res_exists(KpRes *r, const char *name);

/* A file read in pieces on any thread (music, movies), from handles of its own: its size (the manifest's for an
 * install's listed file, whose sectors' CRCs are checked as they are read; a repack's listed size), then reads. */
KpResStream *kp_res_stream_open(KpRes *r, const char *name, uint64_t *size, char *err, size_t err_size);
int kp_res_stream_read(KpResStream *s, uint64_t offset, void *out, size_t size);
void kp_res_stream_close(KpResStream *s);
/* A file's first `want` bytes (fewer when it is shorter) and its size, read on the calling thread through the KpRes's
 * own handles (the sounds' and the music's headers: the size checked as a stream's, not the digest); NULL as
 * kp_res_read. */
uint8_t *kp_res_head(KpRes *r, const char *name, size_t want, size_t *got, uint64_t *total, char *err, size_t err_size);
/* The reader the core builds its structures through (kp_files.h): kp_res_read and kp_res_head over r, its profile. */
void kp_res_files(KpRes *r, KpFiles *f);

/* The files read so far by status; the list of them (--res-log: "name<TAB>status<TAB>source" a line, by name). */
void kp_res_counts(const KpRes *r, int counts[KP_RES_STATUS_COUNT]);
int kp_res_write_log(const KpRes *r, const char *path, char *err, size_t err_size);

/* The 1.17 bytes of a tileset file Remastered grew by appending (its prefix, the CV5 groups Remastered filled zeroed),
 * or .vx4 from .vx4ex (converter/casc/source.py _rebuild): a malloc'd copy of want bytes, or NULL when no rule
 * applies. key is the normalized 1.17 name, casc_name the copy's. */
uint8_t *kp_res_casc_rebuild(const char *key, const char *casc_name, const uint8_t *data, size_t size, uint32_t want);

/* The repack's listing file and its first line */
#define KP_RES_LISTING "(koprulu)"
#define KP_RES_LISTING_MAGIC "koprulu repack 1"

#ifdef __cplusplus
}
#endif

#endif
