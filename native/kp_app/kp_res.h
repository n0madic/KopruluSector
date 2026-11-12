/*
 * The original data read at run time (ADR 0018, stage D1): files by their MPQ path ("arr\\units.dat", any case, '\\'
 * or '/') from the sources the player names, each checked against the profile's input manifest (kp_defs.h):
 *
 *   - a repack (koprulu.mpq, or koprulu-core.mpq and the groups its listing names; kp_convert repack): the
 *     converter's archives of the files the game reads, in their original formats (Remastered's rebuilt 1.17 files
 *     among them, a WAV as Ogg Vorbis where that is smaller); its "(koprulu)" file names the profile and each file's
 *     size, SHA-256 and status (the source's: exact, derived, differs, ...; an Ogg's WAV's digest, size and header).
 *     It is read before the install; an exact or derived file is the manifest's (its listed digest checked against
 *     the manifest's), a file that is not its listed size (one the simulation is built from: its listed SHA-256 too),
 *     or a repack of another profile, falls back to the install (Android's groups are read in place, slices of the
 *     signed APK: KpResConfig.repack_file);
 *   - an install: 1.17 (StarCraft.exe and the profile's archives, the first copy of a name in the profile's order;
 *     a copy that differs from the manifest is refused: a file the simulation is built from to the byte, one that
 *     only shows or plays by its size) or StarCraft: Remastered (.build.info, CASC: the converter's
 *     ScrSource rules, scdata/casc/source.py: its prefixes, the moved files, the tilesets' 1.17 prefix, .vx4 from
 *     .vx4ex; the files only Remastered's own formats hold rebuilt from them, stage D6, kp_res_scr.c: PCX and GRP from
 *     DDS, TBL and dialog BINs from its XML strings, an SMK as the KPW1 over its WebM, kp_smk.h; a copy no rule brings
 *     to the manifest's digest is read as it is, "differs").
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

/* What a read found (the converter's statuses, scdata/casc/source.py) */
typedef enum {
	KP_RES_MISSING,    /* no source has it */
	KP_RES_ABSENT,     /* the manifest lists it as absent from the reference install */
	KP_RES_REPACKED,   /* from a repack, equal to its listing */
	KP_RES_EXACT,      /* from an install, the manifest's (1.17: a file that only shows or plays by its size) */
	KP_RES_DERIVED,    /* rebuilt from Remastered's copy, or its own formats, to the manifest's digest */
	KP_RES_DIFFERS,    /* Remastered's copy, which no rule brings to the manifest's digest */
	KP_RES_UNLISTED,   /* a name the manifest does not hold, read unchecked */
	KP_RES_REFUSED,    /* a 1.17 copy that differs from the manifest */
	KP_RES_ERROR,      /* a source could not read it */
	/* rebuilt from Remastered's own formats (kp_res_scr.c: DDS, XML, WebM) to other bytes than the 1.17 file's: */
	KP_RES_EQUIVALENT,    /* a PCX of the same pixels (another palette's bytes) */
	KP_RES_COLOUR_EQUAL,  /* a PCX of the same colours */
	KP_RES_APPROXIMATION, /* neither (lossy art, edited texts, a movie that stands in for its SMK) */
	KP_RES_REMASTERED,    /* Remastered's own file by its CASC name (kp_res_raw_read), which no manifest lists */
	KP_RES_CONVERTED,     /* a repack's own file by a Remastered name: kp_convert's of the install's (a 1.17 movie as WebM) */
	KP_RES_STATUS_COUNT
} KpResStatus;

typedef struct KpRes KpRes;
typedef struct KpResStream KpResStream;

/* What KpResConfig.record is told of a name the caller looks for (ADR 0019: kp_convert manifest) */
typedef enum {
	KP_RES_RECORD_READ,    /* read whole: its bytes */
	KP_RES_RECORD_OPENED,  /* its head read or opened as a stream: no bytes (the recorder reads it whole) */
	KP_RES_RECORD_MISSING, /* looked for, not found */
	KP_RES_RECORD_DAMAGED, /* a copy the sources hold but cannot give whole (a disc copied with a bad read, issue #2) */
} KpResRecord;

typedef struct {
	const KpDefsProfile *profile; /* NULL: kp_defs_bw_1_17_0_1 */
	void (*log)(void *ctx, const char *line); /* NULL: no log */
	void *log_ctx;
	/* Each name of the profile's files the caller looks for, as it names it (kp_res_read, kp_res_head,
	 * kp_res_stream_open, kp_res_exists; not Remastered's own names, kp_res_raw_*), and what was found. With it a 1.17
	 * install is read as the profile's reference, which kp_convert manifest writes the input manifest from: as it is,
	 * nothing checked against the manifest, the names it lists as absent looked for. NULL: none. */
	void (*record)(void *ctx, const char *name, KpResRecord what, const uint8_t *data, size_t size);
	void *record_ctx;
	/* The program's own repack, outside the folders (Android: its groups in the APK's assets), NULL: none. A repack
	 * file by its name (koprulu.mpq, else koprulu-core.mpq, then the groups the core's listing names): 1 and what
	 * kp_mpq_open opens it by in out (a slice, kp_mpq_slice_name), 0 when there is no such file, -1 with the reason in
	 * out. */
	int (*repack_file)(void *ctx, const char *file, char *out, size_t size);
	void *repack_ctx;
} KpResConfig;

/* What a folder holds. */
KpResSourceKind kp_res_detect(const char *dir, const KpDefsProfile *profile);
const char *kp_res_source_name(KpResSourceKind kind);
const char *kp_res_status_name(KpResStatus status);

/* The sources of the folders (kp_res_detect each; one install, the first; any number of repacks) and the config's
 * repack_file's, or NULL with the reason: a folder that holds no data, an install whose archives cannot be opened, no
 * usable source. */
KpRes *kp_res_open(const char *const *dirs, int count, const KpResConfig *config, char *err, size_t err_size);
void kp_res_close(KpRes *r);
/* A repack's archive added (the web's groups, fetched before a mission): 0, or -1 with the reason. */
int kp_res_add_repack(KpRes *r, const char *path, char *err, size_t err_size);
/* The group of the repack (a file the core's listing names, beside the core) whose prefix a name has, when it is not
 * open: 1 and its file name in out, else 0 (the name is in no group, or its group is open). The web fetches it and
 * adds it (kp_res_add_repack) before a mission starts. */
int kp_res_group_wanted(const KpRes *r, const char *name, char *out, size_t size);
/* The folder of the repack's core (its groups lie beside it), "" without a repack or when the core is not a folder's
 * (KpResConfig.repack_file). */
const char *kp_res_repack_dir(const KpRes *r);
KpResSourceKind kp_res_install(const KpRes *r);
int kp_res_repacks(const KpRes *r);
/* The profile the sources are read by. */
const KpDefsProfile *kp_res_profile(const KpRes *r);

/* A malloc'd copy of a file's bytes (NUL terminated past size) and its status, or NULL: err empty when no source has
 * it (missing, absent), the reason otherwise (a refused copy, a failed read: a damaged copy). */
uint8_t *kp_res_read(KpRes *r, const char *name, size_t *size, KpResStatus *status, char *err, size_t err_size);
/* Whether a source has the file; a Remastered install has every file of the manifest (source.py has: a rebuild stands
 * in for one it lacks). */
int kp_res_exists(KpRes *r, const char *name);

/* A file read in pieces on any thread (music, movies), from handles of its own: its size (the manifest's for an
 * install's listed file, whose sectors' CRCs are checked as they are read; a repack's listed size), then reads. */
KpResStream *kp_res_stream_open(KpRes *r, const char *name, uint64_t *size, char *err, size_t err_size);
int kp_res_stream_read(KpResStream *s, uint64_t offset, void *out, size_t size);
void kp_res_stream_close(KpResStream *s);
/* A file's first `want` bytes (fewer when it is shorter) and its size, read on the calling thread through the KpRes's
 * own handles (the sounds' and the music's headers: the size checked as a stream's, not the digest); a repack's Ogg
 * of a WAV answers the WAV's header and size from its listing; NULL as kp_res_read. */
uint8_t *kp_res_head(KpRes *r, const char *name, size_t want, size_t *got, uint64_t *total, char *err, size_t err_size);
/* The reader the core builds its structures through (kp_files.h): kp_res_read and kp_res_head over r, its profile. */
void kp_res_files(KpRes *r, KpFiles *f);

/* The files read so far by status; the list of them (--res-log: "name<TAB>status<TAB>source" a line, by name). */
void kp_res_counts(const KpRes *r, int counts[KP_RES_STATUS_COUNT]);
int kp_res_write_log(const KpRes *r, const char *path, char *err, size_t err_size);

/* Remastered's own files by their CASC names ("locales/ruRU/Assets/sound/terran/advisor/tadupd00.wav", any case, '/'
 * or '\\'; ADR 0019, stage C2): a repack's copy (listed "remastered", or "converted": its size checked, a CHK's SHA-256
 * too; a group's not open, none), else the Remastered install's, unchecked. A malloc'd copy, or NULL (err empty when no source has it). */
uint8_t *kp_res_raw_read(KpRes *r, const char *raw, size_t *size, char *err, size_t err_size);
/* Its first `want` bytes and size as kp_res_head (a repack's Ogg of a WAV: the WAV's header and size). */
uint8_t *kp_res_raw_head(KpRes *r, const char *raw, size_t want, size_t *got, uint64_t *total, char *err, size_t err_size);
/* A Remastered install's file's content key (CASC's MD5 of its bytes, from the storage's index; ADR 0019, stage C6: the
 * HD interface's placement table names its files' keys): 0, or -1 when no install names it (a repack keeps none). */
int kp_res_raw_key(KpRes *r, const char *raw, uint8_t key[16]);
/* Whether a repack lists the file (a group's read by ranges too, open or not: kp_res_raw_range) or the Remastered install
 * holds it (named and downloaded: kp_casc_held). */
int kp_res_raw_held(KpRes *r, const char *raw);
/* Where a repack keeps a file of a group read by ranges that is not open (the cinematics', ADR 0019 stage C4: the
 * core's listing names each of its files' place): 1, its group's file name ("koprulu-cinematics.mpq", beside the core)
 * in file and its bytes' offset in it and size; 0 when no listing places it so, or its group is open (kp_res_raw_read
 * reads it). The web fetches those bytes alone. */
int kp_res_raw_range(const KpRes *r, const char *raw, char *file, size_t file_size, uint64_t *offset, uint32_t *size);
/* The core's reader over the raw names (kp_files.h): kp_res_raw_read and kp_res_raw_head over r, its profile. */
void kp_res_raw_files(KpRes *r, KpFiles *f);

/* Where a Remastered file lies for a reader on another thread (ADR 0019, stage C5: the HD pages, which the loader's
 * thread reads; a KpRes is one thread's): an open repack's archive (what kp_mpq_open opens: a file's path or a slice's
 * name) and its MPQ name there, or the Remastered install's folder and the CASC name. */
#define KP_RES_PATH 1024
typedef struct {
	KpResSourceKind kind; /* KP_RES_SOURCE_REPACK or KP_RES_SOURCE_CASC */
	char path[KP_RES_PATH];
	char name[KP_RES_PATH];
	uint64_t size;        /* a repack's listed size; 0 for the install's (kp_res_reader_size) */
} KpResPlace;
typedef struct KpResReader KpResReader;

/* A raw name's place (as kp_res_raw_read would read it; noted among the files read): 1, or 0 when no open repack lists
 * it (its group not open: kp_res_raw_range) and the install does not hold it, or an open repack keeps it as an Ogg
 * of a WAV (its head: no place holds the WAV's bytes). */
int kp_res_raw_place(KpRes *r, const char *raw, KpResPlace *p);
/* A reader of places on one thread, through handles of its own (the last archive or install, and file, it read stay
 * open for the next read). */
KpResReader *kp_res_reader_new(void);
/* The size of the place's file: 0, or -1 when it cannot be opened. */
int kp_res_reader_size(KpResReader *h, const KpResPlace *p, uint64_t *size);
/* size bytes at offset of the place's file into out: 0, or -1. */
int kp_res_reader_read(KpResReader *h, const KpResPlace *p, uint64_t offset, void *out, size_t size);
void kp_res_reader_free(KpResReader *h);
/* A Remastered install's text locale ("deDE"; Remastered downloads that language's files), "" none. */
const char *kp_res_text_locale(const KpRes *r);
/* The text locale of a .build.info's text (NUL terminated) into out (5 bytes at least), "" none: of the non-empty lines
 * the first names the columns ("Name!TYPE:size", '|' apart), the row whose Active is 1 is read (else the first; as
 * scdata/casc/storage.py parse_build_info), and of its Tags, ':' apart groups of words, the first group with "text?"
 * and a locale gives that locale (the Python converter's emit/cinematics.py text_locale). */
void kp_res_build_info_text_locale(const char *text, char *out, size_t size);

/* The 1.17 bytes of a tileset file Remastered grew by appending (its prefix, the CV5 groups Remastered filled zeroed),
 * or .vx4 from .vx4ex (scdata/casc/source.py _rebuild): a malloc'd copy of want bytes, or NULL when no rule
 * applies. key is the normalized 1.17 name, casc_name the copy's. */
uint8_t *kp_res_casc_rebuild(const char *key, const char *casc_name, const uint8_t *data, size_t size, uint32_t want);

/* The repack's listing file and its first line */
#define KP_RES_LISTING "(koprulu)"
#define KP_RES_LISTING_MAGIC "koprulu repack 4"

#ifdef __cplusplus
}
#endif

#endif
