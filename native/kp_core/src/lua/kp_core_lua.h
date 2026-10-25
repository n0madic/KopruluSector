/* The Lua binding of kp_core (kp_core_lua.c): the standard Lua C API only. */
#ifndef KP_CORE_LUA_H
#define KP_CORE_LUA_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef KP_DEFOLD
#include <dmsdk/lua/lua.h>
#include <dmsdk/lua/lauxlib.h>
#else
#include "lua.h"
#include "lauxlib.h"
#endif

/* Registers the module table `kp_core` (global and package.loaded); leaves it on the stack. */
int kp_core_register(lua_State *L);

/* Metatable of kp_core.rng.game userdata; the payload is a KpRngGame. */
#define KP_LUA_RNG_GAME "kp_core.rng.game"
#define KP_LUA_RNG_CRT "kp_core.rng.crt"

/* Pushes the tables kp_core.images (kp_images_lua.c), kp_core.game (kp_game_lua.c) and kp_core.ui (kp_ui_lua.c). */
void kp_images_lua_push(lua_State *L);
void kp_game_lua_push(lua_State *L);
void kp_ui_lua_push(lua_State *L);

/* The native game of a kp_core.game table at index t (kp_game_lua.c); a Lua error for any other value. */
struct KpGame;
struct KpGame *kp_lua_check_game(lua_State *L, int t);

#ifdef __cplusplus
}
#endif

#endif
