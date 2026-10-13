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
-- Derived sections and the producer versions this build reads (docs/formats/kpm.md).
M.DERIVED = {
	WALK = { algorithm_id = "WLK1", algorithm_version = 1 },
	RGNS = { algorithm_id = "RGN1", algorithm_version = 2 },
	CONT = { algorithm_id = "CNT1", algorithm_version = 1 },
	PACK = { algorithm_id = "PAK1", algorithm_version = 1 },
}
M.REGION_SIZE, M.SPLIT_SIZE, M.CONTOUR_SIZE = 32, 6, 8
M.SECTIONS = { WAVS = { schema_version = 2 } }
for name, want in pairs(M.DERIVED) do M.SECTIONS[name] = want end

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
function M.parse_rules(r)
	local out, pos = {}, 0
	while pos < #r do
		local size = u32(r, pos + 4)
		out[#out + 1] = { tag = r:sub(pos + 1, pos + 4), data = r:sub(pos + 9, pos + 8 + size) }
		pos = pos + 8 + size
	end
	return out
end

-- WAV string id -> index in the mission sound collection, or false when the file is missing;
-- and string id -> { length=, avg_bytes_per_sec= } RIFF metadata for the native duration.
local function parse_sounds(w)
	local out, info = {}, {}
	for i = 0, u16(w, 0) - 1 do
		local a = 2 + 12 * i
		local sid, index = u16(w, a), u16(w, a + 2)
		out[sid] = index ~= M.NO_SOUND and index
		info[sid] = { length = u32(w, a + 4), avg_bytes_per_sec = u32(w, a + 8) }
	end
	return out, info
end

-- PACK: resource packs the mission loads (ADR 0004), sorted names.
function M.parse_packs(s)
	local out, pos = {}, 1
	for i = 1, u8(s, 0) do
		local len = u8(s, pos)
		out[i] = s:sub(pos + 2, pos + 1 + len)
		pos = pos + 1 + len
	end
	return out
end

-- RGNS: header, then region records, neighbour pool, W x H raster, split records.
function M.parse_regions(s)
	local r = { data = s, width = u16(s, 0), height = u16(s, 2), count = u16(s, 4), split_count = u16(s, 6),
		pool_words = u32(s, 8) }
	r.regions_at = 12
	r.pool_at = r.regions_at + r.count * M.REGION_SIZE
	r.raster_at = r.pool_at + 2 * r.pool_words
	r.splits_at = r.raster_at + 2 * r.width * r.height
	if r.splits_at + r.split_count * M.SPLIT_SIZE ~= #s then error("mission: RGNS size mismatch", 0) end
	return r
end

-- Region i (0-based, original ID order) as a table; neighbours in native order.
function M.region(r, i)
	local s, a = r.data, r.regions_at + i * M.REGION_SIZE
	local count, offset = u8(s, a + 7), u32(s, a + 12)
	local neighbors = {}
	for k = 1, count do neighbors[k] = u16(s, r.pool_at + 2 * (offset + k - 1)) end
	return { class = u16(s, a), group = u16(s, a + 2), tiles = u16(s, a + 4), ground = u8(s, a + 6),
		zero_neighbors = u8(s, a + 8), cut_rank = u8(s, a + 9), neighbors = neighbors,
		center_x = bytes.s32(s, a + 16), center_y = bytes.s32(s, a + 20),
		bounds = { bytes.s16(s, a + 24), bytes.s16(s, a + 26), bytes.s16(s, a + 28), bytes.s16(s, a + 30) } }
end

-- Region or split id of map tile (x, y).
function M.region_cell(r, x, y)
	return u16(r.data, r.raster_at + 2 * (y * r.width + x))
end

-- CONT: four counts, four native capacities, then top/right/bottom/left 8-byte records.
function M.parse_contours(s)
	local c = { data = s, counts = {}, capacities = {}, at = {} }
	local pos = 16
	for d = 1, 4 do
		c.counts[d], c.capacities[d] = u16(s, 2 * (d - 1)), u16(s, 8 + 2 * (d - 1))
		c.at[d] = pos
		pos = pos + c.counts[d] * M.CONTOUR_SIZE
	end
	if pos ~= #s then error("mission: CONT size mismatch", 0) end
	return c
end

-- Record k (1-based) of direction d (1 top, 2 right, 3 bottom, 4 left): axis, start, end, dir, info.
function M.contour(c, d, k)
	local s, a = c.data, c.at[d] + (k - 1) * M.CONTOUR_SIZE
	return bytes.s16(s, a), bytes.s16(s, a + 2), bytes.s16(s, a + 4), u8(s, a + 6), u8(s, a + 7)
end

-- expect: optional extra container checks (profile_id, source_hash).
function M.parse(data, name, expect)
	local c = kpf.parse(data, name)
	expect = expect or {}
	c:check({ kind = M.KIND, schema_version = M.SCHEMA, profile_id = expect.profile_id,
		source_hash = expect.source_hash, sections = M.SECTIONS })
	local mission = parse_meta(c:section("META"))
	mission.tileset = mission.era % 8 -- ERA handler 0x468b00 masks with 7 (expansion on)
	for field, fourcc in pairs(RAW) do mission[field] = c:section(fourcc) end
	mission.rules = M.parse_rules(c:section("RULE"))
	mission.sounds, mission.sound_info = parse_sounds(c:section("WAVS"))
	-- Derived terrain data stays packed (plan §4.3); the simulation builds its state from it.
	mission.walk = c:section("WALK") -- (height*4) rows of (width*4) mini-cells
	mission.regions = M.parse_regions(c:section("RGNS"))
	mission.contours = M.parse_contours(c:section("CONT"))
	mission.packs = M.parse_packs(c:section("PACK"))
	if #mission.tiles ~= 2 * mission.width * mission.height then
		error(("mission: %s: MTXM size does not match %dx%d"):format(name, mission.width, mission.height), 0)
	end
	return mission
end

return M
