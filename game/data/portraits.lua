-- portraits.kpd reader (kind PORT). Layout: docs/formats/kpd.md; writer: converter/kpf/kpd.py.
-- Frames live in the portrait atlases (generated/atlases/portraits_<group>.atlas) as indices
-- into the movie's own per-frame palette stored here.
local bytes = require("game.data.bytes")
local kpf = require("game.data.kpf")

local u8, u16, u32 = bytes.u8, bytes.u16, bytes.u32

local M = {}

M.KIND = "PORT"
M.SCHEMA = 2
local NONE16 = 0xffff

local function kind(first, mask)
	return { first = first ~= NONE16 and first + 1 or nil, mask = mask }
end

-- Returns { rows = { [portrait id 0-based] = { idle = { first=, mask= }, talking = {...} } },
-- smks = { { path=, width=, height=, frames=, header_frames=, ring=, ms_per_frame=, palettes = {..} } },
-- palettes = { <768 bytes>, ... } }. `mask` bit v set means variant digit v exists.
function M.parse(data, name, expect)
	local c = kpf.parse(data, name)
	expect = expect or {}
	c:check({ kind = M.KIND, schema_version = M.SCHEMA, profile_id = expect.profile_id,
		source_hash = expect.source_hash })
	local r, rows = c:section("ROWS"), {}
	for i = 0, u16(r, 0) - 1 do
		local a = 2 + 8 * i
		rows[i] = { idle = kind(u16(r, a), u16(r, a + 2)), talking = kind(u16(r, a + 4), u16(r, a + 6)) }
	end
	local s, smks, pos = c:section("SMKS"), {}, 2
	for i = 1, u16(s, 0) do
		local len = u8(s, pos)
		local m = { path = s:sub(pos + 2, pos + 1 + len) }
		pos = pos + 1 + len
		m.width, m.height, m.frames, m.header_frames = u16(s, pos), u16(s, pos + 2), u16(s, pos + 4), u16(s, pos + 6)
		m.ring = u8(s, pos + 8) == 1
		m.ms_per_frame = u32(s, pos + 9) / 1000
		pos = pos + 13
		m.palettes = {}
		for f = 1, m.frames do
			m.palettes[f] = u16(s, pos) + 1
			pos = pos + 2
		end
		smks[i] = m
	end
	local p, palettes = c:section("PALS"), {}
	for i = 1, u16(p, 0) do palettes[i] = p:sub(3 + (i - 1) * 768, 2 + i * 768) end
	return { rows = rows, smks = smks, palettes = palettes }
end

-- SMK index (1-based) of variant digit v of a row/kind, or nil when that file does not exist.
function M.variant(p, id, which, v)
	local k = p.rows[id][which]
	if not k.first or k.mask % (2 ^ (v + 1)) < 2 ^ v then return nil end
	local index = k.first
	for d = 0, v - 1 do
		if k.mask % (2 ^ (d + 1)) >= 2 ^ d then index = index + 1 end
	end
	return index
end

return M
