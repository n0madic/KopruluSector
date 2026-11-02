/* The order queue and its issuers against the original's order functions (docs/research/native-primary-orders.md;
 * plan §7 phase 3 item 5).
 *
 * tests/golden/oracle/orders_primary.json holds 535 cases that tools/research/probe_native_primary_orders_117.py ran
 * on the original code with a real order pool: issuers, allocation and freeing, insertion, pruning append, clearing,
 * activation and its locks, EndOrder, Cloak, the burrowed AI's Unburrow, the AI's highlight limit and the subunit's
 * order mapping. Each case is rebuilt here on a game started from a scenario map: one unit takes the probe's unit
 * fields (owner 3, order 3, state 7, order point 10, 20 with FoW type 17, timers 15 and 23, user flags 0x82, flingy
 * flags 15, auto target = the second unit, sprite at 300, 400 with flags 255), a second unit is the probe's OTHER and
 * a third its subunit; orders.dat and units.dat take the probe's table bytes (stock orders.dat for the stock_flags
 * cases), and the order pool keeps only the probe's free nodes. orders_special.json holds the special order consumers
 * (RescuePassive, the trap). The orders on remembered units of the fog (a right click on a ghost, RightClickAction,
 * OrderAttackObscured) run on fog_right_click's map. Needs the converter's generated data; skipped without it. */
#include "kp_test.h"
#include "kp_ids.h"

#define GOLDEN "tests/golden/oracle/orders_primary.json"
#define SPECIAL "tests/golden/oracle/orders_special.json"
#define MAP "generated/scenarios/idle_marines.kpm"
#define SEED 946684819
#define NONE (-1)
#define OTHER 0x800200 /* the probe's second unit record, as targets and auto targets show it */
#define NO_TYPE 228
#define CLOAK 109
#define AI_GUARD_0 0x10001 /* a unit's AI record: guard record 0 (KP_AI_REF(KP_AI_GUARD, 0)), the probes' nonnull pointer */
#define MAX_POOL 2000

static const char *const FILES[] = { KP_ORACLE_DATA "dat.kpd", MAP, NULL };

typedef struct {
	KpOracleGame *og;
	KpGame *g;
	int64_t unit, other, sub;
	int64_t pool[MAX_POOL];
	int pool_count;
	KpJsonDoc *doc;
	int64_t played; /* the animation the probe's stop saw (animation_boundary) */
} Fixture;

/* A game of the probes' map (events on, no campaign row). */
static KpOracleGame *new_game(KpT *T, int expansion, int no_units)
{
	return kp_t_probe_game(T, MAP, SEED, expansion, no_units);
}

/* A node unlinked from the order pool's free list. */
static void unlink_free(KpOrders *o, int64_t node)
{
	if (o->free.head == node) o->free.head = (int32_t)o->next[node];
	if (o->free.tail == node) o->free.tail = (int32_t)o->prev[node];
	if (o->prev[node] != NONE) o->next[o->prev[node]] = o->next[node];
	if (o->next[node] != NONE) o->prev[o->next[node]] = o->prev[node];
	o->next[node] = o->prev[node] = NONE;
}

/* The probe's options of a fixture (probe new()). */
typedef struct {
	int64_t unit_id, current_order, status, pool_count, animation;
	int ai, stock;
} FixtureOpts;

static void fixture_opts(FixtureOpts *f)
{
	f->unit_id = 15;
	f->current_order = 3;
	f->status = 1;
	f->pool_count = 16;
	f->animation = 0;
	f->ai = 0;
	f->stock = 0;
}

static void fixture(KpT *T, const FixtureOpts *f, Fixture *fx)
{
	KpOracleGame *og = new_game(T, 0, 0);
	KpGame *g = og->g;
	KpImages *w = g->w;
	int64_t units[3], t3[3], i;
	int k, n;
	n = kp_t_list_units(g, KP_LIST_ACTIVE, units, 3);
	kp_t_ok(T, n == 3, "three units on the map");
	if (!f->stock) {
		for (i = 0; i <= 188; i++) {
			kp_t_dat_set(T, og, "orders", "interruptable", i, 1);
			kp_t_dat_set(T, og, "orders", "highlight_icon", i, 0xffff);
			kp_t_dat_set(T, og, "orders", "changes_subunit_order", i, 0);
		}
	}
	t3[0] = f->unit_id;
	t3[1] = 16;
	t3[2] = 17;
	for (k = 0; k < 3; k++) {
		kp_t_dat_set(T, og, "units", "special_ability_flags", t3[k], 0);
		kp_t_dat_set(T, og, "units", "return_to_idle", t3[k], 3);
		kp_t_dat_set(T, og, "units", "attack_unit", t3[k], 10);
		kp_t_dat_set(T, og, "units", "attack_move", t3[k], 14);
	}
	kp_order_clear_queue(g, units[0]);
	for (k = 0; k < 3; k++) {
		int64_t m = units[k];
		int32_t s;
		g->u.type[m] = k == 0 ? f->unit_id : k == 1 ? 16 : 17;
		g->u.owner[m] = 3;
		g->u.order[m] = k == 0 ? f->current_order : 3;
		g->u.order_state[m] = 7;
		g->u.order_type[m] = 17;
		g->u.order_x[m] = 10;
		g->u.order_y[m] = 20;
		g->u.order_unit[m] = 0;
		g->u.status[m] = k == 0 ? f->status : 1;
		g->u.queue_timer[m] = 15;
		g->u.recent_order_timer[m] = 23;
		g->u.user_action[m] = 0x82;
		g->u.movement_flags[m] = 15;
		g->u.auto_target[m] = units[1];
		g->u.queue_count[m] = 0;
		g->u.subunit[m] = 0;
		g->u.ai[m] = 0;
		s = (int32_t)g->u.sprite[m];
		w->spr.x[s] = 300;
		w->spr.y[s] = 400;
		w->img.anim[w->spr.main[s]] = (int32_t)f->animation;
	}
	w->spr.flags[g->u.sprite[units[0]]] = 255;
	if (f->ai) g->u.ai[units[0]] = AI_GUARD_0;
	/* The pool keeps its first pool_count free nodes (the probe's POOL + 20 i, in order). */
	fx->pool_count = 0;
	for (i = g->orders.free.head; i != NONE;) {
		int64_t nx = g->orders.next[i];
		if (fx->pool_count < f->pool_count) fx->pool[fx->pool_count++] = i;
		else unlink_free(&g->orders, i);
		i = nx;
	}
	g->orders.allocated = 0;
	w->unit = 0xabcdef;
	w->has_unit = 1;
	w->flingy = 0x234567;
	w->has_flingy = 1;
	fx->og = og;
	fx->g = g;
	fx->unit = units[0];
	fx->other = units[1];
	fx->sub = units[2];
}

static int64_t ref(const Fixture *fx, int64_t m) { return m == fx->other ? OTHER : m; }

static int64_t word(int64_t v) { return ((v % 65536) + 65536) % 65536; }

static KpJson *state(const Fixture *fx, int64_t m)
{
	KpGame *g = fx->g;
	KpJsonDoc *doc = fx->doc;
	KpJson *o = kp_json_new(doc, KP_JSON_OBJECT);
	kp_json_set_int(doc, o, "order", g->u.order[m]);
	kp_json_set_int(doc, o, "state", g->u.order_state[m]);
	kp_json_set_int(doc, o, "fow", g->u.order_type[m]);
	kp_json_set_int(doc, o, "x", word(g->u.order_x[m]));
	kp_json_set_int(doc, o, "y", word(g->u.order_y[m]));
	kp_json_set_int(doc, o, "target", ref(fx, g->u.order_unit[m]));
	kp_json_set_int(doc, o, "status", g->u.status[m]);
	kp_json_set_int(doc, o, "highlight", g->u.queue_count[m]);
	kp_json_set_int(doc, o, "wait", g->u.queue_timer[m]);
	kp_json_set_int(doc, o, "move_update_timer", g->u.recent_order_timer[m]);
	kp_json_set_int(doc, o, "user_flags", g->u.user_action[m]);
	kp_json_set_int(doc, o, "auto_target", ref(fx, g->u.auto_target[m]));
	kp_json_set_int(doc, o, "flingy_flags", g->u.movement_flags[m]);
	kp_json_set_int(doc, o, "sprite_flags", g->w->spr.flags[g->u.sprite[m]]);
	return o;
}

static KpJson *queue(const Fixture *fx)
{
	KpGame *g = fx->g;
	KpJsonDoc *doc = fx->doc;
	KpJson *out = kp_json_new(doc, KP_JSON_ARRAY);
	int64_t i;
	for (i = g->u.queue_head[fx->unit]; i != NONE; i = g->orders.next[i]) {
		KpJson *e = kp_json_push(doc, out, kp_json_new(doc, KP_JSON_OBJECT));
		kp_json_set_int(doc, e, "id", g->orders.order[i]);
		kp_json_set_int(doc, e, "fow", g->orders.type[i]);
		kp_json_set_int(doc, e, "position", word(g->orders.x[i]) + word(g->orders.y[i]) * 65536);
		kp_json_set_int(doc, e, "target", ref(fx, g->orders.unit[i]));
	}
	return out;
}

static KpJson *ids(const Fixture *fx)
{
	KpJson *out = kp_json_new(fx->doc, KP_JSON_ARRAY);
	int64_t i;
	for (i = fx->g->u.queue_head[fx->unit]; i != NONE; i = fx->g->orders.next[i]) kp_json_push_int(fx->doc, out, fx->g->orders.order[i]);
	return out;
}

static int queue_length(const Fixture *fx)
{
	int n = 0;
	int64_t i;
	for (i = fx->g->u.queue_head[fx->unit]; i != NONE; i = fx->g->orders.next[i]) n++;
	return n;
}

static KpJson *counts(const Fixture *fx)
{
	KpJson *out = kp_json_new(fx->doc, KP_JSON_ARRAY);
	int64_t free_count = 0, i;
	for (i = fx->g->orders.free.head; i != NONE; i = fx->g->orders.next[i]) free_count++;
	kp_json_push_int(fx->doc, out, free_count);
	kp_json_push_int(fx->doc, out, fx->g->orders.allocated);
	return out;
}

/* The probe's POOL + 20 k address of node i (0 for none). */
static int64_t address(KpT *T, const Fixture *fx, int64_t i)
{
	int k;
	if (i == NONE) return 0;
	for (k = 0; k < fx->pool_count; k++) {
		if (fx->pool[k] == i) return 0x808000 + k * 20;
	}
	kp_t_fail(T, "node outside the fixture pool");
}

static KpJson *free_chain(KpT *T, const Fixture *fx)
{
	KpJson *out = kp_json_new(fx->doc, KP_JSON_ARRAY);
	int64_t i;
	for (i = fx->g->orders.free.head; i != NONE; i = fx->g->orders.next[i]) kp_json_push_int(fx->doc, out, address(T, fx, i));
	return out;
}

static void enqueue(const Fixture *fx, const int64_t *orders, int n)
{
	int k;
	for (k = 0; k < n; k++) kp_order_insert_after(fx->g, fx->unit, orders[k], 0, 0, 0, NO_TYPE, NONE);
}

static int64_t target_of(const Fixture *fx, int64_t t) { return t == OTHER ? fx->other : t; }

/* An array of the values pushed by the caller. */
static KpJson *list(const Fixture *fx) { return kp_json_new(fx->doc, KP_JSON_ARRAY); }
static KpJson *add(const Fixture *fx, KpJson *a, KpJson *v) { return kp_json_push(fx->doc, a, v), a; }
static KpJson *add_int(const Fixture *fx, KpJson *a, int64_t v) { return kp_json_push_int(fx->doc, a, v), a; }

static int64_t in(const KpJson *c, const char *key, int64_t fallback) { return kp_json_field_int(kp_json_get(c, "inputs"), key, fallback); }

typedef KpJson *(*Run)(KpT *T, Fixture *fx, const KpJson *c);

static KpJson *issue_no_target(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	fixture_opts(&f);
	fixture(T, &f, fx);
	kp_t_dat_set(T, fx->og, "orders", "interruptable", in(c, "order", 0), in(c, "interruptible", 0));
	kp_issue_order(fx->g, fx->unit, in(c, "order", 0));
	r = list(fx);
	add(fx, r, state(fx, fx->unit));
	add(fx, r, queue(fx));
	add(fx, r, counts(fx));
	add_int(fx, r, fx->g->w->unit);
	return add_int(fx, r, fx->g->w->flingy);
}

static KpJson *stock_flags_issue_no_target(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	fixture_opts(&f);
	f.stock = 1;
	fixture(T, &f, fx);
	kp_issue_order(fx->g, fx->unit, in(c, "id", 0));
	r = list(fx);
	add(fx, r, state(fx, fx->unit));
	add(fx, r, queue(fx));
	return add(fx, r, counts(fx));
}

static KpJson *position(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	fixture_opts(&f);
	fixture(T, &f, fx);
	if (strcmp(kp_json_field_str(kp_json_get(c, "inputs"), "entry", ""), "0x42a340") == 0) {
		kp_issue_order_at(fx->g, fx->unit, 10, in(c, "x", 0), in(c, "y", 0));
	} else {
		kp_issue_order_fow(fx->g, fx->unit, 10, 65, in(c, "x", 0), in(c, "y", 0));
	}
	r = list(fx);
	add(fx, r, state(fx, fx->unit));
	add(fx, r, queue(fx));
	return add(fx, r, counts(fx));
}

static KpJson *target(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	int64_t t;
	fixture_opts(&f);
	fixture(T, &f, fx);
	t = target_of(fx, in(c, "target", 0));
	if (strcmp(kp_json_field_str(kp_json_get(c, "inputs"), "entry", ""), "0x42a380") == 0) {
		kp_issue_order_target(fx->g, fx->unit, 10, t);
	} else {
		kp_issue_order_packed(fx->g, fx->unit, 10, 123, 456, t);
	}
	r = list(fx);
	add(fx, r, state(fx, fx->unit));
	add(fx, r, queue(fx));
	return add(fx, r, counts(fx));
}

/* Allocation, freeing and reuse through the queue entries that allocate and free one node. */
static KpJson *allocator_payload_widths(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	int64_t packed = in(c, "packed_position", 0), node;
	fixture_opts(&f);
	f.pool_count = in(c, "pool_count", 16);
	fixture(T, &f, fx);
	kp_order_insert_after(fx->g, fx->unit, in(c, "cl_input", 0), packed % 65536, packed / 65536 % 65536, fx->other,
		in(c, "dx_input", 0), NONE);
	node = fx->g->u.queue_head[fx->unit];
	r = list(fx);
	add_int(fx, r, address(T, fx, node));
	add(fx, r, counts(fx));
	if (node != NONE) {
		const KpJson *e = kp_json_at(queue(fx), 0);
		add_int(fx, r, kp_json_field_int(e, "id", 0));
		add_int(fx, r, kp_json_field_int(e, "fow", 0));
		add_int(fx, r, kp_json_field_int(e, "position", 0));
		add_int(fx, r, kp_json_field_int(e, "target", 0));
	}
	return r;
}

static KpJson *allocator_free_roundtrip(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	fixture_opts(&f);
	f.pool_count = in(c, "pool_count", 16);
	fixture(T, &f, fx);
	kp_order_insert_after(fx->g, fx->unit, 0xbc, 0, 0, 0, NO_TYPE, NONE);
	kp_order_remove(fx->g, fx->unit, fx->g->u.queue_head[fx->unit]);
	r = list(fx);
	add(fx, r, counts(fx));
	return add(fx, r, free_chain(T, fx));
}

static KpJson *allocator_reuse_preserves_existing_head(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	fixture_opts(&f);
	f.pool_count = in(c, "pool_count", 16);
	fixture(T, &f, fx);
	kp_order_insert_after(fx->g, fx->unit, 0xbc, 0, 0, 0, NO_TYPE, NONE);
	kp_order_remove(fx->g, fx->unit, fx->g->u.queue_head[fx->unit]);
	kp_order_insert_after(fx->g, fx->unit, 10, 0, 0, 0, NO_TYPE, NONE);
	return kp_json_new_int(fx->doc, address(T, fx, fx->g->u.queue_head[fx->unit]));
}

static KpJson *activation_preserves_primary_timer_and_secondary_order(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	(void)c;
	fixture_opts(&f);
	fixture(T, &f, fx);
	fx->g->u.order_timer[fx->unit] = 33;
	fx->g->u.order_signal[fx->unit] = 6;
	kp_issue_order(fx->g, fx->unit, 10);
	r = list(fx);
	add_int(fx, r, fx->g->u.order_timer[fx->unit]);
	return add_int(fx, r, fx->g->u.order_signal[fx->unit]);
}

static KpJson *highlight_roundtrip(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	fixture_opts(&f);
	fixture(T, &f, fx);
	fx->g->u.queue_count[fx->unit] = in(c, "initial", 0);
	kp_t_dat_set(T, fx->og, "orders", "highlight_icon", 10, in(c, "highlight_descriptor", 0));
	kp_issue_order(fx->g, fx->unit, 10);
	return kp_json_new_int(fx->doc, fx->g->u.queue_count[fx->unit]);
}

static KpJson *activation_lock(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	fixture_opts(&f);
	f.status = in(c, "status", 1);
	fixture(T, &f, fx);
	kp_t_dat_set(T, fx->og, "orders", "interruptable", in(c, "order", 0), in(c, "interruptible", 0));
	kp_issue_order(fx->g, fx->unit, in(c, "order", 0));
	r = list(fx);
	add_int(fx, r, fx->g->u.order[fx->unit]);
	add_int(fx, r, fx->g->u.status[fx->unit]);
	add_int(fx, r, fx->g->u.user_action[fx->unit]);
	add_int(fx, r, queue_length(fx));
	return add(fx, r, counts(fx));
}

static KpJson *tail_pruning(KpT *T, Fixture *fx, const KpJson *c)
{
	const KpJson *i = kp_json_get(c, "inputs"), *q = kp_json_get(i, "queue"), *intr = kp_json_get(i, "interruptible");
	FixtureOpts f;
	KpJson *r;
	int64_t orders[16];
	int k, n = kp_json_len(q);
	fixture_opts(&f);
	fixture(T, &f, fx);
	for (k = 0; k < n && k < 16; k++) orders[k] = kp_json_int(kp_json_at(q, k), 0);
	enqueue(fx, orders, n);
	for (k = 0; k < n; k++) kp_t_dat_set(T, fx->og, "orders", "interruptable", orders[k], kp_json_int(kp_json_at(intr, k), 0));
	kp_order_append(fx->g, fx->unit, kp_json_field_int(i, "new_order", 0), 123, 456, 0, 65, kp_json_field_int(i, "clear_others", 0) != 0);
	r = list(fx);
	add(fx, r, ids(fx));
	add(fx, r, counts(fx));
	return add_int(fx, r, fx->g->u.order[fx->unit]);
}

static KpJson *clear_queue(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	int k;
	fixture_opts(&f);
	fixture(T, &f, fx);
	kp_t_dat_set(T, fx->og, "orders", "highlight_icon", 10, in(c, "highlight", 0));
	for (k = 0; k < in(c, "queue_size", 0); k++) kp_order_insert_after(fx->g, fx->unit, 10, 0, 0, 0, NO_TYPE, NONE);
	kp_order_clear_queue(fx->g, fx->unit);
	r = list(fx);
	add(fx, r, queue(fx));
	add(fx, r, counts(fx));
	return add_int(fx, r, fx->g->u.queue_count[fx->unit]);
}

/* The order, the queue, the counts and the user flags. */
static KpJson *order_queue_counts_flags(Fixture *fx)
{
	KpJson *r = list(fx);
	add_int(fx, r, fx->g->u.order[fx->unit]);
	add(fx, r, queue(fx));
	add(fx, r, counts(fx));
	return add_int(fx, r, fx->g->u.user_action[fx->unit]);
}

static KpJson *current_death_append_gate(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	fixture_opts(&f);
	f.current_order = in(c, "current_order", 3);
	fixture(T, &f, fx);
	kp_issue_order(fx->g, fx->unit, 10);
	return order_queue_counts_flags(fx);
}

static KpJson *pool_exhaustion(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	fixture_opts(&f);
	f.pool_count = in(c, "pool_count", 16);
	fixture(T, &f, fx);
	kp_issue_order(fx->g, fx->unit, 10);
	return order_queue_counts_flags(fx);
}

/* The local player's unit reports "queue full" (the probe stops at the notification 0x46ae70). */
static KpJson *local_pool_exhaustion_notification_prefix(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpEvent e;
	int reported = 0;
	int64_t player = -1;
	(void)c;
	fixture_opts(&f);
	f.pool_count = 0;
	fixture(T, &f, fx);
	kp_set_local_player(fx->g, 3);
	kp_issue_order(fx->g, fx->unit, 10);
	while (kp_event_take(fx->g, &e)) {
		if (e.kind == KP_EVENT_ERROR) {
			reported = 1;
			player = e.a;
		}
	}
	kp_t_ok(T, reported && player == 3, "the queue-full notification of the local player");
	return order_queue_counts_flags(fx);
}

static KpJson *retained_head_activation(KpT *T, Fixture *fx, const KpJson *c)
{
	static const int64_t TEN[1] = { 10 };
	FixtureOpts f;
	KpJson *r;
	fixture_opts(&f);
	f.pool_count = in(c, "pool_count", 16);
	fixture(T, &f, fx);
	enqueue(fx, TEN, 1);
	kp_t_dat_set(T, fx->og, "orders", "interruptable", 10, 0);
	kp_issue_order(fx->g, fx->unit, 13);
	r = list(fx);
	add_int(fx, r, fx->g->u.order[fx->unit]);
	add_int(fx, r, fx->g->u.status[fx->unit]);
	add(fx, r, ids(fx));
	return add(fx, r, counts(fx));
}

static KpJson *activate_after_released_lock(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	fixture_opts(&f);
	f.status = in(c, "previous_status", 1);
	fixture(T, &f, fx);
	kp_issue_order(fx->g, fx->unit, 10);
	fx->g->u.status[fx->unit] = 1;
	kp_order_activate(fx->g, fx->unit);
	r = list(fx);
	add(fx, r, state(fx, fx->unit));
	add(fx, r, queue(fx));
	return add(fx, r, counts(fx));
}

static KpJson *order_done_empty_queue(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	fixture_opts(&f);
	f.ai = kp_json_truthy(kp_json_get(kp_json_get(c, "inputs"), "ai"));
	f.status = in(c, "status", 1);
	fixture(T, &f, fx);
	kp_order_done(fx->g, fx->unit);
	r = list(fx);
	add_int(fx, r, fx->g->u.order[fx->unit]);
	add(fx, r, ids(fx));
	add(fx, r, counts(fx));
	return add_int(fx, r, fx->g->u.user_action[fx->unit]);
}

static KpJson *activate_empty_queue_is_noop(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	int64_t n;
	(void)c;
	fixture_opts(&f);
	fixture(T, &f, fx);
	kp_order_activate(fx->g, fx->unit);
	n = fx->unit;
	r = list(fx);
	add_int(fx, r, fx->g->u.order[n]);
	add_int(fx, r, fx->g->u.order_state[n]);
	add_int(fx, r, fx->g->u.queue_timer[n]);
	add_int(fx, r, fx->g->u.recent_order_timer[n]);
	add_int(fx, r, fx->g->u.user_action[n]);
	return add(fx, r, counts(fx));
}

static KpJson *cloak_pool(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	fixture_opts(&f);
	f.pool_count = in(c, "pool_count", 16);
	fixture(T, &f, fx);
	kp_t_dat_set(T, fx->og, "orders", "interruptable", CLOAK, in(c, "cloak_interruptible", 0));
	kp_issue_order(fx->g, fx->unit, CLOAK);
	r = list(fx);
	add_int(fx, r, fx->g->u.order[fx->unit]);
	add_int(fx, r, fx->g->u.status[fx->unit]);
	add(fx, r, ids(fx));
	return add(fx, r, counts(fx));
}

static KpJson *cloak_lock(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	fixture_opts(&f);
	f.status = in(c, "status", 1);
	fixture(T, &f, fx);
	kp_t_dat_set(T, fx->og, "orders", "interruptable", CLOAK, 0);
	kp_issue_order(fx->g, fx->unit, CLOAK);
	r = list(fx);
	add_int(fx, r, fx->g->u.order[fx->unit]);
	add_int(fx, r, fx->g->u.status[fx->unit]);
	return add(fx, r, ids(fx));
}

static KpJson *cloak_prepend_ai_limit(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	fixture_opts(&f);
	f.ai = kp_json_truthy(kp_json_get(kp_json_get(c, "inputs"), "ai"));
	fixture(T, &f, fx);
	fx->g->u.queue_count[fx->unit] = 9;
	kp_t_dat_set(T, fx->og, "orders", "interruptable", CLOAK, 0);
	kp_issue_order(fx->g, fx->unit, CLOAK);
	r = list(fx);
	add_int(fx, r, fx->g->u.order[fx->unit]);
	add(fx, r, ids(fx));
	return add(fx, r, counts(fx));
}

static KpJson *burrow(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	fixture_opts(&f);
	f.unit_id = in(c, "unit_id", 15);
	f.ai = kp_json_truthy(kp_json_get(kp_json_get(c, "inputs"), "ai"));
	f.status = in(c, "status", 1);
	fixture(T, &f, fx);
	kp_issue_order(fx->g, fx->unit, in(c, "new_order", 0));
	r = list(fx);
	add_int(fx, r, fx->g->u.order[fx->unit]);
	add(fx, r, ids(fx));
	add(fx, r, counts(fx));
	return add_int(fx, r, ref(fx, fx->g->u.auto_target[fx->unit]));
}

static KpJson *prepend_ai_highlight_limit(KpT *T, Fixture *fx, const KpJson *c)
{
	static const int64_t TEN[1] = { 10 };
	FixtureOpts f;
	KpJson *r;
	fixture_opts(&f);
	f.ai = kp_json_truthy(kp_json_get(kp_json_get(c, "inputs"), "ai"));
	fixture(T, &f, fx);
	enqueue(fx, TEN, 1);
	fx->g->u.queue_count[fx->unit] = in(c, "highlight_count", 0);
	kp_order_insert_before(fx->g, fx->unit, 11, 0, 0, 0, NO_TYPE, fx->g->u.queue_head[fx->unit]);
	r = list(fx);
	add(fx, r, ids(fx));
	return add(fx, r, counts(fx));
}

static KpJson *insert_middle(KpT *T, Fixture *fx, const KpJson *c)
{
	static const int64_t THREE[3] = { 10, 11, 12 };
	FixtureOpts f;
	KpJson *r;
	int64_t anchor;
	fixture_opts(&f);
	fixture(T, &f, fx);
	enqueue(fx, THREE, 3);
	anchor = fx->g->orders.next[fx->g->u.queue_head[fx->unit]];
	if (kp_json_truthy(kp_json_get(kp_json_get(c, "inputs"), "before"))) {
		kp_order_insert_before(fx->g, fx->unit, 13, 123, 456, fx->other, 65, anchor);
	} else {
		kp_order_insert_after(fx->g, fx->unit, 13, 123, 456, fx->other, 65, anchor);
	}
	r = list(fx);
	add(fx, r, ids(fx));
	return add(fx, r, counts(fx));
}

static KpJson *prepend_ai_limit_counts_highlights_not_nodes(KpT *T, Fixture *fx, const KpJson *c)
{
	static const int64_t TWELVE[12] = { 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10 };
	FixtureOpts f;
	KpJson *r;
	(void)c;
	fixture_opts(&f);
	f.ai = 1;
	fixture(T, &f, fx);
	enqueue(fx, TWELVE, 12);
	fx->g->u.queue_count[fx->unit] = 0;
	kp_order_insert_before(fx->g, fx->unit, 11, 0, 0, 0, NO_TYPE, fx->g->u.queue_head[fx->unit]);
	r = list(fx);
	add_int(fx, r, queue_length(fx));
	return add(fx, r, counts(fx));
}

static KpJson *subunit_mapping(KpT *T, Fixture *fx, const KpJson *c)
{
	int64_t order = in(c, "parent_order", 0);
	FixtureOpts f;
	KpJson *r;
	fixture_opts(&f);
	fixture(T, &f, fx);
	fx->g->u.subunit[fx->unit] = fx->sub;
	kp_t_dat_set(T, fx->og, "units", "special_ability_flags", 17, 0x10);
	kp_t_dat_set(T, fx->og, "units", "return_to_idle", 17, 4);
	kp_t_dat_set(T, fx->og, "units", "attack_unit", 17, 11);
	kp_t_dat_set(T, fx->og, "units", "attack_move", 17, 15);
	if (order == 34) kp_t_dat_set(T, fx->og, "orders", "changes_subunit_order", order, 1); /* inherit */
	kp_issue_order_at(fx->g, fx->unit, order, 123, 456);
	r = list(fx);
	add_int(fx, r, fx->g->u.order[fx->unit]);
	add_int(fx, r, fx->g->u.order[fx->sub]);
	add_int(fx, r, word(fx->g->u.order_x[fx->sub]));
	add_int(fx, r, word(fx->g->u.order_y[fx->sub]));
	return add(fx, r, counts(fx));
}

static KpJson *subunit_target_order_propagation(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	(void)c;
	fixture_opts(&f);
	fixture(T, &f, fx);
	fx->g->u.subunit[fx->unit] = fx->sub;
	kp_t_dat_set(T, fx->og, "units", "special_ability_flags", 17, 0x10);
	kp_t_dat_set(T, fx->og, "units", "attack_unit", 17, 11);
	kp_issue_order_target(fx->g, fx->unit, 10, fx->other);
	r = list(fx);
	add_int(fx, r, fx->g->u.order[fx->sub]);
	add_int(fx, r, ref(fx, fx->g->u.order_unit[fx->sub]));
	add_int(fx, r, word(fx->g->u.order_x[fx->sub]));
	add_int(fx, r, word(fx->g->u.order_y[fx->sub]));
	return add(fx, r, counts(fx));
}

/* Active main animations: the probe stops before the original's first iscript call (0x411730) and records the
 * animation it would play; here the first image play call stops the issuer the same way (a failure out of the world's
 * play probe hook). */
#define STOP "probe stop"

static int play_probe(KpImages *w, void *ctx, KpHook hook, int op, const int64_t *args, int argc, int64_t *out)
{
	Fixture *fx = (Fixture *)ctx;
	(void)hook;
	(void)op;
	(void)argc;
	(void)out;
	fx->played = args[1];
	kp_images_fail(w, STOP);
}

static KpJson *animation_boundary(KpT *T, Fixture *fx, const KpJson *c)
{
	FixtureOpts f;
	KpJson *r;
	KpTry t;
	KpImages *w;
	volatile int stopped = 0;
	int64_t n;
	fixture_opts(&f);
	f.animation = in(c, "main_animation", 0);
	fixture(T, &f, fx);
	w = fx->g->w;
	fx->g->probe_hook = play_probe;
	fx->g->probe_hook_ctx = fx;
	w->has_play_probe = 1;
	kp_images_begin(w, &t);
	if (setjmp(t.buf)) {
		stopped = 1;
	} else {
		kp_issue_order(fx->g, fx->unit, in(c, "order", 0));
	}
	kp_images_end(w, &t);
	w->has_play_probe = 0;
	fx->g->probe_hook = NULL;
	kp_t_ok(T, stopped && strcmp(w->error, STOP) == 0, "%s", stopped ? w->error : "no iscript call");
	w->error[0] = '\0';
	n = fx->unit;
	r = list(fx);
	add_int(fx, r, fx->g->u.order[n]);
	add_int(fx, r, fx->g->u.order_state[n]);
	add_int(fx, r, fx->g->u.queue_timer[n]);
	add_int(fx, r, fx->g->u.recent_order_timer[n]);
	add(fx, r, kp_json_new_bool(fx->doc, 0));
	return add_int(fx, r, fx->played);
}

static const struct {
	const char *family;
	Run fn;
} RUNS[] = {
	{ "issue_no_target", issue_no_target }, { "stock_flags_issue_no_target", stock_flags_issue_no_target },
	{ "position", position }, { "target", target }, { "allocator_payload_widths", allocator_payload_widths },
	{ "allocator_free_roundtrip", allocator_free_roundtrip },
	{ "allocator_reuse_preserves_existing_head", allocator_reuse_preserves_existing_head },
	{ "activation_preserves_primary_timer_and_secondary_order", activation_preserves_primary_timer_and_secondary_order },
	{ "highlight_roundtrip", highlight_roundtrip }, { "activation_lock", activation_lock }, { "tail_pruning", tail_pruning },
	{ "clear_queue", clear_queue }, { "current_death_append_gate", current_death_append_gate },
	{ "pool_exhaustion", pool_exhaustion },
	{ "local_pool_exhaustion_notification_prefix", local_pool_exhaustion_notification_prefix },
	{ "retained_head_activation", retained_head_activation }, { "activate_after_released_lock", activate_after_released_lock },
	{ "order_done_empty_queue", order_done_empty_queue }, { "activate_empty_queue_is_noop", activate_empty_queue_is_noop },
	{ "cloak_pool", cloak_pool }, { "cloak_lock", cloak_lock }, { "cloak_prepend_ai_limit", cloak_prepend_ai_limit },
	{ "burrow", burrow }, { "prepend_ai_highlight_limit", prepend_ai_highlight_limit }, { "insert_middle", insert_middle },
	{ "prepend_ai_limit_counts_highlights_not_nodes", prepend_ai_limit_counts_highlights_not_nodes },
	{ "subunit_mapping", subunit_mapping }, { "subunit_target_order_propagation", subunit_target_order_propagation },
	{ "animation_boundary", animation_boundary },
};
#define RUN_COUNT ((int)(sizeof RUNS / sizeof RUNS[0]))

/* Whether name is key or key's family member (key followed by "_"). */
static int of_family(const char *name, const char *key)
{
	size_t n = strlen(key);
	return strcmp(name, key) == 0 || (strncmp(name, key, n) == 0 && name[n] == '_');
}

/* The family of a case name (the probe's label prefix): the longest matching key. */
static int family(const char *name)
{
	int k, best = -1;
	for (k = 0; k < RUN_COUNT; k++) {
		if (of_family(name, RUNS[k].family) && (best < 0 || strlen(RUNS[k].family) > strlen(RUNS[best].family))) best = k;
	}
	return best;
}

typedef struct {
	const KpJson *c;
	Run fn;
	KpJsonDoc doc;
	KpJson *got;
} Case;

static void run_case(KpT *T, void *ud)
{
	Case *x = (Case *)ud;
	Fixture *fx = (Fixture *)calloc(1, sizeof *fx);
	if (!fx) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, fx);
	fx->doc = &x->doc;
	x->got = x->fn(T, fx, x->c);
}

/* "got X, original Y" of a differing case. */
static void difference(KpBuf *bad, const char *name, const KpJson *got, const KpJson *want)
{
	KpBuf a, b;
	kp_buf_init(&a);
	kp_buf_init(&b);
	kp_json_write(&a, got);
	kp_json_write(&b, want);
	kp_buf_printf(bad, "%s%s: got %s, original %s", bad->size ? "\n" : "", name, kp_buf_text(&a), kp_buf_text(&b));
	kp_buf_free(&a);
	kp_buf_free(&b);
}

static void order_functions_equal_the_original(KpT *T)
{
	const KpJson *cases;
	KpBuf *bad;
	int k, checked = 0, failures = 0;
	if (!kp_t_ready(T, "test_orders_native", FILES)) return;
	cases = kp_json_get(kp_t_golden(T, GOLDEN), "cases");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(cases) && failures < 5; k++) {
		const KpJson *c = kp_json_at(cases, k);
		const char *name = kp_json_field_str(c, "case", "");
		int f = family(name), mark = kp_t_mark(T);
		char failure[512];
		Case x;
		kp_t_ok(T, f >= 0, "no fixture for case %s", name);
		x.c = c;
		x.fn = RUNS[f].fn;
		x.got = NULL;
		kp_json_doc_init(&x.doc);
		checked++;
		if (kp_t_pcall(T, run_case, &x, failure, sizeof failure) != 0) {
			kp_buf_printf(bad, "%s%s: %s", bad->size ? "\n" : "", name, failure);
			failures++;
		} else if (!kp_json_equal(x.got, kp_json_get(c, "expected"))) {
			difference(bad, name, x.got, kp_json_get(c, "expected"));
			failures++;
		}
		kp_json_doc_free(&x.doc);
		kp_t_release(T, mark);
	}
	kp_t_ok(T, failures == 0, "%s", kp_buf_text(bad));
	kp_t_ok(T, checked > 0, "no case checked");
}

/* The special order consumers of phase 3 (orders_special.json; probe tools/research/probe_native_special_orders_117.py):
 * RescuePassive 0x48d120 with the rescuer search and a resource depot's whole owner list, and the trap's hiding and
 * empty retry 0x428490. The probe runs on a flat one-region map with no units; here the scenario map starts without its
 * units and every probe point moves by SHIFT onto its open ground (the cases depend only on the units' relative
 * positions). */
#define SHIFT_X 232
#define SHIFT_Y 332
#define UNIT_ADDRESS 0x5da3a0
#define UNIT_STRIDE 0x150
#define RESCUE_PASSIVE 0xa1
#define TRAP 0xa7
#define CREATED 16

typedef struct {
	KpGame *g;
	int64_t created[CREATED];
	int count;
	KpJsonDoc *doc;
} Special;

static KpGame *empty_game(KpT *T, const int64_t *controllers)
{
	KpGame *g = new_game(T, 1, 1)->g;
	int p;
	for (p = 0; p < 12; p++) g->players.controller[p] = controllers[p];
	return g;
}

static int64_t create(KpT *T, Special *s, int64_t t, int64_t x, int64_t y, int64_t owner)
{
	int64_t n = kp_create_map_unit(s->g, t, x + SHIFT_X, y + SHIFT_Y, owner);
	kp_t_ok(T, n != 0, "the unit is created");
	if (s->count < CREATED) s->created[s->count++] = n;
	return n;
}

/* A player's unit list as the probe's record addresses (creation order). */
static KpJson *chain(const Special *s, int64_t p)
{
	KpJson *out = kp_json_new(s->doc, KP_JSON_ARRAY);
	int64_t n;
	int k;
	for (n = s->g->player_units[p]; n != 0; n = s->g->u.player_next[n]) {
		for (k = 0; k < s->count; k++) {
			if (s->created[k] == n) kp_json_push_int(s->doc, out, UNIT_ADDRESS + k * UNIT_STRIDE);
		}
	}
	return out;
}

static KpJson *trap_initialize_empty_retry(KpT *T, Special *s, const KpJson *c)
{
	static const int64_t CONTROLLERS[12] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
	KpJsonDoc *doc = s->doc;
	KpJson *r;
	int64_t n;
	KpGame *g;
	s->g = g = empty_game(T, CONTROLLERS);
	n = create(T, s, in(c, "type", 0), 768, 768, 3);
	kp_t_ok(T, kp_order_has_handler(TRAP), "the trap's handler");
	kp_order_handler_run(g, TRAP, n);
	kp_order_handler_run(g, TRAP, n);
	r = kp_json_new(doc, KP_JSON_ARRAY);
	kp_json_push_int(doc, r, g->u.order_state[n]);
	kp_json_push_int(doc, r, g->u.order_timer[n]);
	kp_json_push_int(doc, r, g->u.status[n] & 0x200300);
	kp_json_push_int(doc, r, g->w->spr.elevation[g->u.sprite[n]]);
	kp_json_push_int(doc, r, g->u.visibility[n]);
	kp_json_push_int(doc, r, g->u.secondary_timer[n]);
	kp_json_push_int(doc, r, 0);
	return r;
}

static KpJson *rescue_human_selector(KpT *T, Special *s, const KpJson *c)
{
	const KpJson *i = kp_json_get(c, "inputs"), *xy = kp_json_get(i, "xy");
	int64_t controllers[12] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 }, rescued;
	KpJsonDoc *doc = s->doc;
	KpJson *r;
	KpGame *g;
	controllers[3] = kp_json_field_int(i, "rescue_controller", 0);
	controllers[0] = kp_json_field_int(i, "candidate_controller", 0);
	s->g = g = empty_game(T, controllers);
	rescued = create(T, s, 0, kp_json_int(kp_json_at(kp_json_at(xy, 0), 0), 0), kp_json_int(kp_json_at(kp_json_at(xy, 0), 1), 0),
		kp_json_field_int(i, "rescue_owner", 0));
	create(T, s, 0, kp_json_int(kp_json_at(kp_json_at(xy, 1), 0), 0), kp_json_int(kp_json_at(kp_json_at(xy, 1), 1), 0),
		kp_json_field_int(i, "candidate_owner", 0));
	g->u.order_timer[rescued] = kp_json_field_int(i, "timer", 0);
	kp_order_handler_run(g, RESCUE_PASSIVE, rescued);
	r = kp_json_new(doc, KP_JSON_ARRAY);
	kp_json_push_int(doc, r, g->u.owner[rescued]);
	kp_json_push_int(doc, r, g->u.order_timer[rescued]);
	kp_json_push_int(doc, r, g->u.order[rescued]);
	kp_json_push(doc, r, chain(s, 0));
	kp_json_push(doc, r, chain(s, 3));
	kp_json_push_int(doc, r, 0);
	kp_json_push_int(doc, r, g->w->spr.owner[g->u.sprite[rescued]]);
	kp_json_push_int(doc, r, g->count_all[0]);
	kp_json_push_int(doc, r, g->count_all[3]);
	kp_json_push_int(doc, r, g->count_completed[0]);
	kp_json_push_int(doc, r, g->count_completed[3]);
	return r;
}

static KpJson *rescue_completed_depot_whole_owner_list(KpT *T, Special *s, const KpJson *c)
{
	static const int64_t CONTROLLERS[12] = { 2, 2, 2, 3, 2, 2, 2, 2, 2, 2, 2, 2 };
	const KpJson *i = kp_json_get(c, "inputs");
	KpJsonDoc *doc = s->doc;
	KpJson *r;
	int64_t depot, other, old_owner = kp_json_field_int(i, "old_owner", 0);
	KpGame *g;
	s->g = g = empty_game(T, CONTROLLERS);
	depot = create(T, s, kp_json_field_int(i, "depot_type", 0), 768, 768, old_owner);
	other = create(T, s, kp_json_field_int(i, "other_type", 0), 1200, 1200, old_owner);
	create(T, s, kp_json_field_int(i, "human_type", 0), 828, 768, kp_json_field_int(i, "new_owner", 0));
	kp_order_handler_run(g, RESCUE_PASSIVE, depot);
	r = kp_json_new(doc, KP_JSON_ARRAY);
	kp_json_push_int(doc, r, g->u.owner[depot]);
	kp_json_push_int(doc, r, g->u.owner[other]);
	kp_json_push(doc, r, chain(s, 0));
	kp_json_push(doc, r, chain(s, 3));
	kp_json_push_int(doc, r, 0);
	return r;
}

typedef KpJson *(*SpecialRun)(KpT *T, Special *s, const KpJson *c);
static const struct {
	const char *family;
	SpecialRun fn;
} SPECIAL_RUNS[] = {
	{ "trap_initialize_empty_retry", trap_initialize_empty_retry }, { "rescue_human_selector", rescue_human_selector },
	{ "rescue_completed_depot_whole_owner_list", rescue_completed_depot_whole_owner_list },
};

typedef struct {
	const KpJson *c;
	SpecialRun fn;
	KpJsonDoc doc;
	KpJson *got;
} SpecialCase;

static void run_special(KpT *T, void *ud)
{
	SpecialCase *x = (SpecialCase *)ud;
	Special *s = (Special *)calloc(1, sizeof *s);
	if (!s) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, s);
	s->doc = &x->doc;
	x->got = x->fn(T, s, x->c);
}

static void special_order_consumers_equal_the_original(KpT *T)
{
	const KpJson *cases;
	KpBuf *bad;
	int k;
	if (!kp_t_ready(T, "test_orders_native", FILES)) return;
	cases = kp_json_get(kp_t_golden(T, SPECIAL), "cases");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(cases); k++) {
		const KpJson *c = kp_json_at(cases, k);
		const char *name = kp_json_field_str(c, "case", "");
		SpecialCase x;
		char failure[512];
		int f = -1, mark = kp_t_mark(T);
		size_t i;
		for (i = 0; i < sizeof SPECIAL_RUNS / sizeof SPECIAL_RUNS[0]; i++) {
			if (of_family(name, SPECIAL_RUNS[i].family)) f = (int)i;
		}
		kp_t_ok(T, f >= 0, "no fixture for case %s", name);
		x.c = c;
		x.fn = SPECIAL_RUNS[f].fn;
		x.got = NULL;
		kp_json_doc_init(&x.doc);
		if (kp_t_pcall(T, run_special, &x, failure, sizeof failure) != 0) {
			kp_buf_printf(bad, "%s%s: %s", bad->size ? "\n" : "", name, failure);
		} else if (!kp_json_equal(x.got, kp_json_get(c, "expected"))) {
			difference(bad, name, x.got, kp_json_get(c, "expected"));
		}
		kp_json_doc_free(&x.doc);
		kp_t_release(T, mark);
	}
	kp_t_ok(T, bad->size == 0, "%s", kp_buf_text(bad));
}

/* --- the orders on remembered units of the fog --- */

/* fog_right_click's map: a Marine of player 0 at 1000, 1100 and a Supply Depot of player 1 at 900, 1104. */
#define FOG_MAP "generated/scenarios/fog_right_click.kpm"
#define TYPE_MARINE 0
#define TYPE_SIEGE_TANK 5
#define TYPE_SUPPLY_DEPOT 109
#define TYPE_MISSILE_TURRET 124
#define STATUS_CAN_MOVE 0x20000
#define STATUS_BURROWED 0x10
#define RIGHT_CLICK 0x14
#define FOG_STEPS 400 /* enough for the Marine to walk back across the map's part */

typedef struct {
	KpOracleGame *og;
	KpGame *g;
	KpReplay empty;
	KpRunner run;
	int64_t step, marine, depot;
} FogGame;

static int64_t first_of_type(KpGame *g, int64_t t)
{
	int64_t n;
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = g->u.next[n]) {
		if (g->u.type[n] == t) return n;
	}
	return 0;
}

static void sprite_point(KpGame *g, int64_t n, int64_t *x, int64_t *y)
{
	int32_t s = kp_unit_sprite(g, n);
	*x = g->w->spr.x[s];
	*y = g->w->spr.y[s];
}

/* The map's game run past its first steps (the vision of the start). */
static int fog_game(KpT *T, const char *label, FogGame *f)
{
	static const char *const FOG_FILES[] = { KP_ORACLE_DATA "dat.kpd", FOG_MAP, NULL };
	if (!kp_t_ready(T, label, FOG_FILES)) return 0;
	f->og = kp_t_game(T, FOG_MAP, SEED, 0, NULL);
	f->g = f->og->g;
	kp_replay_init(&f->empty);
	kp_runner_init(&f->run, f->g, &f->empty);
	f->step = 4;
	kp_runner_to(&f->run, f->step);
	f->marine = first_of_type(f->g, TYPE_MARINE);
	f->depot = first_of_type(f->g, TYPE_SUPPLY_DEPOT);
	kp_t_ok(T, f->marine != 0 && f->depot != 0, "the map's Marine and Supply Depot");
	return f->marine != 0 && f->depot != 0;
}

/* Steps until unit n's order is no longer `order`; 0 when it still is after FOG_STEPS. */
static int fog_steps_until_not(FogGame *f, int64_t n, int64_t order)
{
	int64_t end = f->step + FOG_STEPS;
	while (f->step < end) {
		kp_runner_to(&f->run, ++f->step);
		if (f->g->u.order[n] != order) return 1;
	}
	return 0;
}

/* FindUnitOfTypeAtPoint 0x418210 and its callback 0x417f00: the point must lie in the type's placement box centred on
 * the unit's sprite, (unsigned)(x + w / 2 - px) < w and the same for y. */
static void fog_unit_takes_the_placement_box(KpT *T)
{
	FogGame f;
	int64_t x, y, w, h;
	if (!fog_game(T, "test_orders_native.fog_unit", &f)) return;
	sprite_point(f.g, f.depot, &x, &y);
	w = kp_t_dat(T, f.og, "units", "staredit_placement_size", TYPE_SUPPLY_DEPOT * 2);
	h = kp_t_dat(T, f.og, "units", "staredit_placement_size", TYPE_SUPPLY_DEPOT * 2 + 1);
	kp_t_ok(T, w > 1 && h > 1, "the depot's placement box %lld x %lld", (long long)w, (long long)h);
	kp_t_eq(T, kp_fog_unit(f.g, x, y, TYPE_SUPPLY_DEPOT), f.depot, "the centre");
	kp_t_eq(T, kp_fog_unit(f.g, x + w / 2, y + h / 2, TYPE_SUPPLY_DEPOT), f.depot, "the far corner (difference 0)");
	kp_t_eq(T, kp_fog_unit(f.g, x - w / 2 + 1, y - h / 2 + 1, TYPE_SUPPLY_DEPOT), f.depot, "the near corner (w - 1)");
	kp_t_eq(T, kp_fog_unit(f.g, x - w / 2, y, TYPE_SUPPLY_DEPOT), 0, "left of the box (difference w)");
	kp_t_eq(T, kp_fog_unit(f.g, x + w / 2 + 1, y, TYPE_SUPPLY_DEPOT), 0, "right of the box (difference -1)");
	kp_t_eq(T, kp_fog_unit(f.g, x, y - h / 2, TYPE_SUPPLY_DEPOT), 0, "above the box");
	kp_t_eq(T, kp_fog_unit(f.g, x, y + h / 2 + 1, TYPE_SUPPLY_DEPOT), 0, "below the box");
	kp_t_eq(T, kp_fog_unit(f.g, x, y, TYPE_MARINE), 0, "another type at the point");
}

/* ExecuteRightClickCommand 0x47f680: a right click on a remembered unit whose tile the player sees again is a click
 * on the unit that stands there (the Marine attacks the depot); it stopped the game before. */
static void right_click_on_a_ghost_in_view_targets_the_unit(KpT *T)
{
	FogGame f;
	int64_t x, y, ref;
	uint8_t cmd[10];
	if (!fog_game(T, "test_orders_native.right_click_ghost", &f)) return;
	sprite_point(f.g, f.depot, &x, &y);
	kp_t_ok(T, kp_point_visible_to(f.g, x, y, 0), "player 0 sees the depot's tile");
	ref = kp_unit_ref(f.g, f.marine);
	kp_command_select(f.g, 0, &ref, 1);
	cmd[0] = RIGHT_CLICK;
	kp_le_put16(cmd + 1, (uint32_t)x);
	kp_le_put16(cmd + 3, (uint32_t)y);
	kp_le_put16(cmd + 5, 0); /* no unit under the cursor */
	kp_le_put16(cmd + 7, TYPE_SUPPLY_DEPOT); /* the ghost's type */
	cmd[9] = 0;
	kp_execute_command(f.g, 0, cmd, sizeof cmd);
	kp_t_eq(T, f.g->u.order[f.marine], kp_t_dat(T, f.og, "units", "attack_unit", TYPE_MARINE), "the Marine's AttackUnit");
	kp_t_eq(T, f.g->u.order_unit[f.marine], f.depot, "on the depot");
}

/* OrderRightClickAction 0x431200 and ReissueRightClick 0x47f530 where nothing of the remembered type stands any more:
 * the click resolves to a move to the point (fog_right_click covers the attack against the original). */
static void right_click_action_on_an_empty_point_moves_there(KpT *T)
{
	FogGame f;
	int64_t x, y;
	if (!fog_game(T, "test_orders_native.right_click_action", &f)) return;
	sprite_point(f.g, f.marine, &x, &y);
	x += 64;
	kp_issue_order_fow(f.g, f.marine, KP_ORD_RIGHT_CLICK_ACTION, TYPE_SUPPLY_DEPOT, x, y);
	kp_t_ok(T, fog_steps_until_not(&f, f.marine, KP_ORD_RIGHT_CLICK_ACTION), "the order ends");
	kp_t_eq(T, f.g->u.order[f.marine], KP_ORD_MOVE, "a Move");
	kp_t_eq(T, f.g->u.order_x[f.marine], x, "to the point's x");
	kp_t_eq(T, f.g->u.order_y[f.marine], y, "to the point's y");
	kp_t_eq(T, f.g->u.order_unit[f.marine], 0, "without a target");
}

/* OrderAttackObscured 0x427c90 (order 9 here) once the owner sees the point: the unit's AttackUnit on the unit of the
 * remembered type, the turret's too; at a point where none stands the order only ends. Not observed in the original
 * (its targeting click found no ghost in fog_right_click's map); the behaviour is the atlas's (P09 0x427c90). */
static void attack_obscured_attacks_the_unit_once_in_view(KpT *T)
{
	FogGame f;
	int64_t x, y, tank, turret, far_x, far_y;
	if (!fog_game(T, "test_orders_native.attack_obscured", &f)) return;
	sprite_point(f.g, f.depot, &x, &y);
	kp_issue_order_fow(f.g, f.marine, KP_ORD_MOVE_TO_ATTACK, TYPE_SUPPLY_DEPOT, x, y);
	kp_t_ok(T, fog_steps_until_not(&f, f.marine, KP_ORD_MOVE_TO_ATTACK), "the Marine's order is replaced");
	kp_t_eq(T, f.g->u.order[f.marine], kp_t_dat(T, f.og, "units", "attack_unit", TYPE_MARINE), "the Marine's AttackUnit");
	kp_t_eq(T, f.g->u.order_unit[f.marine], f.depot, "on the depot");
	/* a unit with a turret: both attack */
	tank = kp_create_map_unit(f.g, TYPE_SIEGE_TANK, 1000, 1180, 0);
	kp_t_ok(T, tank != 0, "a Siege Tank");
	turret = f.g->u.subunit[tank];
	kp_t_ok(T, kp_is_subunit(f.g, turret), "with its turret");
	kp_issue_order_fow(f.g, tank, KP_ORD_MOVE_TO_ATTACK, TYPE_SUPPLY_DEPOT, x, y);
	kp_t_ok(T, fog_steps_until_not(&f, tank, KP_ORD_MOVE_TO_ATTACK), "the tank's order is replaced");
	kp_t_eq(T, f.g->u.order[tank], kp_t_dat(T, f.og, "units", "attack_unit", TYPE_SIEGE_TANK), "the tank's AttackUnit");
	kp_t_eq(T, f.g->u.order_unit[tank], f.depot, "the tank on the depot");
	kp_t_eq(T, f.g->u.order[turret], kp_t_dat(T, f.og, "units", "attack_unit", f.g->u.type[turret]), "the turret's AttackUnit");
	kp_t_eq(T, f.g->u.order_unit[turret], f.depot, "the turret on the depot");
	/* a point out of sight with nothing there: the Marine walks until it sees the point, then the order ends */
	sprite_point(f.g, f.marine, &far_x, &far_y);
	far_x += 480;
	kp_t_ok(T, !kp_point_visible_to(f.g, far_x, far_y, 0), "the far point is out of sight");
	kp_issue_order_fow(f.g, f.marine, KP_ORD_MOVE_TO_ATTACK, TYPE_SUPPLY_DEPOT, far_x, far_y);
	kp_t_ok(T, fog_steps_until_not(&f, f.marine, KP_ORD_MOVE_TO_ATTACK), "the order ends");
	kp_t_ok(T, kp_point_visible_to(f.g, far_x, far_y, 0), "only once the point is in sight");
	kp_t_eq(T, f.g->u.order_unit[f.marine], 0, "without a target");
	kp_t_ok(T, f.g->u.order[f.marine] != kp_t_dat(T, f.og, "units", "attack_unit", TYPE_MARINE), "no attack");
}

/* OrderAttackObscured 0x427c90 for a unit that cannot move (status 0x20000 clear): it waits for the point without a
 * move request, then attacks the unit of the remembered type there. */
static void attack_obscured_by_a_unit_that_cannot_move(KpT *T)
{
	FogGame f;
	int64_t x, y, tower, move_x, move_y;
	if (!fog_game(T, "test_orders_native.attack_obscured_static", &f)) return;
	sprite_point(f.g, f.depot, &x, &y);
	tower = kp_create_map_unit(f.g, TYPE_MISSILE_TURRET, 1104, 1040, 0);
	kp_t_ok(T, tower != 0, "a Missile Turret");
	if (tower == 0) return;
	kp_t_ok(T, (f.g->u.status[tower] & STATUS_CAN_MOVE) == 0, "that cannot move");
	move_x = f.g->u.move_x[tower];
	move_y = f.g->u.move_y[tower];
	kp_issue_order_fow(f.g, tower, KP_ORD_MOVE_TO_ATTACK, TYPE_SUPPLY_DEPOT, x, y);
	kp_t_ok(T, fog_steps_until_not(&f, tower, KP_ORD_MOVE_TO_ATTACK), "the turret's order is replaced");
	kp_t_eq(T, f.g->u.move_x[tower], move_x, "no move requested (x)");
	kp_t_eq(T, f.g->u.move_y[tower], move_y, "no move requested (y)");
	kp_t_eq(T, f.g->u.order[tower], kp_t_dat(T, f.og, "units", "attack_unit", TYPE_MISSILE_TURRET), "its AttackUnit");
	kp_t_eq(T, f.g->u.order_unit[tower], f.depot, "on the depot");
}

#define LURKER_MAP "generated/scenarios/burrowed_lurker_morph.kpm"
#define BURROW 0x2c
#define UNIT_MORPH 0x23
#define BURROW_STEPS 200

/* Steps of run until unit n's order is `order` (or its type `type`); 0 when it still is not after BURROW_STEPS. */
static int steps_until(KpRunner *run, int64_t *step, KpGame *g, int64_t n, int64_t order, int64_t type)
{
	int64_t end = *step + BURROW_STEPS;
	while (*step < end) {
		kp_runner_to(run, ++*step);
		if (g->u.order[n] == order || g->u.type[n] == type) return 1;
	}
	return 0;
}

/* Order_Burrowed 0x43c810 with a morph at the queue's head (ExecUnitMorph 0x472a50 does not test the burrow): the
 * burrowed Hydralisk unburrows with the morph queued after it, then morphs, while a standing one selected with it
 * morphs at once; the game stopped there before. The scenarios burrowed_lurker_morph (alone) and
 * burrowed_lurker_mixed (with a standing one) cover it against the original. */
static void burrowed_hydralisk_unburrows_to_morph(KpT *T)
{
	static const char *const LURKER_FILES[] = { KP_ORACLE_DATA "dat.kpd", LURKER_MAP, NULL };
	KpGame *g;
	KpReplay empty;
	KpRunner run;
	int64_t step = 4, hydra, standing, x, y, refs[2], head;
	uint8_t cmd[3];
	if (!kp_t_ready(T, "test_orders_native.burrowed_morph", LURKER_FILES)) return;
	g = kp_t_game(T, LURKER_MAP, SEED, 1, NULL)->g; /* Brood War: Lurker Aspect's ff24 */
	kp_replay_init(&empty);
	kp_runner_init(&run, g, &empty);
	kp_runner_to(&run, step);
	hydra = first_of_type(g, KP_TYPE_HYDRALISK);
	kp_t_ok(T, hydra != 0, "the map's Hydralisk");
	if (hydra == 0) return;
	refs[0] = kp_unit_ref(g, hydra);
	kp_command_select(g, 0, refs, 1);
	cmd[0] = BURROW;
	cmd[1] = 0;
	kp_execute_command(g, 0, cmd, 2);
	kp_t_ok(T, steps_until(&run, &step, g, hydra, KP_ORD_BURROWED, KP_TYPE_NONE), "the Hydralisk burrows");
	sprite_point(g, hydra, &x, &y);
	standing = kp_create_map_unit(g, KP_TYPE_HYDRALISK, x + 100, y - 40, 0);
	kp_t_ok(T, standing != 0, "a standing Hydralisk");
	if (standing == 0) return;
	refs[1] = kp_unit_ref(g, standing);
	kp_command_select(g, 0, refs, 2); /* the burrowed one first, as burrowed_lurker_mixed selects them */
	cmd[0] = UNIT_MORPH;
	kp_le_put16(cmd + 1, KP_TYPE_LURKER);
	kp_execute_command(g, 0, cmd, 3);
	head = g->u.queue_head[hydra];
	kp_t_eq(T, g->u.order[hydra], KP_ORD_BURROWED, "the morph waits behind Burrowed");
	kp_t_ok(T, head != KP_NONE && g->orders.order[head] == KP_ORD_ZERG_UNIT_MORPH, "at the queue's head");
	kp_t_eq(T, g->u.order[standing], KP_ORD_ZERG_UNIT_MORPH, "the standing one morphs at once");
	kp_runner_to(&run, ++step);
	head = g->u.queue_head[hydra];
	kp_t_eq(T, g->u.order[hydra], KP_ORD_UNBURROWING, "the Hydralisk unburrows");
	kp_t_ok(T, head != KP_NONE && g->orders.order[head] == KP_ORD_ZERG_UNIT_MORPH && g->orders.next[head] == KP_NONE,
		"with the morph alone after it");
	kp_t_ok(T, steps_until(&run, &step, g, hydra, KP_ORD_NONE, KP_TYPE_LURKER_EGG), "then becomes a Lurker Egg");
	kp_t_eq(T, g->u.status[hydra] & STATUS_BURROWED, 0, "unburrowed");
	kp_t_eq(T, g->u.type[standing], KP_TYPE_LURKER_EGG, "the standing one is a Lurker Egg too");
}

#define MINIMAP_PING 0x58
#define PING_OWN 17 /* ExecMinimapPing 0x472a00: 0x11 + (the command's player 0x508d44 != the local one 0x508d50) */
#define PING_ALLY 18

/* The minimap pings a ping command of player p at (x, y) shows: their count, the last one in *out. */
static int ping_events(KpGame *g, int64_t p, int64_t x, int64_t y, KpEvent *out)
{
	uint8_t cmd[5];
	KpEvent e;
	int n = 0;
	while (kp_event_take(g, &e)) {
	}
	cmd[0] = MINIMAP_PING;
	kp_le_put16(cmd + 1, (uint32_t)x);
	kp_le_put16(cmd + 3, (uint32_t)y);
	kp_execute_command(g, p, cmd, sizeof cmd);
	while (kp_event_take(g, &e)) {
		if (e.kind != KP_EVENT_MINIMAP_PING) continue;
		*out = e;
		n++;
	}
	return n;
}

/* ExecMinimapPing 0x472a00 (command 0x58, Alt and a click on the minimap): a ping for the local player when the
 * command's player is the local one (colour 17) or allied towards it (18), none for an enemy's or outside the map; the
 * command stopped the game before. Not observed in the original (a scenario's inputs have no Alt-click). */
static void minimap_ping_shows_the_local_players_and_allies(KpT *T)
{
	KpGame *g;
	KpEvent e;
	int64_t local, other;
	if (!kp_t_ready(T, "test_orders_native.minimap_ping", FILES)) return;
	g = new_game(T, 0, 0)->g;
	local = g->local_player;
	other = local == 0 ? 1 : 0;
	kp_t_eq(T, ping_events(g, local, 100, 200, &e), 1, "the local player's ping");
	kp_t_eq(T, e.a, 100, "at its x");
	kp_t_eq(T, e.b, 200, "at its y");
	kp_t_eq(T, e.c, PING_OWN, "in the local player's colour");
	kp_t_eq(T, ping_events(g, local, g->map_w_px, 200, &e), 0, "none past the map's width");
	kp_t_eq(T, ping_events(g, local, 100, g->map_h_px, &e), 0, "none past its height");
	kp_set_alliance(g, other, local, 0);
	kp_set_alliance(g, local, other, 1);
	kp_t_eq(T, ping_events(g, other, 100, 200, &e), 0, "none from a player not allied towards the local one");
	kp_set_alliance(g, other, local, 1);
	kp_t_eq(T, ping_events(g, other, 300, 400, &e), 1, "an ally's ping");
	kp_t_eq(T, e.c, PING_ALLY, "in the ally's colour");
}

static const KpTest TESTS[] = {
	{ "order_functions_equal_the_original", order_functions_equal_the_original, 0 },
	{ "special_order_consumers_equal_the_original", special_order_consumers_equal_the_original, 0 },
	{ "fog_unit_takes_the_placement_box", fog_unit_takes_the_placement_box, 0 },
	{ "right_click_on_a_ghost_in_view_targets_the_unit", right_click_on_a_ghost_in_view_targets_the_unit, 0 },
	{ "right_click_action_on_an_empty_point_moves_there", right_click_action_on_an_empty_point_moves_there, 0 },
	{ "attack_obscured_attacks_the_unit_once_in_view", attack_obscured_attacks_the_unit_once_in_view, 0 },
	{ "attack_obscured_by_a_unit_that_cannot_move", attack_obscured_by_a_unit_that_cannot_move, 0 },
	{ "minimap_ping_shows_the_local_players_and_allies", minimap_ping_shows_the_local_players_and_allies, 0 },
	{ "burrowed_hydralisk_unburrows_to_morph", burrowed_hydralisk_unburrows_to_morph, 0 },
};
KP_SUITE(kp_suite_orders_native, "test_orders_native", TESTS);
