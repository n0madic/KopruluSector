-- Pure-Lua implementation of the LuaBitOp API for test runs on PUC Lua 5.1,
-- which has no `bit` module. Defold ships BitOp on every platform, so this
-- shim is never part of the game. Results follow BitOp: signed 32-bit values.
-- tests/lua/test_bitshim.lua cross-checks it against LuaJIT's native module.
local floor = math.floor

local TWO32 = 4294967296
local TWO31 = 2147483648

local function u(x)
	return floor(x) % TWO32
end

local function s(x)
	x = x % TWO32
	if x >= TWO31 then return x - TWO32 end
	return x
end

local function bitwise(a, b, op)
	a, b = u(a), u(b)
	local r, p = 0, 1
	for _ = 1, 32 do
		local ab, bb = a % 2, b % 2
		if op(ab, bb) then r = r + p end
		a, b, p = (a - ab) / 2, (b - bb) / 2, p * 2
	end
	return r
end

local function f_and(x, y) return x == 1 and y == 1 end
local function f_or(x, y) return x == 1 or y == 1 end
local function f_xor(x, y) return x ~= y end

local function fold(op, a, b, ...)
	local r = bitwise(a, b, op)
	for i = 1, select("#", ...) do
		r = bitwise(r, (select(i, ...)), op)
	end
	return s(r)
end

local M = {}

function M.tobit(x) return s(u(x)) end
function M.bnot(x) return s(TWO32 - 1 - u(x)) end
function M.band(a, b, ...) return fold(f_and, a, b, ...) end
function M.bor(a, b, ...) return fold(f_or, a, b, ...) end
function M.bxor(a, b, ...) return fold(f_xor, a, b, ...) end

function M.lshift(x, n)
	n = u(n) % 32
	return s((u(x) % 2 ^ (32 - n)) * 2 ^ n)
end

function M.rshift(x, n)
	n = u(n) % 32
	return s(floor(u(x) / 2 ^ n))
end

function M.arshift(x, n)
	n = u(n) % 32
	return s(floor(s(u(x)) / 2 ^ n))
end

function M.rol(x, n)
	n = u(n) % 32
	local v = u(x)
	return s((v % 2 ^ (32 - n)) * 2 ^ n + floor(v / 2 ^ (32 - n)))
end

function M.ror(x, n)
	n = u(n) % 32
	return M.rol(x, 32 - n)
end

function M.bswap(x)
	local v = u(x)
	local b0, b1, b2, b3 = v % 256, floor(v / 256) % 256, floor(v / 65536) % 256, floor(v / 16777216)
	return s(b0 * 16777216 + b1 * 65536 + b2 * 256 + b3)
end

function M.tohex(x, n)
	n = n or 8
	local upper = n < 0
	if upper then n = -n end
	local str = string.format(upper and "%08X" or "%08x", u(x))
	return str:sub(-n)
end

return M
