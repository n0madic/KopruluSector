-- Mission container (.kpm, kind MISN) reader. Layout: docs/formats/kpm.md;
-- writer: converter/kpf/kpm.py. Large sections stay strings (plan §4.3); runtime
-- structures are built from them by the simulation when a mission starts.
local bytes = require("game.data.bytes")
local kpf = require("game.data.kpf")

local u8, u16, u32 = bytes.u8, bytes.u16, bytes.u32

local M = {}

M.KIND = "MISN"
M.SCHEMA = 1
M.NO_SOUND = 0xffff
M.META_COLR = 0x01

-- Raw CHK payloads kept unchanged: field name -> section.
local RAW = {
	tiles = "MTXM", mask = "MASK", sprites = "SPRT", units = "UNIT", locations = "LOCS",
	strings = "STRS", triggers = "TRIG", briefing = "MBRF", properties = "UPRP",
}

local function byte_list(s, off, n)
	local t = {}
	for i = 1, n do t[i] = u8(s, off + i - 1) end
	return t
end

local function parse_meta(m)
	local forces = { player = byte_list(m, 32, 8), name = {}, flags = byte_list(m, 48, 4) }
	for i = 1, 4 do forces.name[i] = u16(m, 40 + 2 * (i - 1)) end
	local flags = u8(m, 64)
	return {
		chk_version = u16(m, 0),
		era = u16(m, 2),
		width = u16(m, 4),
		height = u16(m, 6),
		owners = byte_list(m, 8, 12),
		races = byte_list(m, 20, 12),
		forces = forces,
		name_string = u16(m, 52),
		description_string = u16(m, 54),
		colors = (flags % 2 == 1) and byte_list(m, 56, 8) or nil,
	}
end

-- Ordered override sections: { {tag=, data=}, ... } in native dispatch order.
local function parse_rules(r)
	local out, pos = {}, 0
	while pos < #r do
		local size = u32(r, pos + 4)
		out[#out + 1] = { tag = r:sub(pos + 1, pos + 4), data = r:sub(pos + 9, pos + 8 + size) }
		pos = pos + 8 + size
	end
	return out
end

-- WAV string id -> index in the mission sound collection, or false when the file is missing.
local function parse_sounds(w)
	local out = {}
	for i = 0, u16(w, 0) - 1 do
		local index = u16(w, 4 + 4 * i)
		out[u16(w, 2 + 4 * i)] = index ~= M.NO_SOUND and index
	end
	return out
end

-- expect: optional extra container checks (profile_id, source_hash).
function M.parse(data, name, expect)
	local c = kpf.parse(data, name)
	expect = expect or {}
	c:check({ kind = M.KIND, schema_version = M.SCHEMA, profile_id = expect.profile_id,
		source_hash = expect.source_hash })
	local mission = parse_meta(c:section("META"))
	mission.tileset = mission.era % 8 -- ERA handler 0x468b00 masks with 7 (expansion on)
	for field, fourcc in pairs(RAW) do mission[field] = c:section(fourcc) end
	mission.rules = parse_rules(c:section("RULE"))
	mission.sounds = parse_sounds(c:section("WAVS"))
	if #mission.tiles ~= 2 * mission.width * mission.height then
		error(("mission: %s: MTXM size does not match %dx%d"):format(name, mission.width, mission.height), 0)
	end
	return mission
end

return M
