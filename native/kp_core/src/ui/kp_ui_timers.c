/* The UI timer queue of timer.cpp (kp_ui.h; atlas P43 0x4c5840..0x4c5b70, the list helpers of P44 0x4cab80..). */
#include "kp_ui.h"

#include <string.h>

#define LATENESS_LIMIT 500

void kp_ui_timers_init(KpUiTimers *t)
{
	int i;
	memset(t, 0, sizeof *t);
	t->active = t->fired = KP_UI_NONE;
	for (i = 0; i < KP_UI_TIMERS; i++) t->nodes[i].next = i + 1 < KP_UI_TIMERS ? i + 1 : KP_UI_NONE;
	t->free_list = 0;
}

/* TimerSortKey 0x4c5b70: -(period + last), u32 arithmetic, compared signed. */
static int32_t sort_key(const KpUiTimer *n)
{
	return (int32_t)(0u - (n->period + n->last));
}

/* ListInsertSorted 0x4cabe0: before the first node whose key is not greater than the node's (descending keys, a
 * new node before equal ones). */
static void insert_sorted(KpUiTimers *t, int32_t *head, int32_t node)
{
	int32_t k = sort_key(&t->nodes[node]);
	int32_t *link = head;
	while (*link != KP_UI_NONE && k < sort_key(&t->nodes[*link])) link = &t->nodes[*link].next;
	t->nodes[node].next = *link;
	*link = node;
}

/* ListUnlink 0x4cac30: the node, or KP_UI_NONE when it is not in the list. */
static int32_t unlink_node(KpUiTimers *t, int32_t *head, int32_t node)
{
	int32_t *link = head;
	while (*link != KP_UI_NONE && *link != node) link = &t->nodes[*link].next;
	if (*link == KP_UI_NONE) return KP_UI_NONE;
	*link = t->nodes[node].next;
	return node;
}

static int contains(const KpUiTimers *t, int32_t head, int32_t node)
{
	for (; head != KP_UI_NONE; head = t->nodes[head].next) {
		if (head == node) return 1;
	}
	return 0;
}

/* TimerMatchesOwnerAndId 0x4c5b30 over the search keys. */
static int32_t find_keyed(const KpUiTimers *t, int32_t head)
{
	for (; head != KP_UI_NONE; head = t->nodes[head].next) {
		if (t->nodes[head].owner == t->key_owner && t->nodes[head].id == t->key_id) return head;
	}
	return KP_UI_NONE;
}

static int32_t find_owner(const KpUiTimers *t, int32_t head)
{
	for (; head != KP_UI_NONE; head = t->nodes[head].next) {
		if (t->nodes[head].owner == t->key_owner) return head;
	}
	return KP_UI_NONE;
}

/* A freed node goes to the free list's end: a timer set right after a deletion (a callback that deletes its own timer
 * and opens a dialog) does not take the node RunUiTimers still holds, which the original's heap does not hand out at
 * once either (the gluLogin new ID popup's flic timers). */
static void free_node(KpUiTimers *t, int32_t node)
{
	int32_t *link = &t->free_list;
	while (*link != KP_UI_NONE) link = &t->nodes[*link].next;
	t->nodes[node].next = KP_UI_NONE;
	*link = node;
}

static void trace(KpUiTimers *t, int kind, int64_t owner, uint16_t id, uint32_t period, int callback, uint32_t tick)
{
	KpUiTimerRecord *r;
	if (t->trace == NULL || t->trace->count >= KP_UI_TIMER_TRACE) return;
	r = &t->trace->rec[t->trace->count++];
	r->kind = (uint8_t)kind;
	r->owner = owner;
	r->id = id;
	r->period = period;
	r->callback = callback;
	r->tick = tick;
	r->bin = r->record = -1;
	if (t->trace->controls != NULL && owner >= 0 && owner < KP_UI_CONTROLS && t->trace->controls[owner].used) {
		r->bin = t->trace->controls[owner].bin;
		r->record = t->trace->controls[owner].record;
	}
}

int kp_ui_set_timer(KpUiTimers *t, int64_t owner, uint16_t id, uint32_t period, int callback, uint32_t now)
{
	int32_t node = KP_UI_NONE;
	KpUiTimer *n;
	if (t->set_ticks != NULL && t->set_tick_next < t->set_tick_count) now = t->set_ticks[t->set_tick_next++];
	if (t->active != KP_UI_NONE) { /* the keys are stored only here (0x4c5975) */
		t->key_owner = owner;
		t->key_id = id;
		node = find_keyed(t, t->active);
		if (node != KP_UI_NONE) unlink_node(t, &t->active, node);
	}
	if (node == KP_UI_NONE && t->fired != KP_UI_NONE) { /* with an empty active queue: the previous search's keys */
		node = find_keyed(t, t->fired);
		if (node != KP_UI_NONE) unlink_node(t, &t->fired, node);
	}
	if (node == KP_UI_NONE) {
		if (t->free_list == KP_UI_NONE) return -1;
		node = t->free_list;
		t->free_list = t->nodes[node].next;
	}
	n = &t->nodes[node];
	n->callback = callback;
	n->owner = owner;
	n->id = id;
	n->period = period;
	n->last = now;
	n->lateness = 0;
	insert_sorted(t, &t->active, node);
	trace(t, KP_UI_TRACE_SET, owner, id, period, callback != 0, now);
	return 0;
}

void kp_ui_delete_timer(KpUiTimers *t, int64_t owner, uint16_t id)
{
	int32_t node, *head = &t->active;
	trace(t, KP_UI_TRACE_DEL, owner, id, 0, 0, 0);
	t->key_owner = owner;
	t->key_id = id;
	node = find_keyed(t, t->active);
	if (node == KP_UI_NONE) {
		head = &t->fired;
		node = find_keyed(t, t->fired);
	}
	if (node == KP_UI_NONE) return;
	unlink_node(t, head, node);
	free_node(t, node);
}

void kp_ui_delete_owner_timers(KpUiTimers *t, int64_t owner)
{
	int32_t *heads[2];
	int k;
	trace(t, KP_UI_TRACE_DELALL, owner, 0, 0, 0, 0);
	heads[0] = &t->active;
	heads[1] = &t->fired;
	for (k = 0; k < 2; k++) {
		int32_t node;
		while (*heads[k] != KP_UI_NONE) { /* 0x4c5840 stores the owner key before each search */
			t->key_owner = owner;
			node = find_owner(t, *heads[k]);
			if (node == KP_UI_NONE) break;
			unlink_node(t, heads[k], node);
			free_node(t, node);
		}
	}
}

static int run_from(KpUiTimers *t, int32_t node, uint32_t now, int fired, KpUiTimerFn fn, void *ctx);

int kp_ui_run_timers(KpUiTimers *t, uint32_t now, KpUiTimerFn fn, void *ctx)
{
	return run_from(t, t->active, now, 0, fn, ctx);
}

int kp_ui_resume_timers(KpUiTimers *t, KpUiTimerFn fn, void *ctx)
{
	int32_t next;
	if (t->suspended_count == 0) return 0;
	t->suspended_count--;
	next = t->suspended[t->suspended_count].next;
	/* the interrupted loop's next node, when its own work did not delete it (0x4c5ac5's test) */
	if (next != KP_UI_NONE && !contains(t, t->active, next)) next = KP_UI_NONE;
	return run_from(t, next, t->suspended[t->suspended_count].now, t->suspended[t->suspended_count].fired, fn, ctx);
}

static int run_from(KpUiTimers *t, int32_t node, uint32_t now, int fired, KpUiTimerFn fn, void *ctx)
{
	t->in_pass++;
	while (node != KP_UI_NONE) {
		KpUiTimer *n = &t->nodes[node];
		int32_t next, late = (int32_t)(now - n->last - n->period);
		if (late <= 0) break;
		if (late < LATENESS_LIMIT) n->lateness += (late - n->lateness) >> 5;
		n->last = now;
		next = n->next;
		if (fired++ == 0) trace(t, KP_UI_TRACE_PASS, 0, 0, 0, 0, now);
		trace(t, KP_UI_TRACE_FIRE, n->owner, n->id, n->period, n->callback != 0, now);
		fn(ctx, n->owner, n->id, n->callback, n->period);
		/* a node its own work deleted or replaced is not pushed again (0x4c5ac5) */
		if (unlink_node(t, &t->active, node) == node) {
			t->nodes[node].next = t->fired;
			t->fired = node;
		}
		if (t->suspend && t->suspended_count < (int)(sizeof t->suspended / sizeof t->suspended[0])) {
			/* the callback's modal loop runs: this pass goes on at its end (kp_ui_resume_timers) */
			t->suspend = 0;
			t->suspended[t->suspended_count].next = next;
			t->suspended[t->suspended_count].now = now;
			t->suspended[t->suspended_count].fired = fired;
			t->suspended_count++;
			t->in_pass--;
			return fired;
		}
		if (next == KP_UI_NONE || !contains(t, t->active, next)) break;
		node = next;
	}
	t->in_pass--;
	while (t->fired != KP_UI_NONE) { /* every fired node, from the fired queue's head, sorted back */
		node = t->fired;
		t->fired = t->nodes[node].next;
		insert_sorted(t, &t->active, node);
	}
	return fired;
}

int kp_ui_timer_remaining(const KpUiTimers *t, int64_t owner, uint16_t id, uint32_t now, uint32_t *ms)
{
	int32_t node;
	for (node = t->active; node != KP_UI_NONE; node = t->nodes[node].next) {
		const KpUiTimer *n = &t->nodes[node];
		if (n->owner == owner && n->id == id) {
			int32_t left = (int32_t)(n->last + n->period - now);
			*ms = left > 0 ? (uint32_t)left : 0;
			return 1;
		}
	}
	return 0;
}

int kp_ui_timer_order(const KpUiTimers *t, int32_t *out, int max)
{
	int count = 0;
	int32_t node;
	for (node = t->active; node != KP_UI_NONE && count < max; node = t->nodes[node].next) out[count++] = node;
	return count;
}
