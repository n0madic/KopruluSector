-- Full-game emulator traces exported by tools/oracle/export_emulator_traces.py: the M1
-- reference (scripted Wasteland victory) and the catalog Wasteland run without input.
-- The simulation's digest comparison uses these from phase 3; here the data must load on
-- every Lua backend and be internally consistent.
local DIR = "tests/golden/oracle/missions/"

local function load(T, name)
	return assert(loadstring or load)(T.read(DIR .. name .. ".lua"))()
end

local function field_index(trace)
	local idx = {}
	for i, name in ipairs(trace.fields) do idx[name] = i end
	return idx
end

local function check_digests(T, trace, frames)
	local f = field_index(trace)
	T.eq(#trace.digests, frames)
	for i, row in ipairs(trace.digests) do
		T.eq(row[f.f], i, "frame number")
		T.eq(#row[f.h], 16, "digest hash length")
		T.eq(#row[f.min], 8, "minerals per player")
		T.ok(row[f.rng] >= 0 and row[f.rng] < 4294967296, "rng range")
	end
end

local S = {}

function S.index(T)
	local index = load(T, "index")
	T.eq(index[1], "m1_wasteland")
	T.eq(index[2], "terran01_no_input")
end

function S.m1_wasteland(T)
	local m1 = load(T, "m1_wasteland")
	T.eq(m1.mission, "terran/terran01")
	T.eq(m1.status, "victory")
	T.eq(m1.result.frame, 5586)
	T.eq(m1.result.value, 3)
	check_digests(T, m1, 5632)
	T.ok(#m1.input_log > 0 and #m1.events > 0)
	local last = 0
	for _, e in ipairs(m1.input_log) do
		T.ok(e.t >= last, "input log in time order")
		last = e.t
	end
end

function S.terran01_no_input(T)
	local run = load(T, "terran01_no_input")
	T.eq(run.mission, "terran/terran01")
	check_digests(T, run, 3000)
	T.ok(#run.sounds > 0)
end

return S
