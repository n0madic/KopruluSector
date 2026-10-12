-- Heading vectors of the 256 flingy directions (the executable's table 0x5026b0, x then y
-- DWORD per direction, fixed point 1/256): x = round(256 sin a), y = round(-256 cos a) with
-- a = 2 pi d / 256. The formula reproduces all 256 entries of the original table; the
-- closest product is 0.0007 from a rounding tie, far above any sin/cos error.
local M = { x = {}, y = {} }

M.COUNT = 256
M.ONE = 256

for d = 0, M.COUNT - 1 do
	local a = 2 * math.pi * d / M.COUNT
	M.x[d] = math.floor(math.sin(a) * M.ONE + 0.5)
	M.y[d] = math.floor(-math.cos(a) * M.ONE + 0.5)
end

-- The slope bounds of the 64 directions of a quadrant (the executable's table 0x502eb0,
-- fixed point 1/256): SlopeToDirectionOffset 0x40b940 searches it. Index 63 holds 0xffffffff;
-- the values do not follow a closed formula, so they are kept as data.
M.SLOPES = { [0] = 7, 13, 19, 26, 32, 38, 45, 51, 58, 65, 71, 78, 85, 92, 99, 107, 114, 122, 129, 137, 146, 154,
	163, 172, 181, 190, 200, 211, 221, 233, 244, 256, 269, 283, 297, 312, 329, 346, 364, 384, 405, 428, 452, 479,
	509, 542, 578, 619, 664, 716, 775, 844, 926, 1023, 1141, 1287, 1476, 1726, 2076, 2600, 3471, 5211, 10429,
	0xffffffff }

return M
