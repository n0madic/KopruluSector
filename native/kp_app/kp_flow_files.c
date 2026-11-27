/*
 * The flow's files (kp_flow_files.h): the map lists of Play Custom, Create Game and Load Replay (atlas P19, P20), the
 * saves' files (the profile's, the network's), the replays' files, Import and Export, the map transfer's files and the
 * custom map a game plays read from a file, a replay or a save. The games they start are kp_flow.c's.
 */
#include "kp_flow_files.h"
#include "kp_gametype.h"
#include "kp_ids.h"
#include "kp_netsave.h"
#include "kp_text.h"

#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- the saves' files --- */

/* The file of the logged-in ID's save name (the original's save\<ID>\<name>). */
void kp_flow_save_file(const KpFlow *f, const char *name, char *out, size_t size)
{
	kp_save_file_name(kp_profile_save_owner(f->profile), name, out, size);
}

/* The bytes of the logged-in ID's save name (malloc'd, *size), or NULL when its file does not read. */
uint8_t *kp_flow_read_save(const KpFlow *f, const char *name, size_t *size)
{
	char file[KP_SAVE_FILE_NAME], path[1100];
	kp_flow_save_file(f, name, file, sizeof file);
	pref_path(f, file, path, sizeof path);
	return kp_read_file(path, size);
}

/* A folder made, or one that is there: 0, else -1 (errno). */
int kp_flow_make_dir(const char *path)
{
#ifdef _WIN32
	return _mkdir(path) == 0 || errno == EEXIST ? 0 : -1;
#else
	return mkdir(path, 0755) == 0 || errno == EEXIST ? 0 : -1;
#endif
}

/* The file of owner's save name deleted: 0 when it stays (no right to it, held open elsewhere; logged), a missing one
 * counted as deleted. */
static int remove_save_file(KpFlow *f, const char *owner, const char *name)
{
	char file[KP_SAVE_FILE_NAME], path[1100];
	kp_save_file_name(owner, name, file, sizeof file);
	pref_path(f, file, path, sizeof path);
	if (remove(path) == 0 || errno == ENOENT) return 1;
	kp_flow_say(f, "save %s: cannot delete %s: %s", name, path, strerror(errno));
	return 0;
}

/* DeleteSaveFile 0x498b30's file for the interface's save and load dialogs: 0 when it stays, its name kept in the
 * profile. */
static int delete_save(void *host, const char *name)
{
	KpFlow *f = (KpFlow *)host;
	if (f->net_game) { /* a network game's: its file in maps/save */
		char path[KP_FLOW_MAP_PATH];
		if (!kp_flow_net_save_name_ok(name)) return 0;
		kp_flow_net_save_path(f, name, path, sizeof path);
		if (remove(path) != 0 && errno != ENOENT) {
			kp_flow_say(f, "save %s: cannot delete %s: %s", name, path, strerror(errno));
			return 0;
		}
		kp_flow_game_saves(f, f->ui);
		return 1;
	}
	if (!remove_save_file(f, kp_profile_save_owner(f->profile), name)) return 0;
	kp_profile_remove_save(f->profile, name);
	f->host.save_profile(f->host.ctx);
	return 1;
}

static void delete_save_file(void *host, const KpProfileSave *save)
{
	/* DeleteFoundFile 0x498ae0 does not look at DeleteFileA's result: the save leaves the list anyway */
	(void)remove_save_file((KpFlow *)host, save->owner, save->name);
}

/* DeleteProfileSaves 0x498a70 for the login's deleted ID: its saves' files and names (the login list's sync writes the
 * profile). */
static void delete_profile_saves(void *host, const char *name)
{
	KpFlow *f = (KpFlow *)host;
	kp_profile_drop_saves(f->profile, name, delete_save_file, f);
}

/* A save file of an older profile to its name now (kp_profile_rename_old_saves): 0 when it failed. One renamed by a run
 * that did not write the profile (a check's) has its new name already. */
int kp_flow_rename_old_save(void *host, const char *from, const char *to)
{
	KpFlow *f = (KpFlow *)host;
	char a[1100], b[1100];
	FILE *done;
	int why;
	pref_path(f, from, a, sizeof a);
	pref_path(f, to, b, sizeof b);
	if (rename(a, b) == 0) return 1;
	why = errno; /* the rename's, before the look at the new name */
	if ((done = fopen(b, "rb")) != NULL) {
		fclose(done);
		return 1;
	}
	kp_flow_say(f, "save: cannot rename %s to %s: %s (it leaves the list)", a, b, strerror(why));
	return 0;
}

/* --- Load Replay (glue state 12; atlas P19, P20) --- */

#define STR_EXPANSION 60       /* gluAll: a scenario of the expansion */
#define STR_DAMAGED 63
#define STR_NAME_TOO_LONG 62
#define STR_MISSING 66
#define STR_INVALID_SAVE 64    /* LoadSaveEntryInfo 0x45d5e0's titles: before the file is found, after */
#define STR_SAVED_GAME 47
#define STR_SAVE_NAME_TOO_LONG 65
#define STR_SAVE_VERSION 67    /* a save of another version */
#define SAVE_TARGET_MAX 31     /* LoadSavedGameByName 0x498bd0: the name and the extension, 31 characters at most */
#define REPLAY_NAME_LIMIT 32   /* LoadReplayEntryInfo 0x45d250: a file name of 32 bytes or more is refused */
#define KP_REP_LIST_PLAYERS 8  /* its entry's +0x479 */
#define MAP_ORIGINAL_VERSION 59 /* LoadMapEntryInfo 0x45d870: a header that fails with a later VER is the expansion's */
#define CUSTOM_REPLAYS "replays" /* Play Custom's root folder of the replays (the original's maps\replays) */
#define CUSTOM_BASES (1 + KP_APP_RES_DIRS)

/* Whether Create Game lists saved network games and a resumed game finds them: in multiplayer. The files are Koprulu's
 * own (kp_netsave.h), and the original's in the network oracle's runs (KpAppFlags.original_saves) only: the product
 * resumes no game of the original's, whose file it reads as unreadable (the plan leaves a mixed resume out). */
static int listed_saves(const KpUiMenus *m) { return m->multiplayer; }

/* ValidateSaveFile 0x499750 (out NULL; h and name as kp_netsave_probe's) or ReadSaveHeaderAndGameBlock 0x499c50 (out,
 * the game's expansion byte) over a saved network game's bytes and file name: Koprulu's after its build and data
 * (kp_save_check: another build's or other data's is another version), the original's with original_saves. */
static int read_net_save(const KpFlow *f, const uint8_t *bytes, size_t size, const char *file, KpNetGameHeader *h,
	char *name, KpNetGameSave *out)
{
	int expansion = f->ui && f->ui->menus.expansion, rc;
	if (kp_netsave_koprulu(bytes, size)) {
		KpNetGameSave s;
		KpError e;
		if (!f->identity) return KP_NETSAVE_UNREADABLE;
		rc = kp_save_check(bytes, size, f->identity, &e);
		if (rc != KP_SAVE_LOADABLE) return rc == KP_SAVE_OTHER_BUILD ? KP_NETSAVE_OTHER_VERSION : KP_NETSAVE_UNREADABLE;
		rc = kp_netsave_koprulu_read(bytes, size, file, out ? expansion : -1, &s, name);
		if (rc == KP_NETSAVE_VALID && h) *h = s.header;
		if (rc == KP_NETSAVE_VALID && out) *out = s;
		return rc;
	}
	if (!f->flags || !f->flags->original_saves) return KP_NETSAVE_UNREADABLE;
	rc = kp_netsave_probe(bytes, size, file, h, name);
	if (rc == KP_NETSAVE_VALID && out && !kp_netsave_read(bytes, size, expansion, out)) rc = KP_NETSAVE_UNREADABLE;
	return rc;
}

/* Whether s begins with prefix, the ASCII letters folded. */
static int prefix_nocase(const char *s, const char *prefix)
{
	size_t n = strlen(prefix);
	char head[ENTRY_PATH];
	if (strlen(s) < n || n >= sizeof head) return 0;
	memcpy(head, s, n);
	head[n] = 0;
	return kp_compare_nocase(head, prefix) == 0;
}

/* The CHK groups a custom game of a template's record reads: Use Map Settings', else the melee ones. */
KpChkLoad kp_flow_custom_load_of(const uint8_t *template_record)
{
	return kp_template_ums(template_record) ? KP_CHK_UMS : KP_CHK_MELEE;
}

/* Whether a file's name and path are within the original's limits (LoadReplayEntryInfo 0x45d250, LoadMapEntryInfo
 * 0x45d870): the name below 32 bytes in the map's encoding, the path and the name past 260 with a separator. */
static int entry_name_fits(const char *path)
{
	const char *base = strrchr(path, '/');
	char legacy[KP_UI_MAP_FILE];
	return kp_text_to_legacy(base ? base + 1 : path, legacy, sizeof legacy) < REPLAY_NAME_LIMIT && strlen(path) + 1 < 260;
}

/* The replay file at path read and checked; -1 with the gluAll text that refuses it. */
int kp_flow_read_replay_at(const char *path, KpRep *rep, uint16_t *refusal)
{
	uint8_t *bytes;
	size_t size;
	KpRepRefusal why = KP_REP_OK;
	KpError err;
	int rc;
	memset(rep, 0, sizeof *rep);
	if (!entry_name_fits(path)) {
		*refusal = STR_NAME_TOO_LONG;
		return -1;
	}
	if (!(bytes = kp_read_file(path, &size))) {
		*refusal = STR_MISSING;
		return -1;
	}
	rc = kp_rep_read(rep, bytes, size, &why, &err);
	free(bytes);
	if (rc != 0) {
		kp_rep_free(rep);
		*refusal = why == KP_REP_EXPANSION ? STR_EXPANSION : STR_DAMAGED;
		return -1;
	}
	return 0;
}

/* The catalog's mission of a replay's campaign row: its index, KP_FLOW_CUSTOM for a custom map's replay (row 0: its map
 * is its CHK), -1 for a row the catalog does not have. */
int kp_flow_replay_mission(const KpFlow *f, const KpRep *rep)
{
	int row = kp_rep_campaign(rep), k;
	if (row == 0) return KP_FLOW_CUSTOM;
	for (k = 0; k < f->data->mission_count; k++) {
		if (f->data->missions[k].catalog && f->data->missions[k].index == row) return k;
	}
	return -1;
}

/* A custom replay's map: the game type of its template (Use Map Settings or a melee type) picks the CHK's groups. */
static KpChkLoad replay_load(const KpRep *rep)
{
	return kp_flow_custom_load_of(rep->header + KP_REP_TEMPLATE);
}

/* The scenario's name of a replay's header as UTF-8 (the header's is in the map's encoding). */
static void replay_map_name(const KpRep *rep, char *out, size_t size)
{
	char map[KP_REP_MAP_NAME_SIZE + 1];
	memcpy(map, rep->header + KP_REP_MAP_NAME, KP_REP_MAP_NAME_SIZE);
	map[KP_REP_MAP_NAME_SIZE] = 0;
	(void)kp_text_from_legacy(map, out, size);
	if (!out[0]) snprintf(out, size, "%s", "map");
}

void kp_flow_custom_close(KpFlow *f)
{
	if (f->custom_map) kp_data_map_close(f->custom_map);
	free(f->custom_map);
	f->custom_map = NULL;
	f->has_custom_setup = 0;
}

/* The last part of a path or a URI after '/', '\\' or ':' (an Android document's "primary:Download/x.rep"). */
const char *kp_flow_last_part(const char *s)
{
	const char *at = s, *p;
	for (p = s; *p; p++) {
		if (*p == '/' || *p == '\\' || *p == ':') at = p + 1;
	}
	return at;
}

static int file_there(const char *path)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) return 0;
	fclose(fp);
	return 1;
}

/* The maps' folders: the user's maps folder, then each data folder's Maps (in the case its file system has), as the
 * original's user base before the install (ScanMapDirectory 0x45c9a0, ResolveGameFile 0x45cb90). Their count. */
static int map_roots(const KpFlow *f, char roots[CUSTOM_BASES][ENTRY_PATH])
{
	int n = 0, k;
	pref_path(f, MAP_DIR, roots[n++], ENTRY_PATH);
	for (k = 0; f->flags && k < f->flags->res_dir_count && n < CUSTOM_BASES; k++) {
		char **folders;
		int nf = 0, j;
		folders = kp_list_folders(f->flags->res_dirs[k], &nf);
		for (j = 0; j < nf; j++) {
			if (kp_compare_nocase(folders[j], MAP_DIR) == 0) {
				snprintf(roots[n++], ENTRY_PATH, "%s/%s", f->flags->res_dirs[k], folders[j]);
				break;
			}
		}
		kp_list_files_free(folders, nf);
	}
	return n;
}

/* A custom map's saves' name: "maps/" and its path below a folder of map_roots (compared without case), else its
 * file's name (StripKnownDirectoryFromMapPath 0x49a130 of the install's and the user's bases). */
static void map_save_name(const KpFlow *f, const char *path, char *out, size_t size)
{
	char roots[CUSTOM_BASES][ENTRY_PATH];
	const char *rest = kp_flow_last_part(path);
	int n = map_roots(f, roots), k;
	for (k = 0; k < n; k++) {
		size_t r = strlen(roots[k]);
		if (prefix_nocase(path, roots[k]) && (path[r] == '/' || path[r] == '\\') && path[r + 1]) {
			rest = path + r + 1;
			break;
		}
	}
	snprintf(out, size, MAP_DIR "/%s", rest);
}

/* The file of a custom map's saves' name (its part after "maps/") as a load finds it (ResolveGameFile 0x45cb90): in
 * the first folder of map_roots that has it, else in the last one (the original keeps the install's path). */
static void map_saved_path(const KpFlow *f, const char *rest, char *out, size_t size)
{
	char roots[CUSTOM_BASES][ENTRY_PATH];
	int n = map_roots(f, roots), k;
	for (k = 0; k < n; k++) {
		snprintf(out, size, "%s/%s", roots[k], rest);
		if (file_there(out)) return;
	}
}

/* --- the map transfer's files (KpTransferIo, phase N stage N3) --- */

#define DOWNLOAD_DIR "download" /* the user's maps\download\ (OpenMapTransferSession 0x477090) */
#define FIND_DEPTH 16           /* Koprulu's bound of FindFileByName 0x499450's recursion (the original's is MAX_PATH's) */

static uint8_t *transfer_read(void *ud, const char *path, size_t *size)
{
	(void)ud;
	return kp_read_file(path, size);
}

/* FileMatchesTransfer 0x440fb0: the file has the offer's size and CRC-32 (its size asked first, so a large file of
 * the name is not read) */
static int file_matches(const char *path, uint32_t size, uint32_t crc)
{
	FILE *fp = fopen(path, "rb");
	long at;
	uint8_t *bytes;
	size_t n = 0;
	int same;
	if (!fp) return 0;
	at = fseek(fp, 0, SEEK_END) == 0 ? ftell(fp) : -1;
	fclose(fp);
	if (at < 0 || (unsigned long)at != size || !(bytes = kp_read_file(path, &n))) return 0;
	same = n == size && kp_transfer_crc(bytes, n) == crc;
	free(bytes);
	return same;
}

/* A file's test while a folder is searched: its path and its name; 1 takes it */
typedef int (*FindMatch)(void *ctx, const char *path, const char *name);

/* EnumerateFiles 0x49aa20 (FindFileByName 0x499450's walk) below dir: the folder's files in FindFirstFileA's order
 * (NTFS's), then its non-hidden subfolders in the same order, each searched the same way; the first file whose path
 * fits out and that match takes. 1 with its path in out. */
static int find_below(const char *dir, FindMatch match, void *ctx, int depth, char *out, size_t out_size)
{
	char **names;
	char path[ENTRY_PATH];
	int n = 0, k, hit = 0;
	names = kp_list_ntfs(dir, 0, &n);
	for (k = 0; k < n && !hit; k++) {
		if (snprintf(path, sizeof path, "%s/%s", dir, names[k]) >= (int)sizeof path || strlen(path) >= out_size) continue;
		if ((hit = match(ctx, path, names[k])) != 0) snprintf(out, out_size, "%s", path);
	}
	kp_list_files_free(names, n);
	if (hit || depth >= FIND_DEPTH) return hit;
	names = kp_list_ntfs(dir, 1, &n);
	for (k = 0; k < n && !hit; k++) {
		if (snprintf(path, sizeof path, "%s/%s", dir, names[k]) >= (int)sizeof path) continue;
		hit = find_below(path, match, ctx, depth + 1, out, out_size);
	}
	kp_list_files_free(names, n);
	return hit;
}

typedef struct {
	const char *name;
	uint32_t size, crc;
} TransferMatch;

/* FileMatchesTransfer 0x440fb0 for the offered name (without case) */
static int transfer_match(void *ctx, const char *path, const char *name)
{
	const TransferMatch *t = (const TransferMatch *)ctx;
	return kp_compare_nocase(name, t->name) == 0 && file_matches(path, t->size, t->crc);
}

/* FindLocalCopy 0x441080: each folder of map_roots in turn (the user's maps, its download folder included, before the
 * installs'). The offered name is windows-1252, the folders' names UTF-8. */
static int transfer_find(void *ud, const char *name, uint32_t size, uint32_t crc, char *out, size_t out_size)
{
	const KpFlow *f = (const KpFlow *)ud;
	char roots[CUSTOM_BASES][ENTRY_PATH], utf8[KP_TRANSFER_NAME * KP_TEXT_UTF8_MAX];
	TransferMatch t;
	int n = map_roots(f, roots), k;
	(void)kp_text_from_legacy(name, utf8, sizeof utf8);
	t.name = utf8;
	t.size = size;
	t.crc = crc;
	for (k = 0; k < n; k++) {
		if (find_below(roots[k], transfer_match, &t, 0, out, out_size)) return 1;
	}
	return 0;
}

/* SaveDownloadedFile 0x4418c0: <user>maps/download/<name>, the folders made when missing; the offered name is
 * windows-1252, the file's UTF-8 (APFS takes no other bytes) */
static int transfer_save(void *ud, const char *name, const uint8_t *data, size_t size, char *out, size_t out_size)
{
	KpFlow *f = (KpFlow *)ud;
	char dir[ENTRY_PATH], utf8[KP_TRANSFER_NAME * KP_TEXT_UTF8_MAX];
	KpError err;
	(void)kp_text_from_legacy(name, utf8, sizeof utf8);
	pref_path(f, MAP_DIR, dir, sizeof dir);
	if (kp_flow_make_dir(dir) != 0) {
		kp_flow_say(f, "map transfer: cannot create %s: %s", dir, strerror(errno));
		return 0;
	}
	if (strlen(dir) + sizeof "/" DOWNLOAD_DIR > sizeof dir) return 0;
	strcat(dir, "/" DOWNLOAD_DIR);
	if (kp_flow_make_dir(dir) != 0) {
		kp_flow_say(f, "map transfer: cannot create %s: %s", dir, strerror(errno));
		return 0;
	}
	if (snprintf(out, out_size, "%s/%s", dir, utf8) >= (int)out_size) return 0;
	if (kp_write_file_atomic(out, data, size, &err) != 0) {
		kp_flow_say(f, "map transfer: %s", err.msg);
		return 0;
	}
	f->host.sync_files(f->host.ctx);
	return 1;
}

KpTransferIo kp_flow_transfer_io(KpFlow *f)
{
	KpTransferIo io;
	memset(&io, 0, sizeof io);
	io.read = transfer_read;
	io.find = transfer_find;
	io.save = transfer_save;
	io.ud = f;
	return io;
}

/* The custom map the next game plays (ADR 0024): its scenario (malloc'd, taken also on failure) under name, read for
 * load; its entry's key "maps/<name>" names its saves. 0, or -1 with why. */
static int custom_from_chk(KpFlow *f, uint8_t *chk, size_t size, const char *name, KpChkLoad load, char *why,
	size_t why_size)
{
	KpError e;
	kp_flow_custom_close(f);
	if (!(f->custom_map = (KpDataMap *)calloc(1, sizeof *f->custom_map))) {
		free(chk);
		snprintf(why, why_size, "out of memory");
		return -1;
	}
	if (kp_data_map_from_chk(f->custom_map, &f->data->files, chk, size, name, &e) != 0) {
		free(f->custom_map);
		f->custom_map = NULL;
		snprintf(why, why_size, "%s", e.msg);
		return -1;
	}
	memset(&f->custom_entry, 0, sizeof f->custom_entry);
	snprintf(f->custom_entry.name, sizeof f->custom_entry.name, "%s", name);
	snprintf(f->custom_entry.key, sizeof f->custom_entry.key, "maps/%s", name);
	f->custom_load = load;
	f->custom_path[0] = 0;
	snprintf(f->custom_key, sizeof f->custom_key, MAP_DIR "/%s", name);
	return 0;
}

/* A malloc'd copy of a replay's CHK (NULL out of memory, which kp_data_map_from_chk reports) and its map's name. */
static uint8_t *replay_chk_copy(const KpRep *rep, char *name, size_t name_size)
{
	uint8_t *chk = (uint8_t *)malloc(rep->chk_size ? rep->chk_size : 1);
	if (chk && rep->chk_size) memcpy(chk, rep->chk, rep->chk_size);
	replay_map_name(rep, name, name_size);
	return chk;
}

/* A copy of a replay's CHK as the custom map the next game plays. */
int kp_flow_custom_from_replay(KpFlow *f, const KpRep *rep, char *why, size_t why_size)
{
	char name[64];
	uint8_t *chk = replay_chk_copy(rep, name, sizeof name);
	return custom_from_chk(f, chk, rep->chk_size, name, replay_load(rep), why, why_size);
}

/* The custom map the next game plays from its file at path (its sounds read from it), read for load. */
int kp_flow_custom_from_file(KpFlow *f, const char *path, const char *name, KpChkLoad load, char *why, size_t why_size)
{
	KpError e;
	kp_flow_custom_close(f);
	if (!(f->custom_map = (KpDataMap *)calloc(1, sizeof *f->custom_map))) {
		snprintf(why, why_size, "out of memory");
		return -1;
	}
	if (kp_data_map_open(f->custom_map, &f->data->files, path, &e) != 0) {
		free(f->custom_map);
		f->custom_map = NULL;
		snprintf(why, why_size, "%s", e.msg);
		return -1;
	}
	memset(&f->custom_entry, 0, sizeof f->custom_entry);
	snprintf(f->custom_entry.name, sizeof f->custom_entry.name, "%s", name);
	snprintf(f->custom_entry.key, sizeof f->custom_entry.key, "maps/%s", name);
	f->custom_load = load;
	snprintf(f->custom_path, sizeof f->custom_path, "%s", path);
	map_save_name(f, path, f->custom_key, sizeof f->custom_key);
	return 0;
}

/* A save's custom map (its MAPC, named mission: "maps/" and its path) as the next game's: the file the name resolves to
 * (map_saved_path) when its scenario is the save's, for its sounds (OpenMapArchive 0x469de0 of the restored path),
 * else the save's scenario alone, its path kept for the next scenario (CreateNextScenarioGame 0x4bb360 takes the
 * restored path's folder). map's scenario is taken. 0, or -1 with why. */
int kp_flow_custom_from_save(KpFlow *f, const char *mission, KpSaveMap *map, char *why, size_t why_size)
{
	const char *rest = strncmp(mission, MAP_DIR "/", sizeof MAP_DIR) == 0 ? mission + sizeof MAP_DIR : mission;
	char path[ENTRY_PATH];
	uint8_t *chk = NULL;
	size_t size = 0;
	KpError e;
	map_saved_path(f, rest, path, sizeof path);
	if (kp_flow_custom_from_file(f, path, kp_flow_last_part(rest), map->load, why, why_size) == 0
		&& (chk = kp_data_map_chk(f->custom_map, &size, &e)) != NULL && size == map->chk_size
		&& memcmp(chk, map->chk, size) == 0) {
		free(chk);
		free(map->chk);
		map->chk = NULL;
	} else {
		free(chk);
		if (custom_from_chk(f, map->chk, map->chk_size, kp_flow_last_part(rest), map->load, why, why_size) != 0) {
			map->chk = NULL;
			return -1;
		}
		map->chk = NULL;
		snprintf(f->custom_path, sizeof f->custom_path, "%s", path);
	}
	snprintf(f->custom_key, sizeof f->custom_key, "%s", mission);
	return 0;
}

/* Whether a custom replay's map loads as its game reads it (its mission built, then let go). */
static int custom_replay_loads(KpFlow *f, const KpRep *rep, char *why, size_t why_size)
{
	KpDataMap map;
	KpMission m;
	KpError e;
	char name[64];
	uint8_t *chk = replay_chk_copy(rep, name, sizeof name);
	int rc;
	if (kp_data_map_from_chk(&map, &f->data->files, chk, rep->chk_size, name, &e) != 0) {
		snprintf(why, why_size, "%s", e.msg);
		return 0;
	}
	rc = kp_data_map_mission(&map, replay_load(rep), &m, &e);
	if (rc == 0) kp_mission_free(&m);
	else snprintf(why, why_size, "%s", e.msg);
	kp_data_map_close(&map);
	return rc == 0;
}

/* Why a replay's playback is unverified (KP_UI_REPLAY_*): another simulation revision first (its commands may play
 * another game), then other data, then saves loaded during the game. A file without Koprulu's tail (the original's) is
 * not marked. */
static uint8_t replay_unverified(const KpFlow *f, const KpRepTail *t)
{
	if (t == NULL) return KP_UI_REPLAY_VERIFIED;
	if (t->sim_revision != KP_REP_SIM_REVISION) return KP_UI_REPLAY_REVISION;
	if (f->identity && memcmp(t->data_hash, f->identity->source_hash, sizeof t->data_hash) != 0) return KP_UI_REPLAY_DATA;
	return t->loads > 0 ? KP_UI_REPLAY_LOADS : KP_UI_REPLAY_VERIFIED;
}

/* SortedInsertEntry 0x45b3f0's order of two folders: their display names "[name]" by _stricmp (ASCII folding). */
static int compare_folders(const void *pa, const void *pb)
{
	const char *a = *(const char *const *)pa, *b = *(const char *const *)pb;
	for (;; a++, b++) {
		int x = *a ? (unsigned char)*a : ']', y = *b ? (unsigned char)*b : ']';
		if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
		if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
		if (x != y) return x - y;
		if (!*a || !*b) return 0; /* both at their ']' */
	}
}

/* A listed name the list can hold: UTF-8 within the entry's buffer. */
static int listable(const char *name, size_t size) { return strlen(name) < size && kp_text_valid_utf8((const uint8_t *)name, strlen(name)); }

/* The folders the screen's listed folder is in, as ScanMapDirectory 0x45c9a0 scans the user base before the install
 * (BuildMapList 0x45ba40): Load Replay's the replays folder; Play Custom's below its root's "replays" the replays folder
 * (the original's maps\replays), else the user's maps folder, then each data folder's Maps. Their count. */
static int entry_bases(const KpFlow *f, const KpUiMenus *m, char bases[CUSTOM_BASES][ENTRY_PATH])
{
	const char *dir = m->map_dir, *rest = dir;
	size_t r = sizeof CUSTOM_REPLAYS - 1;
	char root[ENTRY_PATH];
	int n = 0, k;
	if (m->state != KP_GLUE_PLAY_CUSTOM
		|| (prefix_nocase(dir, CUSTOM_REPLAYS) && (dir[r] == 0 || dir[r] == '/'))) {
		if (m->state == KP_GLUE_PLAY_CUSTOM) rest = dir[r] == '/' ? dir + r + 1 : dir + r;
		pref_path(f, REPLAY_DIR, root, sizeof root);
		snprintf(bases[n++], ENTRY_PATH, rest[0] ? "%s/%s" : "%s%s", root, rest);
		return n;
	}
	n = map_roots(f, bases);
	for (k = 0; dir[0] && k < n; k++) {
		snprintf(root, sizeof root, "%s", bases[k]);
		snprintf(bases[k], ENTRY_PATH, "%s/%s", root, dir);
	}
	return n;
}

/* The path of entry index's file (its folder's base): 1, or 0 for no such entry or a path past out. */
int kp_flow_entry_path(const KpFlow *f, const KpUiMenus *m, int index, char *out, size_t size)
{
	char bases[CUSTOM_BASES][ENTRY_PATH];
	int n;
	if (index < 0 || index >= m->entry_count) return 0;
	n = entry_bases(f, m, bases);
	if (f->entry_base[index] >= n) return 0;
	return snprintf(out, size, "%s/%s", bases[f->entry_base[index]], m->entries[index].file) < (int)size;
}

/* A name found in a base: the first base's of a name (the user's: InsertMapEntrySorted 0x45b3f0's table keeps the
 * first) */
typedef struct {
	const char *name;
	uint8_t base;
} Listed;

static int compare_listed_folders(const void *a, const void *b)
{
	const Listed *x = (const Listed *)a, *y = (const Listed *)b;
	return compare_folders(&x->name, &y->name);
}

static int compare_listed_files(const void *a, const void *b)
{
	return kp_compare_nocase(((const Listed *)a)->name, ((const Listed *)b)->name);
}

/* name added to list (at most KP_UI_MAP_ENTRIES) unless a name of it is there in any case: its count */
static int add_listed(Listed *list, int n, const char *name, int base)
{
	int k;
	for (k = 0; k < n; k++) {
		if (kp_compare_nocase(list[k].name, name) == 0) return n;
	}
	if (n == KP_UI_MAP_ENTRIES) return n;
	list[n].name = name;
	list[n].base = (uint8_t)base;
	return n + 1;
}

/* A replay entry as LoadReplayEntryInfo 0x45d250 reads it (the file at path). */
static void replay_entry(KpFlow *f, KpUiMapEntry *e, const char *path)
{
	KpRep rep;
	KpRepPlayer p;
	char map[KP_REP_MAP_NAME_SIZE + 1];
	int j;
	e->kind = KP_UI_ENTRY_REPLAY;
	if (kp_flow_read_replay_at(path, &rep, &e->description) != 0) {
		e->status = e->description == STR_MISSING || e->description == STR_NAME_TOO_LONG ? 1 : 2;
		return;
	}
	memcpy(map, rep.header + KP_REP_MAP_NAME, KP_REP_MAP_NAME_SIZE);
	map[KP_REP_MAP_NAME_SIZE] = 0;
	(void)kp_text_from_legacy(map, e->map, sizeof e->map);
	e->width = (uint16_t)kp_le_u16(rep.header + KP_REP_MAP_SIZE);
	e->height = (uint16_t)kp_le_u16(rep.header + KP_REP_MAP_SIZE + 2);
	e->tileset = (uint16_t)kp_le_u16(rep.header + KP_REP_TILESET);
	e->players = KP_REP_LIST_PLAYERS;
	e->slots = rep.header[KP_REP_SLOTS];
	e->game_type = (uint16_t)kp_le_u16(rep.header + KP_REP_TEMPLATE + KP_TEMPLATE_TYPE);
	e->game_subtype = (uint16_t)kp_le_u16(rep.header + KP_REP_TEMPLATE + KP_TEMPLATE_SUBTYPE);
	e->unverified = replay_unverified(f, rep.tail);
	for (j = 0; j < KP_REP_PLAYERS; j++) { /* the records' computers and humans (their counts are hidden offline) */
		kp_rep_player(&rep, j, &p);
		if (p.type == KP_CTRL_COMPUTER) e->computers++;
		else if (p.type == KP_CTRL_HUMAN) e->humans++;
	}
	kp_rep_free(&rep);
}

/* ListMapDirectory 0x45bc70 over the screen's folders (entry_bases; InitMapList 0x455aa0 makes the root): below the
 * root the folder above first (AddFolderEntry 0x45b080's ".."), the folders (Play Custom's root with its "replays"),
 * then the files: Load Replay's replays, Play Custom's maps (.scm; .scx with Brood War), replays and, on Create Game's,
 * the saved network games (listed_saves). A replay's header is read here; a map's or a saved game's when the screen
 * first needs it (load_entry). */
static int list_entries(void *host, KpUi *ui)
{
	KpFlow *f = (KpFlow *)host;
	KpUiMenus *m = &ui->menus;
	int custom = m->state == KP_GLUE_PLAY_CUSTOM, nb, nl = 0, nf = 0, nn = 0, b, k, x, exts = 0;
	int saves = custom && listed_saves(m);
	const char *ext[4];
	char bases[CUSTOM_BASES][ENTRY_PATH], root[ENTRY_PATH], path[ENTRY_PATH];
	char **lists[CUSTOM_BASES * 5];
	int counts[CUSTOM_BASES * 5];
	Listed *folders = (Listed *)calloc(KP_UI_MAP_ENTRIES, sizeof *folders);
	Listed *files = (Listed *)calloc(KP_UI_MAP_ENTRIES, sizeof *files);
	m->entry_count = 0;
	if (!folders || !files) {
		free(folders);
		free(files);
		return 0;
	}
	if (custom) {
		ext[exts++] = ".scm";
		ext[exts++] = ".scx";
	}
	ext[exts++] = REPLAY_EXT;
	if (saves) ext[exts++] = save_ext(m);
	pref_path(f, REPLAY_DIR, root, sizeof root);
	(void)kp_flow_make_dir(root);
	if (custom) {
		pref_path(f, MAP_DIR, root, sizeof root);
		(void)kp_flow_make_dir(root);
	}
	nb = entry_bases(f, m, bases);
	if (custom && m->map_dir[0] == 0) nf = add_listed(folders, nf, CUSTOM_REPLAYS, 0);
	for (b = 0; b < nb; b++) {
		lists[nl] = kp_list_folders(bases[b], &counts[nl]);
		for (k = 0; k < counts[nl]; k++) nf = add_listed(folders, nf, lists[nl][k], b);
		nl++;
		for (x = 0; x < exts; x++) {
			if (strcmp(ext[x], ".scx") == 0 && !m->expansion) continue; /* ScanMapDirectory: scx with Brood War */
			lists[nl] = kp_list_files(bases[b], ext[x], &counts[nl]);
			for (k = 0; k < counts[nl]; k++) nn = add_listed(files, nn, lists[nl][k], b);
			nl++;
		}
	}
	if (nf > 1) qsort(folders, (size_t)nf, sizeof *folders, compare_listed_folders);
	if (nn > 1) qsort(files, (size_t)nn, sizeof *files, compare_listed_files);
	if (m->map_dir[0] != 0) {
		memset(&m->entries[0], 0, sizeof m->entries[0]);
		f->entry_base[0] = 0;
		m->entries[m->entry_count++].kind = KP_UI_ENTRY_UP;
	}
	for (k = 0; k < nf && m->entry_count < KP_UI_MAP_ENTRIES; k++) {
		KpUiMapEntry *e = &m->entries[m->entry_count];
		if (!listable(folders[k].name, sizeof e->file)) continue;
		memset(e, 0, sizeof *e);
		e->kind = KP_UI_ENTRY_FOLDER;
		snprintf(e->file, sizeof e->file, "%s", folders[k].name);
		f->entry_base[m->entry_count++] = folders[k].base;
	}
	for (k = 0; k < nn && m->entry_count < KP_UI_MAP_ENTRIES; k++) {
		KpUiMapEntry *e = &m->entries[m->entry_count];
		if (!listable(files[k].name, sizeof e->file)) continue;
		memset(e, 0, sizeof *e);
		snprintf(e->file, sizeof e->file, "%s", files[k].name);
		f->entry_base[m->entry_count] = files[k].base;
		e->kind = has_replay_ext(e->file) ? KP_UI_ENTRY_REPLAY
			: saves && has_ext(e->file, save_ext(m)) ? KP_UI_ENTRY_SAVE : KP_UI_ENTRY_MAP;
		e->first_human = -1;
		if (e->kind == KP_UI_ENTRY_REPLAY) {
			if (snprintf(path, sizeof path, "%s/%s", bases[files[k].base], e->file) < (int)sizeof path) {
				replay_entry(f, e, path);
			} else {
				e->status = 1;
				e->description = STR_NAME_TOO_LONG;
			}
		}
		m->entry_count++;
	}
	for (k = 0; k < nl; k++) kp_list_files_free(lists[k], counts[k]);
	free(folders);
	free(files);
	return m->entry_count;
}

/* LoadMapHeader 0x46b5c0 of the map at path for the map lists: 0, or -1 with why (h->version the VER read, 0 none). */
static int map_header_at(KpFlow *f, const char *path, KpMapHeader *h, char *why, size_t why_size)
{
	KpDataMap map;
	KpError e;
	uint8_t *chk;
	size_t size;
	int rc;
	memset(h, 0, sizeof *h);
	if (kp_data_map_open(&map, &f->data->files, path, &e) != 0) {
		snprintf(why, why_size, "%s", e.msg);
		return -1;
	}
	if (!(chk = kp_data_map_chk(&map, &size, &e))) {
		snprintf(why, why_size, "%s", e.msg);
		kp_data_map_close(&map);
		return -1;
	}
	rc = kp_map_header(h, chk, size, f->data->files.profile, f->ui->menus.expansion, why, why_size);
	free(chk);
	kp_data_map_close(&map);
	return rc;
}

/* A file's name after its folder in the original's encoding (a saved game's display name is made of it) */
static void legacy_file(const char *path, char *out, size_t size)
{
	const char *base = strrchr(path, '/');
	(void)kp_text_to_legacy(base ? base + 1 : path, out, size);
}

/* A saved game's file (its size bounded by KP_NETSAVE_FILE_MAX): its bytes (malloc'd), NULL when it does not read */
uint8_t *kp_flow_read_save_file(const char *path, size_t *size)
{
	FILE *fp = fopen(path, "rb");
	long at;
	if (!fp) return NULL;
	at = fseek(fp, 0, SEEK_END) == 0 ? ftell(fp) : -1;
	fclose(fp);
	if (at < 0 || (unsigned long)at > KP_NETSAVE_FILE_MAX) return NULL;
	return kp_read_file(path, size);
}

/* LoadSaveEntryInfo 0x45d5e0 of the saved game entry e at path (NULL: its path too long): its status, title and the
 * gluAll text of an unreadable one (66 missing or unreadable, 65 its name too long, 67 another version), else the
 * header's information */
static void load_save_entry(const KpFlow *f, KpUiMapEntry *e, const char *path)
{
	KpNetGameHeader h;
	char name[KP_NETSAVE_NAME], file[KP_UI_MAP_FILE];
	uint8_t *bytes;
	size_t size = 0;
	int rc;
	FILE *probe;
	e->status = 1;
	e->save_title = STR_INVALID_SAVE;
	e->description = STR_MISSING;
	if (path && !(probe = fopen(path, "rb"))) return;
	if (path) fclose(probe);
	e->save_title = STR_SAVED_GAME;
	if (!path || !entry_name_fits(path)) {
		e->description = STR_SAVE_NAME_TOO_LONG;
		return;
	}
	legacy_file(path, file, sizeof file);
	rc = (bytes = kp_flow_read_save_file(path, &size)) != NULL ? read_net_save(f, bytes, size, file, &h, name, NULL)
		: KP_NETSAVE_UNREADABLE;
	free(bytes);
	if (rc != KP_NETSAVE_VALID) {
		e->description = rc == KP_NETSAVE_OTHER_VERSION ? STR_SAVE_VERSION : STR_MISSING;
		return;
	}
	(void)kp_text_from_legacy(h.title, e->map, sizeof e->map);
	(void)kp_text_from_legacy(name, e->text, sizeof e->text);
	e->game_players = h.players;
	e->players = h.max_players;
	e->width = h.width;
	e->height = h.height;
	e->tileset = h.tileset;
	e->status = 0;
}

/* LoadMapEntryInfo 0x45d870 of the map entry index: its status and the gluAll text of an unreadable one (66 missing,
 * 62 its name too long, 60 a later version's, 63 another refusal), else the header's information; a saved game's by
 * load_save_entry. */
static void load_entry(void *host, KpUi *ui, int index)
{
	KpFlow *f = (KpFlow *)host;
	KpUiMenus *m = &ui->menus;
	KpUiMapEntry *e;
	KpMapHeader h;
	char path[ENTRY_PATH], why[KP_ERROR_SIZE], title[KP_UI_MAP_NAME];
	FILE *probe;
	if (index >= 0 && index < m->entry_count && m->entries[index].kind == KP_UI_ENTRY_SAVE) {
		load_save_entry(f, &m->entries[index], kp_flow_entry_path(f, m, index, path, sizeof path) ? path : NULL);
		return;
	}
	if (index < 0 || index >= m->entry_count || m->entries[index].kind != KP_UI_ENTRY_MAP) return;
	e = &m->entries[index];
	e->status = 1;
	if (!kp_flow_entry_path(f, m, index, path, sizeof path)) {
		e->description = STR_NAME_TOO_LONG;
		return;
	}
	if (!(probe = fopen(path, "rb"))) {
		e->description = STR_MISSING;
		return;
	}
	fclose(probe);
	if (!entry_name_fits(path)) {
		e->description = STR_NAME_TOO_LONG;
		return;
	}
	if (map_header_at(f, path, &h, why, sizeof why) != 0) {
		kp_flow_say(f, "map %s: %s", e->file, why);
		e->status = h.version > MAP_ORIGINAL_VERSION ? 2 : 1;
		e->description = h.version > MAP_ORIGINAL_VERSION ? STR_EXPANSION : STR_DAMAGED;
		return;
	}
	(void)kp_text_from_legacy(h.title, title, sizeof title);
	if (title[0] == 0) snprintf(title, sizeof title, "%s", e->file); /* the file's name for no title */
	(void)kp_text_copy_chars(e->map, sizeof e->map, title, KP_UI_MAP_TITLE_CHARS);
	(void)kp_text_from_legacy(h.description, e->text, sizeof e->text);
	e->width = h.width;
	e->height = h.height;
	e->tileset = h.tileset;
	e->players = h.players;
	e->computers = h.computers;
	e->humans = h.humans;
	memcpy(e->user_select, h.user_select, sizeof e->user_select);
	e->first_human = h.first_human;
	e->status = 0;
}

int kp_flow_entry_map(KpFlow *f, int index, KpMapHeader *h, char *path, size_t size)
{
	const KpUiMenus *m = &f->ui->menus;
	char why[KP_ERROR_SIZE];
	if (index < 0 || index >= m->entry_count || m->entries[index].kind != KP_UI_ENTRY_MAP) return 0;
	if (!kp_flow_entry_path(f, m, index, path, size)) return 0;
	if (map_header_at(f, path, h, why, sizeof why) != 0) {
		kp_flow_say(f, "map %s: %s", m->entries[index].file, why);
		return 0;
	}
	return 1;
}

int kp_flow_entry_save(KpFlow *f, int index, KpNetGameHeader *h)
{
	const KpUiMenus *m = &f->ui->menus;
	char path[ENTRY_PATH], file[KP_UI_MAP_FILE];
	uint8_t *bytes;
	size_t size = 0;
	int rc;
	if (index < 0 || index >= m->entry_count || m->entries[index].kind != KP_UI_ENTRY_SAVE) return 0;
	if (!kp_flow_entry_path(f, m, index, path, sizeof path) || !(bytes = kp_flow_read_save_file(path, &size))) return 0;
	legacy_file(path, file, sizeof file);
	rc = read_net_save(f, bytes, size, file, h, NULL, NULL);
	free(bytes);
	return rc == KP_NETSAVE_VALID;
}

static uint16_t check_replay(void *host, int index);

int kp_flow_entry_replay(KpFlow *f, int index, KpNetReplayEntry *out)
{
	const KpUiMenus *m = &f->ui->menus;
	char why[KP_ERROR_SIZE];
	uint16_t refusal = 0;
	KpRep rep;
	int ok;
	memset(out, 0, sizeof *out);
	/* TODO: Create Game lists the maps folders only, not <pref>replays/ where Koprulu writes its replays (1.17 keeps
	 * them in its maps folder): a replay to host has to be copied there */
	if (check_replay(f, index) != 0 || !kp_flow_entry_path(f, m, index, out->path, sizeof out->path)) return 0;
	if (kp_flow_read_replay_at(out->path, &rep, &refusal) != 0) return 0;
	/* Koprulu's guard: a replay without a lobby's start information (a single player's: the record's command byte 0)
	 * would send a start no seat takes (ExecuteLobbyCommands 0x476650 stops at the id), the lobby then waiting on */
	if (rep.header[KP_REP_SEED_RECORD] != KP_LOBBY_START_INFO_MSG) {
		kp_flow_say(f, "replay %s: no network game's, it cannot be hosted", m->entries[index].file);
		kp_rep_free(&rep);
		return 0;
	}
	memcpy(out->game, rep.header + KP_REP_GAME, KP_REP_GAME_SIZE);
	memcpy(out->seed_record, rep.header + KP_REP_SEED_RECORD, sizeof out->seed_record);
	ok = kp_map_header(&out->map, rep.chk, rep.chk_size, f->data->files.profile, f->ui->menus.expansion, why,
		sizeof why) == 0;
	if (!ok) kp_flow_say(f, "replay %s: %s", m->entries[index].file, why);
	kp_rep_free(&rep);
	return ok;
}

typedef struct {
	const KpFlow *f;
	const char *ext;
	uint32_t id;
	char name[KP_NETSAVE_NAME]; /* the display name of the save found */
} SaveMatch;

/* MatchSaveIdCallback 0x499690: a save of the extension whose header (ValidateSaveFile 0x499750) has the ID */
static int save_id_match(void *ctx, const char *path, const char *name)
{
	SaveMatch *s = (SaveMatch *)ctx;
	KpNetGameHeader h;
	char file[KP_UI_MAP_FILE];
	uint8_t *bytes;
	size_t size = 0;
	int rc;
	if (!has_ext(name, s->ext) || !(bytes = kp_flow_read_save_file(path, &size))) return 0;
	legacy_file(path, file, sizeof file);
	rc = read_net_save(s->f, bytes, size, file, &h, s->name, NULL);
	free(bytes);
	return rc == KP_NETSAVE_VALID && h.save_id == s->id;
}

/* LoadSavedGameByName 0x498bd0's callback 0x4994b0: a file named the target (without case, in the original's
 * encoding) */
static int save_name_match(void *ctx, const char *path, const char *name)
{
	char file[KP_UI_MAP_FILE];
	(void)name;
	legacy_file(path, file, sizeof file);
	return kp_compare_nocase(file, (const char *)ctx) == 0;
}

int kp_flow_find_save(KpFlow *f, uint32_t id, KpNetGameSave *out)
{
	const KpUiMenus *m = &f->ui->menus;
	SaveMatch match;
	char dir[ENTRY_PATH], path[ENTRY_PATH], target[KP_NETSAVE_NAME + 8];
	uint8_t *bytes;
	size_t size = 0;
	int ok;
	f->net_load[0] = 0;
	if (!listed_saves(m)) return 0;
	memset(&match, 0, sizeof match);
	match.f = f;
	match.ext = save_ext(m);
	match.id = id;
	pref_path(f, MAP_DIR, dir, sizeof dir);
	if (!find_below(dir, save_id_match, &match, 0, path, sizeof path)) {
		kp_flow_say(f, "network: no saved game %08x below %s", (unsigned)id, dir);
		return 0;
	}
	/* the name and the extension, 31 characters at most (0x4995a0) */
	snprintf(target, sizeof target, "%s%s", match.name, match.ext);
	target[SAVE_TARGET_MAX] = 0;
	if (!find_below(dir, save_name_match, target, 0, path, sizeof path) || !(bytes = kp_flow_read_save_file(path, &size))) {
		kp_flow_say(f, "network: the saved game %s does not open", target);
		return 0;
	}
	legacy_file(path, target, sizeof target);
	ok = read_net_save(f, bytes, size, target, NULL, NULL, out) == KP_NETSAVE_VALID;
	free(bytes);
	if (!ok) kp_flow_say(f, "network: the saved game %s does not read", path);
	else snprintf(f->net_load, sizeof f->net_load, "%s", path); /* the file the game loads (0x6a5e34 kept open) */
	return ok;
}

/* The maps' signature key, read from the 1.17 install's executable at its first need (kp_data_map_key; the reason
 * there is none said once), NULL without one. */
static const uint8_t *map_key(KpFlow *f)
{
	if (f->map_key_state == 0) {
		char why[KP_ERROR_SIZE];
		f->map_key_state = kp_data_map_key(f->data, f->map_key, why, sizeof why) ? 1 : -1;
		if (f->map_key_state < 0 && why[0]) kp_flow_say(f, "maps' class: no signature key: %s", why);
	}
	return f->map_key_state > 0 ? f->map_key : NULL;
}

/* OpenMapArchive 0x469de0's class of map entry index (a signed map's certificate, kp_map_class.h), asked by the
 * interface only for the templates that read it (ValidateGameTemplate 0x45b700's Ladder, Tournament and Server, none
 * offered offline). */
static uint32_t map_class(void *host, KpUi *ui, int index)
{
	KpFlow *f = (KpFlow *)host;
	char path[ENTRY_PATH];
	if (index < 0 || index >= ui->menus.entry_count || !kp_flow_entry_path(f, &ui->menus, index, path, sizeof path)) return 0;
	return kp_map_class(path, map_key(f));
}

/* CreateGameFromEntry 0x45bfc0's reading of the map at Ok (its header again, the player in its first open slot), and
 * Koprulu's refusal of a map its game does not load (gluAll 51, as an invalid scenario). */
static uint16_t check_map(void *host, int index, int *local_slot)
{
	KpFlow *f = (KpFlow *)host;
	const KpUiMenus *m = &f->ui->menus;
	KpMapHeader h;
	KpDataMap map;
	KpMission mission;
	KpError e;
	char path[ENTRY_PATH], why[KP_ERROR_SIZE];
	int rc;
	*local_slot = -1;
	if (index < 0 || index >= m->entry_count || m->entries[index].kind != KP_UI_ENTRY_MAP) return STR_NOT_VALID;
	if (!kp_flow_entry_path(f, m, index, path, sizeof path)) return STR_NOT_VALID;
	if (map_header_at(f, path, &h, why, sizeof why) != 0) {
		kp_flow_say(f, "map %s: %s", m->entries[index].file, why);
		return h.version > MAP_ORIGINAL_VERSION ? STR_EXPANSION : STR_NOT_VALID;
	}
	/* PlaceLocalHumanForCustomGame 0x4bb550's 0x80000007 (offline only: a network game's lobby places its players) */
	if (h.first_human < 0 && !m->multiplayer) return STR_NOT_VALID;
	if (kp_data_map_open(&map, &f->data->files, path, &e) != 0) {
		kp_flow_say(f, "map %s: %s", m->entries[index].file, e.msg);
		return STR_NOT_VALID;
	}
	rc = kp_data_map_mission(&map, kp_flow_custom_load_of(m->create.record), &mission, &e);
	kp_data_map_close(&map);
	if (rc != 0) {
		kp_flow_say(f, "map %s: refused: %s", m->entries[index].file, e.msg);
		return STR_NOT_VALID;
	}
	kp_mission_free(&mission);
	*local_slot = h.first_human;
	return 0;
}

/* CreateGameFromEntry 0x45bfc0's reading of the replay at Ok, and Koprulu's refusals of a replay it cannot play (gluAll
 * 51, as an invalid scenario): a campaign row the catalog does not have, a custom map its game does not load, a
 * network game's command. A campaign replay's CHK is compared with the mission's when the game starts (a web build may
 * not have the mission's files yet). */
static uint16_t check_replay(void *host, int index)
{
	KpFlow *f = (KpFlow *)host;
	const KpUiMenus *m = &f->ui->menus;
	KpRep rep;
	KpSessionStart st;
	uint16_t refusal = 0;
	char rel[ENTRY_PATH], why[KP_ERROR_SIZE];
	int k;
	if (index < 0 || index >= m->entry_count || m->entries[index].kind != KP_UI_ENTRY_REPLAY) return STR_NOT_VALID;
	if (!kp_flow_entry_path(f, m, index, rel, sizeof rel)) return STR_NOT_VALID;
	if (kp_flow_read_replay_at(rel, &rep, &refusal) != 0) {
		/* CreateGameFromEntry's status test: an unreadable entry (status 1) is gluAll 51, a refused read (2) 60 */
		return refusal == STR_MISSING || refusal == STR_NAME_TOO_LONG ? STR_NOT_VALID : STR_EXPANSION;
	}
	kp_session_start_defaults(&st);
	k = kp_flow_replay_mission(f, &rep);
	if (k == -1 || kp_session_replay_start(&st, &rep) != 0) {
		kp_flow_say(f, "replay %s: refused (outside version 1)", rel);
		refusal = STR_NOT_VALID;
	} else if (k == KP_FLOW_CUSTOM && !custom_replay_loads(f, &rep, why, sizeof why)) {
		kp_flow_say(f, "replay %s: refused: %s", rel, why);
		refusal = STR_NOT_VALID;
	}
	kp_rep_free(&rep);
	return refusal;
}

/* --- Import (Load Replay's and Play Custom's) and Load Replay's Export (Koprulu's: the platform's file picker) --- */

#define IMPORT_REPLAY_DEFAULT "Replay"
#define IMPORT_MAP_DEFAULT "Map"
#define IMPORT_COPIES 99 /* " (2)" .. " (99)" */
#define MAP_EXT ".scm"
#define MAP_EXPANSION_EXT ".scx"

static int hex_digit(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* The name a stem, a copy's number and ext give, the stem cut at a character's start until the whole name fits out and
 * its legacy form is below the original's 32 bytes (the legacy form leaves out what windows-1252 lacks, so a long stem
 * of such characters is short there alone). */
static void import_candidate(const char *stem, int copy, const char *ext, char *out, size_t size)
{
	char cut[KP_UI_MAP_FILE], suffix[16], legacy[KP_UI_MAP_FILE * 2];
	size_t n;
	if (copy > 1) snprintf(suffix, sizeof suffix, " (%d)", copy);
	else suffix[0] = 0;
	snprintf(cut, sizeof cut, "%s", stem);
	for (n = strlen(cut);; ) {
		int w = snprintf(out, size, "%s%s%s", cut, suffix, ext);
		if (n == 0 || (w > 0 && (size_t)w < size && kp_text_to_legacy(out, legacy, sizeof legacy) < REPLAY_NAME_LIMIT)) {
			return;
		}
		do n--; while (n > 0 && ((unsigned char)cut[n] & 0xc0) == 0x80);
		while (n > 0 && (cut[n - 1] == ' ' || cut[n - 1] == '.')) n--; /* no space or dot left before the suffix */
		cut[n] = 0;
	}
}

/* The picked file's stem: its last part, a URI's escapes undone, the first of exts it ends in (in any case) left out,
 * what no file system takes made '_', else fallback. The index of the extension left out, or -1. */
static int import_stem(const char *source, const char *const *exts, int ext_count, const char *fallback, char *stem,
	size_t size)
{
	char decoded[1100];
	const char *p = kp_flow_last_part(source), *name;
	size_t n = 0, len;
	int ext = -1, k;
	for (; *p && n + 1 < sizeof decoded; p++) { /* a URI's escapes ("%3A", "%2F") undone */
		int hi = p[0] == '%' ? hex_digit(p[1]) : -1, lo = hi >= 0 ? hex_digit(p[2]) : -1;
		if (lo >= 0) {
			decoded[n++] = (char)(hi << 4 | lo);
			p += 2;
		} else {
			decoded[n++] = *p;
		}
	}
	decoded[n] = 0;
	name = kp_flow_last_part(decoded);
	len = strlen(name);
	for (k = 0; k < ext_count && ext < 0; k++) {
		if (has_ext(name, exts[k])) {
			ext = k;
			len -= strlen(exts[k]);
		}
	}
	for (n = 0; n < len && n + 1 < size; n++) { /* what no file system takes, and the controls */
		unsigned char c = (unsigned char)name[n];
		stem[n] = c < 0x20 || strchr("<>:\"/\\|?*", c) ? '_' : (char)c;
	}
	while (n > 0 && n < len && ((unsigned char)name[n] & 0xc0) == 0x80) n--; /* a cut name ends at a character's start */
	stem[n] = 0;
	while (n > 0 && (stem[n - 1] == ' ' || stem[n - 1] == '.')) stem[--n] = 0; /* Windows drops them */
	if (n == 0 || !kp_text_valid_utf8((const uint8_t *)stem, n)) snprintf(stem, size, "%s", fallback);
	return ext;
}

static const char *const REPLAY_EXTS[] = { REPLAY_EXT };
static const char *const MAP_EXTS[] = { MAP_EXT, MAP_EXPANSION_EXT };

void kp_flow_replay_import_name(const char *source, char *out, size_t size)
{
	char stem[KP_UI_MAP_FILE];
	(void)import_stem(source, REPLAY_EXTS, 1, IMPORT_REPLAY_DEFAULT, stem, sizeof stem);
	import_candidate(stem, 1, REPLAY_EXT, out, size);
}

/* The folder an import copies into, made level by level below the preferences' folder: the listed folder's user base
 * (entry_bases' first: Load Replay's in the replays folder, Play Custom's in the user's maps folder or, below its
 * "replays", the replays folder), else root's. 0, or -1. */
static int import_dir(KpFlow *f, int custom, const char *root, char *dir, size_t size)
{
	const KpUiMenus *m = f->ui ? &f->ui->menus : NULL;
	size_t k, pref = strlen(f->pref);
	int ok = 1;
	if (m && (m->state == KP_GLUE_PLAY_CUSTOM) == custom) {
		char bases[CUSTOM_BASES][ENTRY_PATH];
		(void)entry_bases(f, m, bases);
		snprintf(dir, size, "%s", bases[0]);
	} else {
		pref_path(f, root, dir, size);
	}
	for (k = pref; dir[k] && ok; k++) {
		if (dir[k] != '/') continue;
		dir[k] = 0;
		ok = kp_flow_make_dir(dir) == 0;
		dir[k] = '/';
	}
	return ok && kp_flow_make_dir(dir) == 0 ? 0 : -1;
}

/* The picked bytes copied into dir under the first free name of stem and ext (path its file): 0 with the name in file,
 * else -1 said. */
static int import_copy(KpFlow *f, const char *what, const char *name, const char *dir, const char *stem, const char *ext,
	const uint8_t *bytes, size_t size, char *file, size_t file_size, char *path, size_t path_size)
{
	KpError err;
	int copy;
	for (copy = 1; copy <= IMPORT_COPIES; copy++) {
		import_candidate(stem, copy, ext, file, file_size);
		snprintf(path, path_size, "%s/%s", dir, file);
		if (!file_there(path)) break;
	}
	if (copy > IMPORT_COPIES || kp_write_file_atomic(path, bytes, size, &err) != 0) {
		kp_flow_say(f, "%s import %s: %s", what, name, copy > IMPORT_COPIES ? "no free name" : err.msg);
		return -1;
	}
	return 0;
}

int kp_flow_import_replay(KpFlow *f, const char *name, const uint8_t *bytes, size_t size, char *out, size_t out_size)
{
	char stem[KP_UI_MAP_FILE], file[KP_UI_MAP_FILE], dir[ENTRY_PATH], path[ENTRY_PATH + KP_UI_MAP_FILE];
	KpRep rep;
	KpRepRefusal why = KP_REP_OK;
	KpError err;
	if (out && out_size) out[0] = 0;
	if (kp_rep_read(&rep, bytes, size, &why, &err) != 0) {
		kp_rep_free(&rep);
		kp_flow_say(f, "replay import %s: refused: %s", name, err.msg);
		if (f->ui) kp_ui_file_imported(f->ui, NULL, why == KP_REP_EXPANSION ? STR_EXPANSION : STR_DAMAGED);
		return -1;
	}
	kp_rep_free(&rep);
	(void)import_stem(name, REPLAY_EXTS, 1, IMPORT_REPLAY_DEFAULT, stem, sizeof stem);
	if (import_dir(f, 0, REPLAY_DIR, dir, sizeof dir) != 0
		|| import_copy(f, "replay", name, dir, stem, REPLAY_EXT, bytes, size, file, sizeof file, path, sizeof path) != 0) {
		if (f->ui) kp_ui_file_imported(f->ui, NULL, STR_NOT_VALID);
		return -1;
	}
	f->host.sync_files(f->host.ctx);
	kp_flow_say(f, "replay import %s: %s", name, path);
	if (out) snprintf(out, out_size, "%s", file);
	if (f->ui) kp_ui_file_imported(f->ui, file, 0);
	return 0;
}

int kp_flow_import_map(KpFlow *f, const char *name, const uint8_t *bytes, size_t size, char *out, size_t out_size)
{
	char stem[KP_UI_MAP_FILE], file[KP_UI_MAP_FILE], dir[ENTRY_PATH], path[ENTRY_PATH + KP_UI_MAP_FILE];
	char why[KP_ERROR_SIZE];
	const char *ext;
	KpMapHeader h;
	int at;
	if (out && out_size) out[0] = 0;
	if (!f->ui) return -1; /* the screen's expansion byte reads the header */
	at = import_stem(name, MAP_EXTS, 2, IMPORT_MAP_DEFAULT, stem, sizeof stem);
	ext = at >= 0 ? MAP_EXTS[at] : f->ui->menus.expansion ? MAP_EXPANSION_EXT : MAP_EXT;
	if (import_dir(f, 1, MAP_DIR, dir, sizeof dir) != 0
		|| import_copy(f, "map", name, dir, stem, ext, bytes, size, file, sizeof file, path, sizeof path) != 0) {
		kp_ui_file_imported(f->ui, NULL, STR_NOT_VALID);
		return -1;
	}
	/* read as the list reads it (LoadMapEntryInfo 0x45d870): a file that is no map of the screen's goes again */
	if (map_header_at(f, path, &h, why, sizeof why) != 0) {
		kp_flow_say(f, "map import %s: refused: %s", name, why);
		(void)remove(path);
		kp_ui_file_imported(f->ui, NULL, h.version > MAP_ORIGINAL_VERSION ? STR_EXPANSION : STR_DAMAGED);
		return -1;
	}
	f->host.sync_files(f->host.ctx);
	kp_flow_say(f, "map import %s: %s", name, path);
	if (out) snprintf(out, out_size, "%s", file);
	kp_ui_file_imported(f->ui, file, 0);
	return 0;
}

/* Import's picker: the screen's kind of file (Play Custom's maps, Load Replay's replays). */
static void import_file(void *host)
{
	KpFlow *f = (KpFlow *)host;
	int custom = f->ui && f->ui->menus.state == KP_GLUE_PLAY_CUSTOM;
	if (f->host.pick_file) f->host.pick_file(f->host.ctx, custom ? KP_FLOW_PICK_MAP : KP_FLOW_PICK_REPLAY);
}

static void export_replay(void *host, int index)
{
	KpFlow *f = (KpFlow *)host;
	const KpUiMenus *m = &f->ui->menus;
	char path[ENTRY_PATH];
	uint8_t *bytes;
	size_t size;
	if (!f->host.export_replay || index < 0 || index >= m->entry_count || m->entries[index].kind != KP_UI_ENTRY_REPLAY
		|| !kp_flow_entry_path(f, m, index, path, sizeof path)) {
		return;
	}
	if (!(bytes = kp_read_file(path, &size))) {
		kp_flow_say(f, "replay export %s: cannot be read", path);
		kp_ui_file_imported(f->ui, NULL, STR_DAMAGED);
		return;
	}
	f->host.export_replay(f->host.ctx, m->entries[index].file, bytes, size);
	free(bytes);
}

/* --- Load Game (the load screen and the in-game load dialog) --- */

/* Koprulu's reading of a listed save at Ok (kp_save_check): 0 for one that loads; gluAll 67 for a save of another build
 * or of other game data, 66 for a missing file or one that does not read. The original's list leaves such saves out
 * (AddSavedGameToList 0x499250 skips what ValidateSaveFile 0x499750 refuses, statuses 2 and 1 with these texts in its
 * multiplayer list); Koprulu's is the profile's, so the player is told why at Ok. */
static uint16_t check_save(void *host, int index)
{
	KpFlow *f = (KpFlow *)host;
	const KpUiMenus *m = &f->ui->menus;
	uint8_t *bytes;
	size_t size;
	KpError err;
	int refusal;
	if (index < 0 || index >= m->save_count) return KP_UI_GLUALL_SAVE_DAMAGED;
	if (!(bytes = kp_flow_read_save(f, m->saves[index], &size))) {
		kp_flow_say(f, "load %s: no file", m->saves[index]);
		return KP_UI_GLUALL_SAVE_DAMAGED;
	}
	refusal = kp_save_check(bytes, size, f->identity, &err);
	free(bytes);
	if (refusal == KP_SAVE_LOADABLE) return 0;
	kp_flow_say(f, "load %s: refused: %s", m->saves[index], err.msg);
	return refusal == KP_SAVE_OTHER_BUILD ? KP_UI_GLUALL_SAVE_VERSION : KP_UI_GLUALL_SAVE_DAMAGED;
}

/* --- the score's Save Replay: the save-replay dialog's files (replay.cpp, atlas P28) in <pref>replays --- */

/* A name that is one file of a folder: no separator, control character, "." or "..". The save-replay dialog's (typed
 * past network.tbl 17's characters, or a listed file's) and a custom game's next scenario (a map's trigger). */
int kp_flow_file_name_ok(const char *name)
{
	const unsigned char *p;
	if (name[0] == 0 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
	for (p = (const unsigned char *)name; *p; p++) {
		if (*p < 0x20 || *p == '/' || *p == '\\') return 0;
	}
	return 1;
}

/* The name's <pref>replays/<name>.rep: 0, or -1 for a name that is no file of the folder. */
static int replay_path(const KpFlow *f, const char *name, char *path, size_t size)
{
	char dir[ENTRY_PATH];
	int n;
	if (!kp_flow_file_name_ok(name)) return -1;
	pref_path(f, REPLAY_DIR, dir, sizeof dir);
	n = snprintf(path, size, "%s/%s%s", dir, name, REPLAY_EXT);
	return n < 0 || (size_t)n >= size ? -1 : 0;
}

static void list_replays(void *host, KpUi *ui)
{
	/* EnumerateReplayFiles 0x4803d0: the folder made, its .rep files in NTFS's order (kp_list_replays) */
	KpFlow *f = (KpFlow *)host;
	char dir[ENTRY_PATH];
	char **names;
	int count = 0, k;
	pref_path(f, REPLAY_DIR, dir, sizeof dir);
	if (kp_flow_make_dir(dir) != 0) {
		kp_flow_say(f, "save replay: cannot create %s: %s", dir, strerror(errno));
		return;
	}
	names = kp_list_replays(dir, &count);
	for (k = 0; k < count; k++) kp_ui_replay_listed(ui, names[k]);
	kp_list_files_free(names, count);
}

static int replay_exists(void *host, const char *name)
{
	/* SaveReplayAs 0x4802c0's GetFileAttributesA */
	char path[ENTRY_PATH + KP_UI_MAP_FILE];
	return replay_path((const KpFlow *)host, name, path, sizeof path) == 0 && file_there(path);
}

static int copy_last_replay(void *host, const char *name)
{
	/* CopyLastReplayTo 0x47fe60: LastReplay.rep's bytes to the name's file */
	KpFlow *f = (KpFlow *)host;
	char last[ENTRY_PATH + KP_UI_MAP_FILE], path[ENTRY_PATH + KP_UI_MAP_FILE], dir[ENTRY_PATH];
	uint8_t *bytes;
	size_t size = 0;
	KpError err;
	int ok;
	if (replay_path(f, name, path, sizeof path) != 0) {
		kp_flow_say(f, "save replay %s: not a file name", name);
		return 0;
	}
	pref_path(f, REPLAY_DIR, dir, sizeof dir);
	snprintf(last, sizeof last, "%s/%s", dir, LAST_REPLAY);
	if (kp_flow_make_dir(dir) != 0 || !(bytes = kp_read_file(last, &size))) {
		kp_flow_say(f, "save replay %s: no %s", name, last);
		return 0;
	}
	ok = kp_write_file_atomic(path, bytes, size, &err) == 0;
	free(bytes);
	if (!ok) {
		kp_flow_say(f, "save replay %s: %s", name, err.msg);
		return 0;
	}
	f->host.sync_files(f->host.ctx);
	kp_flow_say(f, "save replay: %s", path);
	return 1;
}

static int delete_replay(void *host, const char *name)
{
	/* DeleteReplayFile 0x47ffc0 (Koprulu's Delete) */
	KpFlow *f = (KpFlow *)host;
	char path[ENTRY_PATH + KP_UI_MAP_FILE];
	if (replay_path(f, name, path, sizeof path) != 0) {
		kp_flow_say(f, "delete replay %s: not a file name", name);
		return 0;
	}
	if (remove(path) != 0) {
		kp_flow_say(f, "delete replay %s: %s", name, strerror(errno));
		return 0;
	}
	f->host.sync_files(f->host.ctx);
	kp_flow_say(f, "delete replay: %s", path);
	return 1;
}

void kp_flow_interface_host(KpFlow *f, KpUi *ui)
{
	ui->list_entries = list_entries;
	ui->load_entry = load_entry;
	ui->map_class = map_class;
	ui->check_map = check_map;
	ui->templates = &f->data->templates;
	ui->check_replay = check_replay;
	ui->check_save = check_save;
	ui->import_file = f->host.pick_file ? import_file : NULL;
	ui->export_replay = f->host.export_replay ? export_replay : NULL;
	ui->delete_save = delete_save;
	ui->delete_profile_saves = delete_profile_saves;
	ui->list_replays = list_replays;
	ui->replay_exists = replay_exists;
	ui->copy_last_replay = copy_last_replay;
	ui->delete_replay = delete_replay;
	ui->host = f;
}

/* --- the network saves' names (kp_netsave.h) --- */

/* Whether a network save's name in the original's encoding (the save command's: another seat's input, windows-1252)
 * names a file in maps/save and nothing else: shorter than the header's name field and a name IsInvalidFileName
 * 0x408300 takes (ExecSaveGame 0x472f50 and SaveGame 0x498d20 ask it: no separator, no character of network.tbl's set,
 * no control character, no DOS device name; kp_transfer_name_valid), and not "." or "..". */
int kp_flow_net_save_legacy_ok(const char *legacy)
{
	if (strcmp(legacy, ".") == 0 || strcmp(legacy, "..") == 0) return 0;
	return kp_transfer_name_valid(legacy, KP_NETSAVE_NAME);
}

/* The same of a UTF-8 name (the dialogs', a file's): each of its characters windows-1252's. */
int kp_flow_net_save_name_ok(const char *name)
{
	char legacy[KP_NETSAVE_NAME + 8];
	return kp_text_to_legacy(name, legacy, sizeof legacy) == kp_text_chars((const uint8_t *)name) && kp_flow_net_save_legacy_ok(legacy);
}

/* A network save's file: <pref>maps/save/<name>.mlt (.mlx with Brood War; BuildSaveFilePath 0x499f70 in multiplayer:
 * maps\save\ whoever is logged in). */
void kp_flow_net_save_path(const KpFlow *f, const char *name, char *out, size_t size)
{
	char rel[ENTRY_PATH];
	snprintf(rel, sizeof rel, NET_SAVE_DIR "/%s%s", name, save_ext(&f->ui->menus));
	pref_path(f, rel, out, size);
}
