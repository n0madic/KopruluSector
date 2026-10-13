/*
 * Shared parts of the hybrid bridges (ADR 0008 until N.6): Lua code that is not ported yet
 * reads and writes the C records through proxies with the shape of the Lua tables they
 * replace. A proxy keeps its owner (the userdata that owns the records) alive through its
 * environment.
 *   array: t[i] for first <= i < count reads and writes an int32_t record field (a DWORD
 *          field is read back unsigned); other keys read nil;
 *   array64: the same over int64_t fields, which hold any integral Lua number (|v| < 2^53);
 *   list:  t.head, t.tail of a KpList;
 *   lists: t[i] is the list proxy of base[i].
 */
#ifndef KP_LUA_BRIDGE_H
#define KP_LUA_BRIDGE_H

#include <math.h>
#include <stdint.h>

#include "../core/kp_images.h"
#include "../core/kp_int.h"
#include "kp_core_lua.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline int kp_integral(lua_Number v) { return v == floor(v) && v >= -2147483648.0 && v <= 4294967295.0; }

/* An integral number in [-2^31, 2^32) as its 32-bit pattern read signed; 0 for anything else. */
static inline int32_t kp_to_i32(lua_Number v)
{
	if (!kp_integral(v)) return 0;
	return v > 2147483647.0 ? kp_s32((uint32_t)v) : (int32_t)v;
}

/* An integral Lua number as int64_t (game state); 0 for anything else. */
static inline int kp_integral64(lua_Number v) { return v == floor(v) && v >= -9007199254740992.0 && v <= 9007199254740992.0; }

static inline int kp_push_int(lua_State *L, int64_t v)
{
	lua_pushnumber(L, (lua_Number)v);
	return 1;
}

static inline int kp_push_bool(lua_State *L, int v)
{
	lua_pushboolean(L, v);
	return 1;
}

/* A record proxy: named fields of a C struct, read and written like a Lua table's. */
typedef enum { KP_FIELD_INT, KP_FIELD_ARRAY64, KP_FIELD_LIST } KpFieldKind;
typedef struct {
	const char *name;
	KpFieldKind kind;
	void *ptr; /* int64_t (INT), int64_t[] (ARRAY64) or KpList (LIST) */
	int32_t first, count; /* ARRAY64 */
} KpField;
void kp_bridge_push_record(lua_State *L, int owner_index, const KpField *fields, int count);

/* Defines the proxy metatables (once per state). */
void kp_bridge_register(lua_State *L);

/* A new userdata of `meta` whose environment holds the value at owner_index. */
void *kp_bridge_proxy(lua_State *L, size_t size, const char *meta, int owner_index);
/* Pushes the owner kept by the proxy at proxy_index. */
void kp_bridge_push_owner(lua_State *L, int proxy_index);

void kp_bridge_push_array(lua_State *L, int owner_index, int32_t *base, int32_t first, int32_t count, int is_unsigned);
void kp_bridge_push_array64(lua_State *L, int owner_index, int64_t *base, int32_t first, int32_t count);
void kp_bridge_push_list(lua_State *L, int owner_index, KpList *list);
void kp_bridge_push_lists(lua_State *L, int owner_index, KpList *base, int32_t count);

/* The images world (kp_images_lua.c) for other bridges: the KpImages of the world userdata at
 * k; entering a call that may reach the world's hooks from state L (returns the state to
 * restore with kp_images_lua_leave); raising a failure the world reported (a hook's Lua error
 * value or the world's message). */
KpImages *kp_images_lua_check(lua_State *L, int k);
lua_State *kp_images_lua_enter(KpImages *w, lua_State *L);
void kp_images_lua_leave(KpImages *w, lua_State *saved);
int kp_images_lua_raise(KpImages *w, lua_State *L);

#ifdef __cplusplus
}
#endif

#endif
