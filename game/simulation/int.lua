-- Exact x86 integer arithmetic on Lua doubles (Lua 5.1, LuaJIT with or without JIT).
-- Every function takes integral numbers and returns canonical values: unsigned
-- results in [0, 2^N), signed results in [-2^(N-1), 2^(N-1)).
-- Products are split into 16-bit limbs so intermediates stay below 2^53.
local bit = bit or require("bit")
local band, bor, bxor = bit.band, bit.bor, bit.bxor
local floor = math.floor

local TWO8, TWO16, TWO32 = 256, 65536, 4294967296

local M = {}

M.U32_MAX = TWO32 - 1
M.S32_MIN = -2147483648
M.S32_MAX = 2147483647

function M.u8(x) return x % TWO8 end
function M.u16(x) return x % TWO16 end
function M.u32(x) return x % TWO32 end

function M.s8(x)
	x = x % TWO8
	if x >= 128 then return x - TWO8 end
	return x
end

function M.s16(x)
	x = x % TWO16
	if x >= 32768 then return x - TWO16 end
	return x
end

function M.s32(x)
	x = x % TWO32
	if x >= 2147483648 then return x - TWO32 end
	return x
end

function M.add(a, b) return (a + b) % TWO32 end
function M.sub(a, b) return (a - b) % TWO32 end
function M.neg(a) return (-a) % TWO32 end

-- Low 32 bits of a * b (x86 `imul r32` / `mul` low half; same bits for signed and unsigned).
function M.mul(a, b)
	a, b = a % TWO32, b % TWO32
	local alo, ahi = a % TWO16, floor(a / TWO16)
	local blo, bhi = b % TWO16, floor(b / TWO16)
	local lo = alo * blo
	local mid = (ahi * blo + alo * bhi) % TWO16
	return (lo + mid * TWO16) % TWO32
end

-- Unsigned high 32 bits of a * b (x86 `mul` EDX).
function M.mulhi(a, b)
	a, b = a % TWO32, b % TWO32
	local alo, ahi = a % TWO16, floor(a / TWO16)
	local blo, bhi = b % TWO16, floor(b / TWO16)
	local lo = alo * blo
	local m1, m2 = ahi * blo, alo * bhi
	local mid = floor(lo / TWO16) + m1 % TWO16 + m2 % TWO16
	return (ahi * bhi + floor(m1 / TWO16) + floor(m2 / TWO16) + floor(mid / TWO16)) % TWO32
end

-- C/x86 signed division and remainder: quotient truncates toward zero, the
-- remainder takes the sign of the dividend. Division by zero raises an error
-- (the original faults); INT_MIN / -1 also faults on x86 and raises here.
function M.idiv(a, b)
	if b == 0 then error("integer division by zero", 2) end
	if a == M.S32_MIN and b == -1 then error("integer overflow in division", 2) end
	local q = a / b
	if q >= 0 then return floor(q) end
	return -floor(-q)
end

function M.irem(a, b)
	return a - M.idiv(a, b) * b
end

-- Unsigned division (x86 `div` with zero EDX).
function M.udiv(a, b)
	if b == 0 then error("integer division by zero", 2) end
	return floor((a % TWO32) / (b % TWO32))
end

function M.urem(a, b)
	if b == 0 then error("integer division by zero", 2) end
	return (a % TWO32) % (b % TWO32)
end

-- Shifts take the count modulo 32, as x86 does for 32-bit operands.
function M.shl(x, n)
	n = n % 32
	return (x % (2 ^ (32 - n))) * 2 ^ n
end

function M.shr(x, n)
	return floor((x % TWO32) / 2 ^ (n % 32))
end

function M.sar(x, n)
	return floor(M.s32(x) / 2 ^ (n % 32))
end

-- Bitwise operations normalised to unsigned 32-bit; like the bit library they take any
-- number of operands (the two-operand call stays the fast path).
function M.band(a, b, c, ...)
	if c == nil then return band(a, b) % TWO32 end
	return band(a, b, c, ...) % TWO32
end

function M.bor(a, b, c, ...)
	if c == nil then return bor(a, b) % TWO32 end
	return bor(a, b, c, ...) % TWO32
end

function M.bxor(a, b, c, ...)
	if c == nil then return bxor(a, b) % TWO32 end
	return bxor(a, b, c, ...) % TWO32
end
function M.bnot(a) return TWO32 - 1 - a % TWO32 end

return M
