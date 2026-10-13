-- The native core (ADR 0008) against the Lua code it shares data with: the suite of game/simulation/int.lua runs
-- unchanged on kp_core.int; the loaders return what game/data/kpf.lua, dat.lua, mission.lua, grpmeta.lua and
-- scripts.lua return for every converted data file; the bridge's edge cases and the event ring of a native game.
-- The game itself is checked against the original by the oracle suites (test_initial_state, test_scenarios,
-- test_orders_native, test_references, test_save). Skipped without the module (tools/native/build.sh, or a Defold
-- engine with the extension).
local dat = require("game.data.dat")
local grpmeta = require("game.data.grpmeta")
local kpf = require("game.data.kpf")
local mission = require("game.data.mission")
local scripts = require("game.data.scripts")
local native = require("game.native")
local oracle = require("tools.lua.oracle")

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
	local files = { oracle.DATA .. "dat.kpd" }
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
	if not T.has(oracle.DATA .. "dat.kpd") then return end
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
	local path = oracle.DATA .. "dat.kpd"
	if not T.has(path) then return end
	local data = T.read(path)
	local bad = same(core.dat(data, path), dat.parse(data, path), "dat")
	T.ok(not bad, "differs: " .. tostring(bad))
	T.raises(function() core.dat(T.read("tests/fixtures/generated/kpf/basic.kpd"), "basic") end, "file kind")
end

function S.missions_match(T)
	if skip(T, "missions_match") then return end
	if not T.has(oracle.DATA .. "dat.kpd") then return end
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
	local gpath, ipath = oracle.DATA .. "grpmeta.kpd", oracle.DATA .. "iscript.kpd"
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

local function golden(T, kind, name)
	return assert((loadstring or load)(T.read("tests/golden/oracle/" .. kind .. "/" .. name .. ".lua")))()
end

local function commands_of(rows)
	local out = {}
	for _, r in ipairs(rows or {}) do
		out[#out + 1] = { turn = r.turn, player = r.player,
			bytes = (r.bytes:gsub("%x%x", function(h) return string.char(tonumber(h, 16)) end)) }
	end
	return out
end

local function mission_path(mission)
	return "generated/" .. mission .. ".kpm"
end

-- The event ring of a native game: the IScript sounds of the steps and the local player's error notifications
-- and text lines, each drained once in emission order; a game without a presentation records none.
function S.native_events(T)
	if skip(T, "native_events") then return end
	if not T.has(oracle.DATA .. "dat.kpd") then return end
	local function kinds(name, events)
		local g = golden(T, "scenarios", name)
		local game = oracle.start_mission(T.read, mission_path(g.mission), g.seed, g.expansion, { events = events })
		local run = oracle.runner(game, commands_of(g.commands), g.ui)
		local n, last = { 0, 0, 0, 0 }, 0
		run.on_event = function(e)
			n[e.kind] = n[e.kind] + 1
			T.ok(e.seq > last, "sequence numbers grow")
			last = e.seq
			if e.kind == 3 then T.eq(e.text, "placement failed") end
		end
		run.to(150)
		T.eq(#oracle.events(game), 0, "the runner drains every step")
		return n
	end
	local n = kinds("rescue_fog", true)
	T.ok(n[1] > 0, "sounds: " .. n[1])
	n = kinds("placement_fail", true)
	T.ok(n[2] > 0 and n[3] > 0, ("errors %d, text lines %d"):format(n[2], n[3]))
	n = kinds("placement_fail", false)
	T.eq(n[1] + n[2] + n[3] + n[4], 0, "the ring off")
end

-- Unit sounds (PlaySoundAtUnit 0x446940, here through BeginCloak 0x41d200): heard by the local player at the unit's
-- sprite when the unit is theirs or its sprite is seen (in a replay: seen by the world's vision); sound 0 plays
-- nothing; without a unit the sound has no position (KP_EVENT_SOUND_SCREEN).
function S.unit_sounds(T)
	if skip(T, "unit_sounds") then return end
	if not T.has(oracle.DATA .. "dat.kpd") then return end
	local g = golden(T, "scenarios", "marine_group")
	local game = oracle.start_mission(T.read, mission_path(g.mission), g.seed, g.expansion, { events = true })
	oracle.runner(game, {}, {}).to(3)
	oracle.events(game)
	local spr = game.img.spr
	local n = game.lists.active.head
	local s = game.u.sprite[n]
	local function sounds()
		local out = {}
		for _, e in ipairs(oracle.events(game)) do
			if e.kind == 1 then out[#out + 1] = e end
		end
		return out
	end
	game:begin_cloak(n, 0x111)
	local e = sounds()
	T.eq(#e, 1, "the cloak sound")
	T.eq(e[1].a, 0x111)
	T.eq(e[1].b, spr.x[s])
	T.eq(e[1].c, spr.y[s])
	local m = game.u.next[n]
	game:begin_cloak(m, 0)
	T.eq(#sounds(), 0, "sound 0")
	local k = game.u.next[m]
	local owner = game.u.owner[k]
	game.u.owner[k] = (owner + 1) % 8
	spr.visibility[game.u.sprite[k]] = 0
	game:begin_cloak(k, 0x111)
	T.eq(#sounds(), 0, "another player's unseen unit")
	game:play_unit_sound(0x20, 0)
	local all = oracle.events(game)
	T.eq(#all, 1, "a sound without a unit")
	T.eq(all[1].kind, 4)
	T.eq(all[1].a, 0x20)
	-- In a replay the world's vision decides, even for the local player's unit.
	local img = game.img
	img.replay, img.local_vision = 1, 0
	game:play_unit_sound(0x111, n)
	T.eq(#sounds(), 0, "a replay's vision that does not see the unit")
	img.local_vision = 0xff
	game:play_unit_sound(0x111, n)
	T.eq(#sounds(), 1, "a replay's vision that sees it")
	-- The local player is the game's and its world's, with the vision derived from it; a replay's world keeps the
	-- replay's vision.
	game.local_player = 2
	T.eq(img.local_vision, 0xff, "the replay's vision stays")
	img.replay = 0
	game.local_player = 3
	T.eq(game.local_player, 3)
	T.eq(img.local_player, 3, "the world's local player follows")
	T.eq(game.local_vision, 8, "the game's vision follows")
	T.eq(img.local_vision, 8, "the world's vision follows")
	T.ok(not pcall(function() game.local_player = 12 end), "no player 12")
	T.ok(not pcall(function() game.local_player = "3" end), "a player is a number")
	T.ok(not pcall(function() img.local_player = 4 end), "the world takes it from the game")
	T.ok(not pcall(function() game.local_vision = 1 end), "the vision is derived")
	T.eq(game.local_player, 3)
end

-- Whatever raises inside a method (a C failure, a Lua error of the Lua API) reaches the caller as a Lua error and
-- leaves the game usable: its failure handlers and Lua states are restored.
function S.method_errors_leave_the_game_usable(T)
	if skip(T, "method_errors_leave_the_game_usable") then return end
	if not T.has(oracle.DATA .. "dat.kpd") then return end
	local g = golden(T, "scenarios", "marine_group")
	local game = oracle.start_mission(T.read, mission_path(g.mission), g.seed, g.expansion)
	local run = oracle.runner(game, {}, {})
	run.to(3)
	local n = game.lists.active.head
	local ok, err = pcall(game.execute_command, game, 0, 5)
	T.ok(not ok and tostring(err):find("not a string", 1, true), "a number for bytes: " .. tostring(err))
	ok, err = pcall(game.run_turn, game, { { player = 0, bytes = 12 } })
	T.ok(not ok and tostring(err):find("not a string", 1, true), "a number for a command's bytes: " .. tostring(err))
	-- lua_getfield runs the record's __index: a Lua error raised by the Lua API inside the method.
	local record = setmetatable({}, { __index = function() error("record probe", 0) end })
	ok, err = pcall(game.apply_unit_record, game, n, record)
	T.eq(tostring(err), "record probe")
	T.ok(not ok)
	ok, err = pcall(game.img.draw_collect, game.img, 0, 1, nil)
	T.ok(not ok and tostring(err):find("argument 3", 1, true), "a world method's failure: " .. tostring(err))
	ok, err = pcall(game.unit_is_dead, game, 0)
	T.ok(not ok and tostring(err):find("out of range", 1, true), "a C failure after them: " .. tostring(err))
	run.to(40)
	T.ok(game.frame > 0, "the game runs on")
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
