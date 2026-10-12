-- Preplaced UNIT records (mission.units, 36 bytes each) and their native property rules.
-- Lua twin of converter/derive/unit_records.py, which reproduces the native outputs of all
-- campaign property inputs, the melee filter and every campaign start location.
local bytes = require("game.data.bytes")

local u8, u16, u32 = bytes.u8, bytes.u16, bytes.u32
local band = bit.band

local M = {}

M.RECORD = 36
M.VALID_HP = 0x00020000
M.VALID_SHIELDS = 0x00040000
M.VALID_ENERGY = 0x00080000
M.VALID_RESOURCES = 0x00100000
M.VALID_HANGAR = 0x00200000
M.STATE_INVINCIBLE = 0x10
M.STATUS_INVINCIBLE = 0x04000000
M.RESOURCE_CONTAINER = 0x2000
M.START_LOCATION = 214
M.HERO_ENERGY, M.NORMAL_ENERGY = 250, 200
M.NEUTRAL_OWNER = 11
local MELEE_KEPT = { [176] = true, [177] = true, [178] = true, [188] = true, [89] = true, [90] = true,
	[93] = true, [94] = true, [95] = true, [96] = true }

-- Decodes record i (1-based) of a UNIT payload.
function M.record(s, i)
	local a = (i - 1) * M.RECORD
	return {
		serial = u32(s, a), x = u16(s, a + 4), y = u16(s, a + 6), type = u16(s, a + 8),
		relation_flags = u16(s, a + 10), valid_states = u16(s, a + 12), valid_properties = u16(s, a + 14),
		owner = u8(s, a + 16), hp_percent = u8(s, a + 17), shield_percent = u8(s, a + 18),
		energy_percent = u8(s, a + 19), resources = u32(s, a + 20), hangar = u16(s, a + 24),
		states = u16(s, a + 26), related_serial = u32(s, a + 32),
	}
end

function M.count(s)
	return #s / M.RECORD
end

function M.valid_flags(r)
	return r.valid_states + r.valid_properties * 65536
end

-- ApplyUnitRecordProperties 0x46a420. `max` = { hp_fixed=, shields=, energy=, resource_container= };
-- `current` = { health_fixed=, shields_fixed=, energy_fixed=, status=, resources= } is updated.
function M.apply_properties(r, max, current)
	local valid = M.valid_flags(r)
	if band(valid, M.VALID_HP) ~= 0 then
		local value = math.floor(((r.hp_percent * max.hp_fixed) % 4294967296) / 100)
		current.health_fixed = math.min(math.max(value, 1), max.hp_fixed)
	end
	if band(valid, M.VALID_SHIELDS) ~= 0 then
		current.shields_fixed = math.min(math.floor(max.shields * r.shield_percent / 100), max.shields) * 256
	end
	if band(valid, M.VALID_ENERGY) ~= 0 then
		current.energy_fixed = math.min(max.energy, math.floor(max.energy * r.energy_percent / 100) % 256) * 256
	end
	if band(valid, M.STATE_INVINCIBLE) ~= 0 and band(r.states, M.STATE_INVINCIBLE) ~= 0 then
		if current.status % (2 * M.STATUS_INVINCIBLE) < M.STATUS_INVINCIBLE then
			current.status = current.status + M.STATUS_INVINCIBLE
		end
	end
	if band(valid, M.VALID_RESOURCES) ~= 0 and max.resource_container then
		current.resources = r.resources % 65536
	end
	return current
end

-- 0x469600: with the melee mode byte set, only neutral resources and critters stay.
function M.excluded_by_melee_filter(mode_byte, owner, unit_type)
	if mode_byte == 0 then return false end
	return owner ~= M.NEUTRAL_OWNER or not MELEE_KEPT[unit_type]
end

-- 0x46a280 outside replays: x, y (nil when not a start location) and the camera tile
-- (cx, cy) when the owner is the local player.
function M.start_location(r, local_player)
	if r.type ~= M.START_LOCATION then return nil end
	if r.owner == local_player then
		return r.x, r.y, math.max(0, math.floor(r.x / 32) - 10), math.max(0, math.floor(r.y / 32) - 6)
	end
	return r.x, r.y
end

return M
