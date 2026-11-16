/* The on-screen text lines (plan phase 9B; atlas P23 0x46a760..0x46b070): the 11-line message ring the game's texts
 * and messages go to, line 11 at (420, 24) and the error line, each with its expiry; DrawTextLines 0x46acd0 draws
 * them over the game view (DrawGameScreenLayer 0x445ea0). Koprulu draws them into a layer of their own, index 0
 * transparent; the screen's dirty cells (0x4cac50) are the layer's, not the dialogs' region. */
#include "kp_text.h"
#include "kp_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_game.h"
#include "kp_gametype.h"
#include "kp_ids.h"

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
#define FORCE_NAME_STRING 0x12 /* network.tbl: GetForceName 0x4b2bf0's 0x12 + the force, "Team 1".."Team 4" */
#define TIMED_MESSAGE_SOUND 23
#define FORMAT_SIZE 512        /* twice the original's buffer: UTF-8 (ADR 0016) */
#define CHAT_COLOUR 2          /* DisplayChatMessage 0x46a860: another sender's line colour */
#define CHAT_LOCAL_COLOUR 7    /* the local player's */
#define CHAT_OBSERVER_STRING 27 /* the tag of an observer's line (KP_CTRL_OBSERVER) */

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
		n = end - at < KP_UI_LINE_SIZE - 1 ? end - at : kp_text_floor(text + at, KP_UI_LINE_SIZE - 1); /* SStrCopy 501 */
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
	if (l->layer.pixels == NULL || text == NULL) return;
	(void)kp_text_copy((char *)l->text[LINE_ERROR], KP_UI_LINE_SIZE, (const char *)text);
	l->colour[LINE_ERROR] = ERROR_COLOUR;
	l->expiry[LINE_ERROR] = ui->now + MESSAGE_MS;
	l->changed = 1;
}

void kp_ui_timed_message(KpUi *ui, const uint8_t *text, uint32_t ms)
{
	if (text == NULL) return;
	kp_ui_lines_add(ui, text, SYSTEM_COLOUR, ui->now + (ms != 0 ? ms : MESSAGE_MS), 0);
	(void)kp_ui_play_sound(ui, TIMED_MESSAGE_SOUND);
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

void kp_ui_format_name_xy(char *out, size_t size, const uint8_t *format, const uint8_t *name, int64_t x, int64_t y)
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

/* CopyPrintable 0x4a8320: the bytes from 0x20 (CopyPrintableKeepWhitespace 0x4a8350 keeps 9, 10 and 13 too). */
static void copy_printable(char *out, size_t size, const uint8_t *in, int whitespace)
{
	size_t at = 0;
	for (; in != NULL && *in != 0 && at + 1 < size; in++) {
		if (*in >= 0x20 || (whitespace && (*in == 9 || *in == 10 || *in == 13))) out[at++] = (char)*in;
	}
	out[at] = 0;
}

/* A chat line's colour by its sender's minimap colour (DisplayChatMessage 0x46a860's tables 0x46aa54, 0x46aa94). */
static uint8_t chat_colour(uint8_t minimap)
{
	static const uint8_t FROM[] = { 0x13, 0x33, 0x4d, 0x54, 0x6f, 0x80, 0x86, 0x87, 0x88, 0x9a, 0x9c, 0x9f, 0xa4, 0xa5,
		0xb9 };
	static const uint8_t TO[] = { 21, 28, 29, 22, 8, 31, 27, 23, 25, 30, 17, 15, 16, 14, 24 };
	size_t k;
	for (k = 0; k < sizeof FROM; k++) {
		if (FROM[k] == minimap) return TO[k];
	}
	return CHAT_COLOUR;
}

/* DisplayChatMessage 0x46a860 (KP_EVENT_CHAT; the sound is the game's): a sender past the players says the text alone;
 * in a replay the live turns' line is "name: text"; else "name:<c> text" with c the local player's colour code 7 or
 * another's 2, the line in c or, with usechatcolors, in the sender's minimap colour; an observer's name is tagged, else
 * a team game's with the sender's force name (IsTeamGame 0x4b2d90, GetForceName 0x4b2bf0). */
static void chat_message(KpUi *ui, const char *text, int64_t sender, int from_replay)
{
	KpGame *g = ui->game;
	char line[FORMAT_SIZE], name[FORMAT_SIZE];
	uint8_t colour = CHAT_COLOUR, c;
	if (text == NULL) return;
	if (g == NULL || sender < 0 || sender >= 8) {
		snprintf(line, sizeof line, "%s", text);
	} else if (g->w->replay && !from_replay) {
		copy_printable(name, sizeof name, kp_ui_record_name(ui, sender), 0);
		snprintf(line, sizeof line, "%s: %s", name, text);
	} else {
		c = sender == g->local_player ? CHAT_LOCAL_COLOUR : CHAT_COLOUR;
		colour = (ui->options & KP_UI_OPTION_CHAT_COLOURS) != 0
			? chat_colour(ui->minimap.player_colours[kp_ui_player_row(ui, sender) & 0xf]) : c;
		copy_printable(name, sizeof name, kp_ui_record_name(ui, sender), 1);
		if (g->setup.type[sender] == KP_CTRL_OBSERVER || kp_setup_team_game(&g->setup)) {
			const uint8_t *tag = kp_ui_tbl(ui, KP_UI_NETWORK, g->setup.type[sender] == KP_CTRL_OBSERVER ? CHAT_OBSERVER_STRING
				: (uint32_t)(FORCE_NAME_STRING + (g->players.force[sender] & 0xff)));
			snprintf(line, sizeof line, "[%s]%s:%c %s", tag != NULL ? (const char *)tag : "", name, (char)c, text);
		} else {
			snprintf(line, sizeof line, "%s:%c %s", name, (char)c, text);
		}
	}
	kp_ui_lines_add(ui, (const uint8_t *)line, colour, ui->now + MESSAGE_MS, 0);
}

void kp_ui_game_event(KpUi *ui, int64_t kind, int64_t a, int64_t b, int64_t c, const char *text)
{
	if (!kp_ui_sound_played(ui, kind, a, b, c)) kp_ui_sound_event(ui, kind, a, b, c);
	switch (kind) {
	case KP_EVENT_ERROR: kp_ui_error_line(ui, kp_ui_tbl(ui, c ? KP_UI_NETWORK : KP_UI_STAT_TXT, (uint32_t)b)); break;
	case KP_EVENT_NETWORK_TEXT: kp_ui_error_line(ui, kp_ui_tbl(ui, KP_UI_NETWORK, (uint32_t)a)); break;
	case KP_EVENT_GAME_EXIT: ui->game_exit = (uint16_t)a; break;
	case KP_EVENT_PLAYER_MESSAGE: { /* the string with the player's name and the number (0x4cb860) */
		char line[FORMAT_SIZE];
		const uint8_t *format = kp_ui_tbl(ui, KP_UI_NETWORK, (uint32_t)a);
		if (format == NULL) break;
		kp_ui_format_name_xy(line, sizeof line, format, kp_ui_record_name(ui, b), c, 0);
		kp_ui_timed_message(ui, (const uint8_t *)line, 0);
		break;
	}
	case KP_EVENT_FORCE_MESSAGE: { /* the string with the force's name */
		char line[FORMAT_SIZE];
		const uint8_t *format = kp_ui_tbl(ui, KP_UI_NETWORK, (uint32_t)a);
		const uint8_t *name = kp_ui_tbl(ui, KP_UI_NETWORK, (uint32_t)(FORCE_NAME_STRING + (b & 0xff)));
		if (format == NULL) break;
		kp_ui_format_name_xy(line, sizeof line, format, name != NULL ? name : (const uint8_t *)"", 0, 0);
		kp_ui_timed_message(ui, (const uint8_t *)line, 0);
		break;
	}
	case KP_EVENT_CHAT: chat_message(ui, text, a, b != 0); break;
	case KP_EVENT_SAVE_GAME: kp_ui_saving(ui, text != NULL ? text : ""); break;
	case KP_EVENT_CURSOR_MODES: kp_ui_cursor_modes_ended(ui, a != 0); break;
	case KP_EVENT_UNIT_UNPLACED: {
		char line[FORMAT_SIZE];
		kp_ui_format_name_xy(line, sizeof line, kp_ui_tbl(ui, KP_UI_NETWORK, UNPLACED_STRING), kp_ui_unit_type_name(ui, a), b, c);
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
		if (a != 0) kp_ui_game_paused(ui);
		else kp_ui_warp_cursor(ui);
		break;
	case KP_EVENT_CAMERA: /* TrigActCenterView 0x4b6350 */
		if (c == KP_CAMERA_PAN) {
			kp_ui_start_pan(ui, (int32_t)a, (int32_t)b);
		} else if (c == KP_CAMERA_SET) {
			int32_t x = (int32_t)a, y = (int32_t)b;
			kp_ui_view_target(ui, &x, &y);
			kp_ui_set_screen_position(ui, x, y);
		} else {
			kp_ui_end_pan(ui);
		}
		break;
	case KP_EVENT_GAME_RESULT:
		/* the game stood from this step: OpenModelessDialog's UnpauseGame finds no paused time to discount */
		kp_ui_game_paused(ui);
		(void)kp_ui_show_result(ui, (int)a);
		break;
	default: break;
	}
	(void)text;
}
