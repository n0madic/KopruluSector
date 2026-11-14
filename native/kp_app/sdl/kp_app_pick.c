/*
 * Load Replay's Import and Export over the platform's file picker (kp_flow.h's pick_replay and export_replay; plan
 * phase RP, stage RP4): SDL's file dialogs on the computers and Android (a content:// URI there, which SDL's file
 * streams open, its display name asked of the content resolver and the save dialog given the replay's name through
 * Koprulu's lines in SDLActivity, tools/native/android_app.sh), and on the web a file input and a download (the File System Access API's save picker where the
 * browser has it, which lets the player name the file). SDL may answer a dialog on another thread: the answer waits
 * under a lock for the next frame (kp_app_pick_frame), which imports or writes on the main thread. iOS has no SDL file
 * dialog and scripted or hidden runs offer none: there the buttons stay hidden and the screen is the original's.
 */
#include "kp_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#ifdef SDL_PLATFORM_ANDROID
#include <jni.h>
#endif

#define PICK_PATH 1100
#define WEB_IMPORT_FILE "/kp_import.rep" /* the page's picked file, in the memory file system until it is copied */
#define PICK_CURSOR_MS 1000 /* how long the hidden system cursor is reapplied after a dialog closes */

#ifndef __EMSCRIPTEN__
static const SDL_DialogFileFilter FILTERS[] = { { "StarCraft replays", "rep" } };

/* What a dialog answered, waiting for the main thread; and the bytes an export writes once it has a path. */
static struct {
	SDL_Mutex *lock;
	int open;              /* a dialog is open: another click waits for it */
	int kind;              /* the open dialog's: 1 import, 2 export */
	int answered;          /* its answer is in path (empty: cancelled or failed) */
	char path[PICK_PATH];
	uint8_t *bytes;        /* the export's (malloc'd) */
	size_t size;
	uint64_t cursor_until; /* main thread's: the hidden system cursor is reapplied until this tick */
} pick;

enum { PICK_IMPORT = 1, PICK_EXPORT = 2 };

static void dialog_answer(void *userdata, const char *const *files, int filter)
{
	(void)userdata;
	(void)filter;
	if (files == NULL) SDL_Log("replay picker: %s", SDL_GetError());
	SDL_LockMutex(pick.lock);
	snprintf(pick.path, sizeof pick.path, "%s", files != NULL && files[0] != NULL ? files[0] : "");
	pick.answered = 1;
	SDL_UnlockMutex(pick.lock);
}
#else
/* The page's file input: the picked file's bytes go to WEB_IMPORT_FILE and its name to Module.kp_import_name. */
EM_JS(void, web_pick_replay, (void), {
	var input = document.createElement("input");
	input.type = "file";
	input.accept = ".rep";
	input.onchange = function() {
		var file = input.files && input.files[0];
		if (!file) return;
		file.arrayBuffer().then(function(buf) {
			FS.writeFile("/kp_import.rep", new Uint8Array(buf));
			Module.kp_import_name = file.name;
		}).catch(function(e) { console.error("replay import: " + e); });
	};
	input.click();
});

/* The bytes as a file the player saves: the save picker (its name editable) where the browser has one, else a
 * download under the name. */
EM_JS(void, web_export_replay, (const char *name, const uint8_t *bytes, int size), {
	var fileName = UTF8ToString(name);
	var blob = new Blob([HEAPU8.slice(bytes, bytes + size)], { type: "application/octet-stream" });
	function download() {
		var a = document.createElement("a");
		a.href = URL.createObjectURL(blob);
		a.download = fileName;
		document.body.appendChild(a);
		a.click();
		setTimeout(function() { URL.revokeObjectURL(a.href); a.remove(); }, 0);
	}
	if (window.showSaveFilePicker) {
		window.showSaveFilePicker({ suggestedName: fileName, types: [{ description: "StarCraft replays",
			accept: { "application/octet-stream": [".rep"] } }] })
			.then(function(h) { return h.createWritable(); })
			.then(function(w) { return w.write(blob).then(function() { return w.close(); }); })
			.catch(function(e) { if (e && e.name !== "AbortError") { console.error("replay export: " + e); download(); } });
	} else {
		download();
	}
});

/* The page's picked file's name, taken (0: none waiting). */
EM_JS(int, web_take_import, (char *out, int size), {
	var name = Module.kp_import_name;
	if (!name) return 0;
	Module.kp_import_name = null;
	stringToUTF8(name, out, size);
	return 1;
});
#endif

#ifdef SDL_PLATFORM_ANDROID
/* SDLActivity's static method of Koprulu's (one String in): its String answer in out (empty: none); NULL text passes
 * null. On the main thread, which SDL attached to the VM. */
static void android_call(const char *method, const char *returns, const char *text, char *out, size_t size)
{
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	jobject activity = env ? (jobject)SDL_GetAndroidActivity() : NULL;
	jclass cls = activity ? (*env)->GetObjectClass(env, activity) : NULL;
	char signature[64];
	jmethodID m;
	jstring arg = NULL;
	if (out && size) out[0] = 0;
	snprintf(signature, sizeof signature, "(Ljava/lang/String;)%s", returns);
	if (cls && (m = (*env)->GetStaticMethodID(env, cls, method, signature)) != NULL
		&& (text == NULL || (arg = (*env)->NewStringUTF(env, text)) != NULL)) {
		if (out == NULL) {
			(*env)->CallStaticVoidMethod(env, cls, m, arg);
		} else {
			jstring answer = (jstring)(*env)->CallStaticObjectMethod(env, cls, m, arg);
			if (!(*env)->ExceptionCheck(env) && answer != NULL) {
				const char *chars = (*env)->GetStringUTFChars(env, answer, NULL);
				if (chars) snprintf(out, size, "%s", chars);
				if (chars) (*env)->ReleaseStringUTFChars(env, answer, chars);
			}
			if (answer) (*env)->DeleteLocalRef(env, answer);
		}
	}
	if (env && (*env)->ExceptionCheck(env)) {
		SDL_Log("replay picker: %s failed", method);
		(*env)->ExceptionClear(env);
	}
	if (arg) (*env)->DeleteLocalRef(env, arg);
	if (cls) (*env)->DeleteLocalRef(env, cls);
	if (activity) (*env)->DeleteLocalRef(env, activity);
}
#endif

static void host_pick_replay(void *ctx)
{
#ifdef __EMSCRIPTEN__
	(void)ctx;
	web_pick_replay();
#else
	KpApp *a = (KpApp *)ctx;
	SDL_LockMutex(pick.lock);
	if (pick.open) {
		SDL_UnlockMutex(pick.lock);
		return;
	}
	pick.open = 1;
	pick.kind = PICK_IMPORT;
	pick.answered = 0;
	SDL_UnlockMutex(pick.lock);
	SDL_ShowOpenFileDialog(dialog_answer, NULL, a->window, FILTERS, 1, NULL, false);
#endif
}

static void host_export_replay(void *ctx, const char *name, const uint8_t *bytes, size_t size)
{
#ifdef __EMSCRIPTEN__
	(void)ctx;
	web_export_replay(name, bytes, (int)size);
#else
	KpApp *a = (KpApp *)ctx;
	char location[PICK_PATH];
	const char *docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
	uint8_t *copy;
	SDL_LockMutex(pick.lock);
	if (pick.open || !(copy = (uint8_t *)malloc(size ? size : 1))) {
		SDL_UnlockMutex(pick.lock);
		return;
	}
	memcpy(copy, bytes, size);
	free(pick.bytes);
	pick.bytes = copy;
	pick.size = size;
	pick.open = 1;
	pick.kind = PICK_EXPORT;
	pick.answered = 0;
	SDL_UnlockMutex(pick.lock);
	/* the documents folder with the replay's name (the dialog's name field where the platform has one; Android's
	 * document picker takes the name alone) */
	snprintf(location, sizeof location, "%s%s", docs ? docs : "", name);
#ifdef SDL_PLATFORM_ANDROID
	android_call("kpSetFileDialogName", "V", name, NULL, 0);
#endif
	SDL_ShowSaveFileDialog(dialog_answer, NULL, a->window, FILTERS, 1, location);
#endif
}

void kp_app_pick_setup(KpApp *a)
{
	KpFlowHost *h = &a->flow.host;
	h->pick_replay = NULL;
	h->export_replay = NULL;
	/* a scripted or hidden run is the original's screen; iOS has no SDL file dialog */
	if (a->flags.headless || a->flags.script[0] || a->flags.script_live[0] || kp_flow_dev_checks(&a->flags)) return;
#if defined(SDL_PLATFORM_IOS)
	(void)host_pick_replay;
	(void)host_export_replay;
#else
#ifndef __EMSCRIPTEN__
	if (!pick.lock && !(pick.lock = SDL_CreateMutex())) return;
#endif
	h->pick_replay = host_pick_replay;
	h->export_replay = host_export_replay;
#endif
}

void kp_app_pick_frame(KpApp *a)
{
#ifdef __EMSCRIPTEN__
	char name[PICK_PATH];
	if (a->flow.host.pick_replay && web_take_import(name, (int)sizeof name)) {
		size_t size = 0;
		uint8_t *bytes = kp_read_file(WEB_IMPORT_FILE, &size);
		if (bytes) (void)kp_flow_import_replay(&a->flow, name, bytes, size, NULL, 0);
		else SDL_Log("replay import %s: cannot be read", name);
		free(bytes);
		(void)remove(WEB_IMPORT_FILE);
	}
#else
	char path[PICK_PATH];
	uint8_t *bytes;
	size_t size = 0;
	int kind;
	if (!pick.lock) return;
	/* SDL hides the system cursor by the window's cursor rectangle (macOS) and the closing dialog leaves its arrow
	 * over the window: SDL still counts the cursor hidden, so host_cursor does nothing, and AppKit looks at the
	 * rectangle again only on a mouse event (a click), not when the sheet's closing animation ends. SDL_SetCursor(NULL)
	 * redraws the cursor (the rectangles invalidated) each frame until the sheet is surely gone. */
	if (pick.cursor_until) {
		if (SDL_GetTicks() >= pick.cursor_until) pick.cursor_until = 0;
		else if (!SDL_CursorVisible()) SDL_SetCursor(NULL);
	}
	SDL_LockMutex(pick.lock);
	if (!pick.open || !pick.answered) {
		SDL_UnlockMutex(pick.lock);
		return;
	}
	kind = pick.kind;
	snprintf(path, sizeof path, "%s", pick.path);
	bytes = pick.bytes;
	size = pick.size;
	pick.bytes = NULL;
	pick.size = 0;
	pick.open = pick.answered = 0;
	SDL_UnlockMutex(pick.lock);
	pick.cursor_until = SDL_GetTicks() + PICK_CURSOR_MS;
	if (path[0] && kind == PICK_IMPORT) {
		void *data = SDL_LoadFile(path, &size);
		char name[PICK_PATH];
		snprintf(name, sizeof name, "%s", path);
#ifdef SDL_PLATFORM_ANDROID
		if (strncmp(path, "content:", 8) == 0) { /* the URI names no file: the document's display name, if it has one */
			char shown[PICK_PATH];
			android_call("kpDisplayName", "Ljava/lang/String;", path, shown, sizeof shown);
			if (shown[0]) snprintf(name, sizeof name, "%s", shown);
		}
#endif
		if (data) (void)kp_flow_import_replay(&a->flow, name, (const uint8_t *)data, size, NULL, 0);
		else SDL_Log("replay import %s: %s", path, SDL_GetError());
		SDL_free(data);
	} else if (path[0] && kind == PICK_EXPORT) {
		if (SDL_SaveFile(path, bytes, size)) SDL_Log("replay export: %s", path);
		else SDL_Log("replay export %s: %s", path, SDL_GetError());
	}
	free(bytes);
#endif
}

void kp_app_pick_free(void)
{
#ifndef __EMSCRIPTEN__
	free(pick.bytes);
	pick.bytes = NULL;
	if (pick.lock) SDL_DestroyMutex(pick.lock);
	pick.lock = NULL;
#endif
}
