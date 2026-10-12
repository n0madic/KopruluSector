-- UNIT record property rules against the Python twin (fixture: converter/kpf/fixtures.py).
local ur = require("game.data.unit_records")

local DIR = "tests/fixtures/generated/kpf/"

local S = {}

function S.matches_python(T)
	local e = assert(loadstring or load)(T.read(DIR .. "expect.lua"))()
	local s = T.read(DIR .. "units.bin")
	T.eq(ur.count(s), #e.unit_rows)
	for i, row in ipairs(e.unit_rows) do
		local r = ur.record(s, i)
		local max = { hp_fixed = row[1], shields = row[2], energy = row[3] and ur.HERO_ENERGY or ur.NORMAL_ENERGY,
			resource_container = row[4] }
		local c = ur.apply_properties(r, max, { health_fixed = row[1], shields_fixed = row[2] * 256,
			energy_fixed = 25600, status = 0, resources = 42330 })
		T.eq(c.health_fixed, row[5], "health " .. i)
		T.eq(c.shields_fixed, row[6], "shields " .. i)
		T.eq(c.energy_fixed, row[7], "energy " .. i)
		T.eq(c.status, row[8], "status " .. i)
		T.eq(c.resources, row[9], "resources " .. i)
		local x, y, cx, cy = ur.start_location(r, 3)
		T.eq(x or -1, row[10], "start x " .. i)
		T.eq(y or -1, row[11], "start y " .. i)
		T.eq(cx or -1, row[12], "camera x " .. i)
		T.eq(cy or -1, row[13], "camera y " .. i)
	end
end

function S.melee_filter(T)
	T.eq(ur.excluded_by_melee_filter(0, 0, 0), false)
	T.eq(ur.excluded_by_melee_filter(1, 11, 176), false)
	T.eq(ur.excluded_by_melee_filter(1, 11, 175), true)
	T.eq(ur.excluded_by_melee_filter(1, 0, 176), true)
end

return S
