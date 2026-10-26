/* The interface helpers of the C suites (kp_test_ui.h). */
#include "kp_test_ui.h"


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
