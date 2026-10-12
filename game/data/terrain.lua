-- Tileset container (tilesets/<name>.kpd, kind TSET) and the native terrain import.
-- Layout: docs/formats/kpd.md; writer: converter/kpf/kpd.py; Python twin and native
-- checks: converter/derive/terrain.py.
local bytes = require("game.data.bytes")
local kpf = require("game.data.kpf")

local u8, u16, u32 = bytes.u8, bytes.u16, bytes.u32
local byte = string.byte
local band, bor, rshift, lshift = bit.band, bit.bor, bit.rshift, bit.lshift

local M = {}

M.KIND = "TSET"
-- Tileset file names by ERA & 7 (generated/data/tilesets/<name>.kpd).
M.TILESETS = { [0] = "badlands", "platform", "install", "ashworld", "jungle", "desert", "ice", "twilight" }
M.SCHEMA = 1
M.MFLAGS_ALGORITHM = "MFL1"
M.MFLAGS_VERSION = 1
M.CV5_RECORD = 52
M.CV5_FLAG_MASK = 0xd8fa
M.FLAG_TILE_HIGH_BIT = 0x00400000
M.INITIAL_LOW_WORD = 0xffff

local TWO32 = 4294967296

local function unsigned(v)
	if v < 0 then return v + TWO32 end
	return v
end

-- Returns { cv5 = <raw>, groups = n, mflags = { [megatile] = flags }, rgb = <768 bytes>,
-- cycles = { speed = n, { low, high }, ... } }.
function M.parse_tileset(data, name, expect)
	local c = kpf.parse(data, name)
	expect = expect or {}
	c:check({
		kind = M.KIND, schema_version = M.SCHEMA, profile_id = expect.profile_id, source_hash = expect.source_hash,
		sections = { MFLG = { algorithm_id = M.MFLAGS_ALGORITHM, algorithm_version = M.MFLAGS_VERSION } },
	})
	local cv5 = c:section("CV5 ")
	local m = c:section("MFLG")
	local mflags = {}
	for i = 0, #m / 4 - 1 do mflags[i] = u32(m, 4 * i) end
	local wpe = c:section("WPE ")
	local rgb = {}
	for i = 0, 255 do rgb[i + 1] = wpe:sub(4 * i + 1, 4 * i + 3) end
	local cy = c:section("CYCL")
	local cycles = { speed = u8(cy, 1) }
	for i = 0, u8(cy, 0) - 1 do cycles[i + 1] = { u8(cy, 2 + 2 * i), u8(cy, 3 + 2 * i) } end
	return {
		cv5 = cv5, groups = #cv5 / M.CV5_RECORD, vf4 = c:section("VF4 "), mflags = mflags,
		rgb = table.concat(rgb), cycles = cycles,
	}
end

-- ImportMapTiles 0x445720 over the mission's MTXM. Returns megatile ids and tile flags
-- (0-based arrays, row-major) or nil, cell index when a tile selects a missing CV5 group.
-- Plain tables are a transient form for the map viewer.
-- TODO: the simulation's mutable terrain grid representation (phase 3, plan §4.3).
function M.import(mission, ts)
	local tiles, cv5, groups, mflags = mission.tiles, ts.cv5, ts.groups, ts.mflags
	local ids, flags = {}, {}
	for i = 0, mission.width * mission.height - 1 do
		local lo, hi = byte(tiles, 2 * i + 1, 2 * i + 2)
		local raw = lo + hi * 256
		local group = band(rshift(raw, 4), 0x7ff)
		if group >= groups then return nil, i end
		local base = group * M.CV5_RECORD
		local mega = u16(cv5, base + 20 + band(raw, 15) * 2)
		local f = bor(M.INITIAL_LOW_WORD, lshift(band(u16(cv5, base + 2), M.CV5_FLAG_MASK), 16),
			mflags[band(mega, 0x7fff)])
		if raw >= 0x8000 then
			mega = bor(mega, 0x8000)
			f = bor(f, M.FLAG_TILE_HIGH_BIT)
		end
		ids[i] = mega
		flags[i] = unsigned(f)
	end
	return ids, flags
end

return M
