/*
 * The palette of the indexed pipeline (plan §7 phase 2 item 5): the tileset palette with the original's colour cycling,
 * the brightness (gamma) option, grayscale and fades (docs/research/native-palette-bootstrap.md,
 * native-pixel-effects.md).
 *
 * Clock: the original runs the cycle driver 0x4ccd30 from its game loop whenever the tick counter advanced more than
 * 9 ms (0x445020: GetTickCount), not per game frame; with the Windows tick counter's 15.625 ms resolution that is once
 * per counter step. Cycling stops while the game is paused (0x47cb60).
 */
#include "kp_present.h"

#include <math.h>
#include <string.h>

#define FIRST_PASS_LIMIT 0x80
#define SECOND_PASS_LIMIT 0x100
#define GAMMA_BASE 200
#define GAMMA_SCALE 100

/* 0x46a3b0: clears the eight records (0x4ccf00), sets one per definition entry (0x4cd000: speed, counter = speed,
 * low, high) and enables them (0x4cd070). */
static void bootstrap(KpPalette *p, const KpCycles *cycles)
{
	int k;
	memset(p->records, 0, sizeof p->records);
	for (k = 0; cycles && k < cycles->count && k < KP_PALETTE_RECORDS; k++) {
		KpCycleRecord *r = &p->records[k];
		r->speed = cycles->speed;
		r->counter = cycles->speed;
		r->low = cycles->low[k];
		r->high = cycles->high[k];
		r->active = 1;
	}
}

void kp_palette_init(KpPalette *p, const uint8_t rgb[768], const KpCycles *cycles)
{
	int i;
	memset(p, 0, sizeof *p);
	for (i = 0; i < 256; i++) {
		p->r[i] = rgb[3 * i];
		p->g[i] = rgb[3 * i + 1];
		p->b[i] = rgb[3 * i + 2];
	}
	p->brightness = KP_PALETTE_BRIGHTNESS_DEFAULT;
	p->gamma_brightness = -1;
	p->version = 1;
	bootstrap(p, cycles);
}

static void rotate_channel(uint8_t *ch, int32_t low, int32_t high)
{
	uint8_t last = ch[high];
	int32_t i;
	for (i = high; i > low; i--) ch[i] = ch[i - 1];
	ch[low] = last;
}

/* 0x4ccbc0 from record `first`: stops at record 8, a zero speed or a low entry above `limit`; an active record's BYTE
 * counter steps down and at zero reloads its speed and rotates entries low..high right by one (0x4cccf0). */
static int schedule(KpPalette *p, int first, int32_t limit, int *changed)
{
	int k = first;
	while (k < KP_PALETTE_RECORDS && p->records[k].speed != 0) {
		KpCycleRecord *r = &p->records[k];
		if (limit < r->low) break;
		if (r->active) {
			r->counter = (r->counter - 1 + 256) % 256;
			if (r->counter == 0) {
				r->counter = r->speed;
				rotate_channel(p->r, r->low, r->high);
				rotate_channel(p->g, r->low, r->high);
				rotate_channel(p->b, r->low, r->high);
				*changed = 1;
			}
		}
		k++;
	}
	return k;
}

int kp_palette_cycle(KpPalette *p)
{
	int changed = 0, k = schedule(p, 0, FIRST_PASS_LIMIT, &changed);
	if (k < KP_PALETTE_RECORDS) schedule(p, k, SECOND_PASS_LIMIT, &changed);
	if (changed) p->version++;
	return changed;
}

/* 0x4cf930: 60..140, anything else 100. */
void kp_palette_set_brightness(KpPalette *p, int32_t v)
{
	if (v < KP_PALETTE_BRIGHTNESS_MIN || v > KP_PALETTE_BRIGHTNESS_MAX) v = KP_PALETTE_BRIGHTNESS_DEFAULT;
	if (v != p->brightness) {
		p->brightness = v;
		p->version++;
	}
}

double kp_palette_gamma_exponent(int32_t brightness) { return (double)(GAMMA_BASE - brightness) / GAMMA_SCALE; }

/* 0x4cf760 per channel byte: trunc(256 * pow(c / 256, (200 - brightness) / 100)). */
void kp_palette_gamma_table(int32_t brightness, uint8_t out[256])
{
	double e = kp_palette_gamma_exponent(brightness);
	int c;
	for (c = 0; c < 256; c++) {
		out[c] = brightness == KP_PALETTE_BRIGHTNESS_DEFAULT ? (uint8_t)c
			: (uint8_t)((int64_t)floor(256.0 * pow((double)c / 256.0, e)) % 256);
	}
}

/* 0x4ccf40: each component floor(77 R / 256) + floor(151 G / 256) + floor(28 B / 256). */
int32_t kp_palette_gray(int32_t r, int32_t g, int32_t b) { return 77 * r / 256 + 151 * g / 256 + 28 * b / 256; }

void kp_palette_current(const KpPalette *p, uint8_t out[768])
{
	int i;
	for (i = 0; i < 256; i++) {
		out[3 * i] = p->r[i];
		out[3 * i + 1] = p->g[i];
		out[3 * i + 2] = p->b[i];
	}
}

/* The palette the device receives (0x4cf8c0: gamma unless brightness is 100), cached per version. */
const uint8_t *kp_palette_rgb(KpPalette *p)
{
	int i;
	if (p->rgb_version == p->version) return p->rgb;
	if (p->gamma_brightness != p->brightness) {
		kp_palette_gamma_table(p->brightness, p->gamma);
		p->gamma_brightness = p->brightness;
	}
	for (i = 0; i < 256; i++) {
		p->rgb[3 * i] = p->gamma[p->r[i]];
		p->rgb[3 * i + 1] = p->gamma[p->g[i]];
		p->rgb[3 * i + 2] = p->gamma[p->b[i]];
	}
	p->rgb_version = p->version;
	return p->rgb;
}

static void finish_fade(KpPalette *p)
{
	int i;
	for (i = 0; i < 256; i++) {
		p->r[i] = p->fade_to[3 * i];
		p->g[i] = p->fade_to[3 * i + 1];
		p->b[i] = p->fade_to[3 * i + 2];
	}
	p->fading = 0;
	p->version++;
}

/* 0x4ccd60 from the current palette to `target`: the original submits step 0 (n above 0) or the target at once
 * (n = 0), waits until FADE_WAIT_MS after its start, then submits the remaining steps without waiting: the palette
 * shows `from` for FADE_WAIT_MS, then the target. The original's wait blocks its game loop; this one does not. */
void kp_palette_fade_to(KpPalette *p, const uint8_t target[768], int32_t steps)
{
	if (steps > KP_PALETTE_FADE_MAX_STEPS) steps = KP_PALETTE_FADE_MAX_STEPS;
	memmove(p->fade_to, target, 768);
	p->fading = 1;
	p->fade_at = p->clock + (steps == 0 ? 0 : KP_PALETTE_FADE_WAIT_MS);
	if (steps == 0) finish_fade(p);
}

/* 0x4ccf40: the grayscale of the current palette as the fade target, faded at once. */
void kp_palette_grayscale(KpPalette *p)
{
	uint8_t out[768];
	int i;
	for (i = 0; i < 256; i++) {
		uint8_t v = (uint8_t)kp_palette_gray(p->r[i], p->g[i], p->b[i]);
		out[3 * i] = out[3 * i + 1] = out[3 * i + 2] = v;
	}
	kp_palette_fade_to(p, out, 0);
}

/* The cycle driver once per elapsed CYCLE_PERIOD_MS, at most MAX_CYCLES_PER_ADVANCE times (the original runs it at
 * most once per pass of its game loop, so a stall is not caught up), not while paused; a pending fade's end. */
int kp_palette_advance(KpPalette *p, double ms, int paused)
{
	uint32_t before = p->version;
	int steps = 0;
	p->clock += ms;
	p->cycle_acc += ms;
	while (p->cycle_acc >= KP_PALETTE_CYCLE_PERIOD_MS && steps < KP_PALETTE_MAX_CYCLES_PER_ADVANCE) {
		p->cycle_acc -= KP_PALETTE_CYCLE_PERIOD_MS;
		steps++;
		if (!paused) kp_palette_cycle(p);
	}
	if (p->cycle_acc >= KP_PALETTE_CYCLE_PERIOD_MS) p->cycle_acc = 0;
	if (p->fading && p->clock >= p->fade_at) finish_fade(p);
	return p->version != before;
}

void kp_palette_tone(const uint8_t base[768], const uint8_t current[768], const uint8_t skip[256], uint8_t out[1024])
{
	int c, v, i, coloured = 0, greyed = 0;
	memset(out, 0, 1024);
	for (c = 0; c < 3; c++) {
		int32_t sum[256], count[256], lo = -1, hi = -1, prev;
		memset(sum, 0, sizeof sum);
		memset(count, 0, sizeof count);
		for (i = 0; i < 256; i++) {
			if (skip[i]) continue;
			sum[base[3 * i + c]] += current[3 * i + c];
			count[base[3 * i + c]]++;
		}
		for (v = 0; v < 256; v++) {
			if (!count[v]) continue;
			if (lo < 0) lo = v;
			hi = v;
		}
		if (lo < 0) { /* nothing to go by: as it is */
			for (v = 0; v < 256; v++) out[4 * v + c] = (uint8_t)v;
			continue;
		}
		prev = -1;
		for (v = 0; v < 256; v++) {
			int32_t y;
			if (count[v]) {
				y = (sum[v] + count[v] / 2) / count[v];
				prev = v;
			} else if (v < lo) { /* below the darkest level known: in its proportion */
				y = lo ? (v * ((sum[lo] + count[lo] / 2) / count[lo]) + lo / 2) / lo : 0;
			} else if (v > hi) { /* above the brightest: in its proportion */
				y = hi ? (v * ((sum[hi] + count[hi] / 2) / count[hi]) + hi / 2) / hi : v;
			} else { /* between two known levels */
				int32_t next = v + 1, y0, y1;
				while (!count[next]) next++;
				y0 = (sum[prev] + count[prev] / 2) / count[prev];
				y1 = (sum[next] + count[next] / 2) / count[next];
				y = y0 + ((y1 - y0) * (v - prev) + (next - prev) / 2) / (next - prev);
			}
			out[4 * v + c] = (uint8_t)(y < 0 ? 0 : y > 255 ? 255 : y);
		}
	}
	for (i = 0; i < 256; i++) {
		if (skip[i] || (base[3 * i] == base[3 * i + 1] && base[3 * i] == base[3 * i + 2])) continue;
		coloured++;
		greyed += current[3 * i] == current[3 * i + 1] && current[3 * i] == current[3 * i + 2];
	}
	out[3] = (uint8_t)(coloured > 0 && greyed == coloured ? 255 : 0);
}
