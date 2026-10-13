-- The native core (ADR 0008) against its Lua counterparts. Stage N.0: the suites of
-- game/simulation/int.lua and rng.lua run unchanged on kp_core.int and kp_core.rng, and the
-- loaders return what game/data/kpf.lua, dat.lua, mission.lua, grpmeta.lua and scripts.lua
-- return for every converted data file. Stage N.1: the image suites run unchanged on the C
-- world (game/images/native.lua), and the initial states, scenarios and order cases run as
-- hybrid games, the Lua unit code over the C world. Skipped without the module
-- (tools/native/build.sh, or a Defold engine with the extension).
local dat = require("game.data.dat")
local grpmeta = require("game.data.grpmeta")
local kpf = require("game.data.kpf")
local mission = require("game.data.mission")
local scripts = require("game.data.scripts")
local native = require("game.native")
local state_model = require("tools.lua.state_model")

local core = native.find("kp_core")

local S = {}

-- KP_CORE_REQUIRED in the environment makes a missing module a failure (the sanitizer run).
local function skip(T, what)
	if core then return false end
	if os and os.getenv and os.getenv("KP_CORE_REQUIRED") then error("test_kp_core: no kp_core module", 0) end
	T.env.log("SKIP test_kp_core." .. what .. ": no kp_core module")
	return true
end

-- Loads `module` afresh with the modules of `swaps` ({ [name] = replacement }) in place while it
-- and the modules named in `reload` load (so they capture the replacements too); `after`
-- (optional) gets the fresh modules by name before package.loaded is restored.
local function require_with(module, swaps, reload, after)
	local names = { module }
	for _, name in ipairs(reload or {}) do names[#names + 1] = name end
	for name in pairs(swaps) do names[#names + 1] = name end
	local saved = {}
	for _, name in ipairs(names) do saved[name] = package.loaded[name] end
	for _, name in ipairs(names) do package.loaded[name] = nil end
	for name, replacement in pairs(swaps) do package.loaded[name] = replacement end
	local ok, loaded = pcall(require, module)
	if ok and after then
		local fresh = {}
		for _, name in ipairs(names) do fresh[name] = package.loaded[name] end
		ok, loaded = pcall(function()
			after(fresh)
			return loaded
		end)
	end
	for _, name in ipairs(names) do package.loaded[name] = saved[name] end
	if not ok then error(loaded, 0) end
	return loaded
end

-- Runs every test of `suite_module` loaded by require_with.
local function run_suite_with(T, suite_module, swaps, reload, after)
	local suite = require_with(suite_module, swaps, reload, after)
	local tests = {}
	for name in pairs(suite) do tests[#tests + 1] = name end
	table.sort(tests)
	for _, name in ipairs(tests) do
		local passed, err = pcall(suite[name], T)
		if not passed then error(("%s.%s on kp_core: %s"):format(suite_module, name, tostring(err)), 0) end
	end
	T.ok(#tests > 0, suite_module .. " has tests")
end

function S.int_suite(T)
	if skip(T, "int_suite") then return end
	run_suite_with(T, "tests.lua.test_int", { ["game.simulation.int"] = core.int })
end

function S.rng_suite(T)
	if skip(T, "rng_suite") then return end
	run_suite_with(T, "tests.lua.test_rng", { ["game.simulation.rng"] = core.rng })
end

-- Deep equality of plain tables of numbers, strings and booleans.
local function same(a, b, path)
	if type(a) ~= "table" or type(b) ~= "table" then
		if a ~= b then return path end
		return nil
	end
	for k, v in pairs(a) do
		local bad = same(v, b[k], path .. "." .. tostring(k))
		if bad then return bad end
	end
	for k in pairs(b) do
		if a[k] == nil then return path .. "." .. tostring(k) .. " (missing)" end
	end
	return nil
end

local function data_files(T)
	local files = { state_model.DATA .. "dat.kpd" }
	local seen = {}
	local function add(m)
		local path = "generated/" .. m .. ".kpm"
		if not seen[path] then
			seen[path] = true
			files[#files + 1] = path
		end
	end
	for _, dir in ipairs({ "initial_state", "scenarios" }) do
		local root = "tests/golden/oracle/" .. dir .. "/"
		local index = assert((loadstring or load)(T.read(root .. "index.lua")))()
		for _, name in ipairs(index) do
			local g = assert((loadstring or load)(T.read(root .. name .. ".lua")))()
			add(g.mission:find("^scenarios/") and g.mission or "missions/" .. g.mission)
		end
	end
	return files
end

function S.containers_match(T)
	if skip(T, "containers_match") then return end
	if not T.has(state_model.DATA .. "dat.kpd") then return end
	local n = 0
	for _, path in ipairs(data_files(T)) do
		local data = T.read(path)
		local want = kpf.parse(data, path, { verify_crc = true })
		local got = core.kpf_parse(data, path, true)
		for _, field in ipairs({ "kind", "schema_version", "converter_version", "profile_id", "source_hash",
			"provenance_hash" }) do
			T.eq(got[field], want[field], path .. " " .. field)
		end
		T.eq(#got.sections, #want.sections, path .. " sections")
		for i, s in ipairs(want.sections) do
			local bad = same(got.sections[i], s, path .. " section " .. s.fourcc)
			T.ok(not bad, "differs: " .. tostring(bad))
		end
		n = n + 1
	end
	T.ok(n > 64, "data files checked: " .. n)
end

function S.container_errors(T)
	if skip(T, "container_errors") then return end
	local data = T.read("tests/fixtures/generated/kpf/basic.kpd")
	local function patched(offset, value) return data:sub(1, offset) .. string.char(value) .. data:sub(offset + 2) end
	local cases = {
		{ "short", "shorter" },
		{ "XXXX" .. data:sub(5), "bad magic" },
		{ data:sub(1, -5), "truncated" },
		{ patched(8, 2), "version 2" },
	}
	for _, c in ipairs(cases) do
		local ok_lua, err_lua = pcall(kpf.parse, c[1], "x")
		local ok_c, err_c = pcall(core.kpf_parse, c[1], "x")
		T.ok(not ok_lua and not ok_c, "both reject: " .. c[2])
		T.eq(err_c, err_lua, "message for " .. c[2])
	end
	local w = kpf.parse(data, "x"):info("WALK")
	local pos = w.offset + w.size - 1
	local corrupt = patched(pos, (data:byte(pos + 1) + 1) % 256)
	core.kpf_parse(corrupt, "x")
	local ok_lua, err_lua = pcall(kpf.parse, corrupt, "x", { verify_crc = true })
	local ok_c, err_c = pcall(core.kpf_parse, corrupt, "x", true)
	T.ok(not ok_lua and not ok_c, "checksum rejected")
	T.eq(err_c, err_lua, "checksum message")
end

function S.dat_matches(T)
	if skip(T, "dat_matches") then return end
	local path = state_model.DATA .. "dat.kpd"
	if not T.has(path) then return end
	local data = T.read(path)
	local bad = same(core.dat(data, path), dat.parse(data, path), "dat")
	T.ok(not bad, "differs: " .. tostring(bad))
	T.raises(function() core.dat(T.read("tests/fixtures/generated/kpf/basic.kpd"), "basic") end, "file kind")
end

function S.missions_match(T)
	if skip(T, "missions_match") then return end
	if not T.has(state_model.DATA .. "dat.kpd") then return end
	local n = 0
	for _, path in ipairs(data_files(T)) do
		if path:find("%.kpm$") then
			local data = T.read(path)
			local want, got = mission.parse(data, path), core.mission(data, path)
			local bad = same(got, want, path)
			T.ok(not bad, "differs: " .. tostring(bad))
			n = n + 1
		end
	end
	T.ok(n >= 64, "missions checked: " .. n)
end

-- The image data of stage N.1: grpmeta.kpd and iscript.kpd as the Lua readers return them.
function S.image_data_matches(T)
	if skip(T, "image_data_matches") then return end
	local gpath, ipath = state_model.DATA .. "grpmeta.kpd", state_model.DATA .. "iscript.kpd"
	if not T.has(gpath) then return end
	local data = T.read(gpath)
	local bad = same(core.grpmeta(data, gpath), grpmeta.parse(data, gpath), "grpmeta")
	T.ok(not bad, "differs: " .. tostring(bad))
	data = T.read(ipath)
	bad = same(core.iscript(data, ipath), scripts.parse_iscript(data, ipath), "iscript")
	T.ok(not bad, "differs: " .. tostring(bad))
	local other = T.read("tests/fixtures/generated/kpf/basic.kpd")
	T.raises(function() core.grpmeta(other, "basic") end, "expected \"GRPM\"")
	T.raises(function() core.iscript(other, "basic") end, "expected \"ISCR\"")
end

-- Stage N.1: the image suites run unchanged on the C world (game/images/native.lua), with the
-- C game generator.
local function image_swaps()
	return { ["game.images.world"] = require("game.images.native").module(core), ["game.simulation.rng"] = core.rng }
end

local IMAGE_SUITES = {
	{ "tests.lua.test_images" },
	{ "tests.lua.test_selection" },
	{ "tests.lua.test_iscript_golden", { "tests.lua.iscript_trace" } },
	{ "tests.lua.test_health_bar", { "tests.lua.iscript_trace" } },
	{ "tests.lua.test_draw_list" },
}

-- The C world is the same under every Lua runtime, and the proxies make the suites slow on the
-- interpreters, so they run with the JIT only (the Lua code they drive runs on every runtime
-- in its own suites).
local function interpreted(T, what)
	if jit and jit.status and jit.status() then return false end
	T.env.log("SKIP test_kp_core." .. what .. ": runs with the LuaJIT JIT")
	return true
end

function S.image_suites(T)
	if skip(T, "image_suites") or interpreted(T, "image_suites") then return end
	for _, suite in ipairs(IMAGE_SUITES) do run_suite_with(T, suite[1], image_swaps(), suite[2]) end
end

-- Hybrid games: the Lua unit code over the C world, against the original's golden states
-- (initial states, scenario steps, the order cases). The path search runs in Lua: native/kp_path
-- reads the Lua world's tables and moves into kp_core at stage N.4.
local GAME = { "tools.lua.state_model", "game.simulation.start", "game.simulation.game", "game.pathing.local_search" }

local function game_swaps()
	local swaps = image_swaps()
	swaps["game.pathing.native"] = { mode = "lua" }
	return swaps
end

-- The ported game methods in C (kp_core.game) on the fresh Game table.
local function install_game(fresh) core.game.install(fresh["game.simulation.game"].Game) end
local GAME_SUITES = { "tests.lua.test_initial_state", "tests.lua.test_scenarios", "tests.lua.test_orders_native" }

function S.hybrid_game_suites(T)
	if skip(T, "hybrid_game_suites") or interpreted(T, "hybrid_game_suites") then return end
	for _, suite in ipairs(GAME_SUITES) do run_suite_with(T, suite, game_swaps(), GAME, install_game) end
end

-- Bridge edge cases: a Lua error raised inside a nested world method, map sizes outside the
-- row table, a turret move from a sprite without a main image, a disabled sound callback.
function S.bridge_edge_cases(T)
	if skip(T, "bridge_edge_cases") then return end
	local world = require("game.images.native").module(core)
	local hooks = setmetatable({}, { __index = function(_, name) error("no hook " .. name, 0) end })
	local w = world.new({ dat = {}, grp = {}, iscript = { code = "", headers = {} }, rng = core.rng.game(1),
		hooks = hooks, map_width = 64, map_height = 48 })
	rawset(hooks, "order_signal", function(self)
		local ok, err = pcall(self.hook, self, "missing") -- raw Lua error inside the nested method
		T.ok(not ok and err == "no hook missing", "nested error: " .. tostring(err))
		error("boom", 0)
	end)
	w.flingy = 0
	local ok, err = pcall(w.flingy_signal, w, 1, 0)
	T.ok(not ok and err == "boom", "hook error raised by the method: " .. tostring(err))
	ok, err = pcall(w.flingy_signal, w, 1, 0)
	T.ok(not ok and err == "boom", "and again: " .. tostring(err))
	T.raises(function() w.map_h = 300 end, "outside 1..256")
	T.raises(function() w:set_map(64, 0) end, "outside 1..256")
	T.raises(function() world.new({ map_height = 257 }) end, "outside 1..256")
	w:init_pools()
	w:sprite_take_main(world.NONE, 5) -- sprite 5 has no main image: nothing happens, as in Lua
	w.sound = false
	T.eq(w.free_images.head, 0)
end

-- The method of ADR 0008 until N.6: the Lua game and the hybrid game run side by side on every
-- scenario's command and interface logs, and their full dump lines (with the image bounds) are
-- compared after every step, so a divergence shows at its first differing field.
function S.hybrid_equals_lua_line_by_line(T)
	if skip(T, "hybrid_equals_lua_line_by_line") or interpreted(T, "hybrid_equals_lua_line_by_line") then return end
	-- The sanitizer run (tools/ci.sh) covers the same C paths through hybrid_game_suites.
	if os and os.getenv and os.getenv("KP_CORE_SANITIZED") then return end
	if not T.has(state_model.DATA .. "dat.kpd") then return end
	local hybrid = require_with("tools.lua.state_model", game_swaps(), GAME, install_game)
	local dir = "tests/golden/oracle/scenarios/"
	local function load(name) return assert((loadstring or load)(T.read(dir .. name .. ".lua")))() end
	local compared = 0
	for _, name in ipairs(load("index")) do
		local g = load(name)
		local commands = {}
		for _, r in ipairs(g.commands) do
			commands[#commands + 1] = { turn = r.turn, player = r.player,
				bytes = (r.bytes:gsub("%x%x", function(h) return string.char(tonumber(h, 16)) end)) }
		end
		local path = "generated/" .. g.mission .. ".kpm"
		local lua_game = state_model.start_mission(T.read, path, g.seed, g.expansion)
		local c_game = hybrid.start_mission(T.read, path, g.seed, g.expansion)
		local lua_run, c_run = state_model.runner(lua_game, commands, g.ui), hybrid.runner(c_game, commands, g.ui)
		local last = 0
		for step in pairs(g.steps) do last = math.max(last, step) end
		for step = 1, last do
			lua_run.to(step)
			c_run.to(step)
			local want, got = state_model.lines(lua_game), hybrid.lines(c_game)
			for k = 1, math.max(#want, #got) do
				if want[k] ~= got[k] then
					error(("%s step %d line %d:\n  Lua: %s\n  C:   %s"):format(name, step, k, tostring(want[k]),
						tostring(got[k])), 0)
				end
			end
			compared = compared + 1
		end
	end
	T.ok(compared > 100, "steps compared: " .. compared)
end

-- Lengths and counts read from a file never wrap the bounds checks (32-bit sums in C).
function S.mission_rejects_wrapping_lengths(T)
	if skip(T, "mission_rejects_wrapping_lengths") then return end
	local path = "generated/scenarios/marine_walk.kpm"
	if not T.has(path) then return end
	local data = T.read(path)
	local rule = kpf.parse(data, path):info("RULE")
	T.ok(rule.size >= 8, "RULE holds an override")
	local at = rule.offset + 4 -- the first override's length
	local patched = data:sub(1, at) .. string.char(0xf8, 0xff, 0xff, 0xff) .. data:sub(at + 5)
	T.raises(function() core.mission(patched, path) end, "RULE section is malformed")
	local meta = kpf.parse(data, path):info("META")
	local wide = data:sub(1, meta.offset + 4) .. string.char(0xff, 0xff, 0xff, 0xff) .. data:sub(meta.offset + 9)
	T.raises(function() core.mission(wide, path) end, "MTXM size does not match 65535x65535")
end

return S
