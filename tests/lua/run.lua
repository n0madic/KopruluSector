-- Command-line test entry. Run from the repository root:
--   luajit tests/lua/run.lua          (LuaJIT, JIT on)
--   luajit -joff tests/lua/run.lua    (LuaJIT interpreter, as on iOS)
--   tools/toolchain/lua5.1 tests/lua/run.lua   (PUC Lua 5.1, as on HTML5)
package.path = "./?.lua;" .. package.path

local backend
if jit then
	backend = jit.version .. (jit.status() and " (JIT on)" or " (JIT off)")
else
	backend = _VERSION
end

if not bit then
	local ok, lib = pcall(require, "bit")
	if ok then
		bit = lib
	else
		bit = dofile("tests/lua/bitshim.lua")
		package.loaded.bit = bit
		backend = backend .. " + bit shim"
	end
end

local env = {
	backend = backend,
	log = print,
	read = function(path)
		local f = assert(io.open(path, "rb"))
		local data = f:read("*a")
		f:close()
		return data
	end,
}

local suites
if arg[1] then suites = { arg[1] } end
local failed = require("tests.lua.runner").run(env, suites)
os.exit(failed == 0 and 0 or 1)
