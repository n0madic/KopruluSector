/*
 * The sound mixer of the interface (plan phase 9D, phase S stage S5): kp_ui decides what plays on which of the
 * original's eight channels (kp_ui_sound.c); this plays its commands through SDL3_mixer. A channel's new sound stops
 * the one it held. The music plays the music container's track, a trigger's WAV the mission's sounds container (by the
 * WAVS index of its string id), a streamed sfxdata file (the result dialogs' sounds) its pack's sound as a stream.
 * DirectSound's volume (hundredths of a decibel) and pan become exact gains: a pan of +2000 attenuates the left side by
 * 20 dB.
 */
#include "kp_app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* sounds.kpd PACK's packs in their 1-based order */
static const char *const PACKS[KP_APP_SOUND_PACKS] = { "effects", "terran", "protoss", "zerg" };

static float gain(int32_t volume)
{
	if (volume < -10000) volume = -10000;
	return powf(10.0f, (float)volume / 2000.0f);
}

int kp_audio_init(KpAudio *au, const uint8_t *sounds_kpd, size_t size)
{
	KpKpf c;
	KpError err;
	const KpKpfSection *s;
	int k;
	memset(au, 0, sizeof *au);
	if (kp_kpf_parse(&c, sounds_kpd, size, "sounds.kpd", 0, &err) == 0) {
		if ((s = kp_kpf_find(&c, "PACK")) != NULL && s->size >= 2) {
			au->pack_count = (uint16_t)kp_le_u16(s->data);
			if (au->pack_count > s->size - 2) au->pack_count = (uint16_t)(s->size - 2);
			au->pack_of = s->data + 2;
		}
		kp_kpf_free(&c);
	}
	if (!MIX_Init()) {
		SDL_Log("audio: %s (the game plays without sound)", SDL_GetError());
		return -1;
	}
	au->mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
	if (!au->mixer) {
		SDL_Log("audio: no device: %s (the game plays without sound)", SDL_GetError());
		return -1;
	}
	for (k = 0; k < KP_APP_CHANNELS; k++) au->channels[k] = MIX_CreateTrack(au->mixer);
	for (k = 0; k < KP_APP_STREAMS; k++) au->streams[k] = MIX_CreateTrack(au->mixer);
	au->music = MIX_CreateTrack(au->mixer);
	au->click = MIX_CreateTrack(au->mixer);
	return 0;
}

static void free_audio(MIX_Audio **list, int n)
{
	int k;
	for (k = 0; k < n; k++) {
		if (list[k]) MIX_DestroyAudio(list[k]);
		list[k] = NULL;
	}
}

/* The music track held goes (its bytes with it, once the mixer's track lets go of it). */
static void drop_track(KpAudio *au)
{
	if (au->track_audio) MIX_DestroyAudio(au->track_audio);
	au->track_audio = NULL;
}

void kp_audio_drop_packs(KpAudio *au)
{
	int k;
	for (k = 0; k < KP_APP_SOUND_PACKS; k++) {
		kp_pack_free(&au->own[k]);
		free(au->own_bytes[k]);
		au->own_bytes[k] = NULL;
		au->own_tried[k] = 0;
	}
}

void kp_audio_free(KpAudio *au)
{
	if (au->mixer) MIX_DestroyMixer(au->mixer); /* its tracks with it */
	free_audio(au->sfx, KP_APP_SFX);
	drop_track(au);
	free_audio(au->wavs, 256);
	kp_audio_drop_packs(au);
	MIX_Quit();
	memset(au, 0, sizeof *au);
}

static MIX_Audio *load(KpAudio *au, const uint8_t *ogg, uint32_t size, int predecode)
{
	if (!ogg || size == 0) return NULL;
	return MIX_LoadAudio_IO(au->mixer, SDL_IOFromConstMem(ogg, size), predecode, true);
}

/* Sound pack p (from 0): the playing world's, else the mixer's own copy (the glue has no world), read on first use. */
static const KpPack *sound_pack(KpApp *a, int p)
{
	KpAudio *au = &a->audio;
	int k;
	for (k = 0; k < a->world.pack_count; k++) {
		if (strcmp(a->world.packs[k].name, PACKS[p]) == 0) return &a->world.packs[k].pack;
	}
	if (!au->own_bytes[p] && !au->own_tried[p]) {
		char path[64];
		size_t n = 0;
		KpError e;
		au->own_tried[p] = 1;
		snprintf(path, sizeof path, "generated/packs/%s.kpd", PACKS[p]);
		if ((au->own_bytes[p] = kp_app_read(a, path, &n)) != NULL && kp_pack_load(&au->own[p], au->own_bytes[p], n, path, &e) != 0) {
			SDL_Log("audio: %s", e.msg);
			free(au->own_bytes[p]);
			au->own_bytes[p] = NULL;
		}
	}
	return au->own_bytes[p] ? &au->own[p] : NULL;
}

/* The sound of an sfx id from its pack (decoded on first play and kept until the world ends). */
static MIX_Audio *sfx(KpApp *a, uint32_t id)
{
	KpAudio *au = &a->audio;
	const KpPack *pack;
	const KpPackSound *s;
	int p;
	if (id >= KP_APP_SFX || id >= au->pack_count) return NULL;
	if (au->sfx[id]) return au->sfx[id];
	p = au->pack_of[id];
	if (p < 1 || p > KP_APP_SOUND_PACKS || !(pack = sound_pack(a, p - 1))) return NULL;
	if ((s = kp_pack_sound(pack, id)) != NULL) au->sfx[id] = load(au, s->ogg, s->size, 1);
	return au->sfx[id];
}

/* packs/music.kpd: its track index is read at start (kp_app_music_open); a track's bytes are read when it plays. */
#define MUSIC_PATH "generated/packs/music.kpd"

static int music_read(void *ctx, uint64_t offset, uint8_t *out, size_t size)
{
	KpApp *a = (KpApp *)ctx;
#ifdef __EMSCRIPTEN__
	if (!a->music_bytes || offset > a->music_size || size > a->music_size - offset) return -1;
	memcpy(out, a->music_bytes + offset, size);
	return 0;
#else
	uint8_t *b = kp_app_read_range(a, MUSIC_PATH, offset, size);
	if (!b) return -1;
	memcpy(out, b, size);
	free(b);
	return 0;
#endif
}

int kp_app_music_open(KpApp *a, char *err, size_t err_size)
{
	uint8_t *head;
	size_t n, file_size;
	KpError e;
#ifdef __EMSCRIPTEN__
	/* the web's files are in memory already: the container is kept whole and the tracks are slices of it */
	head = a->music_bytes = kp_app_read(a, MUSIC_PATH, &a->music_size);
	n = file_size = a->music_size;
#else
	head = kp_app_read_head(a, MUSIC_PATH, KP_MUSIC_HEAD, &n, &file_size);
#endif
	if (!head) {
		snprintf(err, err_size, "cannot read %s: %s", MUSIC_PATH, SDL_GetError());
		return -1;
	}
	if (kp_music_index(&a->music_index, head, n, file_size, music_read, a, &e) != 0) {
		SDL_Log("audio: %s (the game plays without music)", e.msg);
	}
#ifndef __EMSCRIPTEN__
	free(head);
#endif
	return 0;
}

#ifndef __EMSCRIPTEN__
/* A track's OGG bytes as an SDL stream that owns them: the mixer decodes from it for as long as it plays, and the
 * bytes go when the last reference to the audio does (not while the mixer's thread may still read them). */
typedef struct {
	uint8_t *data;
	size_t size, pos;
} OwnedBytes;

static Sint64 SDLCALL owned_size(void *u) { return (Sint64)((OwnedBytes *)u)->size; }

static Sint64 SDLCALL owned_seek(void *u, Sint64 offset, SDL_IOWhence whence)
{
	OwnedBytes *o = (OwnedBytes *)u;
	Sint64 at = whence == SDL_IO_SEEK_CUR ? (Sint64)o->pos + offset : whence == SDL_IO_SEEK_END ? (Sint64)o->size + offset : offset;
	if (at < 0 || (uint64_t)at > o->size) return -1;
	o->pos = (size_t)at;
	return at;
}

static size_t SDLCALL owned_read(void *u, void *ptr, size_t size, SDL_IOStatus *status)
{
	OwnedBytes *o = (OwnedBytes *)u;
	size_t n = o->size - o->pos < size ? o->size - o->pos : size;
	memcpy(ptr, o->data + o->pos, n);
	o->pos += n;
	if (n < size) *status = SDL_IO_STATUS_EOF;
	return n;
}

static bool SDLCALL owned_close(void *u)
{
	OwnedBytes *o = (OwnedBytes *)u;
	free(o->data);
	free(o);
	return true;
}

/* The audio of bytes (malloc'd, taken over), streamed from memory. */
static MIX_Audio *load_owned(KpAudio *au, uint8_t *bytes, size_t size)
{
	SDL_IOStreamInterface iface;
	OwnedBytes *o = (OwnedBytes *)calloc(1, sizeof *o);
	SDL_IOStream *io;
	if (!o) {
		free(bytes);
		return NULL;
	}
	o->data = bytes;
	o->size = size;
	SDL_INIT_INTERFACE(&iface);
	iface.size = owned_size;
	iface.seek = owned_seek;
	iface.read = owned_read;
	iface.close = owned_close;
	if (!(io = SDL_OpenIO(&iface, o))) {
		free(bytes);
		free(o);
		return NULL;
	}
	return MIX_LoadAudio_IO(au->mixer, io, 0, true); /* closed, and the bytes freed, if it fails */
}
#endif

/* The music container's track k, read from the file when it is not the one held (one track is held at a time). */
static MIX_Audio *track(KpApp *a, int k, int *loops)
{
	KpAudio *au = &a->audio;
	const KpMusicTrack *t;
	*loops = 0;
	if (k < 0 || k >= a->music_index.count) return NULL;
	t = &a->music_index.tracks[k];
	*loops = t->loops;
	if (au->track_audio && au->track_k == k) return au->track_audio;
	drop_track(au);
	if (t->size == 0) return NULL;
#ifdef __EMSCRIPTEN__
	if (!a->music_bytes || t->offset > a->music_size || t->size > a->music_size - t->offset) return NULL;
	au->track_audio = load(au, a->music_bytes + t->offset, t->size, 0);
#else
	{
		uint8_t *bytes = kp_app_read_range(a, MUSIC_PATH, t->offset, t->size);
		au->track_audio = bytes ? load_owned(au, bytes, t->size) : NULL;
	}
#endif
	au->track_k = k;
	return au->track_audio;
}

/* A trigger's WAV: the mission's WAVS index of its string id. */
static MIX_Audio *wav(KpApp *a, uint32_t string_id)
{
	KpAudio *au = &a->audio;
	const KpMission *m = a->world.mission;
	int k;
	if (!m) return NULL; /* no game's world */
	for (k = 0; k < m->sound_count; k++) {
		const KpMissionSound *s = &m->sounds[k];
		if (s->string_id == string_id && s->index < 256) {
			const KpPackSound *ps;
			if (au->wavs[s->index]) return au->wavs[s->index];
			ps = kp_pack_sound(&a->world.mission_sounds, s->index);
			if (ps) au->wavs[s->index] = load(au, ps->ogg, ps->size, 0);
			return au->wavs[s->index];
		}
	}
	return NULL;
}

/* A world's end: its decoded sounds go with it, the trigger WAVs and the sfx of its packs (the next world or the glue
 * decodes again what it plays). */
void kp_audio_mission(KpApp *a)
{
	free_audio(a->audio.wavs, 256);
	free_audio(a->audio.sfx, KP_APP_SFX);
}

static bool play(MIX_Track *t, MIX_Audio *audio, float g, int32_t pan, int loops)
{
	MIX_StereoGains gains;
	SDL_PropertiesID props = 0;
	bool ok;
	MIX_SetTrackAudio(t, audio);
	MIX_SetTrackGain(t, g);
	gains.left = pan > 0 ? gain(-pan) : 1.0f;
	gains.right = pan < 0 ? gain(pan) : 1.0f;
	MIX_SetTrackStereo(t, &gains);
	if (loops) {
		props = SDL_CreateProperties();
		SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
	}
	ok = MIX_PlayTrack(t, props);
	if (props) SDL_DestroyProperties(props);
	if (!ok) SDL_Log("audio: %s", SDL_GetError());
	return ok;
}

void kp_audio_commands(KpApp *a, const KpUiSoundCommand *commands, size_t n)
{
	KpAudio *au = &a->audio;
	size_t k;
	if (!au->mixer) return;
	for (k = 0; k < n; k++) {
		const KpUiSoundCommand *c = &commands[k];
		switch (c->type) {
		case KP_UI_SOUND_PLAY:
		case KP_UI_SOUND_STOP:
			if (c->channel >= 0 && c->channel < KP_APP_CHANNELS) {
				MIX_Track *t = au->channels[(int)c->channel];
				MIX_Audio *s = c->type == KP_UI_SOUND_PLAY ? sfx(a, c->sound) : NULL;
				MIX_StopTrack(t, 0);
				if (s) {
					play(t, s, gain(c->volume), c->pan < -KP_APP_PAN_FULL ? -KP_APP_PAN_FULL
						: c->pan > KP_APP_PAN_FULL ? KP_APP_PAN_FULL : c->pan, 0);
					au->started++;
				}
			}
			break;
		case KP_UI_SOUND_MUSIC: {
			/* StartMusicTrack 0x46f1b0: a new track closes the stream before it */
			int loops;
			MIX_Audio *m;
			MIX_StopTrack(au->music, 0); /* before the next track is read: the old one's bytes go with it */
			m = track(a, c->sound, &loops);
			au->track = c->sound;
			au->has_track = 1;
			au->music_volume = c->volume;
			if (m) play(au->music, m, gain(c->volume), 0, loops);
			break;
		}
		case KP_UI_SOUND_MUSIC_VOLUME:
			au->music_volume = c->volume;
			MIX_SetTrackGain(au->music, gain(c->volume));
			break;
		case KP_UI_SOUND_MUSIC_STOP:
			MIX_StopTrack(au->music, 0);
			drop_track(au);
			au->has_track = 0;
			break;
		case KP_UI_SOUND_STREAM:
		case KP_UI_SOUND_STREAM_SFX: { /* a trigger's WAV, or an sfxdata file streamed (0x49e7c0) */
			MIX_Audio *s = c->type == KP_UI_SOUND_STREAM ? wav(a, c->sound) : sfx(a, c->sound);
			if (s) {
				play(au->streams[au->stream_next], s, gain(c->volume), 0, 0);
				au->stream_next = (au->stream_next + 1) % KP_APP_STREAMS;
				au->streamed++;
			}
			break;
		}
		case KP_UI_SOUND_STREAMS_STOP: {
			int j;
			for (j = 0; j < KP_APP_STREAMS; j++) MIX_StopTrack(au->streams[j], 0);
			break;
		}
		case KP_UI_SOUND_CLICK: /* PlayUiClickSound 0x49e460: not again while it sounds */
			if (!MIX_TrackPlaying(au->click)) {
				MIX_Audio *s = sfx(a, c->sound);
				if (s) play(au->click, s, gain(c->volume), 0, 0);
			}
			break;
		default:
			break;
		}
	}
}

void kp_audio_stop_all(KpAudio *au)
{
	int k;
	if (!au->mixer) return;
	for (k = 0; k < KP_APP_CHANNELS; k++) MIX_StopTrack(au->channels[k], 0);
	for (k = 0; k < KP_APP_STREAMS; k++) MIX_StopTrack(au->streams[k], 0);
	MIX_StopTrack(au->music, 0);
	drop_track(au);
	au->has_track = 0;
}
