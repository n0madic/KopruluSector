-- IScript VM against the original code: every animation of every image of the retail
-- iscript.bin, run by tests/lua/iscript_trace.lua and compared with the hashes the
-- original-code probe (tools/research/probe_native_iscript_117.py) recorded in
-- tests/golden/oracle/iscript/cases.lua. Needs the converter's generated/data; skipped
-- without it (and in the headless Defold build, which does not bundle generated/).
local trace = require("tests.lua.iscript_trace")
local dat = require("game.data.dat")
local grpmeta = require("game.data.grpmeta")
local scripts = require("game.data.scripts")

local DATA = "generated/data/"
local GOLDEN = "tests/golden/oracle/iscript/cases.lua"
-- LuaJIT with the JIT runs every case; the interpreters run every STRIDE-th one.
local STRIDE = 8

local S = {}

local function golden(T)
	return assert((loadstring or load)(T.read(GOLDEN)))()
end

function S.scenario_matches_probe(T)
	local g = golden(T)
	T.eq(g.frames, trace.FRAMES)
	T.eq(g.seed, trace.SEED)
	T.eq(g.root_sprite, trace.ROOT_SPRITE)
	T.eq(g.root_x, trace.ROOT_X)
	T.eq(g.root_y, trace.ROOT_Y)
	T.eq(g.map, trace.MAP)
	T.ok(#g.cases > 2900, "case count")
end

function S.every_animation_equals_original(T)
	if not T.has(DATA .. "iscript.kpd") then
		T.env.log("SKIP test_iscript_golden: no " .. DATA)
		return
	end
	local data = {
		dat = dat.parse(T.read(DATA .. "dat.kpd"), "dat.kpd"),
		grp = grpmeta.parse(T.read(DATA .. "grpmeta.kpd"), "grpmeta.kpd"),
		iscript = scripts.parse_iscript(T.read(DATA .. "iscript.kpd"), "iscript.kpd"),
	}
	local g = golden(T)
	local stride = (jit and jit.status and jit.status()) and 1 or STRIDE
	local bad, ran = {}, 0
	for k = 1, #g.cases, stride do
		local c = g.cases[k]
		local r = trace.run(data, c[1], c[2], g.frames)
		ran = ran + 1
		if r.frames ~= c[3] or r.hash ~= c[4] then
			bad[#bad + 1] = ("image %d anim %d: frames %d/%d hash %d/%d %s"):format(c[1], c[2], r.frames, c[3],
				r.hash, c[4], r.fault or "")
		end
	end
	T.ok(#bad == 0, #bad .. " of " .. ran .. " cases differ from the original:\n" .. table.concat(bad, "\n", 1,
		math.min(#bad, 20)))
end

-- Synthetic programs appended to the retail script (opcodes and operand ranges no stock
-- animation reaches without a unit): retail code with the probe's tail and index.
function S.synthetic_programs_equal_original(T)
	if not T.has(DATA .. "iscript.kpd") then return end
	local g = golden(T)
	local retail = scripts.parse_iscript(T.read(DATA .. "iscript.kpd"), "iscript.kpd").code
	T.eq(#retail, g.retail_size)
	local tail = g.synthetic_iscript:gsub("..", function(h) return string.char(tonumber(h, 16)) end)
	local index = g.synthetic_index
	local code = string.char(index % 256, math.floor(index / 256) % 256, math.floor(index / 65536) % 256, 0)
		.. retail:sub(5) .. tail
	local data = {
		dat = dat.parse(T.read(DATA .. "dat.kpd"), "dat.kpd"),
		grp = grpmeta.parse(T.read(DATA .. "grpmeta.kpd"), "grpmeta.kpd"),
		iscript = trace.raw_iscript(code),
	}
	for _, c in ipairs(g.synthetic) do
		local r = trace.run(data, c[1], c[3], g.frames, false, c[2])
		T.eq(r.frames, c[4], ("image %d anim %d frames (%s)"):format(c[1], c[3], tostring(r.fault)))
		T.eq(r.hash, c[5], ("image %d anim %d hash"):format(c[1], c[3]))
	end
end

return S
