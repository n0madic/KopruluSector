/* The on-screen text lines (plan phase 9B; atlas P23 0x46a760..0x46b070): the 11-line message ring the game's texts
 * and messages go to, line 11 at (420, 24) and the error line, each with its expiry; DrawTextLines 0x46acd0 draws
 * them over the game view (DrawGameScreenLayer 0x445ea0). Koprulu draws them into a layer of their own, index 0
 * transparent; the screen's dirty cells (0x4cac50) are the layer's, not the dialogs' region. */
#include "kp_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_game.h"

#define RING 11
#define LINE_CHAT 11
#define LINE_ERROR 12
#define FONT_SLOT_10 1         /* 0x6d3028 */
#define WRAP_WIDTH 620         /* 0x6d303c in AddTextLines */
#define RING_X 10
#define RING_Y 0x70
#define RING_RIGHT 630
#define RING_SPAN 183          /* the ring's lines spread over 183 pixels */
#define ERROR_Y 0x127
#define CHAT_X 420
#define CHAT_Y 24
#define CHAT_RIGHT 620
#define RING_FORMAT 0x11
#define ERROR_FORMAT 0x12
#define CHAT_FORMAT 0x14
#define MESSAGE_MS 7000
#define ERROR_COLOUR 4
#define SYSTEM_COLOUR 3        /* DisplaySystemMessage 0x46b070's DL from 0x4261f0 */
#define TRIGGER_COLOUR 2
#define UNPLACED_STRING 0x12   /* network.tbl: "%s ... (%d, %d)", the unit that could not be placed */
#define FORMAT_SIZE 256        /* the original's buffer */

static KpUiText *select_font(KpUi *ui)
{
	(void)kp_ui_text_select_font(&ui->text, ui->fonts[FONT_SLOT_10], ui->font_sizes[FONT_SLOT_10]);
	return &ui->text;
}

/* InitTextLineMetrics 0x46a7f0: the line height, spread over the ring's span when the lines are short. */
static void metrics(KpUi *ui)
{
	KpUiLines *l = &ui->lines;
	KpUiText *t = select_font(ui);
	l->height = (uint16_t)(t->gap + t->max_height);
	l->spacing = l->height;
	if ((uint32_t)l->height * RING < RING_SPAN) l->spacing = (uint16_t)(l->height + (RING_SPAN - RING * l->height) / RING);
	t->font = NULL;
	l->next = RING - 1;
}

int kp_ui_lines_reset(KpUi *ui)
{
	KpUiLines *l = &ui->lines;
	uint8_t *pixels = l->layer.pixels;
	memset(l, 0, sizeof *l);
	if (pixels == NULL) pixels = (uint8_t *)malloc((size_t)KP_UI_SCREEN_W * KP_UI_SCREEN_H);
	if (pixels == NULL) return -1;
	l->layer.w = KP_UI_SCREEN_W;
	l->layer.h = KP_UI_SCREEN_H;
	l->layer.pixels = pixels;
	l->changed = 1;
	metrics(ui);
	return 0;
}

void kp_ui_lines_add(KpUi *ui, const uint8_t *text, uint8_t colour, uint32_t expiry, uint8_t flag)
{
	KpUiLines *l = &ui->lines;
	KpUiText *t;
	size_t at = 0;
	if (l->layer.pixels == NULL || text == NULL) return;
	t = select_font(ui);
	t->clip_left = 0;
	t->clip_top = 0;
	t->clip_right = WRAP_WIDTH;
	while (text[at] != 0) {
		size_t end, n;
		uint8_t s;
		t->x = 0;
		end = kp_ui_find_line_break(t, text, at);
		if (end == at) { /* no break: mid-word */
			t->x = 0;
			t->wrap = 0;
			end = kp_ui_find_line_break(t, text, at);
			t->wrap = 1;
		}
		s = l->next;
		l->next = (uint8_t)(s + 1 < RING ? s + 1 : 0);
		l->expiry[s] = expiry;
		l->flag[s] = flag;
		l->colour[s] = colour;
		n = end - at < KP_UI_LINE_SIZE - 1 ? end - at : KP_UI_LINE_SIZE - 1; /* SStrCopy 501 */
		memcpy(l->text[s], text + at, n);
		l->text[s][n] = 0;
		at = end;
	}
	t->font = NULL;
	l->changed = 1;
}

void kp_ui_error_line(KpUi *ui, const uint8_t *text)
{
	KpUiLines *l = &ui->lines;
	size_t n;
	if (l->layer.pixels == NULL || text == NULL) return;
	n = strlen((const char *)text);
	if (n > KP_UI_LINE_SIZE - 1) n = KP_UI_LINE_SIZE - 1;
	memcpy(l->text[LINE_ERROR], text, n);
	l->text[LINE_ERROR][n] = 0;
	l->colour[LINE_ERROR] = ERROR_COLOUR;
	l->expiry[LINE_ERROR] = ui->now + MESSAGE_MS;
	l->changed = 1;
}

void kp_ui_lines_clear_trigger(KpUi *ui)
{
	KpUiLines *l = &ui->lines;
	int k;
	for (k = 0; k < RING; k++) {
		if (l->text[k][0] != 0 && (l->flag[k] & 1)) {
			l->text[k][0] = 0;
			l->changed = 1;
		}
	}
}

void kp_ui_lines_expire(KpUi *ui)
{
	KpUiLines *l = &ui->lines;
	int k;
	for (k = 0; k < KP_UI_LINES; k++) {
		if (l->text[k][0] != 0 && (int32_t)(ui->now - l->expiry[k]) >= 0) {
			l->text[k][0] = 0;
			l->changed = 1;
		}
	}
}

static void draw_line(KpUi *ui, const uint8_t *text, uint8_t colour, uint8_t format, int32_t x, int32_t y, int16_t right)
{
	KpUiText *t = &ui->text;
	kp_ui_select_color(t, colour);
	t->clip_right = right;
	t->flags = format;
	t->clip_left = (int16_t)x;
	t->clip_top = (int16_t)y;
	t->clip_bottom = (int16_t)(y + ui->lines.height);
	(void)kp_ui_draw_text(t, x, y, text);
}

const KpUiSurface *kp_ui_lines_layer(KpUi *ui)
{
	KpUiLines *l = &ui->lines;
	KpUiSurface *target;
	int32_t y = RING_Y;
	int k;
	uint8_t s;
	if (l->layer.pixels == NULL) return NULL;
	if (!l->changed) return &l->layer;
	memset(l->layer.pixels, 0, (size_t)l->layer.w * (size_t)l->layer.h);
	target = ui->text.target;
	ui->text.target = &l->layer;
	(void)select_font(ui);
	for (k = 0, s = l->next; k < RING; k++, s = (uint8_t)((s + 1) % RING)) {
		if (l->text[s][0] != 0) draw_line(ui, l->text[s], l->colour[s], RING_FORMAT, RING_X, y, RING_RIGHT);
		y += l->spacing;
	}
	if (l->text[LINE_ERROR][0] != 0) {
		draw_line(ui, l->text[LINE_ERROR], l->colour[LINE_ERROR], ERROR_FORMAT, RING_X, ERROR_Y, RING_RIGHT);
	}
	if (l->text[LINE_CHAT][0] != 0) {
		draw_line(ui, l->text[LINE_CHAT], l->colour[LINE_CHAT], CHAT_FORMAT, CHAT_X, CHAT_Y, CHAT_RIGHT);
	}
	ui->text.font = NULL;
	ui->text.target = target;
	l->changed = 0;
	return &l->layer;
}

/* The original's SStrVPrintf of a table string with a name and two numbers (%s, %d, %%; anything else is copied). */
static void format_name_xy(char *out, size_t size, const uint8_t *format, const uint8_t *name, int64_t x, int64_t y)
{
	size_t at = 0;
	int numbers = 0;
	const uint8_t *f = format;
	out[0] = 0;
	if (f == NULL) return;
	while (*f != 0 && at + 1 < size) {
		if (f[0] == '%' && f[1] == 's') {
			const uint8_t *n = name;
			while (n != NULL && *n != 0 && at + 1 < size) out[at++] = (char)*n++;
			f += 2;
		} else if (f[0] == '%' && f[1] == 'd') {
			char digits[24];
			int k;
			snprintf(digits, sizeof digits, "%d", (int)(numbers++ == 0 ? x : y));
			for (k = 0; digits[k] != 0 && at + 1 < size; k++) out[at++] = digits[k];
			f += 2;
		} else if (f[0] == '%' && f[1] == '%') {
			out[at++] = '%';
			f += 2;
		} else {
			out[at++] = (char)*f++;
		}
	}
	out[at] = 0;
}

void kp_ui_game_event(KpUi *ui, int64_t kind, int64_t a, int64_t b, int64_t c, const char *text)
{
	if (!kp_ui_sound_played(ui, kind, a, b, c)) kp_ui_sound_event(ui, kind, a, b, c);
	switch (kind) {
	case KP_EVENT_ERROR: kp_ui_error_line(ui, kp_ui_tbl(ui, c ? KP_UI_NETWORK : KP_UI_STAT_TXT, (uint32_t)b)); break;
	case KP_EVENT_NETWORK_TEXT: kp_ui_error_line(ui, kp_ui_tbl(ui, KP_UI_NETWORK, (uint32_t)a)); break;
	case KP_EVENT_GAME_EXIT: ui->game_exit = (uint16_t)a; break;
	case KP_EVENT_SAVE_GAME: kp_ui_saving(ui, text != NULL ? text : ""); break;
	case KP_EVENT_CURSOR_MODES: kp_ui_cursor_modes_ended(ui, a != 0); break;
	case KP_EVENT_UNIT_UNPLACED: {
		char line[FORMAT_SIZE];
		format_name_xy(line, sizeof line, kp_ui_tbl(ui, KP_UI_NETWORK, UNPLACED_STRING), kp_ui_unit_type_name(ui, a), b, c);
		kp_ui_lines_add(ui, (const uint8_t *)line, SYSTEM_COLOUR, ui->now + MESSAGE_MS, 0);
		break;
	}
	case KP_EVENT_TRIGGER_TEXT: /* Display Text 0x4b6300, Transmission 0x4b60e0: with subtitles or the action's flag */
		if (ui->game != NULL && ((ui->options & KP_UI_OPTION_SUBTITLES) || c)) {
			kp_ui_lines_add(ui, (const uint8_t *)kp_map_string(ui->game, a), TRIGGER_COLOUR, ui->now + (uint32_t)b, 1);
		}
		break;
	case KP_EVENT_TRIGGER_SKIP: /* 0x4bda80: the trigger texts and the trigger portrait end */
		kp_ui_lines_clear_trigger(ui);
		kp_ui_portrait_trigger_stop(ui);
		break;
	case KP_EVENT_PORTRAIT:
		if (ui->game != NULL) {
			int32_t x = -1, y = -1;
			if (c != 0) {
				int32_t s = kp_unit_sprite(ui->game, c);
				x = (int16_t)ui->game->w->spr.x[s];
				y = (int16_t)ui->game->w->spr.y[s];
			}
			kp_ui_portrait_trigger(ui, a, x, y, (uint32_t)b);
		}
		break;
	case KP_EVENT_MINIMAP_PING: kp_ui_minimap_ping(ui, (uint32_t)a, (uint32_t)b, (uint8_t)c); break;
	case KP_EVENT_SELECTION_SENT: { /* the selection as the game left it then: a step may take several units out */
		int64_t units[KP_SLOTS];
		int n = 0;
		while (n < KP_SLOTS && (units[n] = kp_selection_sent_unit(a, b, c, n)) != 0) n++;
		if (ui->game != NULL) kp_ui_selection_sent(ui, units, n);
		break;
	}
	case KP_EVENT_SELECTION_GONE: kp_ui_selection_gone(ui, a); break;
	case KP_EVENT_TRIGGER_PAUSE: /* TrigActUnpauseGame 0x4b6080: the cursor to the view's centre */
		if (a == 0) kp_ui_warp_cursor(ui);
		break;
	case KP_EVENT_CAMERA: /* TrigActCenterView 0x4b6350 */
		if (c == 0) kp_ui_start_pan(ui, (int32_t)a, (int32_t)b);
		else kp_ui_end_pan(ui);
		break;
	case KP_EVENT_GAME_RESULT: (void)kp_ui_show_result(ui, (int)a); break;
	default: break;
	}
	(void)text;
}
