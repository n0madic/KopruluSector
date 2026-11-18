/* The interface helpers of the C suites (kp_test_ui.h). */
#include "kp_test_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_game.h"
#include "kp_ids.h"
#include "kp_start.h"

static void free_ui(void *p)
{
	kp_oracle_ui_free((KpOracleUi *)p);
	free(p);
}

KpOracleUi *kp_t_ui(KpT *T, int flags)
{
	KpOracleUi *u = (KpOracleUi *)calloc(1, sizeof *u);
	KpError err;
	kp_t_ok(T, u != NULL, "out of memory");
	if (kp_oracle_ui_new(u, ".", flags, &err) != 0) {
		free(u);
		kp_t_fail(T, "%s", err.msg);
	}
	kp_t_defer(T, free_ui, u);
	return u;
}

/* A fixture container parsed: its sections point into kp_t_read's bytes. */
typedef struct {
	KpKpf c;
	KpUiData d;
} Fixture;

static void free_fixture(void *p)
{
	Fixture *f = (Fixture *)p;
	kp_ui_data_free(&f->d);
	kp_kpf_free(&f->c);
	free(f);
}

static Fixture *fixture(KpT *T, const char *name)
{
	char path[256];
	size_t size;
	uint8_t *data;
	KpError err;
	Fixture *f = (Fixture *)calloc(1, sizeof *f);
	kp_t_ok(T, f != NULL, "out of memory");
	snprintf(path, sizeof path, KP_T_FIXTURES "%s", name);
	data = kp_t_read(T, path, &size);
	if (kp_kpf_parse(&f->c, data, size, name, 1, &err) != 0) {
		free(f);
		kp_t_fail(T, "%s", err.msg);
	}
	kp_t_defer(T, free_fixture, f);
	return f;
}

static KpUiBytes part(KpT *T, const KpKpf *c, const char *fourcc, int optional)
{
	KpUiBytes b = { NULL, 0 };
	const KpKpfSection *s = kp_kpf_find(c, fourcc);
	kp_t_ok(T, s != NULL || optional, "the fixture lacks %s", fourcc);
	if (s) {
		b.data = s->data;
		b.size = s->size;
	}
	return b;
}

static void files(KpT *T, const KpKpf *c, const char *fourcc, KpUiFiles *out, int optional)
{
	KpUiBytes b = part(T, c, fourcc, optional);
	if (b.data) kp_t_ok(T, kp_ui_files_section(b.data, b.size, out) == 0, "the fixture's %s is malformed", fourcc);
}

const uint8_t *kp_t_fixture_section(KpT *T, const char *name, const char *fourcc, size_t *n)
{
	KpUiBytes b = part(T, &fixture(T, name)->c, fourcc, 0);
	*n = b.size;
	return b.data;
}

KpUiData *kp_t_fixture_ui_data(KpT *T)
{
	static const char *const FONTS[KP_UI_FNT_FONTS] = { "F8  ", "F10 ", "F14 ", "F16 ", "F16X" };
	Fixture *ui = fixture(T, "ui.kpd"), *fonts = fixture(T, "fonts.kpd");
	KpUiData *d = &ui->d;
	int k;
	d->profile = kp_defs_profile(ui->c.profile_id);
	d->dlgs = part(T, &ui->c, "DLGS", 0);
	d->pool = part(T, &ui->c, "POOL", 0);
	d->theme_fonts = part(T, &ui->c, "TFNT", 0);
	d->theme_effects = part(T, &ui->c, "TEFF", 1);
	files(T, &ui->c, "DGRP", &d->grps, 0);
	files(T, &ui->c, "PCXS", &d->pcx, 0);
	files(T, &ui->c, "GFIL", &d->glue, 0);
	files(T, &ui->c, "GHST", &d->ghosts, 1);
	files(T, &ui->c, "VFNT", &d->vector_fonts, 1);
	d->font_rows = part(T, &fonts->c, "TFGM", 0);
	for (k = 0; k < KP_UI_FNT_FONTS; k++) d->fonts[k] = part(T, &fonts->c, FONTS[k], 0);
	return d;
}

static void free_kp_ui(void *p) { kp_ui_free((KpUi *)p); }

KpUi *kp_t_fixture_ui(KpT *T)
{
	char err[256];
	KpUi *ui = kp_ui_new_data(kp_t_fixture_ui_data(T), err, sizeof err);
	kp_t_ok(T, ui != NULL, "%s", err);
	kp_t_defer(T, free_kp_ui, ui);
	return ui;
}

void kp_t_ui_input(KpUi *ui, int type, int64_t a, int64_t b)
{
	kp_oracle_ui_input(ui, type, a, b);
}

void kp_t_ui_idle(KpUi *ui) { kp_t_ui_input(ui, KP_UI_IDLE, ui->mouse_x, ui->mouse_y); }

int kp_t_ui_take_commands(KpUi *ui, uint8_t *first, size_t *first_size)
{
	uint8_t buf[KP_UI_COMMAND_BYTES];
	size_t n;
	int count = 0;
	if (first_size) *first_size = 0;
	while ((n = kp_ui_take_command(ui, buf, sizeof buf)) != 0) {
		if (count++ == 0 && first) {
			memcpy(first, buf, n);
			if (first_size) *first_size = n;
		}
	}
	return count;
}

const char *kp_t_ui_bin(const KpUi *ui, int32_t d, char *out, size_t size)
{
	kp_oracle_ui_bin_name(ui, ui->controls[d].bin, 1, out, size);
	return out;
}

static KpTNetHost *net_host(void *h) { return (KpTNetHost *)h; }

static void host_chat(void *h, const char *text, uint16_t recipients)
{
	snprintf(net_host(h)->chat, sizeof net_host(h)->chat, "%s", text);
	net_host(h)->recipients = recipients;
	net_host(h)->chats++;
}

static int host_latency(void *h) { return net_host(h)->latency; }

static int host_lagging(void *h, int slots[8], uint32_t *seconds)
{
	KpTNetHost *n = net_host(h);
	if (n->lagging < 0) return -1;
	memcpy(slots, n->slots, sizeof n->slots);
	*seconds = n->seconds;
	return n->lagging;
}

static void host_drop(void *h) { net_host(h)->drops++; }

void kp_t_net_host(KpUi *ui, KpTNetHost *h)
{
	memset(h, 0, sizeof *h);
	h->lagging = -1;
	h->net.game_chat = host_chat;
	h->net.game_latency = host_latency;
	h->net.game_lagging = host_lagging;
	h->net.game_drop = host_drop;
	h->net.host = h;
	ui->net = &h->net;
	ui->menus.multiplayer = 1;
}

#define NET_PAUSES 3

void kp_t_net_game(KpT *T, KpGame *g, int64_t alpha, int64_t beta, int64_t alpha_storm)
{
	int k;
	kp_t_eq(T, g->local_record, alpha, "the seat is Alpha's");
	g->setup.multiplayer = 1;
	g->setup.template_record[KP_TEMPLATE_ALLIES] = 1;
	for (k = 0; k < KP_SETUP_SLOTS; k++) g->setup.storm[k] = -1;
	g->setup.type[alpha] = g->setup.type[beta] = KP_CTRL_HUMAN;
	g->players.controller[alpha] = g->players.controller[beta] = KP_CTRL_HUMAN;
	g->setup.storm[alpha] = alpha_storm;
	g->setup.storm[beta] = 1 - alpha_storm;
	snprintf(g->setup.name[alpha], sizeof g->setup.name[alpha], "Alpha");
	snprintf(g->setup.name[beta], sizeof g->setup.name[beta], "Beta");
	g->pauses[alpha] = g->pauses[beta] = NET_PAUSES;
	kp_set_alliance(g, alpha, beta, 0);
}
