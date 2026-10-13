-- Ownership transfer (ChangeUnitOwner 0x426ed0; atlas P09) with the AI hooks around it:
-- RemoveUnitAi 0x48dbc0 before, AssignCreatedUnitAi 0x4b1fe0 after, and the order targets
-- that stop being enemies (ClearNonEnemyTargets 0x41dd40). Methods of the game.
local int = require("game.simulation.int")

local band, bor, bnot = int.band, int.bor, int.bnot
local bit_band = int.bit_band

local M = {}

M.CONTROLLER_COMPUTER = 1
M.NEUTRAL = 11
M.SPRITE_SELECTED = 0x08
M.STATUS_COMPLETED, M.STATUS_GROUNDED_BUILDING = 0x1, 0x2
M.DAT_WORKER, M.DAT_SUBUNIT = 0x08, 0x10
M.AI_TOWN = 0x2 -- units.dat AI flag: a computer's unit joins its town instead of a guard record
M.MEDIC, M.FLAG = 0x22, 0xd7
M.RACE_PROTOSS = 2

function M.install(G)
	-- IsEnemyOfPlayer 0x429110: the player is not allied with the unit's owner (the sprite's
	-- owner for the neutral player).
	local function enemy_of_player(self, p, n)
		local owner = self.u.owner[n]
		if owner == M.NEUTRAL then owner = self.img.spr.owner[self.u.sprite[n]] end
		return self.alliance[p][owner] == 0
	end

	-- RemoveUnitAi 0x48dbc0: every AI structure forgets the unit. Computer players' AI regions
	-- (0x4b0770) hold the strength of their enemies, and the AI records, towns and the free
	-- Medic slot belong to the AI phase, so a unit that any of them would change raises.
	-- TODO: the AI regions' references to the unit (+0x50..+0x5c of each region record),
	-- which the AI phase models; they are empty before any AI runs.
	function G:remove_unit_ai(n, killed)
		local u = self.u
		for p = 0, 11 do
			if self.players[p].controller == M.CONTROLLER_COMPUTER and enemy_of_player(self, p, n) then
				error("ai: region strength of a unit leaving its owner (0x4b0770)", 0)
			end
		end
		if u.ai[n] ~= 0 then error(("ai: removing the AI record of a unit (0x48dbc0, killed %d)"):format(killed), 0) end
		if u.type[n] == M.MEDIC and self.players[u.owner[n]].controller == M.CONTROLLER_COMPUTER then
			error("ai: the free Medic of a computer player (0x483220)", 0)
		end
	end

	-- AssignCreatedUnitAi 0x4b1fe0: a computer player's unit joins its first town (none exist
	-- before the AI phase, 0x484f40) or takes a guard record.
	function G:assign_unit_ai(n)
		local u = self.u
		local p = u.owner[n]
		if p >= 8 then return end
		if u.type[n] == M.MEDIC and self.players[p].controller == M.CONTROLLER_COMPUTER then
			error("ai: the free Medic of a computer player (0x482520)", 0)
		end
		if bit_band(self.dat.units.ai_internal[u.type[n]], M.AI_TOWN) ~= 0 then return end
		self:guard_attach(n)
	end

	-- ClearNonEnemyTargets 0x41dd40: units that attack (orders.dat weapon targeting) drop an
	-- order target that is no longer their enemy: n's own target, then every active and
	-- hidden unit (its subunit when it has one) that targets n.
	local function clear_non_enemy_targets(self, n)
		local u, d = self.u, self.dat
		local targeting = d.orders.use_weapon_targeting
		local target = u.order_unit[n]
		if target ~= 0 and targeting[u.order[n]] ~= 0 and not self:is_enemy(n, target) then u.order_unit[n] = 0 end
		for _, list in ipairs({ self.lists.active, self.lists.hidden }) do
			local m = list.head
			while m ~= 0 do
				local x = m
				local sub = u.subunit[m]
				if sub ~= 0 and bit_band(d.units.special_ability_flags[u.type[sub]], M.DAT_SUBUNIT) ~= 0 then x = sub end
				if u.order_unit[x] == n and targeting[u.order[x]] ~= 0 and not self:is_enemy(x, n) then
					u.order_unit[x] = 0
				end
				m = u.next[m]
			end
		end
	end

	-- ChangeUnitOwner 0x426ed0: the unit (and its subunit) moves to the new owner's unit
	-- list with its counts, selection circle colour, AI and speed upgrades. `score` counts
	-- a completed unit as produced by the new owner.
	function G:change_owner(n, owner, score)
		local u, w = self.u, self.img
		if u.owner[n] == owner then return end
		self:remove_unit_ai(n, 0)
		local morphing = self:is_morphing_zerg_building(n)
		if morphing then u.status[n] = bor(u.status[n], M.STATUS_COMPLETED) end
		self:account_live(n, -1)
		local completed = bit_band(u.status[n], M.STATUS_COMPLETED) ~= 0
		if completed then self:account_completed(n, -1, false) end
		local s = u.sprite[n]
		local selected = bit_band(w.spr.flags[s], M.SPRITE_SELECTED) ~= 0
		local slot = 0
		if selected then
			slot = w.spr.selection[s]
			self:remove_selection_circle(n)
		end
		local prev, nxt = u.player_prev[n], u.player_next[n]
		if prev ~= 0 then u.player_next[prev] = nxt end
		if nxt ~= 0 then u.player_prev[nxt] = prev end
		if self.player_units[u.owner[n]] == n then self.player_units[u.owner[n]] = nxt end
		u.player_prev[n], u.player_next[n] = 0, 0
		u.owner[n] = owner
		if selected then self:add_selection_circle(n, slot) end
		local head = self.player_units[owner]
		u.player_prev[n], u.player_next[n] = 0, head
		if head ~= 0 then u.player_prev[head] = n end
		self.player_units[owner] = n
		self:account_live(n, 1)
		if completed then self:account_completed(n, 1, score) end
		if morphing then u.status[n] = band(u.status[n], bnot(M.STATUS_COMPLETED)) end
		clear_non_enemy_targets(self, n)
		if self:unit_race(n) == M.RACE_PROTOSS and bit_band(u.status[n], M.STATUS_GROUNDED_BUILDING) ~= 0 then
			self.power_dirty = true
		end
		self:assign_unit_ai(n)
		local sub = u.subunit[n]
		if sub ~= 0 and bit_band(self.dat.units.special_ability_flags[u.type[sub]], M.DAT_SUBUNIT) ~= 0 then
			self:change_owner(sub, owner, score)
		end
		self:apply_creation_upgrades(n)
		if bit_band(self.dat.units.special_ability_flags[u.type[n]], M.DAT_WORKER) ~= 0 then
			local item = u.union_0xc0[n]
			if item ~= 0 and u.type[item] == M.FLAG and (owner == u.owner[item] or owner >= 8) then
				error("owner: a worker drops the flag it carries (0x4272c0)", 0)
			end
		end
	end
end

return M
