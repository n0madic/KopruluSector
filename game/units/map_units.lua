-- Units created from the map (docs/research/native-mission-units.md): CreateMapUnit
-- 0x469460, ApplyUnitRecordProperties 0x46a420 and the disabled-doodad state 0x428f70.
-- Methods of the game.
local int = require("game.simulation.int")
local unit_records = require("game.data.unit_records")

local band, bor = int.band, int.bor

local M = {}

M.EXPANSION_ONLY = 0x200 -- units.dat StarEdit availability flag
-- Creep providers at creation (0x4c0790 with completion 1).
M.CREEP_BUILDINGS = { [0x84] = true, [0x85] = true, [0x90] = true, [0x92] = true }
M.CREEP_MORPHS = { [0x83] = true, [0x8f] = true }
M.CREEP_FLAG = 0x20000 -- units.dat flag: lays creep
M.ERROR_PLACEMENT_NOTICE = 0x12
M.RESOURCE_CONTAINER = 0x2000
M.MINERAL_FIRST, M.MINERAL_LAST = 0xb0, 0xb2
-- Hangar children and capacities (0x41cc20, 0x41cba0: normal, upgraded or hero).
M.HANGAR_CHILD = { [0x48] = 0x49, [0x52] = 0x49, [0x53] = 0x55, [0x51] = 0x55 }
M.HANGAR_CAPACITY = { [0x49] = { 4, 8, 8, 0x2b }, [0x55] = { 5, 10, 10, 0x24 } } -- normal, upgraded, hero, upgrade
M.DOODAD_TRAPS = { [0xcb] = true, [0xd1] = true, [0xd2] = true, [0xd3] = true, [0xd4] = true, [0xd5] = true }
M.DOODAD_LOW = { [0xcb] = true, [0xd1] = true, [0xcd] = true, [0xce] = true, [0xcf] = true, [0xd0] = true }
M.VALID = { hp = 0x20000, shields = 0x40000, energy = 0x80000, resources = 0x100000, hangar = 0x200000 }
M.STATE = { cloaked = 1, burrowed = 2, lifted = 4, hallucinated = 8, invincible = 0x10 }
M.TECH_BURROWING, M.ANIM_BURROW, M.ORDER_BURROWED = 11, 14, 117
M.ANIM_ADDON_ATTACHED = 0x11
M.BURROWED_SPRITE, M.BURROWED_LURKER_SPRITE = 300, 0x1f8
-- Cloaking technology of a type (0x434900): Personnel Cloaking, Cloaking Field, none.
M.CLOAK_TECHS = { [1] = 10, [0x10] = 10, [100] = 10, [99] = 10, [0x68] = 10, [0x33] = 10, [8] = 9, [0x15] = 9 }
M.NO_TECH = 0x2c

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
		if M.CREEP_BUILDINGS[t] or M.CREEP_MORPHS[t] or band(d.special_ability_flags[t], M.CREEP_FLAG) ~= 0 then
			local s = self.u.sprite[n]
			self:creep_place_initial(t, int.s16(self.img.spr.x[s]), int.s16(self.img.spr.y[s]))
		end
		if not self:unit_complete(n) then
			-- The original shows a 7-second text line for the local player (0x46b070) and
			-- kills the unit (0x425700).
			if self.on_text then self.on_text(owner, "placement failed") end
			self.u.user_action[n] = bor(self.u.user_action[n], 4)
			self:unit_kill(n)
			return 0
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
		if band(valid, M.VALID.hangar) ~= 0 then self:create_hangar_units(n, r.hangar % 256) end
		local states = r.states
		if band(valid, M.STATE.cloaked) ~= 0 and band(states, M.STATE.cloaked) ~= 0 then
			if self:can_use_tech(self:cloak_tech(n), u.owner[n], n) == 1 then self:cloak_hidden(n) end
		end
		if band(valid, M.STATE.burrowed) ~= 0 and band(states, M.STATE.burrowed) ~= 0
			and self:can_use_tech(M.TECH_BURROWING, u.owner[n], n) == 1 then
			self.img:sprite_play(u.sprite[n], M.ANIM_BURROW, true)
			self:set_burrowed(n)
			self:cloak_hidden(n)
			u.status[n] = bor(u.status[n], 0x200000)
			self:issue_order(n, M.ORDER_BURROWED)
		end
		if band(valid, M.STATE.lifted) ~= 0 and band(states, M.STATE.lifted) ~= 0 then self:lift_off_initial(n) end
		if band(valid, M.STATE.hallucinated) ~= 0 and band(states, M.STATE.hallucinated) ~= 0 then
			error("map units: initial hallucinated state (0x430a00)", 0)
		end
		if band(valid, M.STATE.invincible) ~= 0 and band(states, M.STATE.invincible) ~= 0 then
			u.status[n] = bor(u.status[n], 0x4000000)
		end
		if t == 0x28 then error("map units: Broodling timer (0x434a90)", 0) end
	end

	-- Hangar capacity 0x41cba0.
	function G:hangar_capacity(n)
		local u = self.u
		local child = M.HANGAR_CHILD[u.type[n]]
		if not child then return 0 end
		local c = M.HANGAR_CAPACITY[child]
		if band(self.dat.units.special_ability_flags[u.type[n]], 0x40) ~= 0 then return c[3] end
		return self:upgrade_level(u.owner[n], c[4]) ~= 0 and c[2] or c[1]
	end

	-- CreateInitialHangarUnits 0x46a180: up to the capacity, created and completed in place
	-- (0x424900, 0x424da0) and added to the hangar (0x41cc80).
	function G:create_hangar_units(n, count)
		local u = self.u
		local child_type = M.HANGAR_CHILD[u.type[n]]
		if not child_type then return end
		local capacity = self:hangar_capacity(n)
		if count > capacity then count = capacity end
		local s = u.sprite[n]
		local x, y = int.s16(self.img.spr.x[s]), int.s16(self.img.spr.y[s])
		for _ = 1, count do
			local c = self:unit_create(child_type, x, y, u.owner[n])
			if c ~= 0 then
				self:unit_prefinish(c)
				self:unit_finish_completion(c)
				self:set_union_unit(c, "union_0xc0", n)
				self:hangar_add(n, c)
			end
		end
	end

	-- AddToHangar 0x41cc80: c becomes the head of n's inside list.
	function G:hangar_add(n, c)
		local u = self.u
		self:unit_hide(c)
		local head = band(u.union_units[n], 1) ~= 0 and u.union_0xc0[n] or 0
		self:set_union_unit(c, "union_0xc4", head)
		self:set_union_unit(c, "union_0xc8", 0)
		if head ~= 0 then self:set_union_unit(head, "union_0xc8", c) end
		u.union_0xc8[n] = band(u.union_0xc8[n], 0xffffff00) + (u.union_0xc8[n] + 1) % 256
		self:set_union_unit(n, "union_0xc0", c)
		u.union_0xcc[c] = band(u.union_0xcc[c], 0xffffff00)
	end

	function G:cloak_tech(n) return M.CLOAK_TECHS[self.u.type[n]] or M.NO_TECH end

	-- AddToCloakedList 0x4340d0: the first cloak of a unit links it at the head of 0x665d40
	-- (through +0xf0/+0xf4) unless it is cloaked already or has a next link.
	function G:cloak_list_add(n)
		local u = self.u
		local count = u.is_cloaked[n]
		u.is_cloaked[n] = (count + 1) % 256
		if count == 0 and band(u.status[n], 0x100) == 0 and u.burrow_next[n] == 0 then
			u.burrow_next[n] = self.cloaked_head
			u.burrow_prev[n] = 0
			if self.cloaked_head ~= 0 then u.burrow_prev[self.cloaked_head] = n end
			self.cloaked_head = n
		end
	end

	-- TryRemoveFromCloakList 0x434da0: a unit without a cloak source this step leaves the list
	-- (losing the free-cloak bit); true when it did.
	function G:cloak_list_remove(n)
		local u = self.u
		if u.is_cloaked[n] ~= 0 then return false end
		if self.cloaked_head == n then self.cloaked_head = u.burrow_next[n] end
		local prev, next = u.burrow_prev[n], u.burrow_next[n]
		if prev ~= 0 then u.burrow_next[prev] = next end
		if next ~= 0 then u.burrow_prev[next] = prev end
		u.status[n] = band(u.status[n], 0xfffff7ff)
		u.burrow_prev[n], u.burrow_next[n] = 0, 0
		return true
	end

	-- CloakHidden 0x41d410 (burrowed or initially cloaked units).
	function G:cloak_hidden(n)
		local u = self.u
		if band(u.status[n], 0x100) ~= 0 then return end
		self:cloak_list_add(n)
		self.img:sprite_set_cloak_mode(u.sprite[n], 3, 0, 0)
		u.status[n] = bor(u.status[n], 0x300)
		self:set_secondary_order(n, 0x6d)
		u.visibility[n] = 0x80000000
	end

	-- SetBurrowed 0x43d940.
	function G:set_burrowed(n)
		local u, w = self.u, self.img
		self:reset_movement(n)
		u.status[n] = bor(u.status[n], 0x810)
		local s = u.sprite[n]
		w.spr.flags[s] = bor(w.spr.flags[s], 0x40)
		if band(w.spr.flags[s], 0x0f) ~= 0 then error("map units: burrowing a selected sprite (0x415f90)", 0) end
		w.spr.id[s] = u.type[n] == 0x67 and M.BURROWED_LURKER_SPRITE or M.BURROWED_SPRITE
		w.spr.elevation[s] = 1
	end

	-- GetUnitRace 0x428d60: Zerg 0, Terran 1, Protoss 2, else 4 (StarEdit group flags).
	function G:unit_race(n)
		local group = self.dat.units.staredit_group_flags[self.u.type[n]]
		if band(group, 1) ~= 0 then return 0 end
		if band(group, 4) ~= 0 then return 2 end
		if band(group, 2) ~= 0 then return 1 end
		return 4
	end

	-- AttachAddon 0x4359c0.
	function G:attach_addon(building, addon)
		local u, w = self.u, self.img
		self:set_union_unit(building, "union_0xc0", addon)
		if u.owner[addon] ~= u.owner[building] then error("map units: addon of another owner (0x426ed0)", 0) end
		local m = addon
		repeat
			w.spr.owner[u.sprite[m]] = u.owner[building]
			m = u.subunit[m]
		until m == 0 or band(self.dat.units.special_ability_flags[u.type[m]], 0x10) == 0
		local saved = w.unit
		w.unit = addon
		w:sprite_play(u.sprite[addon], M.ANIM_ADDON_ATTACHED, true)
		w.unit = saved
		if u.type[building] == 0x6c and u.union_0xd0[building] ~= 0 then
			error("map units: Nuclear Silo missile owner (0x426ed0)", 0)
		end
	end

	-- ApplyInitialLiftedState 0x46a210: a building that may lift off starts in the air.
	M.ORDER_LIFTOFF, M.ANIM_LIFTOFF, M.SHADOW_METHOD, M.LIFTED_SHADOW_Y = 0x48, 13, 10, 0x2a
	function G:lift_off_initial(n)
		local u, w = self.u, self.img
		if self:can_issue_order(M.ORDER_LIFTOFF, u.owner[n], n) ~= 1 then return end
		local s = u.sprite[n]
		local tail = w.spr.images[s].tail
		if w.img.method[tail] == M.SHADOW_METHOD then w.img.yoff[tail] = M.LIFTED_SHADOW_Y end
		w:sprite_play(s, M.ANIM_LIFTOFF, true)
		self:unit_deactivate_spatial(n)
		-- 0x437010: no longer a grounded building, now able to turn; 0x20000 unless a subunit.
		local status = bor(band(u.status[n], 0xfffffffd), 0x10000)
		if band(self.dat.units.special_ability_flags[u.type[n]], 0x10) ~= 0 then
			status = band(status, int.bnot(0x20000))
		else
			status = bor(status, 0x20000)
		end
		u.status[n] = status
		self:reset_movement(n)
		w.spr.elevation[s] = 12
		u.status[n] = bor(u.status[n], 4)
		self:reset_movement(n)
		self:unit_activate_spatial(n)
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
