-- Per-step unit timers (UpdateUnitTimers 0x43a320 with UpdateUnitEnergy 0x438e20,
-- UpdateSpellTimers 0x4351e0 and AcidSporeTimerTick 0x43e280; atlas P09). Timer expiries
-- whose effects are not ported raise with their native address. Methods of the game.
local int = require("game.simulation.int")

local band = int.band

local M = {}

M.SPRITE_HIDDEN = 0x20
M.DAT_REGENERATES, M.DAT_SPELLCASTER = 0x80, 0x200000
M.DAT_FLYING_BUILDING = 0x20
M.ZERGLING, M.DEVOURING_ONE = 0x25, 0x36
M.STATUS_COMPLETED, M.STATUS_GROUNDED_BUILDING = 0x1, 0x2
M.STATUS_CLOAKED, M.STATUS_FREE_CLOAK = 0x300, 0x800
M.STATUS_HALLUCINATION = 0x40000000
M.SHIELD_REGEN, M.ENERGY_REGEN = 7, 8
M.SPELL_PERIOD = 8 -- cycle_counter +0x122 runs 0..7
M.DARK_ARCHON, M.DARK_ARCHON_ENERGY = 0x3f, 0x3200
M.RACE_TERRAN = 1

-- Spell timers that end an effect: field -> native end function.
local SPELL_ENDS = {
	{ "stasis_timer", 0x4310f0 }, { "stim_timer", nil }, { "ensnare_timer", 0x43dee0 },
	{ "matrix_timer", 0x4384c0 },
}

function M.install(G)
	local function decrement(t, n)
		if t[n] ~= 0 then t[n] = t[n] - 1 end
	end

	-- GetEnergyCap 0x434a50.
	function G:energy_cap(n)
		if self.u.type[n] == M.DARK_ARCHON then
			error("timers: Dark Archon energy cap (0x42ffd0)", 0)
		end
		return self:max_energy(n)
	end

	-- UpdateUnitEnergy 0x438e20.
	function G:update_unit_energy(n)
		local u = self.u
		local status = u.status[n]
		if band(self.dat.units.special_ability_flags[u.type[n]], M.DAT_SPELLCASTER) == 0
			or band(status, M.STATUS_HALLUCINATION) ~= 0 or band(status, M.STATUS_COMPLETED) == 0 then
			return
		end
		if band(status, M.STATUS_CLOAKED) ~= 0 and band(status, M.STATUS_FREE_CLOAK) == 0 then
			error("timers: cloak energy upkeep (0x4348c0, 0x435300)", 0)
		end
		local energy, cap = u.energy[n], self:energy_cap(n)
		if energy == cap then return end
		energy = energy + M.ENERGY_REGEN
		if energy > cap then energy = cap end
		-- A selected unit's health bar images get the redraw bit here (0x416bb0), which only
		-- drives the original's dirty rectangles (game/images/world.lua): nothing to keep.
		u.energy[n] = energy
	end

	-- AcidSporeTimerTick 0x43e280: each spore timer that runs out takes one spore.
	function G:update_acid_spores(n)
		local u = self.u
		local before = u.acid_spores[n]
		for k = 0, 8 do
			local t = u["acid_time" .. k]
			if t[n] ~= 0 then
				t[n] = t[n] - 1
				if t[n] == 0 then u.acid_spores[n] = (u.acid_spores[n] - 1) % 256 end
			end
		end
		if u.acid_spores[n] ~= 0 then
			error("timers: acid spore overlays (0x422b60, 0x422ae0)", 0)
		elseif before ~= 0 then
			error("timers: acid spore overlay removal (0x422e70)", 0)
		end
	end

	-- UpdateSpellTimers 0x4351e0.
	function G:update_spell_timers(n)
		local u = self.u
		for _, e in ipairs(SPELL_ENDS) do
			local t = u[e[1]]
			if t[n] ~= 0 then
				t[n] = t[n] - 1
				if t[n] == 0 then
					if e[2] then error(("timers: end of %s (0x%x)"):format(e[1], e[2]), 0) end
					self:recalculate_speed(n) -- stim ends: RecalculateUnitSpeed 0x43aea0
				end
			end
		end
		if u.irradiate_timer[n] ~= 0 then error("timers: irradiate (0x4385d0, 0x438620)", 0) end
		if u.lockdown_timer[n] ~= 0 then
			u.lockdown_timer[n] = u.lockdown_timer[n] - 1
			if u.lockdown_timer[n] == 0 then error("timers: end of lockdown (0x4386e0)", 0) end
		end
		if u.maelstrom_timer[n] ~= 0 then
			u.maelstrom_timer[n] = u.maelstrom_timer[n] - 1
			if u.maelstrom_timer[n] == 0 then error("timers: end of maelstrom (0x438730)", 0) end
		end
		if u.plague_timer[n] ~= 0 then error("timers: plague (0x43ddb0, 0x43ddf0)", 0) end
		decrement(u.storm_timer, n)
		self:update_acid_spores(n)
	end

	-- UpdateUnitTimers 0x43a320.
	function G:update_unit_timers(n)
		local u, units = self.u, self.dat.units
		local t = u.type[n]
		decrement(u.order_timer, n)
		decrement(u.ground_cooldown, n)
		decrement(u.air_cooldown, n)
		decrement(u.spell_cooldown, n)
		local s = u.sprite[n]
		local sflags = self.img.spr.flags
		if units.shield_enabled[t] ~= 0 then
			local max = units.shield_amount[t] * 256
			if u.shields[n] ~= max then
				local v = u.shields[n] + M.SHIELD_REGEN
				if v > max then v = max end
				u.shields[n] = v -- the redraw bit of the health bars (0x416bb0) is not kept
			end
		end
		if (t == M.ZERGLING or t == M.DEVOURING_ONE) and u.ground_cooldown[n] == 0 then u.queue_timer[n] = 0 end
		u.being_healed[n] = 0
		if band(u.status[n], M.STATUS_COMPLETED) ~= 0 or band(sflags[s], M.SPRITE_HIDDEN) == 0 then
			u.cycle_counter[n] = (u.cycle_counter[n] + 1) % 256
			if u.cycle_counter[n] >= M.SPELL_PERIOD then
				u.cycle_counter[n] = 0
				self:update_spell_timers(n)
			end
		end
		if band(u.status[n], M.STATUS_COMPLETED) == 0 then return end
		local hp = u.hp[n]
		if band(units.special_ability_flags[t], M.DAT_REGENERATES) ~= 0 and hp > 0 and hp ~= units.hit_points[t] then
			error("timers: hit point regeneration (0x41b900)", 0)
		end
		self:update_unit_energy(n)
		decrement(u.recent_order_timer, n)
		if u.remove_timer[n] ~= 0 then
			u.remove_timer[n] = u.remove_timer[n] - 1
			if u.remove_timer[n] == 0 then
				self:unit_kill(n)
				return
			end
		end
		if self:unit_race(n) == M.RACE_TERRAN
			and (band(u.status[n], M.STATUS_GROUNDED_BUILDING) ~= 0
				or band(units.special_ability_flags[t], M.DAT_FLYING_BUILDING) ~= 0) then
			local max = int.sar(units.hit_points[t], 8)
			if max == 0 then
				max = int.sar(hp + 0xff, 8)
				if max == 0 then max = 1 end
			end
			if int.idiv(int.sar(hp + 0xff, 8) * 100, max) < 0x22 then
				error("timers: burning Terran building (0x41e150)", 0)
			end
		end
	end
end

return M
