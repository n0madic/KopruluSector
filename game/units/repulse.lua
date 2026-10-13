-- The air repulsion of flying units (atlas P10): flyers count themselves in a grid of
-- 48-pixel cells (AddAirDensity 0x481760, RemoveAirDensity 0x481830); a flyer in a cell with
-- other flyers drifts away along its repulse angle (ApplyAirRepulsion 0x481880,
-- UpdateRepulseAngle 0x4814e0) and settles where it drifted (UpdateAirRepulsion 0x4815d0).
-- The grid is self.density, cell -> count (0x67cf60, 0xab cells per row). Methods of the game.
local directions = require("game.data.directions")
local int = require("game.simulation.int")

local band, bor = int.band, int.bor
local bit_band = int.bit_band
local s8, s16 = int.s8, int.s16

local M = {}

M.CELL = 48
M.COLUMNS = 0xab
M.RANDOM_DENSITY = 37 -- RandomizeRepulse source of 0x481760
M.CAN_MOVE = 0x20000
M.STATUS_LOCKS = 0x3000
M.DAT_BUILDING = 1
M.INTERCEPTOR = 0x49
M.EDGE = 0x20 -- the margin inside the map edges that turns the repulse angle back
M.SETTLE = 0x18 -- the distance below which the move target follows the drift
M.TIMER = 7 -- the low bits of +0x14c: steps until the repulse angle is recomputed

-- The repulse angle jitter 0x67cf68 (filled by InitRepulseAngleJitter 0x4816f0): the offset
-- from the target direction for each angle difference, 0x20..0x3f on one side and
-- 0xc0..0xdf on the other.
local JITTER = {}
for k = 0, 255 do
	JITTER[k] = k < 0x80 and k % 0x20 + 0x20 or k % 0x20 + 0xc0
end

-- The drift bias of each repulse profile (the high nibble of +0x14c), 0x508e68.
local BIAS = { [0] = { -5, -5 }, { -5, 5 }, { 5, -5 }, { 5, 5 }, { 5, -5 }, { 5, 5 }, { -5, -5 }, { -5, 5 },
	{ -5, 5 }, { 5, -5 }, { -5, -5 }, { 5, 5 }, { -5, 5 }, { 5, -5 }, { 5, 5 }, { -5, -5 } }

-- Signed division by two rounding towards zero (cdq; sub; sar).
local function half(v)
	return int.idiv(v, 2)
end

-- The drift d moves away from zero by the bias b while it is small and towards zero once it
-- passes 4b.
local function biased(d, b)
	if d > b * 4 then return d - b end
	if d > 0 then return d + b end
	if d < -b * 4 then return d + b end
	if d < 0 then return d - b end
	return d
end

function M.install(G)
	-- Flyers that take part in the repulsion: movable non-building units except Interceptors.
	local function repelled(self, n)
		local u = self.u
		if bit_band(u.status[n], M.CAN_MOVE) == 0 then return false end
		local t = u.type[n]
		return bit_band(self.dat.units.special_ability_flags[t], M.DAT_BUILDING) == 0 and t ~= M.INTERCEPTOR
	end

	local function cell(u, n)
		return u.repulse_x[n] + u.repulse_y[n] * M.COLUMNS
	end

	-- AddAirDensity 0x481760: a flyer without a repulse profile draws one.
	function G:air_density_add(n)
		if not repelled(self, n) then return end
		local u = self.u
		if bit_band(u.repulse_flags[n], 0xf0) == 0 then
			local r = self.rng:raw(M.RANDOM_DENSITY)
			u.repulse_angle[n] = r % 256
			local high = band(int.shr(r, 8), 0xf0)
			if high == 0 then high = 0xf0 end
			u.repulse_flags[n] = bor(u.repulse_flags[n], high)
		end
		u.repulse_flags[n] = band(u.repulse_flags[n], 0xf8)
		local s = u.sprite[n]
		u.repulse_x[n] = int.idiv(s16(self.img.spr.x[s]), M.CELL) % 256
		u.repulse_y[n] = int.idiv(s16(self.img.spr.y[s]), M.CELL) % 256
		local c = cell(u, n)
		local v = self.density[c] or 0
		if v ~= 0xff then self.density[c] = v + 1 end
	end

	-- RemoveAirDensity 0x481830.
	function G:air_density_remove(n)
		if not repelled(self, n) then return end
		local c = cell(self.u, n)
		local v = self.density[c] or 0
		if v ~= 0 then self.density[c] = v - 1 end
	end

	-- UpdateRepulseAngle 0x4814e0: the angle turns to the jittered direction of the move
	-- target and away from the map edges; near an edge the profile is cleared.
	local function update_angle(self, n)
		local u, ext = self.u, self.dat.units.unit_extents
		local s = u.sprite[n]
		local sx, sy = s16(self.img.spr.x[s]), s16(self.img.spr.y[s])
		local a = u.repulse_angle[n]
		if sx ~= u.move_x[n] or sy ~= u.move_y[n] then
			local d = self:direction_to(sx, sy, u.move_x[n], u.move_y[n])
			a = (JITTER[(a - d) % 256] + d) % 256
		end
		local t = u.type[n]
		local near_x
		if sx < s16(ext[t * 4]) + M.EDGE then
			near_x = s8(a) < 0
		elseif sx > self.map_w_px - s16(ext[t * 4 + 2]) - M.EDGE - 1 then
			near_x = s8(a) >= 0
		end
		if near_x ~= nil then
			if near_x then a = (-a) % 256 end
			u.repulse_flags[n] = band(u.repulse_flags[n], 0x0f)
		end
		local near_y
		if sy < s16(ext[t * 4 + 1]) + M.EDGE then
			near_y = s8(a + 0x40) >= 0
		elseif sy > self.map_h_px - s16(ext[t * 4 + 3]) - M.EDGE - 1 then
			near_y = s8(a + 0x40) < 0
		end
		if near_y ~= nil then
			a = (a + 0x40) % 256
			if near_y then a = (-a) % 256 end
			a = (a - 0x40) % 256
			u.repulse_flags[n] = band(u.repulse_flags[n], 0x0f)
		end
		u.repulse_angle[n] = a
	end

	-- ApplyAirRepulsion 0x481880: a flyer sharing its cell drifts the prepared step along its
	-- repulse angle (further in a crowded cell once it reached its move target); true when it
	-- drifted.
	function G:air_repulsion(n)
		local u, mv = self.u, self.mv
		if not repelled(self, n) or bit_band(u.status[n], M.STATUS_LOCKS) ~= 0 then return false end
		local density = self.density[cell(u, n)] or 0
		if density <= 1 then
			u.repulse_flags[n] = band(u.repulse_flags[n], 0xf8)
			return false
		end
		local f = u.repulse_flags[n]
		if bit_band(f, M.TIMER) ~= 0 then
			u.repulse_flags[n] = f - 1
		else
			u.repulse_flags[n] = bor(f, M.TIMER)
			update_angle(self, n)
		end
		local a = u.repulse_angle[n]
		local dx, dy = half(directions.x[a]), half(directions.y[a])
		local s = u.sprite[n]
		if u.move_x[n] == s16(self.img.spr.x[s]) and u.move_y[n] == s16(self.img.spr.y[s]) then -- 0x41a250
			local k = (int.shr(density, 2) + 1) % 256
			dx, dy = half(k * dx), half(k * dy)
		end
		local b = BIAS[int.shr(u.repulse_flags[n], 4)]
		dx, dy = biased(dx, b[1]), biased(dy, b[2])
		mv.next_halt_x = int.s32(mv.next_halt_x + dx)
		mv.next_halt_y = int.s32(mv.next_halt_y + dy)
		mv.next_x, mv.next_y = s16(int.sar(mv.next_halt_x, 8)), s16(int.sar(mv.next_halt_y, 8))
		return true
	end

	-- UpdateAirRepulsion 0x4815d0: after the step the flyer moves to its new cell; one that
	-- drifted close to its move target takes the drifted position as the target.
	function G:air_repulsion_settle(n, drifted)
		if not repelled(self, n) then return end
		local u, mv = self.u, self.mv
		local s = u.sprite[n]
		local sx, sy = s16(self.img.spr.x[s]), s16(self.img.spr.y[s])
		if u.repulse_x[n] ~= int.idiv(sx, M.CELL) or u.repulse_y[n] ~= int.idiv(sy, M.CELL) then
			self:air_density_remove(n)
			self:air_density_add(n)
		end
		if not drifted then return end
		if math.abs(u.move_x[n] - mv.next_x) < M.SETTLE and math.abs(u.move_y[n] - mv.next_y) < M.SETTLE then
			u.move_x[n], u.waypoint_x[n] = mv.next_x, mv.next_x
			u.move_y[n], u.waypoint_y[n] = mv.next_y, mv.next_y
		end
	end
end

return M
