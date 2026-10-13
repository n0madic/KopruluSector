-- Finds the project's native modules (native/kp_core, ADR 0008). In
-- Defold the engine's native extensions register them as globals; elsewhere (headless tests,
-- tools) they load as Lua C modules from build/native (tools/native/build.sh) when present.
-- Returns nil without the module.
local M = {}

function M.find(name)
	local loaded = package.loaded[name] or rawget(_G, name)
	if loaded then return loaded end
	if rawget(_G, "sys") then return nil end -- Defold: extensions register at engine start
	if not package.cpath:find("build/native/", 1, true) then
		package.cpath = "./build/native/?.so;" .. package.cpath
	end
	local ok, module = pcall(require, name)
	return ok and module or nil
end

return M
