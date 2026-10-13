/* Proxies of the hybrid bridges (kp_lua_bridge.h). */
#include "kp_lua_bridge.h"

#include <string.h>

#define ARRAY_META "kp_core.array"
#define LIST_META "kp_core.list"
#define LISTS_META "kp_core.lists"
#define ARRAY64_META "kp_core.array64"

typedef struct {
	int32_t *base;
	int32_t first, count;
	int is_unsigned;
} Array;

typedef struct {
	int64_t *base;
	int32_t first, count;
} Array64;

typedef struct {
	KpList *list;
} List;

typedef struct {
	KpList *base;
	int32_t count;
} Lists;

void *kp_bridge_proxy(lua_State *L, size_t size, const char *meta, int owner_index)
{
	void *p = lua_newuserdata(L, size);
	luaL_getmetatable(L, meta);
	lua_setmetatable(L, -2);
	lua_newtable(L);
	lua_pushvalue(L, owner_index);
	lua_rawseti(L, -2, 1);
	lua_setfenv(L, -2);
	return p;
}

void kp_bridge_push_owner(lua_State *L, int proxy_index)
{
	lua_getfenv(L, proxy_index);
	lua_rawgeti(L, -1, 1);
	lua_remove(L, -2);
}

/* The record index of key k, or -1 outside [first, count). */
static int32_t record_key(lua_State *L, int k, int32_t first, int32_t count)
{
	lua_Number v = lua_tonumber(L, k);
	if (lua_type(L, k) != LUA_TNUMBER || v != floor(v) || v < first || v >= count) return -1;
	return (int32_t)v;
}

/* The metamethods run only on userdata of their own metatable, so they read the payload
 * without checking it again. */
static int array_index(lua_State *L)
{
	Array *a = (Array *)lua_touserdata(L, 1);
	int32_t i = record_key(L, 2, a->first, a->count);
	if (i < 0) {
		lua_pushnil(L);
		return 1;
	}
	if (a->is_unsigned) return kp_push_int(L, (uint32_t)a->base[i]);
	return kp_push_int(L, a->base[i]);
}

static int array_newindex(lua_State *L)
{
	Array *a = (Array *)lua_touserdata(L, 1);
	int32_t i = record_key(L, 2, a->first, a->count);
	lua_Number v = lua_tonumber(L, 3);
	if (i < 0) return luaL_error(L, "kp_core: record %s outside the array", lua_tostring(L, 2));
	if (lua_type(L, 3) != LUA_TNUMBER || !kp_integral(v)) return luaL_error(L, "kp_core: a record field takes 32-bit integers");
	a->base[i] = kp_to_i32(v);
	return 0;
}

void kp_bridge_push_array(lua_State *L, int owner_index, int32_t *base, int32_t first, int32_t count, int is_unsigned)
{
	Array *a = (Array *)kp_bridge_proxy(L, sizeof *a, ARRAY_META, owner_index);
	a->base = base;
	a->first = first;
	a->count = count;
	a->is_unsigned = is_unsigned;
}

static int array64_index(lua_State *L)
{
	Array64 *a = (Array64 *)lua_touserdata(L, 1);
	int32_t i = record_key(L, 2, a->first, a->count);
	if (i < 0) {
		lua_pushnil(L);
		return 1;
	}
	return kp_push_int(L, a->base[i]);
}

static int array64_newindex(lua_State *L)
{
	Array64 *a = (Array64 *)lua_touserdata(L, 1);
	int32_t i = record_key(L, 2, a->first, a->count);
	lua_Number v = lua_tonumber(L, 3);
	if (i < 0) return luaL_error(L, "kp_core: record %s outside the array", lua_tostring(L, 2));
	if (lua_type(L, 3) != LUA_TNUMBER || !kp_integral64(v)) return luaL_error(L, "kp_core: a game field takes integers");
	a->base[i] = (int64_t)v;
	return 0;
}

void kp_bridge_push_array64(lua_State *L, int owner_index, int64_t *base, int32_t first, int32_t count)
{
	Array64 *a = (Array64 *)kp_bridge_proxy(L, sizeof *a, ARRAY64_META, owner_index);
	a->base = base;
	a->first = first;
	a->count = count;
}

static int list_index(lua_State *L)
{
	List *l = (List *)lua_touserdata(L, 1);
	const char *k = lua_tostring(L, 2);
	if (k && strcmp(k, "head") == 0) return kp_push_int(L, l->list->head);
	if (k && strcmp(k, "tail") == 0) return kp_push_int(L, l->list->tail);
	lua_pushnil(L);
	return 1;
}

static int list_newindex(lua_State *L)
{
	List *l = (List *)lua_touserdata(L, 1);
	const char *k = lua_tostring(L, 2);
	lua_Number v = lua_tonumber(L, 3);
	if (lua_type(L, 3) != LUA_TNUMBER || !kp_integral(v)) return luaL_error(L, "kp_core: list links are integers");
	if (k && strcmp(k, "head") == 0) l->list->head = kp_to_i32(v);
	else if (k && strcmp(k, "tail") == 0) l->list->tail = kp_to_i32(v);
	else return luaL_error(L, "kp_core: a list has only head and tail");
	return 0;
}

void kp_bridge_push_list(lua_State *L, int owner_index, KpList *list)
{
	List *l = (List *)kp_bridge_proxy(L, sizeof *l, LIST_META, owner_index);
	l->list = list;
}

static int lists_index(lua_State *L)
{
	Lists *ls = (Lists *)lua_touserdata(L, 1);
	int32_t i = record_key(L, 2, 0, ls->count);
	if (i < 0) {
		lua_pushnil(L);
		return 1;
	}
	kp_bridge_push_owner(L, 1);
	kp_bridge_push_list(L, lua_gettop(L), &ls->base[i]);
	return 1;
}

void kp_bridge_push_lists(lua_State *L, int owner_index, KpList *base, int32_t count)
{
	Lists *ls = (Lists *)kp_bridge_proxy(L, sizeof *ls, LISTS_META, owner_index);
	ls->base = base;
	ls->count = count;
}

static void define(lua_State *L, const char *meta, lua_CFunction index, lua_CFunction newindex)
{
	luaL_newmetatable(L, meta);
	lua_pushcfunction(L, index);
	lua_setfield(L, -2, "__index");
	if (newindex) {
		lua_pushcfunction(L, newindex);
		lua_setfield(L, -2, "__newindex");
	}
	lua_pop(L, 1);
}

void kp_bridge_register(lua_State *L)
{
	define(L, ARRAY_META, array_index, array_newindex);
	define(L, ARRAY64_META, array64_index, array64_newindex);
	define(L, LIST_META, list_index, list_newindex);
	define(L, LISTS_META, lists_index, NULL);
}
