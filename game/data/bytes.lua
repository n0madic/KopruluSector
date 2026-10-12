-- Little-endian integer reads from Lua strings. Offsets are 0-based byte offsets,
-- matching the format specifications in docs/formats/.
local byte = string.byte

local M = {}

function M.u8(s, off)
	return byte(s, off + 1)
end

function M.u16(s, off)
	local b1, b2 = byte(s, off + 1, off + 2)
	return b1 + b2 * 256
end

function M.u32(s, off)
	local b1, b2, b3, b4 = byte(s, off + 1, off + 4)
	return b1 + b2 * 256 + b3 * 65536 + b4 * 16777216
end

function M.s8(s, off)
	local v = byte(s, off + 1)
	if v >= 128 then return v - 256 end
	return v
end

function M.s16(s, off)
	local v = M.u16(s, off)
	if v >= 32768 then return v - 65536 end
	return v
end

function M.s32(s, off)
	local v = M.u32(s, off)
	if v >= 2147483648 then return v - 4294967296 end
	return v
end

-- Fixed-size ASCII field without trailing NUL padding.
function M.cstr(s, off, size)
	local raw = s:sub(off + 1, off + size)
	local nul = raw:find("\0", 1, true)
	if nul then raw = raw:sub(1, nul - 1) end
	return raw
end

return M
