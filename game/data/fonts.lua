-- fonts.kpd reader (kind FONT). Layout: docs/formats/kpd.md; writer: converter/kpf/kpd.py.
-- Each font keeps its decoded FNT bytes; glyph pixels are drawn from the glyph atlas
-- (generated/fonts), where texel k + 1 means colour-row entry k and 0 means untouched.
local bytes = require("game.data.bytes")
local kpf = require("game.data.kpf")

local u8, u32 = bytes.u8, bytes.u32

local M = {}

M.KIND = "FONT"
M.SCHEMA = 1
M.FONTS = { font8 = "F8  ", font10 = "F10 ", font14 = "F14 ", font16 = "F16 ", font16x = "F16X" }

local Font = {}
Font.__index = Font

-- Metrics of byte `code`: width, height, x offset, y offset; nil outside the font or for
-- an empty slot (space 32 has no glyph; layout advances it by max_width / 2).
function Font:glyph(code)
	if code < self.low or code > self.high then return nil end
	local off = u32(self.data, 8 + 4 * (code - self.low))
	if off == 0 then return nil end
	local d = self.data
	return u8(d, off), u8(d, off + 1), u8(d, off + 2), u8(d, off + 3)
end

local function parse_font(data, name)
	if data:sub(1, 4) ~= "FONT" then error("fonts: " .. name .. " has no FONT header", 0) end
	return setmetatable({ name = name, data = data, low = u8(data, 4), high = u8(data, 5),
		max_width = u8(data, 6), max_height = u8(data, 7) }, Font)
end

-- Returns { font8 = Font, ..., tfontgam = <192 bytes: 24 rows of 8>, tfont = <40 bytes> }.
function M.parse(data, name, expect)
	local c = kpf.parse(data, name)
	expect = expect or {}
	c:check({ kind = M.KIND, schema_version = M.SCHEMA, profile_id = expect.profile_id,
		source_hash = expect.source_hash })
	local out = { tfontgam = c:section("TFGM"), tfont = c:section("TFNT") }
	for font, fourcc in pairs(M.FONTS) do out[font] = parse_font(c:section(fourcc), font) end
	return out
end

return M
