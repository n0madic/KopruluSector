-- The native core for the suites that need it (ADR 0008): kp_core's images world and generators, and a guard that
-- skips a suite's tests without the module (a Defold engine without the extension).
local native = require("game.native")
local images = require("game.images.native")

local M = {}

M.core = native.find("kp_core")
M.world = M.core and images.module(M.core) or nil
M.rng = M.core and M.core.rng or nil

-- Wraps every test of suite S so that it is skipped without the module.
function M.guard(S, suite)
	for name, test in pairs(S) do
		S[name] = function(T)
			if not M.core then
				-- KP_CORE_REQUIRED in the environment makes a missing module a failure (the sanitizer run).
				if os and os.getenv and os.getenv("KP_CORE_REQUIRED") then error(suite .. ": no kp_core module", 0) end
				T.env.log(("SKIP %s.%s: no kp_core module"):format(suite, name))
				return
			end
			return test(T)
		end
	end
	return S
end

return M
