/* The sound effects' channels (plan phase 9D): snd.cpp's eight channels 0x6aa990 over DirectSound buffers, the sound
 * records 0x6a61d0 (size, last start, duration, cached buffer) with the cache 0x6aa95c and its limit 0x5093c4, the
 * loader thread's work, PlaySound 0x49e740, PlaySoundAtVolume 0x49e780, PlaySoundInternal 0x49e4e0, StartCachedSound
 * 0x49e620, QueueSoundLoad 0x49e000 with SoundLoaderThread 0x49e090, AcquireSoundChannel 0x49db60, EvictSoundCache
 * 0x49e930, the preload lists 0x49e3f0/0x49e350 and StopAllSounds 0x49eca0 (atlas P34, P35; research
 * native-audiovisual.md). A buffer plays from its start for its bytes at the format's rate, as a sound card (and the
 * emulator's DirectSound) does; the mixing is the caller's, which takes the plays and stops as commands. The loader
 * thread runs at once (another core): a queued sound starts in the same call. */
#include "kp_ui.h"

#include <string.h>

#include "kp_game.h"

#define SOUND_FILE 0x01         /* sounds.kpd SFX flags: the file exists */
#define SOUND_PARSED 0x02
#define SFX_PRELOAD 0x01       /* sfxdata flags 0x51d8c8 */
#define SFX_UNIT_EXCLUSIVE 0x02
#define SFX_USED 0x04          /* the eviction's second chance */
#define SFX_LOADING 0x08
#define SFX_EXCLUSIVE 0x10
#define CH_WAITING 0x08        /* a channel's flags (+6, the sound's flags when it started) */
#define CH_NO_PREEMPT 0x20
#define START_GAP_MS 80        /* StartCachedSound: a sound starts again only 80 ms after its last start */
#define LOADED_GAP 0x51        /* the loader's and the preload's "last start" (record +4 = 0x51 - now) */
#define CACHE_LIMIT 0x400000   /* 0x5093c4 without the registry's "Sound Memory Cache" */
#define NO_UNIT_TYPE 0xffff
#define PERCENT 100
#define FULL_VOLUME 99
#define CLICK_SOUND 15         /* 0x51c2ac: the UI click's sfxdata entry */

static KpUiSound *sd(KpUi *ui) { return &ui->sound; }

static const uint8_t *record(const KpUi *ui, uint32_t id)
{
	if (ui->sounds == NULL || id >= (uint32_t)ui->sound_count) return NULL;
	return ui->sounds + KP_UI_SOUND_RECORD * (size_t)id;
}

static uint8_t sfx_priority(KpUi *ui, uint32_t id)
{
	return ui->game ? (uint8_t)KP_DAT(ui->game, sfxdata, priority, id) : 0;
}

void kp_ui_sound_init(KpUi *ui, int device)
{
	KpUiSound *s = sd(ui);
	int k;
	memset(s, 0, sizeof *s);
	s->device = device != 0;
	s->voices = 1;
	s->effects = s->effects_saved = KP_UI_EFFECTS_VOLUME; /* InitDefaultVolumes 0x49dfa0 */
	s->music = s->music_saved = KP_UI_MUSIC_VOLUME;
	s->cache_limit = CACHE_LIMIT;
	s->call_type = -1;
	for (k = 0; k < KP_UI_SFX; k++) s->buffer[k] = -1;
	for (k = 0; k < KP_UI_CHANNELS; k++) s->channels[k].buffer = -1;
}

void kp_ui_sound_flags(KpUi *ui)
{
	/* the sfxdata flags the game's data loads (the runtime bits start clear) */
	KpUiSound *s = sd(ui);
	int k;
	if (ui->game == NULL) return;
	for (k = 0; k < KP_UI_SFX; k++) s->flags[k] = (uint8_t)KP_DAT(ui->game, sfxdata, flags, k);
}

/* --- the buffers --- */

/* A buffer is playing while its play cursor has not reached its end (the cursor advances by the format's average
 * bytes per second). */
static int playing(KpUi *ui, int16_t b)
{
	KpUiBuffer *x;
	const uint8_t *r;
	uint32_t avg;
	if (b < 0) return 0;
	x = &sd(ui)->buffers[b];
	if (!x->playing) return 0;
	r = record(ui, x->sound);
	avg = r ? kp_le_u32(r + 10) : 0;
	if (avg == 0 || (uint64_t)(uint32_t)(ui->now - x->start) * avg >= (uint64_t)x->size * 1000u) {
		x->playing = 0;
		return 0;
	}
	return 1;
}

static void command(KpUi *ui, uint8_t type, int channel, int16_t b)
{
	KpUiSound *s = sd(ui);
	KpUiSoundCommand *c;
	if (s->command_count >= KP_UI_SOUND_COMMANDS) return;
	c = &s->commands[s->command_count++];
	memset(c, 0, sizeof *c);
	c->type = type;
	c->channel = (int8_t)channel;
	if (b >= 0) {
		c->sound = s->buffers[b].sound;
		c->volume = s->buffers[b].volume;
		c->pan = s->buffers[b].pan;
	}
}

/* Play(0, 0, 0) of a buffer on a channel at its volume and pan (SetVolume of the attenuation less 0x6aa96c). */
static void play(KpUi *ui, int16_t b, int channel, int32_t attenuation, int32_t pan)
{
	KpUiBuffer *x = &sd(ui)->buffers[b];
	if (!x->playing) x->start = ui->now;
	x->playing = 1;
	x->volume = attenuation - sd(ui)->master;
	x->pan = pan;
	command(ui, KP_UI_SOUND_PLAY, channel, b);
}

/* Stop with SetCurrentPosition(0), or Release of a temporary buffer. */
static void stop(KpUi *ui, int channel, int release)
{
	KpUiChannel *c = &sd(ui)->channels[channel];
	KpUiBuffer *x = &sd(ui)->buffers[c->buffer];
	x->playing = 0;
	command(ui, KP_UI_SOUND_STOP, channel, -1);
	if (release) x->used = 0;
}

static void release(KpUi *ui, int16_t b) { if (b >= KP_UI_SFX) sd(ui)->buffers[b].used = 0; }

/* DuplicateSoundBuffer: a temporary copy of a cached buffer, -1 when none is free. */
static int16_t duplicate(KpUi *ui, int16_t b)
{
	KpUiSound *s = sd(ui);
	int k;
	for (k = KP_UI_SFX; k < KP_UI_BUFFERS; k++) {
		if (!s->buffers[k].used) {
			s->buffers[k] = s->buffers[b];
			s->buffers[k].used = 1;
			s->buffers[k].playing = 0;
			return (int16_t)k;
		}
	}
	return -1;
}

/* LoadSoundEffectBuffer 0x49d900: the sound's cached buffer from sounds.kpd, -1 for a missing or rejected WAV. */
static int16_t load(KpUi *ui, uint32_t id)
{
	KpUiSound *s = sd(ui);
	const uint8_t *r = record(ui, id);
	if (r == NULL || !(kp_le_u16(r) & SOUND_PARSED) || kp_le_u32(r + 18) == 0) return -1;
	memset(&s->buffers[id], 0, sizeof s->buffers[id]);
	s->buffers[id].used = 1;
	s->buffers[id].sound = (uint16_t)id;
	s->buffers[id].size = kp_le_u32(r + 18);
	s->size[id] = s->buffers[id].size;
	s->duration[id] = kp_ui_sound_duration(ui, id);
	s->buffer[id] = (int16_t)id;
	return (int16_t)id;
}

/* FreeCachedSound 0x49dd70. */
static void free_cached(KpUi *ui, uint32_t id)
{
	KpUiSound *s = sd(ui);
	int k;
	if (s->buffer[id] < 0) return;
	for (k = 0; k < KP_UI_CHANNELS; k++) {
		if (s->channels[k].buffer == s->buffer[id]) s->channels[k].buffer = -1;
	}
	s->buffers[id].used = 0;
	s->buffers[id].playing = 0;
	s->buffer[id] = -1;
	if (!(s->flags[id] & SFX_PRELOAD)) s->cache_total -= s->size[id];
}

/* EvictSoundCache 0x49e930: a second-chance clock from the cursor, then from 0. */
static int32_t evict(KpUi *ui, int32_t need)
{
	KpUiSound *s = sd(ui);
	int32_t freed = 0;
	int pass, k;
	for (pass = 0; pass < 2; pass++) {
		int first = pass == 0 ? s->evict_cursor : 0;
		int last = pass == 0 ? (s->evict_cursor == 0 ? KP_UI_SFX - 1 : s->evict_cursor - 1) : KP_UI_SFX - 1;
		for (k = first; k != last; ) {
			if (s->buffer[k] >= 0 && !(s->flags[k] & (SFX_PRELOAD | SFX_LOADING))) {
				if (s->flags[k] & SFX_USED) {
					s->flags[k] &= (uint8_t)~SFX_USED;
				} else if (!playing(ui, s->buffer[k])) {
					int32_t size = (int32_t)s->size[k];
					free_cached(ui, (uint32_t)k);
					need -= size;
					freed += size;
					if (need <= 0) return freed;
				}
			}
			s->evict_cursor = (uint16_t)(s->evict_cursor == KP_UI_SFX - 1 ? 0 : s->evict_cursor + 1);
			k = pass == 0 ? (k == KP_UI_SFX - 1 ? 0 : k + 1) : k + 1;
		}
	}
	return freed;
}

/* --- the channels --- */

/* AcquireSoundChannel 0x49db60: the last free (or finished) channel, else the first playing one of the lowest priority
 * below the new sound's that may be preempted; -1 none. */
static int acquire(KpUi *ui, uint8_t priority)
{
	KpUiSound *s = sd(ui);
	int c, found = KP_UI_CHANNELS;
	uint8_t lowest = priority;
	for (c = 0; c < KP_UI_CHANNELS; c++) {
		KpUiChannel *ch = &s->channels[c];
		if (ch->flags & CH_WAITING) continue;
		if (ch->buffer >= 0) {
			if (playing(ui, ch->buffer)) continue;
			if (ch->release) release(ui, ch->buffer);
			ch->buffer = -1;
		}
		found = c;
	}
	if (found < KP_UI_CHANNELS) return found;
	for (c = 0; c < KP_UI_CHANNELS; c++) {
		KpUiChannel *ch = &s->channels[c];
		if ((ch->flags & CH_WAITING) || ch->buffer < 0 || (ch->flags & CH_NO_PREEMPT) || ch->priority >= lowest) continue;
		lowest = ch->priority;
		found = c;
	}
	if (found == KP_UI_CHANNELS) return -1;
	stop(ui, found, s->channels[found].release);
	s->channels[found].buffer = -1;
	return found;
}

/* StartCachedSound 0x49e620. */
static int start_cached(KpUi *ui, uint32_t id, int32_t attenuation, int32_t pan, uint16_t unit_type, uint8_t flags,
	uint8_t release_after, uint8_t priority)
{
	KpUiSound *s = sd(ui);
	int16_t b = s->buffer[id];
	int c;
	if ((uint32_t)(ui->now + s->last_start[id]) < START_GAP_MS) return 0;
	if ((c = acquire(ui, priority)) < 0) return 0;
	if (playing(ui, b)) {
		if ((b = duplicate(ui, b)) < 0) return 0;
		release_after = 1;
	}
	play(ui, b, c, attenuation, pan);
	s->last_start[id] = 0u - ui->now;
	s->channels[c].buffer = b;
	s->channels[c].unit_type = unit_type;
	s->channels[c].flags = flags;
	s->channels[c].priority = priority;
	s->channels[c].release = release_after;
	s->channels[c].id = id;
	s->channels[c].volume = attenuation;
	return 1;
}

/* SoundLoaderThread 0x49e090's pass over the waiting channels. */
static void loader(KpUi *ui)
{
	KpUiSound *s = sd(ui);
	uint32_t added = 0;
	int c;
	for (c = 0; c < KP_UI_CHANNELS; c++) {
		KpUiChannel *ch = &s->channels[c];
		uint32_t id = ch->id;
		if (!(ch->flags & CH_WAITING)) continue;
		s->last_start[id] = LOADED_GAP - ui->now;
		if (load(ui, id) >= 0) {
			if (!(s->flags[id] & SFX_PRELOAD)) added += s->size[id];
			if (ch->flags & CH_WAITING) {
				play(ui, s->buffer[id], c, ch->volume, ch->pan);
				ch->buffer = s->buffer[id];
				ch->release = 0;
				ch->flags = s->flags[id];
				ch->priority = sfx_priority(ui, id);
			}
		}
		s->flags[id] &= (uint8_t)~SFX_LOADING;
		ch->flags &= (uint8_t)~CH_WAITING;
	}
	s->cache_total += added;
}

/* QueueSoundLoad 0x49e000 (the loader wakes at once: SetEvent before the cache's check). */
static int queue_load(KpUi *ui, uint32_t id, int32_t attenuation, int32_t pan, uint16_t unit_type)
{
	KpUiSound *s = sd(ui);
	int c;
	if (id >= KP_UI_SFX) return 0;
	if ((c = acquire(ui, sfx_priority(ui, id))) < 0) return 0;
	s->flags[id] |= SFX_USED | SFX_LOADING;
	s->channels[c].flags |= CH_WAITING;
	s->channels[c].pan = pan;
	s->channels[c].id = id;
	s->channels[c].volume = attenuation;
	s->channels[c].unit_type = unit_type;
	loader(ui);
	if ((int32_t)s->cache_total > (int32_t)s->cache_limit) evict(ui, (int32_t)(s->cache_total - s->cache_limit));
	return 1;
}

static void note_call(KpUi *ui, uint32_t id, int32_t attenuation, int32_t pan, int32_t unit_type, int path, int result)
{
	KpUiSound *s = sd(ui);
	KpUiSoundCall *k;
	if (s->call_count >= KP_UI_SOUND_CALLS) return;
	k = &s->calls[s->call_count++];
	k->id = id;
	k->attenuation = attenuation;
	k->pan = pan;
	k->unit_type = unit_type;
	k->path = (int8_t)path;
	k->result = (int8_t)result;
}

int kp_ui_play_sound_internal(KpUi *ui, uint32_t id, int32_t attenuation, int32_t pan, int64_t unit)
{
	KpUiSound *s = sd(ui);
	int32_t unit_type = !unit ? -1 : sd(ui)->call_type >= 0 ? sd(ui)->call_type : (int32_t)ui->game->u.type[unit];
	uint8_t f;
	int c, r;
	if (!s->device || s->effects == 0 || id >= KP_UI_SFX) {
		note_call(ui, id, attenuation, pan, unit_type, KP_UI_SOUND_REJECTED, 0);
		return 0;
	}
	f = s->flags[id];
	if ((unit && (f & SFX_UNIT_EXCLUSIVE)) || (f & SFX_EXCLUSIVE)) {
		for (c = 0; c < KP_UI_CHANNELS; c++) {
			KpUiChannel *ch = &s->channels[c];
			if (ch->buffer < 0) continue;
			if ((f & SFX_EXCLUSIVE) ? ch->id != id : (ch->unit_type != (uint16_t)unit_type || !(ch->flags & SFX_UNIT_EXCLUSIVE))) {
				continue;
			}
			if (playing(ui, ch->buffer)) { /* the earlier one plays: this one is dropped */
				note_call(ui, id, attenuation, pan, unit_type, KP_UI_SOUND_REJECTED, 0);
				return 0;
			}
			if (ch->release) release(ui, ch->buffer);
			ch->buffer = -1;
		}
	}
	if (s->flags[id] & SFX_LOADING) {
		note_call(ui, id, attenuation, pan, unit_type, KP_UI_SOUND_REJECTED, 0);
		return 0;
	}
	if (s->buffer[id] < 0) {
		(void)queue_load(ui, id, attenuation, pan, unit ? (uint16_t)unit_type : NO_UNIT_TYPE);
		note_call(ui, id, attenuation, pan, unit_type, KP_UI_SOUND_QUEUED, 1);
		return 1;
	}
	s->flags[id] |= SFX_USED;
	r = start_cached(ui, id, attenuation, pan, unit ? (uint16_t)unit_type : NO_UNIT_TYPE, s->flags[id], 0,
		sfx_priority(ui, id));
	note_call(ui, id, attenuation, pan, unit_type, KP_UI_SOUND_STARTED, r);
	return r;
}

int32_t kp_ui_attenuation_at(const KpUi *ui, int32_t k)
{
	/* 0x4efc00[k] (signed, unchecked in the original: clamped here) */
	if (ui->attenuation == NULL || ui->attenuation_count == 0) return 0;
	if (k < 0) k = 0;
	if (k >= ui->attenuation_count) k = ui->attenuation_count - 1;
	return (int32_t)kp_le_u32(ui->attenuation + 4 * (size_t)k);
}

int32_t kp_ui_attenuation(const KpUi *ui, int32_t percent)
{
	/* 0x4efc00[(effects x percent) / 100] */
	return kp_ui_attenuation_at(ui, (int32_t)ui->sound.effects * percent / PERCENT);
}

int kp_ui_play_sound(KpUi *ui, uint32_t id)
{
	/* PlaySound 0x49e740 */
	if (!sd(ui)->device || sd(ui)->effects == 0) return 0;
	return kp_ui_play_sound_internal(ui, id, kp_ui_attenuation(ui, FULL_VOLUME), 0, 0);
}

void kp_ui_play_click(KpUi *ui)
{
	KpUiSound *s = sd(ui);
	KpUiSoundCommand *c;
	if (!s->device || s->effects == 0 || s->command_count >= KP_UI_SOUND_COMMANDS) return;
	c = &s->commands[s->command_count++];
	memset(c, 0, sizeof *c);
	c->type = KP_UI_SOUND_CLICK;
	c->channel = -1;
	c->sound = CLICK_SOUND;
	c->volume = kp_ui_attenuation(ui, FULL_VOLUME) - s->master; /* 0x4efc00[(effects x 99) / 100] less 0x6aa96c */
}

int kp_ui_play_sound_at_volume(KpUi *ui, uint32_t id, int32_t percent, int32_t pan, int64_t unit)
{
	/* PlaySoundAtVolume 0x49e780 */
	if (!sd(ui)->device || sd(ui)->effects == 0) return 0;
	return kp_ui_play_sound_internal(ui, id, kp_ui_attenuation(ui, percent), pan, unit);
}

void kp_ui_preload(KpUi *ui, const uint16_t *list, int count)
{
	/* SetSoundPreloadList 0x49e3f0 with PreloadSound 0x49e350 */
	KpUiSound *s = sd(ui);
	int k;
	if (!s->device) return;
	if (list == NULL) {
		for (k = 0; k < s->preload_count; k++) {
			if (s->preload[k] < KP_UI_SFX) free_cached(ui, s->preload[k]);
		}
		s->preload_count = 0;
		return;
	}
	s->preload_count = count < KP_UI_PRELOAD ? count : KP_UI_PRELOAD;
	memcpy(s->preload, list, sizeof s->preload[0] * (size_t)s->preload_count);
	for (k = 0; k < s->preload_count; k++) {
		uint16_t id = s->preload[k];
		if (id >= KP_UI_SFX || !(s->flags[id] & SFX_PRELOAD) || s->effects == 0) continue;
		s->last_start[id] = LOADED_GAP - ui->now;
		if (s->buffer[id] >= 0) {
			s->flags[id] |= SFX_USED;
		} else if (load(ui, id) >= 0) {
			if (!(s->flags[id] & SFX_PRELOAD)) s->cache_total += s->size[id];
			s->flags[id] |= SFX_USED;
			if ((int32_t)s->cache_total > (int32_t)s->cache_limit) evict(ui, (int32_t)(s->cache_total - s->cache_limit));
		}
	}
}

void kp_ui_stop_sounds(KpUi *ui)
{
	/* StopAllSounds 0x49eca0 */
	KpUiSound *s = sd(ui);
	int c;
	kp_ui_stop_streams(ui);
	for (c = 0; c < KP_UI_CHANNELS; c++) {
		KpUiChannel *ch = &s->channels[c];
		if (ch->flags & CH_WAITING) {
			ch->flags &= (uint8_t)~CH_WAITING;
		} else if (ch->buffer >= 0 && playing(ui, ch->buffer)) {
			stop(ui, c, ch->release);
			ch->buffer = -1;
		}
	}
}

size_t kp_ui_take_sound_commands(KpUi *ui, KpUiSoundCommand *out, size_t capacity)
{
	KpUiSound *s = sd(ui);
	size_t n = (size_t)s->command_count < capacity ? (size_t)s->command_count : capacity;
	memcpy(out, s->commands, sizeof *out * n);
	s->command_count = 0;
	return n;
}

size_t kp_ui_take_sound_calls(KpUi *ui, KpUiSoundCall *out, size_t capacity)
{
	KpUiSound *s = sd(ui);
	size_t n = (size_t)s->call_count < capacity ? (size_t)s->call_count : capacity;
	memcpy(out, s->calls, sizeof *out * n);
	s->call_count = 0;
	return n;
}

void kp_ui_stream(KpUi *ui, uint16_t string)
{
	/* PlayStreamedSoundFile 0x49e810: with a device and the effects on, at 99 percent */
	KpUiSound *s = sd(ui);
	KpUiSoundCommand *c;
	if (!s->device || s->effects == 0 || s->command_count >= KP_UI_SOUND_COMMANDS) return;
	c = &s->commands[s->command_count++];
	memset(c, 0, sizeof *c);
	c->type = KP_UI_SOUND_STREAM;
	c->channel = -1;
	c->sound = string;
	c->volume = kp_ui_attenuation(ui, FULL_VOLUME) - s->master;
}

void kp_ui_stream_sfx(KpUi *ui, uint16_t id)
{
	/* PlayStreamedSfx 0x49e7c0: a file of sfxdata, PlayStreamedSoundFile 0x49e810's stream */
	KpUiSound *s = sd(ui);
	KpUiSoundCommand *c;
	const uint8_t *r = record(ui, id);
	if (r == NULL || !(kp_le_u16(r) & SOUND_FILE) || !s->device || s->effects == 0
		|| s->command_count >= KP_UI_SOUND_COMMANDS) return;
	c = &s->commands[s->command_count++];
	memset(c, 0, sizeof *c);
	c->type = KP_UI_SOUND_STREAM_SFX;
	c->channel = -1;
	c->sound = id;
	c->volume = kp_ui_attenuation(ui, FULL_VOLUME) - s->master;
}

void kp_ui_stop_streams(KpUi *ui)
{
	KpUiSound *s = sd(ui);
	if (s->command_count >= KP_UI_SOUND_COMMANDS) return;
	memset(&s->commands[s->command_count], 0, sizeof s->commands[0]);
	s->commands[s->command_count].type = KP_UI_SOUND_STREAMS_STOP;
	s->commands[s->command_count].channel = -1;
	s->command_count++;
}
