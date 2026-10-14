-- The damage table of original code (phase 4 oracle): WeaponDamage of every weapon at every unit type, with the
-- target owner's upgrades at levels 0 and 3, equals the rows of tests/golden/oracle/damage_table.lua
-- (tools/research/probe_damage_table_117.py ran the original's 0x41dfc0 on the DAT of an expansion game). The target is
-- one unit retyped for each row, as the probe's CUnit: player 0, incomplete, hit points 0x40000000, full shields;
-- the shot is the weapon's damage << 8, divisor 1, direction 0, no attacker, player 1.
local native_only = require("tests.lua.native_only")
local oracle = require("tools.lua.oracle")
local crc32 = require("game.data.crc32")

local GOLDEN = "tests/golden/oracle/damage_table.lua"
local MISSION = "generated/scenarios/medic_heal.kpm" -- an expansion game: the Brood War DAT
local ZEALOT = 65 -- a sprite with a shield LO for the shield hit image
local UNIT_TYPES, WEAPONS, UPGRADES = 228, 130, 61
local HP = 0x40000000
local TARGET_PLAYER, ATTACKING_PLAYER = 0, 1

local S = {}

function S.damage_table_equals_the_original(T)
	if not native_only.ready(T, "test_damage_table", { oracle.DATA .. "dat.kpd", MISSION }) then return end
	local golden = assert((loadstring or load)(T.read(GOLDEN)))()
	local g = oracle.start_mission(T.read, MISSION, 1, 1)
	local u, units, weapons = g.u, g.dat.units, g.dat.weapons
	local n = g:unit_create(ZEALOT, 1000, 1000, TARGET_PLAYER)
	T.ok(n ~= 0, "the target unit")
	local bad = {}
	for _, level in ipairs(golden.levels) do
		for id = 0, UPGRADES - 1 do g.upgrades[TARGET_PLAYER][id] = level end
		for t = 0, UNIT_TYPES - 1 do
			local shields = units.shield_enabled[t] ~= 0 and units.shield_amount[t] * 256 or 0
			local row = {}
			u.type[n] = t
			for w = 0, WEAPONS - 1 do
				u.hp[n], u.shields[n], u.status[n] = HP, shields, 0
				g:weapon_damage(n, weapons.damage_amount[w] * 256, w, 1, 0, 0, ATTACKING_PLAYER)
				row[#row + 1] = (HP - u.hp[n]) .. "/" .. (shields - u.shields[n])
			end
			if crc32.of(table.concat(row, " ")) ~= golden.rows[level][t + 1] then
				bad[#bad + 1] = ("level %d unit type %d"):format(level, t)
			end
		end
	end
	T.eq(table.concat(bad, ", "), "", "rows that differ from the original")
end

return S
