-- The three independent random generators of StarCraft 1.17.0.1 (plan D14).
-- None of them may be replaced by math.random, and none consumes another's state.
--   game     : simulation LCG, 0x478200 Rand15 / 0x478270 RandRange (docs/research/native-prng.md)
--   crt      : MSVC CRT rand/srand, 0x4d14d4 / 0x4d14f5 (docs/research/atlas/c01.md)
--   portrait : console portrait RNG 0x4a6e00 (docs/research/native-audiovisual.md)
local int = require("game.simulation.int")

local mul, shr, bxor = int.mul, int.shr, int.bxor
local floor = math.floor

local TWO32 = 4294967296

local M = {}

M.SOURCE_COUNT = 256 -- dword counters at 0x673940 (1024 bytes)

---------------------------------------------------------------------------
-- Game RNG
---------------------------------------------------------------------------
local Game = {}
Game.__index = Game

local GAME_MULTIPLIER = 0x015a4e35

function M.game(initial_seed)
	local counters = {}
	for i = 0, M.SOURCE_COUNT - 1 do counters[i] = 0 end
	return setmetatable({
		initial_seed = initial_seed or 0, -- 0x673934
		state = 0,                        -- 0x67393c
		enabled = 0,                      -- 0x673930 (dword; nonzero enables)
		total = 0,                        -- 0x673938
		counters = counters,              -- 0x673940[256]
	}, Game)
end

-- 0x4781c0: stores the new flag and returns the old one.
function Game:set_enabled(flag)
	local old = self.enabled
	self.enabled = flag % TWO32
	return old
end

-- 0x478240: live state = seed; total and per-source counters cleared.
function Game:seed(seed)
	self.state = seed % TWO32
	self.total = 0
	local counters = self.counters
	for i = 0, M.SOURCE_COUNT - 1 do counters[i] = 0 end
end

-- 0x478200: disabled returns 0 without any side effect.
function Game:raw(source)
	if self.enabled == 0 then return 0 end
	local state = (mul(self.state, GAME_MULTIPLIER) + 1) % TWO32
	self.state = state
	local counters = self.counters
	counters[source] = (counters[source] + 1) % TWO32
	self.total = (self.total + 1) % TWO32
	return floor(state / 65536) % 32768
end

-- 0x478270: disabled returns `from`; otherwise from + ((raw * span) mod 2^32 >> 15),
-- span = (to - from + 1) mod 2^32. All arithmetic is uint32.
function Game:range(source, from, to)
	if self.enabled == 0 then return from % TWO32 end
	local r = self:raw(source)
	local span = (to - from + 1) % TWO32
	return (from + shr(mul(r, span), 15)) % TWO32
end

function Game:snapshot()
	local counters = {}
	for i = 0, M.SOURCE_COUNT - 1 do counters[i + 1] = self.counters[i] end
	return { self.initial_seed, self.state, self.enabled, self.total, counters }
end

function Game:restore(s)
	self.initial_seed, self.state, self.enabled, self.total = s[1], s[2], s[3], s[4]
	for i = 0, M.SOURCE_COUNT - 1 do self.counters[i] = s[5][i + 1] end
end

---------------------------------------------------------------------------
-- CRT rand (per-thread holdrand; default 1)
---------------------------------------------------------------------------
local Crt = {}
Crt.__index = Crt

local CRT_MULTIPLIER, CRT_INCREMENT = 214013, 2531011

function M.crt(seed)
	return setmetatable({ holdrand = (seed or 1) % TWO32 }, Crt)
end

function Crt:srand(seed)
	self.holdrand = seed % TWO32
end

function Crt:rand()
	local h = (mul(self.holdrand, CRT_MULTIPLIER) + CRT_INCREMENT) % TWO32
	self.holdrand = h
	return floor(h / 65536) % 32768
end

function Crt:snapshot() return { self.holdrand } end
function Crt:restore(s) self.holdrand = s[1] end

---------------------------------------------------------------------------
-- Console portrait RNG
---------------------------------------------------------------------------
local Portrait = {}
Portrait.__index = Portrait

-- Words stored in the mapped PE at 0x50d7cc/0x50d7d0/0x50d7d4. They are file
-- state, not a claimed post-startup snapshot.
M.PORTRAIT_FILE_STATE = { 0x12345678, 0xe9e9e9e9, 0x10934134 }
local PORTRAIT_STEP = 0x71823402

function M.portrait(x, y, z)
	local s = M.PORTRAIT_FILE_STATE
	return setmetatable({ x = x or s[1], y = y or s[2], z = z or s[3] }, Portrait)
end

-- 0x4a6e00: ECX = low, EDX = high, stack = mask size (a power of two).
-- Rejected candidates still advance the state.
function Portrait:draw(low, high, mask_size)
	local x, y, z = self.x, self.y, self.z
	local limit = high - low + 1
	local candidate
	repeat
		x = (x + y) % TWO32
		y = (y + PORTRAIT_STEP) % TWO32
		z = (z + x) % TWO32
		x = floor(x / 2)
		y = bxor(y, z)
		candidate = bxor(y, x) % mask_size
	until candidate < limit
	self.x, self.y, self.z = x, y, z
	return low + candidate
end

function Portrait:snapshot() return { self.x, self.y, self.z } end
function Portrait:restore(s) self.x, self.y, self.z = s[1], s[2], s[3] end

return M
