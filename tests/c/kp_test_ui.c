/* The interface helpers of the C suites (kp_test_ui.h). */
#include "kp_test_ui.h"

#include <ctype.h>

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

void kp_t_ui_input(KpUi *ui, int type, int64_t a, int64_t b)
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
	const KpUiControl *c = &ui->controls[d];
	size_t k, n = c->bin >= 0 && c->bin < ui->bin_count ? ui->bins[c->bin].name_len : 0;
	for (k = 0; k < n && k + 1 < size; k++) out[k] = (char)tolower((unsigned char)ui->bins[c->bin].name[k]);
	out[k] = '\0';
	return out;
}
