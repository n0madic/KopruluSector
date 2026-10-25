// The Defold extension of kp_core: registers the module table `kp_core` in each script
// context (the library and its Lua binding are plain C in src/core and src/lua).
#define EXTENSION_NAME KpCore
#define LIB_NAME "KpCore"

#include <dmsdk/sdk.h>

#include "../lua/kp_core_lua.h"

#include <string.h>

// kp_core.copy_to_buffer(bytes, buffer): copies a string into a Defold buffer's bytes (the interface canvas's
// changed rectangle, presentation/ui/canvas.lua), which a Lua loop would set byte by byte. The buffer must hold at
// least the string's length.
static int CopyToBuffer(lua_State* L)
{
    size_t size;
    const char* bytes = luaL_checklstring(L, 1, &size);
    dmScript::LuaHBuffer* buffer = dmScript::CheckBuffer(L, 2);
    void* data = 0;
    uint32_t capacity = 0;
    if (dmBuffer::GetBytes(buffer->m_Buffer, &data, &capacity) != dmBuffer::RESULT_OK || capacity < size)
        return luaL_error(L, "copy_to_buffer: the buffer holds %d bytes, not %d", (int)capacity, (int)size);
    memcpy(data, bytes, size);
    return 0;
}

static dmExtension::Result Initialize(dmExtension::Params* params)
{
    lua_State* L = params->m_L;
    int top = lua_gettop(L);
    kp_core_register(L);
    lua_pushcfunction(L, CopyToBuffer);
    lua_setfield(L, -2, "copy_to_buffer");
    lua_pop(L, 1);
    assert(top == lua_gettop(L));
    return dmExtension::RESULT_OK;
}

static dmExtension::Result Finalize(dmExtension::Params* params)
{
    return dmExtension::RESULT_OK;
}

DM_DECLARE_EXTENSION(EXTENSION_NAME, LIB_NAME, 0, 0, Initialize, 0, 0, Finalize)
