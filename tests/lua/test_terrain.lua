-- Tileset container and terrain import against the Python twin (converter/derive/terrain.py,
-- which is checked against native output). Fixtures: converter/kpf/fixtures.py.
local terrain = require("game.data.terrain")
local mission = require("game.data.mission")

local DIR = "tests/fixtures/generated/kpf/"

local function expect(T)
	return assert(loadstring or load)(T.read(DIR .. "expect.lua"))()
end

local S = {}

function S.tileset(T)
	local ts = terrain.parse_tileset(T.read(DIR .. "tileset.kpd"), "tileset.kpd")
	T.eq(ts.groups, 70)
	T.eq(#ts.rgb, 768)
	T.eq(ts.rgb:byte(4), 4 * 3 % 256) -- entry 1 red = WPE byte 4
	T.eq(ts.cycles.speed, 8)
	T.eq(#ts.cycles, 3)
	T.eq(ts.cycles[3][2], 254)
	T.ok(ts.mflags[39] ~= nil and ts.mflags[40] == nil)
end

function S.import_matches_python(T)
	local e = expect(T)
	local ts = terrain.parse_tileset(T.read(DIR .. "tileset.kpd"), "tileset.kpd")
	local m = mission.parse(T.read(DIR .. "mission.kpm"), "mission.kpm")
	local ids, flags = terrain.import(m, ts)
	for i = 0, 31 do
		T.eq(ids[i], e.terrain_ids[i + 1], "megatile " .. i)
		T.eq(flags[i], e.terrain_flags[i + 1], "flags " .. i)
	end
end

function S.missing_group_stops(T)
	local ts = terrain.parse_tileset(T.read(DIR .. "tileset.kpd"), "tileset.kpd")
	ts.groups = 63 -- tile 1008 (cell 8) selects group 63, now missing
	local ids, cell = terrain.import(mission.parse(T.read(DIR .. "mission.kpm"), "mission.kpm"), ts)
	T.eq(ids, nil)
	T.eq(cell, 8)
end

return S
