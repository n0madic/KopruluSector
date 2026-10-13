/* kp_path: the local path search in C (kp_path_core.c). */
#ifndef KP_PATH_H
#define KP_PATH_H

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

/* Registers the module table `kp_path` (global and package.loaded); leaves it on the stack. */
int kp_path_register(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif
