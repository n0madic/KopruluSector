/*
 * The residency of the HD pages (ADR 0013, stage R4a): the HD packs' pages stay in their files and only those the
 * drawn images show are loaded, into a fixed number of slots (the layers of kp_gfx's HD textures) with the least
 * recently wanted page evicted first. The caller loads the pages taken here (kp_app: a thread reading the files) and
 * places them when they arrive; a page not resident draws its images' SD frames.
 */
#include "kp_present.h"

#include <stdlib.h>
#include <string.h>

int kp_hd_cache_init(KpHdCache *c, int32_t pages, int32_t slots)
{
	memset(c, 0, sizeof *c);
	if (pages < 0 || slots <= 0) return -1;
	c->pages = pages;
	c->slots = slots;
	c->slot_of = (int32_t *)malloc(sizeof *c->slot_of * (size_t)(pages ? pages : 1));
	c->page_of = (int32_t *)malloc(sizeof *c->page_of * (size_t)slots);
	c->used = (uint32_t *)calloc((size_t)(pages ? pages : 1), sizeof *c->used);
	c->retry = (uint32_t *)calloc((size_t)(pages ? pages : 1), sizeof *c->retry);
	c->state = (uint8_t *)calloc((size_t)(pages ? pages : 1), 1);
	c->queue = (int32_t *)malloc(sizeof *c->queue * (size_t)(pages ? pages : 1));
	if (!c->slot_of || !c->page_of || !c->used || !c->retry || !c->state || !c->queue) {
		kp_hd_cache_free(c);
		return -1;
	}
	kp_hd_cache_reset(c);
	return 0;
}

void kp_hd_cache_free(KpHdCache *c)
{
	free(c->slot_of);
	free(c->page_of);
	free(c->used);
	free(c->retry);
	free(c->state);
	free(c->queue);
	memset(c, 0, sizeof *c);
}

void kp_hd_cache_reset(KpHdCache *c)
{
	int32_t k;
	for (k = 0; k < c->pages; k++) {
		c->slot_of[k] = -1;
		c->retry[k] = c->clock;
		if (c->state[k] != KP_HD_FAILED) c->state[k] = KP_HD_ABSENT;
	}
	for (k = 0; k < c->slots; k++) c->page_of[k] = -1;
	c->queue_head = c->queue_count = 0;
	c->dirty = 1;
}

void kp_hd_cache_tick(KpHdCache *c) { c->clock++; }

void kp_hd_cache_want(KpHdCache *c, int32_t page)
{
	if (page < 0 || page >= c->pages) return;
	c->used[page] = c->clock;
	if (c->state[page] != KP_HD_ABSENT || (int32_t)(c->clock - c->retry[page]) < 0) return;
	c->state[page] = KP_HD_WANTED;
	/* the queue holds each page once at most: a page enters it only from absent */
	c->queue[(c->queue_head + c->queue_count) % c->pages] = page;
	c->queue_count++;
}

void kp_hd_cache_want_images(KpHdCache *c, const KpImageData *d, const KpFrameTable *hd)
{
	int f;
	if (!d->data || !hd->texels) return;
	for (f = 0; f < KP_FAMILIES; f++) { /* each family's slots up to the highest it has used */
		int32_t slot, end = KP_FAMILY_BASE[f] + d->next_slot[f];
		for (slot = KP_FAMILY_BASE[f]; slot < end; slot++) {
			int32_t code = (int32_t)d->data[(size_t)slot * 8 + 4], frame = code / (2 * KP_DRAW_KINDS);
			const float *t;
			if (frame < 0 || frame >= hd->height * (KP_FRAME_TABLE_WIDTH / 2)) continue;
			t = &hd->texels[(size_t)frame * 8];
			if (t[2] > 0) kp_hd_cache_want(c, (int32_t)t[4]);
		}
	}
}

int kp_hd_cache_take(KpHdCache *c, int32_t *page)
{
	while (c->queue_count > 0) {
		int32_t p = c->queue[c->queue_head];
		c->queue_head = (c->queue_head + 1) % c->pages;
		c->queue_count--;
		if (c->state[p] != KP_HD_WANTED) continue;
		if (c->used[p] != c->clock) { /* wanted by an earlier tick only: not loaded until wanted again */
			c->state[p] = KP_HD_ABSENT;
			continue;
		}
		c->state[p] = KP_HD_LOADING;
		*page = p;
		return 1;
	}
	return 0;
}

int32_t kp_hd_cache_place(KpHdCache *c, int32_t page, int32_t *evicted)
{
	int32_t k, slot = -1;
	*evicted = -1;
	/* only a page taken and not forgotten since (a reset while it loaded): one queue entry and one slot a page */
	if (page < 0 || page >= c->pages || c->state[page] != KP_HD_LOADING) return -1;
	for (k = 0; k < c->slots && slot < 0; k++) {
		if (c->page_of[k] < 0) slot = k;
	}
	if (slot < 0) { /* the least recently wanted page, one not wanted this tick */
		uint32_t oldest = c->clock;
		for (k = 0; k < c->slots; k++) {
			int32_t p = c->page_of[k];
			if (c->used[p] != c->clock && (slot < 0 || c->clock - c->used[p] > c->clock - oldest)) {
				slot = k;
				oldest = c->used[p];
			}
		}
	}
	if (slot < 0) {
		c->state[page] = KP_HD_ABSENT;
		c->retry[page] = c->clock + KP_HD_RETRY;
		return -1;
	}
	if (c->page_of[slot] >= 0) {
		*evicted = c->page_of[slot];
		c->slot_of[*evicted] = -1;
		c->state[*evicted] = KP_HD_ABSENT;
	}
	c->page_of[slot] = page;
	c->slot_of[page] = slot;
	c->state[page] = KP_HD_RESIDENT;
	c->dirty = 1;
	return slot;
}

void kp_hd_cache_failed(KpHdCache *c, int32_t page)
{
	if (page >= 0 && page < c->pages) c->state[page] = KP_HD_FAILED;
}

int kp_hd_cache_take_dirty(KpHdCache *c)
{
	int dirty = c->dirty;
	c->dirty = 0;
	return dirty;
}

int32_t kp_hd_cache_pending(const KpHdCache *c)
{
	int32_t k, n = 0;
	for (k = 0; k < c->pages; k++) n += c->state[k] == KP_HD_WANTED || c->state[k] == KP_HD_LOADING;
	return n;
}
