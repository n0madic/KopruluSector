/* Protoss psi power (kp_game.h), ported from the atlas (P10, P11): the power mask of the Psi field graphic
 * (InitPsiFieldMask 0x42fe60), the provider list of the Pylons (Order_InitializePsiProvider 0x42f5b0,
 * UnlinkPowerProvider 0x430610) with their Psi field sprites (0x42f270), the power predicate (IsPointPowered
 * 0x430000) and the power update of the Protoss buildings (UpdateProtossPower 0x430670). */
#include "kp_game_internal.h"

#include <string.h>

#define PSI_FIELD_IMAGE 584
#define PSI_FIELD_SPRITE 321
#define DAT_REQUIRES_PSI 0x80000
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_DISABLED 0x400
#define SPRITE_SELECTED 0x08
#define SPRITE_HIDDEN 0x20
#define RACE_PROTOSS 2
#define CONTROLLER_RESCUE 3
#define ANIM_IS_WORKING 19
#define ANIM_DISABLE 24
#define ANIM_ENABLE 27
#define ORDER_CONSTRUCTING 31
#define NO_UPGRADE 61

/* InitPsiFieldMask 0x42fe60: the cells of the mask sample frame 0 of the Psi field graphic at their centres, mirrored
 * into four quadrants. */
void kp_power_init(KpGame *g)
{
	KpPower *p = &g->power;
	KpImages *w = g->w;
	int32_t entry = kp_images_grp_of(w, PSI_FIELD_IMAGE);
	int x, y, fw, fh;
	int64_t r, c;
	memset(p, 0, sizeof *p);
	if (entry <= 0) return; /* data without the graphic: no mask, nothing is powered */
	kp_grpmeta_frame(w->grp, (uint32_t)entry, 0, &x, &y, &fw, &fh);
	p->half_w = (fw + 15) / 32;
	p->half_h = (fh + 15) / 32;
	if (p->half_w * p->half_h * 4 > KP_POWER_MASK) {
		kp_game_fail(g, "power: a psi field of %lld x %lld tiles", (long long)p->half_w, (long long)p->half_h);
	}
	for (r = 0; r < p->half_h; r++) {
		for (c = 0; c < p->half_w; c++) {
			uint8_t v = (uint8_t)kp_grpmeta_pixel_opaque(w->grp, (uint32_t)entry, 0, 16 + 32 * c, 16 + 32 * r);
			int64_t stride = 2 * p->half_w;
			p->mask[r * stride + p->half_w + c] = v;
			p->mask[r * stride + p->half_w - 1 - c] = v;
			p->mask[(2 * p->half_h - 1 - r) * stride + p->half_w + c] = v;
			p->mask[(2 * p->half_h - 1 - r) * stride + p->half_w - 1 - c] = v;
		}
	}
	p->half_w *= 32;
	p->half_h *= 32;
	g->power_head = 0;
	kp_scalar_set(g, KP_POWER_DIRTY, 0);
}

/* IsPointPowered 0x430000: a type that needs psi stands powered at (x, y) for player. */
int kp_point_powered(KpGame *g, int64_t type, int64_t x, int64_t y, int64_t player)
{
	KpPower *p = &g->power;
	int64_t n;
	if (kp_band(KP_DAT(g, units, special_ability_flags, type), DAT_REQUIRES_PSI) == 0) return 1;
	for (n = g->power_head; n != 0; n = g->u.rally_unit[n]) {
		int32_t s;
		int64_t sx, sy, row, col;
		if (g->u.owner[n] != kp_band(player, 0xff)) continue;
		s = kp_unit_sprite(g, n);
		sx = kp_s16v(g->w->spr.x[s]);
		sy = kp_s16v(g->w->spr.y[s]);
		if ((sx > x ? sx - x : x - sx) >= p->half_w || (sy > y ? sy - y : y - sy) >= p->half_h) continue;
		row = (p->half_h - sy + y) / 32;
		col = (p->half_w - sx + x) / 32;
		if (p->mask[row * 2 * (p->half_w / 32) + col] != 0) return 1;
	}
	return 0;
}

static int local_provider(KpGame *g, int64_t n) { return !g->w->replay && g->u.owner[n] == g->local_player; }

/* CreatePsiFieldSprite 0x42f270: the provider's Psi field, hidden unless the local player shows the fields (all of
 * them when the provider is selected); 0 when no sprite is left. */
static int psi_field(KpGame *g, int64_t n)
{
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n), f;
	f = kp_sprite_create(w, PSI_FIELD_SPRITE, (int32_t)kp_s16v(w->spr.x[s]), (int32_t)kp_s16v(w->spr.y[s]), 0);
	g->u.union_0xd0[n] = f == KP_NONE ? 0 : f + 1;
	if (f == KP_NONE) return 0;
	w->spr.elevation[f] = 1;
	if (local_provider(g, n)) {
		if (g->power.fields_shown) {
			kp_sprite_set_visibility(w, f, (int32_t)g->local_vision);
			return 1;
		}
		if (kp_band(w->spr.flags[s], SPRITE_SELECTED) != 0) {
			/* as the original's: ShowAllPsiFieldOverlays sets 0x665b64 only after its loop, so a hide that meets a
			 * selected provider without a field shows the fields again (both sides alike) */
			kp_power_show_fields(g);
			return 1;
		}
	}
	w->spr.flags[f] = (int32_t)kp_bor(w->spr.flags[f], SPRITE_HIDDEN);
	kp_sprite_set_visibility(w, f, 0);
	return 1;
}

/* ShowAllPsiFieldOverlays 0x430320: the local player's Psi fields show (for a placement that needs power or a selected
 * Pylon). */
void kp_power_show_fields(KpGame *g)
{
	KpImages *w = g->w;
	int64_t n;
	for (n = g->power_head; n != 0; n = g->u.rally_unit[n]) {
		int32_t f;
		if (!local_provider(g, n)) continue;
		if (g->u.union_0xd0[n] == 0 && !psi_field(g, n)) continue;
		f = (int32_t)(g->u.union_0xd0[n] - 1);
		w->spr.flags[f] = (int32_t)kp_band(w->spr.flags[f], kp_bnot(SPRITE_HIDDEN));
		kp_sprite_set_visibility(w, f, (int32_t)g->local_vision);
	}
	g->power.fields_shown = 1;
}

/* HideAllPsiFieldOverlays 0x42fda0: every Psi field (made where missing) hides again. */
void kp_power_hide_fields(KpGame *g)
{
	KpImages *w = g->w;
	int64_t n;
	for (n = g->power_head; n != 0; n = g->u.rally_unit[n]) {
		int32_t f;
		if (g->u.union_0xd0[n] == 0 && !psi_field(g, n)) continue;
		f = (int32_t)(g->u.union_0xd0[n] - 1);
		w->spr.flags[f] = (int32_t)kp_bor(w->spr.flags[f], SPRITE_HIDDEN);
		kp_sprite_set_visibility(w, f, 0);
	}
	g->power.fields_shown = 0;
}

/* Order_InitializePsiProvider 0x42f5b0 (order 164): a Pylon joins the front of the provider list. */
void kp_order_init_psi_provider(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t head = g->power_head;
	if (head != n && u->rally_unit[n] == 0 && g->power.prev[n] == 0) {
		g->power.prev[n] = 0;
		u->rally_unit[n] = head;
		if (head != 0) g->power.prev[head] = n;
		g->power_head = n;
		psi_field(g, n);
		kp_scalar_set(g, KP_POWER_DIRTY, 1);
	}
	kp_issue_order(g, n, KP_DAT(g, units, return_to_idle, u->type[n]));
	kp_order_unlock_done(g, n);
}

/* The removal of a provider (TeardownUnitTypeLinks 0x4253a0 for type 156): its Psi field goes, it leaves the list
 * (UnlinkPowerProvider 0x430610) and the power is updated (MarkPowerDirty 0x42ffc0). */
void kp_power_remove_provider(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t prev = g->power.prev[n], next = u->rally_unit[n];
	if (u->union_0xd0[n] != 0) {
		kp_sprite_destroy(g->w, (int32_t)(u->union_0xd0[n] - 1));
		u->union_0xd0[n] = 0;
	}
	if (prev != 0) u->rally_unit[prev] = next;
	if (next != 0) g->power.prev[next] = prev;
	g->power.prev[n] = 0;
	if (g->power_head == n) g->power_head = next;
	u->rally_unit[n] = 0;
	kp_scalar_set(g, KP_POWER_DIRTY, 1);
}

/* 0x42f240: a building constructing an incomplete unit. */
int kp_building_unit(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[n];
	return u->order[n] == ORDER_CONSTRUCTING && kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0 && t != 0
		&& kp_band(u->status[t], STATUS_COMPLETED) == 0;
}

/* UpdateProtossPower 0x430670: the Protoss buildings go dark or light up when the power changed. */
void kp_update_power(KpGame *g)
{
	KpUnits *u = &g->u;
	int64_t n;
	if (kp_scalar(g, KP_POWER_DIRTY) != 1) return;
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = u->next[n]) {
		int32_t s;
		int enabled;
		if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) == 0 || kp_unit_race(g, n) != RACE_PROTOSS
			|| kp_controller(g, u->owner[n]) == CONTROLLER_RESCUE) {
			continue;
		}
		enabled = kp_band(u->status[n], STATUS_DISABLED) == 0;
		s = kp_unit_sprite(g, n);
		if (!kp_point_powered(g, u->type[n], kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), u->owner[n])) {
			u->status[n] = kp_bor(u->status[n], STATUS_DISABLED);
			kp_reset_to_idle_animation(g, n);
			if (kp_band(u->status[n], STATUS_COMPLETED) != 0 && enabled) kp_sprite_play(g->w, s, ANIM_DISABLE, 1);
		} else {
			u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_DISABLED));
			if (kp_band(u->status[n], STATUS_COMPLETED) == 0 || enabled) continue;
			kp_sprite_play(g->w, s, ANIM_ENABLE, 1);
			if (kp_building_unit(g, n) || kp_band(u->union_0xc8[n] >> 8, 0xff) != NO_UPGRADE) {
				kp_sprite_play(g->w, s, ANIM_IS_WORKING, 1);
			}
		}
	}
	kp_scalar_set(g, KP_POWER_DIRTY, 0);
	/* the status interface refresh 0x4a7ed0 is the presentation's */
}
