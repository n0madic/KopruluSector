/* The product's network play (kp_netplay.h). */
#include "kp_netplay.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "kp_text.h"

#define PROGRAM_STAR 0x53544152u /* 'STAR': FillProgramInfo 0x46f330's program id of the original game */
#define PROGRAM_SEXP 0x53455850u /* 'SEXP': of the expansion's */
#define PROGRAM_VERSION 0xd5u    /* the version Storm's joins compare (0x66cca8) */
#define PROVIDER_PLAYERS 8       /* the provider entry's min(caps' maximum players, 8) (0x4bad40) */
#define NETTBL_PROVIDER 0x53     /* InitNetworkProvider 0x4757e0's report of a provider that did not initialize */
#define NETTBL_NETWORK_ERROR 0x51 /* "A network error has occured." */

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

static void deliver(void *ud, int socket, const KpNetAddr *from, const uint8_t *data, size_t size)
{
	KpNetPlay *p = (KpNetPlay *)ud;
	if (kp_snp_udp_input(&p->snp, socket, from, data, size)) p->signalled = 1;
}

/* What arrived. Sockets that failed (a closed socket: a phone's system after the background) are read no more, and the
 * player is told once with network.tbl's network error; choosing the provider again opens new ones. */
static void read_sockets(KpNetPlay *p, uint32_t ms)
{
	if (p->sock_failed) return;
	if (kp_net_sock_wait(p->sock, ms, deliver, p) >= 0) return;
	p->sock_failed = 1;
	say(p, "network: the sockets failed: no more reads until the provider is chosen again");
	kp_ui_menus_net_error(p->nu.ui, NETTBL_NETWORK_ERROR);
}

static uint32_t s_now(void *ud)
{
	(void)ud;
	return kp_net_sock_now();
}

/* SNetJoinGame's waits: the sockets read while Storm pumps itself */
static void s_sleep(void *ud, uint32_t ms)
{
	KpNetPlay *p = (KpNetPlay *)ud;
	uint32_t until = kp_net_sock_now() + ms;
	int32_t left;
	while ((left = (int32_t)(until - kp_net_sock_now())) > 0) {
		if (p->sock_failed) {
			kp_net_sock_sleep((uint32_t)left); /* nothing to read: no spin until Storm's timeout */
			return;
		}
		read_sockets(p, (uint32_t)left);
	}
}

static void s_event(void *ud, const KpSnetEvent *e)
{
	KpNetPlay *p = (KpNetPlay *)ud;
	if (p->game_on) kp_netgame_event(&p->game, e);
}

/* ResetNetworkProvider 0x475a20, ShutdownNetwork 0x477f60: the game (Storm leaves it), SNet, the provider, the
 * sockets and the lookups of host addresses freed */
static void stop(KpNetPlay *p)
{
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
	if (p->snp_on) (void)kp_snp_udp_direct(&p->snp, a);
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
	sh.sleep = s_sleep;
	sh.event = s_event;
	sh.ud = p;
	p->signalled = 0;
	p->wake = KP_SNET_INFINITE;
	if (!(p->snet = kp_snet_new(&p->snp, &sh, program, PROGRAM_VERSION))) {
		say(p, "network: out of memory");
		stop(p);
		kp_ui_menus_net_error(ui, NETTBL_PROVIDER);
		return 0;
	}
	calls = kp_netgame_snet_calls(p->snet);
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
	say(p, "network: %s on ports %u and %u", KP_NETPLAY_UDP_NAME, kp_net_sock_port(p->sock, KP_NET_SOCKET_LIST),
		kp_net_sock_port(p->sock, KP_NET_SOCKET_GAME));
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
	uint32_t now, t;
	int sig;
	take_answers(p);
	if (!p->sock || !p->snet) return;
	read_sockets(p, 0);
	/* Storm's pump thread 0x15022780: a pass when a datagram arrived or a send signalled it, or its timer ran out */
	now = kp_net_sock_now();
	sig = p->signalled | kp_snet_take_signal(p->snet);
	if (!sig && (p->wake == KP_SNET_INFINITE || (int32_t)(now - p->wake) < 0)) return;
	p->signalled = 0;
	t = kp_snet_pump(p->snet, sig);
	p->wake = t == KP_SNET_INFINITE ? KP_SNET_INFINITE : now + t;
}

void kp_netplay_leave(KpNetPlay *p)
{
	if (p->game_on) kp_netgame_abort(&p->game);
}

void kp_netplay_free(KpNetPlay *p)
{
	stop(p);
	kp_netui_free(&p->nu);
}
