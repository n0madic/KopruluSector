/* The game (kp_game.h): creation, failures, the Lua calls of the hybrid. */
#include "kp_game_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

KpGame *kp_game_new(KpImages *w, KpRngGame *rng)
{
	KpGame *g = (KpGame *)calloc(1, sizeof *g);
	if (!g) return NULL;
	g->w = w;
	g->rng = rng;
	return g;
}

void kp_game_free(KpGame *g) { free(g); }

void kp_game_fail(KpGame *g, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(g->w->error, sizeof g->w->error, fmt, ap);
	va_end(ap);
	kp_images_rethrow(g->w);
}

int64_t kp_lua(KpGame *g, const char *method, int argc, int64_t a, int64_t b, int64_t c, int64_t d)
{
	int64_t args[4], res[1];
	args[0] = a;
	args[1] = b;
	args[2] = c;
	args[3] = d;
	res[0] = KP_NIL;
	g->lua(g, method, argc, args, 1, res);
	return res[0];
}

int64_t kp_game_unit(KpGame *g, int64_t n)
{
	if (n < 1 || n > KP_UNITS) kp_game_fail(g, "unit: record %lld out of range", (long long)n);
	return n;
}

int64_t kp_idiv64(KpGame *g, int64_t a, int64_t b)
{
	if (b == 0) kp_game_fail(g, "integer division by zero");
	if (a == -2147483648LL && b == -1) kp_game_fail(g, "integer overflow in division");
	return a / b;
}

int64_t kp_udiv64(KpGame *g, int64_t a, int64_t b)
{
	if (b == 0 || (uint32_t)b == 0) kp_game_fail(g, "integer division by zero");
	return (int64_t)((uint32_t)a / (uint32_t)b);
}

int64_t kp_urem64(KpGame *g, int64_t a, int64_t b)
{
	if (b == 0 || (uint32_t)b == 0) kp_game_fail(g, "integer division by zero");
	return (int64_t)((uint32_t)a % (uint32_t)b);
}

void kp_list64_unlink(int64_t *prev, int64_t *next, KpList *list, int64_t node, int64_t none)
{
	if (list->head == node) list->head = (int32_t)next[node];
	if (list->tail == node) list->tail = (int32_t)prev[node];
	if (prev[node] != none) next[prev[node]] = next[node];
	if (next[node] != none) prev[next[node]] = prev[node];
	next[node] = none;
	prev[node] = none;
}

void kp_list64_insert_after(int64_t *prev, int64_t *next, KpList *list, int64_t existing, int64_t node, int64_t none)
{
	if (list->tail == existing) list->tail = (int32_t)node;
	prev[node] = existing;
	next[node] = next[existing];
	if (next[existing] != none) prev[next[existing]] = node;
	next[existing] = node;
}

void kp_list64_insert_after_head(int64_t *prev, int64_t *next, KpList *list, int64_t node, int64_t none)
{
	if (list->head == none) {
		list->head = (int32_t)node;
		list->tail = (int32_t)node;
		return;
	}
	kp_list64_insert_after(prev, next, list, list->head, node, none);
}

void kp_list64_insert_before(int64_t *prev, int64_t *next, KpList *list, int64_t existing, int64_t node, int64_t none)
{
	if (list->head == existing) list->head = (int32_t)node;
	prev[node] = prev[existing];
	next[node] = existing;
	if (prev[existing] != none) next[prev[existing]] = node;
	prev[existing] = node;
}
