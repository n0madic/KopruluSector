// The Defold extension of kp_core: registers the module table `kp_core` in each script
// context (the library and its Lua binding are plain C in src/core and src/lua).
#define EXTENSION_NAME KpCore
#define LIB_NAME "KpCore"

#include <dmsdk/sdk.h>

#include "../lua/kp_core_lua.h"

static dmExtension::Result Initialize(dmExtension::Params* params)
{
    lua_State* L = params->m_L;
    int top = lua_gettop(L);
    kp_core_register(L);
    lua_pop(L, 1);
    assert(top == lua_gettop(L));
    return dmExtension::RESULT_OK;
}

static dmExtension::Result Finalize(dmExtension::Params* params)
{
    return dmExtension::RESULT_OK;
}

DM_DECLARE_EXTENSION(EXTENSION_NAME, LIB_NAME, 0, 0, Initialize, 0, 0, Finalize)
