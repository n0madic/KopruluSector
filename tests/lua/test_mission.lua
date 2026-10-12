-- Reads the mission fixture written by converter/kpf/fixtures.py (run it first).
local mission = require("game.data.mission")
local strings = require("game.data.strings")
local bytes = require("game.data.bytes")

local DIR = "tests/fixtures/generated/kpf/"

local function expect(T)
	return assert(loadstring or load)(T.read(DIR .. "expect.lua"))()
end

local function load_fixture(T)
	local e = expect(T)
	return mission.parse(T.read(DIR .. "mission.kpm"), "mission.kpm",
		{ profile_id = "bw-1.17.0.1", source_hash = e.source_hash }), e
end

local S = {}

function S.meta(T)
	local m = load_fixture(T)
	T.eq(m.chk_version, 205)
	T.eq(m.era, 13)
	T.eq(m.tileset, 5)
	T.eq(m.width, 8)
	T.eq(m.height, 4)
	for i = 1, 12 do
		T.eq(m.owners[i], i - 1, "owner " .. i)
		T.eq(m.races[i], 19 + i, "race " .. i)
	end
	for i = 1, 8 do T.eq(m.forces.player[i], i, "force of player " .. i) end
	T.eq(m.forces.name[1], 9 + 10 * 256)
	T.eq(m.forces.flags[4], 20)
	T.eq(m.name_string, 1)
	T.eq(m.description_string, 4)
	T.eq(m.colors[1], 7)
	T.eq(m.colors[8], 0)
end

function S.raw_sections(T)
	local m = load_fixture(T)
	T.eq(#m.tiles, 64)
	T.eq(bytes.u16(m.tiles, 0), 1000)
	T.eq(bytes.u16(m.tiles, 60), 1030)
	T.eq(bytes.u16(m.tiles, 62), 1000 + 0x8000)
	T.eq(bytes.u8(m.mask, 31), 31)
	T.eq(#m.locations, 255 * 20)
	T.eq(#m.properties, 1280)
	T.eq(#m.triggers, 2400)
	T.eq(#m.briefing, 2400)
	T.eq(m.units, "")
	T.eq(m.sprites, "")
end

function S.rules_in_dispatch_order(T)
	local m = load_fixture(T)
	local tags = {}
	for i, r in ipairs(m.rules) do tags[i] = r.tag .. ":" .. #r.data end
	T.eq(table.concat(tags, " "), "UNIx:4168 UPGx:794 TECx:396 PUNI:5700 PUPx:2318 PTEx:1672")
end

function S.sounds(T)
	local m = load_fixture(T)
	T.eq(m.sounds[2], 0)
	T.eq(m.sounds[3], false)
	T.eq(m.sounds[1], nil)
	T.eq(m.sound_info[2].length, 4410)
	T.eq(m.sound_info[2].avg_bytes_per_sec, 44100)
	T.eq(m.sound_info[3].length, 0)
end

function S.map_strings(T)
	local m, e = load_fixture(T)
	T.eq(strings.count(m.strings), #e.mission_strings)
	for i, s in ipairs(e.mission_strings) do T.eq(strings.get(m.strings, i), s, "string " .. i) end
	T.eq(strings.get(m.strings, 0), nil)
	T.eq(strings.get(m.strings, #e.mission_strings + 1), "")
end

function S.derived_sections(T)
	local m, e = load_fixture(T)
	local d = e.mission_derived
	T.eq(#m.walk, d.walk_size)
	T.eq(m.regions.count, d.regions)
	T.eq(m.regions.split_count, d.splits)
	local last = mission.region(m.regions, d.regions - 1)
	T.eq(last.class, d.last_class)
	T.eq(last.group, d.last_group)
	T.eq(last.tiles, d.last_tiles)
	T.eq(#last.neighbors, #d.last_neighbors)
	for i, n in ipairs(d.last_neighbors) do T.eq(last.neighbors[i], n, "neighbour " .. i) end
	T.eq(last.center_x, d.last_center_x)
	T.eq(mission.region_cell(m.regions, 3, 2), d.cell_3_2)
	for k = 1, 4 do
		T.eq(m.contours.counts[k], d.contour_counts[k], "contour count " .. k)
		T.eq(m.contours.capacities[k], d.capacities[k], "capacity " .. k)
	end
	local axis, start, stop, dir, info = mission.contour(m.contours, 1, 1)
	local f = d.first_contour
	T.eq(axis, f[1]); T.eq(start, f[2]); T.eq(stop, f[3]); T.eq(dir, f[4]); T.eq(info, f[5])
end

function S.packs(T)
	local m = load_fixture(T)
	T.eq(#m.packs, 2)
	T.eq(m.packs[1], "effects")
	T.eq(m.packs[2], "terran")
end

function S.rejects_other_kinds(T)
	T.raises(function() mission.parse(T.read(DIR .. "basic.kpd"), "basic.kpd") end, "file kind")
	local e = expect(T)
	T.raises(function()
		mission.parse(T.read(DIR .. "mission.kpm"), "mission.kpm", { source_hash = string.rep("x", 32) })
	end, "different data set")
end

return S
