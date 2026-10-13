-- The native core for the suites that need it (ADR 0008): kp_core's images world and generators, and a guard that
-- skips a suite's tests without the module (a Defold engine without the extension).
local native = require("game.native")
local images = require("game.images.native")

local M = {}

M.core = native.find("kp_core")
M.world = M.core and images.module(M.core) or nil
M.rng = M.core and M.core.rng or nil

-- True with the module; without it logs the skip of `label`, or fails when KP_CORE_REQUIRED is in the environment
-- (the sanitizer run, where a module that does not load must not pass as skipped suites).
local function has_core(T, label)
	if M.core then return true end
	if os and os.getenv and os.getenv("KP_CORE_REQUIRED") then error(label .. ": no kp_core module", 0) end
	T.env.log("SKIP " .. label .. ": no kp_core module")
	return false
end

-- Wraps every test of suite S so that it is skipped without the module.
function M.guard(S, suite)
	for name, test in pairs(S) do
		S[name] = function(T)
			if not has_core(T, suite .. "." .. name) then return end
			return test(T)
		end
	end
	return S
end

-- Whether a test over native games can run: the module (as guard) and the files it reads (a skip without them).
function M.ready(T, label, files)
	if not has_core(T, label) then return false end
	for _, path in ipairs(files or {}) do
		if not T.has(path) then
			T.env.log(("SKIP %s: no %s"):format(label, path))
			return false
		end
	end
	return true
end

return M
