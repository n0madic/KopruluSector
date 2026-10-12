-- Reads dat.kpd written by converter/kpf/fixtures.py (run it first).
local dat = require("game.data.dat")

local DIR = "tests/fixtures/generated/kpf/"

local function expect(T)
	return assert(loadstring or load)(T.read(DIR .. "expect.lua"))()
end

local S = {}

function S.columns_match_python(T)
	local e = expect(T)
	local t = dat.parse(T.read(DIR .. "dat.kpd"), "dat.kpd", { source_hash = e.source_hash })
	T.ok(#e.dat_probes > 0)
	for _, p in ipairs(e.dat_probes) do
		local name, column, id, element, want = p[1], p[2], p[3], p[4], p[5]
		local layout = t.layout[name].columns[column]
		T.eq(t[name][column][id * layout.per_id + element], want, name .. "." .. column .. "[" .. id .. "]")
	end
end

function S.layout(T)
	local t = dat.parse(T.read(DIR .. "dat.kpd"), "dat.kpd")
	T.eq(t.layout.units.records, 228)
	T.eq(t.layout.units.columns.unit_extents.per_id, 4)
	T.eq(t.layout.units.columns.infestation.first_id, 106)
	T.eq(t.units.infestation[105], nil)
	T.ok(t.units.infestation[106] ~= nil)
	T.eq(t.layout.weapons.records, 130)
	T.eq(t.layout.images.records, 999)
end

function S.rejects_other_kinds(T)
	T.raises(function() dat.parse(T.read(DIR .. "mission.kpm"), "mission.kpm") end, "file kind")
end

return S
