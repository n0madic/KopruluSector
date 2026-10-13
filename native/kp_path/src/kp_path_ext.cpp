// The Defold extension of kp_path: registers the module table `kp_path` in each script
// context (game/pathing/native.lua finds it as a global).
#define EXTENSION_NAME KpPath
#define LIB_NAME "KpPath"
#define MODULE_NAME "kp_path"

#include <dmsdk/sdk.h>

#include "kp_path.h"

static dmExtension::Result Initialize(dmExtension::Params* params)
{
    lua_State* L = params->m_L;
    int top = lua_gettop(L);
    kp_path_register(L);
    lua_pop(L, 1);
    assert(top == lua_gettop(L));
    return dmExtension::RESULT_OK;
}

static dmExtension::Result Finalize(dmExtension::Params* params)
{
    return dmExtension::RESULT_OK;
}

DM_DECLARE_EXTENSION(EXTENSION_NAME, LIB_NAME, 0, 0, Initialize, 0, 0, Finalize)
