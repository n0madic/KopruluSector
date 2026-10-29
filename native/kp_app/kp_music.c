/* The music container's track index (kp_music.h). */
#include "kp_music.h"

#include <stdio.h>
#include <string.h>

static int fail(KpError *err, const char *what)
{
	snprintf(err->msg, sizeof err->msg, "music.kpd: %s", what);
	return -1;
}

int kp_music_index(KpMusicIndex *ix, const uint8_t *head, size_t head_size, size_t file_size, KpMusicRead read, void *ctx,
	KpError *err)
{
	KpKpf c;
	const KpKpfSection *s;
	uint8_t count, entry[5];
	uint64_t at;
	int k;
	memset(ix, 0, sizeof *ix);
	if (kp_kpf_parse_prefix(&c, head, head_size, file_size, "music.kpd", err) != 0) return -1;
	s = kp_kpf_find(&c, "TRKS");
	if (!s || s->size < 1) {
		kp_kpf_free(&c);
		return fail(err, "no TRKS section");
	}
	if (read(ctx, s->offset, &count, 1) != 0) {
		kp_kpf_free(&c);
		return fail(err, "cannot read the track count");
	}
	if (count > KP_MUSIC_TRACKS) {
		kp_kpf_free(&c);
		return fail(err, "more tracks than the index holds");
	}
	at = 1;
	for (k = 0; k < count; k++) {
		KpMusicTrack *t = &ix->tracks[k];
		const char *why = NULL;
		uint32_t size = 0;
		if (at + 5 > s->size) why = "the track table is truncated";
		else if (read(ctx, s->offset + at, entry, sizeof entry) != 0) why = "cannot read a track's header";
		else if ((size = kp_le_u32(entry + 1)) > s->size - at - 5) why = "a track runs past the TRKS section";
		if (why) {
			kp_kpf_free(&c);
			memset(ix, 0, sizeof *ix);
			return fail(err, why);
		}
		t->offset = (uint32_t)(s->offset + at + 5);
		t->size = size;
		t->loops = entry[0] & 1;
		at += 5 + size;
	}
	ix->count = count;
	kp_kpf_free(&c);
	return 0;
}
