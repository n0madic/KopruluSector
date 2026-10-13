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
	"tests.lua.test_dat",
	"tests.lua.test_scripts",
	"tests.lua.test_terrain",
	"tests.lua.test_palettes",
	"tests.lua.test_fonts",
	"tests.lua.test_ui",
	"tests.lua.test_sounds",
	"tests.lua.test_portraits",
	"tests.lua.test_grpmeta",
	"tests.lua.test_rules",
	"tests.lua.test_unit_records",
	"tests.lua.test_sprite_check",
	"tests.lua.test_pool",
	"tests.lua.test_snapshot",
	"tests.lua.test_golden_m0e",
	"tests.lua.test_golden_missions",
	"tests.lua.test_clock",
	"tests.lua.test_images",
	"tests.lua.test_iscript_golden",
	"tests.lua.test_draw_list",
	"tests.lua.test_slots",
	"tests.lua.test_world_lookups",
	"tests.lua.test_palette",
	"tests.lua.test_events",
	"tests.lua.test_browser",
	"tests.lua.test_health_bar",
	"tests.lua.test_selection",
	"tests.lua.test_viewer_loads",
	"tests.lua.test_terrain_fill",
	"tests.lua.test_world_images",
	"tests.lua.test_vision",
	"tests.lua.test_initial_state",
	"tests.lua.test_scenarios",
	"tests.lua.test_save",
	"tests.lua.test_references",
	"tests.lua.test_play_world",
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

	-- True when a file is readable (optional inputs such as generated/ data).
	function T.has(path)
		return (pcall(env.read, path))
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
