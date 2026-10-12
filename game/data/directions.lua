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

return M
