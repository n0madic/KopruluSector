/*
 * The music container's track index (packs/music.kpd, kind MUSC, section TRKS: u8 count, per track u8 flags, u32 size,
 * the OGG). The product reads the index at start, from the file's head and the tracks' small headers, and reads a
 * track's bytes only when it plays (kp_app_audio.c), so the 31 MB container is not in memory. Headless: the host
 * reads the file through a callback.
 */
#ifndef KP_MUSIC_H
#define KP_MUSIC_H

#include <stddef.h>
#include <stdint.h>

#include "kp_kpf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_MUSIC_TRACKS 32
#define KP_MUSIC_HEAD 4096 /* the file's first bytes the index needs: the header and the section table */

typedef struct {
	uint32_t offset, size; /* the OGG's place in the file (size 0: an empty track) */
	int loops;
} KpMusicTrack;

typedef struct {
	KpMusicTrack tracks[KP_MUSIC_TRACKS];
	int count; /* the tracks indexed (the container's, at most KP_MUSIC_TRACKS) */
} KpMusicIndex;

/* Reads size bytes at offset of the file into out; 0 on success. */
typedef int (*KpMusicRead)(void *ctx, uint64_t offset, uint8_t *out, size_t size);

/* The index of the container of file_size bytes whose first head_size are in head; -1 with err (and no tracks) when it
 * has no TRKS section, more than KP_MUSIC_TRACKS tracks, a track table cut short, a header that cannot be read or a
 * track that runs past the section. */
int kp_music_index(KpMusicIndex *ix, const uint8_t *head, size_t head_size, size_t file_size, KpMusicRead read, void *ctx,
	KpError *err);

#ifdef __cplusplus
}
#endif

#endif
