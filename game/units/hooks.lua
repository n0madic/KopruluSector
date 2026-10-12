-- The unit side of the IScript VM's context hooks (game/images/iscript.lua lists them).
-- Hooks not bound here stay missing, so an opcode that needs them raises instead of doing
-- nothing (plan §7 phase 3: no animation-only no-ops). Methods of the game.
local int = require("game.simulation.int")
local directions = require("game.data.directions")

local band, bor, bnot = int.band, int.bor, int.bnot

local M = {}

function M.install(G)
	-- Speed components from the heading WORD +0x4a (0x40c220, 0x40bf50, 0x40bfd0).
	local function velocity(self, n)
		local u = self.u
		local h = u.heading2[n]
		u.speed_x[n] = int.sar(int.mul(directions.x[h], u.speed1[n]), 8)
		u.speed_y[n] = int.sar(int.mul(directions.y[h], u.speed1[n]), 8)
	end

	-- SetCurrentSpeed 0x40c220.
	function G:flingy_set_speed(n, v)
		local u = self.u
		u.speed2[n] = v
		if u.speed1[n] ~= v then
			u.speed1[n] = v
			velocity(self, n)
		end
	end

	-- TurnCounterclockwise 0x40bf50 / TurnClockwise 0x40bfd0 (8 heading units per step).
	function G:flingy_turn(n, steps)
		local u = self.u
		local h = (u.heading[n] + steps * 8) % 256
		u.velocity_direction[n], u.heading[n], u.heading2[n] = h, h, h
		velocity(self, n)
		local s = u.sprite[n]
		local sx, sy = int.s16(self.img.spr.x[s]), int.s16(self.img.spr.y[s])
		if sx ~= u.target_waypoint_x[n] or sy ~= u.target_waypoint_y[n] then
			u.target_waypoint_x[n], u.target_waypoint_y[n] = sx, sy
		end
		self.img:sprite_set_facing(s, h)
	end

	-- SetSpawnFrame 0x40cf00: the next target waypoint 16 pixels ahead in direction n * 8.
	function G:flingy_set_spawn_frame(n, d)
		local u = self.u
		local k = (d * 8) % 256
		local x = int.sar(int.shl(directions.x[k], 12), 8) + u.halt_x[n]
		local y = int.sar(int.shl(directions.y[k], 12), 8) + u.halt_y[n]
		x, y = int.sar(x, 8), int.sar(y, 8)
		if x ~= u.target_waypoint_x[n] or y ~= u.target_waypoint_y[n] then
			u.target_waypoint_x[n], u.target_waypoint_y[n] = int.s16(x), int.s16(y)
		end
	end

	-- NobrkCodeEnd 0x429cc0.
	function G:nobrk_end(n)
		local u, w = self.u, self.img
		u.status[n] = band(u.status[n], bnot(0x2000))
		local s = u.sprite[n]
		w.spr.flags[s] = band(w.spr.flags[s], 0x7f)
		if u.queue_head[n] ~= -1 and band(u.user_action[n], 1) ~= 0 then
			self:reset_to_idle_animation(n)
			self:order_activate(n)
		end
	end

	function G:image_hooks()
		local g = self
		return {
			unit_status = function(_, n) return g.u.status[n] end,
			unit_owner = function(_, n) return g.u.owner[n] end,
			unit_type = function(_, n) return g.u.type[n] end,
			unit_sprite = function(_, n) return g.u.sprite[n] end,
			unit_heading = function(_, n) return g.u.heading[n] end,
			unit_target = function(_, n)
				local t = g.u.order_unit[n]
				if t == 0 then return nil end
				return t
			end,
			set_cloak_state = function(_, n, state) g:unit_set_cloak_state(n, state) end,
			move_speed = function(_, n, v) return g:top_speed_modified(n, v) end,
			set_move_speed = function(_, n, v) g:flingy_set_speed(n, v) end,
			turn_ccw = function(_, n, steps) g:flingy_turn(n, -steps) end,
			turn_cw = function(_, n, steps) g:flingy_turn(n, steps) end,
			set_spawn_frame = function(_, n, d) g:flingy_set_spawn_frame(n, d) end,
			set_flingy_direction = function(_, n, d) g:flingy_set_heading(n, d) end,
			set_top_speed = function(_, n, v) g.u.top_speed[n] = v end,
			order_signal = function(_, n, set, clear)
				g.u.order_signal[n] = band(bor(g.u.order_signal[n], set), bnot(clear)) % 256
			end,
			clear_repeat_attack = function(_, n) g.u.movement_flags[n] = band(g.u.movement_flags[n], 0xf7) end,
			nobrk_start = function(_, n)
				g.u.status[n] = bor(g.u.status[n], 0x2000)
				local s = g.u.sprite[n]
				g.img.spr.flags[s] = bor(g.img.spr.flags[s], 0x80)
			end,
			nobrk_end = function(_, n) g:nobrk_end(n) end,
			ignore_rest_idle = function(_, n) g:reset_to_idle_animation(n) end,
			hide_order_marker = function() g.order_marker = false end, -- 0x417ef0
			unit_fits = function(_, t, x, y) return g:unit_type_fits(t, x, y) end,
			-- The unit of a selected sprite's health bar: its local selection slot (0x40f6e0).
			selection_unit = function(_, s) return g.local_selection[g.img.spr.selection[s]] end,
		}
	end
end

return M
