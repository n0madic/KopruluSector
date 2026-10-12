-- The `bit` module in use (native BitOp, or the shim on PUC Lua) must follow
-- BitOp semantics. On LuaJIT the shim is also cross-checked against native BitOp.
local S = {}

local function load_shim(T)
	local text = T.read("tests/lua/bitshim.lua")
	return assert((loadstring or load)(text))()
end

local function check_known(T, b, label)
	T.eq(b.band(0xFFFFFFFF, 0x0F0F0F0F), 0x0F0F0F0F, label)
	T.eq(b.bor(0x80000000, 1), -2147483647, label)
	T.eq(b.bxor(-1, 0x12345678), -305419897, label)
	T.eq(b.bnot(0), -1, label)
	T.eq(b.lshift(1, 31), -2147483648, label)
	T.eq(b.lshift(0xFFFFFFFF, 31), -2147483648, label)
	T.eq(b.rshift(-1, 28), 15, label)
	T.eq(b.arshift(-256, 4), -16, label)
	T.eq(b.rol(0x80000001, 1), 3, label)
	T.eq(b.ror(3, 1), -2147483647, label)
	T.eq(b.tobit(0xFFFFFFFF), -1, label)
	T.eq(b.lshift(5, 32), 5, label)
	T.eq(b.bswap(0x12345678), 0x78563412, label)
	T.eq(b.tohex(255), "000000ff", label)
end

function S.active_module_known_values(T)
	check_known(T, bit, "active bit")
end

function S.shim_known_values(T)
	check_known(T, load_shim(T), "shim")
end

function S.shim_matches_native(T)
	if not jit then return end -- only LuaJIT has a native module to compare with
	local s = load_shim(T)
	local x = 0x9E3779B9
	for i = 1, 2000 do
		x = (x * 1103515245 + 12345) % 4294967296
		local y = (x * 69069 + i) % 4294967296
		local n = i % 37
		T.eq(s.band(x, y), bit.band(x, y))
		T.eq(s.bor(x, y), bit.bor(x, y))
		T.eq(s.bxor(x, y), bit.bxor(x, y))
		T.eq(s.bnot(x), bit.bnot(x))
		T.eq(s.lshift(x, n), bit.lshift(x, n))
		T.eq(s.rshift(x, n), bit.rshift(x, n))
		T.eq(s.arshift(x, n), bit.arshift(x, n))
		T.eq(s.rol(x, n), bit.rol(x, n))
		T.eq(s.ror(x, n), bit.ror(x, n))
		T.eq(s.tobit(x), bit.tobit(x))
	end
end

return S
