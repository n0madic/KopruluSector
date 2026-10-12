-- Effects of the UMS override sections (mission.rules, native dispatch order) on the game
-- tables. Lua twin of converter/derive/rules.py, which reproduces the native output of
-- every campaign override section; see that file for the handler rules.
local bytes = require("game.data.bytes")

local u8, u16 = bytes.u8, bytes.u16

local M = {}

M.UNITS, M.WEAPONS, M.PLAYERS = 228, 130, 12
M.NO_WEAPON, M.NO_SUBUNIT = 130, 228
M.UNIT_FIELDS = { { "health", 4 }, { "shields", 2 }, { "armor", 1 }, { "build_time", 2 },
	{ "mineral_cost", 2 }, { "gas_cost", 2 }, { "name", 2 } }
M.UPGRADE_FIELDS = { "mineral_base", "mineral_factor", "gas_base", "gas_factor", "time_base", "time_factor" }
M.TECH_FIELDS = { "mineral_cost", "gas_cost", "research_time", "energy" }
local LAYOUT = {
	UNIS = { 228, 100, 0xfd0 }, UNIx = { 228, 130, 0x1048 },
	UPGS = { 46, 0, 0x256 }, UPGx = { 61, 1, 0x31a }, TECS = { 24, 0, 0xd8 }, TECx = { 44, 0, 0x18c },
	UPGR = { 46, 0, 0x6d4 }, PUPx = { 61, 0, 0x90e }, PTEC = { 24, 0, 0x390 }, PTEx = { 44, 0, 0x688 },
	PUNI = { 228, 0, 0x1644 },
}
local BW_UPGRADES, BW_TECHS = 46, 24

local function uint(s, at, width)
	if width == 1 then return u8(s, at) end
	if width == 2 then return u16(s, at) end
	return bytes.u32(s, at)
end

-- Effects keyed like the Python twin: units[field][unit], weapon_damage[w], weapon_bonus[w],
-- upgrades[field][id], techs[field][id], availability[p][unit],
-- upgrade_levels[f][p][id] (f 0 max, 1 start), tech_states[f][p][id] (0 available, 1 researched).
function M.new()
	local e = { units = {}, weapon_damage = {}, weapon_bonus = {}, upgrades = {}, techs = {}, availability = {},
		upgrade_levels = { [0] = {}, [1] = {} }, tech_states = { [0] = {}, [1] = {} } }
	for _, f in ipairs(M.UNIT_FIELDS) do e.units[f[1]] = {} end
	for _, f in ipairs(M.UPGRADE_FIELDS) do e.upgrades[f] = {} end
	for _, f in ipairs(M.TECH_FIELDS) do e.techs[f] = {} end
	for p = 0, M.PLAYERS - 1 do
		e.availability[p] = {}
		for f = 0, 1 do e.upgrade_levels[f][p], e.tech_states[f][p] = {}, {} end
	end
	return e
end

-- units: { subunit1 = {}, ground_weapon = {}, air_weapon = {} } (dat.kpd units table).
function M.apply(e, tag, raw, units, expansion)
	local layout = LAYOUT[tag] or error("rules: unknown section " .. tag, 0)
	local count, extra, length = layout[1], layout[2], layout[3]
	if #raw ~= length then error(("rules: %s is %d bytes, expected %d"):format(tag, #raw, length), 0) end
	if tag == "UNIS" or tag == "UNIx" then
		local offset, fields = M.UNITS, {}
		for _, f in ipairs(M.UNIT_FIELDS) do
			fields[#fields + 1] = { f[1], offset, f[2] }
			offset = offset + M.UNITS * f[2]
		end
		for u = M.UNITS - 1, 0, -1 do
			if u8(raw, u) == 0 then
				for _, f in ipairs(fields) do e.units[f[1]][u] = uint(raw, f[2] + u * f[3], f[3]) end
				local sub = units.subunit1[u]
				local weapon_unit = sub == M.NO_SUBUNIT and u or sub
				for _, w in ipairs({ units.ground_weapon[weapon_unit], units.air_weapon[weapon_unit] }) do
					if w ~= M.NO_WEAPON then
						if w >= extra then
							error(("rules: %s custom unit %d uses weapon %d past the native buffer"):format(tag, u, w), 0)
						end
						e.weapon_damage[w] = u16(raw, offset + 2 * w)
						e.weapon_bonus[w] = u16(raw, offset + 2 * (extra + w))
					end
				end
			end
		end
	elseif tag == "UPGS" or tag == "UPGx" or tag == "TECS" or tag == "TECx" then
		local upgrade = tag:sub(1, 3) == "UPG"
		local target, names = upgrade and e.upgrades or e.techs, upgrade and M.UPGRADE_FIELDS or M.TECH_FIELDS
		for u = 0, count - 1 do
			if u8(raw, u) == 0 then
				for f, name in ipairs(names) do target[name][u] = u16(raw, count + extra + ((f - 1) * count + u) * 2) end
			end
		end
	elseif tag == "PUNI" then
		for p = 0, M.PLAYERS - 1 do
			for u = 0, M.UNITS - 1 do
				local i = p * M.UNITS + u
				e.availability[p][u] = u8(raw, 13 * M.UNITS + i) ~= 0 and u8(raw, 12 * M.UNITS + u) or u8(raw, i)
			end
		end
	else
		local upgrade = tag == "UPGR" or tag == "PUPx"
		local target, gated = upgrade and e.upgrade_levels or e.tech_states, upgrade and BW_UPGRADES or BW_TECHS
		for p = 0, M.PLAYERS - 1 do
			for u = 0, count - 1 do
				local i = p * count + u
				local default = u8(raw, 26 * count + i) ~= 0
				for _, f in ipairs({ 1, 0 }) do
					if not (f == 1 and u >= gated and not expansion) then
						target[f][p][u] = default and u8(raw, (24 + f) * count + u) or u8(raw, f * 12 * count + i)
					end
				end
			end
		end
	end
	return e
end

-- All override sections of a mission (mission.rules) in order.
function M.effects(rule_list, units, expansion)
	local e = M.new()
	for _, r in ipairs(rule_list) do M.apply(e, r.tag, r.data, units, expansion) end
	return e
end

return M
