/* The game's sounds as the interface plays them (plan phase 9D; gamemap.cpp, atlas P15): the visibility-gated unit
 * sound PlaySoundAtUnit 0x446940 and the map point's PlaySoundAtPosition 0x446a20 with their volume from the distance
 * to the view (0x4463e0) and pan (0x446360, 0x422b00), the units' answers (PlayUnitYesSound 0x446120,
 * PlayUnitSelectSound 0x446c30 with ChooseYesSound 0x447130, ChooseWhatSound 0x447190 and SelectionSoundPrefers
 * 0x49c140, drawing from the CRT generator the creep art shares), the cooldowns of the idle event (0x446ff0), the
 * quiet mode and the talking requests that follow a voice (RequestUnitTalking 0x4a7200). */
#include "kp_ui.h"

#include <string.h>

#include "kp_game.h"
#include "kp_ids.h"
#include "kp_rng.h"

#define VOLUME_MAX 99
#define VOLUME_SHIFT 9
#define VOLUME_AUDIBLE 10      /* PlaySoundAtUnit: a volume up to 10 plays nothing */
#define QUIET_DIVISOR 5
#define TILE 32
#define SELECT_COOLDOWN 10
#define ANNOYED_LEAD 3
#define CRITTER_PATIENCE 10 /* answers past the lead */
#define TALK_COLOUR 0x11       /* the talking requests' colour after an answer */
#define SECOND_MS 1000
#define TYPES_WITH_SOUNDS 105  /* units.dat's yes and annoyed ranges cover types 0..105 */
#define OWNER_NEUTRAL 11
#define DAT_BUILDING 0x01
#define STATUS_COMPLETED 0x01
#define STATUS_DISABLED 0x400
#define GROUP_TERRAN 2
#define GROUP_PROTOSS 4
#define SOUND_TERRAN_BURNING 403  /* 0x446c30: a red completed building's answer */
#define SOUND_DAMAGED 18
#define SOUND_DISABLED_BUILDING 0x72 /* + race */
#define SOUND_DISABLED 15
#define OPTION_ADVISOR 0x01    /* 0x6d3148 bits: the enable argument of PlaySoundAtUnit */
#define OPTION_ANSWERS 0x02
#define OPTION_BUILDINGS 0x04
#define RAND_SHIFT 8
#define ATTACK_NOTICE_REACH 352   /* SuppressNearbyAttackNotices 0x4460d0: +0x87 = 100 around the unit */
#define ATTACK_NOTICE_UNITS 1700
#define ATTACK_NOTICE_SILENCE 100
#define ATTACK_COOLDOWN 100
#define ATTACK_COLOUR 0x10
#define SOUND_ATTACKED 0x78       /* + race; 0x75 for a unit with status 2 */
#define SOUND_ATTACKED_BUILDING 0x75
#define SOUND_SYSTEM_MESSAGE 2
#define FIRST_COMMON_SOUND 1   /* InitSoundState 0x446800: the lists begin with the sounds 1..113 */
#define LAST_COMMON_SOUND 113

static KpUiSound *sd(KpUi *ui) { return &ui->sound; }
static KpGame *game(KpUi *ui) { return ui->game; }

/* ComputeDistanceVolume 0x4463e0: 99 less 99/512 per pixel outside the view. */
static int32_t distance_volume(KpUi *ui, int32_t x, int32_t y)
{
	int32_t sx = ui->input.screen_x, sy = ui->input.screen_y, off = 0, v;
	if (x < sx) off += sx - x;
	else if (x > sx + ui->view_w) off += x - (sx + ui->view_w);
	if (y < sy) off += sy - y;
	else if (y > sy + ui->view_h) off += y - (sy + ui->view_h);
	v = VOLUME_MAX - ((off * VOLUME_MAX) >> VOLUME_SHIFT);
	return v < 0 ? 0 : v;
}

/* ComputeSoundPan 0x446360. */
static int32_t sound_pan(KpUi *ui, int32_t x)
{
	/* the view's centre tile: 10 of the original's 640 pixels */
	int32_t d = x / TILE - (ui->input.screen_x >> 5) - ui->view_w / TILE / 2, a = d < 0 ? -d : d, p;
	if (a > 80) p = 10000;
	else if (a > 40) p = 5000;
	else if (a > 20) p = 1000;
	else if (a > 10) p = 600;
	else if (a > 5) p = 300;
	else if (a > 2) p = 100;
	else p = 0;
	return d < 0 ? -p : p;
}

/* The volume of PlaySoundAtUnit and PlaySoundAtPosition: the largest of the caller's minimum, the sound's and the
 * distance's, a fifth when quiet; 0 when inaudible. */
static int32_t volume(KpUi *ui, uint32_t sfx, int32_t min_volume, int32_t distance)
{
	int32_t v = min_volume, m = (int32_t)(int8_t)KP_DAT(game(ui), sfxdata, minimum_volume, sfx);
	if (m > v) v = m;
	if (distance > v) v = distance;
	if (sd(ui)->quiet) v /= QUIET_DIVISOR;
	return v <= VOLUME_AUDIBLE ? 0 : v;
}

static int enabled(KpUi *ui, uint32_t enable) { return enable != 0 && sd(ui)->effects != 0; }

/* PlaySoundAtUnit 0x446940 with the unit's sprite at (x, y) (the visibility of a game unit's sound is the game's:
 * kp_core tested it). */
static int sound_at_unit(KpUi *ui, uint32_t sfx, int64_t unit, int32_t x, int32_t y, uint32_t enable, int32_t min_volume)
{
	int32_t pan = 0, dist = VOLUME_MAX, v;
	if (!sd(ui)->voices || sfx == 0 || !enabled(ui, enable)) return 0;
	if (unit != 0) {
		pan = sound_pan(ui, x);
		dist = distance_volume(ui, x, y);
	}
	v = volume(ui, sfx, min_volume, dist);
	return v ? kp_ui_play_sound_at_volume(ui, sfx, v, pan, unit) : 0;
}

int kp_ui_sound_at_unit(KpUi *ui, uint32_t sfx, int64_t unit, uint32_t enable, int32_t min_volume)
{
	KpGame *g = game(ui);
	int32_t s = unit ? kp_unit_sprite(g, unit) : 0;
	return sound_at_unit(ui, sfx, unit, unit ? (int16_t)g->w->spr.x[s] : 0, unit ? (int16_t)g->w->spr.y[s] : 0, enable,
		min_volume);
}

int kp_ui_sound_at_position(KpUi *ui, uint32_t sfx, int32_t x, int32_t y, uint32_t enable, int32_t min_volume)
{
	/* PlaySoundAtPosition 0x446a20 (the tile's visibility is the game's) */
	int32_t v;
	if (!sd(ui)->voices || sfx == 0 || !enabled(ui, enable)) return 0;
	v = volume(ui, sfx, min_volume, distance_volume(ui, x, y));
	return v ? kp_ui_play_sound_at_volume(ui, sfx, v, sound_pan(ui, x), 0) : 0;
}

/* GetSoundDurationWholeSeconds 0x446740 of the cached duration (0 for a sound never loaded) and sfxdata's adjustment:
 * a talking request's length. */
static uint32_t talk_duration(KpUi *ui, uint32_t sfx)
{
	uint32_t d = sfx < KP_UI_SFX ? sd(ui)->duration[sfx] / SECOND_MS * SECOND_MS : 0;
	return d + (uint32_t)(int32_t)(int16_t)KP_DAT(game(ui), sfxdata, portrait_length_adjust, sfx);
}

void kp_ui_voice_talk(KpUi *ui, int64_t unit, uint32_t sfx, int ping, uint8_t colour)
{
	kp_ui_portrait_talk(ui, unit, talk_duration(ui, sfx), ping, colour, sfx);
}

/* A random sound of [first, last] (CRT rand >> 8, signed remainder), the next one instead of the previous one. */
static uint32_t random_sound(KpUi *ui, uint32_t first, uint32_t last)
{
	KpGame *g = game(ui);
	int32_t r;
	uint32_t id;
	if (g->crt == NULL) {
		kp_game_fail(g, "ui: a unit voice's choice without the crt generator");
		return first;
	}
	r = (int32_t)kp_rng_crt_rand(g->crt) >> RAND_SHIFT;
	id = first + (uint32_t)(r % (int32_t)(last - first + 1));
	if (id == sd(ui)->previous) id = id + 1 > last ? first : id + 1;
	sd(ui)->previous = id;
	return id;
}

/* ChooseYesSound 0x447130. */
static uint32_t choose_yes(KpUi *ui, int64_t t)
{
	uint32_t first, last;
	if (t > TYPES_WITH_SOUNDS) return 0;
	first = (uint32_t)KP_DAT(game(ui), units, yes_sound_start, t);
	last = (uint32_t)KP_DAT(game(ui), units, yes_sound_end, t);
	if (first == 0) return 0;
	return first == last ? first : random_sound(ui, first, last);
}

/* ChooseWhatSound 0x447190 with the annoyed index (1-based). */
static uint32_t choose_what(KpUi *ui, int64_t t, int32_t index)
{
	KpGame *g = game(ui);
	uint32_t first, last;
	if (t <= TYPES_WITH_SOUNDS && (first = (uint32_t)KP_DAT(g, units, pissed_sound_start, t)) != 0) {
		int32_t count = (int32_t)((uint32_t)KP_DAT(g, units, pissed_sound_end, t) - first + 1);
		if (index > count) sd(ui)->annoyed = 0;
		else if (index >= 1) return first + (uint32_t)index - 1;
	}
	first = (uint32_t)KP_DAT(g, units, what_sound_start, t);
	last = (uint32_t)KP_DAT(g, units, what_sound_end, t);
	if (first == 0) return 0;
	return first == last ? first : random_sound(ui, first, last);
}

void kp_ui_yes_sound(KpUi *ui, int64_t unit)
{
	/* PlayUnitYesSound 0x446120 */
	KpGame *g = game(ui);
	KpUiSound *s = sd(ui);
	uint32_t saved, id;
	if (!s->voices || s->quiet) return;
	s->select_unit = 0;
	if (g->w->replay || g->u.owner[unit] != g->local_player || kp_unit_disabled(g, unit)) return;
	saved = s->previous;
	id = choose_yes(ui, g->u.type[unit]);
	if (kp_ui_sound_at_unit(ui, id, unit, ui->options & OPTION_ANSWERS, VOLUME_MAX)) {
		kp_ui_voice_talk(ui, unit, id, 0, TALK_COLOUR);
	} else {
		s->previous = saved;
	}
}

/* IsHpAtMostOneThird 0x438dd0 and IsHpInYellowRange 0x446080: the unit's hit points in percent of its type's. */
static int32_t hp_percent(KpGame *g, int64_t n)
{
	int32_t hp = (int32_t)((g->u.hp[n] + 0xff) >> 8), max = (int32_t)(KP_DAT(g, units, hit_points, g->u.type[n]) >> 8);
	if (max == 0) max = hp ? hp : 1;
	return hp * 100 / max;
}

/* The sounds of the interface's own call to the game play inside the call, as the original's do: the events since
 * sequence `mark`, which the host passes on later (kp_ui_sound_played). */
static void play_call_sounds(KpUi *ui, int64_t mark)
{
	KpUiSound *s = sd(ui);
	KpEvent ev;
	int64_t i;
	for (i = 0; kp_event_peek(game(ui), i, &ev); i++) {
		if (ev.seq <= mark || s->played_count >= KP_UI_PLAYED_EVENTS) continue;
		s->played[s->played_count].kind = ev.kind;
		s->played[s->played_count].a = ev.a;
		s->played[s->played_count].b = ev.b;
		s->played[s->played_count].c = ev.c;
		s->played_count++;
		kp_ui_sound_event(ui, ev.kind, ev.a, ev.b, ev.c);
	}
}

int kp_ui_sound_played(KpUi *ui, int64_t kind, int64_t a, int64_t b, int64_t c)
{
	KpUiSound *s = sd(ui);
	int k;
	for (k = 0; k < s->played_count; k++) {
		if (s->played[k].kind == kind && s->played[k].a == a && s->played[k].b == b && s->played[k].c == c) {
			memmove(&s->played[k], &s->played[k + 1], sizeof s->played[0] * (size_t)(s->played_count - k - 1));
			s->played_count--;
			return 1;
		}
	}
	return 0;
}

/* PlayUnitSelectSound 0x446c30; a critter selected 13 times in a row dies (offline: the multiplayer byte 0x672664 is
 * 0) before its answer. */
static void select_sound(KpUi *ui, int64_t unit)
{
	KpGame *g = game(ui);
	KpUiSound *s = sd(ui);
	int64_t t;
	uint32_t id, saved = s->previous, group;
	int building, completed; /* units.dat's building flag; the answer's branch also wants it completed */
	if (!s->voices || s->quiet) return;
	if (unit == 0 || g->w->replay
		|| (g->u.owner[unit] != g->local_player
			&& !(g->u.owner[unit] == OWNER_NEUTRAL && (kp_resource_type(g->u.type[unit]) || kp_is_critter(g, unit))))) {
		s->select_unit = 0;
		s->annoyed = 0;
		return;
	}
	if (unit == s->select_unit && s->cooldown[0] != 0) return;
	s->cooldown[0] = SELECT_COOLDOWN;
	if (unit != s->select_unit) {
		s->annoyed = 0;
		s->select_unit = unit;
	}
	t = g->u.type[unit];
	group = (uint32_t)KP_DAT(g, units, staredit_group_flags, t);
	building = (KP_DAT(g, units, special_ability_flags, t) & DAT_BUILDING) != 0;
	completed = building && (g->u.status[unit] & STATUS_COMPLETED);
	if (completed) {
		int32_t pct = hp_percent(g, unit);
		if (pct <= 33 && (group & GROUP_TERRAN)) id = SOUND_TERRAN_BURNING;
		else if (pct <= 33 && (group & GROUP_PROTOSS)) id = SOUND_DAMAGED;
		else if (pct >= 34 && pct <= 66 && (group & (GROUP_TERRAN | GROUP_PROTOSS))) id = SOUND_DAMAGED;
		else if (kp_unit_disabled(g, unit)) id = SOUND_DISABLED_BUILDING + (uint32_t)kp_unit_race(g, unit);
		else id = choose_what(ui, t, 0);
	} else {
		int32_t a = (int32_t)s->annoyed - ANNOYED_LEAD;
		if (a < 0) a = 0;
		id = kp_unit_disabled(g, unit) ? SOUND_DISABLED : choose_what(ui, t, a);
		if (kp_is_critter(g, unit) && a >= CRITTER_PATIENCE) {
			int64_t mark = kp_event_sequence(g);
			kp_critter_annoyed(g, unit);
			play_call_sounds(ui, mark);
		}
	}
	if (id == 0) return;
	if (kp_ui_sound_at_unit(ui, id, unit, ui->options & (building ? OPTION_BUILDINGS : OPTION_ANSWERS), VOLUME_MAX)) {
		if (!building) {
			kp_ui_voice_talk(ui, unit, id, 0, TALK_COLOUR);
			s->annoyed++;
		}
	} else {
		s->previous = saved;
	}
}

void kp_ui_selection_sound(KpUi *ui, const int64_t *units, int n)
{
	/* SelectUnitsLocally 0x49cf80's voice */
	int64_t best = 0;
	int k;
	for (k = 0; k < n; k++) {
		if (kp_selection_prefers(game(ui), best, units[k])) best = units[k];
	}
	select_sound(ui, best);
}

void kp_ui_tick_sounds(KpUi *ui)
{
	/* TickSoundCooldowns 0x446ff0 (the idle event's) */
	KpUiSound *s = sd(ui);
	KpGame *g = game(ui);
	int k;
	for (k = 0; k < 3; k++) {
		if (s->cooldown[k] != 0) s->cooldown[k]--;
	}
	if (s->tick_counter-- == 0) {
		int64_t n;
		s->tick_counter = 1;
		for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = g->u.next[n]) {
			if (g->u.attack_notify_timer[n] != 0) g->u.attack_notify_timer[n]--;
		}
	}
}

int kp_ui_advisor_sound(KpUi *ui, uint32_t id)
{
	/* the advisor's playback (0x4461f0, 0x446760, 0x446b70, 0x446e90, 0x447060): no option, 99 or the sound's minimum,
	 * without a position */
	int32_t v, m;
	if (!sd(ui)->voices || id == 0 || sd(ui)->effects == 0) return 0;
	m = (int32_t)(int8_t)KP_DAT(game(ui), sfxdata, minimum_volume, id);
	v = m > VOLUME_MAX ? m : VOLUME_MAX;
	if (sd(ui)->quiet) v /= QUIET_DIVISOR;
	return v > VOLUME_AUDIBLE ? kp_ui_play_sound_at_volume(ui, id, v, 0, 0) : 0;
}

/* ShowLocalErrorMessage 0x46ae50 of a stat_txt string (the interface's commands are the local player's). */
static void error_line(KpUi *ui, int64_t player, uint32_t string)
{
	if (player == game(ui)->local_player) kp_ui_error_line(ui, kp_ui_tbl(ui, KP_UI_STAT_TXT, string));
}

void kp_ui_error_sound(KpUi *ui, int64_t player, uint32_t sound, uint32_t string)
{
	/* ShowErrorWithSound 0x4461f0 */
	KpGame *g = game(ui);
	if (player != g->local_player || g->w->replay) return;
	if (sound != 0) (void)kp_ui_advisor_sound(ui, sound);
	error_line(ui, player, string);
}

void kp_ui_error_buzz(KpUi *ui, int64_t unit, uint32_t string, int64_t player)
{
	/* ShowErrorWithRaceBuzz 0x446760: the race's buzz (race + 1) */
	KpGame *g = game(ui);
	int64_t race;
	if (player != g->local_player || g->w->replay) return;
	error_line(ui, player, string);
	race = unit ? kp_unit_race(g, unit) : kp_ui_local_race(ui);
	(void)kp_ui_advisor_sound(ui, (uint32_t)(race + 1));
}

void kp_ui_worker_voice(KpUi *ui, int64_t unit, uint32_t base, int32_t string)
{
	/* ShowWorkerPlacementError 0x446450 (string >= 0) and PlayPlacementErrorVoice 0x446570 (-1): the race's voice at
	 * the unit, its talking request when it played */
	KpGame *g = game(ui);
	uint32_t id;
	if (unit == 0 || g->u.owner[unit] != g->local_player || g->w->replay) return;
	id = base + (uint32_t)kp_unit_race(g, unit);
	if (kp_ui_sound_at_unit(ui, id, unit, 1, VOLUME_MAX)) kp_ui_voice_talk(ui, unit, id, 1, TALK_COLOUR);
	if (string >= 0) error_line(ui, g->u.owner[unit], (uint32_t)string);
}

void kp_ui_placed_sound(KpUi *ui, int64_t type)
{
	/* PlayBuildingPlacedSound 0x446e90: 4 + the type's group race (0x414550: 4 for none) */
	uint32_t group = (uint32_t)KP_DAT(game(ui), units, staredit_group_flags, type);
	uint32_t race = (group & 1) ? 0 : (group & GROUP_TERRAN) ? 1 : (group & GROUP_PROTOSS) ? 2 : 4;
	(void)kp_ui_advisor_sound(ui, 4 + race);
}

void kp_ui_sound_start_game(KpUi *ui)
{
	/* InitSoundState 0x446800 (CL = 1): the answers' state reset and the local race's preload list: the sounds 1..113
	 * and the race's block (Zerg 755..967, Terran 174..472, Protoss 473..754) */
	static const uint16_t RACE_FIRST[3] = { 755, 174, 473 }, RACE_LAST[3] = { 967, 472, 754 };
	KpUiSound *s = sd(ui);
	uint16_t list[KP_UI_PRELOAD];
	int n = 0, race = kp_ui_local_race(ui);
	uint32_t k;
	s->previous = 0;
	s->select_unit = 0;
	s->annoyed = 0;
	s->cooldown[0] = s->cooldown[1] = s->cooldown[2] = 0;
	s->quiet = 0;
	kp_ui_sound_flags(ui);
	for (k = FIRST_COMMON_SOUND; k <= LAST_COMMON_SOUND; k++) list[n++] = (uint16_t)k;
	if (race >= 0 && race < 3) {
		for (k = RACE_FIRST[race]; k <= RACE_LAST[race]; k++) list[n++] = (uint16_t)k;
	}
	kp_ui_preload(ui, list, n);
	if (s->voices) kp_ui_music_start_game(ui);
}

/* A sound event's enable (KP_SOUND_ON, KP_SOUND_OPTION + k, KP_SOUND_OFF) as PlaySoundAtUnit's argument. */
static uint32_t event_enable(KpUi *ui, uint32_t e)
{
	if (e == KP_SOUND_ON) return 1;
	if (e == KP_SOUND_OFF) return 0;
	return ui->options & (1u << (e - KP_SOUND_OPTION));
}

/* IsUnitOffScreen 0x422c20 at the sprite's (x, y): two tiles around the view's (sx + 22, sy + 15 for the original's
 * 640 x 400: its 20 and 13 tiles, the last one partly shown). */
static int off_screen(KpUi *ui, int32_t x, int32_t y)
{
	int32_t tx = x / TILE, ty = y / TILE, sx = ui->input.screen_x >> 5, sy = ui->input.screen_y >> 5;
	int32_t w = ui->view_w / TILE, h = (ui->view_h + TILE - 1) / TILE;
	return !(tx + 2 >= sx && ty + 2 >= sy && tx <= sx + w + 2 && ty <= sy + h + 2);
}

void kp_ui_sound_event(KpUi *ui, int64_t kind, int64_t a, int64_t b, int64_t c)
{
	KpUiSound *s = sd(ui);
	KpGame *g = game(ui);
	uint32_t sfx = (uint32_t)(a & 0xffff), min = (uint32_t)((a >> KP_SOUND_MIN_VOLUME_SHIFT) & 0xff);
	uint32_t enable = (uint32_t)((a >> KP_SOUND_ENABLE_SHIFT) & 0xff); /* a sound event's only (event_enable) */
	int talk = (int)((a >> KP_SOUND_TALK_SHIFT) & 3), camera = (int)((a >> KP_SOUND_CAMERA_SHIFT) & 1), played = 0;
	uint8_t colour = (uint8_t)((a >> KP_SOUND_COLOUR_SHIFT) & 0xff);
	int64_t unit = 0;
	if (!s->voices || g == NULL) return;
	switch (kind) {
	case KP_EVENT_SOUND: /* PlaySoundAtPosition 0x446a20 */
		(void)kp_ui_sound_at_position(ui, sfx, (int32_t)b, (int32_t)c, event_enable(ui, enable), (int32_t)min);
		return;
	case KP_EVENT_SOUND_SCREEN: /* PlaySoundAtUnit without a unit, the advisor's playback; b: the talking unit */
		unit = b;
		played = sound_at_unit(ui, sfx, 0, 0, 0, event_enable(ui, enable), (int32_t)min);
		break;
	case KP_EVENT_SOUND_UNIT:
		unit = b;
		s->call_type = (int32_t)((c >> 32) & 0xffff);
		played = sound_at_unit(ui, sfx, unit, (int16_t)(c & 0xffff), (int16_t)((c >> 16) & 0xffff), event_enable(ui, enable),
			(int32_t)min);
		s->call_type = -1;
		break;
	case KP_EVENT_UNIT_ATTACKED: { /* NotifyUnitAttacked 0x446f00 (its unit's blink is the game's) */
		int32_t x = (int16_t)(c & 0xffff), y = (int16_t)((c >> 16) & 0xffff);
		int64_t found[ATTACK_NOTICE_UNITS];
		int n, k;
		uint32_t id;
		unit = a;
		if (s->cooldown[2] != 0 || !off_screen(ui, x, y) || g->u.attack_notify_timer[unit] != 0) return;
		/* SuppressNearbyAttackNotices 0x4460d0 */
		n = kp_units_in_rect(g, (int16_t)(x - ATTACK_NOTICE_REACH), (int16_t)(y - ATTACK_NOTICE_REACH),
			(int16_t)(x + ATTACK_NOTICE_REACH), (int16_t)(y + ATTACK_NOTICE_REACH), found, ATTACK_NOTICE_UNITS);
		for (k = 0; k < n; k++) g->u.attack_notify_timer[found[k]] = ATTACK_NOTICE_SILENCE;
		s->cooldown[2] = ATTACK_COOLDOWN;
		id = (b ? SOUND_ATTACKED_BUILDING : SOUND_ATTACKED) + (uint32_t)kp_ui_local_race(ui);
		(void)sound_at_unit(ui, id, 0, 0, 0, ui->options & OPTION_ADVISOR, 0);
		kp_ui_voice_talk(ui, unit, id, 1, ATTACK_COLOUR);
		return;
	}
	case KP_EVENT_QUIET: /* EnterQuietMode 0x446930, LeaveQuietMode 0x446fd0 */
		s->quiet = (uint8_t)(a != 0);
		kp_ui_music_quiet(ui, a != 0);
		return;
	case KP_EVENT_TRIGGER_WAV: /* PlayStreamedSoundFile 0x49e810 of the map's WAV */
		kp_ui_stream(ui, (uint16_t)a);
		return;
	case KP_EVENT_TRIGGER_SKIP: /* StopAllStreamedSounds 0x49db50 */
		kp_ui_stop_streams(ui);
		return;
	case KP_EVENT_UNIT_UNPLACED: /* DisplaySystemMessage 0x46b070's sound */
		(void)sound_at_unit(ui, SOUND_SYSTEM_MESSAGE, 0, 0, 0, 1, 0);
		return;
	default:
		return;
	}
	if (unit != 0 && (talk == KP_TALK_ALWAYS || (talk == KP_TALK_PLAYED && played))) {
		kp_ui_voice_talk(ui, unit, sfx, camera, colour);
	}
}
