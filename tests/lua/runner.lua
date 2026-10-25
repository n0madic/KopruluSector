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
	"tests.lua.test_manager",
	"tests.lua.test_browser",
	"tests.lua.test_health_bar",
	"tests.lua.test_selection",
	"tests.lua.test_viewer_loads",
	"tests.lua.test_terrain_fill",
	"tests.lua.test_world_images",
	"tests.lua.test_initial_state",
	"tests.lua.test_scenarios",
	"tests.lua.test_damage_table",
	"tests.lua.test_save",
	"tests.lua.test_references",
	"tests.lua.test_orders_native",
	"tests.lua.test_requirements",
	"tests.lua.test_aiscript",
	"tests.lua.test_campaign",
	"tests.lua.test_fog",
	"tests.lua.test_play_world",
	"tests.lua.test_kp_core",
	"tests.lua.test_kp_ui",
	"tests.lua.test_ui_layout",
}

local function fmt(v)
	if type(v) == "string" then return string.format("%q", v) end
	return tostring(v)
end

local function new_context(env, shard, shards)
	local T = { env = env }

	-- Whether item k (1-based) of a sharded test's loop is this run's: shard `shard` of `shards` takes every
	-- shards-th item (all of them unsharded), so the shards of a parallel run cover each item once.
	function T.mine(k)
		return (k - 1) % (shards or 1) == (shard or 1) - 1
	end

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

-- The test names of a suite module (its functions), sorted.
function M.tests(module)
	local names = {}
	for name, fn in pairs(require(module)) do
		if type(fn) == "function" then names[#names + 1] = name end
	end
	table.sort(names)
	return names
end

-- How many shards a parallel run splits a test into: the suite's SHARDS table (tests whose long loops use T.mine),
-- else 1.
function M.shards(module, name)
	return (require(module).SHARDS or {})[name] or 1
end

-- env.read(path) -> string, env.log(line), env.backend (string). An entry of `suites` is a module name,
-- "module:test" for one test of it, or "module:test@i/n" for shard i of n of that test.
function M.run(env, suites)
	suites = suites or M.SUITES
	local passed, failed = 0, 0
	for _, entry in ipairs(suites) do
		local module, only = entry:match("^([^:]+):(.+)$")
		local shard, shards
		module = module or entry
		if only then
			local name, i, n = only:match("^(.+)@(%d+)/(%d+)$")
			if name then only, shard, shards = name, tonumber(i), tonumber(n) end
		end
		local suite = require(module)
		local names = only and { only } or M.tests(module)
		for _, name in ipairs(names) do
			if type(suite[name]) ~= "function" then error("no test " .. module .. "." .. name) end
			local ok, err = pcall(suite[name], new_context(env, shard, shards))
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
