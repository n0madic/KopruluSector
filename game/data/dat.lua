-- dat.kpd reader: every DAT column unpacked once into a Lua array (SoA, plan §4.3).
-- Layout: docs/formats/kpd.md; writer: converter/kpf/kpd.py.
--
-- tables.units.hit_points[id] is the raw unsigned value for record id (0-based ids).
-- A column with several elements per id (units.unit_extents: 4) is flat:
-- column[id * per_id + k]. Columns covering an id subrange (units.infestation from
-- 106) simply have no entries outside it. Signedness is applied by the consumer.
local bytes = require("game.data.bytes")
local kpf = require("game.data.kpf")

local u8, u16, u32 = bytes.u8, bytes.u16, bytes.u32
local byte = string.byte

local M = {}

M.KIND = "DAT "
M.SCHEMA = 1

-- Section fourcc -> table name.
M.NAMES = {
	FLNG = "flingy", IMAG = "images", MAPD = "mapdata", ORDR = "orders", PORT = "portdata",
	SFXD = "sfxdata", SPRT = "sprites", TECH = "techdata", UNIT = "units", UPGR = "upgrades",
	WEAP = "weapons",
}

local function unpack_column(data, offset, size, count, base)
	local out = {}
	if size == 1 then
		for i = 0, count - 1 do out[base + i] = byte(data, offset + i + 1) end
	elseif size == 2 then
		for i = 0, count - 1 do
			local lo, hi = byte(data, offset + 2 * i + 1, offset + 2 * i + 2)
			out[base + i] = lo + hi * 256
		end
	else
		for i = 0, count - 1 do out[base + i] = u32(data, offset + 4 * i) end
	end
	return out
end

-- Returns { units = { hit_points = {...}, ... }, ..., layout = { units = { records=, columns = {
-- name = { size=, per_id=, count=, first_id= } } } } }.
function M.parse(data, name, expect)
	local c = kpf.parse(data, name)
	expect = expect or {}
	c:check({ kind = M.KIND, schema_version = M.SCHEMA, profile_id = expect.profile_id,
		source_hash = expect.source_hash })
	local cols = c:section("COLS")
	local out = { layout = {} }
	local pos = 2
	for _ = 1, u16(cols, 0) do
		local fourcc = cols:sub(pos + 1, pos + 4)
		local records, ncols = u16(cols, pos + 4), u16(cols, pos + 6)
		pos = pos + 8
		local table_name = M.NAMES[fourcc] or error("dat: unknown table " .. fourcc, 0)
		local section = c:section(fourcc)
		local t, layout = {}, { records = records, columns = {} }
		for _ = 1, ncols do
			local size, per_id, count = u8(cols, pos), u8(cols, pos + 1), u16(cols, pos + 2)
			local first_id, offset, len = u16(cols, pos + 4), u32(cols, pos + 6), u8(cols, pos + 10)
			local column = cols:sub(pos + 12, pos + 11 + len)
			pos = pos + 11 + len
			if offset + size * count > #section then error("dat: column " .. column .. " out of bounds", 0) end
			t[column] = unpack_column(section, offset, size, count, first_id * per_id)
			layout.columns[column] = { size = size, per_id = per_id, count = count, first_id = first_id }
		end
		out[table_name] = t
		out.layout[table_name] = layout
	end
	return out
end

return M
