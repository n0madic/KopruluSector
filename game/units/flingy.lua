-- The flingy part of a unit (CUnit begins with CFlingy): DAT movement parameters, heading,
-- position and move target, and the speed modifiers of status effects and upgrades.
-- Methods of the game (game/simulation/game.lua); field names follow tools/native_state.py.
local bytes = require("game.data.bytes")
local int = require("game.simulation.int")

local band, bor = int.band, int.bor
local s16 = int.s16

local M = {}

M.MOVE_FLINGY, M.MOVE_PARTIAL, M.MOVE_ISCRIPT = 0, 1, 2 -- flingy.dat movement control
M.STATUS_SPEED_UPGRADE = 0x10000000
M.FAST_FLYERS = { [0x46] = true, [0x50] = true, [0x58] = true } -- Scout, hero Scouts: 0x6ab
M.TOP_SPEED_BOOSTED_MAX, M.TOP_SPEED_BOOSTED_MIN = 0x6ab, 0x355
M.WALK_SAMPLES = 32
M.ANIM_WALKING = 11

-- Direction vectors 0x5026b0 (game/data/directions.lua).
local directions = require("game.data.directions")

function M.install(G)
	-- Net speed modifier count of 0x428c10/0x428c70/0x429350/0x4293a0: stim, speed upgrade,
	-- ensnare.
	local function modifier(self, n)
		local u = self.u
		local k = u.stim_timer[n] ~= 0 and 1 or 0
		if band(u.status[n], M.STATUS_SPEED_UPGRADE) ~= 0 then k = k + 1 end
		if u.ensnare_timer[n] ~= 0 then k = k - 1 end
		return k
	end

	-- ApplySpeedModifierToValue 0x429350.
	function G:speed_modified(n, v)
		local k = modifier(self, n)
		if k > 0 then return (v * 2) % 4294967296 end
		if k < 0 then return v - int.shr(v, 2) end
		return v
	end

	-- ApplySpeedModifierToTopSpeed 0x4293a0.
	function G:top_speed_modified(n, v)
		local k = modifier(self, n)
		if k < 1 then
			if k < 0 then return int.shr(v, 1) end
			return v
		end
		if M.FAST_FLYERS[self.u.type[n]] then return M.TOP_SPEED_BOOSTED_MAX end
		v = (v + int.shr(v, 1)) % 4294967296
		if v < M.TOP_SPEED_BOOSTED_MIN then return M.TOP_SPEED_BOOSTED_MIN end
		return v
	end

	local function flingy_of(self, n) return self.dat.units.graphics[self.u.type[n]] end

	-- GetModifiedAcceleration 0x428c10 (WORD) and GetModifiedTurnRadius 0x428c70 (BYTE).
	function G:acceleration_modified(n)
		local v = self.dat.flingy.acceleration[flingy_of(self, n)]
		local k = modifier(self, n)
		if k > 0 then return (v * 2) % 65536 end
		if k < 0 then v = v - int.shr(v, 2) end
		return v
	end

	function G:turn_radius_modified(n)
		local v = self.dat.flingy.turn_radius[flingy_of(self, n)]
		local k = modifier(self, n)
		if k > 0 then return (v * 2) % 256 end
		if k < 0 then v = v - int.shr(v, 2) end
		return v
	end

	-- ComputeIScriptWalkSpeed 0x42dff0: for IScript-driven movers, the top speed is the mean
	-- of 32 samples of the Walking animation's `move` distances run without side effects
	-- (the static record 0x665b48: the main image's header, Walking entry, no wait).
	function G:iscript_walk_speed(n)
		local u = self.u
		if u.movement_type[n] ~= M.MOVE_ISCRIPT then return end
		local w = self.img
		local main = w.spr.main[u.sprite[n]]
		local header = w.img.header[main]
		local code = w.script.code
		if bytes.u16(code, header + 4) < M.ANIM_WALKING then return end
		local entry = bytes.u16(code, header + 8 + 2 * M.ANIM_WALKING)
		if entry == 0 then return end
		local state = { header = header, pc = entry, ret = 0, wait = 0 }
		local sum = 0
		for _ = 1, M.WALK_SAMPLES do
			local saved = w.unit
			w.unit = n
			local speed = w:run_iscript(main, state, true, true) or 0
			w.unit = saved
			sum = sum + self:speed_modified(n, speed)
		end
		u.top_speed[n] = int.idiv(int.s32(sum), M.WALK_SAMPLES)
	end

	-- RecalculateUnitSpeed 0x43aea0 (and the tail of 0x43a8e0 / 0x43aef0).
	function G:recalculate_speed(n)
		local u = self.u
		local control = self.dat.flingy.movement_control[flingy_of(self, n)]
		if control ~= M.MOVE_FLINGY and control ~= M.MOVE_PARTIAL then
			self:iscript_walk_speed(n)
			return
		end
		u.top_speed[n] = self:top_speed_modified(n, self.dat.flingy.speed[flingy_of(self, n)])
		u.acceleration[n] = self:acceleration_modified(n)
		u.turn_speed[n] = self:turn_radius_modified(n)
	end

	-- SetSpeedUpgradeStatus 0x43aef0.
	function G:set_speed_upgrade(n)
		self.u.status[n] = bor(self.u.status[n], M.STATUS_SPEED_UPGRADE)
		self:recalculate_speed(n)
	end

	-- InitFlingy 0x40c2b0 (with InitThingySprite 0x4183b0): returns false when the sprite
	-- cannot be created.
	function G:flingy_init(n, flingy, x, y, owner, heading)
		local u, f = self.u, self.dat.flingy
		u.flingy[n] = flingy
		u.movement_flags[n] = 0
		u.speed2[n] = 0
		u.top_speed[n] = f.speed[flingy]
		u.acceleration[n] = f.acceleration[flingy]
		u.turn_speed[n] = f.turn_radius[flingy]
		u.movement_type[n] = f.movement_control[flingy]
		x, y = s16(x), s16(y)
		u.x[n], u.y[n] = x, y
		u.halt_x[n], u.halt_y[n] = x * 256, y * 256
		if x ~= u.move_x[n] or y ~= u.move_y[n] then
			u.move_x[n], u.move_y[n], u.move_unit[n] = x, y, 0
			u.movement_flags[n] = 1
			u.waypoint_x[n], u.waypoint_y[n] = x, y
		end
		if x ~= u.target_waypoint_x[n] or y ~= u.target_waypoint_y[n] then
			u.target_waypoint_x[n], u.target_waypoint_y[n] = x, y
		end
		u.heading[n], u.velocity_direction[n] = heading, heading
		u.hp[n] = 1 -- the thingy's "+8 = 1" of 0x4183b0 lands in the unit's hit points
		local s = self.img:sprite_create(f.sprite[flingy], x % 65536, y % 65536, owner)
		u.sprite[n] = s
		if s == self.NONE then return false end
		self.img:sprite_set_facing(s, u.heading[n])
		return true
	end

	-- SetFlingyHeading 0x40c1b0.
	function G:flingy_set_heading(n, heading)
		local u = self.u
		heading = heading % 256
		u.velocity_direction[n], u.heading[n], u.heading2[n] = heading, heading, heading
		u.speed_x[n] = int.sar(int.mul(directions.x[heading], u.speed1[n]), 8)
		u.speed_y[n] = int.sar(int.mul(directions.y[heading], u.speed1[n]), 8)
		local s = u.sprite[n]
		local sx, sy = s16(self.img.spr.x[s]), s16(self.img.spr.y[s])
		if sx ~= u.target_waypoint_x[n] or sy ~= u.target_waypoint_y[n] then
			u.target_waypoint_x[n], u.target_waypoint_y[n] = sx, sy
		end
		self.img:sprite_set_facing(s, heading)
	end

	-- SetPixelPosition 0x40cd80.
	function G:flingy_set_position(n, x, y)
		local u = self.u
		x, y = s16(x), s16(y)
		u.halt_x[n], u.x[n], u.y[n], u.halt_y[n] = x * 256, x, y, y * 256
		self.img:sprite_move(u.sprite[n], x % 65536, y % 65536)
	end

	-- SetExactPosition 0x40cc00: the position in 1/256 pixels.
	function G:flingy_set_exact_position(n, hx, hy)
		local u = self.u
		local x, y = s16(int.sar(hx, 8)), s16(int.sar(hy, 8))
		u.x[n], u.y[n], u.halt_x[n], u.halt_y[n] = x, y, hx, hy
		self.img:sprite_move(u.sprite[n], x % 65536, y % 65536)
	end

	-- SetNextTargetWaypoint 0x40cbd0.
	function G:flingy_set_target_waypoint(n, x, y)
		local u = self.u
		if x ~= u.target_waypoint_x[n] or y ~= u.target_waypoint_y[n] then
			u.target_waypoint_x[n], u.target_waypoint_y[n] = s16(x), s16(y)
		end
	end

	-- SetMoveTarget 0x40cca0.
	function G:flingy_set_move_target(n, x, y)
		local u = self.u
		if x ~= u.move_x[n] or y ~= u.move_y[n] then
			u.move_x[n], u.move_y[n] = s16(x), s16(y)
			u.movement_flags[n] = bor(band(u.movement_flags[n], 0xfb), 1)
			u.move_unit[n] = 0
			u.waypoint_x[n], u.waypoint_y[n] = u.move_x[n], u.move_y[n]
		end
	end

	-- FlingyToIdleAnimation 0x40ceb0 with PlayAttackToIdleAnimation 0x4176e0.
	function G:flingy_to_idle(n)
		local w = self.img
		local s = self.u.sprite[n]
		local main = w.spr.main[s]
		local anim = w.img.anim[main]
		local target
		if anim == 2 or anim == 5 then
			target = 8
		elseif anim == 3 or anim == 6 then
			target = 9
		elseif anim == 7 then
			target = 12
		elseif anim == 13 then
			if w.spr.id[s] == 0x1e9 then target = 12 end
		elseif anim == 15 then
			local id = w.spr.id[s]
			if id == 0xef or id == 0x8c or id == 0xc2 then target = 8 end
		end
		if target then
			local i = w.spr.images[s].head
			while i ~= self.NONE do
				w:image_play(i, target)
				i = w.img.next[i]
			end
		end
		self.u.movement_flags[n] = band(self.u.movement_flags[n], 0xf7)
	end
end

return M
