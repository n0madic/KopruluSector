/* The command card (plan phase 9C; statcmd.cpp, atlas P35 0x4a0620..0x4a1950, E01 0x4a15a0): the dialog
 * rez\statbtn<race>.bin, its nine buttons filled from the status unit's button set (or the forced one: targeting,
 * placement, the build menus; or the selection's merged one), the button the current order highlights, the
 * buttons' drawing and their activation by click and hotkey. The buttons' conditions are kp_ui_conditions.c's, their
 * actions kp_ui_actions.c's. */
#include "kp_ui.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "kp_game.h"
#include "kp_ids.h"

#define BUTTON_RECORD 20
#define BUTTONS_FIRST 1
#define BUTTONS_LAST 9
#define PROGRESS_BAR 10
#define CARD_CHILDREN 10
#define ICON_NONE 0xffff
#define ORDER_NONE 0xff
#define HIGHLIGHT_CANCEL 229    /* an order whose highlight is the Cancel icon takes its subunit's */
#define SET_TARGETING 229
#define SET_PLACEMENT 230
#define SET_MIXED 244
#define SET_WORKERS 245
#define SET_CLOAKERS 246
#define SET_ZERG_GROUND 247
#define SLICE_DISABLED 1
#define SLICE_PRESSED 2
#define REMAP_BYTES 16
#define DAT_WORKER 0x08
#define DAT_CLOAKABLE 0x200
#define DAT_BURROWABLE 0x100000
#define STATUS_IN_AIR 0x04
#define CHAR_NO_HOTKEY 0x100
#define RACES 3
#define ERR_TECH_REQUIRED 21    /* the requirement error whose button says 0x2fa */
#define STRING_TECH_REQUIRED 0x2fa
#define STRING_NEXT_LEVEL 0x31b /* "Next Level:" */
#define TOOLTIP_TEXT 128
#define TIP_UNIT 1              /* the enabled string's second character: what the button costs */
#define TIP_UPGRADE 2
#define TIP_SPELL 3
#define TIP_RESEARCH 4
#define TIP_UNIT_NO_SUPPLY 5
#define DAT_TWO_IN_ONE 0x400    /* units.dat: a pair from one egg (its supply doubled) */
#define GROUP_ZERG 1
#define GROUP_TERRAN 2
#define GROUP_PROTOSS 4
#define COST_SLOTS 8
#define COST_GAS 1              /* the cost line's gas icon is the local race's */
#define COST_X 2
#define COST_TEXT_DX 2

static const char RACE_LETTERS[RACES] = { 'z', 't', 'p' };

static KpGame *game(KpUi *ui) { return ui->game; }
static KpUiCard *cc(KpUi *ui) { return &ui->card; }

/* A button set: its linked set, its button count, its first record; NULL past the sets. */
static const uint8_t *set_of(const KpUi *ui, uint16_t set, uint16_t *linked, int *count)
{
	const uint8_t *p;
	if (ui->button_sets == NULL || set >= ui->button_set_count) return NULL;
	p = ui->button_sets + ui->button_set_at[set];
	if (linked) *linked = (uint16_t)kp_le_u16(p);
	if (count) *count = (int)kp_le_u16(p + 2);
	return p + 4;
}

static const uint8_t *record_of(const KpUi *ui, const KpUiControl *c)
{
	return c->user ? ui->button_sets + c->user - 1 : NULL;
}

/* --- drawing --- */

/* DrawCommandButton 0x4a0c00. */
static void draw_button(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	KpUiCard *k = cc(ui);
	int pressed = (c->flags & KP_UI_PRESSED) ? 1 : 0, slice, i;
	int32_t w = c->right - c->left + 1, h = c->bottom - c->top + 1, fw, fh, crop[4];
	(void)x;
	(void)y;
	(void)clip;
	if (k->buttons && kp_ui_grp_frame_size(k->buttons->data, k->buttons->size, pressed, &fw, &fh) == 0) {
		crop[0] = 0;
		crop[1] = 0;
		crop[2] = fw < w ? fw : w;
		crop[3] = fh < h ? fh : h;
		kp_ui_draw_grp_rle(ui, c->left, c->top, k->buttons->data, k->buttons->size, pressed, crop);
	}
	slice = (c->flags & KP_UI_DISABLED) ? SLICE_DISABLED : (pressed || c->wuser == k->highlight) ? SLICE_PRESSED : 0;
	memcpy(ui->remap, k->slices + REMAP_BYTES * slice, REMAP_BYTES);
	if (k->icons && kp_ui_grp_frame_size(k->icons->data, k->icons->size, c->wuser, &fw, &fh) == 0) {
		crop[0] = 0;
		crop[1] = 0;
		crop[2] = fw;
		crop[3] = fh;
		/* TODO: a replay's icons 0x182..0x185 and their offsets (0x4a0da8) */
		kp_ui_draw_grp_rle(ui, c->left + (w - fw) / 2 + pressed, c->top + (h - fh) / 2 + pressed, k->icons->data,
			k->icons->size, c->wuser, crop);
	}
	for (i = 0; i < REMAP_BYTES; i++) ui->remap[i] = (uint8_t)i;
}

/* --- the button set on the card --- */

/* The record's disabled string overwritten with 0x2fa (once and for all, as in the original's static table). */
static void rewrite(KpUi *ui, const uint8_t *rec)
{
	uint32_t at = (uint32_t)(rec - ui->button_sets);
	int i;
	for (i = 0; i < ui->rewritten_count; i++) {
		if (ui->rewritten[i] == at) return;
	}
	if (ui->rewritten_count < KP_UI_REWRITTEN) ui->rewritten[ui->rewritten_count++] = at;
}

static uint32_t disabled_string(const KpUi *ui, const uint8_t *rec)
{
	uint32_t at = (uint32_t)(rec - ui->button_sets);
	int i;
	for (i = 0; i < ui->rewritten_count; i++) {
		if (ui->rewritten[i] == at) return STRING_TECH_REQUIRED;
	}
	return kp_le_u16(rec + 18);
}

/* PopulateCommandCard 0x4a1080. */
static void populate(KpUi *ui)
{
	KpUiCard *k = cc(ui);
	KpGame *g = game(ui);
	int64_t status = ui->hud.status_unit;
	int count = 0, i = 0, r = 0;
	const uint8_t *rec = set_of(ui, k->set, NULL, &count);
	int32_t c;
	for (c = kp_ui_find_control(ui, k->dialog, BUTTONS_FIRST); c >= 0 && ui->controls[c].id >= BUTTONS_FIRST;
		c = ui->controls[c].next) {
		KpUiControl *b = &ui->controls[c];
		if (b->id > BUTTONS_LAST) continue;
		while (rec && i < count) {
			r = kp_ui_button_condition(ui, kp_le_u32(rec + 4), rec[12], g->local_player, status);
			if (r != 0) break;
			i++;
			rec += BUTTON_RECORD;
		}
		if ((g->w->replay || status == 0 || g->u.owner[status] == g->local_player) && rec && i < count
			&& b->id >= (int16_t)kp_le_u16(rec)) {
			uint16_t icon = (uint16_t)kp_le_u16(rec + 2);
			b->user = (uint32_t)(rec - ui->button_sets) + 1;
			if (b->wuser != icon) {
				b->wuser = icon;
				kp_ui_mark_dirty(ui, c);
				(void)kp_ui_send_user(ui, c, KP_UI_FOCUS_LOST, 0);
			}
			kp_ui_show_control(ui, c);
			if (r < 0) {
				if (!(b->flags & KP_UI_DISABLED)) kp_ui_mark_dirty(ui, c);
				b->flags |= KP_UI_DISABLED;
				if (kp_scalar(g, KP_REQUIREMENT_ERROR) == ERR_TECH_REQUIRED) rewrite(ui, rec);
			} else {
				if (b->flags & KP_UI_DISABLED) kp_ui_mark_dirty(ui, c);
				b->flags &= ~KP_UI_DISABLED;
			}
			i++;
			rec += BUTTON_RECORD;
		} else {
			kp_ui_hide_control(ui, c);
			b->user = 0;
			b->wuser = ICON_NONE;
		}
	}
}

/* MergeSelectionButtonSets 0x4a15a0. */
static void merge_sets(KpUi *ui)
{
	KpUiCard *k = cc(ui);
	KpGame *g = game(ui);
	uint16_t base = (uint16_t)g->u.button_set[ui->hud.status_unit], linked_base, linked_s;
	int workers = 0, cloakers = 0, zerg_ground = 0, burrowable = 0, i;
	for (i = 0; i < KP_UI_SLOTS; i++) {
		int64_t n = ui->hud.selection[i];
		uint16_t s;
		uint32_t f;
		int cb = 0, cs = 0;
		const uint8_t *a, *b;
		if (n == 0) continue;
		s = (uint16_t)g->u.button_set[n];
		f = (uint32_t)KP_DAT(g, units, special_ability_flags, g->u.type[n]);
		if (f & DAT_WORKER) workers++;
		if (f & DAT_CLOAKABLE) cloakers++;
		if (!(g->u.status[n] & STATUS_IN_AIR) && kp_unit_race(g, n) == KP_RACE_ZERG) zerg_ground++;
		if (f & DAT_BURROWABLE) burrowable = 1;
		if (s == base) continue;
		a = set_of(ui, s, &linked_s, &cs);
		b = set_of(ui, base, &linked_base, &cb);
		/* the same button array: in the stock data, the same buttons (no two arrays hold the same records) */
		if (a && b && cs == cb && memcmp(a, b, (size_t)cs * BUTTON_RECORD) == 0) continue;
		if (a && linked_s == base) continue;
		if (b && linked_base == s) {
			k->merged = s;
			base = s;
		} else {
			k->merged = SET_MIXED;
		}
	}
	if (k->merged == KP_UI_SET_NONE) return;
	if (ui->hud.selection_count == workers) k->merged = SET_WORKERS;
	else if (ui->hud.selection_count == cloakers) k->merged = SET_CLOAKERS;
	else if (burrowable && ui->hud.selection_count == zerg_ground) k->merged = SET_ZERG_GROUND;
}

/* ComputeCommandCardModes 0x4a18d0. */
void kp_ui_card_modes(KpUi *ui)
{
	KpUiCard *k = cc(ui);
	KpGame *g = game(ui);
	k->forced = k->set = KP_UI_SET_NONE;
	/* TODO: a replay's sets 248 and 249 */
	if (ui->hud.status_unit != 0) {
		if (ui->input.targeting) k->forced = SET_TARGETING;
		else if (kp_scalar(g, KP_PLACEMENT_ACTIVE) == 1) k->forced = SET_PLACEMENT;
	}
	k->merged = KP_UI_SET_NONE;
	if (ui->hud.selection_count > 1 && !g->w->replay) merge_sets(ui);
}

static uint16_t highlight_icon(KpGame *g, uint8_t order)
{
	return order == ORDER_NONE ? ICON_NONE : (uint16_t)KP_DAT(g, orders, highlight_icon, order);
}

/* HighlightCurrentOrderButton 0x4a1950. */
static void highlight(KpUi *ui)
{
	KpUiCard *k = cc(ui);
	uint16_t icon = highlight_icon(game(ui), k->order), sub = highlight_icon(game(ui), k->suborder);
	int32_t c;
	if (icon == ICON_NONE || (sub != ICON_NONE && icon == HIGHLIGHT_CANCEL)) icon = sub;
	for (c = ui->controls[k->dialog].child; c >= 0; c = ui->controls[c].next) {
		if ((ui->controls[c].flags & KP_UI_VISIBLE) && ui->controls[c].wuser == icon) {
			kp_ui_mark_dirty(ui, c);
			k->highlight = icon;
			return;
		}
	}
	k->highlight = ICON_NONE;
}

static void status_orders(KpUi *ui, uint8_t *order, uint8_t *suborder)
{
	KpGame *g = game(ui);
	int64_t n = ui->hud.status_unit, sub = g->u.subunit[n];
	*order = (uint8_t)g->u.order[n];
	*suborder = sub ? (uint8_t)g->u.order[sub] : ORDER_NONE;
}

/* --- the tooltip --- */

/* The card's control under the cursor (HitTestDialog 0x4c2090 at 0x6d3154, 0x6d3158), -1 none; none while paused. */
static int32_t hovered(KpUi *ui)
{
	KpUiEvent e;
	if (kp_scalar(game(ui), KP_PAUSED) != 0) return -1;
	memset(&e, 0, sizeof e);
	e.type = KP_UI_MOUSE_MOVE;
	e.x = (int16_t)ui->mouse_x;
	e.y = (int16_t)ui->mouse_y;
	return kp_ui_hit_test(ui, cc(ui)->dialog, &e);
}

static void set_costs(KpUi *ui, uint16_t minerals, uint16_t gas, uint16_t e2, uint16_t e3, uint16_t e4, uint16_t s5,
	uint16_t s6, uint16_t s7)
{
	/* SetTooltipCosts 0x4a11f0 */
	uint16_t *c = cc(ui)->costs;
	c[0] = minerals;
	c[1] = gas;
	c[2] = e2;
	c[3] = e3;
	c[4] = e4;
	c[5] = s5;
	c[6] = s6;
	c[7] = s7;
}

void kp_ui_card_hide_tooltip(KpUi *ui)
{
	/* HideCommandTooltip 0x4a0ef0 */
	kp_ui_tooltip_hide(ui);
	memset(cc(ui)->costs, 0, sizeof cc(ui)->costs);
	cc(ui)->level = 0;
}

/* DrawTooltipCostLine 0x4a0dc0: per nonzero cost its icons.grp icon and its number under the text. */
static void cost_line(KpUi *ui, int16_t r[4])
{
	static const uint8_t ICONS[COST_SLOTS] = { 0, COST_GAS, 9, 8, 7, 6, 5, 4 }; /* 0x50d630 */
	const KpUiFile *f = kp_ui_file(ui->grps, ui->grp_count, "game\\icons.grp");
	int32_t x = COST_X, w = 0, h = 0;
	int i, drawn = 0;
	for (i = 0; i < COST_SLOTS; i++) {
		uint16_t v = cc(ui)->costs[i];
		int frame = ICONS[i] == COST_GAS ? (int)game(ui)->players.race[game(ui)->local_player] + 1 : ICONS[i];
		const uint8_t *pixels = NULL;
		char text[16];
		if (v == 0) continue;
		w = h = 0;
		if (f != NULL && kp_ui_grp_bitmap(f->data, f->size, frame, &w, &h, &pixels) == 0) {
			int16_t src[4] = { 0, 0, (int16_t)(w - 1), (int16_t)(h - 1) };
			kp_ui_blit_transparent(ui, x, r[3], pixels, w, h, src);
		}
		x += (int16_t)w;
		snprintf(text, sizeof text, "%d", v);
		(void)kp_ui_draw_text(&ui->text, x + COST_TEXT_DX, r[3], (const uint8_t *)text);
		x = ui->text.ext_right;
		drawn = 1;
	}
	if (drawn) {
		if (x > r[2]) r[2] = (int16_t)x;
		r[3] = (int16_t)(r[3] + h + 1);
	}
}

/* ShowCommandTooltipText 0x4a1700 with RenderCommandTooltip 0x4a0a30: over the focused button. */
static void show_text(KpUi *ui, const uint8_t *text)
{
	KpUiCard *k = cc(ui);
	const KpUiControl *d = &ui->controls[k->dialog], *b;
	char buf[TOOLTIP_TEXT];
	if (text == NULL) {
		kp_ui_card_hide_tooltip(ui);
		return;
	}
	if (k->level != 0) {
		const uint8_t *next = kp_ui_tbl(ui, KP_UI_STAT_TXT, STRING_NEXT_LEVEL);
		snprintf(buf, sizeof buf, "%s\n%s %d", (const char *)text, next ? (const char *)next : "", k->level);
		text = (const uint8_t *)buf;
	}
	if (d->active < 0) return;
	b = &ui->controls[d->active];
	kp_ui_tooltip_over(ui, d->left + b->left, d->top - 1 + b->top, text, cost_line);
}

void kp_ui_card_tooltip(KpUi *ui, int32_t c)
{
	KpUiCard *k = cc(ui);
	KpGame *g = game(ui);
	KpUiControl *d = &ui->controls[k->dialog], *b;
	const uint8_t *rec, *s;
	int64_t status = ui->hud.status_unit, owner, t;
	if (c == k->dialog) {
		kp_ui_card_hide_tooltip(ui);
		return;
	}
	if (c < 0) {
		if (d->active >= 0) {
			kp_ui_mark_dirty(ui, d->active);
			d->active = -1;
			kp_ui_card_hide_tooltip(ui);
		}
		return;
	}
	b = &ui->controls[c];
	if (!(b->flags & KP_UI_VISIBLE) || (rec = record_of(ui, b)) == NULL) {
		kp_ui_card_hide_tooltip(ui);
		return;
	}
	s = kp_ui_tbl(ui, KP_UI_STAT_TXT, kp_le_u16(rec + 16));
	owner = status ? g->u.owner[status] : g->local_player;
	t = (int64_t)kp_le_u16(rec + 14);
	if (b->flags & KP_UI_DISABLED) {
		uint32_t id = disabled_string(ui, rec);
		d->active = c;
		memset(k->costs, 0, sizeof k->costs);
		k->level = 0;
		if (id == 0) {
			kp_ui_card_hide_tooltip(ui);
			return;
		}
		if (s && s[0] && s[1] == TIP_UPGRADE) id = (uint16_t)(id + (uint8_t)kp_upgrade_level(g, owner, t));
		show_text(ui, kp_ui_tbl(ui, KP_UI_STAT_TXT, id));
		return;
	}
	kp_ui_focus_control(ui, c);
	k->level = 0;
	switch (s && s[0] ? s[1] : 0) {
	case TIP_UNIT: {
		uint32_t group = (uint32_t)KP_DAT(g, units, staredit_group_flags, t);
		uint32_t m = (KP_DAT(g, units, special_ability_flags, t) & DAT_TWO_IN_ONE) ? 2 : 1;
		uint32_t supply = (uint32_t)KP_DAT(g, units, supply_required, t);
		set_costs(ui, (uint16_t)KP_DAT(g, units, mineral_cost, t), (uint16_t)KP_DAT(g, units, vespene_cost, t), 0, 0, 0,
			(uint16_t)((uint16_t)(m * ((group & GROUP_PROTOSS) ? supply : 0)) >> 1),
			(uint16_t)((uint16_t)(m * ((group & GROUP_TERRAN) ? supply : 0)) >> 1),
			(uint16_t)((uint16_t)(m * ((group & GROUP_ZERG) ? supply : 0)) >> 1));
		break;
	}
	case TIP_UNIT_NO_SUPPLY:
		set_costs(ui, (uint16_t)KP_DAT(g, units, mineral_cost, t), (uint16_t)KP_DAT(g, units, vespene_cost, t), 0, 0, 0, 0, 0,
			0);
		break;
	case TIP_UPGRADE: {
		int64_t minerals, gas;
		kp_upgrade_price(g, owner, t, &minerals, &gas);
		set_costs(ui, (uint16_t)minerals, (uint16_t)gas, 0, 0, 0, 0, 0, 0);
		if (KP_DAT(g, upgrades, max_repeats, t) > 1) k->level = (uint16_t)((uint8_t)kp_upgrade_level(g, owner, t) + 1);
		break;
	}
	case TIP_SPELL: {
		uint16_t e = (uint16_t)KP_DAT(g, techdata, energy_required, t);
		int64_t race = status ? kp_unit_race(g, status) : -1;
		set_costs(ui, 0, 0, race == KP_RACE_PROTOSS ? e : 0, race == KP_RACE_TERRAN ? e : 0, race == KP_RACE_ZERG ? e : 0,
			0, 0, 0);
		break;
	}
	case TIP_RESEARCH: {
		int64_t minerals, gas;
		kp_tech_price(g, t, &minerals, &gas);
		set_costs(ui, (uint16_t)minerals, (uint16_t)gas, 0, 0, 0, 0, 0, 0);
		break;
	}
	default:
		memset(k->costs, 0, sizeof k->costs);
		break;
	}
	show_text(ui, s ? s + 2 : NULL);
}

void kp_ui_card_refresh(KpUi *ui)
{
	KpUiCard *k = cc(ui);
	KpGame *g = game(ui);
	int64_t status = ui->hud.status_unit;
	uint16_t set;
	if (k->dialog < 0 || g == NULL) return;
	if (ui->hud.card_refresh && (k->forced == KP_UI_SET_NONE || g->w->replay)) kp_ui_card_modes(ui);
	if (k->forced != KP_UI_SET_NONE) set = k->forced;
	else if (k->merged != KP_UI_SET_NONE) set = k->merged;
	else set = status ? (uint16_t)g->u.button_set[status] : KP_UI_SET_NONE;
	if (set != k->set) {
		k->set = set;
		ui->hud.card_refresh = 1;
	}
	if (ui->hud.card_refresh) {
		populate(ui);
		k->hovered = hovered(ui);
		kp_ui_card_tooltip(ui, k->hovered);
		if (status) {
			highlight(ui);
			status_orders(ui, &k->order, &k->suborder);
			highlight(ui);
		}
		ui->hud.card_refresh = 0;
	} else if (status) {
		uint8_t o, so;
		status_orders(ui, &o, &so);
		if (o != k->order || so != k->suborder) {
			highlight(ui);
			k->order = o;
			k->suborder = so;
			highlight(ui);
		}
	}
}

void kp_ui_card_override(KpUi *ui, uint16_t set)
{
	cc(ui)->forced = set;
	kp_ui_card_refresh(ui);
}

/* --- the procedures --- */

/* UpdateCommandCardHover 0x4a1040. */
static void card_hover(KpUi *ui, const KpUiEvent *e)
{
	KpUiCard *k = cc(ui);
	int32_t h = kp_scalar(game(ui), KP_PAUSED) != 0 ? -1 : kp_ui_hit_test(ui, k->dialog, e);
	if (h == k->hovered) return;
	k->hovered = h;
	kp_ui_card_tooltip(ui, h);
}

/* CommandButtonInteract 0x4a0660. */
static int button_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	KpUiInteract stock = kp_ui_type_interact(c->type);
	const uint8_t *rec;
	switch (e->type) {
	case KP_UI_MOUSE_MOVE: { /* CommandButtonMouseMove 0x4a0740 */
		int32_t self = (int32_t)(c - ui->controls);
		if (kp_scalar(game(ui), KP_PAUSED) != 0 || kp_ui_hit_test(ui, c->parent, e) != self) {
			cc(ui)->hovered = -1;
			kp_ui_tooltip_hide(ui);
			memset(cc(ui)->costs, 0, sizeof cc(ui)->costs);
			cc(ui)->level = 0;
		} else if (cc(ui)->hovered != self) {
			cc(ui)->hovered = self;
			kp_ui_card_tooltip(ui, self);
		}
		return stock(ui, c, e);
	}
	case KP_UI_LEFT_DOWN: case KP_UI_LEFT_DOUBLE:
		return (c->flags & KP_UI_DISABLED) ? 0 : stock(ui, c, e);
	case KP_UI_LEFT_UP:
		(void)stock(ui, c, e);
		card_hover(ui, e);
		return 1;
	case KP_UI_USER:
		if (e->code == KP_UI_CREATE) {
			c->draw = draw_button;
			c->wuser = ICON_NONE;
			kp_ui_hide_control(ui, (int32_t)(c - ui->controls));
			return stock(ui, c, e);
		}
		if (e->code == KP_UI_CLOSE) { /* activate: the record's action */
			rec = record_of(ui, c);
			if (rec) kp_ui_button_action(ui, kp_le_u32(rec + 8), (uint16_t)kp_le_u16(rec + 14), ui->input.keys[0]);
			return 1;
		}
		if (e->code == KP_UI_HIT_TEST) return (c->flags & KP_UI_DISABLED) ? 1 : stock(ui, c, e);
		return stock(ui, c, e);
	default:
		return stock(ui, c, e);
	}
}

/* CommandProgressBarInteract 0x4a09f0 (InitCommandProgressBar 0x4a0810; its bitmaps are the status panel's). */
static int progress_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		c->draw = kp_ui_draw_progress;
		c->wuser = 0;
	}
	return kp_ui_type_interact(c->type)(ui, c, e);
}

/* MatchButtonHotkey 0x4a0620 within CommandCardHotkey 0x4a07b0. */
static int hotkey(KpUi *ui, KpUiEvent *e)
{
	int32_t c;
	int key;
	if (e->mods & CHAR_NO_HOTKEY) return 0;
	key = tolower(e->key & 0xff);
	for (c = ui->controls[cc(ui)->dialog].child; c >= 0; c = ui->controls[c].next) {
		KpUiControl *b = &ui->controls[c];
		const uint8_t *rec = record_of(ui, b), *s;
		if (b->id < 0 || b->id > 9 || !(b->flags & KP_UI_VISIBLE) || (b->flags & KP_UI_DISABLED) || rec == NULL) continue;
		s = kp_ui_tbl(ui, KP_UI_STAT_TXT, kp_le_u16(rec + 16));
		if (s && s[0] == (uint8_t)key) return kp_ui_send_user(ui, c, KP_UI_ACTIVATE, 0);
	}
	return 0;
}

/* CommandCardDialogProc 0x4a0b70. */
static int card_dialog(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	static const KpUiInteract CHILDREN[CARD_CHILDREN] = { button_interact, button_interact, button_interact,
		button_interact, button_interact, button_interact, button_interact, button_interact, button_interact,
		progress_interact };
	int32_t self = (int32_t)(d - ui->controls);
	KpUiInteract stock = kp_ui_type_interact(0);
	switch (e->type) {
	case KP_UI_KEY_DOWN: case KP_UI_KEY_REPEAT:
		return 0;
	case KP_UI_MOUSE_MOVE:
		card_hover(ui, e);
		return stock(ui, d, e);
	case KP_UI_USER:
		if (e->code == KP_UI_CREATE) {
			kp_ui_bind_interacts(ui, self, CHILDREN, CARD_CHILDREN);
			kp_ui_capture_console(ui, self);
		} else if (e->code == KP_UI_HIT_TEST) {
			return 1;
		}
		return stock(ui, d, e);
	case KP_UI_CHAR:
		return hotkey(ui, e);
	default:
		return stock(ui, d, e);
	}
}

int kp_ui_card_create(KpUi *ui)
{
	KpUiCard *k = cc(ui);
	KpGame *g = game(ui);
	const KpUiFile *ticon;
	char path[40];
	int race = (int)g->players.race[g->local_player];
	int32_t w, h;
	uint8_t pixels[96];
	if (race < 0 || race >= RACES) race = 1;
	memset(k, 0, sizeof *k);
	k->dialog = -1;
	k->set = k->forced = k->merged = KP_UI_SET_NONE;
	k->highlight = ICON_NONE;
	k->hovered = -1;
	snprintf(path, sizeof path, "unit\\cmdbtns\\%ccmdbtns.grp", RACE_LETTERS[race]);
	k->buttons = kp_ui_file(ui->grps, ui->grp_count, path);
	k->icons = kp_ui_file(ui->grps, ui->grp_count, "unit\\cmdbtns\\cmdicons.grp");
	ticon = kp_ui_file(ui->pcx, ui->pcx_count, "unit\\cmdbtns\\ticon.pcx");
	if (ticon && kp_ui_pcx(ticon->data, ticon->size, &w, &h, pixels, sizeof pixels) == 0) memcpy(k->slices, pixels, 96);
	/* TODO: a replay's card rez\statbtnn.bin */
	snprintf(path, sizeof path, "rez\\statbtn%c.bin", RACE_LETTERS[race]);
	k->dialog = kp_ui_load_dialog(ui, path);
	if (k->dialog < 0) return -1;
	kp_ui_init_dialog(ui, k->dialog, card_dialog);
	return 0;
}
