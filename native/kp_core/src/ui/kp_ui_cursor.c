/* The cursor (plan phase 9B; cur.cpp, atlas P13 0x43e410..0x43e7b0): its GRP of the cursor table, the animation and
 * the frame the screen's cursor layer draws at the mouse. Which cursor the mouse shows (the hover, the targeting, the
 * edge scroll: GetPointerCursorId 0x43e6d0, GetEdgeScrollCursorId 0x43e7b0) is phase 9C's input. */
#include "kp_ui.h"

#include <stdio.h>
#include <string.h>

#define CURSOR_PERIOD 100 /* AnimateCursor's 100 ms */
#define HOTSPOT 63        /* the frame offsets are relative to the GRP's 128 x 128 box, the mouse at its middle */
#define FRAME_RECORD 8    /* GRP frame records {x, y, w, h, u32 data} after the 6-byte header */

static const char *const NAMES[] = { "arrow", "Illegal", "TargY", "TargR", "TargG", "TargN", "MagG", "MagR", "MagY",
	"Drag", "Time", "ScrollU", "ScrollUR", "ScrollR", "ScrollDR", "ScrollD", "ScrollDL", "ScrollL", "ScrollUL" };

static const KpUiFile *cursor_grp(const KpUi *ui)
{
	char path[32];
	if (ui->cursor_theme) { /* the glue screen's own arrow: its indices are its palette's */
		snprintf(path, sizeof path, "glue\\%s\\arrow.grp", kp_ui_glue_theme_dir(ui->cursor_theme - 1));
		return kp_ui_file(ui->grps, ui->grp_count, path);
	}
	if (ui->cursor >= sizeof NAMES / sizeof NAMES[0]) return NULL;
	snprintf(path, sizeof path, "cursor\\%s.grp", NAMES[ui->cursor]);
	return kp_ui_file(ui->grps, ui->grp_count, path);
}

static uint32_t frame_count(const KpUiFile *f) { return f->size < 2 ? 0 : (uint32_t)(f->data[0] | (f->data[1] & 0x7f) << 8); }

void kp_ui_cursor_set(KpUi *ui, uint16_t id)
{
	/* SetCursorId 0x43e9a0 changes the GRP only with a new id: the glue's arrow stays under the same id 0 */
	if (id != ui->cursor) ui->cursor_theme = 0;
	ui->cursor = id;
}

void kp_ui_cursor_set_theme(KpUi *ui, int theme) { ui->cursor_theme = kp_ui_glue_theme_dir(theme) ? (uint8_t)(theme + 1) : 0; }

void kp_ui_cursor_animate(KpUi *ui)
{
	const KpUiFile *f = cursor_grp(ui);
	if (f == NULL || frame_count(f) == 1) return;
	if ((int32_t)(ui->now - ui->cursor_next) < 0) return;
	ui->cursor_next = ui->now + CURSOR_PERIOD;
	ui->cursor_counter++;
}

int64_t kp_ui_cursor_key(KpUi *ui)
{
	const KpUiFile *f = cursor_grp(ui);
	uint32_t count;
	if (ui->cursor_hidden || f == NULL || (count = frame_count(f)) == 0) return -1;
	return (int64_t)ui->cursor_theme << 32 | (int64_t)ui->cursor << 16 | (int64_t)(ui->cursor_counter % count);
}

int kp_ui_cursor_frame(KpUi *ui, uint8_t *out, size_t capacity, int32_t *w, int32_t *h, int32_t *dx, int32_t *dy)
{
	const KpUiFile *f = cursor_grp(ui);
	KpUiSurface target, *saved;
	int32_t crop[4];
	uint32_t count;
	int frame;
	const uint8_t *rec;
	if (ui->cursor_hidden || f == NULL || (count = frame_count(f)) == 0) return -1;
	frame = (int)(ui->cursor_counter % count); /* unsigned, as DrawCursor 0x43e490 */
	if (kp_ui_grp_frame_size(f->data, f->size, frame, w, h) != 0 || (size_t)*w * (size_t)*h > capacity) return -1;
	rec = f->data + 6 + FRAME_RECORD * frame;
	*dx = rec[0] - HOTSPOT;
	*dy = rec[1] - HOTSPOT;
	memset(out, 0, (size_t)*w * (size_t)*h);
	target.w = *w;
	target.h = *h;
	target.pixels = out;
	saved = ui->text.target;
	ui->text.target = &target;
	crop[0] = 0;
	crop[1] = 0;
	crop[2] = *w;
	crop[3] = *h;
	kp_ui_draw_grp_rle(ui, 0, 0, f->data, f->size, frame, crop);
	ui->text.target = saved;
	return 0;
}
