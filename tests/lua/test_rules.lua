-- Override effects against the Python twin (converter/derive/rules.py, checked against the
-- native output of every campaign override section). Fixture: converter/kpf/fixtures.py.
local rules = require("game.data.rules")
local mission = require("game.data.mission")

local DIR = "tests/fixtures/generated/kpf/"

local function total(d, depth)
	-- Same checksum as the fixture: sum((key * 31 + value) % 1000003) % 1000003 with tuple
	-- keys hashed as sum((element + 1) * 997^position).
	local sum = 0
	local function walk(t, level, key)
		for k, v in pairs(t) do
			local hk = key + (k + 1) * 997 ^ level
			if level + 1 < depth then
				walk(v, level + 1, hk)
			else
				local h = depth == 1 and k or hk
				sum = sum + (h * 31 + v) % 1000003
			end
		end
	end
	walk(d, 0, 0)
	return sum % 1000003
end

local S = {}

function S.matches_python(T)
	local e = assert(loadstring or load)(T.read(DIR .. "expect.lua"))()
	local units = { subunit1 = {}, ground_weapon = {}, air_weapon = {} }
	for i, a in ipairs(e.rule_associations) do
		units.subunit1[i - 1], units.ground_weapon[i - 1], units.air_weapon[i - 1] = a[1], a[2], a[3]
	end
	local eff = rules.effects(mission.parse_rules(T.read(DIR .. "rules.bin")), units, false)
	local want = e.rule_sums
	for _, f in ipairs(rules.UNIT_FIELDS) do T.eq(total(eff.units[f[1]], 1), want["unit_" .. f[1]], f[1]) end
	for _, f in ipairs(rules.UPGRADE_FIELDS) do T.eq(total(eff.upgrades[f], 1), want["upgrade_" .. f], f) end
	for _, f in ipairs(rules.TECH_FIELDS) do T.eq(total(eff.techs[f], 1), want["tech_" .. f], f) end
	T.eq(total(eff.weapon_damage, 1), want.weapon_damage, "damage")
	T.eq(total(eff.weapon_bonus, 1), want.weapon_bonus, "bonus")
	T.eq(total(eff.availability, 2), want.availability, "availability")
	T.eq(total(eff.upgrade_levels, 3), want.upgrade_levels, "upgrade levels")
	T.eq(total(eff.tech_states, 3), want.tech_states, "tech states")
end

return S
