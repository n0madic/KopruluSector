/* The music (plan phase 9D; modeless.cpp's CD tracks, atlas P24, P14): the track table 0x508c58 (ui.kpd MUSI) with
 * the tracks' lengths (sounds.kpd MUSL), PlayMusicTrack 0x46ef10 with its fade-in timer 0x46efb0, StartMusicTrack
 * 0x46f1b0, PollMusicEnd 0x46ee70 (a track of kind 2 continues with its next one), StopMusicFadeOut 0x46f040, the quiet
 * mode (0x46f120, 0x46f2c0), SetMusicVolume 0x46f170, CloseMusic 0x46f290 and the first track of a game (LoadConsoleSkinAndPickMusic
 * 0x443680). The stream's playback is the caller's: kp_ui gives it the plays, volumes and stops as sound commands
 * (channel -1). */
#include "kp_ui.h"

#include <string.h>

#include "kp_game.h"

#define SILENT (-10000)
#define FADE_START (-3396)     /* 0x4efc04 */
#define FADE_STEP 400
#define FADE_MS 20             /* Win32 timer 3 */
#define QUIET_DROP 750
#define POLL_MS 1000           /* the pump's idle handler polls the end at most once per second */
#define KIND_LOOP 1
#define KIND_ONCE 2
#define RACES 3
#define TRACKS_PER_RACE 3
#define FIRST_EXPANSION_KEY 32
#define MUSI_RECORD_TAIL 3     /* after the label: kind, fade-in, next */

static KpUiMusic *mu(KpUi *ui) { return &ui->music; }

void kp_ui_music_tables(KpUi *ui, const uint8_t *musi, size_t size)
{
	/* ui.kpd MUSI: u8 count, per track a label (u8 length, the path), kind, fade-in, next */
	KpUiMusic *m = mu(ui);
	size_t at = 1;
	int n = size ? musi[0] : 0, k;
	m->track_count = 0;
	for (k = 0; k < n && k < KP_UI_MUSIC_TRACKS && at < size; k++) {
		size_t len = musi[at];
		if (at + 1 + len + MUSI_RECORD_TAIL > size) break;
		at += 1 + len;
		m->kind[k] = musi[at];
		m->fade[k] = musi[at + 1];
		m->next[k] = musi[at + 2];
		at += MUSI_RECORD_TAIL;
		m->track_count = k + 1;
	}
}

void kp_ui_music_lengths(KpUi *ui, const uint8_t *musl, size_t size)
{
	/* sounds.kpd MUSL: u8 count, per track the data length and nAvgBytesPerSec */
	KpUiMusic *m = mu(ui);
	int n = size ? musl[0] : 0, k;
	for (k = 0; k < n && k < KP_UI_MUSIC_TRACKS && 1 + 8 * (size_t)(k + 1) <= size; k++) {
		uint32_t length = kp_le_u32(musl + 1 + 8 * k), avg = kp_le_u32(musl + 5 + 8 * k);
		m->length_ms[k] = avg ? (uint32_t)((uint64_t)length * 1000u / avg) : 0;
	}
}

static void command(KpUi *ui, uint8_t type, uint16_t track, int32_t volume)
{
	KpUiSound *s = &ui->sound;
	KpUiSoundCommand *c;
	if (s->command_count >= KP_UI_SOUND_COMMANDS) return;
	c = &s->commands[s->command_count++];
	memset(c, 0, sizeof *c);
	c->type = type;
	c->channel = -1;
	c->sound = track;
	c->volume = volume;
}

/* The stream's volume: the target, 750 lower when quiet (floor -10000). */
static int32_t heard(const KpUiMusic *m, int32_t v)
{
	if (m->quiet) v = v - QUIET_DROP < SILENT ? SILENT : v - QUIET_DROP;
	return v;
}

/* The music option's attenuation (0x4efbfc[percent], the effects' table one entry earlier), or silence for 0. */
static int32_t option_volume(const KpUi *ui)
{
	uint32_t v = ui->sound.music;
	if (v == 0) return SILENT;
	return kp_ui_attenuation_at(ui, (int32_t)v - 1) - ui->sound.master;
}

static void close_stream(KpUi *ui)
{
	KpUiMusic *m = mu(ui);
	if (m->open) command(ui, KP_UI_SOUND_MUSIC_STOP, m->track, SILENT);
	m->open = 0;
	m->playing = 0;
	m->absent = 0;
}

/* StartMusicTrack 0x46f1b0. */
static void start_track(KpUi *ui, uint16_t track)
{
	KpUiMusic *m = mu(ui);
	if (!m->open || track != m->track) {
		m->track = track;
		close_stream(ui);
		if (track == 0 || track >= m->track_count) return;
		if (m->length_ms[track] == 0) { /* its file is not there */
			m->absent = 1;
			return;
		}
		m->open = 1;
	}
	m->quiet = 0;
	if (ui->sound.music != 0) {
		m->playing = 1;
		m->start = ui->now;
		command(ui, KP_UI_SOUND_MUSIC, track, heard(m, m->volume));
	}
}

void kp_ui_music_play(KpUi *ui, uint16_t track)
{
	/* PlayMusicTrack 0x46ef10 */
	KpUiMusic *m = mu(ui);
	if (m->open && m->playing) return;
	if (track >= m->track_count || !m->fade[track]) {
		start_track(ui, track);
		return;
	}
	{
		int32_t target = m->volume;
		m->volume = SILENT;
		start_track(ui, track);
		m->volume = target;
	}
	if (ui->sound.music != 0) {
		m->fading = FADE_START;
		m->fade_due = ui->now + FADE_MS;
		m->fade_active = 1;
	}
}

int32_t kp_ui_music_level(const KpUi *ui) { return option_volume(ui); }

void kp_ui_music_stop(KpUi *ui)
{
	/* StopMusicFadeOut 0x46f040 (its blocking fade is the mixer's); a track asked for without its file is forgotten */
	KpUiMusic *m = mu(ui);
	if (m->open) close_stream(ui);
	m->absent = 0;
}

void kp_ui_music_again(KpUi *ui)
{
	KpUiMusic *m = mu(ui);
	if (m->absent) kp_ui_music_play(ui, m->track);
}

void kp_ui_music_close(KpUi *ui)
{
	/* CloseMusic 0x46f290 */
	close_stream(ui);
}

void kp_ui_music_quiet(KpUi *ui, int quiet)
{
	/* EnterMusicQuietMode 0x46f120, LeaveMusicQuietMode 0x46f2c0 */
	KpUiMusic *m = mu(ui);
	if ((m->quiet != 0) == (quiet != 0)) return;
	m->quiet = (uint8_t)(quiet != 0);
	if (m->open) command(ui, KP_UI_SOUND_MUSIC_VOLUME, m->track, heard(m, m->volume));
}

void kp_ui_music_volume(KpUi *ui, int32_t volume)
{
	/* SetMusicVolume 0x46f170 */
	KpUiMusic *m = mu(ui);
	m->volume = volume;
	if (m->open) command(ui, KP_UI_SOUND_MUSIC_VOLUME, m->track, heard(m, volume));
}

void kp_ui_music_tick(KpUi *ui)
{
	KpUiMusic *m = mu(ui);
	/* MusicFadeInTimer 0x46efb0 every 20 ms */
	while (m->fade_active && (int32_t)(ui->now - m->fade_due) >= 0) {
		int32_t target = heard(m, m->volume);
		m->fade_due += FADE_MS;
		if (!m->playing) {
			m->fade_active = 0;
			break;
		}
		m->fading += FADE_STEP;
		if (m->fading >= target) {
			m->fading = target;
			m->fade_active = 0;
		}
		command(ui, KP_UI_SOUND_MUSIC_VOLUME, m->track, m->fading);
	}
	/* PollMusicEnd 0x46ee70, at most once per second */
	if ((int32_t)(ui->now - m->poll_due) >= 0) {
		m->poll_due = ui->now + POLL_MS;
		if (m->playing && m->track < m->track_count && m->kind[m->track] == KIND_ONCE
			&& ui->now - m->start >= m->length_ms[m->track]) {
			start_track(ui, m->next[m->track]);
		}
	}
}

void kp_ui_music_start_game(KpUi *ui)
{
	/* RunGame 0x443f40's StopMusicFadeOut first (the ready room's or the menus' track ends there), then
	 * LoadConsoleSkinAndPickMusic 0x443680's track and RunGame 0x445310's PlayMusicTrack */
	static const uint8_t KEYS[2][RACES] = { { 12, 1, 22 }, { 49, 40, 32 } }; /* 0x50334c, 0x503350 */
	KpUiMusic *m = mu(ui);
	KpGame *g = ui->game;
	int race = kp_ui_local_race(ui);
	int64_t key = kp_scalar(g, KP_MISSION_INDEX);
	uint16_t track;
	kp_ui_music_stop(ui);
	m->volume = option_volume(ui);
	if (race < 0 || race >= RACES) return;
	track = (uint16_t)(1 + TRACKS_PER_RACE * race);
	if (key != KP_NIL && key != 0) {
		int64_t base = KEYS[key >= FIRST_EXPANSION_KEY][race];
		track = (uint16_t)(track + (int)((key - base) % TRACKS_PER_RACE));
	} else {
		if (m->rotation < 0) {
			/* the first rotation of the session: rand() % 3 of the CRT generator, which RunGame 0x443f40 has just
			 * seeded with the game's seed (srand 0x4d14f5 before 0x443680), so the draw is the game's own */
			if (g->crt == NULL) {
				kp_game_fail(g, "ui: the music's first rotation without the crt generator");
				return;
			}
			m->rotation = (int)(kp_rng_crt_rand(g->crt) % TRACKS_PER_RACE);
		}
		m->rotation = (m->rotation + 1) % TRACKS_PER_RACE;
		track = (uint16_t)(track + m->rotation);
	}
	kp_ui_music_play(ui, track);
}
