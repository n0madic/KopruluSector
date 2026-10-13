/* The entry point of kp_path as a plain Lua C module (tests and benchmarks outside Defold;
 * tools/native/build.sh). The Defold extension registers the module in native/kp_path. */
#include "kp_path.h"

int luaopen_kp_path(lua_State *L) { return kp_path_register(L); }
