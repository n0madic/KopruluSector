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
#include "kp_flow_files.h"
#include "kp_gametype.h"
#include "kp_ids.h"
#include "kp_net_sock.h"
#include "kp_netplay.h"
#include "kp_netsave.h"
#include "kp_text.h"

#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RESULT_DEFEAT 2 /* KP_EVENT_GAME_RESULT's */
#define RESULT_VICTORY 3
#define VIEW_ROWS_ORIGINAL 400 /* the original's game view: 640 x 400 */
#define LOG_LINE 512

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
void kp_flow_say(KpFlow *f, const char *fmt, ...)
{
	char line[LOG_LINE];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	f->host.log(f->host.ctx, line);
}

static double now(KpFlow *f) { return f->host.now(f->host.ctx); }

static const char MISSIONS[] = "missions/"; /* a campaign mission's key: "missions/" + its campaign and name */

/* A language's text over the campaign mission just loaded (kp_locale_mission: its map's strings from the data), which
 * kp_host_game_text takes; the base's, a map the data lacks or a refused one leaves the mission's, with one line. */
static void mission_text(KpFlow *f, const KpDataMission *entry)
{
	const char *id = f->flags->locale, *name = entry->key + sizeof MISSIONS - 1;
	KpHostGame *hg = &f->session->host;
	KpLocaleFound found;
	uint8_t *strs = NULL;
	size_t size = 0;
	int k, rc;
	KpError e;
	if (id[0] == 0 || strcmp(id, KP_LOCALE_BASE) == 0 || strncmp(entry->key, MISSIONS, sizeof MISSIONS - 1) != 0) return;
	memset(&found, 0, sizeof found);
	e.msg[0] = 0;
	rc = kp_locale_mission(&f->data->raw, id, name, &hg->data.mission, &found, &strs, &size, NULL, &e);
	for (k = 0; k < found.problem_count; k++) kp_flow_say(f, "language %s: %s; that string stays the mission's", id, found.problems[k]);
	kp_locale_found_free(&found);
	if (rc == 0) kp_flow_say(f, "language %s: the data has no map of %s, the mission's own text", id, name);
	else if (rc < 0) kp_flow_say(f, "language %s: %s; the mission's own text", id, e.msg);
	else if (kp_host_game_text(hg, strs, size, &e) != 0) kp_flow_say(f, "language %s: %s: %s", id, name, e.msg);
}

/* A language's speech over the campaign mission just loaded (ADR 0019, stage C2): the TIME of its map's triggers, which
 * a transmission waits for its lines (the audio plays them in place of the scenario's WAVs). A map the data lacks or
 * refuses leaves the mission's own, with one line; so does each record that differs in more than TIME, named unless
 * the profile excepts the mission (defs/l10n/locales.json's speech: Remastered's own fixes). A success says "speech of". */
static void mission_speech(KpFlow *f, const KpDataMission *entry)
{
	const char *id = f->flags->speech, *name = entry->key + sizeof MISSIONS - 1;
	KpHostSpeech sp;
	KpError e;
	int rc;
	if (id[0] == 0 || strcmp(id, KP_LOCALE_BASE) == 0 || strncmp(entry->key, MISSIONS, sizeof MISSIONS - 1) != 0) return;
	if ((rc = kp_host_game_speech(&f->session->host, &f->data->raw, id, name, &sp, &e)) < 0) {
		kp_flow_say(f, "speech %s: %s; the mission's own triggers", id, e.msg);
		return;
	}
	if (rc == 0) {
		kp_flow_say(f, "speech %s: the data has no map of %s, the mission's own triggers", id, name);
		return;
	}
	if (sp.differ && !sp.excepted) {
		kp_flow_say(f, "speech %s: %s: %d records differ from the mission's in more than TIME, the first %s: they keep the "
			"mission's (defs/l10n/locales.json's speech lists a mission's own fixes)", id, name, sp.differ, sp.first);
	} else if (!sp.differ && sp.excepted) {
		kp_flow_say(f, "speech %s: %s: defs/l10n/locales.json's speech excepts it, but its triggers differ in TIME alone", id, name);
	}
	kp_flow_say(f, "speech of %s: %s: %d actions' TIME", id, name, sp.times);
}

static uint32_t now_ms(KpFlow *f) { return (uint32_t)(uint64_t)(now(f) * 1000); }

/* The mission shown: a catalog mission or test scenario, or the custom map's (KP_FLOW_CUSTOM); NULL none. */
static const KpDataMission *current_entry(const KpFlow *f)
{
	if (f->current == KP_FLOW_CUSTOM) return &f->custom_entry;
	return f->current >= 0 ? &f->data->missions[f->current] : NULL;
}

static const char *current_name(const KpFlow *f)
{
	const KpDataMission *e = current_entry(f);
	return e ? e->name : "?";
}

static void stop(KpFlow *f, const char *what)
{
	kp_flow_say(f, "play %s: stopped: %s", current_name(f), what);
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
		kp_flow_say(f, "no Remastered console art in the data: the original's console");
	} else if (kp_ui_set_console_art(ui, f->console_art, f->console_art_size) != 0) {
		kp_flow_say(f, "the console art is not current: the original's console; repack the install again (kp_convert repack)");
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

/* A view's top-left (*x, *y) kept on map m: with a console, the interface's limits. */
static void clamp_view(const KpFlow *f, const KpMission *m, int console, int64_t *x, int64_t *y)
{
	int64_t max_x, max_y, mw = m->width, mh = m->height;
	max_x = mw * KP_WORLD_TILE - f->world_w;
	max_y = mh * KP_WORLD_TILE - f->world_h;
	if (console) {
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
	if (*x > max_x) *x = max_x;
	if (*y > max_y) *y = max_y;
	if (*x < 0) *x = 0;
	if (*y < 0) *y = 0;
}

void kp_flow_clamp_camera(KpFlow *f)
{
	if (!f->world) return;
	clamp_view(f, kp_session_mission(f->session), f->playing && f->hud_on, &f->cam_x, &f->cam_y);
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
	if (kp_session_guard(f->session, end_game, f->ui) != 0) kp_flow_say(f, "play: the console's end failed: %s", f->session->error);
}

/* --- the replay of a game (atlas P28; docs/formats/rep.md) ---
 * The score's Save Replay names a custom game's replay (the save-replay dialog's files below); it stays disabled after
 * a campaign game, as in the original, where Load Replay's Export names one (the host's file picker,
 * kp_flow_import_replay and export_replay below; docs/phase-replay.md, RP4). */


/* A name in the header's encoding (windows-1252, as the original's records hold it): UTF-8 converted, other bytes
 * kept. */
static void legacy_name(const char *text, char *out, size_t size)
{
	if (!text) {
		out[0] = 0;
	} else if (kp_text_valid_utf8((const uint8_t *)text, strlen(text))) {
		(void)kp_text_to_legacy(text, out, size);
	} else {
		snprintf(out, size, "%s", text);
	}
}

/* The recorded game's header with the names StartGame 0x443f40 gives its records: the logged-in ID's (Anonymous
 * without one) and AssignDefaultComputerPlayerNames 0x4074a0's; a network game's humans keep the lobby's names (the
 * setup's, kp_session_replay_game) and its header the game's name; and its tail: what the game started from. */
static void replay_start(KpFlow *f, const KpSessionStart *st)
{
	KpSession *s = f->session;
	KpRepGame game;
	char player[KP_REP_GAME_NAME_SIZE];
	int p, network = f->net_game && f->netplay && f->netplay->game_on;
	f->replay_saved = 0;
	if (!s->rec) return;
	kp_session_replay_game(s, st->seed, &game);
	if (network) {
		snprintf(player, sizeof player, "%s", f->netplay->game.header.name);
		game.player_name = player;
	} else if (f->profile && f->profile->name[0]) {
		legacy_name(f->profile->name, player, sizeof player);
		game.player_name = player;
	}
	for (p = 0; p < 8; p++) {
		KpRepPlayer *r = &game.players[p];
		if (network && r->type == KP_CTRL_HUMAN) {
			/* the lobby's name, as kp_session_replay_game took it */
		} else if (r->type == KP_CTRL_HUMAN) {
			snprintf(r->name, sizeof r->name, "%s", game.player_name);
		} else {
			legacy_name(kp_ui_default_player_name(f->ui, (int)s->g->expansion, st->mission_index, p,
				(int)s->g->setup.color[p], r->type, r->race), r->name, sizeof r->name);
		}
	}
	kp_session_replay_header(s, &game);
	if (!f->rep_tail && !(f->rep_tail = (KpRepTail *)malloc(sizeof *f->rep_tail))) return;
	memset(f->rep_tail, 0, sizeof *f->rep_tail);
	f->rep_tail->version = KP_REP_TAIL_VERSION;
	f->rep_tail->sim_revision = KP_REP_SIM_REVISION;
	if (f->identity) memcpy(f->rep_tail->data_hash, f->identity->source_hash, sizeof f->rep_tail->data_hash);
	f->rep_tail->has_carry = st->has_carry;
	if (st->has_carry) f->rep_tail->carry = st->carry;
	f->rep_tail->cheats = st->cheats;
	f->rep_tail->objective_count = st->objective_count;
	for (p = 0; p < 8; p++) f->rep_tail->objectives[p] = st->objectives[p];
}

/* EndGame 0x443db0's LastReplay of a game that started and was recorded, once: <pref>replays/LastReplay.rep. The
 * session starts before the ready room, but the original's game begins at its Start: a game whose console never came
 * (the briefing cancelled, or the program left there) writes none and keeps the last game's file. */
static void save_last_replay(KpFlow *f)
{
	KpSession *s = f->seats.count ? f->seats.session[0] : f->session; /* --seats: the player's seat records it */
	KpRep rep;
	KpError err;
	uint8_t *bytes = NULL;
	size_t size;
	char dir[1100], path[1100];
	if (f->replay_saved || !f->hud_on || !s || !s->g || s->failed || !s->rec || !s->rec->recording) return;
	f->replay_saved = 1;
	if (f->rep_tail) {
		f->rep_tail->loads = s->rec_loads;
		f->rep_tail->lost = s->rec_lost;
	}
	if (kp_session_replay_file(s, f->rep_tail, &rep, &err) != 0 || kp_rep_write(&rep, &bytes, &size, &err) != 0) {
		kp_rep_free(&rep);
		kp_flow_say(f, "replay: %s", err.msg);
		return;
	}
	kp_rep_free(&rep);
	pref_path(f, REPLAY_DIR, dir, sizeof dir);
	snprintf(path, sizeof path, "%s/%s", dir, LAST_REPLAY);
	if (kp_flow_make_dir(dir) != 0) {
		kp_flow_say(f, "replay: cannot create %s: %s", dir, strerror(errno));
	} else if (kp_write_file_atomic(path, bytes, size, &err) != 0) {
		kp_flow_say(f, "replay: %s", err.msg);
	} else {
		kp_flow_say(f, "replay: %s, %u frames", path, (unsigned)kp_scalar(s->g, KP_FRAME));
	}
	free(bytes);
	f->host.sync_files(f->host.ctx);
}

static void free_seat_views(KpFlow *f);

static void net_game_end(KpFlow *f);

/* --- the viewers of a network game (kp_watch.h) --- */

static int replay_room(KpFlow *f, char *why, size_t why_size);
static int start_replay(KpFlow *f, const char *name, char *why, size_t why_size);

/* The viewers' socket: open, or opened at port (0: one the system picks); NULL with a line said when it cannot be */
static KpNetSock *watch_socket(KpFlow *f, int port)
{
	KpNetSockConfig c;
	char why[256];
	if (f->watch_sock) return f->watch_sock;
	memset(&c, 0, sizeof c);
	c.single = 1;
	c.port = (uint16_t)port;
	c.map_self = -1;
	f->watch_sock_failed = 0;
	if (!(f->watch_sock = kp_net_sock_open(&c, why, sizeof why))) kp_flow_say(f, "watch: no socket: %s", why);
	else kp_flow_say(f, "watch: UDP port %u", (unsigned)kp_net_sock_port(f->watch_sock, KP_NET_SOCKET_LIST));
	return f->watch_sock;
}

/* The network game this seat created served to its viewers from its start (a resumed game's once its save loaded):
 * the replay file at the start without its record, the record after each step (the session marks it) as it stood
 * --watch-delay seconds before. */
static void watch_serve(KpFlow *f)
{
	KpSession *s = f->session;
	KpRep rep;
	KpError err;
	uint8_t *bytes = NULL;
	size_t size;
	double seconds = f->flags->watch_delay < 0 ? KP_WATCH_DELAY_SECONDS : f->flags->watch_delay;
	uint32_t delay;
	int port = f->flags->watch_port ? f->flags->watch_port : KP_WATCH_PORT;
	if (!s || !s->rec) return;
	if (f->flags->watch_port < 0) return; /* --watch-port off: the hosted games are served to no viewer */
	if (s->rec_lost) { /* a resumed game whose save kept no record: its replay starts at the load, not at the map's start */
		kp_flow_say(f, "watch: the game's record is lost, it is not served");
		return;
	}
	if (!watch_socket(f, port)) return;
	if (kp_net_sock_port(f->watch_sock, KP_NET_SOCKET_LIST) != port) /* the socket a viewer of this program opened */
		kp_flow_say(f, "watch: served on UDP port %u, not %d", (unsigned)kp_net_sock_port(f->watch_sock, KP_NET_SOCKET_LIST), port);
	if (kp_session_replay_file(s, f->rep_tail, &rep, &err) != 0) {
		kp_flow_say(f, "watch: %s", err.msg);
		return;
	}
	rep.record_size = 0; /* the record is the server's part 1, all of it (a resumed game's restored record too) */
	if (kp_rep_write(&rep, &bytes, &size, &err) != 0) {
		kp_rep_free(&rep);
		kp_flow_say(f, "watch: %s", err.msg);
		return;
	}
	kp_rep_free(&rep);
	if (f->watch_serving) kp_watch_server_free(&f->watch_server);
	f->watch_serving = 0;
	delay = kp_watch_delay_frames(seconds, kp_clock_step_ms(&s->clock));
	if (kp_watch_server_init(&f->watch_server, f->netplay->game.seed ^ now_ms(f), bytes, size, delay) == 0) {
		f->watch_serving = 1;
		s->watch_server = &f->watch_server;
		kp_flow_say(f, "watch: serving the game, %u frames behind", (unsigned)delay);
	} else {
		kp_flow_say(f, "watch: out of memory");
	}
	free(bytes);
}

/* The served game's end (its session freed): the whole record published, final; the server answers on. */
static void watch_serve_end(KpFlow *f)
{
	KpSession *s = f->session;
	if (!s || !s->watch_server) return;
	if (kp_watch_server_end(s->watch_server, s->rec, s->g ? (uint32_t)kp_scalar(s->g, KP_FRAME) : 0) != 0)
		kp_flow_say(f, "watch: out of memory at the game's end");
	s->watch_server = NULL;
}

typedef struct {
	KpFlow *f;
	KpNetAddr to;
} WatchReply;

static void watch_send(void *ud, const uint8_t *data, size_t size)
{
	WatchReply *r = (WatchReply *)ud;
	KpNetIo io = kp_net_sock_io(r->f->watch_sock);
	io.send(io.ud, KP_NET_SOCKET_LIST, &r->to, data, size);
}

static void watch_arrived(void *ud, int socket, const KpNetAddr *from, const uint8_t *data, size_t size)
{
	KpFlow *f = (KpFlow *)ud;
	WatchReply r;
	(void)socket;
	if (size <= KP_WATCH_AT_TYPE) return;
	r.f = f;
	r.to = *from;
	if (data[KP_WATCH_AT_TYPE] == KP_WATCH_REQUEST && f->watch_serving)
		kp_watch_server_receive(&f->watch_server, data, size, watch_send, &r);
	else if (data[KP_WATCH_AT_TYPE] == KP_WATCH_DATA && f->watching && f->watch_known && kp_net_addr_eq(from, &f->watch_addr))
		kp_watch_viewer_receive(&f->watch_viewer, data, size, now_ms(f));
}

static void watch_stop(KpFlow *f, const char *why)
{
	kp_flow_say(f, "watch: %s", why);
	f->watching = 0;
	kp_net_lookup_free(f->watch_lookup);
	f->watch_lookup = NULL;
	kp_watch_viewer_free(&f->watch_viewer);
}

/* The viewer's frame: the host's address looked up, the requests, the playback started once the start file came and
 * its record grown */
static void watch_view(KpFlow *f)
{
	KpWatchViewer *v = &f->watch_viewer;
	const uint8_t *bytes;
	size_t size;
	uint32_t frames;
	int whole;
	char why[KP_ERROR_SIZE];
	if (f->watch_lookup) {
		int r = kp_net_lookup_poll(f->watch_lookup, &f->watch_addr, why, sizeof why);
		if (r < 0) return;
		kp_net_lookup_free(f->watch_lookup);
		f->watch_lookup = NULL;
		if (r == 0) {
			watch_stop(f, why);
			f->host.quit(f->host.ctx); /* nothing else to show: the viewer runs without the glue */
			return;
		}
		f->watch_known = 1;
		kp_watch_viewer_init(v, now_ms(f));
		kp_flow_say(f, "watch: asking %s", kp_net_addr_text(&f->watch_addr, why, sizeof why));
	}
	{
		WatchReply r;
		r.f = f;
		r.to = f->watch_addr;
		kp_watch_viewer_poll(v, now_ms(f), watch_send, &r);
	}
	if (v->status == KP_WATCH_FAILED) {
		watch_stop(f, "no game from the host");
		if (!f->watch_game) f->host.quit(f->host.ctx);
		return;
	}
	if (f->watch_begun && !f->watch_game) { /* the playback ended before the record did (the player left it) */
		watch_stop(f, "the playback ended");
		return;
	}
	if (!f->watch_begun && (bytes = kp_watch_viewer_start(v, &size)) != NULL) {
		KpError err;
		int r = replay_room(f, why, sizeof why);
		f->watch_begun = 1;
		f->watch_start = 1;
		if (r == 0 && kp_rep_read(f->replay_play, bytes, size, NULL, &err) != 0) {
			snprintf(why, sizeof why, "the start of the replay: %s", err.msg);
			r = -1;
		}
		if (r != 0 || start_replay(f, "of the network game", why, sizeof why) != 0) {
			f->watch_start = 0;
			watch_stop(f, why);
			f->host.quit(f->host.ctx);
			return;
		}
	}
	if (f->watch_game && f->session && f->session->g && kp_watch_viewer_take(v, &bytes, &size, &frames, &whole)) {
		if (kp_session_watch_grow(f->session, bytes, size, frames, whole) != 0) {
			watch_stop(f, f->session->error);
			return;
		}
		if (whole) watch_stop(f, v->oversize ? "the record grew past what a replay holds: the game ends where it came to"
			: v->status == KP_WATCH_GONE ? "the host is gone: the game ends where it came to" : "the game ended");
	}
}

/* The viewers' part of a frame: what arrived on their socket, the viewer's turn */
static void watch_frame(KpFlow *f)
{
	/* a failed socket is said once: a viewer then times out (KP_WATCH_TIMEOUT_MS), a server answers no more */
	if (f->watch_sock && kp_net_sock_wait(f->watch_sock, 0, watch_arrived, f) < 0 && !f->watch_sock_failed) {
		f->watch_sock_failed = 1;
		kp_flow_say(f, "watch: the socket failed");
	}
	if (f->watching) watch_view(f);
}

static void watch_free(KpFlow *f)
{
	if (f->watching) watch_stop(f, "closed");
	if (f->watch_serving) kp_watch_server_free(&f->watch_server);
	f->watch_serving = 0;
	kp_net_sock_close(f->watch_sock);
	f->watch_sock = NULL;
}

int kp_flow_watch(KpFlow *f, const char *address, char *why, size_t why_size)
{
	char text[KP_NET_HOST + 8];
	if (f->watching || f->watch_game) {
		snprintf(why, why_size, "a viewer already");
		return -1;
	}
	if (kp_flow_glue_active(f)) { /* TODO: as the script's replay, a game started under a glue screen keeps its menus */
		snprintf(why, why_size, "the glue's screen is open");
		return -1;
	}
	if (!watch_socket(f, 0)) {
		snprintf(why, why_size, "no UDP socket");
		return -1;
	}
	/* the viewers' port when the address names none ("[" an IPv6 form is not taken: Storm's addresses are IPv4) */
	snprintf(text, sizeof text, strchr(address, ':') ? "%s" : "%s:%u", address, (unsigned)KP_WATCH_PORT);
	if (!(f->watch_lookup = kp_net_lookup_start(text))) {
		snprintf(why, why_size, "out of memory");
		return -1;
	}
	f->watching = 1;
	f->watch_known = f->watch_begun = 0;
	memset(&f->watch_viewer, 0, sizeof f->watch_viewer);
	kp_flow_say(f, "watch: %s", text);
	return 0;
}

/* The game's console, world, session and seats freed (Storm's game and the LastReplay left to leave_scene; a network
 * game's rebuild frees only these). */
static void free_scene(KpFlow *f)
{
	if (f->hud_on && f->session && f->session->g) end_console(f);
	end_hud(f);
	/* only a world that stands (a build that fails frees its own): a flow freed before its host was set has none */
	if (f->world) f->host.world_free(f->host.ctx);
	f->world = 0;
	if (f->session) {
		watch_serve_end(f);
		if (f->session->g && f->session->g->rng) f->rng_state = f->session->g->rng->state; /* the next lobby's (0x479c90) */
		kp_session_free(f->session);
		free(f->session);
		f->session = NULL;
	}
	free_seat_views(f);
	kp_seats_free(&f->seats); /* the hidden seats' sessions and the turn hub */
	if (f->ui) f->ui->game = NULL; /* a ready room's game too */
	f->result = 0;
}

/* What a scene's end ends besides the scene (its LastReplay, Storm's game, its console, world, session and seats). */
enum {
	LEAVE_PLAYING = 1 << 0, /* no game in play */
	LEAVE_REPLAY = 1 << 1,  /* the next game is the player's unless another replay is chosen */
	LEAVE_NET = 1 << 2,     /* the next game is no network game */
	LEAVE_CUSTOM = 1 << 3,  /* the custom map's archive closed (CloseMapArchive 0x469440: the file is free again) */
	LEAVE_START = 1 << 4,   /* no save loads at the next start, no network game resumes */
	LEAVE_RUN = 1 << 5      /* no campaign continuation, no start run */
};
/* A game's end (EndGame 0x443db0): the scene and what the game was */
#define LEAVE_GAME (LEAVE_PLAYING | LEAVE_REPLAY | LEAVE_NET | LEAVE_CUSTOM)

/* The one way out of a scene: its LastReplay written, Storm's game left (a network game that ends without the
 * console's exit, a failure), the scene freed, then what `what` names (LEAVE_*) cleared. */
static void leave_scene(KpFlow *f, unsigned what)
{
	save_last_replay(f);
	f->watch_game = 0;
	if (f->session) net_game_end(f);
	free_scene(f);
	if (what & LEAVE_CUSTOM) kp_flow_custom_close(f);
	if (what & LEAVE_PLAYING) f->playing = 0;
	if (what & LEAVE_REPLAY) f->playing_replay = 0;
	if (what & LEAVE_NET) f->net_game = 0;
	if (what & LEAVE_START) {
		f->load_on_start[0] = 0;
		f->net_resume = 0;
	}
	if (what & LEAVE_RUN) {
		f->has_continue = 0;
		f->has_start_run = 0;
	}
}

/* The replay's room (f->replay_play) emptied for the next one. -1 out of memory. */
static int replay_room(KpFlow *f, char *why, size_t why_size)
{
	if (!f->replay_play && !(f->replay_play = (KpRep *)calloc(1, sizeof *f->replay_play))) {
		snprintf(why, why_size, "out of memory");
		return -1;
	}
	kp_rep_free(f->replay_play);
	return 0;
}

/* f->replay_play's mission without a ready room, played from its record (name: the replay's, said) */
static int start_replay(KpFlow *f, const char *name, char *why, size_t why_size)
{
	int k = kp_flow_replay_mission(f, f->replay_play);
	if (k == -1) {
		snprintf(why, why_size, "replay %s: cannot be played", name);
		return -1;
	}
	if (k == KP_FLOW_CUSTOM && kp_flow_custom_from_replay(f, f->replay_play, why, why_size) != 0) return -1;
	kp_flow_say(f, "replay %s: %u frames", name, (unsigned)kp_rep_frames(f->replay_play));
	f->glue_room = 0;
	f->playing_replay = 1;
	return k == KP_FLOW_CUSTOM ? kp_flow_start_custom(f, why, why_size) : kp_flow_start_mission(f, k, why, why_size);
}

/* Load Replay's Ok: the replay file's mission without a ready room, played from the file at path. */
static int start_replay_file(KpFlow *f, const char *file, char *why, size_t why_size)
{
	uint16_t refusal;
	if (replay_room(f, why, why_size) != 0) return -1;
	if (kp_flow_read_replay_at(file, f->replay_play, &refusal) != 0) {
		snprintf(why, why_size, "replay %s: cannot be played", file);
		return -1;
	}
	return start_replay(f, file, why, why_size);
}

static int start_replay_game(KpFlow *f, int index, char *why, size_t why_size)
{
	const KpUiMenus *m = &f->ui->menus;
	char path[ENTRY_PATH];
	if (index < 0 || index >= m->entry_count || m->entries[index].kind != KP_UI_ENTRY_REPLAY
		|| !kp_flow_entry_path(f, m, index, path, sizeof path)) {
		snprintf(why, why_size, "no replay %d", index);
		return -1;
	}
	return start_replay_file(f, path, why, why_size);
}

int kp_flow_play_replay(KpFlow *f, const char *name, char *why, size_t why_size)
{
	char file[KP_UI_MAP_FILE], path[ENTRY_PATH];
	size_t n = strlen(name);
	if (n + sizeof REPLAY_EXT > sizeof file) {
		snprintf(why, why_size, "a replay's name is %d bytes at most", (int)(sizeof file - sizeof REPLAY_EXT));
		return -1;
	}
	snprintf(file, sizeof file, "%s%s", name, has_replay_ext(name) ? "" : REPLAY_EXT);
	pref_path(f, REPLAY_DIR "/", path, sizeof path);
	strncat(path, file, sizeof path - strlen(path) - 1);
	return start_replay_file(f, path, why, why_size);
}

int kp_flow_dev_checks(const KpAppFlags *fl)
{
	return fl->autotest || fl->cine_check[0] || fl->originals;
}

int kp_flow_scripted(const KpAppFlags *fl) { return fl->script[0] || fl->script_live[0]; }

int kp_flow_software_gpu(const KpAppFlags *fl)
{
	/* the flow copies the profile's renderer into the flags (video_flags): only the given bit tells the flag's */
	return (fl->video_given & KP_VIDEO_GIVEN_RENDERER) && fl->renderer == KP_RENDERER_GPU;
}

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
	ui->multi_mouse_scroll_speed = s->multi_mouse_scroll;
	ui->multi_key_scroll_speed = s->multi_key_scroll;
	ui->tip_index = s->tip;
	ui->cpu_throttle = (uint8_t)s->cpu_throttle;
	kp_ui_apply_volumes(ui); /* a profile's silence closes the music before its first track */
}

static void sounds(KpFlow *f);

/* A new interface over the data's (ADR 0018). */
static KpUi *new_interface(KpFlow *f, char *why, size_t why_size)
{
	return kp_interface_new_data(&f->data->ui, &f->locale, why, why_size);
}

/* The logical screen a game gets on this window (a game may narrow where a glue screen keeps 640 x 480). */
static void console_screen(const KpFlow *f, int32_t *w, int32_t *h)
{
	int32_t min_w, min_h;
	double scale;
	int mode;
	kp_layout_minimum(f->safe_w, f->safe_h, 0, &min_w, &min_h);
	kp_layout_screen(f->safe_w, f->safe_h, f->flags->scale_mode, 0, min_w, min_h, &mode, &scale, w, h);
}

/* A game's own interface (without the glue each game's console has one): the console's art, the host's calls, the
 * options. */
static KpUi *game_interface(KpFlow *f, char *err, size_t err_size)
{
	char why[256];
	KpUi *ui = new_interface(f, why, sizeof why);
	if (!ui) {
		snprintf(err, err_size, "%s", why);
		return NULL;
	}
	console_art(f, ui);
	kp_flow_interface_host(f, ui);
	kp_flow_video_to_ui(f, ui);
	options_to_ui(f, ui);
	return ui;
}

/* The console of game g over ui, on the shared canvas (the glue's) or its own, with the in-game save dialogs' list. */
static int console_init(KpFlow *f, KpHud *hud, KpUi *ui, KpCanvas *shared, KpGame *g, int from_save, char *err,
	size_t err_size)
{
	KpHudMap m;
	int32_t w, h;
	memset(&m, 0, sizeof m);
	f->host.hud_map(f->host.ctx, &m);
	m.player_colors = g->setup.color; /* the game's colours (0x5264fc) */
	console_screen(f, &w, &h);
	if (kp_hud_init(hud, ui, shared, g, &m, now_ms(f), w, h, from_save) != 0) {
		snprintf(err, err_size, "the console: %s", ui->error);
		kp_hud_free(hud); /* its own canvas */
		return -1;
	}
	kp_flow_game_saves(f, ui);
	return 0;
}

static int seat_consoles(KpFlow *f, int from_save, char *err, size_t err_size);

static int start_hud(KpFlow *f, int from_save, char *err, size_t err_size)
{
	f->fatal_held = 0; /* the new console has no fatal dialog */
	if (!f->glue_on) { /* without the glue each game's console has an interface of its own */
		KpUi *ui = game_interface(f, err, err_size);
		if (!ui) return -1;
		f->host.interface(f->host.ctx, ui);
		if (f->ui) sounds(f); /* the old interface's last commands (its music's stop at the game's end) before it goes */
		kp_ui_free(f->ui);
		f->ui = ui;
	}
	if (console_init(f, &f->hud, f->ui, f->glue_on ? &f->glue.canvas : NULL, f->session->g, from_save, err, err_size)
		!= 0) {
		return -1;
	}
	f->hud_on = 1;
	/* the game's palette from its console's first frame (a glue screen's stays until then) */
	if (f->has_screen_palette) f->host.palette(f->host.ctx, kp_palette_rgb(&f->screen_palette));
	f->game_started_at = now(f);
	return f->seats.count ? seat_consoles(f, from_save, err, err_size) : 0;
}

/* The name a game's saves carry (their native save's mission): a custom map's, else the mission's key. */
static const char *save_name(const KpFlow *f, const KpDataMission *entry)
{
	return entry == &f->custom_entry ? f->custom_key : entry->key;
}

/* The session of a mission (ADR 0018): its mission built from its files (kp_data_mission: a campaign mission's
 * scenario, a test scenario's CHK under KP_DATA_SCENARIOS), over a copy of the data set's DAT, with its CV5 and VF4;
 * NULL with err. */
static KpSession *new_session(KpFlow *f, const KpDataMission *entry, char *err, size_t err_size)
{
	KpSession *s;
	KpMission m;
	KpError e;
	uint8_t *chk = NULL;
	size_t chk_size = 0;
	const KpFiles *files = &f->data->files;
	if (entry == &f->custom_entry) {
		if (!f->custom_map || kp_data_map_mission(f->custom_map, f->custom_load, &m, &e) != 0) {
			snprintf(err, err_size, "map %s: %s", entry->name, f->custom_map ? e.msg : "none");
			return NULL;
		}
		files = &f->custom_map->files;
	} else if (!entry->directory[0]) {
		char path[160];
		snprintf(path, sizeof path, KP_DATA_SCENARIOS "%s.chk", entry->key + sizeof "scenarios/" - 1);
		if (!(chk = f->host.read(f->host.ctx, path, &chk_size))) {
			snprintf(err, err_size, "mission %s: cannot read %s", entry->name, path);
			return NULL;
		}
	}
	if (entry != &f->custom_entry && kp_data_mission(f->data, entry, chk, chk_size, &m, &e) != 0) { /* the CHK taken */
		snprintf(err, err_size, "mission %s: %s", entry->name, e.msg);
		return NULL;
	}
	if (!(s = (KpSession *)calloc(1, sizeof *s))) {
		kp_mission_free(&m);
		snprintf(err, err_size, "out of memory");
		return NULL;
	}
	if (kp_session_load_from(s, &f->data->shared, &f->data->dat, &m, save_name(f, entry), files) != 0) {
		snprintf(err, err_size, "play %s: %s", entry->name, s->error);
		kp_session_free(s);
		free(s);
		return NULL;
	}
	return s;
}

/* --- the seats (--seats, kp_seats.h) --- */

/* Before the player's seat starts: the lobby's computers made the other seats, the hub its start joins. */
static int seats_prepare(KpFlow *f, KpSessionStart *st, char *err, size_t err_size)
{
	KpSeats *s = &f->seats;
	int n = f->flags->seats;
	if (!st->has_setup) {
		snprintf(err, err_size, "seats: no lobby to seat");
		return -1;
	}
	if (kp_seats_setup(&st->setup, n, err, err_size) != 0) return -1;
	if (!(s->hub = kp_hub_new(n, 1))) {
		snprintf(err, err_size, "seats: out of memory");
		return -1;
	}
	s->count = n;
	s->shown = 0;
	s->session[0] = f->session;
	st->hub = s->hub;
	st->seat = 0;
	st->local_storm = 0;
	return 0;
}

/* After the player's seat started: the other seats' sessions of the same game (each its Storm ID's local player, none
 * recording), the hub's frames a turn at the game's speed (SetSpeedTable 0x445110), the first turns. */
static int seats_start(KpFlow *f, const KpDataMission *entry, const KpSessionStart *st, char *err, size_t err_size)
{
	KpSeats *s = &f->seats;
	int k;
	s->hub->frames_per_turn = kp_hub_frames_per_turn((int)kp_scalar(f->session->g, KP_SPEED), KP_HUB_TURNS_PER_SECOND);
	for (k = 1; k < s->count; k++) {
		KpSessionStart other = *st;
		other.seat = k;
		other.local_storm = k;
		other.record = 0;
		if (!(s->session[k] = new_session(f, entry, err, err_size))) return -1;
		if (kp_session_start(s->session[k], &other) != 0) {
			snprintf(err, err_size, "seat %d: %s", k, s->session[k]->error);
			return -1;
		}
	}
	if (kp_seats_begin(s) != 0) {
		snprintf(err, err_size, "%s", s->report);
		return -1;
	}
	kp_flow_say(f, "seats: %d seats, %d frames a turn", s->count, s->hub->frames_per_turn);
	return 0;
}

/* Each draw refreshes the remembered terrain in its view at (cam_x, cam_y) (RefreshRememberedTerrainOnscreen
 * 0x40b220). */
static void view_terrain(const KpFlow *f, KpGame *g, int64_t cam_x, int64_t cam_y)
{
	int64_t x0 = cam_x / KP_WORLD_TILE, y0 = cam_y / KP_WORLD_TILE;
	kp_refresh_terrain_onscreen(g, x0, y0, (cam_x + f->world_w + KP_WORLD_TILE - 1) / KP_WORLD_TILE - x0,
		(cam_y + f->world_h + KP_WORLD_TILE - 1) / KP_WORLD_TILE - y0);
}

typedef struct {
	KpFlow *f;
	int seat, steps, moved, released;
} SeatFrame;

/* A hidden seat's frame under its game's failure handler: the pass of the game loop its own machine runs, without the
 * window. The remembered terrain in its view, its events to its console (its saves, loads and result are not the
 * flow's: the profile's saves and the game's end are the shown seat's), its images' bounds (RefreshAllSpriteBounds
 * 0x415700), its console's frame with no pointer (UpdateGameUi 0x445020, the canvas composed but not shown). */
static void seat_frame_call(void *ud)
{
	SeatFrame *s = (SeatFrame *)ud;
	KpFlow *f = s->f;
	KpGame *g = f->seats.session[s->seat]->g;
	KpEvent e;
	view_terrain(f, g, f->seat_views[s->seat].cam_x, f->seat_views[s->seat].cam_y);
	while (kp_event_take(g, &e)) kp_hud_event(&f->seat_views[s->seat].hud, &e);
	if (s->steps > 0) kp_images_refresh_bounds(g->w);
	kp_hud_frame(&f->seat_views[s->seat].hud, f->world_w, f->world_h, now_ms(f), s->steps, KP_HUD_POINTER_NONE, 0, 0,
		&f->seat_views[s->seat].cam_x, &f->seat_views[s->seat].cam_y, &s->moved, &s->released);
}

/* kp_seats_frame's frame of hidden seat after its steps; a failure stops the seats. */
static void seat_frame(void *ud, int seat, int steps)
{
	KpFlow *f = (KpFlow *)ud;
	KpSession *ss = f->seats.session[seat];
	KpUiSoundCommand c[KP_UI_SOUND_COMMANDS];
	SeatFrame s;
	if (!f->seat_views[seat].hud_on) { /* no console yet (the game's start waits for it): nothing to tell */
		KpEvent e;
		while (kp_event_take(ss->g, &e)) {
		}
		return;
	}
	memset(&s, 0, sizeof s);
	s.f = f;
	s.seat = seat;
	s.steps = steps;
	if (kp_session_guard(ss, seat_frame_call, &s) != 0) return;
	if (s.moved) clamp_view(f, kp_session_mission(ss), 1, &f->seat_views[seat].cam_x, &f->seat_views[seat].cam_y);
	if (s.released) kp_session_release_wait(ss);
	kp_hud_take_commands(f->seat_views[seat].ui, ss);
	while (kp_ui_take_sound_commands(f->seat_views[seat].ui, c, KP_UI_SOUND_COMMANDS) != 0) {
	} /* a hidden seat is not heard */
}

/* The hidden seats' interfaces and consoles, each its own as each machine of a network game has its own: started with
 * the shown seat's, at their start locations' views. */
static int seat_consoles(KpFlow *f, int from_save, char *err, size_t err_size)
{
	int k;
	for (k = 0; k < f->seats.count; k++) {
		if (k == f->seats.shown || f->seat_views[k].ui) continue;
		f->seat_views[k].ui = game_interface(f, err, err_size);
		if (!f->seat_views[k].ui) return -1;
		if (console_init(f, &f->seat_views[k].hud, f->seat_views[k].ui, NULL, f->seats.session[k]->g, from_save, err,
			err_size) != 0) {
			return -1;
		}
		f->seat_views[k].hud_on = 1;
	}
	f->seats.hidden = seat_frame;
	f->seats.hidden_ud = f;
	return 0;
}

/* The hidden seats' consoles ended as the game ends (EndGame 0x443db0), while their games still stand. */
static void free_seat_views(KpFlow *f)
{
	int k;
	for (k = 0; k < KP_HUB_SEATS; k++) {
		KpSession *ss = f->seats.session[k];
		if (f->seat_views[k].hud_on) {
			if (ss && ss->g && kp_session_guard(ss, end_game, f->seat_views[k].ui) != 0) {
				kp_flow_say(f, "seats: seat %d's console end failed: %s", k, ss->error);
			}
			kp_hud_free(&f->seat_views[k].hud);
		}
		kp_ui_free(f->seat_views[k].ui);
		memset(&f->seat_views[k], 0, sizeof f->seat_views[k]);
	}
}

/* After the shown seat's steps: the others to its frame and the shared hashes compared; a difference stops the game. */
static void seats_frame(KpFlow *f)
{
	KpSession *s = f->session;
	if (f->seats.stopped || kp_seats_frame(&f->seats) == 0) return;
	s->failed = 1;
	snprintf(s->error, sizeof s->error, "%s", f->seats.report);
	stop(f, s->error);
}

/* The session of the mission shown: a campaign's next game takes what the game
 * before it left, a catalog mission played directly starts a run of its campaign. */
static int build_scene(KpFlow *f, char *err, size_t err_size)
{
	const KpDataMission *entry = current_entry(f);
	int custom = entry == &f->custom_entry;
	KpSessionStart st;
	int continued = f->has_continue, started = f->has_start_run;
	leave_scene(f, 0);
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
		f->has_run = entry->catalog && kp_campaign_run_of_mission(&f->data->campaign, entry->index, &f->run);
	}
	if (f->playing_replay) f->has_run = 0; /* a replay is no game of the player's campaign */
	if (!(f->session = new_session(f, entry, err, err_size))) return -1;
	mission_text(f, entry);
	mission_speech(f, entry);
	kp_session_start_defaults(&st);
	if (f->playing_replay) { /* Load Replay's game (StartGame 0x443f40 from the file) */
		const KpMission *m = &f->session->host.data.mission;
		if (!f->replay_play || kp_session_replay_start(&st, f->replay_play) != 0) {
			snprintf(err, err_size, "replay: outside version 1");
			return -1;
		}
		if (!custom && (m->chk_size != f->replay_play->chk_size || memcmp(m->blob, f->replay_play->chk, m->chk_size) != 0)) {
			snprintf(err, err_size, "replay: its map is not %s's", entry->name);
			return -1;
		}
		if (f->net_game) { /* a hosted replay: the network's turns beside the record (kp_turns_net_replay) */
			st.local_storm = f->netplay->game.local_storm;
			st.net = &f->netplay->game;
		}
		st.watch = f->watch_game = f->watch_start; /* a viewer's: the record grows (kp_watch.h) */
		f->watch_start = 0;
	} else {
		st.seed = KP_APP_SEED;
		st.mission_index = entry->index;
		st.expansion = f->has_run ? f->run.expansion : 0;
		/* the player's speed starts each game (0x4bb360 passes 0x6d3130) */
		if (!keeps_originals(f)) st.speed = (int)f->profile->settings.game_speed + 1;
		st.record = 1; /* every game's commands for its LastReplay */
		if (custom) { /* a custom map's game: row 0, the lobby's setup, the glue's expansion byte (0x457510) */
			st.mission_index = 0;
			st.expansion = f->ui && f->ui->menus.expansion;
			st.has_setup = f->has_custom_setup;
			if (st.has_setup) st.setup = f->custom_setup;
		}
		if (custom && f->net_game) { /* the chat room's game: the host's seed (0x48) and speed, its turns the network's */
			KpNetGame *ng = &f->netplay->game;
			st.seed = ng->seed;
			st.speed = ng->header.speed + 1;
			st.local_storm = ng->local_storm;
			st.net = ng;
		}
		if (continued) {
			st.has_carry = 1;
			st.carry = f->continue_carry;
			st.cheats = f->continue_cheats;
		}
		if (custom && f->flags->seats && seats_prepare(f, &st, err, err_size) != 0) return -1;
	}
	if (kp_session_start(f->session, &st) != 0) {
		snprintf(err, err_size, "play %s: %s", entry->name, f->session->error);
		return -1;
	}
	if (f->seats.count && seats_start(f, entry, &st, err, err_size) != 0) return -1;
	if (f->playing_replay) {
		int p;
		for (p = 0; p < KP_UI_SLOT_NAMES; p++) { /* the player records' names (0x6726c8), the vision dialog's rows */
			KpRepPlayer r;
			kp_rep_player(f->replay_play, p, &r);
			/* the header's windows-1252 as the interface's UTF-8 (legacy_name made it from UTF-8) */
			(void)kp_text_from_legacy(r.name, (char *)f->ui->slot_names[p], sizeof f->ui->slot_names[p]);
		}
		kp_rep_free(f->replay_play); /* the session has the record */
	} else {
		replay_start(f, &st);
		/* a resumed game is served once its save loaded (its header and record are the save's) */
		if (custom && f->net_game && f->netplay->game.host && !f->seats.count && !f->net_resume) watch_serve(f);
	}
	layout(f); /* the scale the world's art is chosen by (a game started before the first frame has none yet) */
	if (f->host.world_build(f->host.ctx, err, err_size) != 0) return -1;
	f->world = 1;
	f->loads = f->session->loads;
	f->cam_x = f->cam_y = 0;
	if (f->load_on_start[0] || f->net_resume) { /* a saved game: the console at once, the save loaded next */
		if (f->load_on_start[0]) snprintf(f->pending_load, sizeof f->pending_load, "%s", f->load_on_start);
		f->load_on_start[0] = 0;
		if (start_hud(f, 1, err, err_size) != 0) return -1;
	} else if (f->playing_replay) { /* a replay starts at once (RunCreateGameScreen 0x455550: 0x6662b8 = 1) */
		if (start_hud(f, 0, err, err_size) != 0) return -1;
	} else if (f->glue_on && f->glue_room != 0) { /* the game waits for its ready room's Start (the console comes then) */
		kp_session_pause(f->session);
		if (kp_glue_room(&f->glue, f->session->g, f->glue_room) != 0) {
			snprintf(err, err_size, "the ready room: %s", f->ui->error);
			return -1;
		}
	} else if (start_hud(f, 0, err, err_size) != 0) {
		return -1;
	}
	f->host.stop_sounds(f->host.ctx); /* the glue's sounds; its music ends by RunGame's stop (kp_ui_music_start_game) */
	return 0;
}

/* The mission current started once its files are there (ready: host.mission_files' answer); 0, or -1 with nothing of a
 * game that did not start left: no session, no world, no console, no save waiting for it. */
static int start_current(KpFlow *f, int ready, char *err, size_t err_size)
{
	f->waiting = 0;
	f->playing = 1;
	if (ready > 0 && build_scene(f, err, err_size) == 0) return 0;
	leave_scene(f, LEAVE_PLAYING | LEAVE_REPLAY | LEAVE_START);
	return -1;
}

int kp_flow_start_custom(KpFlow *f, char *err, size_t err_size)
{
	if (!f->custom_map) {
		snprintf(err, err_size, "no custom map");
		return -1;
	}
	f->current = KP_FLOW_CUSTOM;
	f->waiting = 0;
	return start_current(f, 1, err, err_size); /* the map's scenario is in memory: nothing to wait for */
}

int kp_flow_start_mission(KpFlow *f, int index, char *err, size_t err_size)
{
	int ready = 1;
	f->current = index;
	f->waiting = 0;
	if (f->host.mission_files && (ready = f->host.mission_files(f->host.ctx, &f->data->missions[index], err, err_size)) == 0) {
		kp_flow_say(f, "play %s: waiting for its files", f->data->missions[index].name);
		f->waiting = 1;
		return 0;
	}
	return start_current(f, ready, err, err_size);
}

/* A mission waiting for its files: started once they came; one that cannot start says why, the glue back at its main
 * menu (a run of --mission with no glue ends, as a start that fails at once does). */
static void mission_wait(KpFlow *f)
{
	char err[512];
	int ready = f->host.mission_files(f->host.ctx, &f->data->missions[f->current], err, sizeof err);
	if (ready == 0 || start_current(f, ready, err, sizeof err) == 0) return;
	kp_flow_say(f, "play %s: %s", f->data->missions[f->current].name, err);
	if (f->glue_on) kp_glue_menus(&f->glue, KP_GLUE_MAIN);
	else f->host.quit(f->host.ctx);
}

/* --- the saves --- */

/* The game's save at path (the campaign's run with campaign, one more section fourcc of extra_size bytes when not
 * NULL): 0, or -1 with why. */
static int save_game_to(KpFlow *f, const char *path, int campaign, const char *fourcc, const uint8_t *extra,
	size_t extra_size, char *why, size_t why_size)
{
	KpSaveCampaign camp;
	KpSavePresentation pres;
	uint8_t *bytes;
	size_t size;
	KpError err;
	uint32_t portrait_ms;
	int camera, k;
	memset(&camp, 0, sizeof camp);
	memset(&pres, 0, sizeof pres);
	if (campaign && f->has_run) {
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
	if (kp_save_encode_with(f->session, f->identity, camp.present ? &camp : NULL, &pres, fourcc, extra, extra_size,
			&bytes, &size, &err) != 0) {
		snprintf(why, why_size, "%s", err.msg);
		return -1;
	}
	k = kp_write_file_atomic(path, bytes, size, &err);
	free(bytes);
	f->host.sync_files(f->host.ctx);
	if (k != 0) snprintf(why, why_size, "%s", err.msg);
	return k;
}

static int save_game(KpFlow *f, const char *file, char *why, size_t why_size)
{
	char path[1100];
	pref_path(f, file, path, sizeof path);
	return save_game_to(f, path, 1, NULL, NULL, 0, why, why_size);
}

/* Whether the game in play is a network game still in Storm's game: its saves are the network's (kp_netsave.h). */
static int net_saves(const KpFlow *f) { return f->net_game && !f->net_left && f->netplay && f->netplay->game_on; }

/* SaveGame 0x498d20 in a network game: every seat writes its own file (kp_flow_net_save_path), the game's save with NETG
 * (kp_netsave.h: the save command's DWORD as the save's ID, kp_netgame_save_record). 0, or -1 with why. */
static int save_net_game(KpFlow *f, const char *name, uint32_t id, char *why, size_t why_size)
{
	KpNetGameSave rec;
	uint8_t netg[KP_NETSAVE_NETG_SIZE];
	char dir[ENTRY_PATH], path[ENTRY_PATH], utf8[KP_NETSAVE_NAME * KP_TEXT_UTF8_MAX];
	(void)kp_text_from_legacy(name, utf8, sizeof utf8); /* the command's windows-1252, the file's UTF-8 */
	if (!kp_flow_net_save_legacy_ok(name)) {
		snprintf(why, why_size, "no file can be named %s", utf8);
		return -1;
	}
	pref_path(f, MAP_DIR, dir, sizeof dir);
	(void)kp_flow_make_dir(dir);
	pref_path(f, NET_SAVE_DIR, dir, sizeof dir);
	if (kp_flow_make_dir(dir) != 0) {
		snprintf(why, why_size, "cannot make %s: %s", dir, strerror(errno));
		return -1;
	}
	kp_netgame_save_record(&f->netplay->game, f->session->g, id, &rec);
	kp_netsave_netg_write(&rec, f->ui->menus.expansion, name, netg);
	kp_flow_net_save_path(f, utf8, path, sizeof path);
	return save_game_to(f, path, 0, KP_NETSAVE_NETG, netg, sizeof netg, why, why_size);
}

typedef struct {
	char name[KP_PROFILE_SAVE_NAME];
	time_t time;
} NetSaveName;

static int newer_save_first(const void *a, const void *b)
{
	const NetSaveName *x = (const NetSaveName *)a, *y = (const NetSaveName *)b;
	if (x->time != y->time) return x->time > y->time ? -1 : 1;
	return kp_compare_nocase(x->name, y->name);
}

/* The in-game save dialogs' names (EnumerateSavedGames 0x4991b0, newest first): a network game's are its saves in
 * maps/save (Koprulu's files of the game's extension, by their names), another game's the profile's. */
void kp_flow_game_saves(KpFlow *f, KpUi *ui)
{
	char dir[ENTRY_PATH], path[ENTRY_PATH];
	const char *ext = save_ext(&ui->menus);
	NetSaveName found[KP_UI_SAVES];
	char **files;
	int count = 0, n = 0, k;
	if (!f->net_game) {
		kp_glue_saves(ui, f->profile);
		return;
	}
	pref_path(f, NET_SAVE_DIR, dir, sizeof dir);
	files = kp_list_files(dir, ext, &count);
	for (k = 0; k < count && n < KP_UI_SAVES; k++) {
		struct stat st;
		uint8_t head[4];
		size_t len = strlen(files[k]) - strlen(ext);
		FILE *in;
		snprintf(path, sizeof path, "%s/%s", dir, files[k]);
		if (stat(path, &st) != 0 || len >= sizeof found[n].name || !(in = fopen(path, "rb"))) continue;
		len = fread(head, 1, sizeof head, in) == sizeof head && kp_netsave_koprulu(head, sizeof head) ? len : 0;
		fclose(in);
		if (len == 0) continue;
		memcpy(found[n].name, files[k], len);
		found[n].name[len] = 0;
		found[n++].time = st.st_mtime;
	}
	kp_list_files_free(files, count);
	if (n > 1) qsort(found, (size_t)n, sizeof *found, newer_save_first);
	for (k = 0; k < n; k++) snprintf(ui->menus.saves[k], sizeof ui->menus.saves[k], "%s", found[k].name);
	ui->menus.save_count = n;
}

/* A save's bytes loaded into the game (its campaign part with campaign; free'd here): 0, or -1 with why. */
static int load_game_bytes(KpFlow *f, uint8_t *bytes, size_t size, int campaign, char *why, size_t why_size)
{
	KpSaveCampaign camp;
	KpSavePresentation pres;
	KpError err;
	int k = kp_save_load(f->session, bytes, size, f->identity, &camp, &pres, &err);
	free(bytes);
	if (k != 0) {
		snprintf(why, why_size, "%s", err.msg);
		return -1;
	}
	/* the save's run replaces the current one; its profile only raises the profile's progress */
	if (campaign && camp.present) {
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

static int load_game(KpFlow *f, const char *file, char *why, size_t why_size)
{
	uint8_t *bytes;
	size_t size;
	char path[1100];
	pref_path(f, file, path, sizeof path);
	if (!(bytes = kp_read_file(path, &size))) {
		snprintf(why, why_size, "no save");
		return -1;
	}
	return load_game_bytes(f, bytes, size, 1, why, why_size);
}

/* LoadGameFromSave 0x499890 of a resumed network game (the file the lobby found, f->net_load): the save's state over
 * the game its start made, then the lobby's players back in their records (RestoreMultiplayerSaveSlots 0x4790b0: the
 * load brought back the saved ones). 0, or -1 with why. */
static int load_net_game(KpFlow *f, char *why, size_t why_size)
{
	uint8_t *bytes;
	size_t size;
	if (!f->netplay || !f->netplay->game_on || !(bytes = kp_flow_read_save_file(f->net_load, &size))) {
		snprintf(why, why_size, "no save");
		return -1;
	}
	if (load_game_bytes(f, bytes, size, 0, why, why_size) != 0) return -1;
	kp_netgame_restore_slots(&f->netplay->game, &f->session->g->setup);
	return 0;
}

/* A saved game started from its file (the glue's Load Saved, an in-game load of another mission's save): its mission
 * without a ready room, whose game loads the save once it stands. With other_only a save of the mission playing is
 * refused (its load failed for another reason: starting the mission again would fail the same way). */
static int start_saved_game(KpFlow *f, const char *name, int other_only, char *why, size_t why_size)
{
	char mission[256];
	uint8_t *bytes;
	size_t size;
	KpError err;
	KpSaveMap map;
	int k, rc;
	if (!(bytes = kp_flow_read_save(f, name, &size))) {
		snprintf(why, why_size, "no save");
		return -1;
	}
	rc = kp_save_mission(bytes, size, f->identity, mission, sizeof mission, NULL, &err);
	if (rc == 0) rc = kp_save_map(bytes, size, f->identity, &map, &err);
	free(bytes);
	if (rc < 0) {
		snprintf(why, why_size, "%s", err.msg);
		return -1;
	}
	if (other_only && current_entry(f) && strcmp(save_name(f, current_entry(f)), mission) == 0) {
		free(map.chk);
		snprintf(why, why_size, "the save is this mission's");
		return -1;
	}
	if (rc == 1) { /* a custom map's game: its map from the save, started as it started, the save loaded then */
		if (kp_flow_custom_from_save(f, mission, &map, why, why_size) != 0) return -1;
		f->has_custom_setup = 1;
		f->custom_setup = map.setup;
		snprintf(f->load_on_start, sizeof f->load_on_start, "%s", name);
		f->glue_room = 0;
		return kp_flow_start_custom(f, why, why_size);
	}
	for (k = 0; k < f->data->mission_count; k++) {
		if (strcmp(f->data->missions[k].key, mission) == 0) {
			snprintf(f->load_on_start, sizeof f->load_on_start, "%s", name);
			f->glue_room = 0;
			return kp_flow_start_mission(f, k, why, why_size);
		}
	}
	snprintf(why, why_size, "no mission %s", mission);
	return -1;
}

/* The ready room of a custom game's local race (RunCreateGameScreen 0x455550: Zerg 8, Terran 7, Protoss 9), else 0. */
static int custom_room(const KpGameSetup *s)
{
	int p;
	for (p = 0; p < 8; p++) {
		if (s->type[p] == KP_CTRL_HUMAN) return s->race[p] == 0 ? 8 : s->race[p] == 1 ? 7 : s->race[p] == 2 ? 9 : 0;
	}
	return 0;
}

/* The custom map at path (named name) as the next game's, its scenario read for the template record's load, and its
 * lobby (kp_setup_lobby: the player's name, local_race and races, the random races from the last game's RNG). 0, or -1
 * with why and nothing kept. */
static int custom_lobby(KpFlow *f, const char *path, const char *name, const uint8_t *record, int64_t local_race,
	const int64_t *races, char *why, size_t why_size)
{
	KpChkLoad load = kp_flow_custom_load_of(record);
	KpMission mission;
	KpError e;
	char player[KP_PLAYER_NAME];
	int rc;
	if (kp_flow_custom_from_file(f, path, name, load, why, why_size) != 0) return -1;
	if (kp_data_map_mission(f->custom_map, load, &mission, &e) != 0) {
		snprintf(why, why_size, "%s", e.msg);
		kp_flow_custom_close(f);
		return -1;
	}
	legacy_name(f->profile ? f->profile->name : NULL, player, sizeof player);
	rc = kp_setup_lobby(&f->custom_setup, &mission, record, player, local_race, races, f->rng_state);
	kp_mission_free(&mission);
	if (rc != 0) {
		snprintf(why, why_size, "map %s: no open slot", name);
		kp_flow_custom_close(f);
		return -1;
	}
	f->has_custom_setup = 1;
	return 0;
}

/* Play Custom's Ok on map entry index (CreateGameFromEntry 0x45bfc0, FinishSinglePlayerCreate 0x4554a0, RunCreateGameScreen
 * 0x455550): the lobby's records of the map, the template and the screen's races (kp_setup_lobby), then the game at
 * once outside Use Map Settings, else from the ready room of the player's race; a race of none goes back to the screen
 * (*back). 0, or -1 with why. */
static int start_custom_game(KpFlow *f, int index, int *back, char *why, size_t why_size)
{
	const KpUiMenus *m = &f->ui->menus;
	const KpUiCreate *c = &m->create;
	char path[ENTRY_PATH];
	int room;
	*back = 0;
	if (index < 0 || index >= m->entry_count || m->entries[index].kind != KP_UI_ENTRY_MAP
		|| !kp_flow_entry_path(f, m, index, path, sizeof path)) {
		snprintf(why, why_size, "no map %d", index);
		return -1;
	}
	if (custom_lobby(f, path, m->entries[index].file, c->record, c->local_race, c->melee ? c->races : NULL, why,
			why_size) != 0) {
		return -1;
	}
	room = c->at_once ? 0 : custom_room(&f->custom_setup);
	if (!c->at_once && room == 0) { /* RunCreateGameScreen's other race: the screen again */
		kp_flow_custom_close(f);
		*back = 1;
		return 0;
	}
	kp_flow_say(f, "custom %s: %s", m->entries[index].file, room ? "the ready room" : "at once");
	f->glue_room = room;
	return kp_flow_start_custom(f, why, why_size);
}

#define GLUE_CHAT_STATE 3 /* the chat room's glue state: a network game that starts at once (no race ready room) */

/* EndGame 0x443db0's network side, once: Storm's game left (LeaveStormGame 0x477e50 with 0x40000001) */
static void net_game_end(KpFlow *f)
{
	if (!f->net_game || f->net_left) return;
	f->net_left = 1;
	f->net_running = 0;
	if (f->netplay && f->netplay->game_on) kp_netgame_game_end(&f->netplay->game);
}

/* A network game whose session failed (a core refusal, a turn that does not split, its resumed save's load): Storm's
 * game left at once, as the others would otherwise wait for this seat's turns until timeout.bin drops it, and the player
 * told with network.tbl's network error. Koprulu's: the original has no refusal. */
static void net_failed(KpFlow *f, const KpSession *s)
{
	if (!f->net_game || f->net_left) return;
	kp_flow_say(f, "network: the game stopped, Storm's game left: %s", s->error);
	net_game_end(f);
	if (f->ui && f->hud_on) kp_ui_error_line(f->ui, kp_ui_tbl(f->ui, KP_UI_NETWORK, KP_NETTBL_NETWORK_ERROR));
}

/* A resumed network game's map and setup (StartGame 0x443f40 with a save open): the scenario and the started records of
 * the file the lobby found (its MAPC, kp_flow_custom_from_save), the lobby's players in their saved records
 * (kp_netgame_restore_slots), its state loaded before the first turn (net_resume). 0, or -1 with why. */
static int resumed_game(KpFlow *f, const KpNetGame *ng, char *why, size_t why_size)
{
	char mission[256];
	uint8_t *bytes;
	size_t size;
	KpError err;
	KpSaveMap map;
	int rc;
	if (!f->net_load[0] || !(bytes = kp_flow_read_save_file(f->net_load, &size))) {
		snprintf(why, why_size, "the saved game's file does not read");
		return -1;
	}
	rc = kp_save_mission(bytes, size, f->identity, mission, sizeof mission, NULL, &err);
	if (rc == 0) rc = kp_save_map(bytes, size, f->identity, &map, &err);
	free(bytes);
	if (rc != 1) {
		if (rc == 0) free(map.chk);
		snprintf(why, why_size, "%s", rc < 0 ? err.msg : "the saved game is no custom map's");
		return -1;
	}
	if (kp_flow_custom_from_save(f, mission, &map, why, why_size) != 0) return -1;
	kp_netgame_restore_slots(ng, &map.setup);
	f->custom_setup = map.setup;
	f->net_resume = 1;
	return 0;
}

/* The chat room's start (PollLobby 0x474f00, StartGame 0x443f40): the map the lobby plays (the host's own, a joiner's
 * copy or the one it received) with the lobby's records (kp_netgame_game_setup), the provider's turns per second asked
 * again (ComputeFramesPerTurn 0x445470), then the game: at once (state 3), or after the race ready room (7..9) of a
 * Use Map Settings game, whose briefing's handshake runs before RunGame. 0, or -1 with why. */
static int start_network_game(KpFlow *f, int state, char *why, size_t why_size)
{
	KpNetGame *ng;
	const char *path;
	if (!f->netplay || !f->netplay->game_on) {
		snprintf(why, why_size, "no network");
		return -1;
	}
	ng = &f->netplay->game;
	path = kp_netgame_map_path(ng);
	f->net_game = 1;
	f->net_left = 0;
	f->net_running = 0;
	f->net_resume = 0;
	if (ng->config.replay) {
		/* a hosted replay (StartGame 0x443f40 with 0x67cc90: the host's file or the one it received), at once, its
		 * record fed beside the network's turns */
		if (!path[0]) {
			snprintf(why, why_size, "no replay");
			return -1;
		}
		if (kp_netgame_game_caps(ng) != 0) {
			snprintf(why, why_size, "the provider's caps");
			return -1;
		}
		kp_flow_say(f, "network replay %s (Storm ID %d)", kp_flow_last_part(path), ng->local_storm);
		return start_replay_file(f, path, why, why_size);
	}
	if (kp_netgame_resumes(ng)) {
		if (resumed_game(f, ng, why, why_size) != 0) return -1;
		path = f->net_load;
	} else {
		if (!path[0]) {
			snprintf(why, why_size, "no map");
			return -1;
		}
		if (kp_flow_custom_from_file(f, path, kp_flow_last_part(path), kp_flow_custom_load_of(ng->header.template_record), why, why_size) != 0)
			return -1;
		kp_netgame_game_setup(ng, &f->custom_setup);
	}
	f->has_custom_setup = 1;
	if (kp_netgame_game_caps(ng) != 0) {
		snprintf(why, why_size, "the provider's caps");
		return -1;
	}
	f->glue_room = state == GLUE_CHAT_STATE ? 0 : state;
	kp_flow_say(f, "network %s: %s (Storm ID %d, seed %08x)", kp_flow_last_part(path), f->glue_room ? "the ready room" : "at once",
		ng->local_storm, (unsigned)ng->seed);
	return kp_flow_start_custom(f, why, why_size);
}

/* The ready room's Start of a network game whose lobby records changed in the room (a player left there:
 * HandlePlayerLeftLobby 0x477c20 in the glue restores the slot, and RunGameSession 0x467ba0's StartGame builds the game
 * from the records after the briefing): the session the room showed, which stood before it, built again from them
 * before its first turn, with the console at once. 1 rebuilt (0, or -1 with why), 0 none needed. */
static int rebuild_network_game(KpFlow *f, int *rc, char *why, size_t why_size)
{
	KpNetGame *ng;
	if (!f->net_game || f->net_left || !f->netplay || !f->netplay->game_on || f->net_resume) return 0;
	ng = &f->netplay->game;
	if (!kp_netgame_setup_changed(ng, &f->custom_setup)) return 0;
	kp_flow_say(f, "network: the lobby's records changed in the ready room, the game built again");
	free_scene(f);
	kp_netgame_game_setup(ng, &f->custom_setup);
	f->glue_room = 0;
	*rc = kp_flow_start_custom(f, why, why_size);
	return 1;
}

/* A network game's part of a frame before its steps: RunGame 0x445310's first turn polled until it ran (the clock runs
 * from then), the messages (GameLoop 0x4451f0's step 5) and timeout.bin while a turn is overdue, the first's too (its
 * TurnManagerStep waits as any other). Whether the steps run. */
static int net_frame(KpFlow *f, KpSession *s)
{
	int ran = 1;
	if (!f->net_running) {
		int r = kp_session_net_begin(s);
		if (r < 0) return 0; /* failed (the session's error) */
		if (r == 1) {
			f->net_running = 1;
			kp_clock_reset(&s->clock);
		}
		ran = r;
	}
	kp_session_net_messages(s);
	if (f->ui) kp_ui_net_wait(f->ui);
	return ran && !s->failed;
}

int kp_flow_play_map(KpFlow *f, const char *path, const char *template_name, char *err, size_t err_size)
{
	const char *name = template_name && template_name[0] ? template_name : MAP_TEMPLATE;
	const char *file = kp_flow_last_part(path);
	const KpTemplate *t = NULL;
	int64_t races[KP_CUSTOM_RACES];
	int k, melee;
	for (k = 0; k < f->data->templates.count && t == NULL; k++) {
		if (kp_compare_nocase(f->data->templates.list[k].name, name) == 0) t = &f->data->templates.list[k];
	}
	if (t == NULL) {
		snprintf(err, err_size, "no game type %s in templates.lst", name);
		return -1;
	}
	/* the screen's offer (RefreshComputerSlots 0x456420, FillRaceList 0x456170): a melee template's player and a
	 * computer in each other slot, all Random; another template's slots as the map has them */
	melee = t->record[KP_TEMPLATE_VICTORY] == KP_VICTORY_MELEE;
	for (k = 0; k < KP_CUSTOM_RACES; k++) races[k] = melee ? KP_RACE_RANDOM : KP_RACE_NONE;
	if (custom_lobby(f, path, file, t->record, melee ? KP_RACE_RANDOM : KP_RACE_NONE, races, err, err_size) != 0) {
		return -1;
	}
	kp_flow_say(f, "custom %s: %s at once", file, t->name);
	f->glue_room = 0;
	return kp_flow_start_custom(f, err, err_size);
}

int kp_flow_load_saved(KpFlow *f, const char *name, char *why, size_t why_size)
{
	return start_saved_game(f, name, 0, why, why_size);
}

int kp_flow_show_seat(KpFlow *f, int seat, char *why, size_t why_size)
{
	KpSeats *s = &f->seats;
	KpSession *old = f->session;
	KpHud hud;
	KpUi *ui;
	int64_t x, y;
	if (!s->count) {
		snprintf(why, why_size, "no seats (--seats)");
		return -1;
	}
	if (seat < 0 || seat >= s->count) {
		snprintf(why, why_size, "no seat %d of %d", seat, s->count);
		return -1;
	}
	if (!f->world || !f->hud_on || !old || !old->g) {
		snprintf(why, why_size, "no game in play");
		return -1;
	}
	if (seat == s->shown) return 0;
	if (!f->seat_views[seat].hud_on) {
		snprintf(why, why_size, "seat %d has no console", seat);
		return -1;
	}
	kp_hud_take_commands(f->ui, old); /* the seat's commands its interface still holds */
	sounds(f);                        /* and its last sounds */
	f->host.world_free(f->host.ctx);
	f->world = 0;
	/* each seat keeps its interface, console and view: the one hidden goes on with them (seat_frame) */
	ui = f->seat_views[seat].ui;
	hud = f->seat_views[seat].hud;
	x = f->seat_views[seat].cam_x;
	y = f->seat_views[seat].cam_y;
	f->seat_views[s->shown].ui = f->ui;
	f->seat_views[s->shown].hud = f->hud;
	f->seat_views[s->shown].hud_on = 1;
	f->seat_views[s->shown].cam_x = f->cam_x;
	f->seat_views[s->shown].cam_y = f->cam_y;
	memset(&f->seat_views[seat], 0, sizeof f->seat_views[seat]);
	f->ui = ui;
	f->hud = hud;
	f->cam_x = x;
	f->cam_y = y;
	s->shown = seat;
	f->session = s->session[seat];
	f->loads = f->session->loads;
	/* its clock from now, paused as the seat's before was (a hidden seat's stepped without it) */
	kp_session_resume(f->session);
	if (old->clock.paused) kp_session_pause(f->session);
	f->host.interface(f->host.ctx, ui);
	kp_flow_video_to_ui(f, ui); /* the options as they are now (a hidden console's dialogs changed none) */
	options_to_ui(f, ui);
	kp_glue_saves(ui, f->profile); /* and the profile's saves, which another seat's saves changed (save_requests) */
	kp_hud_shown(&f->hud);
	layout(f);
	if (f->host.world_build(f->host.ctx, why, why_size) != 0) return -1;
	f->world = 1;
	kp_flow_say(f, "seats: seat %d shown", seat);
	return 0;
}

/* The glue state of the run's campaign menu: the expansion's or the original's. */
static int run_campaign(const KpFlow *f)
{
	if (!f->has_run) return f->ui && f->ui->menus.expansion ? KP_GLUE_EXPANSION_CAMPAIGN : KP_GLUE_CAMPAIGN; /* a custom game's */
	return f->run.expansion != 0 ? KP_GLUE_EXPANSION_CAMPAIGN : KP_GLUE_CAMPAIGN;
}

/* The menu a game the glue started goes back to: a network game's the provider's game list (the score screen's and the
 * ready room's leave in multiplayer), else its campaign's. */
static int after_game_menu(const KpFlow *f)
{
	return f->net_game ? kp_ui_menus_provider_state(f->ui) : run_campaign(f);
}

static void save_requests(KpFlow *f)
{
	char why[256];
	if (f->pending_save[0]) {
		char file[KP_SAVE_FILE_NAME], name[KP_PROFILE_SAVE_NAME], shown[KP_PROFILE_SAVE_NAME * KP_TEXT_UTF8_MAX];
		int rc;
		snprintf(name, sizeof name, "%s", f->pending_save);
		snprintf(shown, sizeof shown, "%s", name);
		f->pending_save[0] = 0;
		if (net_saves(f)) { /* the save command's windows-1252 name (kp_ui_saveload.c, or a 1.17 seat's) */
			(void)kp_text_from_legacy(name, shown, sizeof shown);
			rc = save_net_game(f, name, f->pending_save_id, why, sizeof why);
		} else if (f->net_game) { /* the network game ended before its save was written: never the profile's */
			snprintf(why, sizeof why, "the network game has ended");
			rc = -1;
		} else {
			kp_flow_save_file(f, name, file, sizeof file);
			if ((rc = save_game(f, file, why, sizeof why)) == 0) {
				kp_profile_add_save(f->profile, name);
				f->host.save_profile(f->host.ctx);
			}
		}
		if (rc == 0) {
			kp_flow_game_saves(f, f->ui);
			kp_flow_say(f, "saved %s", shown);
		} else {
			kp_flow_say(f, "save failed: %s", why);
		}
	}
	/* a resumed network game's save, before its first turn (kp_netgame_game_begin reads the restored records) */
	if (f->net_resume && f->playing && f->session && f->session->g) {
		f->net_resume = 0;
		if (load_net_game(f, why, sizeof why) != 0) {
			kp_flow_say(f, "network: the saved game %s does not load: %s", f->net_load, why);
			f->session->failed = 1;
			snprintf(f->session->error, sizeof f->session->error, "the saved game does not load: %s", why);
			stop(f, f->session->error);
		} else {
			kp_flow_say(f, "network: resumed %s", f->net_load);
			if (f->net_game && f->netplay && f->netplay->game.host && !f->seats.count) watch_serve(f);
		}
	}
	/* a save of another mission starts its game, which asks for the save again: loaded at once too */
	while (f->pending_load[0] && f->playing && f->session && f->session->g) {
		char file[KP_SAVE_FILE_NAME], name[KP_PROFILE_SAVE_NAME];
		int menu = run_campaign(f); /* the campaign played, before another mission's start replaces its run */
		snprintf(name, sizeof name, "%s", f->pending_load);
		f->pending_load[0] = 0;
		kp_flow_save_file(f, name, file, sizeof file);
		if (load_game(f, file, why, sizeof why) != 0) {
			char why2[256];
			/* another mission's save: its game */
			if (start_saved_game(f, name, 1, why2, sizeof why2) == 0) continue;
			kp_flow_say(f, "load failed: %s", why);
			if (f->playing && f->hud_on) kp_ui_load_failed(f->ui); /* the player is told (check_save let it through) */
			if (!f->playing) { /* the game went with the other mission's failed start: its campaign's menu */
				kp_flow_say(f, "load: %s", why2);
				if (f->glue_on) kp_glue_menus(&f->glue, menu);
				else f->host.quit(f->host.ctx);
			}
		} else {
			kp_flow_say(f, "loaded %s", name);
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

/* The custom single-player mode 0x6ca71c (PlaceLocalHumanForCustomGame 0x4bb550; a save's copy 0x5264c3): a custom
 * map's game that is no replay's playback, whose end goes to CreateNextScenarioGame 0x4bb360. */
static int custom_mode(const KpFlow *f)
{
	return f->current == KP_FLOW_CUSTOM && !f->playing_replay;
}

static void campaign_after_game(KpFlow *f, int result)
{
	KpCampaignStep step;
	int64_t cinematics[KP_APP_STEP_CINEMATICS];
	int count = 0, k;
	f->step_cinematic_count = 0;
	f->step_glue = KP_GLUE_MAIN; /* a game without a run has no step: not the step of the game before */
	f->step_epilog = 0;
	f->custom_next[0] = 0;
	if (custom_mode(f)) {
		(void)kp_text_from_legacy((const char *)f->session->g->trig.next_scenario, f->custom_next, sizeof f->custom_next);
		kp_flow_say(f, "custom %s: %s%s", current_name(f), f->custom_next[0] ? "the next scenario " : "the campaign menu",
			f->custom_next);
	}
	if (!f->has_run) return;
	kp_campaign_after_game(&f->data->campaign, &f->run, kp_profile_progress(f->profile), result,
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
	kp_flow_say(f, "campaign %s: %s", current_name(f), f->has_next ? "the next game" : "the campaign ends");
}

static void start_glue_game(KpFlow *f, int64_t key, int state)
{
	char err[512];
	int k;
	for (k = 0; k < f->data->mission_count; k++) {
		if (f->data->missions[k].catalog && f->data->missions[k].index == key) {
			f->glue_room = state;
			if (kp_flow_start_mission(f, k, err, sizeof err) != 0) {
				kp_flow_say(f, "glue: %s", err);
				kp_glue_menus(&f->glue, KP_GLUE_MAIN);
			}
			return;
		}
	}
	kp_flow_say(f, "glue: no catalog mission %lld", (long long)key);
	/* as a start that fails: nothing of the game before stays (its scene, the continuation score_next made of it) */
	leave_scene(f, LEAVE_PLAYING | LEAVE_RUN);
	kp_glue_menus(&f->glue, KP_GLUE_MAIN);
}

/* A game's end (LEAVE_GAME: a custom map's archive closed as EndGame 0x443db0 closes it, which Windows needs to replace
 * the file), then the glue's menu state. */
static void leave_glue_game(KpFlow *f, int state)
{
	leave_scene(f, LEAVE_GAME);
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
/* A custom game's end (CreateNextScenarioGame 0x4bb360): without a next scenario the campaign menu (22 with the
 * expansion, else 6); otherwise <the map's folder>/<the next scenario> created with the Use Map Settings template
 * (LoadUseMapSettingsTemplate 0x447b90, CreateGameFromMapPath 0x45c510: the map's races as they are) from the ready
 * room of the player's race (a loaded save's folder is its map's as the load resolved it: kp_flow_custom_from_save). A failure
 * (no folder: a replay's map; a name that is no file of the folder, Koprulu's: the original appends any name; a map
 * that does not load or has no open slot; a race of none) reports network.tbl's 0x6a and opens the campaign menu. */
static void custom_next(KpFlow *f)
{
	char path[ENTRY_PATH], why[KP_ERROR_SIZE];
	const char *slash = strrchr(f->custom_path, '/');
	int menu = run_campaign(f), room;
	leave_scene(f, LEAVE_GAME);
	if (!f->custom_next[0]) {
		kp_glue_menus(&f->glue, menu);
		return;
	}
	why[0] = 0;
	if (slash == NULL
		|| snprintf(path, sizeof path, "%.*s/%s", (int)(slash - f->custom_path), f->custom_path, f->custom_next)
			>= (int)sizeof path) {
		snprintf(why, sizeof why, "no folder of the map");
	} else if (!kp_flow_file_name_ok(f->custom_next)) {
		snprintf(why, sizeof why, "no file of the map's folder");
	} else if (custom_lobby(f, path, f->custom_next, KP_TEMPLATE_UMS, KP_RACE_NONE, NULL, why, sizeof why) == 0) {
		if ((room = custom_room(&f->custom_setup)) != 0) {
			kp_flow_say(f, "custom %s: the next scenario's ready room", f->custom_next);
			f->glue_room = room;
			if (kp_flow_start_custom(f, why, sizeof why) == 0) return;
			leave_scene(f, LEAVE_PLAYING);
		} else {
			snprintf(why, sizeof why, "the player's race is none of the three");
			kp_flow_custom_close(f);
		}
	}
	kp_flow_say(f, "custom %s: %s", f->custom_next, why);
	kp_ui_menus_net_error(f->ui, NET_NEXT_SCENARIO);
	kp_glue_menus(&f->glue, menu);
}

static void score_next(KpFlow *f)
{
	if (f->net_game) {
		/* RunScoreScreen 0x461940 in multiplayer: OK goes back by the provider (SetGlueStateFromProvider 0x457e50),
		 * any other result (the glue's exit) to the main menu */
		leave_glue_game(f, f->ui->score.result == KP_UI_SCORE_OK ? after_game_menu(f) : KP_GLUE_MAIN);
	} else if (custom_mode(f)) {
		custom_next(f);
	} else if (f->has_next) {
		int state = f->next_glue ? f->next_glue : f->glue_room;
		continue_with_next(f);
		start_glue_game(f, f->next_mission, state);
	} else if (f->step_epilog) {
		leave_scene(f, LEAVE_GAME);
		kp_glue_epilog(&f->glue, f->step_expansion);
	} else {
		leave_glue_game(f, f->step_glue);
	}
}

/* The cinematics (ADR 0015) a glue step plays before it goes on, and what goes on: the mission to start, the menu to
 * open, the score screen's next step, the epilog (its state the run's expansion byte). */
enum { THEN_NONE, THEN_MISSION, THEN_MENUS, THEN_NEXT, THEN_EPILOG, THEN_TITLE };
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
	case THEN_TITLE:
		kp_glue_title(&f->glue, now_ms(f));
		kp_flow_say(f, f->glue.screen == KP_GLUE_TITLE ? "glue: the title" : "glue: the main menu");
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
	if (!kp_campaign_after_cinematic(&f->data->campaign, ev->expansion, ev->mission, ev->cinematic, &run,
			kp_profile_progress(f->profile), &step, more, &count, KP_APP_STEP_CINEMATICS)) {
		kp_flow_say(f, "glue: no campaign row of cinematic %d and mission %lld", ev->cinematic, (long long)ev->mission);
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
	return exit == KP_UI_EXIT_PROGRAM;
}

/* After an in-game menu ended the game: Exit Program goes to the main menu (the web's), Quit Mission to the campaign
 * menu, Restart plays the mission again from its ready room. */
static void glue_after_game(KpFlow *f)
{
	int exit = f->after_exit, glue_exit = kp_ui_menus_glue_exit(f->ui, 1);
	f->after_game = 0;
	if (exit == KP_UI_EXIT_PROGRAM || glue_exit) {
		/* a fatal network report in the game: RequestGameExit 0x444e00(4), then the glue's exit (state 25) to the
		 * main menu, which shows the deferred report */
		leave_glue_game(f, KP_GLUE_MAIN);
	} else if (exit == KP_UI_EXIT_RESTART && f->has_run) {
		f->has_continue = 1;
		f->continue_run = f->run;
		f->continue_carry = f->after_carry;
		f->continue_cheats = f->after_cheats;
		start_glue_game(f, f->run.mission, f->glue_room);
	} else {
		leave_glue_game(f, after_game_menu(f));
	}
}

/* --- each frame --- */

static void game_events(KpFlow *f)
{
	KpEvent e;
	while (kp_event_take(f->session->g, &e)) {
		if (f->hud_on) kp_hud_event(&f->hud, &e);
		if (e.kind == KP_EVENT_SAVE_GAME && e.text) {
			/* a multiplayer game's command names it in windows-1252 (kp_ui_saveload.c dispatched_name): a network
			 * game's file takes that name (save_requests), --seats' profile file its UTF-8 */
			if (f->session->g->setup.multiplayer && !f->net_game)
				(void)kp_text_from_legacy(e.text, f->pending_save, sizeof f->pending_save);
			else snprintf(f->pending_save, sizeof f->pending_save, "%s", e.text);
			f->pending_save_id = (uint32_t)e.a;
		}
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
	f->video_flags.renderer = fl->renderer;
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
		/* the host read it already, before its drawing started (kp_app_main.c); a change is taken at the dialog's OK */
		if (!(given & KP_VIDEO_GIVEN_RENDERER)) fl->renderer = v->renderer;
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
	o->renderer = (int8_t)f->flags->renderer;
	o->available = f->video_available;
	o->gpu_name = f->host.gpu_name;
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
		fl->renderer = o->renderer; /* the host takes it at the OK (committed_options), not at each choice */
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
	run.renderer = fl->renderer;
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
		changed |= keep_setting(&s->multi_mouse_scroll, ui->multi_mouse_scroll_speed);
		changed |= keep_setting(&s->multi_key_scroll, ui->multi_key_scroll_speed);
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
		/* the renderer switched once its choice is kept: each choice under the open dialog would make the window
		 * again, and a Cancel puts the running one back (ADR 0028) */
		if (f->host.renderer) f->host.renderer(f->host.ctx, f->flags->renderer);
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
	case KP_GLUE_AWAKE: /* the glue's Sleep after a game: what its end leads to */
		if (f->after_score && f->session && f->session->g) { /* the game's state the score reads stands */
			f->after_score = 0;
			kp_glue_score(&f->glue, f->session->g, f->after_result, f->after_elapsed, f->after_from_save,
				f->after_network ? f->after_storm_results : NULL, f->after_local);
		} else if (f->after_game) {
			glue_after_game(f);
		} else {
			f->after_score = 0;
			kp_glue_menus(&f->glue, KP_GLUE_MAIN);
		}
		break;
	case KP_GLUE_EXIT:
		kp_flow_say(f, "glue: exit");
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
		char why[256] = "no such save";
		if (ev.save < 0 || ev.save >= f->ui->menus.save_count
			|| kp_flow_load_saved(f, f->ui->menus.saves[ev.save], why, sizeof why) != 0) {
			/* the load screen again, as the original's state stays when LoadSavedGameByName 0x498bd0 fails
			 * (RunLoadGameScreen 0x459970); its Back keeps the campaign it came from. The player is told once the
			 * screen stood (gluAll 66, as kp_ui_load_failed in a game): check_save refuses at Ok what kp_save_mission
			 * refuses, so only a save whose mission is not in the data or whose start fails (a custom map the game
			 * refuses, ADR 0023's checks) gets here. */
			kp_flow_say(f, "glue: load failed: %s", why);
			kp_ui_menus_refused(f->ui, KP_UI_GLUALL_SAVE_DAMAGED);
			kp_glue_menus(&f->glue, KP_GLUE_LOAD_SCREEN);
		}
		break;
	}
	case KP_GLUE_REPLAY: {
		char why[256];
		if (start_replay_game(f, ev.save, why, sizeof why) != 0) {
			/* Load Replay again, as the original's screen stays when the game is not created, with why once it stood
			 * (gluAll 51: a map whose game refused it at its start) */
			kp_flow_say(f, "glue: %s", why);
			kp_ui_menus_refused(f->ui, STR_NOT_VALID);
			leave_scene(f, LEAVE_PLAYING | LEAVE_REPLAY);
			kp_glue_menus(&f->glue, KP_GLUE_LOAD_REPLAY);
		}
		break;
	}
	case KP_GLUE_CUSTOM: {
		char why[256];
		int back;
		if (start_custom_game(f, ev.save, &back, why, sizeof why) != 0) {
			/* Play Custom again, as the original's screen stays when the game is not created, with why once it stood */
			kp_flow_say(f, "glue: %s", why);
			kp_ui_menus_refused(f->ui, STR_NOT_VALID);
			leave_scene(f, LEAVE_PLAYING);
			kp_glue_menus(&f->glue, KP_GLUE_PLAY_CUSTOM);
		} else if (back) {
			kp_glue_menus(&f->glue, KP_GLUE_PLAY_CUSTOM);
		}
		break;
	}
	case KP_GLUE_NETWORK: {
		char why[256];
		if (start_network_game(f, ev.state, why, sizeof why) != 0) {
			/* the game is left (AbortNetworkGame 0x474e60: the lobby's transfers, messages, countdown, slots and save
			 * with Storm's game) and the provider's game list shows again */
			kp_flow_say(f, "glue: the network's game: %s", why);
			if (f->netplay) kp_netplay_leave(f->netplay);
			f->net_left = 1;
			leave_scene(f, LEAVE_PLAYING | LEAVE_NET | LEAVE_CUSTOM);
			kp_glue_menus(&f->glue, kp_ui_menus_provider_state(f->ui));
		}
		break;
	}
	case KP_GLUE_ROOM_START: { /* the game with the same interface's console */
		char err[256];
		int rc = 0;
		if (rebuild_network_game(f, &rc, err, sizeof err)) {
			if (rc != 0) {
				kp_flow_say(f, "network: %s", err);
				net_game_end(f); /* Storm's game left though the session is gone */
				leave_glue_game(f, after_game_menu(f));
			}
			break;
		}
		if (start_hud(f, 0, err, sizeof err) != 0) {
			kp_flow_say(f, "%s", err);
			leave_glue_game(f, after_game_menu(f));
			break;
		}
		kp_session_resume(f->session);
		break;
	}
	case KP_GLUE_ROOM_CANCEL:
		if (f->net_game) f->net_left = 1; /* the room's cancel left Storm's game (SendBriefingStart 0x475cc0) */
		/* RunTerranReadyRoom 0x461420 and its twins: the glue's exit (100, or the exit hook's -1) goes to the main
		 * menu (state 25, then 0) */
		if (f->ui->result == KP_UI_ROOM_EXIT || f->ui->result == -1) leave_glue_game(f, KP_GLUE_MAIN);
		else leave_glue_game(f, after_game_menu(f));
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
	if (exit == KP_UI_EXIT_PROGRAM || exit == KP_UI_EXIT_QUIT_MISSION || exit == KP_UI_EXIT_RESTART) {
		/* the in-game menus' Exit Program, Quit Mission, Restart (no result) */
		kp_ui_end_game(ui);
		net_game_end(f);
		save_last_replay(f);
		sounds(f);
		kp_flow_say(f, "play %s: exit %d", current_name(f), exit);
		kp_session_pause(f->session);
		end_hud(f);
		if (exit_ends_program(f, exit)) {
			f->host.quit(f->host.ctx);
		} else if (f->glue_on) {
			f->after_game = 1;
			f->after_score = 0; /* the latest end's: not a score of a game whose Sleep did not end */
			f->after_exit = exit;
			kp_carry_take(f->session->g, &f->after_carry);
			f->after_cheats = kp_cheats(f->session->g);
			kp_glue_sleep(&f->glue); /* then glue_after_game */
		}
	} else if (exit != 0 && (f->result || (f->net_game && (exit == KP_UI_EXIT_VICTORY || exit == KP_UI_EXIT_DEFEAT)))) {
		/* the result dialog's answer (RequestGameExit 0x444e00: 6 victory, 7 defeat) ends the game; a network game's
		 * Quit Mission too (QuitMissionMenuHandler 0x47bff0: 6 after a victory, else 7, the result the exit's) */
		int result = f->result ? f->result : exit == KP_UI_EXIT_VICTORY ? RESULT_VICTORY : RESULT_DEFEAT;
		/* ComputeGameResults 0x477270 while Storm's game still runs (0x4bd220, before EndGame's leave) */
		uint8_t storm_results[KP_NETGAME_PLAYERS] = { 0 };
		int network = f->net_game && !f->net_left && f->netplay && f->netplay->game_on, local = 0;
		if (network) local = kp_netgame_game_results(&f->netplay->game, f->session->g, storm_results);
		kp_ui_end_game(ui);
		net_game_end(f);
		save_last_replay(f);
		sounds(f);
		campaign_after_game(f, result);
		kp_session_pause(f->session);
		end_hud(f);
		if (f->glue_on) { /* the score screen once the glue's Sleep is over (glue_frame's KP_GLUE_AWAKE) */
			f->after_score = 1;
			f->after_game = 0;
			f->after_result = result;
			f->after_elapsed = (uint32_t)(now(f) - f->game_started_at);
			f->after_from_save = f->session->loads > 0;
			f->after_network = network;
			memcpy(f->after_storm_results, storm_results, sizeof storm_results);
			f->after_local = local;
			kp_glue_sleep(&f->glue);
		}
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
		net_failed(f, s);
		return;
	}
	if (s->failed) net_failed(f, s); /* a failure outside the steps (the session's turns, a resumed save's load) */
	if (f->ui != NULL && kp_ui_fatal_shown(f->ui) != f->fatal_held) {
		/* the fatal dialog runs modally over the game: no step under it, and no time to catch up after it */
		f->fatal_held = !f->fatal_held;
		if (f->fatal_held) kp_session_pause(s);
		else kp_session_resume(s);
	}
	if (!kp_flow_glue_active(f) && !s->failed && !f->flags->autotest /* the autotest steps the game itself */
		&& (!f->net_game || net_frame(f, s))) {
		ticks = kp_session_advance(s, now(f), &fraction, f->host.clock_ms);
		if (f->seats.count) seats_frame(f);
		/* a loaded save: its state at once, without motion from the state before */
		if (s->loads != f->loads) {
			f->loads = s->loads;
			restart = 1;
			if (ticks < 1) ticks = 1;
		}
	}
	view_terrain(f, s->g, f->cam_x, f->cam_y);
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
	while ((cp = kp_ui_text_take_missing(&f->ui->text)) != 0) kp_flow_say(f, "text: no glyph for U+%04X", (unsigned)cp);
}

static void start_over(KpFlow *f);

void kp_flow_frame(KpFlow *f)
{
	tell_missing_glyphs(f);
	if (f->waiting) mission_wait(f);
	if (f->cine_then.kind != THEN_NONE && !f->host.cine_active(f->host.ctx)) cine_continue(f); /* the cinematics ended */
	f->fixed_screen = kp_flow_glue_active(f); /* a glue screen keeps the original's 640 x 480 */
	layout(f);
	if (f->netplay) kp_netplay_pump(f->netplay); /* what arrived, before the chat room's pass reads it */
	watch_frame(f);
	if (f->fixed_screen) {
		glue_frame(f);
		cursor(f, f->glue.screen != KP_GLUE_SLEEP); /* the Sleep's screen faded out: no cursor */
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
	if (f->language_chosen) start_over(f); /* the glue stopped at the choice: nothing else goes on while it waits */
}

/* --- the languages (ADR 0016) --- */

static void free_locale(KpFlow *f)
{
	kp_locale_free(&f->text);
	kp_locale_glyphs_free(&f->glyphs);
	free(f->speech_sounds);
	f->speech_sounds = NULL;
	memset(&f->locale, 0, sizeof f->locale);
}

/* host.language_files' answer for a language (1 for the base's, or with no host's answer): with fetch its group asked
 * for, and one that cannot be had said (1: its parts are the base's) */
static int language_files(KpFlow *f, const char *id, int fetch)
{
	char err[256];
	int rc;
	if (!f->host.language_files || !id[0] || strcmp(id, KP_LOCALE_BASE) == 0) return 1;
	err[0] = 0;
	rc = f->host.language_files(f->host.ctx, id, fetch, err, sizeof err);
	if (rc < 0 && fetch) kp_flow_say(f, "language %s: %s", id, err);
	return rc < 0 && fetch ? 1 : rc;
}

/* Whether the data has a language's speech (KP_DATA_SPEECH_MARKER: Remastered's languages not downloaded have none) */
static int has_speech(KpFlow *f, const char *id)
{
	char path[128];
	size_t got = 0;
	uint64_t total = 0;
	uint8_t *head;
	KpError e;
	snprintf(path, sizeof path, KP_DATA_SPEECH_MARKER, id);
	free(head = kp_files_head(&f->data->raw, path, 4, &got, &total, &e));
	return head != NULL;
}

/* The profile's languages (LANG: defs/l10n/locales.json's, the base's first), each installed for the text when it is the
 * base or the data holds its strings (kp_locale_held), for the speech when it is the base or the data has its speech. */
static void list_languages(KpFlow *f)
{
	KpUiLanguages *l = &f->languages;
	const KpDefsTable *t = kp_defs_table(f->data->raw.profile, "LANG");
	size_t at = 1;
	int k, fetched;
	memset(l, 0, sizeof *l);
	l->text = l->speech = l->chosen_text = l->chosen_speech = -1;
	if (t == NULL || t->size < 1) return;
	for (k = 0; k < t->data[0] && l->count < KP_UI_LANGUAGES; k++) {
		size_t id_n, name_n;
		char *id = l->list[l->count].id, *name = l->list[l->count].name;
		if (at + 1 > t->size || at + 1 + (id_n = t->data[at]) + 1 > t->size
			|| at + 2 + id_n + (name_n = t->data[at + 1 + id_n]) > t->size) {
			kp_flow_say(f, "language: the profile's LANG is damaged");
			break;
		}
		if (id_n == 0 || id_n >= KP_UI_LANGUAGE_ID || name_n >= KP_UI_LANGUAGE_NAME) {
			at += 2 + id_n + name_n;
			continue;
		}
		memcpy(id, t->data + at + 1, id_n);
		id[id_n] = 0;
		memcpy(name, t->data + at + 2 + id_n, name_n);
		name[name_n] = 0;
		at += 2 + id_n + name_n;
		/* a language whose files a group not there yet holds (the web's) has both */
		fetched = language_files(f, id, 0) == 0;
		l->list[l->count].text = strcmp(id, KP_LOCALE_BASE) == 0 || fetched || kp_locale_held(&f->data->raw, id);
		l->list[l->count].speech = strcmp(id, KP_LOCALE_BASE) == 0 || fetched || has_speech(f, id);
		l->count++;
	}
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

/* --mission's, --map's or --watch's game at once, without the glue. */
static int starts_a_game(const KpFlow *f) { return f->flags->mission[0] != 0 || f->flags->map[0] != 0 || f->flags->watch[0] != 0; }

/* The language of the start: --locale's; for the glue (not the checks', --mission's, the screenshots' or the benches':
 * theirs is the base's with its original fonts) the profile's when installed, else the system's first installed, else
 * the base's. */
static void choose_language(KpFlow *f)
{
	const KpUiLanguages *l = &f->languages;
	const char *p = f->flags->system_locales;
	int at = language_at(l, f->profile->text_locale);
	if (f->flags->locale[0] != 0 || keeps_originals(f) || starts_a_game(f)) return;
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
	if (f->flags->speech[0] != 0 || keeps_originals(f) || starts_a_game(f)) return;
	if (at < 0 || !l->list[at].speech) at = language_at(l, f->flags->locale);
	if (at >= 0 && !l->list[at].speech) at = -1;
	snprintf(f->flags->speech, sizeof f->flags->speech, "%s", at >= 0 ? l->list[at].id : KP_LOCALE_BASE);
}

/* The speech's sound records (its voices' durations) for one that is not the base's (ADR 0019, stage C2: its voices
 * from the data, locales/<loc>/Assets/); none leaves the base's, with one line. The speech needs no glyphs: --speech
 * alone reads it. */
static void read_speech(KpFlow *f)
{
	const char *speech = f->flags->speech;
	size_t size = 0;
	int voices = 0;
	KpError e;
	uint8_t *sfx;
	if (speech[0] == 0 || strcmp(speech, KP_LOCALE_BASE) == 0) return;
	if (!(sfx = kp_ui_data_speech_sounds(&f->data->ui, &f->data->raw, speech, &size, &voices, &e))) {
		kp_flow_say(f, "speech %s: out of memory, the base's voices", speech);
		return;
	}
	if (e.msg[0]) kp_flow_say(f, "speech %s: %s; that voice's duration stays the base's", speech, e.msg);
	if (voices == 0) {
		kp_flow_say(f, "speech %s: the data has none of its voices, the base's voices", speech);
		free(sfx);
		return;
	}
	f->locale.speech = f->speech_sounds = sfx;
	f->locale.speech_size = size;
	kp_flow_say(f, "speech of %s: %d voices", speech, voices);
}

/* The language's text (kp_locale_build) for one that is not the base's: what does not fit stays the base's, a line each. */
static void read_text(KpFlow *f)
{
	const char *locale = f->flags->locale;
	KpError e;
	int k;
	if (strcmp(locale, KP_LOCALE_BASE) == 0) return;
	if (!kp_locale_held(&f->data->raw, locale)) {
		kp_flow_say(f, "language %s: the data has none of its strings, the base's text", locale);
		return;
	}
	if (kp_locale_build(&f->text, &f->data->ui, &f->data->raw, locale, &e) != 0) {
		kp_flow_say(f, "language %s: %s; the base's text", locale, e.msg);
		return;
	}
	for (k = 0; k < f->text.found.problem_count; k++) {
		kp_flow_say(f, "language %s: %s; that text stays the base's", locale, f->text.found.problems[k]);
	}
}

/* The glyphs of the language's characters (kp_locale_glyphs), the base's too (the vector fonts' text); none from a data
 * without Remastered's TrueType fonts (the 1.17 install), whose FNT fonts keep their own. */
static void read_glyphs(KpFlow *f)
{
	KpLocaleFound found;
	KpError e;
	int k, rc;
	double t0 = f->host.clock_ms ? f->host.clock_ms() : 0;
	memset(&found, 0, sizeof found);
	rc = kp_locale_glyphs(&f->glyphs, &f->data->ui, &f->data->raw, f->text.locale[0] ? &f->text : NULL, &found, &e);
	if (rc < 0) kp_flow_say(f, "language %s: %s; the fonts' own glyphs", f->flags->locale, e.msg);
	else if (rc > 0 && f->host.clock_ms) { /* rasterized at each start (ADR 0019, stage C3): its time on a phone */
		kp_flow_say(f, "glyphs of %s: %d characters in %.1f ms", f->flags->locale, f->glyphs.code_count, f->host.clock_ms() - t0);
	}
	for (k = 0; k < found.problem_count; k++) kp_flow_say(f, "language %s: %s", f->flags->locale, found.problems[k]);
	kp_locale_found_free(&found);
}

/* The languages listed and the start's text and speech chosen: --locale's, or the glue's (the checks and --mission keep
 * the base's) */
static void choose_languages(KpFlow *f)
{
	int glue = f->flags->locale[0] == 0 && !keeps_originals(f) && !starts_a_game(f);
	memset(&f->languages, 0, sizeof f->languages);
	if (f->flags->locale[0] != 0 || glue) {
		list_languages(f);
		choose_language(f);
		choose_speech(f);
	}
}

/* The language (ADR 0016; ADR 0019, stage C3): the glue's start chooses it and the speech among the languages the data
 * holds; then the speech's (read_speech) and, for a language chosen, its text and the glyphs. A part that cannot be had
 * leaves the base's, with one line. The checks and --mission keep the base's text with the original fonts. */
static void read_locale(KpFlow *f)
{
	free_locale(f);
	choose_languages(f);
	read_speech(f);
	if (f->flags->locale[0] == 0) return;
	read_text(f);
	read_glyphs(f);
	kp_locale_to_ui(f->text.locale[0] ? &f->text : NULL, &f->glyphs, &f->locale);
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
	const char *text = l->chosen_text >= 0 ? l->list[l->chosen_text].id : f->profile->text_locale;
	const char *speech = l->chosen_speech >= 0 ? l->list[l->chosen_speech].id : f->profile->speech_locale;
	char err[256];
	int text_there = language_files(f, text, 1), speech_there = language_files(f, speech, 1);
	if (!text_there || !speech_there) { /* asked again each frame; the glue stopped at the choice */
		if (!f->language_waiting) kp_flow_say(f, "glue: the language %s, the speech %s: waiting for their files", text, speech);
		f->language_waiting = 1;
		return;
	}
	f->language_waiting = 0;
	f->language_chosen = 0;
	if (l->chosen_text >= 0) snprintf(f->profile->text_locale, sizeof f->profile->text_locale, "%s", l->list[l->chosen_text].id);
	if (l->chosen_speech >= 0) {
		snprintf(f->profile->speech_locale, sizeof f->profile->speech_locale, "%s", l->list[l->chosen_speech].id);
	}
	f->host.save_profile(f->host.ctx);
	snprintf(f->flags->locale, sizeof f->flags->locale, "%s", f->profile->text_locale);
	snprintf(f->flags->speech, sizeof f->flags->speech, "%s", f->profile->speech_locale);
	kp_flow_say(f, "glue: the language %s, the speech %s", f->profile->text_locale, f->profile->speech_locale);
	kp_flow_free(f);
	f->host.language(f->host.ctx);
	f->language_restart = 1; /* the title is the start's, not a language's */
	if (kp_flow_init(f, err, sizeof err) != 0) {
		kp_flow_say(f, "glue: %s", err);
		f->host.quit(f->host.ctx);
	}
}

/* The first start (RunMainLoop 0x4679b0: the options' bit 0x800 cleared, then cinematics 0 and 24 before the glue): the
 * logo and the intro once the pack has them, the profile written at once (a start without the pack keeps them for one
 * with it); not for the checks and --cine-play's movies. 1 when the flow goes on from them (the title after). */
static int first_start(KpFlow *f)
{
	uint32_t *options = &f->profile->options;
	if (dev_checks(f) || f->flags->cine_play[0] || !(*options & KP_UI_OPTION_EXPANSION_INTRO) || !f->glue.cinematics) {
		return 0;
	}
	*options &= ~KP_UI_OPTION_EXPANSION_INTRO;
	f->host.save_profile(f->host.ctx);
	kp_flow_say(f, "glue: the first start's intro");
	cine_then(f, INTRO, 2, 0, THEN_TITLE, 0, KP_GLUE_MAIN);
	return 1;
}

void kp_flow_toggle_subtitles(KpFlow *f)
{
	if (!f->ui) return;
	f->ui->options ^= KP_UI_OPTION_SUBTITLES;
	if (keep_options(f, KP_UI_OPTION_SUBTITLES)) f->host.save_profile(f->host.ctx); /* no dialog: at once, its box alone */
	kp_flow_say(f, "flow: the subtitles %s", kp_flow_subtitles(f) ? "shown" : "hidden");
}

int kp_flow_subtitles(const KpFlow *f)
{
	return f->ui != NULL && (f->ui->options & KP_UI_OPTION_SUBTITLES) != 0;
}

int kp_flow_languages_ready(KpFlow *f)
{
	int text, speech;
	if (!f->host.language_files) return 1;
	if (!f->language_waiting) choose_languages(f); /* once: read_locale's choice, made again there */
	text = language_files(f, f->flags->locale, 1);
	speech = language_files(f, f->flags->speech, 1);
	if (text && speech) {
		f->language_waiting = 0;
		return 1;
	}
	if (!f->language_waiting) kp_flow_say(f, "language %s, speech %s: waiting for their files", f->flags->locale, f->flags->speech);
	f->language_waiting = 1;
	return 0;
}

/* --- the multiplayer screens' network (kp_netplay.h) --- */

/* The host address's buffers (the profile's, the join screen's popup) hold kp_net.h's form whole. */
typedef char kp_flow_host_sizes_agree[KP_PROFILE_HOST == KP_NET_HOST && KP_UI_ADDRESS == KP_NET_HOST ? 1 : -1];

#ifndef __EMSCRIPTEN__ /* the web has no network play (kp_netplay.h's TODO) */
static void netplay_log(void *ctx, const char *line) { kp_flow_say((KpFlow *)ctx, "%s", line); }

/* The join screen's Address: kept in the profile */
static void netplay_host_changed(void *ctx, const char *text)
{
	KpFlow *f = (KpFlow *)ctx;
	snprintf(f->profile->lan_host, sizeof f->profile->lan_host, "%s", text);
	f->host.save_profile(f->host.ctx);
}

static void netplay_lan_lock(void *ctx, int on)
{
	KpFlow *f = (KpFlow *)ctx;
	if (f->host.lan_lock) f->host.lan_lock(f->host.ctx, on);
}

static int netplay_entry_map(void *ctx, KpUi *ui, int index, KpMapHeader *h, char *path, size_t size)
{
	(void)ui;
	return kp_flow_entry_map((KpFlow *)ctx, index, h, path, size);
}

static int netplay_entry_save(void *ctx, KpUi *ui, int index, KpNetGameHeader *h)
{
	(void)ui;
	return kp_flow_entry_save((KpFlow *)ctx, index, h);
}

static int netplay_entry_replay(void *ctx, KpUi *ui, int index, KpNetReplayEntry *out)
{
	(void)ui;
	return kp_flow_entry_replay((KpFlow *)ctx, index, out);
}

static int netplay_find_save(void *ctx, uint32_t id, KpNetGameSave *out) { return kp_flow_find_save((KpFlow *)ctx, id, out); }

/* The chat room's preview world of the lobby's map */
static int netplay_preview(void *ctx, const char *path, const uint8_t *template_record, KpUiPreview *out)
{
	KpFlow *f = (KpFlow *)ctx;
	KpError e;
	if (kp_map_preview_load(&f->preview, &f->data->files, &f->data->dat, path, template_record, f->ui->menus.expansion,
		out, &e) == 0) {
		return 1;
	}
	kp_flow_say(f, "lobby: map preview of %s: %s", path, e.msg);
	return 0;
}

static void netplay_preview_release(void *ctx) { kp_map_preview_free(&((KpFlow *)ctx)->preview); }
#endif

/* The glue's network over the system's sockets (none on the web) */
static void netplay_open(KpFlow *f)
{
#ifndef __EMSCRIPTEN__
	KpNetPlayConfig c;
	memset(&c, 0, sizeof c);
	if (f->flags->lan_port >= 0 && f->flags->lan_port <= 0xffff)
		c.sock.port = (uint16_t)f->flags->lan_port;
	else
		kp_flow_say(f, "flow: --lan-port %d is not a port (0..65535): the default", f->flags->lan_port);
	c.sock.map_self = -1;
	if (f->flags->lan_map[0]) {
		unsigned base = 0;
		int hosts = 0, self = -1;
		if (sscanf(f->flags->lan_map, "%u:%d:%d", &base, &hosts, &self) < 2 || base == 0 || base > 0xffffu || hosts < 1
			|| self >= hosts || self < -1) {
			kp_flow_say(f, "flow: --lan-map %s is not BASE:HOSTS:SELF", f->flags->lan_map);
		} else {
			c.sock.map_base = (uint16_t)base;
			c.sock.map_hosts = hosts;
			c.sock.map_self = self;
		}
	}
	c.templates = &f->data->templates;
	c.transfer = kp_flow_transfer_io(f);
	c.entry_map = netplay_entry_map;
	c.entry_save = netplay_entry_save;
	c.entry_replay = netplay_entry_replay;
	c.find_save = netplay_find_save;
	c.preview = netplay_preview;
	c.preview_release = netplay_preview_release;
	c.log = netplay_log;
	/* the host by its address: --lan-host's for the run, else the profile's */
	snprintf(c.host, sizeof c.host, "%s", f->flags->lan_host[0] ? f->flags->lan_host : f->profile->lan_host);
	c.host_changed = netplay_host_changed;
	c.lan_lock = netplay_lan_lock;
	c.ctx = f;
	if (!(f->netplay = (KpNetPlay *)malloc(sizeof *f->netplay))) {
		kp_flow_say(f, "flow: no network: out of memory");
		return;
	}
	kp_netplay_init(f->netplay, f->ui, &c);
#else
	(void)f;
#endif
}

static void netplay_close(KpFlow *f)
{
	kp_map_preview_free(&f->preview);
	if (!f->netplay) return;
	kp_netplay_free(f->netplay);
	free(f->netplay);
	f->netplay = NULL;
	if (f->ui) f->ui->net = NULL;
}

int kp_flow_init(KpFlow *f, char *err, size_t err_size)
{
	char why[256];
	int restart = f->language_restart;
	f->language_restart = 0;
	f->current = -1;
	read_locale(f);
	if (!(f->ui = new_interface(f, why, sizeof why))) {
		snprintf(err, err_size, "%s", why);
		return -1;
	}
	if (f->ui->locale_problem[0]) kp_flow_say(f, "language %s: %s; that part stays the base's", f->flags->locale, f->ui->locale_problem);
	if (f->ui->speech_problem[0]) {
		kp_flow_say(f, "speech %s: %s; its voices' durations stay the base's", f->flags->speech, f->ui->speech_problem);
	}
	languages_to_ui(f, f->ui);
	console_art(f, f->ui);
	f->host.interface(f->host.ctx, f->ui);
	kp_flow_interface_host(f, f->ui);
	video_from_profile(f);
	kp_flow_video_to_ui(f, f->ui);
	/* the check boxes: the profile's or the originals', --subtitles over them */
	f->options_seen = (keeps_originals(f) ? f->ui->options : f->profile->options) & KP_UI_OPTIONS_KEPT;
	if (strcmp(f->flags->subtitles, "on") == 0) f->options_seen |= KP_UI_OPTION_SUBTITLES;
	else if (strcmp(f->flags->subtitles, "off") == 0) f->options_seen &= ~KP_UI_OPTION_SUBTITLES;
	else if (f->flags->subtitles[0]) kp_flow_say(f, "flow: --subtitles %s is not on or off", f->flags->subtitles);
	options_to_ui(f, f->ui);
	/* an older profile's saves' files get their names of now, once: before schema 3 their IDs', before 7 the UTF-8 name's */
	if (kp_profile_rename_old_saves(f->profile, kp_flow_rename_old_save, f)) f->host.save_profile(f->host.ctx);
	kp_glue_saves(f->ui, f->profile);
	if (f->flags->mission[0]) {
		int k;
		for (k = 0; k < f->data->mission_count; k++) {
			if (strcmp(f->data->missions[k].name, f->flags->mission) == 0) {
				if (!f->flags->play) f->flags->play = 1;
				return kp_flow_start_mission(f, k, err, err_size);
			}
		}
		snprintf(err, err_size, "no mission %s", f->flags->mission);
		return -1;
	}
	if (f->flags->seats && !f->flags->map[0]) {
		snprintf(err, err_size, "--seats needs --map");
		return -1;
	}
	if (f->flags->map[0]) {
		if (!f->flags->play) f->flags->play = 1;
		return kp_flow_play_map(f, f->flags->map, f->flags->map_template, err, err_size);
	}
	if (f->flags->watch[0]) {
		if (!f->flags->play) f->flags->play = 1;
		return kp_flow_watch(f, f->flags->watch, err, err_size);
	}
	if (f->flags->autotest) return 0; /* the autotest starts its games itself */
	kp_glue_init(&f->glue, f->ui, f->profile, now_ms(f));
	f->glue_on = 1;
	netplay_open(f);
	/* View Intro plays the logo and the intro the pack has */
	f->glue.cinematics = f->host.cine_has(f->host.ctx, KP_APP_CINE_LOGO)
		|| f->host.cine_has(f->host.ctx, KP_APP_CINE_EXPANSION_INTRO);
	if (first_start(f)) return 0;
	if (restart) kp_glue_menus(&f->glue, KP_GLUE_MAIN); /* a language chosen: back to the main menu */
	else kp_glue_title(&f->glue, now_ms(f));
	kp_flow_say(f, f->glue.screen == KP_GLUE_TITLE ? "glue: the title" : "glue: the main menu");
	return 0;
}

void kp_flow_free(KpFlow *f)
{
	leave_scene(f, 0);
	watch_free(f);
	netplay_close(f);
	if (f->glue_on) kp_glue_free(&f->glue);
	f->glue_on = 0;
	if (f->ui) kp_ui_free(f->ui);
	f->ui = NULL;
	free_locale(f);
	free(f->rep_tail);
	f->rep_tail = NULL;
	if (f->replay_play) kp_rep_free(f->replay_play);
	free(f->replay_play);
	f->replay_play = NULL;
	kp_flow_custom_close(f);
}
