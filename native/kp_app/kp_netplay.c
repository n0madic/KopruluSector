/* The product's network play (kp_netplay.h). */
#include "kp_netplay.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_text.h"

#define PROGRAM_STAR 0x53544152u /* 'STAR': FillProgramInfo 0x46f330's program id of the original game */
#define PROGRAM_SEXP 0x53455850u /* 'SEXP': of the expansion's */
#define PROGRAM_VERSION 0xd5u    /* the version Storm's joins compare (0x66cca8) */
#define PROVIDER_PLAYERS 8       /* the provider entry's min(caps' maximum players, 8) (0x4bad40) */
#define NETTBL_PROVIDER 0x53     /* InitNetworkProvider 0x4757e0's report of a provider that did not initialize */
#define NETTBL_NETWORK_ERROR 0x51 /* "A network error has occured." */
#define PUMP_POLL_MS 10u         /* the pump thread's longest wait without a wake socket (PumpUntilControl's pass) */
#define JOIN_PASS_MS 10u         /* a blocking join's wait between its passes (PumpUntilControl's Sleep(10)) */

static KpNetPlay *np(void *host) { return (KpNetPlay *)((KpNetUi *)host)->owner; }

static void say(const KpNetPlay *p, const char *fmt, ...)
{
	char line[256];
	va_list ap;
	if (!p->config.log) return;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	p->config.log(p->config.ctx, line);
}

/* --- the provider's stack --- */

/* What arrived handed to the provider, under the lock: the pump thread reads the sockets without it */
static void deliver(void *ud, int socket, const KpNetAddr *from, const uint8_t *data, size_t size)
{
	KpNetPlay *p = (KpNetPlay *)ud;
	kp_net_lock(p->lock);
	if (kp_snp_udp_input(&p->snp, socket, from, data, size)) p->signalled = 1;
	kp_net_unlock(p->lock);
}

/* The sockets failed: read no more until the provider is chosen again; the player told once (on the main thread) */
static void sockets_failed(KpNetPlay *p)
{
	p->sock_failed = 1;
	say(p, "network: the sockets failed: no more reads until the provider is chosen again");
	kp_ui_menus_net_error(p->nu.ui, NETTBL_NETWORK_ERROR);
}

/* What arrived. Sockets that failed (a closed socket: a phone's system after the background) are read no more, and the
 * player is told once with network.tbl's network error; choosing the provider again opens new ones. */
static void read_sockets(KpNetPlay *p, uint32_t ms)
{
	if (p->sock_failed) return;
	if (kp_net_sock_wait(p->sock, ms, deliver, p) < 0) sockets_failed(p);
}

/* Storm's pump thread 0x15022780's pass: when a datagram arrived or a send signalled it, or its timer ran out. The
 * milliseconds to the next pass, KP_SNET_INFINITE none. Under the lock. */
static uint32_t storm_pass(KpNetPlay *p)
{
	uint32_t now = kp_net_sock_now(), t;
	int sig = p->signalled | kp_snet_take_signal(p->snet);
	if (sig || (p->wake != KP_SNET_INFINITE && (int32_t)(now - p->wake) >= 0)) {
		p->signalled = 0;
		t = kp_snet_pump(p->snet, sig);
		p->wake = t == KP_SNET_INFINITE ? KP_SNET_INFINITE : now + t;
	}
	if (p->wake == KP_SNET_INFINITE) return KP_SNET_INFINITE;
	return (int32_t)(p->wake - now) > 0 ? p->wake - now : 0;
}

/* The pump thread: the sockets read and Storm pumped until the provider stops or the sockets fail, the lock released
 * while it waits. Without a wake socket a send's signal waits at most PUMP_POLL_MS. */
static void pump_thread(void *ud)
{
	KpNetPlay *p = (KpNetPlay *)ud;
	int n;
	kp_net_lock(p->lock);
	while (!p->stopping) {
		uint32_t ms = storm_pass(p);
		if (!p->wakes && ms > PUMP_POLL_MS) ms = PUMP_POLL_MS;
		kp_net_unlock(p->lock);
		n = kp_net_sock_wait(p->sock, ms, deliver, p);
		kp_net_lock(p->lock);
		if (n < 0) {
			p->pump_failed = 1;
			break;
		}
	}
	kp_net_unlock(p->lock);
}

/* The pump thread started over the provider's stack (none: the frames pump, kp_netplay_pump) */
static void start_pump(KpNetPlay *p)
{
	if (!(p->lock = kp_net_lock_new())) return;
	p->stopping = p->pump_failed = 0;
	p->wakes = kp_net_sock_wake(p->sock); /* a first wake, harmless: whether the socket is there */
	if ((p->pump = kp_net_thread_start(pump_thread, p))) return;
	kp_net_lock_free(p->lock);
	p->lock = NULL;
}

/* The pump thread ended and joined, its lock freed */
static void stop_pump(KpNetPlay *p)
{
	if (p->pump) {
		kp_net_lock(p->lock);
		p->stopping = 1;
		kp_net_unlock(p->lock);
		(void)kp_net_sock_wake(p->sock);
		kp_net_thread_join(p->pump);
		p->pump = NULL;
	}
	kp_net_lock_free(p->lock);
	p->lock = NULL;
}

/* Storm's events (inside its receive calls, the lock released around them) */
static uint32_t s_now(void *ud)
{
	(void)ud;
	return kp_net_sock_now();
}

static void s_event(void *ud, const KpSnetEvent *e)
{
	KpNetPlay *p = (KpNetPlay *)ud;
	if (p->game_on) kp_netgame_event(&p->game, e);
}

static void s_unlock(void *ud) { kp_net_unlock(((KpNetPlay *)ud)->lock); }
static void s_lock(void *ud) { kp_net_lock(((KpNetPlay *)ud)->lock); }

/* --- the game's calls into Storm, under the lock (Storm's critical section) --- */

static KpNetPlay *cp(void *ud) { return (KpNetPlay *)ud; }

static KpSnet *enter(KpNetPlay *p)
{
	kp_net_lock(p->lock);
	return p->snet;
}

/* A call ended: its error kept for the game (the pump's calls may set Storm's), a send's signal handed to the pump
 * thread and the thread woken (Storm's pump event), the lock released */
static int done(KpNetPlay *p, int r)
{
	int wake = 0;
	p->error = kp_snet_error(p->snet);
	if (kp_snet_take_signal(p->snet)) {
		p->signalled = 1;
		wake = p->pump != NULL;
	}
	kp_net_unlock(p->lock);
	if (wake) (void)kp_net_sock_wake(p->sock);
	return r;
}

/* A buffer of at least size bytes: 1, 0 out of memory */
static int room(uint8_t **buf, size_t *have, size_t size)
{
	uint8_t *b;
	if (size <= *have) return 1;
	if (!(b = (uint8_t *)realloc(*buf, size))) return 0;
	*buf = b;
	*have = size;
	return 1;
}

/* A turn or a message this seat sends: its own copy, read back, holds as many bytes */
static int sent(KpNetPlay *p, uint32_t size)
{
	if (size > p->local_max) p->local_max = size;
	return room(&p->turns, &p->turns_room, (size_t)KP_NETGAME_PLAYERS * KP_SNP_DATAGRAM + p->local_max) &&
		room(&p->message, &p->message_room, (size_t)KP_SNP_DATAGRAM + p->local_max);
}

static int no_memory(KpNetPlay *p)
{
	p->error = KP_SNET_ERROR_NO_MEMORY;
	return 0;
}

static int l_create(void *ud, const char *name, const char *password, const char *stats, uint32_t game_type,
	uint32_t ladder_type, uint32_t mode, const void *tmpl, uint32_t tmpl_size, uint32_t max_players,
	const char *player_name, const char *player_desc, int *player_id)
{
	KpSnet *s = enter(cp(ud));
	return done(cp(ud), kp_snet_create_game(s, name, password, stats, game_type, ladder_type, mode, tmpl, tmpl_size,
		max_players, player_name, player_desc, player_id));
}

static int l_join_begin(void *ud, uint32_t game_id, const char *name, const char *password, const char *player_name,
	const char *player_desc)
{
	KpSnet *s = enter(cp(ud));
	return done(cp(ud), kp_snet_join_begin(s, game_id, name, password, player_name, player_desc));
}

static int l_join_poll(void *ud, int *player_id)
{
	KpSnet *s = enter(cp(ud));
	return done(cp(ud), kp_snet_join_poll(s, player_id));
}

/* SNetJoinGame's blocking join (the calls' join_game; the flow joins by join_begin and join_poll): the handshake's
 * passes every JOIN_PASS_MS with the lock released between them, the sockets read meanwhile by the pump thread or here
 * (kp_snet_join_game would hold the lock through its waits, and nothing would read the answer) */
static int l_join(void *ud, uint32_t game_id, const char *name, const char *password, const char *player_name,
	const char *player_desc, int *player_id)
{
	KpNetPlay *p = cp(ud);
	int r;
	if (!l_join_begin(ud, game_id, name, password, player_name, player_desc)) return 0;
	while ((r = l_join_poll(ud, player_id)) < 0) {
		if (p->pump || p->sock_failed) kp_net_sock_sleep(JOIN_PASS_MS);
		else read_sockets(p, JOIN_PASS_MS);
	}
	return r;
}

static int l_leave(void *ud, uint32_t reason)
{
	KpSnet *s = enter(cp(ud));
	return done(cp(ud), kp_snet_leave_game(s, reason));
}

static int l_enum(void *ud, int (*fn)(void *fn_ud, const KpSnetGame *g), void *fn_ud, uint32_t *hint)
{
	KpSnet *s = enter(cp(ud));
	return done(cp(ud), kp_snet_enum_games(s, fn, fn_ud, hint));
}

static int l_send_turn(void *ud, const void *data, uint32_t size)
{
	KpNetPlay *p = cp(ud);
	KpSnet *s;
	if (!sent(p, size)) return no_memory(p);
	s = enter(p);
	return done(p, kp_snet_send_turn(s, data, size));
}

/* The turns copied before the lock is released: the pump thread may free Storm's (a record a control command
 * deletes) */
static int l_receive_turns(void *ud, int first, int count, const uint8_t **data, uint32_t *sizes, uint32_t *status)
{
	KpNetPlay *p = cp(ud);
	KpSnet *s = enter(p);
	size_t at = 0, total = 0;
	int r = kp_snet_receive_turns(s, first, count, data, sizes, status), k;
	if (r && p->pump) {
		for (k = 0; k < count; k++) total += data[k] ? sizes[k] : 0;
		if (!room(&p->turns, &p->turns_room, total)) {
			done(p, 0);
			return no_memory(p);
		}
		for (k = 0; k < count; k++) {
			if (!data[k]) continue;
			memcpy(p->turns + at, data[k], sizes[k]);
			data[k] = p->turns + at;
			at += sizes[k];
		}
	}
	return done(p, r);
}

static int l_in_transit(void *ud, uint32_t *turns)
{
	KpSnet *s = enter(cp(ud));
	return done(cp(ud), kp_snet_turns_in_transit(s, turns));
}

static int l_send_message(void *ud, int target, const void *data, uint32_t size)
{
	KpNetPlay *p = cp(ud);
	KpSnet *s;
	if (!sent(p, size)) return no_memory(p);
	s = enter(p);
	return done(p, kp_snet_send_message(s, target, data, size));
}

static int l_receive_message(void *ud, int *sender, const uint8_t **data, uint32_t *size)
{
	KpNetPlay *p = cp(ud);
	KpSnet *s = enter(p);
	int r = kp_snet_receive_message(s, sender, data, size);
	if (r && p->pump && data && size) {
		if (!room(&p->message, &p->message_room, *size)) {
			done(p, 0);
			return no_memory(p);
		}
		memcpy(p->message, *data, *size);
		*data = p->message;
	}
	return done(p, r);
}

static int l_game_info(void *ud, int type, void *buf, uint32_t size, uint32_t *written)
{
	KpSnet *s = enter(cp(ud));
	return done(cp(ud), kp_snet_game_info(s, type, buf, size, written));
}

static int l_set_mode(void *ud, uint32_t mode, int make_private)
{
	KpSnet *s = enter(cp(ud));
	return done(cp(ud), kp_snet_set_game_mode(s, mode, make_private));
}

static int l_player_name(void *ud, int id, char *buf, uint32_t size)
{
	KpSnet *s = enter(cp(ud));
	return done(cp(ud), kp_snet_player_name(s, id, buf, size));
}

static int l_provider_caps(void *ud, KpSnetCaps *caps)
{
	KpSnet *s = enter(cp(ud));
	return done(cp(ud), kp_snet_provider_caps(s, caps));
}

static int l_player_caps(void *ud, int id, KpSnetCaps *caps)
{
	KpSnet *s = enter(cp(ud));
	return done(cp(ud), kp_snet_player_caps(s, id, caps));
}

static int l_drop(void *ud, int id, uint32_t reason)
{
	KpSnet *s = enter(cp(ud));
	return done(cp(ud), kp_snet_drop_player(s, id, reason));
}

static int l_disconnect(void *ud, uint32_t reason)
{
	KpSnet *s = enter(cp(ud));
	return done(cp(ud), kp_snet_disconnect_all(s, reason));
}

static uint32_t l_error(void *ud) { return cp(ud)->error; }

static KpNetCalls locked_calls(KpNetPlay *p)
{
	KpNetCalls c;
	memset(&c, 0, sizeof c);
	c.create_game = l_create;
	c.join_game = l_join;
	c.join_begin = l_join_begin;
	c.join_poll = l_join_poll;
	c.leave_game = l_leave;
	c.enum_games = l_enum;
	c.send_turn = l_send_turn;
	c.receive_turns = l_receive_turns;
	c.turns_in_transit = l_in_transit;
	c.send_message = l_send_message;
	c.receive_message = l_receive_message;
	c.game_info = l_game_info;
	c.set_game_mode = l_set_mode;
	c.player_name = l_player_name;
	c.provider_caps = l_provider_caps;
	c.player_caps = l_player_caps;
	c.drop_player = l_drop;
	c.disconnect_all = l_disconnect;
	c.error = l_error;
	c.ud = p;
	return c;
}

/* ResetNetworkProvider 0x475a20, ShutdownNetwork 0x477f60: the game (Storm leaves it), SNet, the provider, the
 * sockets and the lookups of host addresses freed */
static void stop(KpNetPlay *p)
{
	stop_pump(p); /* before the stack it pumps goes */
	kp_net_lookup_free(p->host_lookup);
	p->host_lookup = NULL;
	kp_net_lookup_free(p->asked);
	p->asked = NULL;
	if (p->game_on) kp_netgame_free(&p->game);
	p->game_on = 0;
	p->nu.game = NULL;
	kp_snet_free(p->snet);
	p->snet = NULL;
	if (p->snp_on) kp_snp_udp_free(&p->snp);
	p->snp_on = 0;
	kp_net_sock_close(p->sock);
	p->sock = NULL;
	p->sock_failed = 0;
	if (p->locked && p->config.lan_lock) p->config.lan_lock(p->config.ctx, 0);
	p->locked = 0;
}

/* --- the host by its address: its lookup off the main thread (kp_net_lookup_*), the answer taken each frame --- */

/* The provider asks the host `text` at a (NULL: no host) */
static void direct(KpNetPlay *p, const char *text, const KpNetAddr *a)
{
	char shown[32];
	if (p->snp_on) {
		kp_net_lock(p->lock);
		(void)kp_snp_udp_direct(&p->snp, a);
		kp_net_unlock(p->lock);
	}
	if (a) say(p, "network: the host %s asked at %s", text, kp_net_addr_text(a, shown, sizeof shown));
}

/* The player's address taken: kept, and asked at a (NULL: none, for "") */
static void take_host(KpNetPlay *p, const char *text, const KpNetAddr *a)
{
	kp_net_lookup_free(p->host_lookup); /* the address it looked up is no longer the host's */
	p->host_lookup = NULL;
	snprintf(p->host, sizeof p->host, "%s", text);
	direct(p, p->host, a);
	if (p->config.host_changed) p->config.host_changed(p->config.ctx, p->host);
}

/* The lookup's answer (1 the address into *a, 0 none: logged, -1 not yet); a lookup that answered is freed */
static int answer(KpNetPlay *p, KpNetLookup **l, KpNetAddr *a)
{
	char error[128];
	int r = kp_net_lookup_poll(*l, a, error, sizeof error);
	if (r < 0) return r;
	if (r == 0) say(p, "network: %s", error);
	kp_net_lookup_free(*l);
	*l = NULL;
	return r;
}

/* The lookups' answers: the host's given to the provider; the popup's address, while the popup waits for it, taken or
 * refused there (one the popup no longer waits for, its Cancel, is dropped) */
static void take_answers(KpNetPlay *p)
{
	KpNetAddr a;
	int r;
	if (p->host_lookup && (r = answer(p, &p->host_lookup, &a)) > 0) direct(p, p->host, &a);
	if (p->asked && (r = answer(p, &p->asked, &a)) >= 0) {
		if (!kp_ui_menus_address_found(p->nu.ui, r)) say(p, "network: the address %s dropped", p->asked_text);
		else if (r) take_host(p, p->asked_text, &a);
	}
}

/* --- the provider hooks --- */

static int p_providers(void *host, KpUiProvider *out, int max)
{
	(void)host;
	if (max < 1) return 0;
	memset(out, 0, sizeof *out);
	out->id = KP_SNP_UDP_ID;
	out->flags = KP_SNET_UDP_CAPS.flags & 2u;
	out->max_players = KP_SNET_UDP_CAPS.maxplayers < PROVIDER_PLAYERS ? KP_SNET_UDP_CAPS.maxplayers : PROVIDER_PLAYERS;
	snprintf(out->name, sizeof out->name, "%s", KP_NETPLAY_UDP_NAME);
	return 1; /* the screen shows gluAll's requirements for 'UDPN', not the provider's description */
}

/* InitializeSelectedProvider 0x4bafa0 (InitNetworkProvider 0x4757e0) */
static int p_select(void *host, uint32_t id)
{
	KpNetPlay *p = np(host);
	KpUi *ui = p->nu.ui;
	KpNetIo io;
	KpSnetHooks sh;
	KpNetCalls calls;
	KpNetGameHooks gh;
	KpNetGameConfig gc;
	char error[128];
	uint32_t program = ui->menus.expansion ? PROGRAM_SEXP : PROGRAM_STAR;
	stop(p);
	if (id != KP_SNP_UDP_ID) return 0;
	if (!(p->sock = kp_net_sock_open(&p->config.sock, error, sizeof error))) {
		say(p, "network: %s", error);
		kp_ui_menus_net_error(ui, NETTBL_PROVIDER);
		return 0;
	}
	if (p->config.lan_lock) {
		p->config.lan_lock(p->config.ctx, 1);
		p->locked = 1;
	}
	io = kp_net_sock_io(p->sock);
	kp_snp_udp_init(&p->snp, &io, program, PROGRAM_VERSION, PROVIDER_PLAYERS);
	p->snp_on = 1;
	/* the host by its address asked once its lookup answers (kp_netplay_pump); one that has no address is only
	 * logged: the broadcast still lists */
	if (p->host[0]) p->host_lookup = kp_net_lookup_start(p->host);
	memset(&sh, 0, sizeof sh);
	sh.now = s_now;
	sh.event = s_event;
	sh.unlock = s_unlock;
	sh.lock = s_lock;
	sh.ud = p;
	p->signalled = 0;
	p->wake = KP_SNET_INFINITE;
	if (!(p->snet = kp_snet_new(&p->snp, &sh, program, PROGRAM_VERSION))) {
		say(p, "network: out of memory");
		stop(p);
		kp_ui_menus_net_error(ui, NETTBL_PROVIDER);
		return 0;
	}
	if (!sent(p, 0)) { /* the copies' first room */
		say(p, "network: out of memory");
		stop(p);
		kp_ui_menus_net_error(ui, NETTBL_PROVIDER);
		return 0;
	}
	calls = locked_calls(p);
	gh = kp_netui_game_hooks(&p->nu);
	memset(&gc, 0, sizeof gc);
	/* SetLocalPlayerName 0x475750 */
	(void)kp_text_to_legacy_or(ui->menus.player, gc.player_name, sizeof gc.player_name, KP_TEXT_LEGACY_DEFAULT);
	gc.templates = p->config.templates;
	gc.transfer = p->config.transfer;
	if (kp_netgame_init(&p->game, &calls, &gh, &gc) != 0) { /* the game reported why */
		stop(p);
		return 0;
	}
	p->game_on = 1;
	p->nu.game = &p->game;
	start_pump(p);
	say(p, "network: %s on ports %u and %u, Storm pumped %s", KP_NETPLAY_UDP_NAME,
		kp_net_sock_port(p->sock, KP_NET_SOCKET_LIST), kp_net_sock_port(p->sock, KP_NET_SOCKET_GAME),
		p->pump ? (p->wakes ? "on its thread" : "on its thread, without wakes") : "each frame");
	return 1;
}

static void p_stop(void *host) { stop(np(host)); }

static void p_address(void *host, char *out, size_t size)
{
	if (size) snprintf(out, size, "%s", np(host)->host);
}

/* The popup's address: "" and a dotted address taken at once, one that is no address refused, a name looked up (-1:
 * the popup waits for take_answers) */
static int p_set_address(void *host, const char *text)
{
	KpNetPlay *p = np(host);
	KpNetLookup *l;
	KpNetAddr a;
	int r;
	kp_net_lookup_free(p->asked); /* an earlier address the popup gave up */
	p->asked = NULL;
	if (text && !text[0]) {
		take_host(p, "", NULL);
		return 1;
	}
	if (!text || !(l = kp_net_lookup_start(text))) {
		say(p, "network: no lookup of \"%s\"", text ? text : "");
		return 0;
	}
	if ((r = answer(p, &l, &a)) > 0) take_host(p, text, &a);
	if (r >= 0) return r;
	p->asked = l;
	snprintf(p->asked_text, sizeof p->asked_text, "%s", text);
	say(p, "network: looking up %s", text);
	return -1;
}

static int n_entry_map(void *owner, KpUi *ui, int index, KpMapHeader *h, char *path, size_t size)
{
	KpNetPlay *p = (KpNetPlay *)owner;
	return p->config.entry_map != NULL && p->config.entry_map(p->config.ctx, ui, index, h, path, size);
}

static int n_entry_save(void *owner, KpUi *ui, int index, KpNetGameHeader *h)
{
	KpNetPlay *p = (KpNetPlay *)owner;
	return p->config.entry_save != NULL && p->config.entry_save(p->config.ctx, ui, index, h);
}

static int n_entry_replay(void *owner, KpUi *ui, int index, KpNetReplayEntry *out)
{
	KpNetPlay *p = (KpNetPlay *)owner;
	return p->config.entry_replay != NULL && p->config.entry_replay(p->config.ctx, ui, index, out);
}

static int n_find_save(void *owner, uint32_t id, KpNetGameSave *out)
{
	KpNetPlay *p = (KpNetPlay *)owner;
	return p->config.find_save != NULL && p->config.find_save(p->config.ctx, id, out);
}

static int n_preview(void *owner, const char *path, const uint8_t *template_record, KpUiPreview *out)
{
	KpNetPlay *p = (KpNetPlay *)owner;
	return p->config.preview != NULL && p->config.preview(p->config.ctx, path, template_record, out);
}

static void n_preview_release(void *owner)
{
	KpNetPlay *p = (KpNetPlay *)owner;
	if (p->config.preview_release != NULL) p->config.preview_release(p->config.ctx);
}

/* --- the product's calls --- */

void kp_netplay_init(KpNetPlay *p, KpUi *ui, const KpNetPlayConfig *config)
{
	memset(p, 0, sizeof *p);
	p->config = *config;
	p->wake = KP_SNET_INFINITE;
	snprintf(p->host, sizeof p->host, "%s", config->host);
	p->nu.ui = ui;
	p->nu.owner = p;
	p->nu.entry_map = n_entry_map;
	p->nu.entry_save = n_entry_save;
	p->nu.entry_replay = n_entry_replay;
	p->nu.find_save = n_find_save;
	p->nu.preview = n_preview;
	p->nu.preview_release = n_preview_release;
	kp_netui_hooks(&p->nu, &p->net);
	p->net.providers = p_providers;
	p->net.select_provider = p_select;
	p->net.reset_provider = p_stop;
	p->net.shutdown = p_stop;
	p->net.address = p_address;
	p->net.set_address = p_set_address;
	ui->net = &p->net;
}

void kp_netplay_pump(KpNetPlay *p)
{
	take_answers(p);
	if (!p->sock || !p->snet) return;
	if (p->pump) {
		int failed;
		kp_net_lock(p->lock);
		failed = p->pump_failed;
		kp_net_unlock(p->lock);
		if (failed) { /* the thread ended: joined, and the frames pump runs Storm's timers on (no more reads) */
			stop_pump(p);
			if (!p->sock_failed) sockets_failed(p);
		}
	}
	if (!p->pump) {
		read_sockets(p, 0);
		(void)storm_pass(p);
	}
	kp_netui_poll(&p->nu);
}

void kp_netplay_leave(KpNetPlay *p)
{
	if (p->game_on) kp_netgame_abort(&p->game);
}

void kp_netplay_free(KpNetPlay *p)
{
	stop(p);
	kp_netui_free(&p->nu);
	free(p->turns);
	free(p->message);
	p->turns = p->message = NULL;
	p->turns_room = p->message_room = 0;
}
