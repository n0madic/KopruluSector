-- CRC-32 (IEEE 802.3, reflected, as zlib.crc32) over a Lua string.
-- Uses the BitOp `bit` module that Defold provides on every platform.
local bit = bit or require("bit")
local bxor, band, rshift = bit.bxor, bit.band, bit.rshift
local byte = string.byte

local TABLE = {}
for i = 0, 255 do
	local c = i
	for _ = 1, 8 do
		if band(c, 1) ~= 0 then
			c = bxor(rshift(c, 1), 0xEDB88320)
		else
			c = rshift(c, 1)
		end
	end
	TABLE[i] = c
end

local M = {}

function M.of(s, first, last)
	first = first or 1
	last = last or #s
	local c = 0xFFFFFFFF
	for i = first, last do
		c = bxor(rshift(c, 8), TABLE[band(bxor(c, byte(s, i)), 0xFF)])
	end
	return bxor(c, 0xFFFFFFFF) % 4294967296
end

return M
