/* Helpers shared by the kp_game sources (not part of the library's API). */
#ifndef KP_GAME_INTERNAL_H
#define KP_GAME_INTERNAL_H

#include "kp_game.h"
#include "kp_images_internal.h"
#include "kp_int.h"

#ifdef __cplusplus
extern "C" {
#endif

/* int.lua over Lua numbers held as int64_t: the 32-bit results of band/bor/... are unsigned. */
static inline int64_t kp_band(int64_t a, int64_t b) { return (int64_t)((uint32_t)a & (uint32_t)b); }
static inline int64_t kp_bor(int64_t a, int64_t b) { return (int64_t)((uint32_t)a | (uint32_t)b); }
static inline int64_t kp_bnot(int64_t a) { return (int64_t)(uint32_t)~(uint32_t)a; }
static inline int64_t kp_bxor(int64_t a, int64_t b) { return (int64_t)((uint32_t)a ^ (uint32_t)b); }
static inline int64_t kp_s32v(int64_t a) { return kp_s32((uint32_t)a); }
static inline int64_t kp_s16v(int64_t a) { return kp_s16((uint32_t)a); }
static inline int64_t kp_shrv(int64_t a, int64_t n) { return (int64_t)kp_shr((uint32_t)a, (uint32_t)n); }
static inline int64_t kp_shlv(int64_t a, int64_t n) { return (int64_t)kp_shl((uint32_t)a, (uint32_t)n); }
/* int.idiv (truncating division of the numbers as they are), int.udiv and int.urem (of their
 * low 32 bits); a zero divisor and INT_MIN / -1 fail, as in int.lua. */
int64_t kp_idiv64(KpGame *g, int64_t a, int64_t b);
int64_t kp_udiv64(KpGame *g, int64_t a, int64_t b);
int64_t kp_urem64(KpGame *g, int64_t a, int64_t b);
static inline int64_t kp_sarv(int64_t a, int64_t n) { return kp_sar(kp_s32((uint32_t)a), (uint32_t)n); }
static inline int64_t kp_mulv(int64_t a, int64_t b) { return (int64_t)kp_mul((uint32_t)a, (uint32_t)b); }
static inline int64_t kp_addv(int64_t a, int64_t b) { return (int64_t)(uint32_t)((uint32_t)a + (uint32_t)b); }
static inline int64_t kp_u32v(int64_t a) { return (int64_t)(uint32_t)a; }
/* Lua's floored % for a positive m. */
static inline int64_t kp_mod(int64_t a, int64_t m)
{
	int64_t r = a % m;
	return r < 0 ? r + m : r;
}

/* The native list helpers over int64_t links whose null link is `none` (0 for units). */
void kp_list64_unlink(int64_t *prev, int64_t *next, KpList *list, int64_t node, int64_t none);
void kp_list64_insert_after(int64_t *prev, int64_t *next, KpList *list, int64_t existing, int64_t node, int64_t none);
void kp_list64_insert_after_head(int64_t *prev, int64_t *next, KpList *list, int64_t node, int64_t none);
void kp_list64_insert_before(int64_t *prev, int64_t *next, KpList *list, int64_t existing, int64_t node, int64_t none);

/* The resource and score types of the trigger conditions, actions and leaderboards (Accumulate, Score, Set
 * Resources, Set Score, the rankings; atlas P40). */
enum { KP_RES_MINERALS = 0, KP_RES_GAS = 1, KP_RES_BOTH = 2 };
enum {
	KP_SCORE_TOTAL = 0, KP_SCORE_UNITS, KP_SCORE_BUILDINGS, KP_SCORE_UNITS_BUILDINGS, KP_SCORE_KILLS, KP_SCORE_RAZINGS,
	KP_SCORE_KILLS_RAZINGS, KP_SCORE_CUSTOM
};
/* The modifiers of a trigger action (byte +0x1b): a switch, a state or a counter (atlas P40). */
enum {
	KP_MOD_SET = 4, KP_MOD_CLEAR = 5, KP_MOD_TOGGLE = 6, KP_MOD_SET_TO = 7, KP_MOD_ADD = 8, KP_MOD_SUBTRACT = 9,
	KP_MOD_RANDOMIZE = 11
};
/* The trigger condition ids (the table 0x50e058, atlas P40; converter/analyze/triggers.py CONDITION_IDS). */
enum {
	KP_COND_NONE = 0, KP_COND_COUNTDOWN, KP_COND_COMMAND, KP_COND_BRING, KP_COND_ACCUMULATE, KP_COND_KILL,
	KP_COND_COMMAND_THE_MOST, KP_COND_COMMANDS_THE_MOST_AT, KP_COND_MOST_KILLS, KP_COND_HIGHEST_SCORE,
	KP_COND_MOST_RESOURCES, KP_COND_SWITCH, KP_COND_ELAPSED_TIME, KP_COND_MISSION_BRIEFING, KP_COND_OPPONENTS,
	KP_COND_DEATHS, KP_COND_COMMAND_THE_LEAST, KP_COND_COMMAND_THE_LEAST_AT, KP_COND_LEAST_KILLS, KP_COND_LOWEST_SCORE,
	KP_COND_LEAST_RESOURCES, KP_COND_SCORE, KP_COND_ALWAYS, KP_COND_NEVER, KP_COND_TYPES
};
/* The trigger action ids (the table 0x50df68, atlas P40; converter/analyze/triggers.py ACTION_IDS). */
enum {
	KP_ACT_NONE = 0, KP_ACT_VICTORY, KP_ACT_DEFEAT, KP_ACT_PRESERVE_TRIGGER, KP_ACT_WAIT, KP_ACT_PAUSE_GAME,
	KP_ACT_UNPAUSE_GAME, KP_ACT_TRANSMISSION, KP_ACT_PLAY_WAV, KP_ACT_DISPLAY_TEXT, KP_ACT_CENTER_VIEW,
	KP_ACT_CREATE_UNIT_WITH_PROPERTIES, KP_ACT_SET_MISSION_OBJECTIVES, KP_ACT_SET_SWITCH, KP_ACT_SET_COUNTDOWN,
	KP_ACT_RUN_AI_SCRIPT, KP_ACT_RUN_AI_SCRIPT_AT, KP_ACT_LB_CONTROL, KP_ACT_LB_CONTROL_AT, KP_ACT_LB_RESOURCES,
	KP_ACT_LB_KILLS, KP_ACT_LB_POINTS, KP_ACT_KILL_UNIT, KP_ACT_KILL_UNIT_AT, KP_ACT_REMOVE_UNIT, KP_ACT_REMOVE_UNIT_AT,
	KP_ACT_SET_RESOURCES, KP_ACT_SET_SCORE, KP_ACT_MINIMAP_PING, KP_ACT_TALKING_PORTRAIT, KP_ACT_MUTE_UNIT_SPEECH,
	KP_ACT_UNMUTE_UNIT_SPEECH, KP_ACT_LB_COMPUTER_PLAYERS, KP_ACT_LB_GOAL_CONTROL, KP_ACT_LB_GOAL_CONTROL_AT,
	KP_ACT_LB_GOAL_RESOURCES, KP_ACT_LB_GOAL_KILLS, KP_ACT_LB_GOAL_POINTS, KP_ACT_MOVE_LOCATION, KP_ACT_MOVE_UNIT,
	KP_ACT_LB_GREED, KP_ACT_SET_NEXT_SCENARIO, KP_ACT_SET_DOODAD_STATE, KP_ACT_SET_INVINCIBILITY, KP_ACT_CREATE_UNIT,
	KP_ACT_SET_DEATHS, KP_ACT_ORDER, KP_ACT_COMMENT, KP_ACT_GIVE_UNITS, KP_ACT_MODIFY_HP, KP_ACT_MODIFY_ENERGY,
	KP_ACT_MODIFY_SHIELDS, KP_ACT_MODIFY_RESOURCES, KP_ACT_MODIFY_HANGAR, KP_ACT_PAUSE_TIMER, KP_ACT_UNPAUSE_TIMER,
	KP_ACT_DRAW, KP_ACT_SET_ALLIANCE, KP_ACT_DISABLE_DEBUG_MODE, KP_ACT_ENABLE_DEBUG_MODE, KP_ACT_TYPES
};

#ifdef __cplusplus
}
#endif

#endif
