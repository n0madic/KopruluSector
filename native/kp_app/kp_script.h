/*
 * Scripted runs of the product (koprulu --script FILE; the language in docs/formats/script.md, ADR 0017): a script's
 * lines parsed into steps, then run a frame at a time over a host. One command a line, `#` a comment; no variables,
 * branches or loops. Headless: the host resolves the names a script uses (keys, values, conditions, controls...), runs
 * its inputs and observations, gives the values and the clock. The product's host is native/kp_app/sdl/kp_app_script.c
 * (over kp_script_flow.c), the tests' a recording one (tests/c/test_script.c).
 *
 * A frame's steps (kp_script_frame, called after the frame is drawn and before it is shown):
 *   - observations (shot, state, ui, log, mark, pick, expect, check) run at once, as many as come;
 *   - an input or an action (key, click, finger, camera, save...) runs and ends the frame's steps, so the steps after it
 *     see the frame drawn after it; on a line ending in " &" the next input runs in the same frame;
 *   - a wait ends the frame's steps until it holds; the steps after it run in the frame it held in.
 * A failed expect, a wait past its time and a step the host cannot run (no game for it) abort the run: the steps left
 * are skipped. A failed check is counted and the run goes on.
 */
#ifndef KP_SCRIPT_H
#define KP_SCRIPT_H

#include <stddef.h>
#include <stdint.h>

#include "kp_kpf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_SCRIPT_LINE 512        /* a line's bytes */
#define KP_SCRIPT_WORDS 32        /* a line's words */
#define KP_SCRIPT_NAME 64         /* a name's bytes with its NUL (a file's, a mark's, a pick's, a word's) */
#define KP_SCRIPT_STRING 128       /* a quoted string's bytes with its NUL */
#define KP_SCRIPT_STEPS 16384     /* a script's steps, its includes' too */
#define KP_SCRIPT_FILES 16        /* the main file and its includes */
#define KP_SCRIPT_INCLUDES 4      /* how deep includes nest */
#define KP_SCRIPT_MARKS 32
#define KP_SCRIPT_MARK_VALUES 32  /* the values one mark keeps (those an @mark.value reads) */
#define KP_SCRIPT_PICKS 16
#define KP_SCRIPT_TERMS 4         /* a condition's terms joined by "and" */
#define KP_SCRIPT_LOG_VALUES 12
#define KP_SCRIPT_WHY 256         /* a host's reason */
#define KP_SCRIPT_TIMEOUT_MS 60000  /* a wait's time without "within" or the header's timeout */
#define KP_SCRIPT_WATCHDOG_MS 60000 /* a frame's time before the host's watchdog ends the program */
#define KP_SCRIPT_EVERY_MS 250      /* until's time between its inputs without "every" */

typedef enum {
	/* inputs and actions: each ends the frame's steps, unless its line ends in " &" */
	KP_SCRIPT_KEY,             /* id: the key; mods; flag: KP_SCRIPT_KEY_*; text */
	KP_SCRIPT_TEXT,            /* text: a layout's text, as the text event gives it */
	KP_SCRIPT_MOVE,            /* p: the mouse moved there */
	KP_SCRIPT_CLICK,           /* p: the left button pressed and released there */
	KP_SCRIPT_RCLICK,          /* p: the right button's */
	KP_SCRIPT_PRESS,           /* p: the left button pressed there */
	KP_SCRIPT_RELEASE,         /* p: the left button released there */
	KP_SCRIPT_FINGER,          /* flag: KP_SCRIPT_FINGER_*; id: the finger; p */
	KP_SCRIPT_TAP,             /* id: the finger; p: down and up there */
	KP_SCRIPT_TOUCH,           /* touch on: the touch layer's mode */
	KP_SCRIPT_CHOOSE,          /* id: the video option's choice clicked */
	KP_SCRIPT_PAUSE,
	KP_SCRIPT_RESUME,
	KP_SCRIPT_CAMERA,          /* flag: KP_SCRIPT_CAMERA_*; p.x, p.y */
	KP_SCRIPT_WORLD,           /* flag: on (the world's images drawn) */
	KP_SCRIPT_FOG,             /* flag: on */
	KP_SCRIPT_SAVE,            /* text: the save's name (the game saved under it) */
	KP_SCRIPT_LOAD,            /* text: the save loaded in the game */
	KP_SCRIPT_LOAD_FILE,       /* text: the save loaded from its file (the glue's Load Saved) */
	KP_SCRIPT_MENU,            /* id: the game menu opened */
	KP_SCRIPT_OPTIONS_DIALOG,  /* id: the options dialog opened */
	KP_SCRIPT_ACTIVATE,        /* id: the open menu's control activated as its click does */
	KP_SCRIPT_SELECT,          /* id: the unit's kind; n: the box's radius; flag: optional (no such unit is no failure) */
	KP_SCRIPT_END_PLACEMENT,
	KP_SCRIPT_CANCEL_TARGETING,
	KP_SCRIPT_AUDIT,           /* audit hd: the canvases count the draws outside the image log */
	KP_SCRIPT_HANG,            /* n: ms the frame takes (the watchdog's test) */
	/* waits */
	KP_SCRIPT_WAIT_FRAMES,     /* n: the steps after it run n frames later */
	KP_SCRIPT_WAIT_TIME,       /* n: ms */
	KP_SCRIPT_WAIT,            /* cond; within (has_within) */
	KP_SCRIPT_UNTIL,           /* cond; within; n: ms between the inputs; the next step is its input */
	/* observations */
	KP_SCRIPT_SHOT,            /* text: the file's name (NAME.png) */
	KP_SCRIPT_STATE,           /* text: NAME.state */
	KP_SCRIPT_UI,              /* text: NAME.ui */
	KP_SCRIPT_LOG,             /* text: the label; values */
	KP_SCRIPT_MARK,            /* id: the mark (its values kept) */
	KP_SCRIPT_PICK,            /* id: the pick (the host sets run->picks[id]) */
	KP_SCRIPT_EXPECT,          /* cond: it holds, or the run aborts */
	KP_SCRIPT_CHECK,           /* id: the check; text: its argument */
	KP_SCRIPT_QUIT,            /* the run ends here */
	KP_SCRIPT_OPS
} KpScriptOp;

typedef enum { KP_SCRIPT_INPUT, KP_SCRIPT_WAITS, KP_SCRIPT_OBSERVES, KP_SCRIPT_ENDS } KpScriptClass;

enum { KP_SCRIPT_KEY_AUTO, KP_SCRIPT_KEY_TEXT, KP_SCRIPT_KEY_NOTEXT };            /* a key's text */
enum { KP_SCRIPT_MOD_SHIFT = 1, KP_SCRIPT_MOD_CTRL = 2, KP_SCRIPT_MOD_ALT = 4, KP_SCRIPT_MOD_GUI = 8 };
enum { KP_SCRIPT_FINGER_DOWN, KP_SCRIPT_FINGER_MOVE, KP_SCRIPT_FINGER_UP };
enum {
	KP_SCRIPT_CAMERA_AT,  /* the view's centre on the world's p.x, p.y */
	KP_SCRIPT_CAMERA_BY,  /* the view moved by p.x, p.y */
	KP_SCRIPT_CAMERA_MAX  /* the view at the map's bottom-right */
};

/* The kinds of names the host resolves. */
typedef enum {
	KP_SCRIPT_NAME_KEY,      /* key NAME */
	KP_SCRIPT_NAME_VALUE,    /* a value: NAME, or NAME:ARG */
	KP_SCRIPT_NAME_COND,     /* a named condition */
	KP_SCRIPT_NAME_CONTROL,  /* control NAME (a point), activate NAME */
	KP_SCRIPT_NAME_CHECK,    /* check NAME */
	KP_SCRIPT_NAME_CHOICE,   /* choose NAME */
	KP_SCRIPT_NAME_MENU,     /* menu NAME */
	KP_SCRIPT_NAME_OPTIONS,  /* options-dialog NAME */
	KP_SCRIPT_NAME_UNIT,     /* select NAME */
	KP_SCRIPT_NAME_DIALOG,   /* a condition's argument: a dialog */
	KP_SCRIPT_NAME_SCREEN,   /* a condition's argument: a screen */
	KP_SCRIPT_NAME_KINDS
} KpScriptKind;

/* What a name takes after it. */
typedef enum {
	KP_SCRIPT_ARG_NONE,
	KP_SCRIPT_ARG_INT,       /* a value's NAME:N */
	KP_SCRIPT_ARG_PICK,      /* a value's NAME:PICK (a pick's name) */
	KP_SCRIPT_ARG_NAME,      /* a condition's next word, a name of arg_kind */
	KP_SCRIPT_ARG_OPT_NAME,  /* a condition's next word when it is a name of arg_kind */
	KP_SCRIPT_ARG_TEXT,      /* a check's quoted string */
	KP_SCRIPT_ARG_OPT_TEXT
} KpScriptArg;

/* A name the host resolved. */
typedef struct {
	int32_t id;
	KpScriptArg arg;
	KpScriptKind arg_kind; /* KP_SCRIPT_ARG_NAME's and _OPT_NAME's */
	int64_t arg_max;       /* KP_SCRIPT_ARG_INT's largest N, a parse error past it (0: the parse's own limit) */
} KpScriptName;

/* A value or a named condition: the host's id, its argument (a number, a pick's slot, a name's id; -1 none). */
typedef struct {
	int32_t id;
	int64_t arg;
} KpScriptRef;

enum { KP_SCRIPT_EXPR_NUMBER, KP_SCRIPT_EXPR_VALUE, KP_SCRIPT_EXPR_MARK };

/* An expression: a number, a value, or a value a mark kept (@mark.value), plus k. */
typedef struct {
	int kind;
	int mark, slot; /* KP_SCRIPT_EXPR_MARK's: the mark and its value's slot */
	KpScriptRef value;
	int64_t k;
} KpScriptExpr;

enum { KP_SCRIPT_EQ, KP_SCRIPT_NE, KP_SCRIPT_LT, KP_SCRIPT_LE, KP_SCRIPT_GT, KP_SCRIPT_GE };

typedef struct {
	int named; /* a named condition (cond), or a comparison (a op b) */
	int op;
	KpScriptRef cond;
	KpScriptExpr a, b;
} KpScriptTerm;

typedef struct {
	int count;
	KpScriptTerm terms[KP_SCRIPT_TERMS];
} KpScriptCond;

enum {
	KP_SCRIPT_AT_XY,        /* x, y: logical pixels */
	KP_SCRIPT_AT_WORLD,     /* x, y: the world's pixels */
	KP_SCRIPT_AT_CONTROL,   /* name: the open dialog's control; flag: its right end (a slider's) */
	KP_SCRIPT_AT_UNIT,      /* name: the pick; x, y: an offset */
	KP_SCRIPT_AT_PAD,       /* name: the touch pad's group button */
	KP_SCRIPT_AT_PAD_TOGGLE,
	KP_SCRIPT_AT_CENTER,    /* the view's centre; x, y: an offset */
	KP_SCRIPT_AT_VIEW,      /* x, y: percent of the logical screen */
	KP_SCRIPT_AT_BESIDE     /* name: the pick; x: the distance (to its right, or its left at the view's edge) */
};

typedef struct {
	int kind;
	int32_t name;
	int flag;
	int32_t x, y;
} KpScriptPoint;

typedef struct {
	KpScriptOp op;
	int same_frame;     /* the line ended in " &": the next input runs in this frame */
	int flag;           /* per op (KP_SCRIPT_KEY_*, KP_SCRIPT_FINGER_*, KP_SCRIPT_CAMERA_*, on, optional) */
	int mods;           /* a key's KP_SCRIPT_MOD_* */
	int file, line;     /* where it is (script->files, 1-based) */
	int32_t id;         /* a host name's id; a finger's; a mark's or a pick's slot */
	int64_t n;          /* frames, ms, a radius */
	KpScriptPoint p;
	KpScriptCond cond;  /* wait's, until's, expect's */
	int has_within;
	KpScriptExpr within; /* ms */
	int value_count;
	KpScriptRef values[KP_SCRIPT_LOG_VALUES]; /* log's */
	char text[KP_SCRIPT_STRING];
} KpScriptStep;

typedef struct {
	char name[KP_SCRIPT_NAME];
	int count;
	KpScriptRef values[KP_SCRIPT_MARK_VALUES];
} KpScriptMark;

typedef struct {
	char name[KP_SCRIPT_NAME]; /* the main file's name without its directory and .kps */
	/* the header */
	char mission[KP_SCRIPT_STRING];
	int options_originals, video_originals, clock_real;
	int64_t timeout_ms, watchdog_ms;
	/* the files, their text kept for the lines quoted in the log */
	int file_count;
	char files[KP_SCRIPT_FILES][KP_SCRIPT_NAME];
	char *texts[KP_SCRIPT_FILES];
	size_t sizes[KP_SCRIPT_FILES];
	int mark_count, pick_count;
	KpScriptMark marks[KP_SCRIPT_MARKS];
	char picks[KP_SCRIPT_PICKS][KP_SCRIPT_NAME];
	int count, cap;
	KpScriptStep *steps;
} KpScript;

typedef struct KpScriptRun KpScriptRun;

/* The host. The parse uses name and read; a run the others. */
typedef struct {
	void *ud;
	/* Resolves word as a name of kind into out; 0, or -1 when it is no such name. */
	int (*name)(void *ud, KpScriptKind kind, const char *word, KpScriptName *out);
	/* An included file's bytes (malloc'd, the parse frees them), NULL when it cannot be read. */
	char *(*read)(void *ud, const char *name, size_t *size);
	/* A value now; 0, or -1 with why when it has none (no game: an expect fails, a wait goes on waiting). */
	int (*value)(void *ud, const KpScriptRun *run, const KpScriptRef *ref, int64_t *out, char *why, size_t size);
	/* A named condition now: 1 holds, 0 not (why may say what there is instead). */
	int (*cond)(void *ud, const KpScriptRun *run, const KpScriptRef *ref, char *why, size_t size);
	/* Runs an input, an action, or an observation of the host's (shot, state, ui, pick, check): 0, -1 with why when the
	 * step cannot run (the run aborts), KP_SCRIPT_CHECK_FAILED with why (counted, the run goes on). */
	int (*exec)(void *ud, KpScriptRun *run, const KpScriptStep *step, char *why, size_t size);
	/* A line of the run's log (the product prefixes "SPIKE "). */
	void (*log)(void *ud, const char *line);
	/* The run aborts on one of its steps (a wait past its time, an expect, a step the host cannot run), after the
	 * step's FAILED line and before the run's last line, so the host's files for the failure are logged before it
	 * (NULL: none). kp_script_abort, a stop from outside, does not call it. */
	void (*aborted)(void *ud, const KpScriptRun *run);
} KpScriptHost;

#define KP_SCRIPT_CHECK_FAILED 1

struct KpScriptRun {
	const KpScript *script;
	int step;             /* the next step */
	int steps;            /* the steps run */
	int checks;           /* the checks run */
	int failures;         /* the checks failed, and 1 for an abort */
	int aborted, done;
	int64_t frame;        /* the frames the run has seen */
	/* the current step's wait (entered: its first frame is past) */
	int entered;
	int64_t frames_left, since_ms, deadline_ms, next_input_ms;
	int inputs;           /* until's: its input's runs */
	int64_t marks[KP_SCRIPT_MARKS][KP_SCRIPT_MARK_VALUES];
	int marked[KP_SCRIPT_MARKS];
	int64_t picks[KP_SCRIPT_PICKS]; /* the host's units, set by its pick steps */
	char why[KP_SCRIPT_WHY];        /* why the current wait does not hold yet */
};

/* Parses the script file `name` (a path: its directory and .kps are left out of s->name) of size bytes; 0, or -1 with
 * err ("file:line: what") and nothing to free. Includes are read through h->read, names resolved through h->name. */
int kp_script_parse(KpScript *s, const char *name, const char *text, size_t size, const KpScriptHost *h, KpError *err);
void kp_script_free(KpScript *s);

KpScriptClass kp_script_class(KpScriptOp op);

/* The step's line as the file has it (trimmed, its comment left out), into out. */
void kp_script_line(const KpScript *s, const KpScriptStep *step, char *out, size_t size);

void kp_script_start(KpScriptRun *run, const KpScript *s);
/* Runs the frame's steps at the host's now_ms (the run's clock); 1 while the run goes on, 0 once it is done (its last
 * line logged: "script NAME: N steps, F failures"). */
int kp_script_frame(KpScriptRun *run, const KpScriptHost *h, int64_t now_ms);
/* Ends the run as aborted (the host's own failure: the program quit early), its last line logged once. */
void kp_script_abort(KpScriptRun *run, const KpScriptHost *h, const char *why);

#ifdef __cplusplus
}
#endif

#endif
