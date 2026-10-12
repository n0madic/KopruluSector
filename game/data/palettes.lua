-- palettes.kpd reader (kind PALS). Layout: docs/formats/kpd.md; writer: converter/kpf/kpd.py.
-- Remap and lookup tables stay strings of palette indices; rows are 256 bytes.
local bytes = require("game.data.bytes")
local kpf = require("game.data.kpf")

local u16 = bytes.u16

local M = {}

M.KIND = "PALS"
M.SCHEMA = 1
-- Remap records 1..7 of 0x508828 (images.dat remapping index = position).
M.REMAP_TABLES = { "ofire", "gfire", "bfire", "bexpl", "trans50", "red", "green" }
M.DARK_SIZE = 0x2000
M.SHIFT_SIZE = 256

local GAME_TABLES = { tunit = "TUNI", tminimap = "TMIN", tselect = "TSEL", thpbar = "THPB" }

local function parse_tileset(s)
	local t, pos = { remap = {} }, 0
	for i, name in ipairs(M.REMAP_TABLES) do
		local rows = u16(s, pos)
		local table_ = { rows = rows, data = s:sub(pos + 3, pos + 2 + rows * 256) }
		t.remap[i], t.remap[name] = table_, table_
		pos = pos + 2 + rows * 256
	end
	t.dark = s:sub(pos + 1, pos + M.DARK_SIZE)
	t.shift = s:sub(pos + M.DARK_SIZE + 1, pos + M.DARK_SIZE + M.SHIFT_SIZE)
	if #t.shift ~= M.SHIFT_SIZE then error("palettes: tileset section truncated", 0) end
	return t
end

-- Returns { tilesets = { [0..7] = { remap = { ofire = { rows=, data= }, [1] = ..., ... },
-- dark=, shift= } }, tunit=, tminimap=, tselect=, thpbar= }.
function M.parse(data, name, expect)
	local c = kpf.parse(data, name)
	expect = expect or {}
	c:check({ kind = M.KIND, schema_version = M.SCHEMA, profile_id = expect.profile_id,
		source_hash = expect.source_hash })
	local out = { tilesets = {} }
	for i = 0, 7 do out.tilesets[i] = parse_tileset(c:section("PAL" .. i)) end
	for field, fourcc in pairs(GAME_TABLES) do out[field] = c:section(fourcc) end
	return out
end

-- Index `color` of player colour row `row` (0..11) from tunit (8 entries per row).
function M.player_color(p, row, k)
	return p.tunit:byte(row * 8 + k + 1)
end

return M
