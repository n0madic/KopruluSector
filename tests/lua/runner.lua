-- Minimal test runner shared by the command-line entry (tests/lua/run.lua) and
-- the headless Defold entry (tests/defold/). A suite module returns
-- { name = function(T) ... end, ... }; the runner calls them in sorted order.
local M = {}

-- Suites run on every backend. Each entry is a module name.
M.SUITES = {
	"tests.lua.test_bit",
	"tests.lua.test_int",
	"tests.lua.test_rng",
	"tests.lua.test_kpf",
	"tests.lua.test_mission",
	"tests.lua.test_pool",
	"tests.lua.test_snapshot",
	"tests.lua.test_golden_m0e",
	"tests.lua.test_clock",
}

local function fmt(v)
	if type(v) == "string" then return string.format("%q", v) end
	return tostring(v)
end

local function new_context(env)
	local T = { env = env }

	function T.eq(actual, expected, msg)
		if actual ~= expected then
			error((msg and (msg .. ": ") or "") .. "expected " .. fmt(expected) .. ", got " .. fmt(actual), 2)
		end
	end

	function T.ok(cond, msg)
		if not cond then error(msg or "assertion failed", 2) end
	end

	function T.raises(fn, pattern, msg)
		local ok, err = pcall(fn)
		if ok then error((msg or "call") .. " did not raise", 2) end
		if pattern and not tostring(err):find(pattern) then
			error((msg or "call") .. " raised unexpected error: " .. tostring(err), 2)
		end
	end

	-- Reads a fixture file (path relative to the repository root).
	function T.read(path)
		return env.read(path)
	end

	return T
end

-- env.read(path) -> string, env.log(line), env.backend (string)
function M.run(env, suites)
	suites = suites or M.SUITES
	local passed, failed = 0, 0
	for _, module in ipairs(suites) do
		local suite = require(module)
		local names = {}
		for name in pairs(suite) do names[#names + 1] = name end
		table.sort(names)
		for _, name in ipairs(names) do
			local ok, err = pcall(suite[name], new_context(env))
			if ok then
				passed = passed + 1
			else
				failed = failed + 1
				env.log(("FAIL %s.%s: %s"):format(module, name, tostring(err)))
			end
		end
	end
	env.log(("%s: %d passed, %d failed"):format(env.backend, passed, failed))
	return failed
end

return M
