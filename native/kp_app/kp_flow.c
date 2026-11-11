/*
 * The product's flow (plan phase S, stage S5; kp_flow.h): the glue's screens (the menus, the ready rooms, the score
 * screens) run the campaign's games; a game's console is the same interface's; the game's end (an in-game menu's exit or
 * the result dialog's answer) leads to the score screen and the campaign's next game. A development switch (--mission)
 * plays one mission with its console and no glue.
 *
 * Every call into the game, or into the interface while a game runs (it calls into the game), runs under the game's
 * failure handler: a failure stops the game (the session keeps its last state) and the product goes on.
 */
#include "kp_flow.h"

#include <ctype.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXIT_PROGRAM 2
#define EXIT_QUIT_MISSION 4
#define EXIT_RESTART 5
#define VIEW_ROWS_ORIGINAL 400 /* the original's game view: 640 x 400 */
#define LOG_LINE 512

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static void say(KpFlow *f, const char *fmt, ...)
{
	char line[LOG_LINE];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	f->host.log(f->host.ctx, line);
}

static double now(KpFlow *f) { return f->host.now(f->host.ctx); }

/* A language's overlay over the campaign mission just loaded (ADR 0016): its text (generated/locale/<loc>/missions/)
 * or its speech's triggers (generated/speech/<loc>/missions/, stage L5: the TIME a transmission waits), laid by `lay`
 * (which takes it); none for the base's, or a refused one, leaves the mission's, with one line naming `what` and `id`. */
static void mission_overlay(KpFlow *f, const KpAppMission *entry, const char *root, const char *what, const char *id,
	const char *own, int (*lay)(KpHostGame *, uint8_t *, size_t, KpError *))
{
	static const char PREFIX[] = "/generated/missions/";
	char path[256];
	uint8_t *overlay;
	size_t size = 0;
	KpError e;
	if (id[0] == 0 || strcmp(id, KP_LOCALE_BASE) == 0 || strncmp(entry->kpm, PREFIX, sizeof PREFIX - 1) != 0) return;
	snprintf(path, sizeof path, "generated/%s/%s/missions/%s", root, id, entry->kpm + sizeof PREFIX - 1);
	if (!(overlay = f->host.read(f->host.ctx, path, &size))) {
		say(f, "%s %s: no %s, the mission's own %s", what, id, path, own);
	} else if (lay(&f->session->host, overlay, size, &e) != 0) {
		say(f, "%s %s: %s: %s", what, id, path, e.msg);
	}
}

static uint32_t now_ms(KpFlow *f) { return (uint32_t)(uint64_t)(now(f) * 1000); }

static void stop(KpFlow *f, const char *what)
{
	say(f, "play %s: stopped: %s", f->current >= 0 ? f->missions[f->current].name : "?", what);
}

static void pref_path(const KpFlow *f, const char *file, char *out, size_t size) { snprintf(out, size, "%s%s", f->pref, file); }

/* The file of the logged-in ID's save name (the original's save\<ID>\<name>). */
static void save_file(const KpFlow *f, const char *name, char *out, size_t size)
{
	kp_save_file_name(kp_profile_save_owner(f->profile), name, out, size);
}

/* --- the screen and the camera --- */

void kp_flow_picture(KpFlow *f)
{
	const int32_t safe[4] = { f->safe_x, f->safe_y, f->safe_w, f->safe_h };
	int32_t p[4];
	kp_layout_picture(f->fixed_screen, f->full_w, f->full_h, safe, p);
	f->win_x = p[0];
	f->win_y = p[1];
	f->win_w = p[2];
	f->win_h = p[3];
}

/* The console's pieces of an interface (none: the original's console on every screen). */
static void console_art(KpFlow *f, KpUi *ui)
{
	ui->console_edges = f->flags->console_edges;
	if (!f->console_art) {
		say(f, "no console.kpd: the original's console; the converter's hd command writes it from a Remastered install");
	} else if (kp_ui_set_console_art(ui, f->console_art, f->console_art_size) != 0) {
		say(f, "console.kpd is not current: the original's console; regenerate the data with the converter");
	}
}

static void layout(KpFlow *f)
{
	int32_t min_w, min_h;
	kp_flow_picture(f);
	kp_layout_minimum(f->win_w, f->win_h, f->fixed_screen, &min_w, &min_h);
	kp_layout_screen(f->win_w, f->win_h, f->flags->scale_mode, f->fixed_screen, min_w, min_h, &f->mode, &f->scale,
		&f->world_w, &f->world_h);
	/* a game's view keeps its centre when its screen changes size (a rotation, a fold, split screen) */
	if (f->fixed_screen || !(f->playing && f->hud_on)) {
		f->game_w = f->game_h = 0;
	} else {
		if (f->game_w > 0 && (f->game_w != f->world_w || f->game_h != f->world_h)) {
			kp_hud_keep_focus(f->game_w, f->game_h, f->world_w, f->world_h, &f->cam_x, &f->cam_y);
		}
		f->game_w = f->world_w;
		f->game_h = f->world_h;
	}
}

void kp_flow_clamp_camera(KpFlow *f)
{
	int64_t max_x, max_y, mw, mh;
	const KpMission *m;
	if (!f->world) return;
	m = kp_session_mission(f->session);
	mw = m->width;
	mh = m->height;
	max_x = mw * KP_WORLD_TILE - f->world_w;
	max_y = mh * KP_WORLD_TILE - f->world_h;
	if (f->playing && f->hud_on) {
		/* the interface's view limits (InitScreenScroll's (W - 20) * 32 and (H - 12) * 32 + 8 for the original's
		 * 640 x 400 view, kept for a wider view of the same height), on its 8-pixel grid */
		int32_t vw, vh;
		kp_hud_view_size(f->world_w, f->world_h, &vw, &vh);
		max_x = mw * KP_WORLD_TILE - vw;
		max_y = (mh - 12) * KP_WORLD_TILE + 8 - (vh - VIEW_ROWS_ORIGINAL);
		if (max_x < 0) max_x = 0;
		if (max_y < 0) max_y = 0;
		max_x -= max_x % 8;
		max_y -= max_y % 8;
	}
	if (max_x < 0) max_x = 0;
	if (max_y < 0) max_y = 0;
	if (f->cam_x > max_x) f->cam_x = max_x;
	if (f->cam_y > max_y) f->cam_y = max_y;
	if (f->cam_x < 0) f->cam_x = 0;
	if (f->cam_y < 0) f->cam_y = 0;
}

/* --- the scene --- */

int kp_flow_glue_active(const KpFlow *f) { return f->glue_on && f->glue.screen != KP_GLUE_NONE; }

static void end_hud(KpFlow *f)
{
	if (f->hud_on) kp_hud_free(&f->hud);
	f->hud_on = 0;
}

/* A game that goes without its menus' end (an in-game load of another mission's save, the program's end): the
 * console's end on the interface as EndGame 0x443db0 does, while the game still stands (the glue's interface outlives
 * the game). */
static void end_game(void *ui) { kp_ui_end_game((KpUi *)ui); }

static void end_console(KpFlow *f)
{
	if (kp_session_guard(f->session, end_game, f->ui) != 0) say(f, "play: the console's end failed: %s", f->session->error);
}

static void destroy_scene(KpFlow *f)
{
	if (f->hud_on && f->session && f->session->g) end_console(f);
	end_hud(f);
	/* only a world that stands (a build that fails frees its own): a flow freed before its host was set has none */
	if (f->world) f->host.world_free(f->host.ctx);
	f->world = 0;
	if (f->session) {
		kp_session_free(f->session);
		free(f->session);
		f->session = NULL;
	}
	if (f->ui) f->ui->game = NULL; /* a ready room's game too */
	f->result = 0;
}

/* The file of owner's save name deleted: 0 when it stays (no right to it, held open elsewhere; logged), a missing one
 * counted as deleted. */
static int remove_save_file(KpFlow *f, const char *owner, const char *name)
{
	char file[KP_SAVE_FILE_NAME], path[1100];
	kp_save_file_name(owner, name, file, sizeof file);
	pref_path(f, file, path, sizeof path);
	if (remove(path) == 0 || errno == ENOENT) return 1;
	say(f, "save %s: cannot delete %s: %s", name, path, strerror(errno));
	return 0;
}

/* DeleteSaveFile 0x498b30's file for the interface's save and load dialogs: 0 when it stays, its name kept in the
 * profile. */
static int delete_save(void *host, const char *name)
{
	KpFlow *f = (KpFlow *)host;
	if (!remove_save_file(f, kp_profile_save_owner(f->profile), name)) return 0;
	kp_profile_remove_save(f->profile, name);
	f->host.save_profile(f->host.ctx);
	return 1;
}

static void delete_save_file(void *host, const KpProfileSave *save)
{
	/* DeleteFoundFile 0x498ae0 does not look at DeleteFileA's result: the save leaves the list anyway */
	(void)remove_save_file((KpFlow *)host, save->owner, save->name);
}

/* DeleteProfileSaves 0x498a70 for the login's deleted ID: its saves' files and names (the login list's sync writes the
 * profile). */
static void delete_profile_saves(void *host, const char *name)
{
	KpFlow *f = (KpFlow *)host;
	kp_profile_drop_saves(f->profile, name, delete_save_file, f);
}

/* A save file of an older profile to its name now (kp_profile_rename_old_saves): 0 when it failed. One renamed by a run
 * that did not write the profile (a check's) has its new name already. */
static int rename_old_save(void *host, const char *from, const char *to)
{
	KpFlow *f = (KpFlow *)host;
	char a[1100], b[1100];
	FILE *done;
	int why;
	pref_path(f, from, a, sizeof a);
	pref_path(f, to, b, sizeof b);
	if (rename(a, b) == 0) return 1;
	why = errno; /* the rename's, before the look at the new name */
	if ((done = fopen(b, "rb")) != NULL) {
		fclose(done);
		return 1;
	}
	say(f, "save: cannot rename %s to %s: %s (it leaves the list)", a, b, strerror(why));
	return 0;
}

static void interface_host(KpFlow *f, KpUi *ui)
{
	ui->delete_save = delete_save;
	ui->delete_profile_saves = delete_profile_saves;
	ui->host = f;
}

int kp_flow_dev_checks(const KpAppFlags *fl)
{
	return fl->autotest || fl->cine_check[0] || fl->originals;
}

int kp_flow_scripted(const KpAppFlags *fl) { return fl->script[0] || fl->script_live[0]; }

static int dev_checks(const KpFlow *f) { return kp_flow_dev_checks(f->flags); }

/* A run that checks, measures or captures has the originals' options and the clock's speed, and writes none of the
 * player's: the tips' dialog would pause its game, a silence close its music, the speed change what its frames show. */
static int keeps_originals(const KpFlow *f)
{
	return dev_checks(f) || f->flags->bench_frames > 0 || f->flags->screenshot[0] != 0;
}

int kp_flow_frame_cap(const KpFlow *f)
{
	if (f->flags->frame_cap > 0) return f->flags->frame_cap;
	/* a script's frames are its time (its waits count them): the profile's throttle would only slow it */
	return !keeps_originals(f) && !kp_flow_scripted(f->flags) && f->ui && f->ui->cpu_throttle ? KP_THROTTLE_FPS : 0;
}

/* The run's check boxes (options_seen) and the profile's options of the sound and speed dialogs and the tips into an
 * interface: the glue's at the start, a glue-less game's own. */
static void options_to_ui(const KpFlow *f, KpUi *ui)
{
	const KpGameSettings *s = &f->profile->settings;
	ui->options = (ui->options & ~KP_UI_OPTIONS_KEPT) | f->options_seen;
	if (keeps_originals(f)) return;
	ui->sound.music = s->music;
	ui->sound.effects = s->effects;
	ui->mouse_scroll_speed = s->mouse_scroll;
	ui->key_scroll_speed = s->key_scroll;
	ui->tip_index = s->tip;
	ui->cpu_throttle = (uint8_t)s->cpu_throttle;
	kp_ui_apply_volumes(ui); /* a profile's silence closes the music before its first track */
}

/* A new interface over the data the flow was given: the original files' (ADR 0018, stage D5), else the containers. */
static KpUi *new_interface(KpFlow *f, char *why, size_t why_size)
{
	if (f->ui_data) return kp_interface_new_data(f->ui_data, &f->locale, why, why_size);
	return kp_interface_new(f->ui_files, f->ui_sizes, &f->locale, why, why_size);
}

static int start_hud(KpFlow *f, int from_save, char *err, size_t err_size)
{
	KpHudMap m;
	int32_t min_w, min_h, w, h;
	double scale;
	int mode;
	f->host.hud_map(f->host.ctx, &m);
	/* the logical screen a game gets on this window (a game may narrow where a glue screen keeps 640 x 480) */
	kp_layout_minimum(f->safe_w, f->safe_h, 0, &min_w, &min_h);
	kp_layout_screen(f->safe_w, f->safe_h, f->flags->scale_mode, 0, min_w, min_h, &mode, &scale, &w, &h);
	if (!f->glue_on) { /* without the glue each game's console has an interface of its own */
		char why[256];
		KpUi *ui = new_interface(f, why, sizeof why);
		if (!ui) {
			snprintf(err, err_size, "%s", why);
			return -1;
		}
		console_art(f, ui);
		f->host.interface(f->host.ctx, ui);
		kp_ui_free(f->ui);
		f->ui = ui;
		interface_host(f, ui);
		kp_flow_video_to_ui(f, ui);
		options_to_ui(f, ui);
	}
	if (kp_hud_init(&f->hud, f->ui, f->glue_on ? &f->glue.canvas : NULL, f->session->g, &m, now_ms(f), w, h, from_save)
		!= 0) {
		snprintf(err, err_size, "the console: %s", f->ui->error);
		kp_hud_free(&f->hud); /* its own canvas */
		return -1;
	}
	f->hud_on = 1;
	/* the game's palette from its console's first frame (a glue screen's stays until then) */
	if (f->has_screen_palette) f->host.palette(f->host.ctx, kp_palette_rgb(&f->screen_palette));
	kp_glue_saves(f->ui, f->profile); /* the in-game save dialogs' list */
	f->game_started_at = now(f);
	return 0;
}

/* The session of a mission of the original data (ADR 0018, stage D5): its scenario's mission built from the files
 * (kp_mission_from_files), a test scenario's .kpm, over a copy of the data set's DAT, with its CV5 and VF4. */
static int load_original(KpFlow *f, const KpAppMission *entry, char *err, size_t err_size)
{
	KpMission m;
	KpError e;
	memset(&m, 0, sizeof m);
	if (entry->directory[0]) {
		if (kp_mission_from_files(&m, f->files, entry->directory, &e) != 0) {
			snprintf(err, err_size, "mission %s: %s", entry->name, e.msg);
			return -1;
		}
	} else {
		size_t size;
		uint8_t *bytes = f->host.read(f->host.ctx, entry->kpm + 1, &size);
		if (!bytes) {
			snprintf(err, err_size, "mission %s: cannot read it", entry->name);
			return -1;
		}
		if (kp_mission_take(&m, bytes, size, entry->kpm, NULL, &e) != 0) { /* the bytes taken, also on failure */
			snprintf(err, err_size, "mission %s: %s", entry->name, e.msg);
			return -1;
		}
	}
	if (!(f->session = (KpSession *)calloc(1, sizeof *f->session))) {
		kp_mission_free(&m);
		snprintf(err, err_size, "out of memory");
		return -1;
	}
	if (kp_session_load_from(f->session, f->shared, f->dat_proto, &m, entry->kpm, f->files) != 0) {
		snprintf(err, err_size, "play %s: %s", entry->name, f->session->error);
		return -1;
	}
	return 0;
}

/* The session of the mission shown: a campaign's next game takes what the game
 * before it left, a catalog mission played directly starts a run of its campaign. */
static int build_scene(KpFlow *f, char *err, size_t err_size)
{
	const KpAppMission *entry = &f->missions[f->current];
	KpSessionStart st;
	char tileset[128];
	uint8_t *mission, *tileset_bytes;
	size_t mission_size, tileset_size;
	int continued = f->has_continue, started = f->has_start_run;
	destroy_scene(f);
	f->has_next = 0;
	f->has_continue = 0; /* a start that fails leaves no continuation for the next one */
	f->has_start_run = 0;
	if (continued) {
		f->has_run = 1;
		f->run = f->continue_run;
	} else if (started && f->start_run.mission == entry->index) {
		f->has_run = 1;
		f->run = f->start_run;
	} else {
		f->has_run = entry->catalog && kp_campaign_run_of_mission(f->campaign, entry->index, &f->run);
	}
	if (f->files) {
		if (load_original(f, entry, err, err_size) != 0) return -1;
	} else {
		if (!(mission = f->host.read(f->host.ctx, entry->kpm + 1, &mission_size))) {
			snprintf(err, err_size, "mission %s: cannot read it", entry->name);
			return -1;
		}
		if (!(f->session = (KpSession *)calloc(1, sizeof *f->session))) {
			free(mission);
			snprintf(err, err_size, "out of memory");
			return -1;
		}
		/* the game reads dat.kpd in place and takes the mission's file; its data serves the world too (the mission
		 * parsed once, its tileset read once) */
		if (kp_session_load(f->session, f->shared, f->dat, f->dat_size, mission, mission_size, entry->kpm) != 0) {
			snprintf(err, err_size, "play %s: %s", entry->name, f->session->error);
			return -1;
		}
	}
	mission_overlay(f, entry, "locale", "language", f->flags->locale, "text", kp_host_game_text);
	/* a speech's triggers time its own WAVs, the converted data's: the original data plays the scenario's (TODO: the
	 * languages' speech from the original data) */
	if (!f->files) mission_overlay(f, entry, "speech", "speech", f->flags->speech, "triggers", kp_host_game_speech);
	kp_host_tileset_path(&f->session->host, tileset, sizeof tileset);
	tileset_size = 0; /* the original data's game has its CV5 and VF4 already (kp_session_load_from) */
	tileset_bytes = f->files ? NULL : f->host.read(f->host.ctx, tileset, &tileset_size);
	kp_session_start_defaults(&st);
	st.seed = KP_APP_SEED;
	st.mission_index = entry->index;
	st.expansion = f->has_run ? f->run.expansion : 0;
	/* the player's speed starts each game (0x4bb360 passes 0x6d3130) */
	if (!keeps_originals(f)) st.speed = (int)f->profile->settings.game_speed + 1;
	if (continued) {
		st.has_carry = 1;
		st.carry = f->continue_carry;
		st.cheats = f->continue_cheats;
	}
	if (kp_session_start(f->session, tileset_bytes, tileset_size, &st) != 0) {
		if (!tileset_bytes && !f->files) snprintf(err, err_size, "play %s: cannot read %s", entry->name, tileset);
		else snprintf(err, err_size, "play %s: %s", entry->name, f->session->error);
		return -1;
	}
	layout(f); /* the scale the world's art is chosen by (a game started before the first frame has none yet) */
	if (f->host.world_build(f->host.ctx, err, err_size) != 0) return -1;
	f->world = 1;
	f->loads = f->session->loads;
	f->cam_x = f->cam_y = 0;
	if (f->load_on_start[0]) { /* a saved game: the console at once, the save loaded next */
		snprintf(f->pending_load, sizeof f->pending_load, "%s", f->load_on_start);
		f->load_on_start[0] = 0;
		if (start_hud(f, 1, err, err_size) != 0) return -1;
	} else if (f->glue_on) { /* the game waits for its ready room's Start (the console comes then) */
		kp_session_pause(f->session);
		if (kp_glue_room(&f->glue, f->session->g, f->glue_room) != 0) {
			snprintf(err, err_size, "the ready room: %s", f->ui->error);
			return -1;
		}
	} else if (start_hud(f, 0, err, err_size) != 0) {
		return -1;
	}
	f->host.stop_sounds(f->host.ctx); /* the glue's own music stops for the game's */
	return 0;
}

int kp_flow_start_mission(KpFlow *f, int index, char *err, size_t err_size)
{
	f->current = index;
	f->playing = 1;
	if (build_scene(f, err, err_size) == 0) return 0;
	/* nothing of a game that did not start stays: no session, no world, no console, no save waiting for it */
	destroy_scene(f);
	f->playing = 0;
	f->load_on_start[0] = 0;
	return -1;
}

/* --- the saves --- */

static int save_game(KpFlow *f, const char *file, char *why, size_t why_size)
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
	if (f->has_run) {
		camp.present = 1;
		camp.run = f->run;
		kp_profile_words(kp_profile_progress(f->profile), camp.progress);
	}
	/* the view (LoadGameFromSave 0x499890 reads the raw screen position back), and the interface's pending ends of the
	 * trigger waits as a save's milliseconds left */
	kp_save_pres_set(&pres, "camera_x", f->cam_x);
	kp_save_pres_set(&pres, "camera_y", f->cam_y);
	if (f->hud_on) {
		if (kp_ui_pending_releases(f->ui, &portrait_ms, &camera)) kp_save_pres_set(&pres, "portrait_ms", portrait_ms);
		if (camera) kp_save_pres_set(&pres, "camera_ms", 0);
	}
	if (kp_save_encode(f->session, f->identity, f->has_run ? &camp : NULL, &pres, &bytes, &size, &err) != 0) {
		snprintf(why, why_size, "%s", err.msg);
		return -1;
	}
	pref_path(f, file, path, sizeof path);
	k = kp_write_file_atomic(path, bytes, size, &err);
	free(bytes);
	f->host.sync_files(f->host.ctx);
	if (k != 0) snprintf(why, why_size, "%s", err.msg);
	return k;
}

static int load_game(KpFlow *f, const char *file, char *why, size_t why_size)
{
	KpSaveCampaign camp;
	KpSavePresentation pres;
	uint8_t *bytes;
	size_t size;
	char path[1100];
	KpError err;
	int k;
	pref_path(f, file, path, sizeof path);
	if (!(bytes = kp_read_file(path, &size))) {
		snprintf(why, why_size, "no save");
		return -1;
	}
	k = kp_save_load(f->session, bytes, size, f->identity, &camp, &pres, &err);
	free(bytes);
	if (k != 0) {
		snprintf(why, why_size, "%s", err.msg);
		return -1;
	}
	/* the save's run replaces the current one; its profile only raises the profile's progress */
	if (camp.present) {
		KpCampaignProfile p;
		kp_profile_from_words(&p, camp.progress);
		f->has_run = 1;
		f->run = camp.run;
		kp_profile_merge(kp_profile_progress(f->profile), &p);
		f->host.save_profile(f->host.ctx);
	}
	f->has_next = 0;
	if (f->hud_on) {
		int64_t x = kp_save_pres_get(&pres, "camera_x", -1), y = kp_save_pres_get(&pres, "camera_y", -1);
		int64_t portrait_ms = kp_save_pres_get(&pres, "portrait_ms", -1);
		kp_ui_game_loaded(f->ui);
		/* the waits the saved game's interface would have ended (the game keeps them waiting) */
		kp_ui_restore_releases(f->ui, portrait_ms >= 0, portrait_ms >= 0 ? (uint32_t)portrait_ms : 0,
			kp_save_pres_get(&pres, "camera_ms", -1) >= 0);
		/* the saved view from this frame on (a save without one keeps the view shown) */
		if (x >= 0 && y >= 0) {
			kp_hud_place_camera(&f->hud, &x, &y);
			f->cam_x = x;
			f->cam_y = y;
		}
	}
	return 0;
}

/* A saved game started from its file (the glue's Load Saved, an in-game load of another mission's save): its mission
 * without a ready room, whose game loads the save once it stands. With other_only a save of the mission playing is
 * refused (its load failed for another reason: starting the mission again would fail the same way). */
static int start_saved_game(KpFlow *f, const char *name, int other_only, char *why, size_t why_size)
{
	char file[KP_SAVE_FILE_NAME], path[1100], mission[256];
	uint8_t *bytes;
	size_t size;
	KpError err;
	int k, rc;
	save_file(f, name, file, sizeof file);
	pref_path(f, file, path, sizeof path);
	if (!(bytes = kp_read_file(path, &size))) {
		snprintf(why, why_size, "no save");
		return -1;
	}
	rc = kp_save_mission(bytes, size, f->identity, mission, sizeof mission, NULL, &err);
	free(bytes);
	if (rc != 0) {
		snprintf(why, why_size, "%s", err.msg);
		return -1;
	}
	if (other_only && f->current >= 0 && strcmp(f->missions[f->current].kpm, mission) == 0) {
		snprintf(why, why_size, "the save is this mission's");
		return -1;
	}
	for (k = 0; k < f->mission_count; k++) {
		if (strcmp(f->missions[k].kpm, mission) == 0) {
			snprintf(f->load_on_start, sizeof f->load_on_start, "%s", name);
			f->glue_room = 0;
			return kp_flow_start_mission(f, k, why, why_size);
		}
	}
	snprintf(why, why_size, "no mission %s", mission);
	return -1;
}

int kp_flow_load_saved(KpFlow *f, const char *name, char *why, size_t why_size)
{
	return start_saved_game(f, name, 0, why, why_size);
}

/* The glue state of the run's campaign menu: the expansion's or the original's. */
static int run_campaign(const KpFlow *f)
{
	return f->has_run && f->run.expansion != 0 ? KP_GLUE_EXPANSION_CAMPAIGN : KP_GLUE_CAMPAIGN;
}

static void save_requests(KpFlow *f)
{
	char why[256];
	if (f->pending_save[0]) {
		char file[KP_SAVE_FILE_NAME], name[KP_PROFILE_SAVE_NAME];
		snprintf(name, sizeof name, "%s", f->pending_save);
		f->pending_save[0] = 0;
		save_file(f, name, file, sizeof file);
		if (save_game(f, file, why, sizeof why) == 0) {
			kp_profile_add_save(f->profile, name);
			f->host.save_profile(f->host.ctx);
			kp_glue_saves(f->ui, f->profile);
			say(f, "saved %s", name);
		} else {
			say(f, "save failed: %s", why);
		}
	}
	/* a save of another mission starts its game, which asks for the save again: loaded at once too */
	while (f->pending_load[0] && f->playing && f->session && f->session->g) {
		char file[KP_SAVE_FILE_NAME], name[KP_PROFILE_SAVE_NAME];
		int menu = run_campaign(f); /* the campaign played, before another mission's start replaces its run */
		snprintf(name, sizeof name, "%s", f->pending_load);
		f->pending_load[0] = 0;
		save_file(f, name, file, sizeof file);
		if (load_game(f, file, why, sizeof why) != 0) {
			char why2[256];
			/* another mission's save: its game */
			if (start_saved_game(f, name, 1, why2, sizeof why2) == 0) continue;
			say(f, "load failed: %s", why);
			if (!f->playing) { /* the game went with the other mission's failed start: its campaign's menu */
				say(f, "load: %s", why2);
				if (f->glue_on) kp_glue_menus(&f->glue, menu);
				else f->host.quit(f->host.ctx);
			}
		} else {
			say(f, "loaded %s", name);
		}
	}
}

/* --- the campaign --- */

/* The menu a campaign's step ends in: its campaign menu, else the main menu (RunScoreScreen 0x461940 sets glue state 0
 * when AdvanceCampaignAfterGame 0x4bb790 finds no step, and RunMainLoop 0x4679b0 does after the epilog, which the glue
 * plays before it: kp_glue_epilog). */
static int step_menu(const KpCampaignStep *step)
{
	return step->kind == KP_CAMPAIGN_STEP_GLUE ? (int)step->glue : KP_GLUE_MAIN;
}

static void campaign_after_game(KpFlow *f, int result)
{
	KpCampaignStep step;
	int64_t cinematics[KP_APP_STEP_CINEMATICS];
	int count = 0, k;
	f->step_cinematic_count = 0;
	f->step_glue = KP_GLUE_MAIN; /* a game without a run has no step: not the step of the game before */
	f->step_epilog = 0;
	if (!f->has_run) return;
	kp_campaign_after_game(f->campaign, &f->run, kp_profile_progress(f->profile), result,
		(const char *)f->session->g->trig.next_scenario, &step, cinematics, &count, KP_APP_STEP_CINEMATICS);
	for (k = 0; k < count; k++) f->step_cinematics[k] = (int)cinematics[k];
	f->step_cinematic_count = count;
	f->host.save_profile(f->host.ctx);
	f->has_next = step.kind == KP_CAMPAIGN_STEP_MISSION || step.kind == KP_CAMPAIGN_STEP_RESTART;
	f->step_glue = step_menu(&step);
	f->step_epilog = step.kind == KP_CAMPAIGN_STEP_EPILOG;
	f->step_expansion = f->run.expansion;
	if (f->has_next) {
		f->next_mission = step.kind == KP_CAMPAIGN_STEP_MISSION ? step.mission : f->run.mission;
		f->next_glue = step.kind == KP_CAMPAIGN_STEP_MISSION ? (int)step.glue : 0;
		f->next_run = f->run;
		kp_carry_take(f->session->g, &f->next_carry);
		f->next_cheats = kp_cheats(f->session->g);
	}
	say(f, "campaign %s: %s", f->missions[f->current].name, f->has_next ? "the next game" : "the campaign ends");
}

static void start_glue_game(KpFlow *f, int64_t key, int state)
{
	char err[512];
	int k;
	for (k = 0; k < f->mission_count; k++) {
		if (f->missions[k].catalog && f->missions[k].index == key) {
			f->glue_room = state;
			if (kp_flow_start_mission(f, k, err, sizeof err) != 0) {
				say(f, "glue: %s", err);
				kp_glue_menus(&f->glue, KP_GLUE_MAIN);
			}
			return;
		}
	}
	say(f, "glue: no catalog mission %lld", (long long)key);
	/* as a start that fails: nothing of the game before stays (its scene, the continuation score_next made of it) */
	destroy_scene(f);
	f->playing = 0;
	f->has_continue = 0;
	f->has_start_run = 0;
	kp_glue_menus(&f->glue, KP_GLUE_MAIN);
}

static void leave_game(KpFlow *f)
{
	destroy_scene(f);
	f->playing = 0;
}

static void leave_glue_game(KpFlow *f, int state)
{
	leave_game(f);
	kp_glue_menus(&f->glue, state);
}

static void continue_with_next(KpFlow *f)
{
	f->has_continue = 1;
	f->continue_run = f->next_run;
	f->continue_carry = f->next_carry;
	f->continue_cheats = f->next_cheats;
}

/* What the score screen leads to: the next game, or the campaign's end: the epilog (mode 9) or the menu its step
 * ended in. */
static void score_next(KpFlow *f)
{
	if (f->has_next) {
		int state = f->next_glue ? f->next_glue : f->glue_room;
		continue_with_next(f);
		start_glue_game(f, f->next_mission, state);
	} else if (f->step_epilog) {
		leave_game(f);
		kp_glue_epilog(&f->glue, f->step_expansion);
	} else {
		leave_glue_game(f, f->step_glue);
	}
}

/* The cinematics (ADR 0015) a glue step plays before it goes on, and what goes on: the mission to start, the menu to
 * open, the score screen's next step, the epilog (its state the run's expansion byte). */
enum { THEN_NONE, THEN_MISSION, THEN_MENUS, THEN_NEXT, THEN_EPILOG };
/* View Intro's and the first start's: Blizzard's logo, then the Brood War intro */
static const int INTRO[2] = { KP_APP_CINE_LOGO, KP_APP_CINE_EXPANSION_INTRO };

static void cine_continue(KpFlow *f)
{
	int kind = f->cine_then.kind;
	f->cine_then.kind = THEN_NONE;
	switch (kind) {
	case THEN_MISSION:
		start_glue_game(f, f->cine_then.mission, f->cine_then.state);
		break;
	case THEN_MENUS:
		kp_glue_menus(&f->glue, f->cine_then.state);
		break;
	case THEN_NEXT:
		score_next(f);
		break;
	case THEN_EPILOG:
		kp_glue_epilog(&f->glue, f->cine_then.state);
		break;
	default:
		break;
	}
}

/* Plays ids[0..n) (each after its prelude when preludes is set), then goes on with kind: at once when nothing plays (no
 * pack, or none of them in it), as the game did before the cinematics. */
static void cine_then(KpFlow *f, const int *ids, int n, int preludes, int kind, int64_t mission, int state)
{
	f->cine_then.kind = kind;
	f->cine_then.mission = mission;
	f->cine_then.state = state;
	if (n > 0 && f->host.cine_start(f->host.ctx, ids, n, preludes)) return;
	cine_continue(f);
}

int kp_flow_cinematics(KpFlow *f, const int *ids, int n)
{
	cine_then(f, ids, n, 0, f->glue_on ? THEN_MENUS : THEN_NONE, 0, KP_GLUE_MAIN);
	return f->host.cine_active(f->host.ctx);
}

/* A mission history's cinematic row (StartCampaignEpisode 0x4bb5b0): its cinematic, then where AdvanceCampaignAfterGame
 * goes from its row: the mission after it, or for a table's last row the campaign's end (its menu, or the epilog, as
 * after a score screen). */
static void history_cinematic(KpFlow *f, const KpGlueEvent *ev)
{
	KpCampaignRun run;
	KpCampaignStep step;
	int64_t more[KP_APP_STEP_CINEMATICS];
	int ids[KP_APP_STEP_CINEMATICS + 1], n = 0, count = 0, k;
	ids[n++] = ev->cinematic;
	if (!kp_campaign_after_cinematic(f->campaign, ev->expansion, ev->mission, ev->cinematic, &run,
			kp_profile_progress(f->profile), &step, more, &count, KP_APP_STEP_CINEMATICS)) {
		say(f, "glue: no campaign row of cinematic %d and mission %lld", ev->cinematic, (long long)ev->mission);
	}
	for (k = 0; k < count; k++) ids[n++] = (int)more[k];
	f->host.save_profile(f->host.ctx);
	if (step.kind == KP_CAMPAIGN_STEP_MISSION) {
		kp_ui_menus_mission_draws(f->ui); /* CreateCampaignMissionGame's, which the glue left to the campaign */
		/* the game goes on from the row the cinematic's advanced to (the original's cached row 0x6ca718), not the
		 * mission's first row */
		f->has_start_run = 1;
		f->start_run = run;
		cine_then(f, ids, n, 1, THEN_MISSION, step.mission, (int)step.glue);
		return;
	}
	if (step.kind == KP_CAMPAIGN_STEP_EPILOG) cine_then(f, ids, n, 1, THEN_EPILOG, 0, run.expansion);
	else cine_then(f, ids, n, 1, THEN_MENUS, 0, step_menu(&step));
}

/* Whether the in-game menus' Exit Program ends the program at once: everywhere but a glue whose Exit opens the main
 * menu (exit_to_menu: the web's page goes on). No glue screen comes between: the main menu's first frame (its dialog
 * before the slide-in, an empty frame) showed for a frame before the window closed. */
static int exit_ends_program(const KpFlow *f, int exit)
{
	if (f->glue_on && f->exit_to_menu) return 0;
	return exit == EXIT_PROGRAM;
}

/* After an in-game menu ended the game: Exit Program goes to the main menu (the web's), Quit Mission to the campaign
 * menu, Restart plays the mission again from its ready room. */
static void glue_after_game(KpFlow *f)
{
	int exit = f->after_exit;
	f->after_game = 0;
	if (exit == EXIT_PROGRAM) {
		leave_glue_game(f, KP_GLUE_MAIN);
	} else if (exit == EXIT_RESTART && f->has_run) {
		f->has_continue = 1;
		f->continue_run = f->run;
		f->continue_carry = f->after_carry;
		f->continue_cheats = f->after_cheats;
		start_glue_game(f, f->run.mission, f->glue_room);
	} else {
		leave_glue_game(f, run_campaign(f));
	}
}

/* --- each frame --- */

static void game_events(KpFlow *f)
{
	KpEvent e;
	while (kp_event_take(f->session->g, &e)) {
		if (f->hud_on) kp_hud_event(&f->hud, &e);
		if (e.kind == KP_EVENT_SAVE_GAME && e.text) snprintf(f->pending_save, sizeof f->pending_save, "%s", e.text);
		else if (e.kind == KP_EVENT_LOAD_GAME && e.text) snprintf(f->pending_load, sizeof f->pending_load, "%s", e.text);
		else if (e.kind == KP_EVENT_GAME_RESULT) f->result = (int)e.a;
	}
}

static void sounds(KpFlow *f)
{
	KpUiSoundCommand c[KP_UI_SOUND_COMMANDS];
	size_t n;
	while ((n = kp_ui_take_sound_commands(f->ui, c, KP_UI_SOUND_COMMANDS)) != 0) f->host.sounds(f->host.ctx, c, n);
}

/* --- the video options (the dialog koprulu\video.bin, the profile's VOPT) --- */

/* The gamma on the screen: the game's palette and the host's HD interface art (a glue screen's palette at its upload). */
static void apply_gamma(KpFlow *f)
{
	kp_palette_set_brightness(&f->screen_palette, f->gamma);
	f->host.gamma(f->host.ctx, f->gamma);
	if (f->has_screen_palette && !kp_flow_glue_active(f)) f->host.palette(f->host.ctx, kp_palette_rgb(&f->screen_palette));
}

/* At the start: the profile's options under the flags given (the development checks keep the originals'). */
static void video_from_profile(KpFlow *f)
{
	const KpVideoSettings *v = &f->profile->video;
	KpAppFlags *fl = f->flags;
	int given = fl->video_given;
	f->video_available = f->host.video_available(f->host.ctx, f->ui);
	f->video_flags.graphics = fl->graphics;
	f->video_flags.fog = fl->fog_style;
	f->video_flags.water = fl->water;
	f->video_flags.font = fl->font_mode;
	f->video_flags.lighting = fl->lighting;
	if (dev_checks(f)) {
		f->gamma = KP_UI_GAMMA_DEFAULT;
		f->portraits = KP_UI_PORTRAITS_ANIMATED;
	} else {
		f->gamma = v->gamma;
		f->portraits = v->portraits;
		if (!(given & KP_VIDEO_GIVEN_GRAPHICS)) fl->graphics = v->graphics;
		if (!(given & KP_VIDEO_GIVEN_FOG)) fl->fog_style = v->fog;
		if (!(given & KP_VIDEO_GIVEN_WATER)) fl->water = v->water;
		if (!(given & KP_VIDEO_GIVEN_FONT)) fl->font_mode = v->font;
		if (!(given & KP_VIDEO_GIVEN_LIGHTING)) fl->lighting = v->lighting;
	}
	apply_gamma(f);
}

void kp_flow_video_to_ui(KpFlow *f, KpUi *ui)
{
	KpUiVideoOptions *o = &ui->video_options;
	ui->gamma = (uint32_t)f->gamma;
	kp_ui_portrait_set_mode(ui, (uint32_t)f->portraits);
	o->graphics = (int8_t)f->flags->graphics;
	o->fog = (int8_t)f->flags->fog_style;
	o->water = (int8_t)f->flags->water;
	o->font = (int8_t)f->flags->font_mode;
	o->lighting = (int8_t)f->flags->lighting;
	o->available = f->video_available;
	f->video_serial = o->serial;
}

/* The dialog's changes (each frame): applied at once (the world's art at its next frame, kp_world_frame). */
static void video_options(KpFlow *f)
{
	const KpUiVideoOptions *o = &f->ui->video_options;
	KpAppFlags *fl = f->flags;
	if (o->serial != f->video_serial) {
		f->video_serial = o->serial;
		fl->graphics = o->graphics;
		fl->fog_style = o->fog;
		fl->water = o->water;
		fl->font_mode = o->font;
		fl->lighting = o->lighting;
		f->portraits = (int32_t)f->ui->portrait.mode;
		if (f->gamma != (int32_t)f->ui->gamma) {
			f->gamma = (int32_t)f->ui->gamma;
			apply_gamma(f);
		}
	}
}

/* The video options as applied into the profile. */
static void keep_video(KpFlow *f)
{
	const KpAppFlags *fl = f->flags;
	KpVideoSettings run;
	run.gamma = f->gamma;
	run.portraits = f->portraits;
	run.graphics = fl->graphics;
	run.fog = fl->fog_style;
	run.water = fl->water;
	run.font = fl->font_mode;
	run.lighting = fl->lighting;
	kp_profile_commit_video(&f->profile->video, &run, &f->video_flags, fl->video_given);
}

/* The given check boxes, where the player changed them, into the profile: 1 when it changed (the caller writes it). A
 * bit a flag gave keeps the profile's until the player changes it. */
static int keep_options(KpFlow *f, uint32_t boxes)
{
	uint32_t changed = (f->ui->options ^ f->options_seen) & boxes;
	if (!changed) return 0;
	f->options_seen ^= changed;
	if (keeps_originals(f)) return 0;
	f->profile->options = (f->profile->options & ~changed) | (f->ui->options & changed);
	return 1;
}

static int keep_setting(uint32_t *kept, uint32_t value)
{
	if (*kept == value) return 0;
	*kept = value;
	return 1;
}

/* The given dialogs' options into the profile: 1 when it changed (the caller writes it). A section takes its own fields
 * alone: Ctrl+M's run-long silence stays out of it until the sound dialog's own OK. */
static int keep_settings(KpFlow *f, uint32_t sections)
{
	KpGameSettings *s = &f->profile->settings;
	const KpUi *ui = f->ui;
	int changed = 0;
	if (keeps_originals(f)) return 0;
	if (sections & KP_UI_SECTION_SOUND) {
		changed |= keep_setting(&s->music, ui->sound.music);
		changed |= keep_setting(&s->effects, ui->sound.effects);
	}
	if (sections & KP_UI_SECTION_SPEED) {
		changed |= keep_setting(&s->mouse_scroll, ui->mouse_scroll_speed);
		changed |= keep_setting(&s->key_scroll, ui->key_scroll_speed);
		changed |= keep_setting(&s->cpu_throttle, ui->cpu_throttle);
		if (ui->speed_chosen != 0) changed |= keep_setting(&s->game_speed, ui->speed_chosen - 1);
	}
	if (sections & KP_UI_SECTION_TIPS) changed |= keep_setting(&s->tip, ui->tip_index);
	return changed;
}

/* The sections whose options dialog got its OK into the profile, after video_options applied them: 1 when it changed
 * (the caller writes it, once a frame). Nothing else reaches it from a dialog: a change under an open dialog, a Cancel
 * or a dialog closed with no answer (put back by the interface) write nothing. */
static int committed_options(KpFlow *f)
{
	uint32_t sections = f->ui->options_committed;
	int changed = keep_settings(f, sections);
	f->ui->options_committed = 0;
	if (sections & (KP_UI_SECTION_SOUND | KP_UI_SECTION_TIPS)) changed |= keep_options(f, KP_UI_OPTIONS_KEPT);
	if (sections & KP_UI_SECTION_VIDEO) {
		f->video_commits++;
		if (!dev_checks(f)) { /* the checks' options are not the player's */
			keep_video(f);
			changed = 1;
		}
	}
	return changed;
}

/* The interface's cursor over the canvas (show 0: none). */
static void cursor(KpFlow *f, int show) { f->host.cursor(f->host.ctx, show ? f->ui : NULL); }

/* A glue screen's palette with the gamma (as kp_palette_rgb gives the game's). */
static void glue_palette(KpFlow *f)
{
	uint8_t table[256], rgb[768];
	int k;
	kp_palette_gamma_table(f->gamma, table);
	for (k = 0; k < 768; k++) rgb[k] = table[f->glue_rgb[k]];
	f->host.palette(f->host.ctx, rgb);
}

static void glue_frame(KpFlow *f)
{
	KpGlueEvent ev;
	int changed, palette;
	f->host.canvas_begin(f->host.ctx, &f->glue.canvas);
	ev = kp_glue_frame(&f->glue, f->world_w, f->world_h, now_ms(f), &changed, f->glue_rgb, &palette);
	if (palette) glue_palette(f);
	f->host.canvas(f->host.ctx, &f->glue.canvas);
	sounds(f);
	video_options(f);
	if (committed_options(f) | ev.profile_changed) f->host.save_profile(f->host.ctx); /* once a frame */
	switch (ev.kind) {
	case KP_GLUE_EXIT:
		say(f, "glue: exit");
		if (!f->exit_to_menu) f->host.quit(f->host.ctx);
		kp_glue_menus(&f->glue, KP_GLUE_MAIN);
		break;
	case KP_GLUE_MISSION:
		if (ev.cinematic != 0) history_cinematic(f, &ev);
		else start_glue_game(f, ev.mission, ev.state);
		break;
	case KP_GLUE_INTRO: /* View Intro (mode 0) */
		cine_then(f, INTRO, 2, 0, THEN_MENUS, 0, KP_GLUE_MAIN);
		break;
	case KP_GLUE_LANGUAGE: /* the frame ends with this interface, the next starts over (start_over) */
		f->language_chosen = 1;
		break;
	case KP_GLUE_LOAD: {
		char why[256];
		if (ev.save < 0 || ev.save >= f->ui->menus.save_count
			|| kp_flow_load_saved(f, f->ui->menus.saves[ev.save], why, sizeof why) != 0) {
			/* the load screen again, as the original's state stays when LoadSavedGameByName 0x498bd0 fails
			 * (RunLoadGameScreen 0x459970); its Back keeps the campaign it came from */
			say(f, "glue: load failed");
			kp_glue_menus(&f->glue, KP_GLUE_LOAD_SCREEN);
		}
		break;
	}
	case KP_GLUE_ROOM_START: { /* the game with the same interface's console */
		char err[256];
		if (start_hud(f, 0, err, sizeof err) != 0) {
			say(f, "%s", err);
			leave_glue_game(f, run_campaign(f));
			break;
		}
		kp_session_resume(f->session);
		break;
	}
	case KP_GLUE_ROOM_CANCEL:
		leave_glue_game(f, run_campaign(f));
		break;
	case KP_GLUE_SCORE_DONE: { /* the cinematic rows between the missions play before the next step (mode 10) */
		int n = f->step_cinematic_count;
		f->step_cinematic_count = 0; /* the host's cinematics copy the ids into their queue */
		cine_then(f, f->step_cinematics, n, 1, THEN_NEXT, 0, 0);
		break;
	}
	default:
		break;
	}
}

/* The console's frame and what it ended with. */
static void hud_frame(KpFlow *f, int ticks)
{
	KpUi *ui = f->ui;
	int moved, released, pointer;
	int32_t px, py;
	int exit;
	f->host.canvas_begin(f->host.ctx, f->hud.canvas);
	pointer = f->host.pointer(f->host.ctx, &px, &py);
	kp_hud_frame(&f->hud, f->world_w, f->world_h, now_ms(f), ticks, pointer, px, py, &f->cam_x, &f->cam_y, &moved,
		&released);
	f->host.canvas(f->host.ctx, f->hud.canvas);
	cursor(f, 1);
	if (moved) kp_flow_clamp_camera(f);
	if (released) kp_session_release_wait(f->session);
	kp_hud_take_commands(ui, f->session);
	sounds(f);
	video_options(f);
	if (committed_options(f)) f->host.save_profile(f->host.ctx);
	exit = ui->game_exit;
	if (exit == EXIT_PROGRAM || exit == EXIT_QUIT_MISSION || exit == EXIT_RESTART) {
		/* the in-game menus' Exit Program, Quit Mission, Restart (no result) */
		kp_ui_end_game(ui);
		sounds(f);
		say(f, "play %s: exit %d", f->missions[f->current].name, exit);
		kp_session_pause(f->session);
		end_hud(f);
		if (exit_ends_program(f, exit)) {
			f->host.quit(f->host.ctx);
		} else if (f->glue_on) {
			f->after_game = 1;
			f->after_exit = exit;
			kp_carry_take(f->session->g, &f->after_carry);
			f->after_cheats = kp_cheats(f->session->g);
		}
	} else if (exit != 0 && f->result) {
		/* the result dialog's answer (RequestGameExit 0x444e00: 6 victory, 7 defeat) ends the game */
		int result = f->result;
		kp_ui_end_game(ui);
		sounds(f);
		campaign_after_game(f, result);
		kp_session_pause(f->session);
		end_hud(f);
		if (f->glue_on) kp_glue_score(&f->glue, f->session->g, result, (uint32_t)(now(f) - f->game_started_at));
		f->result = 0;
	}
}

/* The game's part of a frame, under its failure handler. */
static void game_frame(KpFlow *f)
{
	KpSession *s = f->session;
	KpTry t;
	volatile int ticks = 0, restart = 0;
	double fraction = 1;
	if (!s || !s->g) return;
	kp_images_begin(s->g->w, &t);
	if (setjmp(t.buf)) {
		kp_images_end(s->g->w, &t);
		s->failed = 1;
		snprintf(s->error, sizeof s->error, "%s", s->g->w->error);
		stop(f, s->error);
		return;
	}
	if (!kp_flow_glue_active(f) && !s->failed && !f->flags->autotest) { /* the autotest steps the game itself */
		ticks = kp_session_advance(s, now(f), &fraction, f->host.clock_ms);
		/* a loaded save: its state at once, without motion from the state before */
		if (s->loads != f->loads) {
			f->loads = s->loads;
			restart = 1;
			if (ticks < 1) ticks = 1;
		}
	}
	/* each draw refreshes the remembered terrain in view (RefreshRememberedTerrainOnscreen 0x40b220) */
	{
		int64_t x0 = f->cam_x / KP_WORLD_TILE, y0 = f->cam_y / KP_WORLD_TILE;
		kp_refresh_terrain_onscreen(s->g, x0, y0, (f->cam_x + f->world_w + KP_WORLD_TILE - 1) / KP_WORLD_TILE - x0,
			(f->cam_y + f->world_h + KP_WORLD_TILE - 1) / KP_WORLD_TILE - y0);
	}
	game_events(f);
	/* a game's draw refreshes the bounds of its images, as the original's redraw does (RefreshAllSpriteBounds
	 * 0x415700): the steps move the sprites, not their bounds, and the draw list reads the bounds */
	if (ticks > 0) kp_images_refresh_bounds(s->g->w);
	f->host.world_frame(f->host.ctx, ticks, fraction, restart);
	if (f->hud_on) hud_frame(f, ticks);
	kp_images_end(s->g->w, &t);
}

/* A code point the text could not draw: one line each (ADR 0016). */
static void tell_missing_glyphs(KpFlow *f)
{
	int32_t cp;
	if (f->ui == NULL) return;
	while ((cp = kp_ui_text_take_missing(&f->ui->text)) != 0) say(f, "text: no glyph for U+%04X", (unsigned)cp);
}

static void start_over(KpFlow *f);

void kp_flow_frame(KpFlow *f)
{
	tell_missing_glyphs(f);
	if (f->cine_then.kind != THEN_NONE && !f->host.cine_active(f->host.ctx)) cine_continue(f); /* the cinematics ended */
	if (f->after_game) glue_after_game(f);
	f->fixed_screen = kp_flow_glue_active(f); /* a glue screen keeps the original's 640 x 480 */
	layout(f);
	if (f->fixed_screen) {
		glue_frame(f);
		cursor(f, 1);
		if (!kp_flow_glue_active(f)) { /* a game took over: its layout from its first frame */
			f->fixed_screen = 0;
			layout(f);
		}
	} else if (!(f->playing && f->hud_on)) {
		cursor(f, 0);
	}
	/* the saves the game asked for, and a save the glue's load started a game for: loaded before its first frame
	 * draws, which shows the saved state, not the mission's start */
	if (f->playing && f->session && f->session->g) save_requests(f);
	kp_flow_clamp_camera(f);
	if (f->playing && f->world) game_frame(f);
	if (!f->hud_on && !kp_flow_glue_active(f)) f->host.canvas(f->host.ctx, NULL);
	/* colour cycles run on real time; not while a glue screen's palette shows */
	if (f->has_screen_palette && !kp_flow_glue_active(f)) {
		double t = now(f);
		if (kp_palette_advance(&f->screen_palette, (t - f->palette_clock) * 1000, 0)) {
			f->host.palette(f->host.ctx, kp_palette_rgb(&f->screen_palette));
		}
		f->palette_clock = t;
	}
	if (f->language_chosen) start_over(f);
}

/* --- the languages (ADR 0016) --- */

#define LOCALE_GLYPHS 0  /* f->locale_files: generated/locale/glyphs.kpd, <loc>/strings.kpd, <loc>/ui.kpd */
#define LOCALE_STRINGS 1
#define LOCALE_UI 2
#define LOCALE_SPEECH 3  /* generated/speech/<loc>/sounds.kpd: the speech's durations */

static void free_locale(KpFlow *f)
{
	int k;
	for (k = 0; k < KP_FLOW_LOCALE_FILES; k++) {
		free(f->locale_files[k]);
		f->locale_files[k] = NULL;
	}
	memset(&f->locale, 0, sizeof f->locale);
}

/* generated/locale/glyphs.kpd's LOCS (u8 count; per language u8 length + ID, u8 length + its own name) as the languages,
 * each installed for the text when it is the base or has its strings, for the speech when it is the base or has its
 * voices. */
static void list_languages(KpFlow *f)
{
	KpUiLanguages *l = &f->languages;
	KpKpf c;
	KpError e;
	const KpKpfSection *s;
	memset(l, 0, sizeof *l);
	l->text = l->speech = l->chosen_text = l->chosen_speech = -1;
	if (f->locale.glyphs == NULL || kp_kpf_parse(&c, f->locale.glyphs, f->locale.glyphs_size, "glyphs.kpd", 0, &e) != 0) {
		return;
	}
	if ((s = kp_kpf_find(&c, "LOCS")) != NULL && s->size >= 1) {
		size_t at = 1;
		int k;
		for (k = 0; k < s->data[0] && l->count < KP_UI_LANGUAGES; k++) {
			char path[96];
			size_t id_n, name_n;
			char *id = l->list[l->count].id, *name = l->list[l->count].name;
			if (at + 1 > s->size || at + 1 + (id_n = s->data[at]) + 1 > s->size
				|| at + 2 + id_n + (name_n = s->data[at + 1 + id_n]) > s->size) {
				say(f, "language: generated/locale/glyphs.kpd: LOCS is damaged");
				break;
			}
			if (id_n == 0 || id_n >= KP_UI_LANGUAGE_ID || name_n >= KP_UI_LANGUAGE_NAME) {
				at += 2 + id_n + name_n;
				continue;
			}
			memcpy(id, s->data + at + 1, id_n);
			id[id_n] = 0;
			memcpy(name, s->data + at + 2 + id_n, name_n);
			name[name_n] = 0;
			at += 2 + id_n + name_n;
			snprintf(path, sizeof path, "generated/locale/%s/strings.kpd", id);
			l->list[l->count].text = strcmp(id, KP_LOCALE_BASE) == 0 || f->host.exists(f->host.ctx, path);
			snprintf(path, sizeof path, "generated/speech/%s/units.kpd", id);
			l->list[l->count].speech = strcmp(id, KP_LOCALE_BASE) == 0 || f->host.exists(f->host.ctx, path);
			l->count++;
		}
	}
	kp_kpf_free(&c);
}

static int language_at(const KpUiLanguages *l, const char *id)
{
	int k;
	for (k = 0; k < l->count; k++) {
		if (strcmp(l->list[k].id, id) == 0) return k;
	}
	return -1;
}

int kp_flow_match_language(const KpUiLanguages *l, const char *wanted)
{
	char lang[4] = "", country[4] = "";
	size_t n = strlen(wanted);
	int k;
	if (n < 2) return -1;
	memcpy(lang, wanted, 2);
	if (n >= 4) { /* "ruRU", "ru_RU", "ru-RU" */
		const char *c = wanted[2] == '_' || wanted[2] == '-' ? wanted + 3 : wanted + 2;
		if (strlen(c) >= 2) memcpy(country, c, 2);
	}
	for (k = 0; k < 2; k++) {
		lang[k] = (char)tolower((unsigned char)lang[k]);
		country[k] = (char)toupper((unsigned char)country[k]);
	}
	for (k = 0; country[0] && k < l->count; k++) {
		if (l->list[k].text && strncmp(l->list[k].id, lang, 2) == 0 && strcmp(l->list[k].id + 2, country) == 0) return k;
	}
	for (k = 0; k < l->count; k++) {
		if (l->list[k].text && strncmp(l->list[k].id, lang, 2) == 0) return k;
	}
	return -1;
}

/* The language of the start: --locale's; for the glue (not the checks', --mission's, the screenshots' or the benches':
 * theirs is the base's with its original fonts) the profile's when installed, else the system's first installed, else
 * the base's. None without the glyphs (a conversion of the 1.17 data alone). */
static void choose_language(KpFlow *f)
{
	const KpUiLanguages *l = &f->languages;
	const char *p = f->flags->system_locales;
	int at = language_at(l, f->profile->text_locale);
	if (f->flags->locale[0] != 0 || keeps_originals(f) || f->flags->mission[0] != 0) return;
	if (at < 0 || !l->list[at].text) at = -1;
	while (at < 0 && *p) { /* "ru_RU,en_US" */
		char one[16];
		size_t n = strcspn(p, ",");
		snprintf(one, sizeof one, "%.*s", (int)(n < sizeof one - 1 ? n : sizeof one - 1), p);
		at = kp_flow_match_language(l, one);
		p += n + (p[n] == ',');
	}
	snprintf(f->flags->locale, sizeof f->flags->locale, "%s", at >= 0 ? l->list[at].id : KP_LOCALE_BASE);
}

/* The speech of the start: --speech's; for the glue the profile's when installed, else the text's language's when it has
 * speech, else the base's. The checks and --mission keep the base's, as for the text. */
static void choose_speech(KpFlow *f)
{
	const KpUiLanguages *l = &f->languages;
	int at = language_at(l, f->profile->speech_locale);
	if (f->flags->speech[0] != 0 || keeps_originals(f) || f->flags->mission[0] != 0) return;
	if (at < 0 || !l->list[at].speech) at = language_at(l, f->flags->locale);
	if (at >= 0 && !l->list[at].speech) at = -1;
	snprintf(f->flags->speech, sizeof f->flags->speech, "%s", at >= 0 ? l->list[at].id : KP_LOCALE_BASE);
}

/* The speech's sound records (its voices' durations) for one that is not the base's; none leaves the base's, with one
 * line. The speech needs no glyphs: --speech alone reads it. */
static void read_speech(KpFlow *f)
{
	char path[128];
	const char *speech = f->flags->speech;
	if (speech[0] == 0 || strcmp(speech, KP_LOCALE_BASE) == 0) return;
	snprintf(path, sizeof path, "generated/speech/%s/sounds.kpd", speech);
	f->locale.speech = f->locale_files[LOCALE_SPEECH] = f->host.read(f->host.ctx, path, &f->locale.speech_size);
	if (f->locale.speech == NULL) say(f, "speech %s: no %s", speech, path);
}

/* The language's overlays (ADR 0016): the glyphs for any language, the text for one that is not the base's, the speech's
 * (read_speech); one that is missing leaves the base's, with one line. The glue's start chooses the language once it has the glyphs' list. */
static void read_locale(KpFlow *f)
{
	static const char *const NAMES[KP_FLOW_LOCALE_FILES] = { NULL, "strings.kpd", "ui.kpd" };
	const char *locale = f->flags->locale;
	int k, glue = locale[0] == 0 && !keeps_originals(f) && f->flags->mission[0] == 0;
	free_locale(f);
	memset(&f->languages, 0, sizeof f->languages);
	if (locale[0] != 0 || glue) {
		f->locale_files[LOCALE_GLYPHS] = f->host.read(f->host.ctx, "generated/locale/glyphs.kpd", &f->locale.glyphs_size);
		f->locale.glyphs = f->locale_files[LOCALE_GLYPHS];
		if (f->locale.glyphs == NULL) {
			if (!glue) say(f, "language %s: no generated/locale/glyphs.kpd", locale);
		} else {
			list_languages(f);
			choose_language(f);
			choose_speech(f);
		}
	}
	read_speech(f);
	if (f->locale.glyphs == NULL || strcmp(locale, KP_LOCALE_BASE) == 0) return;
	for (k = LOCALE_STRINGS; k < LOCALE_SPEECH; k++) {
		char path[128];
		size_t size = 0;
		snprintf(path, sizeof path, "generated/locale/%s/%s", locale, NAMES[k]);
		if (!(f->locale_files[k] = f->host.read(f->host.ctx, path, &size))) say(f, "language %s: no %s", locale, path);
		if (k == LOCALE_STRINGS) {
			f->locale.strings = f->locale_files[k];
			f->locale.strings_size = size;
		} else {
			f->locale.ui = f->locale_files[k];
			f->locale.ui_size = size;
		}
	}
}

/* The main menu's languages and the current ones to the new interface. */
static void languages_to_ui(KpFlow *f, KpUi *ui)
{
	KpUiLanguages *l = &ui->menus.languages;
	*l = f->languages;
	l->text = language_at(l, f->flags->locale[0] ? f->flags->locale : KP_LOCALE_BASE);
	l->speech = language_at(l, f->flags->speech[0] ? f->flags->speech : KP_LOCALE_BASE);
	l->chosen_text = l->text;
	l->chosen_speech = l->speech;
}

/* The main menu's languages chose others: kept in the profile, the flow starts over in them (the interface, its
 * overlays, the glue's main menu) without leaving the program. */
static void start_over(KpFlow *f)
{
	const KpUiLanguages *l = &f->ui->menus.languages;
	char err[256];
	f->language_chosen = 0;
	if (l->chosen_text >= 0) snprintf(f->profile->text_locale, sizeof f->profile->text_locale, "%s", l->list[l->chosen_text].id);
	if (l->chosen_speech >= 0) {
		snprintf(f->profile->speech_locale, sizeof f->profile->speech_locale, "%s", l->list[l->chosen_speech].id);
	}
	f->host.save_profile(f->host.ctx);
	snprintf(f->flags->locale, sizeof f->flags->locale, "%s", f->profile->text_locale);
	snprintf(f->flags->speech, sizeof f->flags->speech, "%s", f->profile->speech_locale);
	say(f, "glue: the language %s, the speech %s", f->profile->text_locale, f->profile->speech_locale);
	kp_flow_free(f);
	f->host.language(f->host.ctx);
	if (kp_flow_init(f, err, sizeof err) != 0) {
		say(f, "glue: %s", err);
		f->host.quit(f->host.ctx);
	}
}

/* The first start (RunMainLoop 0x4679b0: the options' bit 0x800 cleared, then cinematics 0 and 24 before the glue): the
 * logo and the intro once the pack has them, the profile written at once (a start without the pack keeps them for one
 * with it); not for the checks and --cine-play's movies. 1 when the flow goes on from them (the main menu after). */
static int first_start(KpFlow *f)
{
	uint32_t *options = &f->profile->options;
	if (dev_checks(f) || f->flags->cine_play[0] || !(*options & KP_UI_OPTION_EXPANSION_INTRO) || !f->glue.cinematics) {
		return 0;
	}
	*options &= ~KP_UI_OPTION_EXPANSION_INTRO;
	f->host.save_profile(f->host.ctx);
	say(f, "glue: the first start's intro");
	cine_then(f, INTRO, 2, 0, THEN_MENUS, 0, KP_GLUE_MAIN);
	return 1;
}

void kp_flow_toggle_subtitles(KpFlow *f)
{
	if (!f->ui) return;
	f->ui->options ^= KP_UI_OPTION_SUBTITLES;
	if (keep_options(f, KP_UI_OPTION_SUBTITLES)) f->host.save_profile(f->host.ctx); /* no dialog: at once, its box alone */
	say(f, "flow: the subtitles %s", kp_flow_subtitles(f) ? "shown" : "hidden");
}

int kp_flow_subtitles(const KpFlow *f)
{
	return f->ui != NULL && (f->ui->options & KP_UI_OPTION_SUBTITLES) != 0;
}

int kp_flow_init(KpFlow *f, char *err, size_t err_size)
{
	char why[256];
	f->current = -1;
	read_locale(f);
	if (!(f->ui = new_interface(f, why, sizeof why))) {
		snprintf(err, err_size, "%s", why);
		return -1;
	}
	if (f->ui->locale_problem[0]) say(f, "language %s: %s; that part stays the base's", f->flags->locale, f->ui->locale_problem);
	if (f->ui->speech_problem[0]) {
		say(f, "speech %s: %s; its voices' durations stay the base's", f->flags->speech, f->ui->speech_problem);
	}
	languages_to_ui(f, f->ui);
	console_art(f, f->ui);
	f->host.interface(f->host.ctx, f->ui);
	interface_host(f, f->ui);
	video_from_profile(f);
	kp_flow_video_to_ui(f, f->ui);
	/* the check boxes: the profile's or the originals', --subtitles over them */
	f->options_seen = (keeps_originals(f) ? f->ui->options : f->profile->options) & KP_UI_OPTIONS_KEPT;
	if (strcmp(f->flags->subtitles, "on") == 0) f->options_seen |= KP_UI_OPTION_SUBTITLES;
	else if (strcmp(f->flags->subtitles, "off") == 0) f->options_seen &= ~KP_UI_OPTION_SUBTITLES;
	else if (f->flags->subtitles[0]) say(f, "flow: --subtitles %s is not on or off", f->flags->subtitles);
	options_to_ui(f, f->ui);
	/* an older profile's saves' files get their names of now, once: before schema 3 their IDs', before 7 the UTF-8 name's */
	if (kp_profile_rename_old_saves(f->profile, rename_old_save, f)) f->host.save_profile(f->host.ctx);
	kp_glue_saves(f->ui, f->profile);
	if (f->flags->mission[0]) {
		int k;
		for (k = 0; k < f->mission_count; k++) {
			if (strcmp(f->missions[k].name, f->flags->mission) == 0) {
				if (!f->flags->play) f->flags->play = 1;
				return kp_flow_start_mission(f, k, err, err_size);
			}
		}
		snprintf(err, err_size, "no mission %s", f->flags->mission);
		return -1;
	}
	if (f->flags->autotest) return 0; /* the autotest starts its games itself */
	kp_glue_init(&f->glue, f->ui, f->profile, now_ms(f));
	f->glue_on = 1;
	/* View Intro plays the logo and the intro the pack has */
	f->glue.cinematics = f->host.cine_has(f->host.ctx, KP_APP_CINE_LOGO)
		|| f->host.cine_has(f->host.ctx, KP_APP_CINE_EXPANSION_INTRO);
	if (first_start(f)) return 0;
	kp_glue_menus(&f->glue, KP_GLUE_MAIN);
	say(f, "glue: the main menu");
	return 0;
}

/* --- the missions --- */

static void add_mission(KpAppMission *out, int *count, int cap, const char *name, const char *base, int catalog)
{
	KpAppMission *m;
	int k;
	if (*count >= cap) return;
	m = &out[(*count)++];
	memset(m, 0, sizeof *m);
	m->catalog = catalog;
	if (catalog) {
		snprintf(m->name, sizeof m->name, "%s", name);
		/* the saves' mission name, whichever data the mission is read from */
		snprintf(m->kpm, sizeof m->kpm, "/generated/missions/%s.kpm", name);
		m->index = *count; /* its number among the catalog's missions (the line of generated/missions/index.txt) */
	} else {
		snprintf(m->name, sizeof m->name, "scenarios/%s", name);
		snprintf(m->kpm, sizeof m->kpm, "/generated/scenarios/%s.kpm", name);
		for (k = 0; k < *count - 1; k++) {
			if (out[k].catalog && strcmp(out[k].name, base) == 0) m->index = out[k].index;
		}
	}
}

/* An index's lines: the catalog's "name tileset width height", the scenarios' "name base tileset width height". */
static void add_missions(KpAppMission *out, int *count, int cap, const char *text, size_t size, int catalog)
{
	size_t at = 0;
	while (text && at < size) {
		char line[256], name[64], base[64];
		size_t end = at, n;
		int tileset, w, h;
		while (end < size && text[end] != '\n') end++;
		n = end - at < sizeof line - 1 ? end - at : sizeof line - 1;
		memcpy(line, text + at, n);
		line[n] = '\0';
		at = end + 1;
		if (catalog) {
			if (sscanf(line, "%63s %d %d %d", name, &tileset, &w, &h) == 4) add_mission(out, count, cap, name, NULL, 1);
		} else if (sscanf(line, "%63s %63s %d %d %d", name, base, &tileset, &w, &h) == 5) {
			add_mission(out, count, cap, name, base, 0);
		}
	}
}

int kp_flow_missions(KpAppMission *out, int cap, const char *catalog, size_t catalog_size, const char *scenarios,
	size_t scenarios_size)
{
	int count = 0;
	add_missions(out, &count, cap, catalog, catalog_size, 1);
	add_missions(out, &count, cap, scenarios, scenarios_size, 0);
	return count;
}

int kp_flow_missions_campaign(KpAppMission *out, int cap, const KpCampaign *campaign, const char *scenarios,
	size_t scenarios_size)
{
	int count = 0, index;
	char name[64];
	/* the mapdata indices from 1 are the catalog's lines (index 0 is no mission) */
	for (index = 1; index < campaign->directory_count && count < cap; index++) {
		if (!kp_campaign_mission_name(campaign, index, name, sizeof name)) break;
		add_mission(out, &count, cap, name, NULL, 1);
		snprintf(out[count - 1].directory, sizeof out[count - 1].directory, "%s", campaign->directories[index]);
	}
	add_missions(out, &count, cap, scenarios, scenarios_size, 0);
	return count;
}

void kp_flow_free(KpFlow *f)
{
	destroy_scene(f);
	if (f->glue_on) kp_glue_free(&f->glue);
	f->glue_on = 0;
	if (f->ui) kp_ui_free(f->ui);
	f->ui = NULL;
	free_locale(f);
}
