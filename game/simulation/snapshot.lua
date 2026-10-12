-- Deterministic structural encoding of simulation state and its fingerprint.
--
-- encode() accepts nested tables of integers, strings and booleans. Map keys are
-- sorted (numbers before strings), so the byte stream does not depend on Lua
-- `pairs` order and is identical on LuaJIT and PUC Lua. Non-integral numbers are
-- rejected: simulation state is integral by contract (plan §0.2).
-- This is the phase-0 prototype of the save serializer (plan D17).
local int = require("game.simulation.int")

local floor = math.floor
local char, byte, concat = string.char, string.byte, table.concat
local mul, bxor = int.mul, int.bxor

local M = {}

local TAG_NIL, TAG_FALSE, TAG_TRUE, TAG_INT, TAG_STR, TAG_ARRAY, TAG_MAP = 0, 1, 2, 3, 4, 5, 6
local MAX_INT = 9007199254740992 -- 2^53

local function u32le(v)
	return char(v % 256, floor(v / 256) % 256, floor(v / 65536) % 256, floor(v / 16777216) % 256)
end

local function encode_int(out, v)
	if v ~= floor(v) or v > MAX_INT or v < -MAX_INT then
		error("snapshot: non-integral or out-of-range number " .. tostring(v), 0)
	end
	local sign = 0
	if v < 0 then sign, v = 1, -v end
	local lo, hi = v % 4294967296, floor(v / 4294967296)
	out[#out + 1] = char(TAG_INT, sign) .. u32le(lo) .. u32le(hi)
end

local function is_array(t)
	local n = #t
	for k in pairs(t) do
		if type(k) ~= "number" or k < 1 or k > n or k ~= floor(k) then return false end
	end
	return true, n
end

local function key_less(a, b)
	local ta, tb = type(a), type(b)
	if ta ~= tb then return ta == "number" end
	return a < b
end

local encode_value

local function encode_table(out, t, depth)
	if depth > 64 then error("snapshot: nesting too deep (cycle?)", 0) end
	local array, n = is_array(t)
	if array then
		out[#out + 1] = char(TAG_ARRAY) .. u32le(n)
		for i = 1, n do encode_value(out, t[i], depth + 1) end
		return
	end
	local keys = {}
	for k in pairs(t) do
		local tk = type(k)
		if tk ~= "number" and tk ~= "string" then error("snapshot: unsupported key type " .. tk, 0) end
		keys[#keys + 1] = k
	end
	table.sort(keys, key_less)
	out[#out + 1] = char(TAG_MAP) .. u32le(#keys)
	for i = 1, #keys do
		encode_value(out, keys[i], depth + 1)
		encode_value(out, t[keys[i]], depth + 1)
	end
end

encode_value = function(out, v, depth)
	local tv = type(v)
	if tv == "number" then
		encode_int(out, v)
	elseif tv == "string" then
		out[#out + 1] = char(TAG_STR) .. u32le(#v) .. v
	elseif tv == "boolean" then
		out[#out + 1] = char(v and TAG_TRUE or TAG_FALSE)
	elseif tv == "nil" then
		out[#out + 1] = char(TAG_NIL)
	elseif tv == "table" then
		encode_table(out, v, depth)
	else
		error("snapshot: unsupported value type " .. tv, 0)
	end
end

function M.encode(value)
	local out = {}
	encode_value(out, value, 0)
	return concat(out)
end

local function read_u32(s, pos)
	local b1, b2, b3, b4 = byte(s, pos, pos + 3)
	if not b4 then error("snapshot: truncated data", 0) end
	return b1 + b2 * 256 + b3 * 65536 + b4 * 16777216, pos + 4
end

local function decode_value(s, pos)
	local tag = byte(s, pos)
	if not tag then error("snapshot: truncated data", 0) end
	pos = pos + 1
	if tag == TAG_NIL then return nil, pos end
	if tag == TAG_FALSE then return false, pos end
	if tag == TAG_TRUE then return true, pos end
	if tag == TAG_INT then
		local sign = byte(s, pos)
		local lo, hi
		lo, pos = read_u32(s, pos + 1)
		hi, pos = read_u32(s, pos)
		local v = hi * 4294967296 + lo
		if sign == 1 then v = -v end
		return v, pos
	end
	if tag == TAG_STR then
		local n
		n, pos = read_u32(s, pos)
		if pos + n - 1 > #s then error("snapshot: truncated data", 0) end
		return s:sub(pos, pos + n - 1), pos + n
	end
	if tag == TAG_ARRAY then
		local n, t = nil, {}
		n, pos = read_u32(s, pos)
		for i = 1, n do t[i], pos = decode_value(s, pos) end
		return t, pos
	end
	if tag == TAG_MAP then
		local n, t = nil, {}
		n, pos = read_u32(s, pos)
		for _ = 1, n do
			local k, v
			k, pos = decode_value(s, pos)
			v, pos = decode_value(s, pos)
			t[k] = v
		end
		return t, pos
	end
	error("snapshot: unknown tag " .. tag, 0)
end

function M.decode(s)
	local v, pos = decode_value(s, 1)
	if pos ~= #s + 1 then error("snapshot: trailing data", 0) end
	return v
end

-- 32-bit FNV-1a over a byte string.
local FNV_OFFSET, FNV_PRIME = 2166136261, 16777619

function M.fnv1a(s)
	local h = FNV_OFFSET
	for i = 1, #s do
		h = mul(bxor(h, byte(s, i)), FNV_PRIME)
	end
	return h
end

function M.fingerprint(value)
	return M.fnv1a(M.encode(value))
end

return M
