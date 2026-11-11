/* The original data of the two installs (ADR 0018): StarCraft: Remastered's interface, rebuilt from its own formats
 * (stage D6), has the 1.17 interface's parts, and each install's trigger WAVs are the files the scenario names. The
 * installs of KP_TEST_ORIGINAL (1.17) and KP_TEST_SCR (StarCraft: Remastered); skipped without them. */
#include "kp_test_app.h"

#include <stdlib.h>
#include <string.h>

#include "kp_files.h"
#include "kp_hud.h"
#include "kp_ui_data.h"
#include "../../native/kp_core/src/core/kp_dat.h"
#include "../../native/kp_core/src/core/kp_mission.h"

static int bytes_eq(const uint8_t *a, size_t an, const uint8_t *b, size_t bn)
{
	return an == bn && (an == 0 || memcmp(a, b, an) == 0);
}

static void *zalloc(KpT *T, size_t n)
{
	void *p = calloc(1, n);
	kp_t_ok(T, p != NULL, "out of memory");
	kp_t_defer(T, free, p);
	return p;
}

static void dat_free(void *p) { kp_dat_free((KpDat *)p); }
static void ui_free(void *p) { kp_ui_data_free((KpUiData *)p); }
static void interface_free(void *p) { kp_ui_free((KpUi *)p); }

/* The interface over an install's files (kp_interface_new_data), released at the end. */
static KpUi *interface_of(KpT *T, const char *env)
{
	KpUiData *d = (KpUiData *)zalloc(T, sizeof *d);
	KpDat *dat = (KpDat *)zalloc(T, sizeof *dat);
	KpFiles f;
	KpError err;
	KpUi *ui;
	char why[256];
	kp_res_files(kp_t_install(T, env), &f);
	kp_t_ok(T, kp_dat_from_files(dat, &f, &err) == 0, "%s", err.msg);
	kp_t_defer(T, dat_free, dat);
	kp_t_ok(T, kp_ui_data_from_files(d, &f, dat, NULL, 0, &err) == 0, "%s", err.msg);
	kp_t_defer(T, ui_free, d);
	ui = kp_interface_new_data(d, NULL, why, sizeof why);
	kp_t_ok(T, ui != NULL, "%s's interface: %s", env, why);
	kp_t_defer(T, interface_free, ui);
	return ui;
}

static int ui_paths_eq(const KpUiFile *x, int xn, const KpUiFile *y, int yn)
{
	int k;
	if (xn != yn) return 0;
	for (k = 0; k < xn; k++)
		if (!bytes_eq((const uint8_t *)x[k].path, x[k].path_len, (const uint8_t *)y[k].path, y[k].path_len)) return 0;
	return 1;
}

/* The interface over StarCraft: Remastered's files (stage D6: the dialogs' BINs and the TBLs rebuilt from its XML, the
 * PCXs and GRPs from its DDS, the portraits' SMKs as KPW1 over its WebM) has the 1.17 interface's parts: the same
 * dialogs with the same records' counts, the same files by name, the fonts, the sounds' records and the portraits'
 * rows equal (Remastered keeps those exactly), each portrait movie a KPW1. Its pictures and some of its texts are the
 * rebuilds' approximations (`kp_data_probe --digests` checks those against the converter's byte for byte). */
static void remastered_interface(KpT *T)
{
	KpUi *a, *b;
	int k;
	a = interface_of(T, "KP_TEST_ORIGINAL");
	b = interface_of(T, "KP_TEST_SCR");
	kp_t_eq(T, b->bin_count, a->bin_count, "dialogs");
	for (k = 0; k < a->bin_count; k++) {
		const KpUiBin *x = &a->bins[k], *y = &b->bins[k];
		kp_t_ok(T, bytes_eq((const uint8_t *)x->name, x->name_len, (const uint8_t *)y->name, y->name_len)
			&& x->widget_count == y->widget_count && x->smk_count == y->smk_count, "dialog %d's records", k);
	}
	kp_t_ok(T, ui_paths_eq(a->grps, a->grp_count, b->grps, b->grp_count), "the dialogs' GRPs");
	kp_t_ok(T, ui_paths_eq(a->pcx, a->pcx_count, b->pcx, b->pcx_count), "the PCXs");
	kp_t_ok(T, ui_paths_eq(a->glue_files, a->glue_file_count, b->glue_files, b->glue_file_count), "the glue's files");
	kp_t_ok(T, ui_paths_eq(a->ghosts, a->ghost_count, b->ghosts, b->ghost_count), "the ghosts");
	for (k = 0; k < KP_UI_FNT_FONTS; k++) {
		kp_t_ok(T, bytes_eq(a->font_files[k], a->font_file_sizes[k], b->font_files[k], b->font_file_sizes[k]), "font %d", k);
	}
	kp_t_ok(T, a->sound_count == b->sound_count
		&& memcmp(a->sounds, b->sounds, (size_t)a->sound_count * KP_UI_SOUND_RECORD) == 0, "the sounds' records");
	kp_t_ok(T, a->portrait_row_count == b->portrait_row_count && a->portrait_row_count > 0
		&& memcmp(a->portrait_rows, b->portrait_rows, (size_t)a->portrait_row_count * 8) == 0, "the portraits' rows");
	kp_t_ok(T, ui_paths_eq(a->portrait_files, a->portrait_file_count, b->portrait_files, b->portrait_file_count),
		"the portraits' movies by name");
	for (k = 0; k < b->portrait_file_count; k++)
		kp_t_ok(T, b->portrait_files[k].size > 4 && memcmp(b->portrait_files[k].data, "KPW1", 4) == 0,
			"portrait movie %d is a KPW1", k);
	kp_t_ok(T, a->status_layout_count == b->status_layout_count && a->button_set_count == b->button_set_count
		&& bytes_eq(a->cheats, a->cheats_size, b->cheats, b->cheats_size)
		&& bytes_eq(a->establishing, a->establishing_size, b->establishing, b->establishing_size),
		"the executable's tables");
}

/* Each trigger WAV a mission finds (WAVS: an index) is the file its string names in the scenario's directory, which
 * the data has; the ones it does not find name a file the data lacks. */
static void mission_sounds_found(KpT *T, const char *env, const char *name, const char *directory)
{
	char path[260];
	KpFiles f;
	KpError err;
	KpMission m;
	KpRes *res;
	int k, found = 0;
	res = kp_t_install(T, env);
	kp_res_files(res, &f);
	memset(&m, 0, sizeof m);
	kp_t_ok(T, kp_mission_from_files(&m, &f, directory, &err) == 0, "%s", err.msg);
	for (k = 0; k < m.sound_count; k++) {
		const KpMissionSound *s = &m.sounds[k];
		int has = kp_mission_sound_path(&m, directory, s->string_id, path, sizeof path) == 1;
		kp_t_ok(T, has || s->index == KP_MISSION_NO_SOUND, "WAV string %u names no file", (unsigned)s->string_id);
		if (has) kp_t_eq(T, kp_res_exists(res, path), s->index != KP_MISSION_NO_SOUND, "%s", path);
		found += s->index != KP_MISSION_NO_SOUND;
	}
	kp_mission_free(&m);
	kp_t_ok(T, found > 0, "%s plays WAVs", name);
}

static void original_mission_sounds(KpT *T)
{
	mission_sounds_found(T, "KP_TEST_ORIGINAL", "terran/terran01", "campaign\\terran\\terran01");
}
static void remastered_mission_sounds(KpT *T)
{
	mission_sounds_found(T, "KP_TEST_SCR", "zerg/zerg02", "campaign\\zerg\\zerg02");
}

static const KpTest TESTS[] = {
	{ "the_1_17_mission_wavs_are_found", original_mission_sounds, 0 },
	{ "the_remastered_mission_wavs_are_found", remastered_mission_sounds, 0 },
	{ "the_remastered_interface_has_the_1_17_parts", remastered_interface, 0 },
};
KP_SUITE(kp_suite_original, "test_original", TESTS);
