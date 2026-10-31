/*
 * The product's flow (plan phase S, stage S5): the glue's screens (the menus, the ready rooms, the score screens) run
 * the campaign's games; a game's console is the same interface's; the game's end (an in-game menu's exit or the result
 * dialog's answer) leads to the score screen and the campaign's next game. A development switch (--mission) plays one
 * mission with its console and no glue.
 *
 * Every call into the game, or into the interface while a game runs (it calls into the game), runs under the game's
 * failure handler: a failure stops the game (the session keeps its last state) and the product goes on.
 */
#include "kp_app.h"

#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXIT_PROGRAM 2
#define EXIT_QUIT_MISSION 4
#define EXIT_RESTART 5
#define VIEW_ROWS_ORIGINAL 400 /* the original's game view: 640 x 400 */

double kp_app_now(void) { return (double)SDL_GetTicksNS() / 1e9; }

static double clock_ms(void) { return (double)SDL_GetTicksNS() / 1e6; }

static uint32_t now_ms(void) { return (uint32_t)(SDL_GetTicksNS() / 1000000u); }

static void stop(KpApp *a, const char *what)
{
	SDL_Log("play %s: stopped: %s", a->current >= 0 ? a->missions[a->current].name : "?", what);
}

/* --- the screen and the camera --- */

static void layout(KpApp *a)
{
	int32_t min_w, min_h;
	kp_layout_minimum(a->win_w, a->win_h, a->fixed_screen, &min_w, &min_h);
	kp_layout_screen(a->win_w, a->win_h, a->flags.scale_mode, a->fixed_screen, min_w, min_h, &a->mode, &a->scale,
		&a->world_w, &a->world_h);
	/* a game's view keeps its centre when its screen changes size (a rotation, a fold, split screen) */
	if (a->fixed_screen || !(a->playing && a->hud_on)) {
		a->game_w = a->game_h = 0;
	} else {
		if (a->game_w > 0 && (a->game_w != a->world_w || a->game_h != a->world_h)) {
			kp_hud_keep_focus(a->game_w, a->game_h, a->world_w, a->world_h, &a->cam_x, &a->cam_y);
		}
		a->game_w = a->world_w;
		a->game_h = a->world_h;
	}
}

void kp_flow_clamp_camera(KpApp *a)
{
	int64_t max_x, max_y, mw, mh;
	if (!a->world.active) return;
	mw = a->world.mission->width;
	mh = a->world.mission->height;
	max_x = mw * KP_WORLD_TILE - a->world_w;
	max_y = mh * KP_WORLD_TILE - a->world_h;
	if (a->playing && a->hud_on) {
		/* the interface's view limits (InitScreenScroll's (W - 20) * 32 and (H - 12) * 32 + 8 for the original's
		 * 640 x 400 view, kept for a wider view of the same height), on its 8-pixel grid */
		int32_t vw, vh;
		kp_hud_view_size(a->world_w, a->world_h, &vw, &vh);
		max_x = mw * KP_WORLD_TILE - vw;
		max_y = (mh - 12) * KP_WORLD_TILE + 8 - (vh - VIEW_ROWS_ORIGINAL);
		if (max_x < 0) max_x = 0;
		if (max_y < 0) max_y = 0;
		max_x -= max_x % 8;
		max_y -= max_y % 8;
	}
	if (max_x < 0) max_x = 0;
	if (max_y < 0) max_y = 0;
	if (a->cam_x > max_x) a->cam_x = max_x;
	if (a->cam_y > max_y) a->cam_y = max_y;
	if (a->cam_x < 0) a->cam_x = 0;
	if (a->cam_y < 0) a->cam_y = 0;
}

/* --- the scene --- */

int kp_flow_glue_active(const KpApp *a) { return a->glue_on && a->glue.screen != KP_GLUE_NONE; }

static void end_hud(KpApp *a)
{
	if (a->hud_on) kp_hud_free(&a->hud);
	a->hud_on = 0;
}

/* A game that goes without its menus' end (an in-game load of another mission's save, the program's end): the
 * console's end on the interface as EndGame 0x443db0 does, while the game still stands (the glue's interface outlives
 * the game). */
static void end_game(void *ui) { kp_ui_end_game((KpUi *)ui); }

static void end_console(KpApp *a)
{
	if (kp_session_guard(a->session, end_game, a->ui) != 0) SDL_Log("play: the console's end failed: %s", a->session->error);
}

static void destroy_scene(KpApp *a)
{
	if (a->hud_on && a->session && a->session->g) end_console(a);
	end_hud(a);
	kp_world_free(a);
	if (a->session) {
		kp_session_free(a->session);
		free(a->session);
		a->session = NULL;
	}
	if (a->ui) a->ui->game = NULL; /* a ready room's game too */
	a->result = 0;
}

static KpHudMap hud_map(KpApp *a)
{
	KpHudMap m;
	m.tileset = &a->world.tileset;
	m.palettes = &a->palettes;
	m.tileset_index = (int)(a->world.mission->tileset & 7);
	m.colors = a->world.mission->has_colors ? a->world.mission->colors : NULL;
	return m;
}

/* DeleteSaveFile 0x498b30's file for the interface's save and load dialogs: 0 when it stays (no right to it, held open
 * elsewhere), its name kept in the profile. */
static int delete_save(void *host, const char *name)
{
	KpApp *a = (KpApp *)host;
	char file[200], path[1100];
	kp_save_file_name(name, file, sizeof file);
	kp_app_pref_path(a, file, path, sizeof path);
	if (remove(path) != 0 && errno != ENOENT) {
		SDL_Log("save %s: cannot delete %s: %s", name, path, strerror(errno));
		return 0;
	}
	kp_profile_remove_save(&a->profile, name);
	kp_app_save_profile(a);
	return 1;
}

static void interface_host(KpApp *a, KpUi *ui)
{
	ui->delete_save = delete_save;
	ui->host = a;
}

static int start_hud(KpApp *a, int from_save, char *err, size_t err_size)
{
	KpHudMap m = hud_map(a);
	int32_t min_w, min_h, w, h;
	double scale;
	int mode;
	/* the logical screen a game gets on this window (a game may narrow where a glue screen keeps 640 x 480) */
	kp_layout_minimum(a->win_w, a->win_h, 0, &min_w, &min_h);
	kp_layout_screen(a->win_w, a->win_h, a->flags.scale_mode, 0, min_w, min_h, &mode, &scale, &w, &h);
	if (!a->glue_on) { /* without the glue each game's console has an interface of its own */
		char why[256];
		KpUi *ui = kp_interface_new(a->ui_files, a->ui_sizes, why, sizeof why);
		if (!ui) {
			snprintf(err, err_size, "%s", why);
			return -1;
		}
		kp_cursor_init(&a->cursor);
		kp_ui_free(a->ui);
		a->ui = ui;
		interface_host(a, ui);
		kp_app_video_to_ui(a, ui);
	}
	if (kp_hud_init(&a->hud, a->ui, a->glue_on ? &a->glue.canvas : NULL, a->session->g, &m, now_ms(), w, h, from_save)
		!= 0) {
		snprintf(err, err_size, "the console: %s", a->ui->error);
		kp_hud_free(&a->hud); /* its own canvas */
		return -1;
	}
	a->hud_on = 1;
	/* the game's palette from its console's first frame (a glue screen's stays until then) */
	if (a->has_screen_palette) kp_gfx_palette(a->gfx, kp_palette_rgb(&a->screen_palette));
	kp_glue_saves(a->ui, &a->profile); /* the in-game save dialogs' list */
	a->game_started_at = kp_app_now();
	return 0;
}

/* The session of the mission shown: a campaign's next game takes what the game
 * before it left, a catalog mission played directly starts a run of its campaign. */
static int build_scene(KpApp *a, char *err, size_t err_size)
{
	const KpAppMission *entry = &a->missions[a->current];
	KpSessionStart st;
	char tileset[128];
	uint8_t *mission, *tileset_bytes;
	size_t mission_size, tileset_size;
	int continued = a->has_continue;
	destroy_scene(a);
	a->has_next = 0;
	a->has_continue = 0; /* a start that fails leaves no continuation for the next one */
	if (continued) {
		a->has_run = 1;
		a->run = a->continue_run;
	} else {
		a->has_run = entry->catalog && kp_campaign_run_of_mission(&a->campaign, entry->index, &a->run);
	}
	if (!(mission = kp_app_read(a, entry->kpm + 1, &mission_size))) {
		snprintf(err, err_size, "mission %s: cannot read it", entry->name);
		return -1;
	}
	if (!(a->session = (KpSession *)calloc(1, sizeof *a->session))) {
		free(mission);
		snprintf(err, err_size, "out of memory");
		return -1;
	}
	/* the game reads dat.kpd in place and takes the mission's file; its data serves the world too (the mission parsed
	 * once, its tileset read once) */
	if (kp_session_load(a->session, &a->shared, a->dat, a->dat_size, mission, mission_size, entry->kpm) != 0) {
		snprintf(err, err_size, "play %s: %s", entry->name, a->session->error);
		return -1;
	}
	kp_host_tileset_path(&a->session->host, tileset, sizeof tileset);
	tileset_bytes = kp_app_read(a, tileset, &tileset_size);
	kp_session_start_defaults(&st);
	st.seed = KP_APP_SEED;
	st.mission_index = entry->index;
	st.expansion = a->has_run ? a->run.expansion : 0;
	if (continued) {
		st.has_carry = 1;
		st.carry = a->continue_carry;
		st.cheats = a->continue_cheats;
	}
	if (kp_session_start(a->session, tileset_bytes, tileset_size, &st) != 0) {
		if (!tileset_bytes) snprintf(err, err_size, "play %s: cannot read %s", entry->name, tileset);
		else snprintf(err, err_size, "play %s: %s", entry->name, a->session->error);
		return -1;
	}
	layout(a); /* the scale the world's art is chosen by (a game started before the first frame has none yet) */
	if (kp_world_build(a, err, err_size) != 0) return -1;
	a->cam_x = a->cam_y = 0;
	if (a->load_on_start[0]) { /* a saved game: the console at once, the save loaded next */
		snprintf(a->pending_load, sizeof a->pending_load, "%s", a->load_on_start);
		a->load_on_start[0] = 0;
		if (start_hud(a, 1, err, err_size) != 0) return -1;
	} else if (a->glue_on) { /* the game waits for its ready room's Start (the console comes then) */
		kp_session_pause(a->session);
		if (kp_glue_room(&a->glue, a->session->g, a->glue_room) != 0) {
			snprintf(err, err_size, "the ready room: %s", a->ui->error);
			return -1;
		}
	} else if (start_hud(a, 0, err, err_size) != 0) {
		return -1;
	}
	kp_audio_stop_all(&a->audio); /* the glue's own music stops for the game's */
	return 0;
}

int kp_flow_start_mission(KpApp *a, int index, char *err, size_t err_size)
{
	a->current = index;
	a->playing = 1;
	if (build_scene(a, err, err_size) == 0) return 0;
	/* nothing of a game that did not start stays: no session, no world, no console, no save waiting for it */
	destroy_scene(a);
	a->playing = 0;
	a->load_on_start[0] = 0;
	return -1;
}

/* --- the saves --- */

static int save_game(KpApp *a, const char *file, char *why, size_t why_size)
{
	KpSaveCampaign camp;
	KpSavePresentation pres;
	uint8_t *bytes;
	size_t size;
	char path[1100];
	KpError err;
	uint32_t portrait_ms;
	int camera, k;
	memset(&camp, 0, sizeof camp);
	memset(&pres, 0, sizeof pres);
	if (a->has_run) {
		camp.present = 1;
		camp.run = a->run;
		kp_profile_words(&a->profile.progress, camp.progress);
	}
	/* the view (LoadGameFromSave 0x499890 reads the raw screen position back), and the interface's pending ends of the
	 * trigger waits as a save's milliseconds left */
	kp_save_pres_set(&pres, "camera_x", a->cam_x);
	kp_save_pres_set(&pres, "camera_y", a->cam_y);
	if (a->hud_on) {
		if (kp_ui_pending_releases(a->ui, &portrait_ms, &camera)) kp_save_pres_set(&pres, "portrait_ms", portrait_ms);
		if (camera) kp_save_pres_set(&pres, "camera_ms", 0);
	}
	if (kp_save_encode(a->session, &a->identity, a->has_run ? &camp : NULL, &pres, &bytes, &size, &err) != 0) {
		snprintf(why, why_size, "%s", err.msg);
		return -1;
	}
	kp_app_pref_path(a, file, path, sizeof path);
	k = kp_write_file_atomic(path, bytes, size, &err);
	free(bytes);
	kp_app_sync_files(a);
	if (k != 0) snprintf(why, why_size, "%s", err.msg);
	return k;
}

static int load_game(KpApp *a, const char *file, char *why, size_t why_size)
{
	KpSaveCampaign camp;
	KpSavePresentation pres;
	uint8_t *bytes;
	size_t size;
	char path[1100];
	KpError err;
	int k;
	kp_app_pref_path(a, file, path, sizeof path);
	if (!(bytes = kp_read_file(path, &size))) {
		snprintf(why, why_size, "no save");
		return -1;
	}
	k = kp_save_load(a->session, bytes, size, &a->identity, &camp, &pres, &err);
	free(bytes);
	if (k != 0) {
		snprintf(why, why_size, "%s", err.msg);
		return -1;
	}
	/* the save's run replaces the current one; its profile only raises the profile's progress */
	if (camp.present) {
		KpCampaignProfile p;
		kp_profile_from_words(&p, camp.progress);
		a->has_run = 1;
		a->run = camp.run;
		kp_profile_merge(&a->profile.progress, &p);
		kp_app_save_profile(a);
	}
	a->has_next = 0;
	if (a->hud_on) {
		int64_t x = kp_save_pres_get(&pres, "camera_x", -1), y = kp_save_pres_get(&pres, "camera_y", -1);
		int64_t portrait_ms = kp_save_pres_get(&pres, "portrait_ms", -1);
		kp_ui_game_loaded(a->ui);
		/* the waits the saved game's interface would have ended (the game keeps them waiting) */
		kp_ui_restore_releases(a->ui, portrait_ms >= 0, portrait_ms >= 0 ? (uint32_t)portrait_ms : 0,
			kp_save_pres_get(&pres, "camera_ms", -1) >= 0);
		/* the saved view from this frame on (a save without one keeps the view shown) */
		if (x >= 0 && y >= 0) {
			kp_hud_place_camera(&a->hud, &x, &y);
			a->cam_x = x;
			a->cam_y = y;
		}
	}
	return 0;
}

/* A saved game started from its file (the glue's Load Saved, an in-game load of another mission's save): its mission
 * without a ready room, whose game loads the save once it stands. With other_only a save of the mission playing is
 * refused (its load failed for another reason: starting the mission again would fail the same way). */
static int start_saved_game(KpApp *a, const char *name, int other_only, char *why, size_t why_size)
{
	char file[200], path[1100], mission[256];
	uint8_t *bytes;
	size_t size;
	KpError err;
	int k, rc;
	kp_save_file_name(name, file, sizeof file);
	kp_app_pref_path(a, file, path, sizeof path);
	if (!(bytes = kp_read_file(path, &size))) {
		snprintf(why, why_size, "no save");
		return -1;
	}
	rc = kp_save_mission(bytes, size, &a->identity, mission, sizeof mission, NULL, &err);
	free(bytes);
	if (rc != 0) {
		snprintf(why, why_size, "%s", err.msg);
		return -1;
	}
	if (other_only && a->current >= 0 && strcmp(a->missions[a->current].kpm, mission) == 0) {
		snprintf(why, why_size, "the save is this mission's");
		return -1;
	}
	for (k = 0; k < a->mission_count; k++) {
		if (strcmp(a->missions[k].kpm, mission) == 0) {
			snprintf(a->load_on_start, sizeof a->load_on_start, "%s", name);
			a->glue_room = 0;
			return kp_flow_start_mission(a, k, why, why_size);
		}
	}
	snprintf(why, why_size, "no mission %s", mission);
	return -1;
}

int kp_flow_load_saved(KpApp *a, const char *name, char *why, size_t why_size)
{
	return start_saved_game(a, name, 0, why, why_size);
}

static void save_requests(KpApp *a)
{
	char why[256];
	if (a->pending_save[0]) {
		char file[200], name[64];
		snprintf(name, sizeof name, "%s", a->pending_save);
		a->pending_save[0] = 0;
		kp_save_file_name(name, file, sizeof file);
		if (save_game(a, file, why, sizeof why) == 0) {
			kp_profile_add_save(&a->profile, name);
			kp_app_save_profile(a);
			kp_glue_saves(a->ui, &a->profile);
			SDL_Log("saved %s", name);
		} else {
			SDL_Log("save failed: %s", why);
		}
	}
	/* a save of another mission starts its game, which asks for the save again: loaded at once too */
	while (a->pending_load[0] && a->playing && a->session && a->session->g) {
		char file[200], name[64];
		snprintf(name, sizeof name, "%s", a->pending_load);
		a->pending_load[0] = 0;
		kp_save_file_name(name, file, sizeof file);
		if (load_game(a, file, why, sizeof why) != 0) {
			char why2[256];
			/* another mission's save: its game */
			if (start_saved_game(a, name, 1, why2, sizeof why2) == 0) continue;
			SDL_Log("load failed: %s", why);
			if (!a->playing) { /* the game went with the other mission's failed start */
				SDL_Log("load: %s", why2);
				if (a->glue_on) kp_glue_menus(&a->glue, KP_GLUE_CAMPAIGN);
				else a->quit = 1;
			}
		} else {
			SDL_Log("loaded %s", name);
		}
	}
}

/* --- the campaign --- */

static void campaign_after_game(KpApp *a, int result)
{
	KpCampaignStep step;
	int64_t cinematics[KP_APP_STEP_CINEMATICS];
	int count = 0, k;
	a->step_cinematic_count = 0;
	if (!a->has_run) return;
	kp_campaign_after_game(&a->campaign, &a->run, &a->profile.progress, result,
		(const char *)a->session->g->trig.next_scenario, &step, cinematics, &count, KP_APP_STEP_CINEMATICS);
	for (k = 0; k < count; k++) a->step_cinematics[k] = (int)cinematics[k];
	a->step_cinematic_count = count;
	kp_app_save_profile(a);
	a->has_next = step.kind == KP_CAMPAIGN_STEP_MISSION || step.kind == KP_CAMPAIGN_STEP_RESTART;
	a->step_glue = step.kind == KP_CAMPAIGN_STEP_GLUE ? (int)step.glue : 0;
	if (a->has_next) {
		a->next_mission = step.kind == KP_CAMPAIGN_STEP_MISSION ? step.mission : a->run.mission;
		a->next_glue = step.kind == KP_CAMPAIGN_STEP_MISSION ? (int)step.glue : 0;
		a->next_run = a->run;
		kp_carry_take(a->session->g, &a->next_carry);
		a->next_cheats = kp_cheats(a->session->g);
	}
	SDL_Log("campaign %s: %s", a->missions[a->current].name, a->has_next ? "the next game" : "the campaign ends");
}

static void start_glue_game(KpApp *a, int64_t key, int state)
{
	char err[512];
	int k;
	for (k = 0; k < a->mission_count; k++) {
		if (a->missions[k].catalog && a->missions[k].index == key) {
			a->glue_room = state;
			if (kp_flow_start_mission(a, k, err, sizeof err) != 0) {
				SDL_Log("glue: %s", err);
				kp_glue_menus(&a->glue, KP_GLUE_MAIN);
			}
			return;
		}
	}
	SDL_Log("glue: no catalog mission %lld", (long long)key);
	/* as a start that fails: nothing of the game before stays (its scene, the continuation score_next made of it) */
	destroy_scene(a);
	a->playing = 0;
	a->has_continue = 0;
	kp_glue_menus(&a->glue, KP_GLUE_MAIN);
}

static void leave_glue_game(KpApp *a, int state)
{
	destroy_scene(a);
	a->playing = 0;
	if (state <= 0) state = a->has_run && a->run.expansion != 0 ? KP_GLUE_EXPANSION_CAMPAIGN : KP_GLUE_CAMPAIGN;
	kp_glue_menus(&a->glue, state);
}

static void continue_with_next(KpApp *a)
{
	a->has_continue = 1;
	a->continue_run = a->next_run;
	a->continue_carry = a->next_carry;
	a->continue_cheats = a->next_cheats;
}

/* What the score screen leads to: the next game, or the campaign menu or the step the campaign ended with. */
static void score_next(KpApp *a)
{
	if (a->has_next) {
		int state = a->next_glue ? a->next_glue : a->glue_room;
		continue_with_next(a);
		start_glue_game(a, a->next_mission, state);
	} else {
		leave_glue_game(a, a->step_glue);
	}
}

/* The cinematics (ADR 0015) a glue step plays before it goes on, and what goes on. */
enum { THEN_NONE, THEN_MISSION, THEN_MENUS, THEN_NEXT };

static void cine_continue(KpApp *a)
{
	int kind = a->cine_then.kind;
	a->cine_then.kind = THEN_NONE;
	switch (kind) {
	case THEN_MISSION:
		start_glue_game(a, a->cine_then.mission, a->cine_then.state);
		break;
	case THEN_MENUS:
		kp_glue_menus(&a->glue, a->cine_then.state);
		break;
	case THEN_NEXT:
		score_next(a);
		break;
	default:
		break;
	}
}

/* Plays ids[0..n) (each after its prelude when preludes is set), then goes on with kind: at once when nothing plays (no
 * pack, or none of them in it), as the game did before the cinematics. */
static void cine_then(KpApp *a, const int *ids, int n, int preludes, int kind, int64_t mission, int state)
{
	a->cine_then.kind = kind;
	a->cine_then.mission = mission;
	a->cine_then.state = state;
	if (n > 0 && kp_app_cine_start(a, ids, n, preludes)) return;
	cine_continue(a);
}

int kp_flow_cinematics(KpApp *a, const int *ids, int n)
{
	cine_then(a, ids, n, 0, a->glue_on ? THEN_MENUS : THEN_NONE, 0, KP_GLUE_MAIN);
	return kp_app_cine_active(a);
}

/* A mission history's cinematic row (StartCampaignEpisode 0x4bb5b0): its cinematic, then where AdvanceCampaignAfterGame
 * goes from its row: the mission after it, or for a table's last row the campaign's end (its menu; the epilog's too,
 * as after a score screen). */
static void history_cinematic(KpApp *a, const KpGlueEvent *ev)
{
	KpCampaignRun run;
	KpCampaignStep step;
	int64_t more[KP_APP_STEP_CINEMATICS];
	int ids[KP_APP_STEP_CINEMATICS + 1], n = 0, count = 0, k, state;
	ids[n++] = ev->cinematic;
	if (!kp_campaign_after_cinematic(&a->campaign, ev->expansion, ev->mission, ev->cinematic, &run, &a->profile.progress,
			&step, more, &count, KP_APP_STEP_CINEMATICS)) {
		SDL_Log("glue: no campaign row of cinematic %d and mission %lld", ev->cinematic, (long long)ev->mission);
	}
	for (k = 0; k < count; k++) ids[n++] = (int)more[k];
	kp_app_save_profile(a);
	if (step.kind == KP_CAMPAIGN_STEP_MISSION) {
		kp_ui_menus_mission_draws(a->ui); /* CreateCampaignMissionGame's, which the glue left to the campaign */
		cine_then(a, ids, n, 1, THEN_MISSION, step.mission, (int)step.glue);
		return;
	}
	state = step.kind == KP_CAMPAIGN_STEP_GLUE ? (int)step.glue
		: step.kind == KP_CAMPAIGN_STEP_MENU ? KP_GLUE_MAIN
		: ev->expansion ? KP_GLUE_EXPANSION_CAMPAIGN : KP_GLUE_CAMPAIGN;
	cine_then(a, ids, n, 1, THEN_MENUS, 0, state);
}

/* After an in-game menu ended the game: Exit Program leaves (the main menu on the web), Quit Mission goes to the
 * campaign menu, Restart plays the mission again from its ready room. */
static void glue_after_game(KpApp *a)
{
	int exit = a->after_exit;
	a->after_game = 0;
	if (exit == EXIT_PROGRAM) {
#ifndef __EMSCRIPTEN__
		a->quit = 1;
#endif
		leave_glue_game(a, KP_GLUE_MAIN);
	} else if (exit == EXIT_RESTART && a->has_run) {
		a->has_continue = 1;
		a->continue_run = a->run;
		a->continue_carry = a->after_carry;
		a->continue_cheats = a->after_cheats;
		start_glue_game(a, a->run.mission, a->glue_room);
	} else {
		leave_glue_game(a, 0);
	}
}

/* --- each frame --- */

static void game_events(KpApp *a)
{
	KpEvent e;
	while (kp_event_take(a->session->g, &e)) {
		if (a->hud_on) kp_hud_event(&a->hud, &e);
		if (e.kind == KP_EVENT_SAVE_GAME && e.text) snprintf(a->pending_save, sizeof a->pending_save, "%s", e.text);
		else if (e.kind == KP_EVENT_LOAD_GAME && e.text) snprintf(a->pending_load, sizeof a->pending_load, "%s", e.text);
		else if (e.kind == KP_EVENT_GAME_RESULT) a->result = (int)e.a;
	}
}

static void sounds(KpApp *a)
{
	KpUiSoundCommand c[KP_UI_SOUND_COMMANDS];
	size_t n;
	while ((n = kp_ui_take_sound_commands(a->ui, c, KP_UI_SOUND_COMMANDS)) != 0) kp_audio_commands(a, c, n);
}

/* The vector text of large scales (ADR 0011): the interface records its glyphs unless the font mode is the original's,
 * and its canvas takes them out while the mode is on at this scale with the font's atlas made. */
static void vector_text_mode(KpApp *a)
{
	KpUi *ui = a->ui;
	int record, on;
	if (ui == NULL) return;
	if (a->vtext_ui != ui) { /* a new interface: its fonts, or --vector-font's for all */
		const uint8_t *const *files;
		const size_t *sizes;
		const uint8_t *one[1];
		size_t one_size[1];
		int count = kp_ui_vector_fonts(ui, &files, &sizes);
		kp_vtext_free(&a->vtext);
		if (a->flags.vector_font[0]) {
			if (!a->vtext_file) a->vtext_file = SDL_LoadFile(a->flags.vector_font, &a->vtext_file_size);
			one[0] = (const uint8_t *)a->vtext_file;
			one_size[0] = a->vtext_file_size;
			files = one;
			sizes = one_size;
			count = a->vtext_file ? 1 : 0;
			if (!a->vtext_file) SDL_Log("vector text: cannot read %s", a->flags.vector_font);
		}
		if (count > 0 && kp_vtext_init(&a->vtext, files, sizes, count) != 0) SDL_Log("vector text: a font is no TrueType font");
		a->vtext.size = a->flags.vector_size > 0 ? a->flags.vector_size : 1.0;
		a->vtext_ui = ui;
	}
	record = a->vtext.ttf_count > 0 && a->flags.font_mode != KP_VTEXT_ORIGINAL;
	on = record && kp_vtext_active(a->flags.font_mode, a->scale)
		&& kp_vtext_atlas(&a->vtext, ui->font_files, ui->font_file_sizes, a->scale) == 0;
	if (record != (ui->text.log != NULL) || on != ui->vector_text) kp_ui_set_vector_text(ui, record, on, a->vtext.codes);
}

/* --- the video options (the dialog koprulu\video.bin, the profile's VOPT) --- */

/* The development checks: their options are the originals' or the defaults (the video check's), never the player's,
 * and they write none. */
static int dev_checks(const KpApp *a)
{
	return a->flags.autotest || a->flags.play_check || a->flags.video_check[0] || a->flags.hd_check[0]
		|| a->flags.save_check[0] || a->flags.touch_check[0]
		|| a->flags.cine_check[0];
}

/* The options the player has: the HD variants imported (a computer's x4; a phone's and the web's x2 only) and the vector
 * text's TrueType fonts. */
static uint8_t video_available(KpApp *a)
{
	uint8_t has = 0;
	size_t n, file_size;
	uint8_t *head;
	const uint8_t *const *fonts;
	const size_t *font_sizes;
	if ((head = kp_app_read_head(a, "generated/hd/hd2/ui.kpd", 1, &n, &file_size)) != NULL) has |= KP_VIDEO_HAS_HD2;
	free(head);
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS) && !defined(__EMSCRIPTEN__)
	if ((head = kp_app_read_head(a, "generated/hd/hd/ui.kpd", 1, &n, &file_size)) != NULL) has |= KP_VIDEO_HAS_HD;
	free(head);
#endif
	if (kp_ui_vector_fonts(a->ui, &fonts, &font_sizes) > 0 || a->flags.vector_font[0]) has |= KP_VIDEO_HAS_VECTOR;
	return has;
}

/* The gamma on the screen: the game's palette and kp_gfx's HD interface art (a glue screen's palette at its upload). */
static void apply_gamma(KpApp *a)
{
	kp_palette_set_brightness(&a->screen_palette, a->gamma);
	kp_gfx_gamma(a->gfx, a->gamma);
	if (a->has_screen_palette && !kp_flow_glue_active(a)) kp_gfx_palette(a->gfx, kp_palette_rgb(&a->screen_palette));
}

/* At the start: the profile's options under the flags given (the development checks keep the originals'). */
static void video_from_profile(KpApp *a)
{
	const KpVideoSettings *v = &a->profile.video;
	int given = a->flags.video_given;
	a->video_available = video_available(a);
	a->video_flags.graphics = a->flags.graphics;
	a->video_flags.fog = a->flags.fog_style;
	a->video_flags.water = a->flags.water;
	a->video_flags.font = a->flags.font_mode;
	if (dev_checks(a)) {
		a->gamma = KP_UI_GAMMA_DEFAULT;
		a->portraits = KP_UI_PORTRAITS_ANIMATED;
	} else {
		a->gamma = v->gamma;
		a->portraits = v->portraits;
		if (!(given & KP_VIDEO_GIVEN_GRAPHICS)) a->flags.graphics = v->graphics;
		if (!(given & KP_VIDEO_GIVEN_FOG)) a->flags.fog_style = v->fog;
		if (!(given & KP_VIDEO_GIVEN_WATER)) a->flags.water = v->water;
		if (!(given & KP_VIDEO_GIVEN_FONT)) a->flags.font_mode = v->font;
	}
	apply_gamma(a);
}

void kp_app_video_to_ui(KpApp *a, KpUi *ui)
{
	KpUiVideoOptions *o = &ui->video_options;
	ui->gamma = (uint32_t)a->gamma;
	kp_ui_portrait_set_mode(ui, (uint32_t)a->portraits);
	o->graphics = (int8_t)a->flags.graphics;
	o->fog = (int8_t)a->flags.fog_style;
	o->water = (int8_t)a->flags.water;
	o->font = (int8_t)a->flags.font_mode;
	o->available = a->video_available;
	a->video_serial = o->serial;
	a->video_commits = o->commits;
}

/* The dialog's changes (each frame): applied at once (the world's art at its next frame, kp_world_frame), kept in the
 * profile at OK. */
static void video_options(KpApp *a)
{
	const KpUiVideoOptions *o = &a->ui->video_options;
	if (o->serial != a->video_serial) {
		a->video_serial = o->serial;
		a->flags.graphics = o->graphics;
		a->flags.fog_style = o->fog;
		a->flags.water = o->water;
		a->flags.font_mode = o->font;
		a->portraits = (int32_t)a->ui->portrait.mode;
		if (a->gamma != (int32_t)a->ui->gamma) {
			a->gamma = (int32_t)a->ui->gamma;
			apply_gamma(a);
		}
	}
	if (o->commits != a->video_commits) {
		KpVideoSettings run;
		a->video_commits = o->commits;
		if (dev_checks(a)) return; /* the checks' options are not the player's */
		run.gamma = a->gamma;
		run.portraits = a->portraits;
		run.graphics = a->flags.graphics;
		run.fog = a->flags.fog_style;
		run.water = a->flags.water;
		run.font = a->flags.font_mode;
		kp_profile_commit_video(&a->profile.video, &run, &a->video_flags, a->flags.video_given);
		kp_app_save_profile(a);
	}
}

/* The canvas's vector glyphs for the frame (NULL canvas: none). */
static void vector_text_draw(KpApp *a, const KpCanvas *c)
{
	if (c != NULL && a->ui != NULL && a->ui->vector_text) {
		kp_vtext_quads(&a->vtext, &c->canvas);
		kp_gfx_text(a->gfx, &a->vtext);
	} else {
		kp_gfx_text(a->gfx, NULL);
	}
}

/* The interface's cursor is drawn over the canvas; the system's is hidden over the window while it is. */
static void cursor(KpApp *a, int show)
{
	int drawn = kp_cursor_frame(&a->cursor, show ? a->ui : NULL, kp_input_cursor(a) != KP_HUD_POINTER_NONE);
	if (drawn) {
		kp_gfx_cursor(a->gfx, &a->cursor, a->mouse_lx + a->cursor.dx, a->mouse_ly + a->cursor.dy);
	} else {
		kp_gfx_cursor(a->gfx, NULL, 0, 0);
	}
	kp_hd_ui_cursor(a, drawn);
	if ((drawn != 0) == SDL_CursorVisible()) {
		if (drawn) SDL_HideCursor();
		else SDL_ShowCursor();
	}
}

/* A glue screen's palette with the gamma (as kp_palette_rgb gives the game's). */
static void glue_palette(KpApp *a)
{
	uint8_t table[256], rgb[768];
	int k;
	kp_palette_gamma_table(a->gamma, table);
	for (k = 0; k < 768; k++) rgb[k] = table[a->glue_rgb[k]];
	kp_gfx_palette(a->gfx, rgb);
}

static void glue_frame(KpApp *a)
{
	KpGlueEvent ev;
	int changed, palette;
	vector_text_mode(a);
	kp_hd_ui_mode(a, &a->glue.canvas);
	ev = kp_glue_frame(&a->glue, a->world_w, a->world_h, now_ms(), &changed, a->glue_rgb, &palette);
	if (palette) glue_palette(a);
	kp_hd_ui_draw(a, &a->glue.canvas);
	kp_gfx_canvas(a->gfx, &a->glue.canvas);
	vector_text_draw(a, &a->glue.canvas);
	sounds(a);
	video_options(a);
	if (ev.profile_changed) kp_app_save_profile(a);
	switch (ev.kind) {
	case KP_GLUE_EXIT:
		SDL_Log("glue: exit");
#ifndef __EMSCRIPTEN__
		a->quit = 1;
#endif
		kp_glue_menus(&a->glue, KP_GLUE_MAIN);
		break;
	case KP_GLUE_MISSION:
		if (ev.cinematic != 0) history_cinematic(a, &ev);
		else start_glue_game(a, ev.mission, ev.state);
		break;
	case KP_GLUE_INTRO: { /* View Intro (mode 0): Blizzard's logo, then the Brood War intro */
		static const int INTRO[2] = { KP_APP_CINE_LOGO, KP_APP_CINE_EXPANSION_INTRO };
		cine_then(a, INTRO, 2, 0, THEN_MENUS, 0, KP_GLUE_MAIN);
		break;
	}
	case KP_GLUE_LOAD: {
		char why[256];
		if (ev.save < 0 || ev.save >= a->profile.save_count
			|| kp_flow_load_saved(a, a->profile.saves[ev.save], why, sizeof why) != 0) {
			SDL_Log("glue: load failed");
			kp_glue_menus(&a->glue, KP_GLUE_CAMPAIGN);
		}
		break;
	}
	case KP_GLUE_ROOM_START: { /* the game with the same interface's console */
		char err[256];
		if (start_hud(a, 0, err, sizeof err) != 0) {
			SDL_Log("%s", err);
			leave_glue_game(a, 0);
			break;
		}
		kp_session_resume(a->session);
		break;
	}
	case KP_GLUE_ROOM_CANCEL:
		leave_glue_game(a, 0);
		break;
	case KP_GLUE_SCORE_DONE: { /* the cinematic rows between the missions play before the next step (mode 10) */
		int n = a->step_cinematic_count;
		a->step_cinematic_count = 0; /* kp_app_cine_start copies the ids into its queue */
		cine_then(a, a->step_cinematics, n, 1, THEN_NEXT, 0, 0);
		break;
	}
	default:
		break;
	}
}

/* The console's frame and what it ended with. */
static void hud_frame(KpApp *a, int ticks)
{
	KpUi *ui = a->ui;
	int moved, released;
	int exit;
	vector_text_mode(a);
	kp_hd_ui_mode(a, a->hud.canvas);
	kp_hud_frame(&a->hud, a->world_w, a->world_h, now_ms(), ticks, kp_input_cursor(a), a->mouse_lx, a->mouse_ly, &a->cam_x,
		&a->cam_y, &moved, &released);
	kp_hd_ui_draw(a, a->hud.canvas);
	kp_gfx_canvas(a->gfx, a->hud.canvas);
	vector_text_draw(a, a->hud.canvas);
	cursor(a, 1);
	if (moved) kp_flow_clamp_camera(a);
	if (released) kp_session_release_wait(a->session);
	kp_hud_take_commands(ui, a->session);
	sounds(a);
	video_options(a);
	exit = ui->game_exit;
	if (exit == EXIT_PROGRAM || exit == EXIT_QUIT_MISSION || exit == EXIT_RESTART) {
		/* the in-game menus' Exit Program, Quit Mission, Restart (no result) */
		kp_ui_end_game(ui);
		sounds(a);
		SDL_Log("play %s: exit %d", a->missions[a->current].name, exit);
		kp_session_pause(a->session);
		end_hud(a);
		if (a->glue_on) {
			a->after_game = 1;
			a->after_exit = exit;
			kp_carry_take(a->session->g, &a->after_carry);
			a->after_cheats = kp_cheats(a->session->g);
		} else if (exit == EXIT_PROGRAM) {
			a->quit = 1;
		}
	} else if (exit != 0 && a->result) {
		/* the result dialog's answer (RequestGameExit 0x444e00: 6 victory, 7 defeat) ends the game */
		int result = a->result;
		kp_ui_end_game(ui);
		sounds(a);
		campaign_after_game(a, result);
		kp_session_pause(a->session);
		end_hud(a);
		if (a->glue_on) {
			kp_glue_score(&a->glue, a->session->g, result, (uint32_t)(kp_app_now() - a->game_started_at));
		}
		a->result = 0;
	}
}

/* The game's part of a frame, under its failure handler. */
static void game_frame(KpApp *a)
{
	KpSession *s = a->session;
	KpTry t;
	volatile int ticks = 0;
	double fraction = 1;
	if (!s || !s->g) return;
	kp_images_begin(s->g->w, &t);
	if (setjmp(t.buf)) {
		kp_images_end(s->g->w, &t);
		s->failed = 1;
		snprintf(s->error, sizeof s->error, "%s", s->g->w->error);
		stop(a, s->error);
		return;
	}
	if (!kp_flow_glue_active(a) && !s->failed && !a->flags.autotest) { /* the autotest steps the game itself */
		ticks = kp_session_advance(s, kp_app_now(), &fraction, clock_ms);
		/* a loaded save: its state at once, without motion from the state before */
		if (s->loads != a->world.loads) {
			a->world.loads = s->loads;
			kp_presenter_restart(&a->world.presenter);
			a->world.hd_prime = 1; /* its first frame waits for its HD pages, as a game's first frame does */
			if (ticks < 1) ticks = 1;
		}
	}
	/* each draw refreshes the remembered terrain in view (RefreshRememberedTerrainOnscreen 0x40b220) */
	{
		int64_t x0 = a->cam_x / KP_WORLD_TILE, y0 = a->cam_y / KP_WORLD_TILE;
		kp_refresh_terrain_onscreen(s->g, x0, y0, (a->cam_x + a->world_w + KP_WORLD_TILE - 1) / KP_WORLD_TILE - x0,
			(a->cam_y + a->world_h + KP_WORLD_TILE - 1) / KP_WORLD_TILE - y0);
	}
	game_events(a);
	/* a game's draw refreshes the bounds of its images, as the original's redraw does (RefreshAllSpriteBounds
	 * 0x415700): the steps move the sprites, not their bounds, and the draw list reads the bounds */
	if (ticks > 0) kp_images_refresh_bounds(s->g->w);
	kp_world_frame(a, ticks, fraction);
	if (a->flags.fog) {
		kp_fog_layer_update(&a->world.fog, s->g, a->cam_x, a->cam_y, a->world_w, a->world_h);
		kp_gfx_fog(a->gfx, &a->world.fog, a->palettes.tilesets[a->world.mission->tileset & 7].dark);
	} else {
		kp_gfx_fog(a->gfx, NULL, NULL);
	}
	if (a->hud_on) hud_frame(a, ticks);
	kp_images_end(s->g->w, &t);
}

void kp_flow_frame(KpApp *a)
{
	if (a->cine_then.kind != THEN_NONE && !kp_app_cine_active(a)) cine_continue(a); /* the cinematics ended */
	if (a->after_game) glue_after_game(a);
	a->fixed_screen = kp_flow_glue_active(a); /* a glue screen keeps the original's 640 x 480 */
	layout(a);
	if (a->fixed_screen) {
		glue_frame(a);
		cursor(a, 1);
		if (!kp_flow_glue_active(a)) { /* a game took over: its layout from its first frame */
			a->fixed_screen = 0;
			layout(a);
		}
	} else if (!(a->playing && a->hud_on)) {
		cursor(a, 0);
	}
	/* the saves the game asked for, and a save the glue's load started a game for: loaded before its first frame
	 * draws, which shows the saved state, not the mission's start */
	if (a->playing && a->session && a->session->g) save_requests(a);
	kp_flow_clamp_camera(a);
	if (a->playing && a->world.active) game_frame(a);
	if (!a->hud_on && !kp_flow_glue_active(a)) {
		kp_gfx_canvas(a->gfx, NULL);
		vector_text_draw(a, NULL);
	}
	/* colour cycles run on real time; not while a glue screen's palette shows */
	if (a->has_screen_palette && !kp_flow_glue_active(a)) {
		double now = kp_app_now();
		if (kp_palette_advance(&a->screen_palette, (now - a->palette_clock) * 1000, 0)) {
			kp_gfx_palette(a->gfx, kp_palette_rgb(&a->screen_palette));
		}
		a->palette_clock = now;
	}
}

int kp_flow_init(KpApp *a, char *err, size_t err_size)
{
	char why[256];
	kp_app_load_profile(a);
	a->current = -1;
	if (!(a->ui = kp_interface_new(a->ui_files, a->ui_sizes, why, sizeof why))) {
		snprintf(err, err_size, "%s", why);
		return -1;
	}
	kp_cursor_init(&a->cursor);
	interface_host(a, a->ui);
	video_from_profile(a);
	kp_app_video_to_ui(a, a->ui);
	kp_glue_saves(a->ui, &a->profile);
	if (a->flags.mission[0]) {
		int k;
		for (k = 0; k < a->mission_count; k++) {
			if (strcmp(a->missions[k].name, a->flags.mission) == 0) {
				if (!a->flags.play) a->flags.play = 1;
				return kp_flow_start_mission(a, k, err, err_size);
			}
		}
		snprintf(err, err_size, "no mission %s", a->flags.mission);
		return -1;
	}
	if (a->flags.play_check && (a->flags.play_check < 1 || a->flags.play_check > KP_APP_PLAY_CHECKS)) {
		snprintf(err, err_size, "--play-check %d: no such check (1..%d)", a->flags.play_check, KP_APP_PLAY_CHECKS);
		return -1;
	}
	if (a->flags.autotest || a->flags.play_check) return 0; /* the checks start their games themselves */
	kp_glue_init(&a->glue, a->ui, &a->profile, now_ms());
	a->glue_on = 1;
	/* View Intro plays the logo and the intro the pack has */
	a->glue.cinematics = kp_app_cine_has(a, KP_APP_CINE_LOGO) || kp_app_cine_has(a, KP_APP_CINE_EXPANSION_INTRO);
	SDL_Log("glue: the main menu");
	return 0;
}

void kp_flow_free(KpApp *a)
{
	destroy_scene(a);
	if (a->glue_on) kp_glue_free(&a->glue);
	a->glue_on = 0;
	if (a->ui) kp_ui_free(a->ui);
	a->ui = NULL;
	kp_vtext_free(&a->vtext);
	a->vtext_ui = NULL;
	SDL_free(a->vtext_file);
	a->vtext_file = NULL;
}
