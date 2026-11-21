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

static void read_sockets(KpNetPlay *p, uint32_t ms)
{
	if (kp_net_sock_wait(p->sock, ms, deliver, p) < 0) say(p, "network: the sockets failed");
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
	while ((left = (int32_t)(until - kp_net_sock_now())) > 0) read_sockets(p, (uint32_t)left);
}

static void s_event(void *ud, const KpSnetEvent *e)
{
	KpNetPlay *p = (KpNetPlay *)ud;
	if (p->game_on) kp_netgame_event(&p->game, e);
}

/* ResetNetworkProvider 0x475a20, ShutdownNetwork 0x477f60: the game (Storm leaves it), SNet, the provider and the
 * sockets freed */
static void stop(KpNetPlay *p)
{
	if (p->game_on) kp_netgame_free(&p->game);
	p->game_on = 0;
	p->nu.game = NULL;
	kp_snet_free(p->snet);
	p->snet = NULL;
	if (p->snp_on) kp_snp_udp_free(&p->snp);
	p->snp_on = 0;
	kp_net_sock_close(p->sock);
	p->sock = NULL;
	if (p->locked && p->config.lan_lock) p->config.lan_lock(p->config.ctx, 0);
	p->locked = 0;
}

/* The host by its address given to the provider (none for ""); 0 when it is no address or has none. TODO: a name is
 * resolved here on the main thread (kp_net_sock_resolve waits for the system's resolver), at each provider selection
 * and at the Address popup's Ok, so a slow or unreachable DNS server stalls the screens: resolve it off the main
 * thread (kp_netplay_pump taking the answer) and let the popup wait for it. */
static int direct_host(KpNetPlay *p, const char *text)
{
	KpNetAddr a;
	char error[128];
	if (!text[0]) {
		if (p->snp_on) (void)kp_snp_udp_direct(&p->snp, NULL);
		return 1;
	}
	if (!kp_net_sock_resolve(text, &a, error, sizeof error)) {
		say(p, "network: %s", error);
		return 0;
	}
	if (p->snp_on) (void)kp_snp_udp_direct(&p->snp, &a);
	say(p, "network: the host %s asked at %s", text, kp_net_addr_text(&a, error, sizeof error));
	return 1;
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
	(void)direct_host(p, p->host); /* a host that has no address is only logged: the broadcast still lists */
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
	(void)kp_text_to_legacy(ui->menus.player, gc.player_name, sizeof gc.player_name); /* SetLocalPlayerName 0x475750 */
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

static int p_set_address(void *host, const char *text)
{
	KpNetPlay *p = np(host);
	char name[KP_NET_HOST];
	uint16_t port;
	if (!text || (text[0] && !kp_net_host_parse(text, name, sizeof name, &port))) {
		say(p, "network: \"%s\" is not an address", text ? text : "");
		return 0;
	}
	if (!direct_host(p, text)) return 0;
	snprintf(p->host, sizeof p->host, "%s", text);
	if (p->config.host_changed) p->config.host_changed(p->config.ctx, p->host);
	return 1;
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
