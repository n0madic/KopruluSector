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

/* Pushes the table kp_core.images (kp_images_lua.c). */
void kp_images_lua_push(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif
