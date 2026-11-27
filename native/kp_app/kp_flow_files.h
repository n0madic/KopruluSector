/*
 * The flow's files (kp_flow_files.c), internal to the flow (kp_flow.c and kp_flow_files.c): the map lists of Play
 * Custom, Create Game and Load Replay, the saves' and the replays' files, Import and Export, the map transfer's files,
 * and the custom map a game plays read from a file, a replay or a save. kp_flow.h's interface host and entry calls are
 * defined there too; the scenes (the games those files start) stay kp_flow.c's.
 */
#ifndef KP_FLOW_FILES_H
#define KP_FLOW_FILES_H

#include "kp_flow.h"
#include "kp_netsave.h"

#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REPLAY_DIR "replays"
#define REPLAY_EXT ".rep"
#define LAST_REPLAY "LastReplay.rep" /* EndGame 0x443db0 -> WriteReplayFile 0x480080("LastReplay", 1) */
#define STR_NOT_VALID 51       /* gluAll "The selected scenario is not valid." */
#define NET_NEXT_SCENARIO 0x6a /* network.tbl: CreateNextScenarioGame 0x4bb360's failure */
#define MAP_DIR "maps"         /* the user's maps folder (the original's user base maps\), a data folder's Maps */
#define NET_SAVE_DIR MAP_DIR "/save" /* the network saves' folder (BuildSaveDirectory 0x499e80 in multiplayer) */
#define ENTRY_PATH KP_FLOW_MAP_PATH
#define MAP_TEMPLATE "Melee" /* --map's game type without --map-template */

/* A line of the flow's log (the host's log). */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
void kp_flow_say(KpFlow *f, const char *fmt, ...);
/* The in-game save dialogs' names (kp_flow.c: a network game's maps/save files, another game's the profile's). */
void kp_flow_game_saves(KpFlow *f, KpUi *ui);

/* A file in the preferences' directory. */
static inline void pref_path(const KpFlow *f, const char *file, char *out, size_t size)
{
	snprintf(out, size, "%s%s", f->pref, file);
}

/* Whether the name ends in ext, the ASCII letters folded. */
static inline int has_ext(const char *name, const char *ext)
{
	size_t n = strlen(name), e = strlen(ext);
	return n >= e && kp_compare_nocase(name + n - e, ext) == 0;
}

/* Whether the name ends in .rep, in any case. */
static inline int has_replay_ext(const char *name) { return has_ext(name, REPLAY_EXT); }

/* The saved network games' extension (0x4995a0): .mlx with Brood War, else .mlt */
static inline const char *save_ext(const KpUiMenus *m) { return m->expansion ? KP_NETSAVE_EXT_BW : KP_NETSAVE_EXT; }

/* The saves: the logged-in ID's file of a save name and its bytes (malloc'd, NULL when it does not read); a folder made
 * (0, or -1); an old save's file renamed (the profile's hook); a saved game's file bounded by KP_NETSAVE_FILE_MAX. */
void kp_flow_save_file(const KpFlow *f, const char *name, char *out, size_t size);
uint8_t *kp_flow_read_save(const KpFlow *f, const char *name, size_t *size);
int kp_flow_make_dir(const char *path);
int kp_flow_rename_old_save(void *host, const char *from, const char *to);
uint8_t *kp_flow_read_save_file(const char *path, size_t *size);
/* The network saves: a name in the original's encoding or in UTF-8 that names a file in maps/save, and its path. */
int kp_flow_net_save_legacy_ok(const char *legacy);
int kp_flow_net_save_name_ok(const char *name);
void kp_flow_net_save_path(const KpFlow *f, const char *name, char *out, size_t size);
/* The replays: a file read and checked (-1 with the gluAll text that refuses it) and the catalog row of its mission. */
int kp_flow_read_replay_at(const char *path, KpRep *rep, uint16_t *refusal);
int kp_flow_replay_mission(const KpFlow *f, const KpRep *rep);
/* The custom map the next game plays: the load of a template's record, read from a replay's CHK, a file or a save
 * (0, or -1 with why), closed as EndGame 0x443db0 closes its archive. */
KpChkLoad kp_flow_custom_load_of(const uint8_t *template_record);
int kp_flow_custom_from_replay(KpFlow *f, const KpRep *rep, char *why, size_t why_size);
int kp_flow_custom_from_file(KpFlow *f, const char *path, const char *name, KpChkLoad load, char *why, size_t why_size);
int kp_flow_custom_from_save(KpFlow *f, const char *mission, KpSaveMap *map, char *why, size_t why_size);
void kp_flow_custom_close(KpFlow *f);
/* The names: a path's last part, a name a file may have, the path of a map list's entry (1, or 0). */
const char *kp_flow_last_part(const char *s);
int kp_flow_file_name_ok(const char *name);
int kp_flow_entry_path(const KpFlow *f, const KpUiMenus *m, int index, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
