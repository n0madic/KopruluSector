-- The native core's foundations (stage N.0 of ADR 0008) against their Lua counterparts: the
-- suites of game/simulation/int.lua and rng.lua run unchanged on kp_core.int and kp_core.rng,
-- and the loaders return what game/data/kpf.lua, dat.lua and mission.lua return for every
-- converted data file. Skipped without the module (tools/native/build.sh, or a Defold engine
-- with the extension).
local dat = require("game.data.dat")
local kpf = require("game.data.kpf")
local mission = require("game.data.mission")
local native = require("game.native")
local state_model = require("tools.lua.state_model")

local core = native.find("kp_core")

local S = {}

local function skip(T, what)
	if core then return false end
	T.env.log("SKIP test_kp_core." .. what .. ": no kp_core module")
	return true
end

-- Runs every test of `suite_module` with `module` replaced by `replacement` while it loads.
local function run_suite_with(T, suite_module, module, replacement)
	local saved_suite, saved = package.loaded[suite_module], package.loaded[module]
	package.loaded[suite_module], package.loaded[module] = nil, replacement
	local ok, suite = pcall(require, suite_module)
	package.loaded[suite_module], package.loaded[module] = saved_suite, saved
	if not ok then error(suite, 0) end
	local names = {}
	for name in pairs(suite) do names[#names + 1] = name end
	table.sort(names)
	for _, name in ipairs(names) do
		local passed, err = pcall(suite[name], T)
		if not passed then error(("%s.%s on kp_core: %s"):format(suite_module, name, tostring(err)), 0) end
	end
	T.ok(#names > 0, suite_module .. " has tests")
end

function S.int_suite(T)
	if skip(T, "int_suite") then return end
	run_suite_with(T, "tests.lua.test_int", "game.simulation.int", core.int)
end

function S.rng_suite(T)
	if skip(T, "rng_suite") then return end
	run_suite_with(T, "tests.lua.test_rng", "game.simulation.rng", core.rng)
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
