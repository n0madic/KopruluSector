/* The interface helpers of the C suites (kp_test_ui.h). */
#include "kp_test_ui.h"

#include <stdlib.h>
#include <string.h>

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
