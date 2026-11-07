/* The list boxes (lbox.cpp, atlas P43 0x4c5ff0..0x4c73c0): the stock type-12 procedure and draw, the items' three
 * arrays, the selection (user code 11), the hover row the mouse tracks with timer 0x2b, the keys, the page and the
 * vertical scroll bar InitListBox 0x4c68c0 creates for a tall list. */
#include "kp_text.h"
#include "kp_ui.h"

#include <stdlib.h>
#include <string.h>

#define TRACK_TIMER 0x2b
#define TRACK_PERIOD 1
#define FLAG_OWNED 0x10             /* an item's text duplicated */
#define FLAG_TALL 0x20000000u       /* rows of the theme's frame 53 */
#define FLAG_NO_HOVER 0x10000000u
#define FLAG_TRACKING 0x40000000u
#define FLAG_SUSPENDED 0x80000000u  /* SetListRedraw(0) */
#define FLAG_DEFAULT 0x80000u
#define FRAME_TALL 53
#define FRAME_TALL_HOVER 56
#define FRAME_ROW 77
#define FRAME_ROW_HOVER 80
#define COLOUR_ITEM 3
#define COLOUR_HOVER 4
#define COLOUR_FOCUS 16             /* 0x6d2378 */
#define COLOUR_FRAME 15             /* 0x6d2377 */
#define COLOUR_OUTER 12             /* 0x6d2374 */
#define FORMAT_ITEM 0x11
#define TEXT_SIZE 256
#define BAR_TYPE 7
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_UP 0x26
#define VK_DOWN 0x28

static KpUiList *ls(const KpUiControl *c) { return c->list; }
static int32_t self_of(KpUi *ui, const KpUiControl *c) { return (int32_t)(c - ui->controls); }

/* The cursor relative to the list's dialog (0x4c29d0). */
static void rel(const KpUi *ui, const KpUiControl *c, const KpUiEvent *e, int32_t *x, int32_t *y)
{
	const KpUiControl *d = &ui->controls[c->parent];
	*x = e->x - d->left;
	*y = e->y - d->top;
}

static void update_bar(KpUi *ui, KpUiControl *c);

/* RecalcListLayout 0x4c6db0 (a list box: no combo extension). */
static void recalc(KpUi *ui, KpUiControl *c)
{
	KpUiList *l = ls(c);
	int32_t page;
	if (l == NULL || (c->flags & FLAG_SUSPENDED)) return;
	page = l->row_h > 0 ? (c->bottom - c->top + 1) / l->row_h : 0;
	l->page = (uint8_t)page;
	l->last_top = 0;
	c->bottom = (int16_t)(c->top + (l->row_h > page * l->row_h ? l->row_h : page * l->row_h));
	if (l->count == 0) l->selected = KP_UI_LIST_NONE;
	else if (l->selected >= l->count && l->selected != KP_UI_LIST_NONE) l->selected = (uint8_t)(l->count - 1);
	l->hover = l->selected;
	l->last_top = (uint8_t)(l->count > l->page ? l->count - l->page : 0);
	if (l->top > l->last_top) l->top = l->last_top;
	c->flags |= FLAG_SUSPENDED;
	update_bar(ui, c);
	c->flags &= ~FLAG_SUSPENDED;
	kp_ui_mark_dirty(ui, self_of(ui, c));
}

/* UpdateListScrollBar 0x4c7320: the bar shows while the rows do not fit (the list narrows for it), its steps are the
 * page's, its range the tops, its position the list's top. */
static void update_bar(KpUi *ui, KpUiControl *c)
{
	KpUiList *l = ls(c);
	KpUiControl *b;
	if (l->bar < 0) return;
	b = &ui->controls[l->bar];
	if (l->count > l->page) {
		if (!(b->flags & KP_UI_VISIBLE) && (c->flags & KP_UI_VISIBLE)) {
			b->flags |= KP_UI_VISIBLE;
			kp_ui_mark_dirty(ui, l->bar);
			c->right = (int16_t)(c->right + b->left - b->right - 3);
		}
		if ((b->flags & KP_UI_VISIBLE) && !(c->flags & KP_UI_VISIBLE)) {
			b->flags &= ~KP_UI_VISIBLE;
			c->right = (int16_t)(c->right + b->right - b->left + 3);
		}
	} else if (b->flags & KP_UI_VISIBLE) {
		b->flags = (b->flags | KP_UI_DISABLED) & ~KP_UI_VISIBLE;
		c->right = (int16_t)(c->right + b->right - b->left + 3);
	}
	kp_ui_scrollbar_set_steps(ui, l->bar, l->page, 1);
	kp_ui_scrollbar_set_range(ui, l->bar, 0, l->last_top);
	if (l->selected != KP_UI_LIST_NONE) (void)kp_ui_send_user(ui, l->bar, KP_UI_VALUE_CHANGED, l->top);
}

/* The bar's change callback 0x4c7300: the list's top follows unless the list tracks the mouse itself. */
static void bar_changed(KpUi *ui, uint32_t list, int32_t value)
{
	KpUiControl *c = &ui->controls[(int32_t)list];
	(void)value;
	if (c->flags & FLAG_TRACKING) return;
	c->list->top = (uint8_t)kp_ui_scrollbar_pos(&ui->controls[c->list->bar]);
	kp_ui_mark_dirty(ui, (int32_t)list);
}

/* ScrollListToItem 0x4c7280. */
static void scroll_to(KpUi *ui, KpUiControl *c, int index)
{
	KpUiList *l = ls(c);
	(void)ui;
	if (index < l->top) l->top = (uint8_t)index;
	else if (index >= l->top + l->page) l->top = (uint8_t)(index - l->page + 1);
}

static int send_value(KpUi *ui, KpUiControl *c, uint32_t value)
{
	return kp_ui_send_user(ui, self_of(ui, c), KP_UI_VALUE_CHANGED, value);
}

/* ListRowAtCursor 0x4c62d0. */
static int row_at(KpUi *ui, KpUiControl *c, const KpUiEvent *e)
{
	KpUiList *l = ls(c);
	int32_t x, y, r;
	rel(ui, c, e, &x, &y);
	if (y < c->top) {
		c->flags &= ~FLAG_TRACKING;
		r = l->hover > l->top ? l->top : (l->hover ? l->hover - 1 : 0);
	} else if (y > c->bottom) {
		c->flags &= ~FLAG_TRACKING;
		if (l->hover < l->top + l->page - 1) r = l->top + l->page - 1;
		else r = l->hover < l->count - 1 ? l->hover + 1 : l->hover;
	} else {
		if (x > c->left && x < c->right) c->flags |= FLAG_TRACKING;
		r = l->top + (int16_t)((y - c->top) / l->row_h);
	}
	if (r >= l->count) r = l->count - 1;
	return r & 0xff;
}

/* IsCursorInListRows 0x4c73c0. */
static int in_rows(KpUi *ui, KpUiControl *c, const KpUiEvent *e)
{
	int32_t x, y;
	rel(ui, c, e, &x, &y);
	return x >= c->left && x <= c->right && y >= c->top && y <= c->bottom;
}

static void hover_to(KpUi *ui, KpUiControl *c, int r)
{
	KpUiList *l = ls(c);
	if (r == l->hover) return;
	l->hover = (uint8_t)r;
	kp_ui_mark_dirty(ui, self_of(ui, c));
	scroll_to(ui, c, r);
}

static int key_down(KpUi *ui, KpUiControl *c, const KpUiEvent *e)
{
	/* ListBoxKeyDown 0x4c6bb0 */
	KpUiList *l = ls(c);
	int s = l->selected, n = (int)l->count - 1, v = -1;
	switch (e->key & 0xff) {
	case VK_PRIOR: if (s != 0 && s <= n) v = s <= l->page ? 0 : s - l->page + 1; break;
	case VK_NEXT: if (s < n) v = s + l->page < n ? s + l->page - 1 : n; break;
	case VK_END: if (s != n) v = n; break;
	case VK_HOME: if (s != 0) v = 0; break;
	case VK_UP: if (s != 0 && s <= n) v = s - 1; break;
	case VK_DOWN: if (s < n) v = s + 1; break;
	default: return 0;
	}
	if (v >= 0) {
		(void)send_value(ui, c, (uint32_t)v);
		scroll_to(ui, c, l->selected);
	}
	return 1;
}

static void init_list(KpUi *ui, KpUiControl *c)
{
	/* InitListBox 0x4c68c0 */
	KpUiList *l = (KpUiList *)calloc(1, sizeof *l);
	int32_t page, font_h;
	if (l == NULL) return;
	kp_ui_list_free(c);
	c->list = l;
	l->capacity = KP_UI_LIST_CAPACITY;
	l->bar = -1;
	kp_ui_select_control_font(ui, c);
	font_h = ui->text.font != NULL ? ui->text.max_height : 0; /* 0x6d3037 */
	if (font_h < ui->theme.row_h) {
		c->flags |= FLAG_TALL;
		l->row_h = (int16_t)ui->theme.row_h;
	} else {
		c->flags &= ~FLAG_TALL;
		l->row_h = (int16_t)font_h;
	}
	ui->text.font = NULL;
	if (l->row_h <= 0) l->row_h = 1;
	page = (c->bottom - c->top + 1) / l->row_h;
	l->page = (uint8_t)page;
	l->last_top = 0;
	c->bottom = (int16_t)(c->top + (l->row_h > page * l->row_h ? l->row_h : page * l->row_h));
	if (c->bottom - c->top > ui->theme.track_h + 2 * ui->theme.up_h) {
		l->bar = kp_ui_create_scroll_bar(ui, self_of(ui, c));
		if (l->bar >= 0) kp_ui_scrollbar_set_callback(ui, l->bar, bar_changed);
	}
}

void kp_ui_list_free(KpUiControl *c)
{
	KpUiList *l = c->list;
	int k;
	if (l == NULL) return;
	for (k = 0; k < KP_UI_LIST_CAPACITY; k++) free(l->owned[k]);
	free(l);
	c->list = NULL;
}

int kp_ui_list_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* ListBoxProc 0x4c6480 */
	KpUiList *l = ls(c);
	int32_t self = self_of(ui, c);
	switch (e->type) {
	case KP_UI_KEY_DOWN: case KP_UI_KEY_REPEAT:
		return l != NULL ? key_down(ui, c, e) : 0;
	case KP_UI_MOUSE_MOVE: /* ListBoxMouseMove 0x4c6d40 */
		if (l == NULL) return 1;
		if (in_rows(ui, c, e) && !(c->flags & FLAG_TRACKING)) {
			c->flags |= FLAG_TRACKING;
			(void)kp_ui_set_timer(&ui->timers, self, TRACK_TIMER, TRACK_PERIOD, 0, ui->now);
			hover_to(ui, c, row_at(ui, c, e));
		} else {
			c->flags &= ~FLAG_TRACKING;
		}
		return 1;
	case KP_UI_LEFT_DOWN: /* ListBoxMouseDown 0x4c6ce0 */
		kp_ui_set_capture(ui, self, KP_UI_LEFT_UP);
		kp_ui_set_capture(ui, self, KP_UI_MOUSE_MOVE);
		kp_ui_set_capture(ui, self, KP_UI_IDLE);
		if (l != NULL && in_rows(ui, c, e)) {
			l->hover = (uint8_t)row_at(ui, c, e);
			kp_ui_mark_dirty(ui, self);
			scroll_to(ui, c, l->hover);
		}
		return 1;
	case KP_UI_LEFT_UP:
		if (l != NULL && l->hover != l->selected && l->hover < l->count) (void)send_value(ui, c, l->hover);
		kp_ui_release_refs(ui, self);
		kp_ui_delete_timer(&ui->timers, self, TRACK_TIMER);
		return 1;
	case KP_UI_LEFT_DOUBLE: { /* the dialog's default button (0x4c2430) */
		int32_t k;
		for (k = ui->controls[c->parent].child; k >= 0; k = ui->controls[k].next) {
			if (ui->controls[k].flags & FLAG_DEFAULT) {
				(void)kp_ui_send_user(ui, k, KP_UI_ACTIVATE, 0);
				break;
			}
		}
		return 1;
	}
	case KP_UI_USER:
		break;
	default:
		return 0;
	}
	switch (e->code) {
	case KP_UI_DESTROY:
		c->flags |= FLAG_SUSPENDED;
		kp_ui_list_free(c);
		break;
	case KP_UI_TIMER:
		if (e->param == TRACK_TIMER && l != NULL) hover_to(ui, c, row_at(ui, c, e)); /* ListBoxTrackTick 0x4c7040 */
		break;
	case KP_UI_POST_LOAD:
		init_list(ui, c);
		break;
	case KP_UI_VALUE_CHANGED:
		if (l != NULL && (l->hover == l->selected || l->hover == (uint8_t)e->param)) {
			l->selected = l->hover = (uint8_t)e->param;
			kp_ui_mark_dirty(ui, self);
		}
		return 1;
	case KP_UI_SHOW:
		c->flags |= KP_UI_VISIBLE;
		if (l != NULL) update_bar(ui, c);
		return 1;
	case KP_UI_HIDE:
		c->flags &= ~(KP_UI_VISIBLE | KP_UI_HOVER);
		if (l != NULL) update_bar(ui, c);
		return 1;
	default:
		break;
	}
	return kp_ui_type_interact(-1)(ui, c, e); /* ControlDefaultInteract 0x4c1e90 */
}

/* DrawListItemDefault 0x4c5ff0. */
static void draw_item(KpUi *ui, KpUiControl *c, int index, const int16_t row[4], int32_t dx, int32_t dy)
{
	KpUiList *l = ls(c);
	int tall = (c->flags & FLAG_TALL) != 0, hover = index == l->hover && !(c->flags & FLAG_NO_HOVER);
	int frame = tall ? (hover ? FRAME_TALL_HOVER : FRAME_TALL) : (hover ? FRAME_ROW_HOVER : FRAME_ROW);
	int32_t tile = tall ? ui->theme.row_w : ui->theme.wide_row_w;
	if (l->count == 0) return;
	if (!(c->flags & KP_UI_PLAIN)) {
		kp_ui_three_part_bar(ui, row[0], row[1], row[2] - row[0] + 1, tile, frame, row, KP_UI_TRANSPARENT);
	}
	if (index < l->count && l->text[index] != NULL) {
		uint8_t text[TEXT_SIZE + 4];
		int32_t avail;
		size_t n = strlen((const char *)l->text[index]);
		kp_ui_select_color(&ui->text, hover ? COLOUR_HOVER : ((l->flags[index] & 0xf) ? (l->flags[index] & 0xf) : COLOUR_ITEM));
		ui->text.flags = FORMAT_ITEM;
		ui->text.clip_left = (int16_t)(row[0] + dx);
		ui->text.clip_top = (int16_t)(row[1] + dy);
		ui->text.clip_right = row[2];
		ui->text.clip_bottom = row[3];
		n = kp_text_copy((char *)text, TEXT_SIZE, (const char *)l->text[index]);
		avail = ui->text.clip_right - ui->text.clip_left;
		if (kp_ui_text_width(&ui->text, text) > avail) { /* trimmed with "..." */
			avail -= kp_ui_text_width(&ui->text, (const uint8_t *)"...");
			while (n > 0 && kp_ui_text_width(&ui->text, text) > avail) {
				n = kp_text_prev(text, n);
				text[n] = 0;
			}
			memcpy(text + n, "...", 4);
		}
		(void)kp_ui_draw_text(&ui->text, row[0] + dx, row[1] + dy, text);
	}
}

void kp_ui_list_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawListBox 0x4c6740 */
	KpUiList *l = ls(c);
	int16_t row[4];
	int k, n;
	uint8_t spacing = ui->text.spacing;
	(void)x;
	(void)y;
	(void)clip;
	if (l == NULL) return;
	kp_ui_select_control_font(ui, c);
	if (c->flags & FLAG_SUSPENDED) {
		c->flags &= ~FLAG_SUSPENDED;
		recalc(ui, c);
		c->flags |= FLAG_SUSPENDED;
	}
	kp_ui_update_focus(ui, c);
	if (!(c->flags & KP_UI_PLAIN)) { /* DrawInsetFrame 0x4c4250 */
		uint8_t inner = ui->ui_colours[(c->flags & KP_UI_FOCUS_LINE) ? COLOUR_FOCUS : COLOUR_FRAME];
		int32_t w = c->right - c->left + 1, h = c->bottom - c->top + 1;
		kp_ui_fill_rect(ui, c->left + 1, c->top + 1, w - 2, 1, inner);
		kp_ui_fill_rect(ui, c->left + 1, c->bottom - 1, w - 2, 1, inner);
		kp_ui_fill_rect(ui, c->left + 1, c->top + 1, 1, h - 2, inner);
		kp_ui_fill_rect(ui, c->right - 1, c->top + 1, 1, h - 2, inner);
		kp_ui_fill_rect(ui, c->left, c->top, w, 1, ui->ui_colours[COLOUR_OUTER]);
		kp_ui_fill_rect(ui, c->left, c->bottom, w, 1, ui->ui_colours[COLOUR_OUTER]);
		kp_ui_fill_rect(ui, c->left, c->top, 1, h, ui->ui_colours[COLOUR_OUTER]);
		kp_ui_fill_rect(ui, c->right, c->top, 1, h, ui->ui_colours[COLOUR_OUTER]);
	}
	ui->text.spacing = 1;
	n = l->page < l->count ? l->page : l->count;
	row[0] = c->left;
	row[1] = c->top;
	row[2] = c->right;
	row[3] = (int16_t)(c->top + l->row_h);
	for (k = 0; k < n; k++) {
		(l->draw_item != NULL ? l->draw_item : draw_item)(ui, c, l->top + k, row, 2, 0);
		row[1] = (int16_t)(row[1] + l->row_h);
		row[3] = (int16_t)(row[3] + l->row_h);
	}
	ui->text.spacing = spacing;
	ui->text.font = NULL;
}

/* --- the items --- */

static KpUiControl *list_of(KpUi *ui, int32_t list) { return list >= 0 && ui->controls[list].list ? &ui->controls[list] : NULL; }

static const uint8_t *keep(KpUiList *l, int at, const uint8_t *text, uint8_t flags)
{
	l->owned[at] = NULL;
	if (flags & FLAG_OWNED) {
		size_t n = strlen((const char *)text) + 1;
		l->owned[at] = (uint8_t *)malloc(n);
		if (l->owned[at] != NULL) memcpy(l->owned[at], text, n);
		return l->owned[at];
	}
	return text;
}

int kp_ui_list_add(KpUi *ui, int32_t list, const uint8_t *text, uint8_t flags)
{
	/* AddListItem 0x4c61d0 */
	KpUiControl *c = list_of(ui, list);
	KpUiList *l;
	int at;
	if (c == NULL) return KP_UI_LIST_NONE;
	l = c->list;
	if (l->count >= l->capacity) return KP_UI_LIST_NONE;
	at = l->count;
	l->text[at] = keep(l, at, text, flags);
	l->flags[at] = flags;
	l->count++;
	recalc(ui, c);
	return at;
}

void kp_ui_list_insert(KpUi *ui, int32_t list, int index, const uint8_t *text, uint8_t flags)
{
	/* InsertListItem 0x4c6a70 */
	KpUiControl *c = list_of(ui, list);
	KpUiList *l;
	int at;
	if (c == NULL || c->list->count >= c->list->capacity) return;
	l = c->list;
	at = l->count == 0 ? 0 : index < l->count ? index : l->count;
	if (at < l->count) {
		int n = l->count - at;
		memmove(&l->text[at + 1], &l->text[at], sizeof l->text[0] * (size_t)n);
		memmove(&l->owned[at + 1], &l->owned[at], sizeof l->owned[0] * (size_t)n);
		memmove(&l->flags[at + 1], &l->flags[at], (size_t)n);
		memmove(&l->data[at + 1], &l->data[at], sizeof l->data[0] * (size_t)n);
	}
	l->text[at] = keep(l, at, text, flags);
	l->flags[at] = flags;
	l->data[at] = 0;
	l->count++;
	recalc(ui, c);
}

void kp_ui_list_delete(KpUi *ui, int32_t list, int index)
{
	/* DeleteListItem 0x4c6f60 */
	KpUiControl *c = list_of(ui, list);
	KpUiList *l;
	int n;
	if (c == NULL || index < 0 || index >= c->list->count) return;
	l = c->list;
	(void)kp_ui_send_user(ui, list, KP_UI_CODE_12, (uint32_t)index);
	l->data[index] = 0;
	free(l->owned[index]);
	n = l->count - index - 1;
	if (n > 0) {
		memmove(&l->text[index], &l->text[index + 1], sizeof l->text[0] * (size_t)n);
		memmove(&l->owned[index], &l->owned[index + 1], sizeof l->owned[0] * (size_t)n);
		memmove(&l->flags[index], &l->flags[index + 1], (size_t)n);
		memmove(&l->data[index], &l->data[index + 1], sizeof l->data[0] * (size_t)n);
	}
	l->count--;
	l->owned[l->count] = NULL;
	recalc(ui, c);
}

void kp_ui_list_clear(KpUi *ui, int32_t list)
{
	/* ClearList 0x4c6ef0 */
	KpUiControl *c = list_of(ui, list);
	uint32_t was;
	if (c == NULL) return;
	was = c->flags & FLAG_SUSPENDED;
	c->flags |= FLAG_SUSPENDED;
	while (c->list != NULL && c->list->count != 0) kp_ui_list_delete(ui, list, c->list->count - 1);
	if (!was && (c->flags & FLAG_SUSPENDED)) {
		c->flags &= ~FLAG_SUSPENDED;
		recalc(ui, c);
	}
}

void kp_ui_list_select(KpUi *ui, int32_t list, int index)
{
	/* SelectListItem 0x4c7250 */
	KpUiControl *c = list_of(ui, list);
	if (c == NULL || !(index < c->list->count || index == KP_UI_LIST_NONE)) return;
	(void)send_value(ui, c, (uint32_t)index);
	scroll_to(ui, c, index);
}

void kp_ui_list_redraw(KpUi *ui, int32_t list, int on)
{
	/* SetListRedraw 0x4c7230 */
	KpUiControl *c = list_of(ui, list);
	if (c == NULL) return;
	if (!on) {
		c->flags |= FLAG_SUSPENDED;
	} else if (c->flags & FLAG_SUSPENDED) {
		c->flags &= ~FLAG_SUSPENDED;
		recalc(ui, c);
	}
}

int kp_ui_list_selection(const KpUi *ui, int32_t list)
{
	const KpUiList *l = list >= 0 ? ui->controls[list].list : NULL;
	return l == NULL || l->count == 0 ? KP_UI_LIST_NONE : l->selected; /* GetListSelection 0x4c6890 */
}

int kp_ui_list_count(const KpUi *ui, int32_t list)
{
	const KpUiList *l = list >= 0 ? ui->controls[list].list : NULL;
	return l == NULL ? 0 : l->count;
}

const uint8_t *kp_ui_list_text(const KpUi *ui, int32_t list, int index)
{
	const KpUiList *l = list >= 0 ? ui->controls[list].list : NULL;
	return l != NULL && index >= 0 && index < l->count && l->text[index] != NULL ? l->text[index] : (const uint8_t *)"";
}

void kp_ui_list_set_data(KpUi *ui, int32_t list, int index, uint32_t data)
{
	/* SetListItemData 0x4c7070 */
	KpUiList *l = list >= 0 ? ui->controls[list].list : NULL;
	if (l != NULL && index >= 0 && index < KP_UI_LIST_CAPACITY) l->data[index] = data;
}

uint32_t kp_ui_list_data(const KpUi *ui, int32_t list, int index)
{
	/* GetListItemData 0x4c6840 */
	const KpUiList *l = list >= 0 ? ui->controls[list].list : NULL;
	return l != NULL && index >= 0 && index < KP_UI_LIST_CAPACITY ? l->data[index] : 0;
}
