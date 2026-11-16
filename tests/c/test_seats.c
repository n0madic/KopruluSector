/* The seats of one game (phase M, stage M6, ADR 0025): a game played on several seats, each its own simulation with its
 * own local player, gives the same shared state hash at every step. */
#include "kp_test.h"
#include "kp_ids.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIR "tests/golden/oracle/"
#define MAX_SECTIONS 64

static const char *const DATA_FILES[] = { KP_T_DATA, NULL };

typedef struct {
	int count;
	char tag[MAX_SECTIONS][5];
	uint64_t hash[MAX_SECTIONS];
} Sections;

static void take_section(void *ud, const char *tag, uint64_t hash, size_t bytes)
{
	Sections *s = (Sections *)ud;
	(void)bytes;
	if (s->count >= MAX_SECTIONS) return;
	memcpy(s->tag[s->count], tag, 4);
	s->tag[s->count][4] = 0;
	s->hash[s->count++] = hash;
}

static int hashes(KpGame *g, Sections *s, uint64_t *total)
{
	memset(s, 0, sizeof *s);
	return kp_shared_hash(g, take_section, s, total);
}

/* The sections two hashes differ in, appended to out. */
static void differing(const Sections *a, const Sections *b, KpBuf *out)
{
	int k;
	for (k = 0; k < a->count && k < b->count; k++) {
		if (a->hash[k] != b->hash[k]) kp_buf_printf(out, " %s", a->tag[k]);
	}
}

/* The first words two terrain arrays differ in, the seat's parts aside (mask: the bits compared; creep: a creep
 * tile's variant, the CRT generator's draw, as one). */
static void terrain_words(const char *what, const int64_t *x, const int64_t *y, int64_t mask, int creep, KpBuf *out)
{
	int k, shown = 0;
	for (k = 0; k < KP_TILES && shown < 4; k++) {
		int64_t p = x[k] & mask, q = y[k] & mask;
		if (creep && (p & 0x7ff0) == 0x10 && (q & 0x7ff0) == 0x10) continue;
		if (p != q) {
			kp_buf_printf(out, " [%s %d: %llx %llx]", what, k, (unsigned long long)x[k], (unsigned long long)y[k]);
			shown++;
		}
	}
}

/* The seat's sprites of a world (the fog ghosts', the cursor's). */
static void seat_sprites(KpImages *w, unsigned char *seat)
{
	int32_t t, guard;
	memset(seat, 0, KP_SPRITES);
	for (t = w->active_fog.head, guard = 0; t >= 0 && t < KP_FOG && guard < KP_FOG; t = w->fog.next[t], guard++) {
		if (w->fog.sprite[t] >= 0 && w->fog.sprite[t] < KP_SPRITES) seat[w->fog.sprite[t]] = 1;
	}
	if (w->cursor >= 0 && w->cursor < KP_LONE && w->thg.sprite[w->cursor] >= 0) seat[w->thg.sprite[w->cursor]] = 1;
}

/* The shared sprites in the rows' order, at most n: their slots. */
static int shared_sprites(KpImages *w, const unsigned char *seat, int32_t *out, int n)
{
	int32_t r, s, guard;
	int k = 0;
	for (r = 0; r < KP_ROWS; r++) {
		for (s = w->rows[r].head, guard = 0; s >= 0 && s < KP_SPRITES && guard < KP_SPRITES; s = w->spr.next[s], guard++) {
			if (!seat[s] && k < n) out[k++] = s;
		}
	}
	return k;
}

/* The first shared sprites of a without a twin in b (the same id, owner and place: a row's order is the seat's) or
 * whose fields or images differ from their twin's, the seat's parts aside (the selection's flags, a Psi field's
 * visibility and hidden flag, the drawing). */
static void sprite_diff(KpImages *wa, KpImages *wb, KpBuf *out)
{
	static unsigned char sa[KP_SPRITES], sb[KP_SPRITES], used[KP_SPRITES];
	static int32_t la[KP_SPRITES], lb[KP_SPRITES];
	int na, nb, k, j, shown = 0;
	seat_sprites(wa, sa);
	seat_sprites(wb, sb);
	memset(used, 0, sizeof used);
	na = shared_sprites(wa, sa, la, KP_SPRITES);
	nb = shared_sprites(wb, sb, lb, KP_SPRITES);
	if (na != nb) kp_buf_printf(out, " [sprites %d %d]", na, nb);
	for (k = 0; k < na && shown < 3; k++) {
		int32_t x = la[k], y = -1, i, m, guard;
		int bad = 0, psi = wa->spr.id[x] == 321;
		for (j = 0; j < nb && y < 0; j++) {
			int32_t c = lb[j];
			if (!used[c] && wb->spr.id[c] == wa->spr.id[x] && wb->spr.owner[c] == wa->spr.owner[x]
				&& wb->spr.x[c] == wa->spr.x[x] && wb->spr.y[c] == wa->spr.y[x]) {
				y = c;
				used[c] = 1;
			}
		}
		if (y < 0) {
			kp_buf_printf(out, " [sprite id %d at %d,%d: none in b]", (int)wa->spr.id[x], (int)wa->spr.x[x], (int)wa->spr.y[x]);
			shown++;
			continue;
		}
#define SPR_DIFF(name, mask) \
		if (!bad && (wa->spr.name[x] & (mask)) != (wb->spr.name[y] & (mask))) { \
			kp_buf_printf(out, " [sprite id %d at %d,%d %s: %lld %lld]", (int)wa->spr.id[x], (int)wa->spr.x[x], \
				(int)wa->spr.y[x], #name, (long long)wa->spr.name[x], (long long)wb->spr.name[y]); \
			bad = 1; \
		}
		SPR_DIFF(visibility, psi ? 0 : -1) SPR_DIFF(elevation, -1) SPR_DIFF(flags, psi ? ~0x2f : ~0x0f)
		SPR_DIFF(width, -1) SPR_DIFF(height, -1)
#undef SPR_DIFF
		i = wa->spr.images[x].head;
		m = wb->spr.images[y].head;
		for (guard = 0; !bad && guard < KP_IMAGES; guard++) {
			while (i >= KP_IMG_MAIN) i = wa->img.next[i];
			while (m >= KP_IMG_MAIN) m = wb->img.next[m];
			if (i < 0 || m < 0) {
				if ((i < 0) != (m < 0)) {
					kp_buf_printf(out, " [sprite id %d images end: %d %d]", (int)wa->spr.id[x], i, m);
					bad = 1;
				}
				break;
			}
#define IMG_DIFF(name) \
			if (!bad && wa->img.name[i] != wb->img.name[m]) { \
				kp_buf_printf(out, " [sprite id %d at %d,%d image %d (id %d) %s: %lld %lld]", (int)wa->spr.id[x], \
					(int)wa->spr.x[x], (int)wa->spr.y[x], guard, (int)wa->img.id[i], #name, (long long)wa->img.name[i], \
					(long long)wb->img.name[m]); \
				bad = 1; \
			}
			IMG_DIFF(id) IMG_DIFF(dir) IMG_DIFF(flags) IMG_DIFF(xoff) IMG_DIFF(yoff) IMG_DIFF(header) IMG_DIFF(pc)
			IMG_DIFF(ret) IMG_DIFF(anim) IMG_DIFF(wait) IMG_DIFF(frameset) IMG_DIFF(frame) IMG_DIFF(x) IMG_DIFF(y)
			IMG_DIFF(grp)
#undef IMG_DIFF
			i = wa->img.next[i];
			m = wb->img.next[m];
		}
		if (bad) shown++;
	}
}

static void explain(KpGame *a, KpGame *b, KpBuf *out)
{
	int k, shown = 0;
	int64_t n;
	for (k = 0; k < KP_SCALAR_COUNT; k++) {
		if (a->scalar[k] != b->scalar[k]) {
			kp_buf_printf(out, " [scalar %s: %lld %lld]", KP_SCALARS[k].name, (long long)a->scalar[k],
				(long long)b->scalar[k]);
		}
	}
	for (n = 0; n <= KP_FLINGIES && shown < 6; n++) {
#define UNIT_DIFF(name) \
		if (strcmp(#name, "sprite") != 0 && !kp_unit_view_field(#name) && a->u.name[n] != b->u.name[n] && shown < 6) { \
			kp_buf_printf(out, " [unit %lld %s: %lld %lld]", (long long)n, #name, (long long)a->u.name[n], \
				(long long)b->u.name[n]); \
			shown++; \
		}
		KP_UNIT_FIELDS(UNIT_DIFF)
#undef UNIT_DIFF
	}
	terrain_words("tiles", a->terrain.tiles, b->terrain.tiles, ~(int64_t)0x40000000, 0, out); /* the creep shown */
	terrain_words("raw", a->terrain.raw_tiles, b->terrain.raw_tiles, -1, 1, out);
	terrain_words("backup", a->terrain.creep_backup, b->terrain.creep_backup, -1, 0, out);
	sprite_diff(a->w, b->w, out);
}

static const KpJson *golden(KpT *T, const char *kind, const char *name)
{
	char path[512];
	snprintf(path, sizeof path, DIR "%s/%s.json", kind, name);
	return kp_t_golden(T, path);
}

/* The interface events every seat of a network game has (the game's pauses, the alliances' dialog, the briefing): the
 * other events are the recording seat's own (its selection, marks, cursor, screen, CRT draws, placement). */
static int game_wide(int kind)
{
	return kind == KP_RUN_PAUSED || kind == KP_RUN_MENU_PAUSE || kind == KP_RUN_ALLIANCE || kind == KP_RUN_OBJECTIVES;
}

/* The saved fields that hold both seat and shared state, which the shared walk takes in a canonical form (kp_save.c,
 * kp_shared_hash): the scalars (the seat's screen, placement, marker and command player among them), the units'
 * sprites and a Pylon's Psi field (by key), the tiles' flags (the creep shown) and words (a creep tile's variant), and
 * the images world's pools, rows and free lists (a fog ghost's sprite and the selection's images in the shared pools).
 * A name ending in '.' covers its struct's fields. */
static const char *const CANONICAL[] = {
	"SCAL/g->scalar", "SCAL/g->scalar_present", "UNIT/g->u.sprite", "UNIT/g->u.union_0xd0", "TERR/t->tiles",
	"TERR/t->raw_tiles", "WRLD/w->spr.", "WRLD/w->img.", "WRLD/w->rows", "WRLD/&w->free_sprites", "WRLD/&w->free_images",
	"WRLD/w->thg.sprite",
};

static int canonical(const char *key)
{
	size_t k;
	for (k = 0; k < sizeof CANONICAL / sizeof CANONICAL[0]; k++) {
		size_t n = strlen(CANONICAL[k]);
		if (CANONICAL[k][n - 1] == '.' ? strncmp(key, CANONICAL[k], n) == 0 : strcmp(key, CANONICAL[k]) == 0) return 1;
	}
	return 0;
}

/* The saved fields two seats' snapshots differ in, the seat's and the canonical ones aside, appended to out once each
 * (seen: the names so far). */
static void seat_field_diff(KpT *T, KpGame *a, KpGame *b, KpBuf *seen, KpBuf *out)
{
	KpSnapshotField *fa = NULL, *fb = NULL;
	size_t na = 0, nb = 0, size = kp_snapshot_size(a), k;
	uint8_t *sa = (uint8_t *)malloc(size), *sb = (uint8_t *)malloc(size);
	KpError err = { "" };
	kp_t_ok(T, sa && sb && kp_snapshot_size(b) == size, "snapshots of one size");
	kp_t_ok(T, kp_snapshot(a, sa, size, &err) == 0 && kp_snapshot(b, sb, size, &err) == 0, "snapshot: %s", err.msg);
	kp_t_ok(T, kp_snapshot_fields(a, &fa, &na) == 0 && kp_snapshot_fields(b, &fb, &nb) == 0, "out of memory");
	kp_t_ok(T, na == nb, "the seats' snapshots walk %lu and %lu fields", (unsigned long)na, (unsigned long)nb);
	for (k = 0; k < na; k++) {
		char key[160];
		if (fa[k].seat || memcmp(sa + fa[k].offset, sb + fb[k].offset, fa[k].bytes) == 0) continue;
		snprintf(key, sizeof key, "%s/%s", fa[k].tag ? fa[k].tag : "-", fa[k].name);
		if (canonical(key)) continue;
		snprintf(key, sizeof key, " %s/%s:%d", fa[k].tag ? fa[k].tag : "-", fa[k].name, fa[k].line);
		if (strstr(kp_buf_text(seen), key)) continue;
		kp_buf_printf(seen, "%s", key);
		kp_buf_printf(out, "%s", key);
	}
	free(fa);
	free(fb);
	free(sa);
	free(sb);
}

/* A golden run as a network game's seats (the multiplayer byte set: no seat's interface ends a wait): the recording
 * seat with its interface events and one seat per other human or computer player (the next player when there is none)
 * with only the game-wide ones, stepped together: the first step whose shared hashes differ, with the sections whose
 * hashed bytes differ and their detail, appended to bad. */
static void seats(KpT *T, const KpJson *g, const char *name, const KpOracleOpts *opts, KpBuf *seen, KpBuf *fields,
	KpBuf *bad)
{
	KpGoldenRun *seat[8];
	int64_t step, lp, player[8];
	int n = 1, k, q;
	seat[0] = kp_t_golden_run(T, g, opts, 0);
	lp = seat[0]->game.g->local_player;
	for (q = 0; q < 8; q++) {
		int64_t ctrl = kp_controller(seat[0]->game.g, q);
		if (q != lp && (ctrl == KP_CTRL_HUMAN || ctrl == KP_CTRL_COMPUTER)) player[n++] = q;
	}
	if (n == 1) player[n++] = (lp + 1) % 8;
	player[0] = lp;
	for (k = 0; k < n; k++) {
		KpGoldenRun *r = k == 0 ? seat[0] : (seat[k] = kp_t_golden_run(T, g, opts, 0));
		r->game.g->setup.multiplayer = 1;
		if (k > 0) {
			int e, kept = 0;
			kp_set_local_player(r->game.g, player[k]);
			for (e = 0; e < r->replay.ui_count; e++) {
				if (game_wide(r->replay.ui[e].kind)) r->replay.ui[kept++] = r->replay.ui[e];
			}
			r->replay.ui_count = kept;
		}
	}
	for (step = 1; step <= seat[0]->replay.last; step++) {
		uint64_t first = 0;
		for (k = 0; k < n; k++) {
			uint64_t h = 0;
			kp_runner_to(&seat[k]->run, step);
			if (kp_shared_hash(seat[k]->game.g, NULL, NULL, &h) != 0) {
				kp_buf_printf(bad, "%s%s step %lld: out of memory", bad->size ? ", " : "", name, (long long)step);
				return;
			}
			if (k == 0) {
				first = h;
			} else if (h != first) {
				Sections sa, sb;
				if (hashes(seat[0]->game.g, &sa, NULL) != 0 || hashes(seat[k]->game.g, &sb, NULL) != 0) return;
				kp_buf_printf(bad, "%s%s step %lld (players %lld, %lld):", bad->size ? ", " : "", name, (long long)step,
					(long long)lp, (long long)player[k]);
				differing(&sa, &sb, bad);
				explain(seat[0]->game.g, seat[k]->game.g, bad);
				return;
			}
		}
	}
	for (k = 1; k < n; k++) seat_field_diff(T, seat[0]->game.g, seat[k]->game.g, seen, fields);
}

static void seat_runs(KpT *T, const char *kind, const KpOracleOpts *opts, KpBuf *bad)
{
	const KpJson *index;
	const unsigned char *mine;
	char path[512];
	int k;
	KpBuf *seen = kp_t_buf(T), *fields = kp_t_buf(T);
	snprintf(path, sizeof path, DIR "%s/index.json", kind);
	index = kp_t_golden(T, path);
	snprintf(path, sizeof path, DIR "%s/%%s.json", kind);
	mine = kp_t_mine_goldens(T, index, path);
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), "");
		int mark = kp_t_mark(T);
		if (!mine[k]) continue;
		seats(T, golden(T, kind, name), name, opts, seen, fields, bad);
		kp_t_release(T, mark);
	}
	if (fields->size) {
		kp_buf_printf(bad, "%sthe seats' snapshots differ in fields neither the seat's nor the shared walk's canonical:%s",
			bad->size ? ", " : "", kp_buf_text(fields));
	}
}

static void custom_games_share_the_state(KpT *T)
{
	const char *root = getenv("KP_TEST_ORIGINAL");
	KpOracleOpts opts;
	char maps[1024];
	KpBuf *bad;
	if (!kp_t_ready(T, "test_seats", DATA_FILES)) return;
	if (!root || !root[0]) kp_t_skip(T, "no KP_TEST_ORIGINAL (the custom games' maps)");
	snprintf(maps, sizeof maps, "%s/maps", root);
	kp_oracle_opts(&opts);
	opts.maps = maps;
	bad = kp_t_buf(T);
	seat_runs(T, "custom", &opts, bad);
	kp_t_ok(T, bad->size == 0, "the seats' states differ: %s", kp_buf_text(bad));
}

static void scenarios_share_the_state(KpT *T)
{
	KpBuf *bad;
	if (!kp_t_ready(T, "test_seats", DATA_FILES)) return;
	bad = kp_t_buf(T);
	seat_runs(T, "scenarios", NULL, bad);
	kp_t_ok(T, bad->size == 0, "the seats' states differ: %s", kp_buf_text(bad));
}

/* One change of a field's bits and back: whether the shared hash moved (the hash back to base after it, or bad). */
#define CHANGE(what, lvalue, bits, shared) \
	do { \
		uint64_t moved = 0, back = 0; \
		(lvalue) ^= (bits); \
		kp_t_ok(T, kp_shared_hash(g, NULL, NULL, &moved) == 0, "out of memory"); \
		(lvalue) ^= (bits); \
		kp_t_ok(T, kp_shared_hash(g, NULL, NULL, &back) == 0 && back == base, "%s: the hash back", what); \
		if ((moved != base) != (shared)) kp_buf_printf(bad, " %s %s", what, (shared) ? "(shared) kept it" : "(seat) moved it"); \
	} while (0)

/* A change of a seat's field leaves the shared hash, a change of a shared field moves it (ADR 0025, stage M6). */
static void seat_fields_leave_the_shared_hash(KpT *T)
{
	KpOracleGame *game;
	KpGame *g;
	KpImages *w;
	KpBuf *bad;
	uint64_t base = 0;
	int64_t n;
	int32_t s, i;
	if (!kp_t_ready(T, "test_seats", DATA_FILES)) return;
	game = kp_t_game(T, "scenarios/marine_group", 1, 0, NULL);
	g = game->g;
	w = g->w;
	n = g->lists[KP_LIST_ACTIVE].head;
	kp_t_ok(T, n != 0, "a unit");
	s = (int32_t)kp_unit_sprite(g, n);
	i = w->spr.main[s];
	kp_t_ok(T, kp_shared_hash(g, NULL, NULL, &base) == 0, "out of memory");
	bad = kp_t_buf(T);
	CHANGE("local selection", g->local_selection[0], 1, 0);
	CHANGE("circle colours", g->circle_colors[0], 1, 0);
	CHANGE("Psi fields shown", g->power.fields_shown, 1, 0);
	CHANGE("screen x", g->scalar[KP_SCREEN_X], 1, 0);
	CHANGE("placement type", g->scalar[KP_PLACEMENT_TYPE], 1, 0);
	CHANGE("command player", g->scalar[KP_COMMAND_PLAYER], 1, 0);
	CHANGE("remembered megatile", g->terrain.megatiles[0], 1, 0);
	CHANGE("creep border", g->terrain.creep_border[0], 1, 0);
	CHANGE("creep shown", g->terrain.tiles[0], 0x40000000, 0);
	if (g->crt) CHANGE("CRT draw", g->crt->holdrand, 1, 0);
	CHANGE("unit view", g->u.attack_notify_timer[n], 1, 0);
	CHANGE("selected mark", w->spr.flags[s], KP_SF_SELECTED, 0);
	CHANGE("flash timer", w->spr.sel_timer[s], 1, 0);
	CHANGE("image bounds", w->img.x[i], 1, 0);
	CHANGE("query stamp", g->spatial.stamp, 1, 0);
	CHANGE("requirement error", g->scalar[KP_REQUIREMENT_ERROR], 1, 0);
	CHANGE("requirement cache", g->req_offsets_ready[0], 1, 0);
	CHANGE("hit points", g->u.hp[n], 1, 1);
	CHANGE("frame", g->scalar[KP_FRAME], 1, 1);
	CHANGE("generator", g->rng->state, 1, 1);
	CHANGE("tile flags", g->terrain.tiles[0], 1, 1);
	CHANGE("sprite x", w->spr.x[s], 1, 1);
	CHANGE("image frame", w->img.frame[i], 1, 1);
	CHANGE("sprite hidden", w->spr.flags[s], KP_SF_HIDDEN, 1);
	kp_t_ok(T, bad->size == 0, "the shared hash and the seat's fields:%s", kp_buf_text(bad));
}

static const KpTest TESTS[] = {
	{ "custom_games_share_the_state", custom_games_share_the_state, 4 },
	{ "scenarios_share_the_state", scenarios_share_the_state, 4 },
	{ "seat_fields_leave_the_shared_hash", seat_fields_leave_the_shared_hash, 0 },
};

KP_SUITE(kp_suite_seats, "test_seats", TESTS);
