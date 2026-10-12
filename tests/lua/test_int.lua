local int = require("game.simulation.int")

local S = {}

function S.wrap(T)
	T.eq(int.u8(256), 0); T.eq(int.u8(-1), 255)
	T.eq(int.u16(65536 + 5), 5); T.eq(int.u16(-1), 65535)
	T.eq(int.u32(-1), 4294967295); T.eq(int.u32(4294967296), 0)
	T.eq(int.s8(128), -128); T.eq(int.s8(255), -1); T.eq(int.s8(127), 127)
	T.eq(int.s16(32768), -32768); T.eq(int.s16(65535), -1)
	T.eq(int.s32(2147483648), -2147483648); T.eq(int.s32(4294967295), -1); T.eq(int.s32(-1), -1)
	T.eq(int.add(4294967295, 1), 0)
	T.eq(int.sub(0, 1), 4294967295)
	T.eq(int.neg(1), 4294967295)
end

function S.mul_boundaries(T)
	T.eq(int.mul(0xFFFFFFFF, 0xFFFFFFFF), 1)
	T.eq(int.mulhi(0xFFFFFFFF, 0xFFFFFFFF), 0xFFFFFFFE)
	T.eq(int.mul(0x015a4e35, 0xFFFFFFFF) + 1, 4272271820) -- docs/research/native-prng.md
	T.eq(int.mul(0x80000000, 2), 0)
	T.eq(int.mulhi(0x80000000, 2), 1)
	T.eq(int.mul(-1, 3), 4294967293) -- signed inputs give the same low bits
	T.eq(int.mul(65536, 65536), 0)
	T.eq(int.mulhi(65536, 65536), 1)
	T.eq(int.mul(0x12345678, 0x9ABCDEF0), 0x242D2080)
	T.eq(int.mulhi(0x12345678, 0x9ABCDEF0), 0x0B00EA4E)
end

function S.mul_matches_uint64(T)
	if not jit then return end
	local ok, ffi = pcall(require, "ffi")
	if not ok then return end
	local u64 = ffi.typeof("uint64_t")
	local TWO32 = u64(4294967296)
	local x = 1
	for i = 1, 5000 do
		x = (x * 1103515245 + 12345 + i) % 4294967296
		local y = (x * 2654435761) % 4294967296
		local p = u64(x) * u64(y)
		T.eq(int.mul(x, y), tonumber(p % TWO32), "mul")
		T.eq(int.mulhi(x, y), tonumber(p / TWO32), "mulhi")
	end
end

function S.division_truncates(T)
	T.eq(int.idiv(7, 2), 3); T.eq(int.irem(7, 2), 1)
	T.eq(int.idiv(-7, 2), -3); T.eq(int.irem(-7, 2), -1)
	T.eq(int.idiv(7, -2), -3); T.eq(int.irem(7, -2), 1)
	T.eq(int.idiv(-7, -2), 3); T.eq(int.irem(-7, -2), -1)
	T.eq(int.idiv(2147483647, 1), 2147483647)
	T.eq(int.idiv(-2147483648, 2), -1073741824)
	T.eq(int.idiv(2147483646, 2147483647), 0)
	T.eq(int.idiv(-2147483647, 2147483646), -1)
	T.raises(function() int.idiv(1, 0) end, "division by zero")
	T.raises(function() int.idiv(-2147483648, -1) end, "overflow")
	T.eq(int.udiv(4294967295, 2), 2147483647)
	T.eq(int.urem(4294967295, 10), 5)
	T.eq(int.udiv(4294967295, 4294967294), 1)
end

function S.shifts(T)
	T.eq(int.shl(1, 31), 2147483648)
	T.eq(int.shl(3, 31), 2147483648)
	T.eq(int.shl(0xFFFFFFFF, 4), 0xFFFFFFF0)
	T.eq(int.shl(5, 32), 5) -- count mod 32, as on x86
	T.eq(int.shr(0x80000000, 31), 1)
	T.eq(int.shr(0xFFFFFFFF, 16), 0xFFFF)
	T.eq(int.shr(-1, 28), 15)
	T.eq(int.sar(0x80000000, 4), -134217728)
	T.eq(int.sar(-1, 31), -1)
	T.eq(int.sar(-7, 1), -4) -- floor, unlike idiv
	T.eq(int.sar(7, 1), 3)
end

function S.bitwise(T)
	T.eq(int.band(0xFFFFFFFF, 0x80000001), 0x80000001)
	T.eq(int.bor(0x80000000, 1), 0x80000001)
	T.eq(int.bxor(0xFFFFFFFF, 0x0F0F0F0F), 0xF0F0F0F0)
	T.eq(int.bnot(0), 0xFFFFFFFF)
	T.eq(int.bnot(0xFFFFFFFF), 0)
end

-- Fixed-point positions are 1/256 px (plan D13); conversions use x86 shifts.
function S.fixed_point_8(T)
	local px = -3
	local fixed = px * 256
	T.eq(int.sar(int.u32(fixed), 8), -3)
	T.eq(int.sar(int.u32(-1), 8), -1)    -- -1/256 px rounds toward -inf
	T.eq(int.idiv(-1, 256), 0)           -- but C division rounds toward zero
	T.eq(int.sar(int.u32(-257), 8), -2)
	T.eq(int.idiv(-257, 256), -1)
end

return S
