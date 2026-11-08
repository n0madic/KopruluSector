/*
 * The scripts' parse and run (native/kp_app/kp_script.c, koprulu --script) over a recording host: every command's
 * parse and the lines the parse refuses, includes, the frames' rules (an input ends the frame's steps, " &" shares it,
 * a wait's steps run in the frame it holds in), timeouts, until's repeated input, marks, expect's abort and check's count.
 */
#include "kp_test.h"

#include "kp_script.h"

#define NEVER ((int64_t)1 << 40)
#define EVENTS 64
#define LINES 32
#define LINE_SIZE 512

typedef struct {
	int64_t frame, now, step_ms;
	int64_t values[8];
	int missing[8];        /* value k has none */
	int64_t cond_from[8];  /* the frame from which named condition k holds */
	int check_fails;       /* the checks fail */
	int fail_op;           /* an op the host cannot run, -1 none */
	int64_t key_adds_gamma; /* a key's effect on the value gamma */
	const char *include_names[8], *include_texts[8];
	int event_count, line_count;
	int aborted_at;        /* the lines logged when the host heard of the run's abort, -1 never */
	char events[EVENTS][96];
	char lines[LINES][LINE_SIZE];
} Rec;

static const struct {
	KpScriptKind kind;
	const char *word;
	int32_t id;
	KpScriptArg arg;
	KpScriptKind arg_kind;
} NAMES[] = {
	{ KP_SCRIPT_NAME_KEY, "Escape", 1, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_KEY, "Return", 2, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_KEY, "F10", 3, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_KEY, "A", 4, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_VALUE, "tick", 0, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_VALUE, "gamma", 1, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_VALUE, "units", 2, KP_SCRIPT_ARG_INT, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_VALUE, "moved", 3, KP_SCRIPT_ARG_PICK, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_VALUE, "camera.x", 4, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_COND, "game", 0, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_COND, "dialog", 1, KP_SCRIPT_ARG_OPT_NAME, KP_SCRIPT_NAME_DIALOG },
	{ KP_SCRIPT_NAME_COND, "screen", 2, KP_SCRIPT_ARG_NAME, KP_SCRIPT_NAME_SCREEN },
	{ KP_SCRIPT_NAME_COND, "nodialog", 3, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_CONTROL, "gamma", 10, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_CONTROL, "ok", 11, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_CHECK, "terrain", 0, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_CHECK, "save-file", 1, KP_SCRIPT_ARG_TEXT, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_CHOICE, "graphics.hd2", 5, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_MENU, "game", 1, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_OPTIONS, "video", 2, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_UNIT, "worker", 0, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_UNIT, "group", 1, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_DIALOG, "GameMenu", 1, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_DIALOG, "Options", 2, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_DIALOG, "quit", 3, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY }, /* a dialog named as a command */
	{ KP_SCRIPT_NAME_DIALOG, "wait", 4, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_SCREEN, "login", 5, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
	{ KP_SCRIPT_NAME_SCREEN, "main", 0, KP_SCRIPT_ARG_NONE, KP_SCRIPT_NAME_KEY },
};

static const char *const OP_NAMES[] = { "key", "text", "move", "click", "rclick", "press", "release", "finger", "tap",
	"touch", "choose", "pause", "resume", "camera", "world", "fog", "save", "load", "load-file", "menu", "options-dialog",
	"activate", "select", "end-placement", "cancel-targeting", "audit", "hang", "wait-frames", "wait-time", "wait",
	"until", "shot", "state", "ui", "log", "mark", "pick", "expect", "check", "quit" };
typedef char op_names_cover_the_ops[sizeof OP_NAMES / sizeof OP_NAMES[0] == KP_SCRIPT_OPS ? 1 : -1];

KP_T_PRINTF(2, 3) static void record(Rec *R, const char *fmt, ...)
{
	va_list ap;
	if (R->event_count == EVENTS) return;
	va_start(ap, fmt);
	vsnprintf(R->events[R->event_count++], sizeof R->events[0], fmt, ap);
	va_end(ap);
}

static int t_name(void *ud, KpScriptKind kind, const char *word, KpScriptName *out)
{
	size_t k;
	(void)ud;
	for (k = 0; k < sizeof NAMES / sizeof NAMES[0]; k++) {
		if (NAMES[k].kind == kind && strcmp(NAMES[k].word, word) == 0) {
			out->id = NAMES[k].id;
			out->arg = NAMES[k].arg;
			out->arg_kind = NAMES[k].arg_kind;
			return 0;
		}
	}
	return -1;
}

static char *t_read(void *ud, const char *name, size_t *size)
{
	Rec *R = (Rec *)ud;
	int k;
	for (k = 0; k < 8 && R->include_names[k]; k++) {
		if (strcmp(R->include_names[k], name) == 0) {
			size_t n = strlen(R->include_texts[k]);
			char *copy = (char *)malloc(n + 1);
			if (!copy) return NULL;
			memcpy(copy, R->include_texts[k], n + 1);
			*size = n;
			return copy;
		}
	}
	return NULL;
}

static int t_value(void *ud, const KpScriptRun *run, const KpScriptRef *ref, int64_t *out, char *why, size_t size)
{
	Rec *R = (Rec *)ud;
	if (ref->id < 0 || ref->id >= 8 || R->missing[ref->id]) {
		snprintf(why, size, "no game");
		return -1;
	}
	*out = R->values[ref->id];
	if (ref->id == 2) *out += ref->arg * 1000;      /* units:N */
	if (ref->id == 3) *out += run->picks[ref->arg]; /* moved:PICK */
	return 0;
}

static int t_cond(void *ud, const KpScriptRun *run, const KpScriptRef *ref, char *why, size_t size)
{
	Rec *R = (Rec *)ud;
	(void)run;
	if (R->frame >= R->cond_from[ref->id]) return 1;
	snprintf(why, size, "condition %d not yet", (int)ref->id);
	return 0;
}

static int t_exec(void *ud, KpScriptRun *run, const KpScriptStep *st, char *why, size_t size)
{
	Rec *R = (Rec *)ud;
	if ((int)st->op == R->fail_op) {
		snprintf(why, size, "no game");
		return -1;
	}
	if (st->op == KP_SCRIPT_PICK) run->picks[st->id] = 42;
	if (st->op == KP_SCRIPT_KEY) R->values[1] += R->key_adds_gamma;
	record(R, "%lld %s %d%s%s", (long long)R->frame, OP_NAMES[st->op], (int)st->id, st->text[0] ? " " : "", st->text);
	if (st->op == KP_SCRIPT_CHECK && R->check_fails) {
		snprintf(why, size, "3 mismatches");
		return KP_SCRIPT_CHECK_FAILED;
	}
	return 0;
}

static void t_log(void *ud, const char *line)
{
	Rec *R = (Rec *)ud;
	if (R->line_count < LINES) snprintf(R->lines[R->line_count++], LINE_SIZE, "%s", line);
}

static void t_aborted(void *ud, const KpScriptRun *run)
{
	Rec *R = (Rec *)ud;
	(void)run;
	R->aborted_at = R->line_count;
}

static KpScriptHost host(Rec *R)
{
	KpScriptHost h;
	memset(&h, 0, sizeof h);
	h.ud = R;
	h.name = t_name;
	h.read = t_read;
	h.value = t_value;
	h.cond = t_cond;
	h.exec = t_exec;
	h.log = t_log;
	h.aborted = t_aborted;
	return h;
}

static void rec_init(Rec *R)
{
	int k;
	memset(R, 0, sizeof *R);
	R->step_ms = 16;
	R->fail_op = -1;
	R->aborted_at = -1;
	for (k = 0; k < 8; k++) R->cond_from[k] = NEVER;
}

static void free_script(void *p)
{
	kp_script_free((KpScript *)p);
	free(p);
}

/* The script of text (tests/scripts/t.kps), released when the test ends. */
static KpScript *parse_ok(KpT *T, Rec *R, const char *text)
{
	KpScriptHost h = host(R);
	KpError err;
	KpScript *s = (KpScript *)calloc(1, sizeof *s);
	if (!s) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_script, s); /* a failed parse leaves s empty: freeing it is safe */
	if (kp_script_parse(s, "tests/scripts/t.kps", text, strlen(text), &h, &err) != 0) kp_t_fail(T, "%s", err.msg);
	return s;
}

/* Runs at most n frames, the clock step_ms a frame; the frames run (the last the one that ended the run). */
static int run_frames(KpScriptRun *run, Rec *R, int n)
{
	KpScriptHost h = host(R);
	int k;
	for (k = 0; k < n; k++) {
		R->frame++;
		R->now += R->step_ms;
		if (!kp_script_frame(run, &h, R->now)) return k + 1;
	}
	return n;
}

static void events_are(KpT *T, const Rec *R, const char *const *want, int count)
{
	int k;
	kp_t_eq(T, R->event_count, count, "the host's events");
	for (k = 0; k < count; k++) kp_t_eq_str(T, R->events[k], want[k], "event %d", k);
}

static void lines_are(KpT *T, const Rec *R, const char *const *want, int count)
{
	int k;
	kp_t_eq(T, R->line_count, count, "the log's lines");
	for (k = 0; k < count; k++) kp_t_eq_str(T, R->lines[k], want[k], "line %d", k);
}

static void parse_reads_the_commands(KpT *T)
{
	static const char TEXT[] =
		"mission zerg/zerg01\n"
		"video originals\n"
		"clock real\n"
		"timeout 5\n"
		"watchdog 2.5\n"
		"# a comment\n"
		"key F10 mod ctrl+shift text \"\xd1\x84\"\n"
		"key A notext  # the key alone\n"
		"text \"there is no cow level\"\n"
		"move 10 20\n"
		"click world 300 400\n"
		"rclick control gamma right\n"
		"press view 60% 70%\n"
		"release center -5 6\n"
		"pick u\n"
		"tap 1 unit u 3 4\n"
		"finger down 2 beside u 96 &\n"
		"finger up 2 pad toggle\n"
		"touch on\n"
		"choose graphics.hd2\n"
		"camera 100 200\n"
		"camera by 120 0\n"
		"camera max\n"
		"world off\n"
		"fog on\n"
		"save \"koprulu save check\"\n"
		"load-file \"koprulu \\\"save\\\" check\"\n"
		"menu game\n"
		"options-dialog video\n"
		"activate ok\n"
		"select group 48 optional\n"
		"end-placement\n"
		"cancel-targeting\n"
		"audit hd\n"
		"hang 0.5\n"
		"wait frames 3\n"
		"wait seconds 0.25\n"
		"mark start\n"
		"wait dialog GameMenu and tick >= @start.tick + 10 within 2000\n"
		"until nodialog every 0.2 key Escape\n"
		"shot start\n"
		"state start\n"
		"ui start\n"
		"log start gamma units:3 moved:u\n"
		"expect gamma == @start.gamma +40\n"
		"check save-file \"koprulu save check\"\n"
		"check terrain\n"
		"quit\n";
	KpScript *s;
	Rec R;
	const KpScriptStep *st;
	rec_init(&R);
	s = parse_ok(T, &R, TEXT);
	kp_t_eq_str(T, s->name, "t", "the script's name");
	kp_t_eq_str(T, s->files[0], "t.kps", "its file");
	kp_t_eq_str(T, s->mission, "zerg/zerg01", "the mission");
	kp_t_ok(T, s->video_originals && s->options_originals && s->clock_real, "video originals (with the options'), a real clock");
	kp_t_eq(T, s->timeout_ms, 5000, "the timeout");
	kp_t_eq(T, s->watchdog_ms, 2500, "the watchdog");
	kp_t_eq(T, s->count, 43, "the steps");
	st = s->steps;
	kp_t_ok(T, st[0].op == KP_SCRIPT_KEY && st[0].id == 3 && st[0].mods == (KP_SCRIPT_MOD_CTRL | KP_SCRIPT_MOD_SHIFT)
		&& st[0].flag == KP_SCRIPT_KEY_TEXT && strcmp(st[0].text, "\xd1\x84") == 0 && st[0].line == 7, "key with mod and text");
	kp_t_ok(T, st[1].flag == KP_SCRIPT_KEY_NOTEXT && st[1].id == 4, "key notext, a comment after it");
	kp_t_eq_str(T, st[2].text, "there is no cow level", "text");
	kp_t_ok(T, st[3].p.kind == KP_SCRIPT_AT_XY && st[3].p.x == 10 && st[3].p.y == 20, "a point X Y");
	kp_t_ok(T, st[4].op == KP_SCRIPT_CLICK && st[4].p.kind == KP_SCRIPT_AT_WORLD && st[4].p.x == 300 && st[4].p.y == 400, "world");
	kp_t_ok(T, st[5].p.kind == KP_SCRIPT_AT_CONTROL && st[5].p.name == 10 && st[5].p.flag == 1, "control's right end");
	kp_t_ok(T, st[6].p.kind == KP_SCRIPT_AT_VIEW && st[6].p.x == 60 && st[6].p.y == 70, "view percents");
	kp_t_ok(T, st[7].p.kind == KP_SCRIPT_AT_CENTER && st[7].p.x == -5 && st[7].p.y == 6, "center with an offset");
	kp_t_ok(T, st[8].op == KP_SCRIPT_PICK && st[8].id == 0 && s->pick_count == 1, "pick");
	kp_t_ok(T, st[9].op == KP_SCRIPT_TAP && st[9].id == 1 && st[9].p.kind == KP_SCRIPT_AT_UNIT && st[9].p.name == 0
		&& st[9].p.x == 3 && st[9].p.y == 4, "tap a pick's unit with an offset");
	kp_t_ok(T, st[10].flag == KP_SCRIPT_FINGER_DOWN && st[10].p.kind == KP_SCRIPT_AT_BESIDE && st[10].p.x == 96
		&& st[10].same_frame, "finger down beside, & sharing the frame");
	kp_t_ok(T, st[11].flag == KP_SCRIPT_FINGER_UP && st[11].p.kind == KP_SCRIPT_AT_PAD_TOGGLE && !st[11].same_frame, "pad toggle");
	kp_t_ok(T, st[12].op == KP_SCRIPT_TOUCH && st[13].op == KP_SCRIPT_CHOOSE && st[13].id == 5, "touch on, choose");
	kp_t_ok(T, st[14].flag == KP_SCRIPT_CAMERA_AT && st[14].p.x == 100 && st[14].p.y == 200, "camera X Y");
	kp_t_ok(T, st[15].flag == KP_SCRIPT_CAMERA_BY && st[15].p.x == 120 && st[16].flag == KP_SCRIPT_CAMERA_MAX, "camera by, max");
	kp_t_ok(T, st[17].op == KP_SCRIPT_WORLD && !st[17].flag && st[18].op == KP_SCRIPT_FOG && st[18].flag, "world off, fog on");
	kp_t_eq_str(T, st[19].text, "koprulu save check", "save's name");
	kp_t_eq_str(T, st[20].text, "koprulu \"save\" check", "a string's escapes");
	kp_t_ok(T, st[21].id == 1 && st[22].id == 2 && st[23].id == 11, "menu, options-dialog, activate");
	kp_t_ok(T, st[24].op == KP_SCRIPT_SELECT && st[24].id == 1 && st[24].n == 48 && st[24].flag, "select group 48 optional");
	kp_t_ok(T, st[27].op == KP_SCRIPT_AUDIT && st[28].op == KP_SCRIPT_HANG && st[28].n == 500, "audit hd, hang");
	kp_t_ok(T, st[29].op == KP_SCRIPT_WAIT_FRAMES && st[29].n == 3, "wait frames");
	kp_t_ok(T, st[30].op == KP_SCRIPT_WAIT_TIME && st[30].n == 250, "wait seconds");
	kp_t_ok(T, st[31].op == KP_SCRIPT_MARK && st[31].id == 0, "mark");
	st = &s->steps[32];
	kp_t_ok(T, st->op == KP_SCRIPT_WAIT && st->cond.count == 2 && st->cond.terms[0].named && st->cond.terms[0].cond.id == 1
		&& st->cond.terms[0].cond.arg == 1, "wait dialog GameMenu");
	kp_t_ok(T, !st->cond.terms[1].named && st->cond.terms[1].a.kind == KP_SCRIPT_EXPR_VALUE && st->cond.terms[1].a.value.id == 0
		&& st->cond.terms[1].op == KP_SCRIPT_GE && st->cond.terms[1].b.kind == KP_SCRIPT_EXPR_MARK && st->cond.terms[1].b.mark == 0
		&& st->cond.terms[1].b.slot == 0 && st->cond.terms[1].b.k == 10, "and tick >= @start.tick + 10");
	kp_t_ok(T, st->has_within && st->within.kind == KP_SCRIPT_EXPR_NUMBER && st->within.k == 2000, "within 2000");
	st = &s->steps[33];
	kp_t_ok(T, st->op == KP_SCRIPT_UNTIL && st->cond.terms[0].cond.id == 3 && st->n == 200 && st[1].op == KP_SCRIPT_KEY
		&& st[1].id == 1, "until with every and its input");
	kp_t_ok(T, s->steps[35].op == KP_SCRIPT_SHOT && s->steps[36].op == KP_SCRIPT_STATE && s->steps[37].op == KP_SCRIPT_UI
		&& strcmp(s->steps[37].text, "start") == 0, "shot, state, ui of one name");
	st = &s->steps[38];
	kp_t_ok(T, st->op == KP_SCRIPT_LOG && strcmp(st->text, "start") == 0 && st->value_count == 3 && st->values[1].id == 2
		&& st->values[1].arg == 3 && st->values[2].id == 3 && st->values[2].arg == 0, "log's values with arguments");
	st = &s->steps[39];
	kp_t_ok(T, st->op == KP_SCRIPT_EXPECT && st->cond.terms[0].b.slot == 1 && st->cond.terms[0].b.k == 40, "expect, +40 glued");
	kp_t_eq(T, s->marks[0].count, 2, "the mark keeps the values read from it (tick, gamma)");
	kp_t_ok(T, s->steps[40].id == 1 && strcmp(s->steps[40].text, "koprulu save check") == 0 && s->steps[41].id == 0,
		"check with its argument, check alone");
	kp_t_ok(T, s->steps[42].op == KP_SCRIPT_QUIT, "quit");
}

static void parse_defaults(KpT *T)
{
	KpScript *s;
	Rec R;
	rec_init(&R);
	s = parse_ok(T, &R, "options originals\nkey A\n");
	kp_t_ok(T, s->options_originals && !s->video_originals && !s->clock_real && !s->mission[0], "options alone, a fixed clock");
	kp_t_eq(T, s->timeout_ms, KP_SCRIPT_TIMEOUT_MS, "the default timeout");
	kp_t_eq(T, s->watchdog_ms, KP_SCRIPT_WATCHDOG_MS, "the default watchdog");
}

static void parse_refuses_bad_lines(KpT *T)
{
	static const struct {
		const char *text, *error;
	} BAD[] = {
		{ "frobnicate", "bad.kps:1: unknown command \"frobnicate\"" },
		{ "key F1O", "bad.kps:1: unknown key \"F1O\"" },
		{ "wait frames 99999999999999999999", "is no number in 1..10000000" },
		{ "wait frames 0", "is no number in 1..10000000" },
		{ "wait seconds 0", "is no time in seconds" },
		{ "wait seconds 1.2345", "is no time in seconds" },
		{ "text \"unclosed", "a string without its closing quote" },
		{ "text \"a\"b", "a string runs into a word" },
		{ "text \"a\\nb\"", "escapes only" },
		{ "key A text", "the key's text expected (a quoted string)" },
		{ "key A text \"\"", "the key's text is empty" },
		{ "key A mod hyper", "\"hyper\" is no modifier" },
		{ "key A notext notext", "a second text or notext" },
		{ "shot ../x", "a file's name is [A-Za-z0-9_-]" },
		{ "shot a\nshot a", "bad.kps:2: a second file named a" },
		{ "ui a\nui A", "bad.kps:2: a second file named A" }, /* one file where the file system ignores case */
		{ "shot failure", "the name failure is the run's" },
		{ "state Failure", "the name Failure is the run's" },
		{ "include ../x.kps", "an include is a file beside the script" },
		{ "include sub/x.kps", "an include is a file beside the script" },
		{ "include missing.kps", "cannot read missing.kps" },
		{ "key A\nmission zerg/zerg01", "bad.kps:2: mission belongs in the header, before the first command" },
		{ "mission a\nmission b", "bad.kps:2: a second mission" },
		{ "mission ../a", "a mission is" },
		{ "clock slow", "the clock is fixed or real" },
		{ "key A &", "bad.kps:1: the last line ends in &" },
		{ "key A &\nwait frames 1", "bad.kps:2: the line before ends in &: this one must be an input" },
		{ "key A &\nshot a\nkey A", "bad.kps:2: the line before ends in &" },
		{ "wait frames 2 &", "& ends only an input's line" },
		{ "&", "& alone" },
		{ "expect gamma == @start.gamma", "no mark \"start\" before this line" },
		{ "mark a\nexpect gamma == @a", "a mark's value is @MARK.VALUE" },
		{ "mark a\nmark a", "a second mark named a" },
		{ "pick a\npick a", "a second pick named a" },
		{ "log x units", "the value units takes a number" },
		{ "log x gamma:2", "the value gamma takes no argument" },
		{ "log x moved:u", "the value moved takes a pick made before" },
		{ "log x frobs", "unknown value \"frobs\"" },
		{ "expect gamma 5", "a comparison (== != < <= > >=) expected, not 5" },
		{ "expect gamma ==", "a value or a number expected" },
		{ "wait frobs", "unknown condition \"frobs\"" },
		{ "wait screen", "the condition screen needs a screen" },
		{ "wait screen key", "the condition screen needs a screen, not \"key\"" },
		{ "wait game and", "a condition expected" },
		{ "wait game and game and game and game and game", "a condition of more than 4 terms" },
		{ "wait game within 5 within 6", "a second within" },
		{ "until nodialog wait frames 1", "until repeats an input" },
		{ "until nodialog", "until needs an input after its condition" },
		{ "until nodialog every 1 every 2 key A", "a second every" },
		{ "press view 160% 5%", "is no percent in 0..100" },
		{ "press view 10 5%", "a view's point is X% Y%" },
		{ "tap 1 unit u", "no pick \"u\" before this line" },
		{ "click frob 1 2", "\"frob\" is no point" },
		{ "click control nothing", "unknown control \"nothing\"" },
		{ "select worker", "the box's radius expected" },
		{ "camera by 1", "the camera's y expected" },
		{ "finger sideways 1 2 3", "a finger goes down, move or up" },
		{ "touch off", "\"on\" expected, not \"off\"" },
		{ "check frob", "unknown check \"frob\"" },
		{ "check save-file", "the check's argument expected" },
		{ "check terrain \"x\"", "unexpected \"a string\"" },
		{ "\"key\" A", "a command expected, not a string" },
		{ "key \"A\"", "key expected, not a string" },
		{ "key A\x01", "a control character" },
		{ "key A\"b", "a quote inside a word" },
	};
	KpScript s;
	KpError err;
	Rec R;
	KpScriptHost h;
	char text[1024];
	size_t k;
	rec_init(&R);
	h = host(&R);
	for (k = 0; k < sizeof BAD / sizeof BAD[0]; k++) {
		kp_t_ok(T, kp_script_parse(&s, "bad.kps", BAD[k].text, strlen(BAD[k].text), &h, &err) != 0, "refused: %s", BAD[k].text);
		kp_t_ok(T, strstr(err.msg, BAD[k].error) != NULL, "%s: the error \"%s\" says \"%s\"", BAD[k].text, err.msg, BAD[k].error);
		kp_t_ok(T, s.steps == NULL && s.texts[0] == NULL, "%s: nothing left to free", BAD[k].text);
	}
	/* the limits: a line's bytes, a word's, a line's words */
	memset(text, 'x', 600);
	text[600] = 0;
	kp_t_ok(T, kp_script_parse(&s, "bad.kps", text, strlen(text), &h, &err) != 0 && strstr(err.msg, "a line longer than 511 bytes"),
		"a long line: %s", err.msg);
	snprintf(text, sizeof text, "key %070d", 1);
	kp_t_ok(T, kp_script_parse(&s, "bad.kps", text, strlen(text), &h, &err) != 0 && strstr(err.msg, "a word longer than 63 bytes"),
		"a long word: %s", err.msg);
	snprintf(text, sizeof text, "log a");
	for (k = 0; k < 32; k++) snprintf(text + strlen(text), sizeof text - strlen(text), " gamma");
	kp_t_ok(T, kp_script_parse(&s, "bad.kps", text, strlen(text), &h, &err) != 0 && strstr(err.msg, "more than 32 words"),
		"many words: %s", err.msg);
	text[0] = 'k';
	memcpy(text, "key A\0", 6);
	kp_t_ok(T, kp_script_parse(&s, "bad.kps", text, 6, &h, &err) != 0 && strstr(err.msg, "a NUL byte"), "a NUL byte: %s", err.msg);
}

static void parse_reads_includes(KpT *T)
{
	KpScript *s, bad;
	KpError err;
	Rec R;
	KpScriptHost h;
	char line[KP_SCRIPT_LINE];
	static const char *const BAD[][2] = {
		{ "include bad.kps", "bad.kps:2: unknown command \"frobnicate\"" },
		{ "include head.kps", "head.kps:1: mission belongs in the main script's header" },
		{ "include loop.kps", "loop.kps:1: includes nest deeper than 4" },
		{ "include amp.kps\nkey A", "amp.kps:1: the last line ends in &" },
		{ "key A &\ninclude enter.kps", "t.kps:2: the line before ends in &" },
	};
	size_t k;
	rec_init(&R);
	R.include_names[0] = "login.kps";
	R.include_texts[0] = "key Return\ninclude enter.kps\nshot after-login\n";
	R.include_names[1] = "enter.kps";
	R.include_texts[1] = "text \"me\"\nkey Return\n";
	R.include_names[2] = "bad.kps";
	R.include_texts[2] = "key A\nfrobnicate\n";
	R.include_names[3] = "head.kps";
	R.include_texts[3] = "mission zerg/zerg01\n";
	R.include_names[4] = "loop.kps";
	R.include_texts[4] = "include loop.kps\n";
	R.include_names[5] = "amp.kps";
	R.include_texts[5] = "key A &\n";
	s = parse_ok(T, &R, "mark m\ninclude login.kps\nexpect gamma == 0\n");
	kp_t_eq(T, s->count, 6, "the steps, the includes' in their place");
	kp_t_eq(T, s->file_count, 3, "the files");
	kp_t_ok(T, s->steps[1].op == KP_SCRIPT_KEY && strcmp(s->files[s->steps[1].file], "login.kps") == 0 && s->steps[1].line == 1,
		"login.kps:1");
	kp_t_ok(T, strcmp(s->files[s->steps[2].file], "enter.kps") == 0 && s->steps[3].line == 2, "enter.kps's lines");
	kp_t_ok(T, s->steps[4].op == KP_SCRIPT_SHOT && s->steps[4].line == 3, "login.kps:3 after its include");
	kp_t_ok(T, s->steps[5].op == KP_SCRIPT_EXPECT && s->steps[5].file == 0 && s->steps[5].line == 3, "t.kps:3");
	kp_script_line(s, &s->steps[2], line, sizeof line);
	kp_t_eq_str(T, line, "text \"me\"", "an included step's line");
	h = host(&R);
	for (k = 0; k < sizeof BAD / sizeof BAD[0]; k++) {
		kp_t_ok(T, kp_script_parse(&bad, "t.kps", BAD[k][0], strlen(BAD[k][0]), &h, &err) != 0, "refused: %s", BAD[k][0]);
		kp_t_ok(T, strstr(err.msg, BAD[k][1]) != NULL, "%s: the error \"%s\" says \"%s\"", BAD[k][0], err.msg, BAD[k][1]);
	}
}

static void a_line_is_the_files(KpT *T)
{
	KpScript *s;
	Rec R;
	char line[KP_SCRIPT_LINE];
	rec_init(&R);
	s = parse_ok(T, &R,
		"\xef\xbb\xbf\tkey A  \r\n  # a comment\r\n\tshot  x\t# its comment\r\nkey A text \"a #b\" # c\ntext \"a\\\\\" # d \"e\n");
	kp_t_eq(T, s->count, 4, "the steps");
	kp_t_eq(T, s->steps[1].line, 3, "the shot's line");
	kp_script_line(s, &s->steps[0], line, sizeof line);
	kp_t_eq_str(T, line, "key A", "a byte order mark, a tab and a CR left out");
	kp_script_line(s, &s->steps[1], line, sizeof line);
	kp_t_eq_str(T, line, "shot  x", "the line as the file has it, trimmed, its comment left out");
	kp_script_line(s, &s->steps[2], line, sizeof line);
	kp_t_eq_str(T, line, "key A text \"a #b\"", "a # in a string is no comment");
	kp_script_line(s, &s->steps[3], line, sizeof line);
	kp_t_eq_str(T, line, "text \"a\\\\\"", "an escaped \\ ends its string before the comment");
}

/* A dialog named as a command that is no input (quit, wait) is a condition's name; an input's command after a condition
 * is until's input. */
static void a_dialog_may_be_named_as_a_command(KpT *T)
{
	KpScript *s;
	Rec R;
	const KpScriptStep *st;
	rec_init(&R);
	s = parse_ok(T, &R, "wait dialog quit\nexpect dialog wait\nuntil dialog quit key Escape\nuntil dialog key Escape\n");
	kp_t_eq(T, s->count, 6, "the steps");
	st = s->steps;
	kp_t_ok(T, st[0].op == KP_SCRIPT_WAIT && st[0].cond.terms[0].cond.arg == 3, "wait dialog quit");
	kp_t_ok(T, st[1].op == KP_SCRIPT_EXPECT && st[1].cond.terms[0].cond.arg == 4, "expect dialog wait");
	kp_t_ok(T, st[2].op == KP_SCRIPT_UNTIL && st[2].cond.terms[0].cond.arg == 3 && st[3].op == KP_SCRIPT_KEY && st[3].id == 1,
		"until dialog quit, then its input");
	kp_t_ok(T, st[4].op == KP_SCRIPT_UNTIL && st[4].cond.terms[0].cond.arg == -1 && st[5].op == KP_SCRIPT_KEY,
		"until dialog (any), its input's command not taken as a dialog");
}

static void an_input_ends_the_frame(KpT *T)
{
	static const char *const EVENTS_WANT[] = { "1 key 1", "2 key 2", "2 key 3", "5 key 4" };
	static const char *const LINES_WANT[] = { "script t.kps:2: one", "script t.kps:5: two", "script t: 7 steps, 0 failures" };
	KpScript *s;
	KpScriptRun run;
	Rec R;
	rec_init(&R);
	s = parse_ok(T, &R, "key Escape\nlog one\nkey Return &\nkey F10\nlog two\nwait frames 2\nkey A\n");
	kp_script_start(&run, s);
	kp_t_eq(T, run_frames(&run, &R, 20), 6, "the frames: the last ends the run");
	events_are(T, &R, EVENTS_WANT, 4);
	lines_are(T, &R, LINES_WANT, 3);
	kp_t_ok(T, run.done && !run.aborted && run.failures == 0, "done, no failures");
	{
		KpScriptHost h = host(&R);
		kp_t_eq(T, kp_script_frame(&run, &h, R.now), 0, "a done run stays done");
	}
	kp_t_eq(T, R.line_count, 3, "its last line once");
}

static void a_wait_holds_or_times_out(KpT *T)
{
	KpScript *s1, *s2, *s3, *s4;
	KpScriptRun run;
	Rec R;
	{
		static const char *const WANT[] = { "3 shot -1 a", "3 key 4" };
		rec_init(&R);
		R.cond_from[0] = 3;
		s1 = parse_ok(T, &R, "wait game\nshot a\nkey A\n");
		kp_script_start(&run, s1);
		kp_t_eq(T, run_frames(&run, &R, 20), 4, "the frames");
		events_are(T, &R, WANT, 2);
	}
	{
		static const char *const WANT[] = { "script t.kps:2: wait game: no change in 112 ms: condition 0 not yet FAILED",
			"script t: 0 steps, 1 failures" };
		rec_init(&R);
		s2 = parse_ok(T, &R, "timeout 0.1\nwait game\nkey A\n");
		kp_script_start(&run, s2);
		kp_t_eq(T, run_frames(&run, &R, 20), 8, "the frame of the timeout (100 ms past the first)");
		lines_are(T, &R, WANT, 2);
		kp_t_ok(T, run.aborted && run.failures == 1 && R.event_count == 0, "aborted: the key never sent");
	}
	{
		static const char *const WANT[] = { "script t.kps:2: wait tick >= 5 within @m.gamma + 20: no change in 64 ms: no game FAILED",
			"script t: 1 steps, 1 failures" };
		rec_init(&R);
		R.values[1] = 30;
		R.missing[0] = 1;
		s3 = parse_ok(T, &R, "mark m\nwait tick >= 5 within @m.gamma + 20\n");
		kp_script_start(&run, s3);
		kp_t_eq(T, run_frames(&run, &R, 20), 5, "within the mark's gamma + 20 ms, no value meanwhile");
		lines_are(T, &R, WANT, 2);
	}
	{
		static const char *const WANT[] = { "5 key 4" };
		rec_init(&R);
		s4 = parse_ok(T, &R, "wait seconds 0.05\nkey A\n");
		kp_script_start(&run, s4);
		kp_t_eq(T, run_frames(&run, &R, 20), 6, "the frames");
		events_are(T, &R, WANT, 1);
	}
}

/* until's input again every S until its condition holds; the input is a step of its own in the last line's count
 * (as in the first line's), or no step run when the condition held at once. */
static void until_repeats_its_input(KpT *T)
{
	static const char *const EVENTS_WANT[] = { "1 key 1", "5 key 1", "7 shot -1 done" };
	static const char *const LINES_WANT[] = { "script t: 3 steps, 0 failures" };
	static const char *const AT_ONCE_EVENTS[] = { "1 shot -1 done" };
	static const char *const AT_ONCE_LINES[] = { "script t: 2 steps, 0 failures" };
	static const char TEXT[] = "until nodialog every 0.05 key Escape\nshot done\n";
	KpScript *s;
	KpScriptRun run;
	Rec R;
	rec_init(&R);
	R.cond_from[3] = 7;
	s = parse_ok(T, &R, TEXT);
	kp_script_start(&run, s);
	kp_t_eq(T, run_frames(&run, &R, 20), 7, "the frames: the run ends in the frame of its last step");
	events_are(T, &R, EVENTS_WANT, 3);
	lines_are(T, &R, LINES_WANT, 1);
	rec_init(&R);
	R.cond_from[3] = 0;
	s = parse_ok(T, &R, TEXT);
	kp_script_start(&run, s);
	kp_t_eq(T, run_frames(&run, &R, 20), 1, "the condition held at once");
	events_are(T, &R, AT_ONCE_EVENTS, 1);
	lines_are(T, &R, AT_ONCE_LINES, 1);
}

static void expect_aborts_and_check_counts(KpT *T)
{
	KpScript *s1, *s2;
	KpScriptRun run;
	Rec R;
	{
		static const char *const WANT[] = { "script t.kps:1: check terrain: 3 mismatches FAILED",
			"script t.kps:2: expect gamma == 5: 4 == 5 does not hold FAILED", "script t: 1 steps, 2 failures" };
		rec_init(&R);
		R.check_fails = 1;
		R.values[1] = 4;
		s1 = parse_ok(T, &R, "check terrain\nexpect gamma == 5\nlog never\n");
		kp_script_start(&run, s1);
		kp_t_eq(T, run_frames(&run, &R, 20), 1, "one frame");
		lines_are(T, &R, WANT, 3);
		kp_t_ok(T, run.aborted && run.checks == 1 && run.failures == 2, "the check counted, the expect aborted");
		kp_t_eq(T, R.aborted_at, 2, "the host hears of the abort after its FAILED line, before the last line");
	}
	{
		static const char *const WANT[] = { "script t.kps:1: click 1 1: no game FAILED", "script t: 0 steps, 1 failures" };
		rec_init(&R);
		R.fail_op = KP_SCRIPT_CLICK;
		s2 = parse_ok(T, &R, "click 1 1\nkey A\n");
		kp_script_start(&run, s2);
		kp_t_eq(T, run_frames(&run, &R, 20), 1, "one frame");
		lines_are(T, &R, WANT, 2);
		kp_t_eq(T, R.aborted_at, 1, "a step the host cannot run: the host hears of the abort before the last line");
	}
}

static void marks_keep_their_values(KpT *T)
{
	KpScript *s1, *s2;
	KpScriptRun run;
	Rec R;
	{
		static const char *const WANT[] = { "script t.kps:5: after: gamma 140, camera.x n/a, units:2 2007, moved:u 42",
			"script t: 5 steps, 0 failures" };
		rec_init(&R);
		R.values[1] = 100;
		R.values[2] = 7;
		R.key_adds_gamma = 40;
		R.missing[4] = 1;
		s1 = parse_ok(T, &R, "mark start\nkey A\nexpect gamma == @start.gamma + 40\npick u\nlog after gamma camera.x units:2 moved:u\n");
		kp_script_start(&run, s1);
		kp_t_eq(T, run_frames(&run, &R, 20), 2, "the frames");
		lines_are(T, &R, WANT, 2);
	}
	{
		static const char *const WANT[] = { "script t.kps:1: mark m: no game FAILED", "script t: 0 steps, 1 failures" };
		rec_init(&R);
		R.missing[1] = 1;
		s2 = parse_ok(T, &R, "mark m\nexpect gamma == @m.gamma\n");
		kp_script_start(&run, s2);
		run_frames(&run, &R, 20);
		lines_are(T, &R, WANT, 2);
	}
}

static void the_host_can_abort(KpT *T)
{
	static const char *const WANT[] = { "script t.kps:2: wait game: the window closed FAILED", "script t: 1 steps, 1 failures" };
	KpScript *s;
	KpScriptRun run;
	KpScriptHost h;
	Rec R;
	rec_init(&R);
	s = parse_ok(T, &R, "key A\nwait game\n");
	kp_script_start(&run, s);
	kp_t_eq(T, run_frames(&run, &R, 2), 2, "two frames");
	h = host(&R);
	kp_script_abort(&run, &h, "the window closed");
	kp_script_abort(&run, &h, "twice");
	lines_are(T, &R, WANT, 2);
	kp_t_eq(T, R.aborted_at, -1, "a stop from outside is no abort of the run's own (no failure's files)");
	kp_t_eq(T, kp_script_frame(&run, &h, R.now), 0, "done");
}

static const KpTest TESTS[] = {
	{ "parse_reads_the_commands", parse_reads_the_commands, 0 },
	{ "parse_defaults", parse_defaults, 0 },
	{ "parse_refuses_bad_lines", parse_refuses_bad_lines, 0 },
	{ "parse_reads_includes", parse_reads_includes, 0 },
	{ "a_line_is_the_files", a_line_is_the_files, 0 },
	{ "a_dialog_may_be_named_as_a_command", a_dialog_may_be_named_as_a_command, 0 },
	{ "an_input_ends_the_frame", an_input_ends_the_frame, 0 },
	{ "a_wait_holds_or_times_out", a_wait_holds_or_times_out, 0 },
	{ "until_repeats_its_input", until_repeats_its_input, 0 },
	{ "expect_aborts_and_check_counts", expect_aborts_and_check_counts, 0 },
	{ "marks_keep_their_values", marks_keep_their_values, 0 },
	{ "the_host_can_abort", the_host_can_abort, 0 },
};

KP_SUITE(kp_suite_script, "test_script", TESTS);
