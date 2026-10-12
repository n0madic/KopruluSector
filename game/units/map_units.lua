-- Units created from the map (docs/research/native-mission-units.md): CreateMapUnit
-- 0x469460, ApplyUnitRecordProperties 0x46a420 and the disabled-doodad state 0x428f70.
-- Methods of the game.
local int = require("game.simulation.int")
local unit_records = require("game.data.unit_records")

local band, bor = int.band, int.bor

local M = {}

M.EXPANSION_ONLY = 0x200 -- units.dat StarEdit availability flag
M.CREEP_BUILDINGS = { [0x84] = true, [0x85] = true, [0x90] = true, [0x92] = true } -- 0x4c0790
M.CREEP_MORPHS = { [0x83] = true, [0x8f] = true }
M.CREEP_FLAG = 0x20000 -- units.dat flag: lays creep
M.ERROR_PLACEMENT_NOTICE = 0x12
M.RESOURCE_CONTAINER = 0x2000
M.MINERAL_FIRST, M.MINERAL_LAST = 0xb0, 0xb2
M.HANGAR_TYPES = { [0x48] = true, [0x52] = true, [0x53] = true, [0x51] = true } -- 0x41cc20
M.DOODAD_TRAPS = { [0xcb] = true, [0xd1] = true, [0xd2] = true, [0xd3] = true, [0xd4] = true, [0xd5] = true }
M.DOODAD_LOW = { [0xcb] = true, [0xd1] = true, [0xcd] = true, [0xce] = true, [0xcf] = true, [0xd0] = true }
M.VALID = { hp = 0x20000, shields = 0x40000, energy = 0x80000, resources = 0x100000, hangar = 0x200000 }
M.STATE = { cloaked = 1, burrowed = 2, lifted = 4, hallucinated = 8, invincible = 0x10 }

function M.install(G)
	-- CreateMapUnit 0x469460.
	function G:create_map_unit(t, x, y, owner)
		local d = self.dat.units
		if band(d.staredit_availability_flags[t], M.EXPANSION_ONLY) ~= 0 and self.expansion == 0 then return 0 end
		local n = self:unit_create(t, x, y, owner)
		if n == 0 then
			self:report_error(owner, self.error_code)
			return 0
		end
		if M.CREEP_BUILDINGS[t] or band(d.special_ability_flags[t], M.CREEP_FLAG) ~= 0 then
			error(("map units: creep of type %d (0x4c0aa0)"):format(t), 0)
		end
		if not self:unit_complete(n) then
			error(("map units: placement failure of type %d (0x46b070/0x425700)"):format(t), 0)
		end
		self:guard_attach(n)
		return n
	end

	-- ApplyUnitRecordProperties 0x46a420 (record from game.data.unit_records).
	function G:apply_unit_record(n, r)
		local u, d = self.u, self.dat.units
		local t = u.type[n]
		local valid = unit_records.valid_flags(r)
		if band(valid, M.VALID.hp) ~= 0 then
			local hp = int.udiv(int.mul(r.hp_percent, d.hit_points[t]), 100)
			self:set_unit_hp(n, hp ~= 0 and hp or 1)
		end
		local max_shields = d.shield_amount[t]
		local shields = int.udiv(r.shield_percent * max_shields, 100)
		if band(valid, M.VALID.shields) ~= 0 then
			if max_shields < shields then shields = max_shields end
			u.shields[n] = shields * 256
		end
		local max_energy = int.sar(self:max_energy(n), 8)
		if band(valid, M.VALID.energy) ~= 0 then
			local e = int.udiv(max_energy * r.energy_percent, 100) % 256
			if max_energy < e then e = max_energy end
			u.energy[n] = (e * 256) % 65536
		end
		if band(valid, M.VALID.resources) ~= 0 and band(d.special_ability_flags[t], M.RESOURCE_CONTAINER) ~= 0 then
			u.union_0xd0[n] = band(u.union_0xd0[n], 0xffff0000) + r.resources % 65536
			if t >= M.MINERAL_FIRST and t <= M.MINERAL_LAST then self:update_mineral_animation(n) end
		end
		if band(valid, M.VALID.hangar) ~= 0 and r.hangar % 256 ~= 0 and M.HANGAR_TYPES[t] then
			error("map units: initial hangar units (0x46a180)", 0)
		end
		local states = r.states
		for name, bit in pairs(M.STATE) do
			if name ~= "invincible" and band(valid, bit) ~= 0 and band(states, bit) ~= 0 then
				error(("map units: initial %s state (0x46a420)"):format(name), 0)
			end
		end
		if band(valid, M.STATE.invincible) ~= 0 and band(states, M.STATE.invincible) ~= 0 then
			u.status[n] = bor(u.status[n], 0x4000000)
		end
		if t == 0x28 then error("map units: Broodling timer (0x434a90)", 0) end
	end

	-- UpdateMineralFieldAnimation 0x4247b0.
	function G:update_mineral_animation(n)
		local u = self.u
		local amount = u.union_0xd0[n] % 65536
		local anim
		if amount >= 0x2ee then
			anim = 0x14
		elseif amount >= 500 then
			anim = 0xf
		else
			anim = amount < 0xfa and 0xd or 0xe
		end
		local word = u.union_0xd0[n]
		if anim ~= int.shr(word, 16) % 256 then
			u.union_0xd0[n] = band(word, 0xff00ffff) + anim * 65536
			self.img:sprite_play(u.sprite[n], anim, true)
		end
	end

	-- DisableDoodadUnit 0x428f70.
	function G:unit_disable_doodad(n)
		local u = self.u
		if band(u.status[n], 0x400) ~= 0 or u.lockdown_timer[n] ~= 0 or u.stasis_timer[n] ~= 0
			or u.maelstrom_timer[n] ~= 0 then
			return
		end
		u.status[n] = bor(u.status[n], 0x400)
		self.img:sprite_play(u.sprite[n], 0xf, true)
		local t = u.type[n]
		u.status[n] = bor(u.status[n], 0x200000)
		if M.DOODAD_LOW[t] then self.img.spr.elevation[u.sprite[n]] = 1 end
		if M.DOODAD_TRAPS[t] then
			self:unit_set_cloak_state(n, 3)
			u.visibility[n] = 0x80000000
			u.secondary_timer[n] = 0
		end
	end
end

return M
