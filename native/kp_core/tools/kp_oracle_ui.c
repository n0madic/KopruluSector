/* The interface's oracle (kp_oracle_ui.h). */
#include "kp_oracle_ui.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/kp_kpf.h"
#include "kp_present.h"

/* --- the interface --- */

static const char *const UI_FILES[6] = { "generated/data/ui.kpd", "generated/data/fonts.kpd", "generated/data/strings.kpd",
	"generated/data/portraits.kpd", "generated/data/sounds.kpd", "generated/data/campaign.kpd" };

const uint8_t *kp_oracle_ui_campaign_section(const KpOracleUi *u, const char *fourcc, size_t *size)
{
	return kp_kpf_table_in(u->files[5], u->sizes[5], fourcc, size);
}

int kp_oracle_ui_new(KpOracleUi *u, const char *root, int flags, KpError *err)
{
	char why[256];
	int k;
	memset(u, 0, sizeof *u);
	for (k = 0; k < 6; k++) {
		int wanted = k < 2 || (k == 2 && (flags & KP_ORACLE_UI_STRINGS)) || (k == 3 && (flags & KP_ORACLE_UI_PORTRAITS))
			|| (k == 4 && (flags & KP_ORACLE_UI_SOUNDS)) || (k == 5 && (flags & (KP_ORACLE_UI_CHEATS | KP_ORACLE_UI_CAMPAIGN)));
		if (wanted && !(u->files[k] = kp_read_under(root, UI_FILES[k], &u->sizes[k], err))) goto failed;
	}
	u->ui = kp_ui_new(u->files[0], u->sizes[0], u->files[1], u->sizes[1], why, sizeof why);
	if (!u->ui) {
		kp_error_set(err, "%s", why);
		goto failed;
	}
	if ((flags & KP_ORACLE_UI_STRINGS) && kp_ui_set_strings(u->ui, u->files[2], u->sizes[2]) != 0) goto ui_failed;
	if ((flags & KP_ORACLE_UI_PORTRAITS) && kp_ui_set_portraits(u->ui, u->files[3], u->sizes[3]) != 0) goto ui_failed;
	if ((flags & KP_ORACLE_UI_SOUNDS) && kp_ui_set_sounds(u->ui, u->files[4], u->sizes[4]) != 0) goto ui_failed;
	if (flags & KP_ORACLE_UI_CHEATS) {
		size_t n;
		const uint8_t *chea = kp_oracle_ui_campaign_section(u, "CHEA", &n);
		if (!chea || kp_ui_set_cheats(u->ui, chea, n) != 0) {
			kp_error_set(err, "ui: a malformed CHEA section");
			goto failed;
		}
	}
	if (flags & KP_ORACLE_UI_CAMPAIGN) {
		size_t n;
		const uint8_t *tabl = kp_oracle_ui_campaign_section(u, "TABL", &n);
		if (!tabl) {
			kp_error_set(err, "ui: no campaign TABL");
			goto failed;
		}
		u->ui->menus.campaign = tabl;
		u->ui->menus.campaign_size = n;
	}
	return 0;
ui_failed:
	kp_error_set(err, "ui: %s", u->ui->error);
failed:
	kp_oracle_ui_free(u);
	return -1;
}

void kp_oracle_ui_free(KpOracleUi *u)
{
	int k;
	if (u->ui) {
		free(u->ui->timers.trace);
		u->ui->timers.trace = NULL;
		kp_ui_free(u->ui);
	}
	for (k = 0; k < 6; k++) free(u->files[k]);
	free(u->palettes);
	for (k = 0; k < 3; k++) free(u->ticks[k]);
	free(u->time_seeds);
	memset(u, 0, sizeof *u);
}

int kp_oracle_ui_colours(KpOracleUi *u, const char *root, const KpOracleGame *og, int minimap, KpError *err)
{
	const KpMission *m = &og->data.mission;
	KpTileset t;
	KpPalettes pal;
	char path[128];
	kp_host_tileset_path(og, path, sizeof path);
	if (kp_tileset_load(&t, og->tileset, og->tileset_size, path, err) != 0) {
		return -1;
	}
	kp_ui_resolve_colours(u->ui, t.rgb, t.cycled);
	if (kp_ui_game_effects(u->ui, t.rgb, t.cycled) != 0) return kp_error_set(err, "ui: out of memory");
	if (minimap) {
		free(u->palettes);
		if (!(u->palettes = kp_read_under(root, "generated/data/palettes.kpd", &u->palettes_size, err))) return -1;
		if (kp_palettes_load(&pal, u->palettes, u->palettes_size, err) != 0) return -1;
		if (!t.mmni || kp_ui_set_minimap(u->ui, t.mmni, t.mmni_size, t.rgb, t.cycled, pal.tilesets[m->tileset & 7].dark,
			m->has_colors ? m->colors : NULL) != 0) {
			return kp_error_set(err, "ui: no tminimap.pcx or tblink.pcx");
		}
	}
	return 0;
}

const char *kp_oracle_race_theme(int64_t race)
{
	static const char *const THEMES[3] = { "dlgs\\zerg.grp", "dlgs\\terran.grp", "dlgs\\protoss.grp" };
	return race >= 0 && race < 3 ? THEMES[race] : THEMES[1];
}

/* --- the input log's entries --- */

int kp_hud_log_parse(KpHudLog *h, const char *text, size_t size, KpError *err)
{
	size_t at = 0;
	memset(h, 0, sizeof *h);
	while (at < size) {
		size_t end = at;
		char line[1024];
		size_t n;
		while (end < size && text[end] != '\n') end++;
		n = end - at < sizeof line - 1 ? end - at : sizeof line - 1;
		memcpy(line, text + at, n);
		line[n] = '\0';
		at = end + 1;
		if (n == 0) continue; /* gmatch("[^\n]+") */
		{
			long long v;
			char bin[64];
			if (sscanf(line, "create %lld", &v) == 1 && strncmp(line, "create ", 7) == 0) {
				h->create = v;
				h->has_create = 1;
			} else if (sscanf(line, "cheats %lld", &v) == 1 && strncmp(line, "cheats ", 7) == 0) {
				h->cheats = v;
				h->has_cheats = 1;
			} else if (strncmp(line, "tick ", 5) == 0 && sscanf(line, "tick %63s %lld", bin, &v) == 2) {
				if (h->bin_count < KP_HUD_BINS) {
					snprintf(h->bins[h->bin_count], sizeof h->bins[0], "%s", bin);
					h->bin_ticks[h->bin_count++] = v;
				}
			} else {
				/* the numbers of the line (an optional minus and digits) */
				KpHudEntry *e;
				const char *p = line;
				h->entries = (KpHudEntry *)kp_grow(h->entries, h->count, &h->cap, sizeof *h->entries);
				e = &h->entries[h->count++];
				memset(e, 0, sizeof *e);
				while (*p) {
					if ((*p == '-' && isdigit((unsigned char)p[1])) || isdigit((unsigned char)*p)) {
						char *next;
						long long x = strtoll(p, &next, 10);
						if (e->count < KP_HUD_VALUES) e->v[e->count++] = x;
						p = next;
					} else {
						p++;
					}
				}
			}
		}
	}
	(void)err;
	return 0;
}

void kp_hud_log_from_golden(KpHudLog *h, const KpJson *g)
{
	const KpJson *ticks = kp_json_get(g, "create_ticks"), *cases = kp_json_get(g, "cases");
	int k, i;
	memset(h, 0, sizeof *h);
	h->has_create = kp_json_get(g, "create") != NULL;
	h->create = kp_json_field_int(g, "create", 0);
	h->has_cheats = kp_json_get(g, "cheats") != NULL;
	h->cheats = kp_json_field_int(g, "cheats", 0);
	for (k = 0; ticks && ticks->type == KP_JSON_OBJECT && k < ticks->count && h->bin_count < KP_HUD_BINS; k++) {
		snprintf(h->bins[h->bin_count], sizeof h->bins[0], "%s", ticks->keys[k]);
		h->bin_ticks[h->bin_count++] = kp_json_int(ticks->items[k], 0);
	}
	for (k = 0; k < kp_json_len(cases); k++) {
		const KpJson *c = kp_json_at(cases, k);
		KpHudEntry *e;
		h->entries = (KpHudEntry *)kp_grow(h->entries, h->count, &h->cap, sizeof *h->entries);
		e = &h->entries[h->count++];
		memset(e, 0, sizeof *e);
		for (i = 0; i < kp_json_len(c) && e->count < KP_HUD_VALUES; i++) e->v[e->count++] = kp_json_int(kp_json_at(c, i), 0);
	}
}

void kp_hud_log_free(KpHudLog *h)
{
	free(h->entries);
	memset(h, 0, sizeof *h);
}

/* --- the console over a replayed game --- */

/* A counted set of keys (the interface's local selections and game calls the entries place themselves). */
typedef struct {
	char **keys;
	int *counts;
	size_t cap, used;
} Counts;

static uint32_t hash(const char *s)
{
	uint32_t h = 2166136261u;
	while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
	return h;
}

static size_t slot(const Counts *c, const char *key)
{
	size_t i = hash(key) & (c->cap - 1);
	while (c->keys[i] && strcmp(c->keys[i], key) != 0) i = (i + 1) & (c->cap - 1);
	return i;
}

static void counts_add(Counts *c, const char *key)
{
	size_t i;
	if ((c->used + 1) * 2 > c->cap) {
		Counts bigger;
		size_t k;
		bigger.cap = c->cap ? c->cap * 2 : 1024;
		bigger.used = c->used;
		bigger.keys = (char **)calloc(bigger.cap, sizeof *bigger.keys);
		bigger.counts = (int *)calloc(bigger.cap, sizeof *bigger.counts);
		if (!bigger.keys || !bigger.counts) kp_out_of_memory();
		for (k = 0; k < c->cap; k++) {
			if (c->keys[k]) {
				size_t j = slot(&bigger, c->keys[k]);
				bigger.keys[j] = c->keys[k];
				bigger.counts[j] = c->counts[k];
			}
		}
		free(c->keys);
		free(c->counts);
		*c = bigger;
	}
	i = slot(c, key);
	if (!c->keys[i]) {
		size_t n = strlen(key) + 1;
		c->keys[i] = (char *)malloc(n);
		if (!c->keys[i]) kp_out_of_memory();
		memcpy(c->keys[i], key, n);
		c->used++;
	}
	c->counts[i]++;
}

/* Takes one of the key's count; 1 when there was one. */
static int counts_take(Counts *c, const char *key)
{
	size_t i;
	if (!c->cap) return 0;
	i = slot(c, key);
	if (!c->keys[i] || c->counts[i] <= 0) return 0;
	c->counts[i]--;
	return 1;
}

static void counts_free(Counts *c)
{
	size_t k;
	for (k = 0; k < c->cap; k++) free(c->keys[k]);
	free(c->keys);
	free(c->counts);
	memset(c, 0, sizeof *c);
}

/* The game calls of tools/hud_log.py (CALLS: 1..6). */
static const char *const CALLS[7] = { "", "paused", "release", "skip", "onscreen", "critter", "menu_pause" };
#define IDLE_EVENT 13 /* an input entry's type (kp_ui.h KP_UI_IDLE) */
#define COMMAND_EVENT 16

static const char *call_name(int64_t code) { return code >= 1 && code <= 6 ? CALLS[code] : ""; }

/* The values of v[from..count-1] joined by ",". */
static void joined(char *out, size_t size, const int64_t *v, int from, int count)
{
	size_t at = 0;
	int k;
	out[0] = '\0';
	for (k = from; k < count && at < size; k++) at += (size_t)snprintf(out + at, size - at, "%s%lld", k > from ? "," : "", (long long)v[k]);
}

/* key_of: an interface event's key as the entries name it ("" none). */
static void key_of(const KpReplayUi *u, char *out, size_t size)
{
	char list[512];
	int n;
	switch (u->kind) {
	case KP_RUN_SELECT: n = u->count; break;
	case KP_RUN_PAUSED: case KP_RUN_RELEASE: case KP_RUN_SKIP: n = 0; break;
	case KP_RUN_CRITTER: case KP_RUN_MENU_PAUSE: n = 1; break;
	case KP_RUN_ONSCREEN: n = 2; break;
	default: out[0] = '\0'; return;
	}
	joined(list, sizeof list, u->values, 0, n);
	snprintf(out, size, "%lld %s %s", (long long)u->frame, kp_run_word(u->kind), list);
}

static void pending(KpHudPending **list, int *count, int *cap, int64_t frame, const KpHudEntry *e, const int64_t *v, int n)
{
	KpHudPending *p;
	*list = (KpHudPending *)kp_grow(*list, *count, cap, sizeof **list);
	p = &(*list)[(*count)++];
	memset(p, 0, sizeof *p);
	p->frame = frame;
	p->entry = e;
	if (v) memcpy(p->v, v, sizeof(int64_t) * (size_t)n);
}

/* The interface's sounds and talks against the log's, in order (the game's sounds reach the interface after the
 * game's step, where the log has them inside it). */
static void match(KpHudReplay *r)
{
	const KpHudObservers *o = r->obs;
	while (r->sw_at < r->sw && r->sh_at < r->sh) {
		const KpHudPending *w = &r->sounds_want[r->sw_at++], *h = &r->sounds_have[r->sh_at++];
		if (o->on_sound) o->on_sound(o->ud, r, w->frame, h->v, w->entry);
	}
	while (r->tw_at < r->tw && r->th_at < r->th) {
		const KpHudPending *w = &r->talks_want[r->tw_at++], *h = &r->talks_have[r->th_at++];
		if (o->on_talk) o->on_talk(o->ud, r, w->frame, h->v, w->entry);
	}
}

static void collect(KpHudReplay *r)
{
	KpUi *ui = r->ui.ui;
	KpUiSoundCall calls[KP_UI_SOUND_CALLS];
	size_t n, k;
	int t;
	if (!r->voices) return;
	n = kp_ui_take_sound_calls(ui, calls, KP_UI_SOUND_CALLS);
	for (k = 0; k < n; k++) {
		int64_t v[6];
		v[0] = calls[k].id;
		v[1] = calls[k].attenuation;
		v[2] = calls[k].pan;
		v[3] = calls[k].unit_type;
		v[4] = calls[k].path;
		v[5] = calls[k].result;
		pending(&r->sounds_have, &r->sh, &r->cap_sh, 0, NULL, v, 6);
	}
	for (t = 0; t < ui->sound.talk_count; t++) {
		int64_t v[5];
		v[0] = ui->sound.talks[t].unit;
		v[1] = ui->sound.talks[t].duration;
		v[2] = ui->sound.talks[t].sound;
		v[3] = ui->sound.talks[t].ping;
		v[4] = ui->sound.talks[t].colour;
		pending(&r->talks_have, &r->th, &r->cap_th, 0, NULL, v, 5);
	}
	ui->sound.talk_count = 0;
	match(r);
}

static void take_commands(KpHudReplay *r, int64_t frame)
{
	uint8_t buf[KP_UI_COMMAND_BYTES];
	size_t n;
	while ((n = kp_ui_take_command(r->ui.ui, buf, sizeof buf)) != 0) {
		if (r->obs->on_command) r->obs->on_command(r->obs->ud, r, frame, buf, n);
	}
}

void kp_oracle_ui_input(KpUi *ui, int64_t type, int64_t a, int64_t b)
{
	KpUiEvent e;
	memset(&e, 0, sizeof e);
	e.type = (uint16_t)type;
	switch (e.type) {
	case KP_UI_KEY_DOWN: case KP_UI_KEY_REPEAT: case KP_UI_KEY_UP: case KP_UI_CHAR: case KP_UI_COMMAND:
		e.key = (uint16_t)a;
		e.mods = (uint16_t)b;
		e.x = ui->mouse_x;
		e.y = ui->mouse_y;
		break;
	case KP_UI_WHEEL_UP: case KP_UI_WHEEL_DOWN:
		e.param = (uint32_t)a;
		break;
	default:
		e.x = (int16_t)a;
		e.y = (int16_t)b;
		break;
	}
	kp_ui_input_event(ui, &e);
}

void kp_oracle_ui_bin_name(const KpUi *ui, int32_t bin, int lower, char *out, size_t size)
{
	size_t k, n = bin >= 0 && bin < ui->bin_count ? ui->bins[bin].name_len : 0;
	for (k = 0; k < n && k + 1 < size; k++) {
		char c = ui->bins[bin].name[k];
		out[k] = lower ? (char)tolower((unsigned char)c) : c;
	}
	if (size) out[k] = '\0';
}

static void drain(KpHudReplay *r)
{
	KpEvent e;
	while (kp_event_take(r->game.g, &e)) kp_ui_game_event(r->ui.ui, e.kind, e.a, e.b, e.c, e.text);
}

static void scroll(KpHudReplay *r, const KpHudEntry *e)
{
	KpUi *ui = r->ui.ui;
	ui->mouse_scroll_speed = (uint32_t)e->v[10];
	ui->key_scroll_speed = (uint32_t)e->v[11];
	if (!r->scrolled) {
		r->scrolled = 1;
		kp_ui_set_screen(ui, (int32_t)e->v[2], (int32_t)e->v[3]); /* the view before the interface's own */
		return;
	}
	/* the record's cursor is where the system cursor was (GetCursorPos: input, not the interface's); the game loop's
	 * UpdateCursor 0x43ea40 looked at it just before (its choice is CURSOR_CHOICE's) */
	kp_ui_edge_scroll(ui, (int32_t)e->v[8], (int32_t)e->v[9], NULL, NULL);
	kp_ui_update_scroll(ui, (int32_t)e->v[8], (int32_t)e->v[9]);
	if (r->obs->on_scroll) r->obs->on_scroll(r->obs->ud, r, e->v[0], e);
}

static void check_tip(KpHudReplay *r)
{
	int32_t have[4];
	const KpUiSurface *t = kp_ui_tooltip(r->ui.ui, &have[0], &have[1], &have[2], &have[3]);
	int shown = t != NULL && have[2] > 0 && have[3] > 0 && have[2] <= t->w && have[3] <= t->h;
	r->obs->on_tip(r->obs->ud, r, r->tip_frame, shown ? have : NULL, r->tip_has_rect ? r->tip_rect : NULL);
	r->tip_wanted = 0;
}

/* run.after_objects: the interface's work of the entries up to the game's frame. */
static void after_objects(void *ud)
{
	KpHudReplay *r = (KpHudReplay *)ud;
	const KpHudReplayOpts *opts = r->opts;
	const KpHudObservers *o = r->obs;
	const KpHudLog *hud = opts->hud;
	KpGame *g = r->game.g;
	KpUi *ui = r->ui.ui;
	int64_t frame = kp_scalar(g, KP_FRAME);
	if (r->voices) { /* the step's sounds start when the original ran the step: the tick of the step's first sound */
		int k;
		for (k = r->next_entry; k < hud->count; k++) {
			const KpHudEntry *e = &hud->entries[k];
			if (e->v[0] > frame) break;
			if (e->v[1] == KP_HUD_SOUND) {
				ui->now = (uint32_t)e->v[10];
				break;
			}
		}
	}
	drain(r);
	/* the commands the game's own work makes the interface send (a unit out of the selection) */
	if (opts->input) take_commands(r, frame);
	while (r->next_entry < hud->count && hud->entries[r->next_entry].v[0] <= frame) {
		const KpHudEntry *e = &hud->entries[r->next_entry];
		const KpHudEntry *nxt = r->next_entry + 1 < hud->count ? &hud->entries[r->next_entry + 1] : NULL;
		int64_t kind = e->v[1];
		if (o->on_entry) o->on_entry(o->ud, r, e);
		if (kind == KP_HUD_PASS) {
			kp_ui_tick(ui, (uint32_t)e->v[2]);
		} else if (kind == KP_HUD_HUD) {
			int64_t k;
			for (k = 0; k < e->v[2]; k++) kp_ui_hud_frame(ui);
		} else if (kind == KP_HUD_PRESENT) {
			/* the screen's draw refreshes the images' bounds the clicks test (RecomputeAllSpriteBounds 0x415700) */
			if (opts->input) kp_images_refresh_bounds(g->w);
			ui->now = (uint32_t)e->v[2];
			kp_ui_present(ui);
			if (o->on_present) o->on_present(o->ud, r, e->v[2]);
			if (r->next_want < opts->frame_count && e->v[0] >= opts->frames[r->next_want]) {
				if (o->on_frame) o->on_frame(o->ud, r, opts->frames[r->next_want]);
				r->next_want++;
			}
		} else if (kind == KP_HUD_TALK && r->voices) {
			pending(&r->talks_want, &r->tw, &r->cap_tw, e->v[0], e, NULL, 0);
		} else if (kind == KP_HUD_SOUND && r->voices) {
			pending(&r->sounds_want, &r->sw, &r->cap_sw, e->v[0], e, NULL, 0);
		} else if (kind == KP_HUD_TALK) {
			ui->now = (uint32_t)e->v[7];
			kp_ui_portrait_talk(ui, e->v[2], (uint32_t)e->v[3], e->v[5] != 0, (uint8_t)e->v[6], (uint32_t)e->v[4]);
		} else if (kind == KP_HUD_SELECT && opts->input) {
			/* a selection right after an input event (not the idle event, whose handler selects nothing) or a command is
			 * the interface's, and so are the ones right after it; the game's own happen inside its step */
			int back = r->next_entry - 1, mine = 0;
			while (back >= 0 && (hud->entries[back].v[1] == KP_HUD_CURSOR || hud->entries[back].v[1] == KP_HUD_CURSOR_CHOICE)) back--;
			if (back >= 0) {
				const KpHudEntry *prev = &hud->entries[back];
				mine = (prev->v[1] == KP_HUD_INPUT && prev->v[2] != IDLE_EVENT) || prev->v[1] == KP_HUD_COMMAND
					|| (prev->v[1] == KP_HUD_SELECT && r->interface_select);
			}
			r->interface_select = mine;
			if (o->on_select && mine) o->on_select(o->ud, r, e->v[0], e->v + 2, e->count - 2);
		} else if (kind == KP_HUD_SELECT) {
			kp_select_locally(g, e->v + 2, e->count - 2 > KP_SLOTS ? KP_SLOTS : e->count - 2);
		} else if (kind == KP_HUD_INPUT && opts->input) {
			ui->now = (uint32_t)e->v[9];
			kp_ui_set_keys(ui, (uint8_t)e->v[5], (uint8_t)e->v[6], (uint8_t)e->v[7], (uint16_t)e->v[8]);
			if (r->scrolled) {
				/* the log's view is the one after the dialogs took the event (DispatchUiEvent's GetTickCount) */
			} else if (e->v[10] >= 0) {
				kp_ui_set_screen(ui, (int32_t)e->v[10], (int32_t)e->v[11]);
			} else {
				int64_t sx = kp_scalar(g, KP_SCREEN_X), sy = kp_scalar(g, KP_SCREEN_Y);
				kp_ui_set_screen(ui, (int32_t)((sx == KP_NIL ? 0 : sx) * 32), (int32_t)((sy == KP_NIL ? 0 : sy) * 32));
			}
			kp_oracle_ui_input(ui, e->v[2], e->v[3], e->v[4]);
			if (r->scrolled && e->v[10] >= 0 && o->on_screen) o->on_screen(o->ud, r, e->v[0], e->v[10], e->v[11], e);
			if (r->tip_wanted) check_tip(r);
			take_commands(r, e->v[0]);
		} else if (kind == KP_HUD_COMMAND && opts->input) {
			kp_oracle_ui_input(ui, COMMAND_EVENT, e->v[2], 0);
			take_commands(r, e->v[0]);
		} else if (kind == KP_HUD_CURSOR_CHOICE && opts->input) {
			/* where the log has the original's choice change, with the system cursor where it saw it */
			if (e->count > 3) kp_ui_cursor_update(ui, (int32_t)e->v[3], (int32_t)e->v[4]);
			else kp_ui_cursor_update(ui, ui->mouse_x, ui->mouse_y);
			if (o->on_cursor) o->on_cursor(o->ud, r, e->v[0], ui->cursor, e->v[2]);
		} else if (kind == KP_HUD_SCROLL && opts->input) {
			if (e != r->scrolled_early) scroll(r, e);
		} else if (kind == KP_HUD_TIP && opts->input) {
			/* the tooltip as the work left it, after the last of a run of records; the dialogs take an event before
			 * the log has it, so an event's tooltip is checked after it */
			if (o->on_tip && !(nxt && nxt->v[1] == KP_HUD_TIP)) {
				r->tip_wanted = 1;
				r->tip_frame = e->v[0];
				r->tip_has_rect = e->count > 2;
				if (r->tip_has_rect) memcpy(r->tip_rect, e->v + 2, sizeof r->tip_rect);
				if (!(nxt && nxt->v[1] == KP_HUD_INPUT)) check_tip(r);
			}
		} else if (kind == KP_HUD_CURSOR) {
			kp_ui_cursor_set(ui, (uint16_t)e->v[2]);
			if (e->count > 3) ui->cursor_counter = (uint32_t)e->v[3];
		} else if (kind == KP_HUD_GAME_CALL && opts->input
			&& (strcmp(call_name(e->v[2]), "skip") == 0 || strcmp(call_name(e->v[2]), "menu_pause") == 0)) {
			/* the interface's own Escape skips and its modeless dialogs' pauses */
		} else if (kind == KP_HUD_GAME_CALL && r->voices && strcmp(call_name(e->v[2]), "critter") == 0) {
			/* the interface's own select sounds kill the critter */
		} else if (kind == KP_HUD_GAME_CALL) {
			const char *name = call_name(e->v[2]);
			KpReplayUi call;
			if (opts->input && strcmp(name, "release") == 0) {
				const KpUiScroll *st = &ui->input.scroll;
				if (nxt && nxt->v[1] == KP_HUD_SCROLL) {
					/* the camera pan's arrival releases the wait inside UpdateMapScroll, before the log has its state */
					scroll(r, nxt);
					r->scrolled_early = nxt;
				} else if (st->pan == 1 && ui->input.screen_x == st->pan_x && ui->input.screen_y == st->pan_y) {
					/* a pan to where the view already is: UpdateMapScroll releases it without a change to log */
					kp_ui_update_scroll(ui, st->cursor_x, st->cursor_y);
				}
			}
			if (opts->input && strcmp(name, "release") == 0 && o->on_release) {
				/* the end of the trigger portrait or the camera pan's arrival: the interface's to say */
				o->on_release(o->ud, r, e->v[0], kp_ui_take_release(ui));
			}
			memset(&call, 0, sizeof call);
			call.frame = e->v[0];
			call.kind = kp_run_kind(name);
			if (e->count > 3) {
				call.values[call.count++] = e->v[3];
				if (e->count > 4) call.values[call.count++] = e->v[4];
			}
			if (call.kind >= 0) kp_runner_apply(&r->run, &call);
			drain(r); /* what the call does (a paused frame's triggers, the skip) reaches the interface at once */
		}
		r->next_entry++;
		collect(r);
	}
	collect(r);
}

int kp_hud_replay_start(KpHudReplay *r, const char *root, const KpHudReplayOpts *opts, const KpHudObservers *obs,
	KpError *err)
{
	static const char *const BINS[KP_UI_HUD_DIALOGS] = { "rez\\statport.bin", "rez\\statlb.bin", "rez\\statres.bin",
		"rez\\statdata.bin", "rez\\statbtn", "rez\\stat_f10.bin", "textbox", "rez\\minimap.bin" };
	const KpHudLog *hud = opts->hud;
	const KpJson *golden = opts->golden;
	KpReplay all;
	KpOracleOpts o;
	Counts placed;
	char path[600], key[640];
	uint32_t ticks[KP_UI_HUD_DIALOGS];
	int k, have_ticks = 0;
	memset(r, 0, sizeof *r);
	memset(&placed, 0, sizeof placed);
	r->opts = opts;
	r->obs = obs;
	/* the interface's local selections and its game calls are the entries' (at their place among the presents); the
	 * run's interface events have the same ones, which are left out so that each runs once */
	for (k = 0; k < hud->count; k++) {
		const KpHudEntry *e = &hud->entries[k];
		char list[512];
		if (e->v[1] == KP_HUD_SELECT) {
			joined(list, sizeof list, e->v, 2, e->count);
			snprintf(key, sizeof key, "%lld select %s", (long long)e->v[0], list);
			counts_add(&placed, key);
		}
		if (e->v[1] == KP_HUD_GAME_CALL) {
			joined(list, sizeof list, e->v, 3, e->count);
			snprintf(key, sizeof key, "%lld %s %s", (long long)e->v[0], call_name(e->v[2]), list);
			counts_add(&placed, key);
		}
	}
	/* with the log's sound records the interface plays the sounds itself */
	for (k = 0; k < hud->count; k++) {
		if (hud->entries[k].v[1] == KP_HUD_SOUND) {
			r->voice_entry = &hud->entries[k];
			break;
		}
	}
	if (!opts->input) r->voice_entry = NULL;
	r->voices = r->voice_entry != NULL;
	if (kp_replay_from_golden(&r->replay, root, golden, err) != 0) {
		counts_free(&placed);
		return -1;
	}
	/* the run's interface events (the newer runs' own ui.jsonl, or the golden's) with those left out */
	kp_replay_init(&all);
	if (opts->ui) {
		if (kp_replay_add_ui_jsonl(&all, opts->ui, opts->ui_size, err) != 0) {
			counts_free(&placed);
			kp_replay_free(&all);
			kp_replay_free(&r->replay);
			return -1;
		}
	} else {
		all.ui = r->replay.ui;
		all.ui_count = r->replay.ui_count;
		all.ui_cap = r->replay.ui_cap;
		r->replay.ui = NULL;
		r->replay.ui_cap = 0;
	}
	r->replay.ui_count = 0;
	for (k = 0; k < all.ui_count; k++) {
		const KpReplayUi *u = &all.ui[k];
		key_of(u, key, sizeof key);
		if (opts->input && (u->kind == KP_RUN_MARK || u->kind == KP_RUN_PLACEMENT || u->kind == KP_RUN_CURSOR
			|| u->kind == KP_RUN_MENU_PAUSE)) {
			continue; /* the interface's own clicks mark and place, its modeless dialogs pause */
		}
		if (r->voices && (u->kind == KP_RUN_CRT || u->kind == KP_RUN_CRITTER)) continue; /* the voices' choices */
		if (key[0] && counts_take(&placed, key)) continue;
		*kp_replay_ui(&r->replay, u->kind, u->frame) = *u;
	}
	kp_replay_free(&all);
	counts_free(&placed);
	if (opts->commands) { /* the run's commands in place of the golden's */
		kp_replay_clear_commands(&r->replay);
		if (kp_replay_add_commands_jsonl(&r->replay, opts->commands, opts->commands_size, err) != 0) {
			kp_replay_free(&r->replay);
			return -1;
		}
	}
	kp_oracle_opts(&o);
	o.host.events = 1;
	o.host.cheats = hud->has_cheats ? hud->cheats : 0;
	snprintf(path, sizeof path, "%s", r->replay.mission);
	if (kp_oracle_game_start(&r->game, root, path, r->replay.seed, r->replay.expansion, &o, err) != 0) {
		kp_replay_free(&r->replay);
		return -1;
	}
	kp_runner_init(&r->run, r->game.g, &r->replay);
	r->run.after_objects = after_objects;
	r->run.ud = r;
	if (kp_oracle_ui_new(&r->ui, root, KP_ORACLE_UI_STRINGS | KP_ORACLE_UI_PORTRAITS | KP_ORACLE_UI_SOUNDS
		| KP_ORACLE_UI_CHEATS, err) != 0 || kp_oracle_ui_colours(&r->ui, root, &r->game, 1, err) != 0) {
		kp_hud_replay_free(r);
		return -1;
	}
	kp_ui_font_set(r->ui.ui, 1);
	if (kp_ui_apply_theme(r->ui.ui, kp_oracle_race_theme(r->game.g->players.race[r->game.g->local_player])) != 0) {
		kp_error_set(err, "ui: no GRP %s", kp_oracle_race_theme(r->game.g->players.race[r->game.g->local_player]));
		kp_hud_replay_free(r);
		return -1;
	}
	r->ui.ui->now = (uint32_t)hud->create;
	memset(ticks, 0, sizeof ticks);
	for (k = 0; k < KP_UI_HUD_DIALOGS; k++) {
		int b;
		for (b = 0; b < hud->bin_count; b++) {
			if (strcmp(hud->bins[b], BINS[k]) == 0) {
				ticks[k] = (uint32_t)hud->bin_ticks[b];
				have_ticks = 1;
			}
		}
	}
	if (kp_ui_hud_create(r->ui.ui, r->game.g, have_ticks ? ticks : NULL) != 0) {
		kp_error_set(err, "ui: the console: %s", r->ui.ui->error);
		kp_hud_replay_free(r);
		return -1;
	}
	/* the original's game loop: the interface events, the turn and the frame count, the object step, then until the
	 * next count its idle loop (UpdateStatusUi, PresentFrame and the message loop's timer passes) */
	if (r->voices) {
		r->ui.ui->sound.device = (uint8_t)(r->voice_entry->v[8] == 1);
		r->ui.ui->sound.effects = (uint32_t)r->voice_entry->v[9];
	} else {
		r->ui.ui->sound.voices = 0;
	}
	return 0;
}

int kp_hud_replay_run(KpHudReplay *r, KpError *err)
{
	KpTry t;
	KpImages *w = r->game.g->w;
	volatile int failed = 0;
	kp_images_begin(w, &t);
	if (setjmp(t.buf)) {
		failed = 1;
	} else {
		int64_t step = 1;
		while (r->next_want < r->opts->frame_count) {
			kp_runner_to(&r->run, ++step);
			if (r->next_entry >= r->opts->hud->count && r->next_want < r->opts->frame_count) {
				kp_images_end(w, &t);
				return kp_error_set(err, "hud_replay: the log ends before frame %lld", (long long)r->opts->frames[r->next_want]);
			}
		}
		collect(r);
		if (r->voices && r->obs->on_sound_end) r->obs->on_sound_end(r->obs->ud, r);
	}
	kp_images_end(w, &t);
	if (failed) return kp_error_set(err, "%s", w->error);
	return 0;
}

void kp_hud_replay_free(KpHudReplay *r)
{
	kp_oracle_ui_free(&r->ui);
	kp_oracle_game_free(&r->game);
	kp_replay_free(&r->replay);
	free(r->sounds_want);
	free(r->sounds_have);
	free(r->talks_want);
	free(r->talks_have);
	memset(r, 0, sizeof *r);
}

/* --- the glue's trace streams --- */

int kp_trace_file_parse(KpTraceFile *f, char *text, const char *const *header_words, KpError *err)
{
	char *line, *next;
	int cap = 0, k;
	memset(f, 0, sizeof *f);
	f->text = text;
	for (k = 0; header_words[k] && k < 8; k++) f->header_words[k] = header_words[k];
	f->header_count = k;
	for (line = text; line && *line; line = next) {
		char *rest;
		size_t n;
		next = strchr(line, '\n');
		if (next) *next++ = '\0';
		if (!*line) continue;
		n = strcspn(line, " \t\r");
		if (n == 0) continue;
		rest = line + n;
		while (*rest == ' ' || *rest == '\t') rest++;
		if (n == 3 && strncmp(line, "kpm", 3) == 0) {
			snprintf(f->kpm, sizeof f->kpm, "%s", rest);
			continue;
		}
		for (k = 0; k < f->header_count; k++) {
			if (strlen(f->header_words[k]) == n && strncmp(f->header_words[k], line, n) == 0) break;
		}
		if (k < f->header_count) {
			f->header[k] = strtoll(rest, NULL, 10);
			continue;
		}
		f->records = (KpTraceRecord *)kp_grow(f->records, f->count, &cap, sizeof *f->records);
		snprintf(f->records[f->count].word, sizeof f->records[0].word, "%.*s", (int)(n < 15 ? n : 15), line);
		f->records[f->count].line = line;
		f->records[f->count].rest = rest;
		f->count++;
	}
	(void)err;
	return 0;
}

void kp_trace_file_free(KpTraceFile *f)
{
	free(f->records);
	free(f->text);
	memset(f, 0, sizeof *f);
}

int64_t kp_trace_header(const KpTraceFile *f, const char *word, int64_t fallback)
{
	int k;
	for (k = 0; k < f->header_count; k++) {
		if (strcmp(f->header_words[k], word) == 0) return f->header[k];
	}
	return fallback;
}

static void stream_add(KpTraceStream *s, const char *line)
{
	size_t n = strlen(line) + 1;
	s->lines = (char **)kp_grow(s->lines, s->count, &s->cap, sizeof *s->lines);
	s->lines[s->count] = (char *)malloc(n);
	if (!s->lines[s->count]) kp_out_of_memory();
	memcpy(s->lines[s->count++], line, n);
}

void kp_trace_streams_free(KpTraceStreams *s)
{
	int k, i;
	for (k = 0; k < KP_TRACE_STREAMS; k++) {
		for (i = 0; i < s->s[k].count; i++) free(s->s[k].lines[i]);
		free(s->s[k].lines);
	}
	memset(s, 0, sizeof *s);
}

void kp_trace_clocks_free(KpTraceClocks *c)
{
	free(c->set_ticks);
	free(c->video_ticks);
	free(c->page_deadlines);
	memset(c, 0, sizeof *c);
}

static void tick_add(uint32_t **list, int *count, int *cap, int64_t v)
{
	*list = (uint32_t *)kp_grow(*list, *count, cap, sizeof **list);
	(*list)[(*count)++] = (uint32_t)v;
}

/* line:gsub("%?%d+", "?"): an owner of no BIN, numbered by the log. */
static void unnumbered(const char *line, char *out, size_t size)
{
	size_t at = 0;
	while (*line && at + 1 < size) {
		if (*line == '?' && isdigit((unsigned char)line[1])) {
			out[at++] = '?';
			line++;
			while (isdigit((unsigned char)*line)) line++;
		} else {
			out[at++] = *line++;
		}
	}
	out[at] = '\0';
}

/* "<line> @<tick>": the line before " @" and the tick, or 0 without one. */
static int split_tick(const char *text, char *line, size_t size, int64_t *tick)
{
	const char *at = strrchr(text, '@');
	size_t n;
	if (!at || at == text || at[-1] != ' ' || !isdigit((unsigned char)at[1])) return 0;
	{
		const char *p = at + 1;
		while (isdigit((unsigned char)*p)) p++;
		if (*p) return 0;
	}
	n = (size_t)(at - 1 - text);
	if (n >= size) n = size - 1;
	memcpy(line, text, n);
	line[n] = '\0';
	*tick = strtoll(at + 1, NULL, 10);
	return 1;
}

void kp_trace_want(const KpTraceFile *f, KpTraceKeep keep, KpTraceStreams *want, KpTraceClocks *clocks)
{
	int k, screen_pages = 0, sc = 0, vc = 0, pc = 0;
	memset(want, 0, sizeof *want);
	memset(clocks, 0, sizeof *clocks);
	for (k = 0; k < f->count; k++) {
		const KpTraceRecord *r = &f->records[k];
		char line[1024], clean[1024];
		int64_t tick;
		if (strcmp(r->word, "set") == 0) { /* the set's tick is the clock's, not compared */
			if (!split_tick(r->line, line, sizeof line, &tick)) continue;
			unnumbered(line, clean, sizeof clean);
			if (!keep || keep(clean)) {
				stream_add(&want->s[KP_TRACE_TIMERS], clean);
				tick_add(&clocks->set_ticks, &clocks->set_count, &sc, tick);
			}
		} else if (strcmp(r->word, "del") == 0 || strcmp(r->word, "delall") == 0 || strcmp(r->word, "pass") == 0
			|| strcmp(r->word, "fire") == 0) {
			unnumbered(r->line, clean, sizeof clean);
			if (!keep || keep(clean)) stream_add(&want->s[KP_TRACE_TIMERS], clean);
		} else if (strcmp(r->word, "brief") == 0) {
			stream_add(&want->s[KP_TRACE_BRIEF], r->rest);
		} else if (strcmp(r->word, "rand") == 0) {
			stream_add(&want->s[KP_TRACE_RANDS], r->rest);
		} else if (strcmp(r->word, "svid") == 0) {
			stream_add(&want->s[KP_TRACE_VIDEOS], r->rest);
		} else if (strcmp(r->word, "flc") == 0 || strcmp(r->word, "bpc") == 0) {
			KpTraceStream *s = &want->s[strcmp(r->word, "flc") == 0 ? KP_TRACE_FLC : KP_TRACE_BPC];
			if (split_tick(r->rest, line, sizeof line, &tick)) {
				stream_add(s, line);
				tick_add(&clocks->video_ticks, &clocks->video_count, &vc, tick);
			} else {
				stream_add(s, r->rest);
			}
		} else if (strcmp(r->word, "credits") == 0) {
			screen_pages = 0;
		} else if (strcmp(r->word, "page") == 0) { /* the page before's deadline is the clock's */
			char first[64];
			long long deadline;
			if (sscanf(r->rest, "%63s %lld", first, &deadline) != 2) continue;
			stream_add(&want->s[KP_TRACE_PAGES], first);
			screen_pages++;
			if (screen_pages > 1) tick_add(&clocks->page_deadlines, &clocks->page_count, &pc, deadline);
		}
	}
}

/* The interface keeps pointers to its oracle clocks: copies that live with u. */
static uint32_t *copy_ticks(const uint32_t *v, int n)
{
	uint32_t *c = (uint32_t *)malloc(sizeof *c * (size_t)(n > 0 ? n : 1));
	if (!c) kp_out_of_memory();
	if (n) memcpy(c, v, sizeof *c * (size_t)n);
	return c;
}

void kp_trace_clocks(KpOracleUi *u, const KpTraceClocks *c)
{
	KpUi *ui = u->ui;
	free(u->ticks[0]);
	u->ticks[0] = copy_ticks(c->set_ticks, c->set_count);
	ui->timers.set_ticks = u->ticks[0];
	ui->timers.set_tick_count = c->set_count;
	ui->timers.set_tick_next = 0;
	if (c->video_count > 0) {
		free(u->ticks[1]);
		u->ticks[1] = copy_ticks(c->video_ticks, c->video_count);
		ui->video_ticks = u->ticks[1];
		ui->video_tick_count = c->video_count;
		ui->video_tick_next = 0;
	}
	if (c->page_count > 0) {
		free(u->ticks[2]);
		u->ticks[2] = copy_ticks(c->page_deadlines, c->page_count);
		ui->page_deadlines = u->ticks[2];
		ui->page_deadline_count = c->page_count;
		ui->page_deadline_next = 0;
	}
}

/* A timer record's owner: its BIN's name in lower case and the record, or "?" (a control of no BIN). */
static void owner_ref(const KpUi *ui, const KpUiTimerRecord *t, char *out, size_t size)
{
	if (t->bin >= 0 && t->bin < ui->bin_count) {
		size_t at;
		kp_oracle_ui_bin_name(ui, t->bin, 1, out, size);
		at = strlen(out);
		snprintf(out + at, size - at, "#%d", (int)t->record);
	} else {
		snprintf(out, size, "?");
	}
}

void kp_trace_take(KpOracleUi *u, KpTraceStreams *have, KpTraceKeep keep)
{
	KpUi *ui = u->ui;
	KpUiTimerTrace *t = ui->timers.trace;
	char line[512], ref[256];
	int k;
	for (k = 0; k < ui->glue.rand_count; k++) {
		snprintf(line, sizeof line, "%u", (unsigned)ui->glue.rand_log[k]);
		stream_add(&have->s[KP_TRACE_RANDS], line);
	}
	ui->glue.rand_count = 0;
	for (k = 0; k < ui->bpc_log_count; k++) {
		snprintf(line, sizeof line, "%d %d %d %d", (int)ui->bpc_log[k].id, (int)ui->bpc_log[k].result, (int)ui->bpc_log[k].shown,
			(int)ui->bpc_log[k].frame);
		stream_add(&have->s[KP_TRACE_BPC], line);
	}
	ui->bpc_log_count = 0;
	for (k = 0; k < ui->page_log_count; k++) {
		snprintf(line, sizeof line, "%u", (unsigned)ui->page_log[k]);
		stream_add(&have->s[KP_TRACE_PAGES], line);
	}
	ui->page_log_count = 0;
	for (k = 0; k < ui->flc_log_count; k++) {
		snprintf(line, sizeof line, "%d %d %d %d", (int)ui->flc_log[k].clip, (int)ui->flc_log[k].result,
			(int)ui->flc_log[k].shown, (int)ui->flc_log[k].frame);
		stream_add(&have->s[KP_TRACE_FLC], line);
	}
	ui->flc_log_count = 0;
	for (k = 0; t && k < t->count; k++) {
		const KpUiTimerRecord *rec = &t->rec[k];
		owner_ref(ui, rec, ref, sizeof ref);
		switch (rec->kind) {
		case KP_UI_TRACE_SET:
			snprintf(line, sizeof line, "set %s %d %u %d", ref, (int)rec->id, (unsigned)rec->period, rec->callback);
			if (getenv("TRACE_SET_TICKS")) fprintf(stderr, "%s @%u\n", line, (unsigned)rec->tick);
			break;
		case KP_UI_TRACE_DEL: snprintf(line, sizeof line, "del %s %d", ref, (int)rec->id); break;
		case KP_UI_TRACE_DELALL: snprintf(line, sizeof line, "delall %s", ref); break;
		case KP_UI_TRACE_FIRE: snprintf(line, sizeof line, "fire %s %d", ref, (int)rec->id); break;
		default: snprintf(line, sizeof line, "pass %u", (unsigned)rec->tick); break;
		}
		if (!keep || keep(line)) stream_add(&have->s[KP_TRACE_TIMERS], line);
	}
	if (t) t->count = 0;
	for (k = 0; k < ui->video_count; k++) {
		size_t i;
		snprintf(line, sizeof line, "%s", ui->videos[k]);
		for (i = 0; line[i]; i++) line[i] = (char)tolower((unsigned char)line[i]);
		stream_add(&have->s[KP_TRACE_VIDEOS], line);
	}
	ui->video_count = 0;
	for (k = 0; k < ui->briefing.log_count; k++) {
		snprintf(line, sizeof line, "%d %d %d %d", (int)ui->briefing.log[k].record, (int)ui->briefing.log[k].index,
			(int)ui->briefing.log[k].type, (int)ui->briefing.log[k].result);
		stream_add(&have->s[KP_TRACE_BRIEF], line);
	}
	ui->briefing.log_count = 0;
}

#define SHOWN 8

int kp_trace_compare(const KpTraceStreams *want, const KpTraceStreams *have)
{
	static const char *const NAMES[KP_TRACE_STREAMS] = { "timer", "brief", "video", "flc", "page", "bpc", "rand" };
	const char *ctx = getenv("ROOM_CONTEXT");
	int context = ctx ? atoi(ctx) : 0, differences = 0, printed = 0, s;
	for (s = 0; s < KP_TRACE_STREAMS; s++) {
		const KpTraceStream *w = &want->s[s], *h = &have->s[s];
		int n = w->count > h->count ? w->count : h->count, k;
		for (k = 0; k < n; k++) {
			const char *a = k < w->count ? w->lines[k] : NULL, *b = k < h->count ? h->lines[k] : NULL;
			if (a && b && strcmp(a, b) == 0) continue;
			differences++;
			printed++;
			if (printed <= SHOWN) {
				int j;
				printf("%s %d: Koprulu %s, the original %s\n", NAMES[s], k + 1, b ? b : "nothing", a ? a : "nothing");
				for (j = k - context > 0 ? k - context : 0; j <= k + context && j < n; j++) {
					printf("  %d  %-40s %s\n", j + 1, j < w->count ? w->lines[j] : "-", j < h->count ? h->lines[j] : "-");
				}
			}
			break;
		}
	}
	return differences;
}

static int by_tick(const void *a, const void *b)
{
	const KpTraceCapture *x = (const KpTraceCapture *)a, *y = (const KpTraceCapture *)b;
	return x->tick < y->tick ? -1 : x->tick > y->tick;
}

int kp_trace_captures(const KpTraceFile *f, KpTraceCapture **out)
{
	KpTraceCapture *caps = NULL;
	int count = 0, cap = 0, k, i;
	for (k = 0; k < f->count; k++) {
		const KpTraceRecord *r = &f->records[k];
		KpTraceCapture *c;
		long long tick, cx, cy, cw, ch, l, t, rr, b;
		char path[512], name[128], under[512];
		int n;
		if (strcmp(r->word, "cap") != 0) continue;
		n = sscanf(r->rest, "%lld %511s %127s %lld %lld %lld %lld %lld %lld %lld %lld %511s", &tick, path, name, &cx, &cy,
			&cw, &ch, &l, &t, &rr, &b, under);
		if (n < 7) continue;
		caps = (KpTraceCapture *)kp_grow(caps, count, &cap, sizeof *caps);
		c = &caps[count++];
		memset(c, 0, sizeof *c);
		c->tick = tick;
		snprintf(c->path, sizeof c->path, "%s", path);
		snprintf(c->name, sizeof c->name, "%s", name);
		c->cursor[0] = cx;
		c->cursor[1] = cy;
		c->cursor[2] = cx + cw - 1;
		c->cursor[3] = cy + ch - 1;
		if (n >= 11) {
			c->has_rect = 1;
			c->rect[0] = l;
			c->rect[1] = t;
			c->rect[2] = rr;
			c->rect[3] = b;
		}
		if (n >= 12) {
			c->has_under = 1;
			snprintf(c->under, sizeof c->under, "%s", under);
		}
	}
	/* sorted by tick, the file's order kept among equal ticks */
	for (k = 1; k < count; k++) {
		KpTraceCapture c = caps[k];
		for (i = k; i > 0 && by_tick(&caps[i - 1], &c) > 0; i--) caps[i] = caps[i - 1];
		caps[i] = c;
	}
	*out = caps;
	return count;
}

int64_t kp_trace_compare_pixels(const KpTraceCapture *cap, KpTracePixel pixel, void *ud, int64_t x0, int64_t y0,
	int64_t x1, int64_t y1, const char *dump)
{
	size_t size;
	uint8_t *theirs = kp_read_file(cap->path, &size), *mine = NULL;
	int64_t differ = 0, x, y, first[4] = { 0, 0, 0, 0 }, n = 0;
	const int64_t w = 640;
	if (!theirs) {
		printf("capture %s at %lld: cannot read %s\n", cap->name, (long long)cap->tick, cap->path);
		return 1;
	}
	if (dump) mine = (uint8_t *)malloc((size_t)((x1 - x0 + 1) * (y1 - y0 + 1)) + 1);
	for (y = y0; y <= y1; y++) {
		for (x = x0; x <= x1; x++) {
			int64_t at = y * w + x;
			int v = pixel(ud, x, y), o = at >= 0 && (size_t)at < size ? theirs[at] : -1;
			if (mine) mine[n++] = (uint8_t)(v >= 0 ? v : o);
			if (v != -1 && v != o
				&& !(x >= cap->cursor[0] && x <= cap->cursor[2] && y >= cap->cursor[1] && y <= cap->cursor[3])) {
				if (differ++ == 0) {
					first[0] = x;
					first[1] = y;
					first[2] = v == -2 ? -1 : v;
					first[3] = o;
				}
			}
		}
	}
	if (differ > 0) {
		printf("capture %s at %lld: %lld pixels differ, the first at (%lld, %lld): Koprulu %lld, the original %lld\n",
			cap->name, (long long)cap->tick, (long long)differ, (long long)first[0], (long long)first[1], (long long)first[2],
			(long long)first[3]);
		if (dump && mine) {
			char path[1024];
			FILE *f;
			snprintf(path, sizeof path, "%s/%s.koprulu.raw", dump, cap->name);
			if ((f = fopen(path, "wb"))) {
				fwrite(mine, 1, (size_t)n, f);
				fclose(f);
			}
		}
	}
	free(mine);
	free(theirs);
	return differ;
}
