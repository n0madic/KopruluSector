/*
 * The sound mixer of the interface (plan phase 9D, phase S stage S5): kp_ui decides what plays on which of the
 * original's eight channels (kp_ui_sound.c); this plays its commands through SDL3_mixer. A channel's new sound stops
 * the one it held. The music plays the music container's track, a trigger's WAV the mission's sounds container (by the
 * WAVS index of its string id), a streamed sfxdata file (the result dialogs' sounds) its pack's sound as a stream. The
 * original data (ADR 0018, stage D5) plays its files: sfxdata's WAVs, the scenario's WAVs by their strings, the music
 * MUSI names streamed from its archive; the languages' speech is the converted data's only.
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

float kp_audio_gain(int32_t volume)
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
	au->cine = MIX_CreateTrack(au->mixer);
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

/* The music stopped and its stream let go (the file closed). */
static void drop_music(KpAudio *au)
{
	MIX_StopTrack(au->music, 0);
	MIX_SetTrackAudio(au->music, NULL);
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
	kp_pack_free(&au->speech);
	free(au->speech_bytes);
	au->speech_bytes = NULL;
	au->speech_tried = 0;
}

void kp_audio_free(KpAudio *au)
{
	if (au->mixer) MIX_DestroyMixer(au->mixer); /* its tracks with it, the music's stream too */
	free_audio(au->sfx, KP_APP_SFX);
	free_audio(au->wavs, KP_APP_WAVS);
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

/* The chosen speech's voices, none for the base's or when its file is missing (one line). A change of the speech drops
 * the voices read and the sfx decoded from them. */
static const KpPack *speech_pack(KpApp *a)
{
	KpAudio *au = &a->audio;
	const char *id = a->flags.speech;
	if (strcmp(au->speech_id, id) != 0) {
		free_audio(au->sfx, KP_APP_SFX);
		kp_pack_free(&au->speech);
		free(au->speech_bytes);
		au->speech_bytes = NULL;
		au->speech_tried = 0;
		snprintf(au->speech_id, sizeof au->speech_id, "%s", id);
	}
	if (id[0] == 0 || strcmp(id, KP_LOCALE_BASE) == 0) return NULL;
	if (!au->speech_bytes && !au->speech_tried) {
		char path[64];
		size_t n = 0;
		KpError e;
		au->speech_tried = 1;
		snprintf(path, sizeof path, "generated/speech/%s/units.kpd", id);
		if ((au->speech_bytes = kp_app_read(a, path, &n)) == NULL) {
			SDL_Log("audio: speech %s: no %s, the base's voices", id, path);
		} else if (kp_mission_sounds_load(&au->speech, au->speech_bytes, n, path, &e) != 0) {
			SDL_Log("audio: %s, the base's voices", e.msg);
			free(au->speech_bytes);
			au->speech_bytes = NULL;
		}
	}
	return au->speech_bytes ? &au->speech : NULL;
}

/* An original file read through kp_res (ADR 0018, stage D5), decoded whole: the sounds' WAVs are short (a missing one
 * says nothing: the original's sounds the data lacks are silent too). */
static MIX_Audio *load_file(KpApp *a, const char *path)
{
	char err[KP_ERROR_SIZE];
	size_t size = 0;
	uint8_t *bytes = kp_res_read(a->res, path, &size, NULL, err, sizeof err);
	MIX_Audio *audio;
	if (!bytes) {
		if (err[0]) SDL_Log("audio: %s", err);
		return NULL;
	}
	audio = MIX_LoadAudio_IO(a->audio.mixer, SDL_IOFromConstMem(bytes, size), true, true);
	if (!audio) SDL_Log("audio: %s: %s", path, SDL_GetError());
	free(bytes);
	return audio;
}

/* The sound of an sfx id: the original data's file (sfxdata's path), else the speech's voice, else its pack's (decoded
 * on first play and kept until the world ends). */
static MIX_Audio *sfx(KpApp *a, uint32_t id)
{
	KpAudio *au = &a->audio;
	const KpPack *pack, *speech;
	const KpPackSound *s = NULL;
	int p;
	if (a->res) {
		if (id >= KP_APP_SFX || id >= a->ui_data.sound_path_count || !a->ui_data.sound_paths[id]) return NULL;
		if (!au->sfx[id]) au->sfx[id] = load_file(a, a->ui_data.sound_paths[id]);
		return au->sfx[id];
	}
	if (id >= KP_APP_SFX || id >= au->pack_count) return NULL;
	speech = speech_pack(a);
	if (au->sfx[id]) return au->sfx[id];
	if (speech) s = kp_pack_sound(speech, id);
	if (!s) {
		p = au->pack_of[id];
		if (p < 1 || p > KP_APP_SOUND_PACKS || !(pack = sound_pack(a, p - 1))) return NULL;
		s = kp_pack_sound(pack, id);
	}
	if (s) au->sfx[id] = load(au, s->ogg, s->size, 1);
	return au->sfx[id];
}

/* packs/music.kpd: its track index is read at start (kp_app_music_open); a track plays as a stream of its bytes in the
 * file (the web's: in the container it keeps in memory), which the mixer decodes as it plays (MIX_SetTrackIOStream), so
 * no track is held in memory. */
#define MUSIC_PATH "generated/packs/music.kpd"

#ifdef __EMSCRIPTEN__
static int music_read(void *ctx, uint64_t offset, uint8_t *out, size_t size)
{
	const KpApp *a = (const KpApp *)ctx;
	if (!a->music_bytes || offset > a->music_size || size > a->music_size - offset) return -1;
	memcpy(out, a->music_bytes + offset, size);
	return 0;
}
#endif

int kp_app_music_open(KpApp *a, char *err, size_t err_size)
{
	KpError e;
	int rc;
#ifdef __EMSCRIPTEN__
	/* the web's files are in memory already: the container is kept whole and the tracks are slices of it */
	if (!(a->music_bytes = kp_app_read(a, MUSIC_PATH, &a->music_size))) {
		snprintf(err, err_size, "cannot read %s: %s", MUSIC_PATH, SDL_GetError());
		return -1;
	}
	rc = kp_music_index(&a->music_index, a->music_bytes, a->music_size, a->music_size, music_read, a, &e);
#else
	{
		uint8_t head[KP_MUSIC_HEAD];
		char full[1200];
		SDL_IOStream *io;
		Sint64 size;
		size_t n;
		kp_app_path(a, MUSIC_PATH, full, sizeof full);
		if (!(io = kp_io_open_head(full, head, sizeof head, &n, &size))) {
			snprintf(err, err_size, "cannot read %s: %s", MUSIC_PATH, SDL_GetError());
			return -1;
		}
		rc = kp_music_index(&a->music_index, head, n, (size_t)size, kp_io_read_into, io, &e);
		SDL_CloseIO(io);
	}
#endif
	if (rc != 0) SDL_Log("audio: %s (the game plays without music)", e.msg);
	return 0;
}

#ifndef __EMSCRIPTEN__
/* A track's bytes in the file as a stream of their own (offset 0 at the track's first byte), read as the mixer decodes
 * them; closing it closes the file. */
typedef struct {
	SDL_IOStream *file;
	Sint64 start, size, pos;
} TrackRange;

static Sint64 SDLCALL range_size(void *u) { return ((TrackRange *)u)->size; }

static Sint64 SDLCALL range_seek(void *u, Sint64 offset, SDL_IOWhence whence)
{
	TrackRange *r = (TrackRange *)u;
	Sint64 at = whence == SDL_IO_SEEK_CUR ? r->pos + offset : whence == SDL_IO_SEEK_END ? r->size + offset : offset;
	if (at < 0 || at > r->size) return -1;
	r->pos = at;
	return at;
}

static size_t SDLCALL range_read(void *u, void *ptr, size_t size, SDL_IOStatus *status)
{
	TrackRange *r = (TrackRange *)u;
	size_t want = (uint64_t)(r->size - r->pos) < size ? (size_t)(r->size - r->pos) : size, got = 0;
	if (want > 0 && SDL_SeekIO(r->file, r->start + r->pos, SDL_IO_SEEK_SET) >= 0) got = SDL_ReadIO(r->file, ptr, want);
	r->pos += (Sint64)got;
	if (got < want) *status = SDL_GetIOStatus(r->file) == SDL_IO_STATUS_ERROR ? SDL_IO_STATUS_ERROR : SDL_IO_STATUS_EOF;
	else if (got < size) *status = SDL_IO_STATUS_EOF;
	return got;
}

static bool SDLCALL range_close(void *u)
{
	TrackRange *r = (TrackRange *)u;
	bool ok = SDL_CloseIO(r->file);
	free(r);
	return ok;
}

SDL_IOStream *kp_app_range_io(const char *full, Sint64 start, Sint64 size)
{
	SDL_IOStreamInterface iface;
	TrackRange *r;
	SDL_IOStream *io;
	if (!(r = (TrackRange *)calloc(1, sizeof *r))) return NULL;
	r->start = start;
	r->size = size;
	if (!(r->file = SDL_IOFromFile(full, "rb")) || SDL_GetIOSize(r->file) < r->start + r->size) {
		if (r->file) SDL_CloseIO(r->file);
		free(r);
		return NULL;
	}
	SDL_INIT_INTERFACE(&iface);
	iface.size = range_size;
	iface.seek = range_seek;
	iface.read = range_read;
	iface.close = range_close;
	if (!(io = SDL_OpenIO(&iface, r))) {
		SDL_CloseIO(r->file);
		free(r);
	}
	return io;
}
#endif

/* kp_res's stream of a file as an SDL_IOStream (the music's tracks: the mixer reads it on its own thread, through the
 * stream's own handles). */
typedef struct {
	KpResStream *s;
	Sint64 size, pos;
} ResIo;

static Sint64 SDLCALL res_size(void *u) { return ((ResIo *)u)->size; }

static Sint64 SDLCALL res_seek(void *u, Sint64 offset, SDL_IOWhence whence)
{
	ResIo *r = (ResIo *)u;
	Sint64 at = whence == SDL_IO_SEEK_CUR ? r->pos + offset : whence == SDL_IO_SEEK_END ? r->size + offset : offset;
	if (at < 0 || at > r->size) return -1;
	r->pos = at;
	return at;
}

static size_t SDLCALL res_read(void *u, void *ptr, size_t size, SDL_IOStatus *status)
{
	ResIo *r = (ResIo *)u;
	size_t want = (uint64_t)(r->size - r->pos) < size ? (size_t)(r->size - r->pos) : size;
	if (want > 0 && kp_res_stream_read(r->s, (uint64_t)r->pos, ptr, want) != 0) {
		*status = SDL_IO_STATUS_ERROR;
		return 0;
	}
	r->pos += (Sint64)want;
	if (want < size) *status = SDL_IO_STATUS_EOF;
	return want;
}

static bool SDLCALL res_close(void *u)
{
	ResIo *r = (ResIo *)u;
	kp_res_stream_close(r->s);
	free(r);
	return true;
}

/* A file of the original data as a stream (NULL: not in the data, said in the log when it cannot be read). */
static SDL_IOStream *res_io(KpRes *res, const char *name)
{
	SDL_IOStreamInterface iface;
	ResIo *r;
	SDL_IOStream *io;
	uint64_t size = 0;
	char err[KP_ERROR_SIZE];
	if (!(r = (ResIo *)calloc(1, sizeof *r))) return NULL;
	if (!(r->s = kp_res_stream_open(res, name, &size, err, sizeof err))) {
		if (err[0]) SDL_Log("audio: %s", err);
		free(r);
		return NULL;
	}
	r->size = (Sint64)size;
	SDL_INIT_INTERFACE(&iface);
	iface.size = res_size;
	iface.seek = res_seek;
	iface.read = res_read;
	iface.close = res_close;
	if (!(io = SDL_OpenIO(&iface, r))) {
		kp_res_stream_close(r->s);
		free(r);
	}
	return io;
}

/* The music's track k as a stream of its bytes (NULL: none, or an empty track), and whether it loops: the original
 * data's file the executable's MUSI names (kind 1 loops), else the music container's track. */
static SDL_IOStream *track_stream(KpApp *a, int k, int *loops)
{
	const KpMusicTrack *t;
	*loops = 0;
	if (a->res) {
		char path[KP_APP_FILE_NAME];
		int kind;
		if (!a->musi || kp_ui_music_track(a->musi, a->musi_size, k, path, sizeof path, &kind) != 0 || !path[0]) return NULL;
		*loops = kind == KP_APP_MUSIC_LOOPS;
		return res_io(a->res, path);
	}
	if (k < 0 || k >= a->music_index.count) return NULL;
	t = &a->music_index.tracks[k];
	*loops = t->loops;
	if (t->size == 0) return NULL;
#ifdef __EMSCRIPTEN__
	if (!a->music_bytes || t->offset > a->music_size || t->size > a->music_size - t->offset) return NULL;
	return SDL_IOFromConstMem(a->music_bytes + t->offset, t->size);
#else
	{
		char full[1200];
		kp_app_path(a, MUSIC_PATH, full, sizeof full);
		return kp_app_range_io(full, (Sint64)t->offset, (Sint64)t->size);
	}
#endif
}

/* A trigger's WAV: the mission's WAVS index of its string id; its file the string names in the scenario's directory
 * (the original data's), else the mission's sounds container's. */
static MIX_Audio *wav(KpApp *a, uint32_t string_id)
{
	KpAudio *au = &a->audio;
	const KpMission *m = a->world.mission;
	int k;
	if (!m) return NULL; /* no game's world */
	for (k = 0; k < m->sound_count; k++) {
		const KpMissionSound *s = &m->sounds[k];
		if (s->string_id == string_id && s->index < KP_APP_WAVS) {
			const KpPackSound *ps;
			if (au->wavs[s->index]) return au->wavs[s->index];
			if (a->res) {
				const KpAppMission *entry = a->flow.current >= 0 ? &a->missions[a->flow.current] : NULL;
				char path[KP_APP_FILE_NAME];
				if (entry && entry->directory[0]
					&& kp_mission_sound_path(m, entry->directory, string_id, path, sizeof path) == 1) {
					au->wavs[s->index] = load_file(a, path);
				}
				return au->wavs[s->index];
			}
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
	free_audio(a->audio.wavs, KP_APP_WAVS);
	free_audio(a->audio.sfx, KP_APP_SFX);
}

/* The track's input (set before) played at gain g and pan, looping or once. */
static bool start(MIX_Track *t, float g, int32_t pan, int loops)
{
	MIX_StereoGains gains;
	SDL_PropertiesID props = 0;
	bool ok;
	MIX_SetTrackGain(t, g);
	gains.left = pan > 0 ? kp_audio_gain(-pan) : 1.0f;
	gains.right = pan < 0 ? kp_audio_gain(pan) : 1.0f;
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

static bool play(MIX_Track *t, MIX_Audio *audio, float g, int32_t pan, int loops)
{
	MIX_SetTrackAudio(t, audio);
	return start(t, g, pan, loops);
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
					play(t, s, kp_audio_gain(c->volume), c->pan < -KP_APP_PAN_FULL ? -KP_APP_PAN_FULL
						: c->pan > KP_APP_PAN_FULL ? KP_APP_PAN_FULL : c->pan, 0);
					au->started++;
				}
			}
			break;
		case KP_UI_SOUND_MUSIC: {
			/* StartMusicTrack 0x46f1b0: a new track closes the stream before it */
			int loops;
			SDL_IOStream *io;
			drop_music(au); /* the old track's stream closed before the next one opens */
			io = track_stream(a, c->sound, &loops);
			au->track = c->sound;
			au->has_track = 1;
			au->music_volume = c->volume;
			/* the stream is the track's from here (closed by it, also when it is refused) */
			if (io && MIX_SetTrackIOStream(au->music, io, true)) start(au->music, kp_audio_gain(c->volume), 0, loops);
			else if (io) SDL_Log("audio: music track %d: %s", (int)c->sound, SDL_GetError());
			break;
		}
		case KP_UI_SOUND_MUSIC_VOLUME:
			au->music_volume = c->volume;
			MIX_SetTrackGain(au->music, kp_audio_gain(c->volume));
			break;
		case KP_UI_SOUND_MUSIC_STOP:
			drop_music(au);
			au->has_track = 0;
			break;
		case KP_UI_SOUND_STREAM:
		case KP_UI_SOUND_STREAM_SFX: { /* a trigger's WAV, or an sfxdata file streamed (0x49e7c0) */
			MIX_Audio *s = c->type == KP_UI_SOUND_STREAM ? wav(a, c->sound) : sfx(a, c->sound);
			if (s) {
				play(au->streams[au->stream_next], s, kp_audio_gain(c->volume), 0, 0);
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
				if (s) play(au->click, s, kp_audio_gain(c->volume), 0, 0);
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
	drop_music(au);
	if (au->cine) MIX_StopTrack(au->cine, 0);
	au->has_track = 0;
}
