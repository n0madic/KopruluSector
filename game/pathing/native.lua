-- The native modules of plan D19 (native/kp_path). In Defold the engine's native extension
-- registers the module itself; elsewhere (headless tests, tools) it is loaded as a Lua C module
-- from build/native (tools/native/build.sh) when present. Without a module the Lua code runs.
-- KP_PATH in the environment chooses: "lua" never uses the module, "check" runs both and
-- compares every result (game/pathing/local_search.lua).
local M = {}

local function find(name)
	local loaded = package.loaded[name] or rawget(_G, name)
	if loaded then return loaded end
	if rawget(_G, "sys") then return nil end -- Defold: extensions register at engine start
	if not package.cpath:find("build/native/", 1, true) then
		package.cpath = "./build/native/?.so;" .. package.cpath
	end
	local ok, module = pcall(require, name)
	return ok and module or nil
end

M.mode = os and os.getenv and os.getenv("KP_PATH") or nil
M.kp_path = M.mode ~= "lua" and find("kp_path") or nil

return M
