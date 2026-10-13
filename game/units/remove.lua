-- Unit removal and recycling (atlas P08/P09; docs/research/native-unit-death.md,
-- native-populated-lifecycle.md): Hide 0x422ba0, the body removal 0x425050 that leaves a
-- removed unit in the pending list, destruction 0x425cd0, and the pending pass of the object
-- step with the recycler 0x4256b0 that returns the CUnit after the free list's head (its
-- uniqueness kept until the record is allocated again). The visible Death animation, corpses
-- and the removal consumers of combat, transports, buildings, resources and the AI belong to
-- later phases and raise with their native addresses. Methods of the game.
local core = require("game.images.core")
local int = require("game.simulation.int")

local band, bor, bnot = int.band, int.bor, int.bnot
local bit_band = int.bit_band
local NONE = core.NONE
local unlink, _, insert_after_head = core.list_helpers(0)

local M = {}

M.SPRITE_SELECTED, M.SPRITE_HIDDEN, M.SPRITE_DASHED = 0x08, 0x20, 0x06
M.DAT_BUILDING, M.DAT_FLYING_BUILDING, M.DAT_WORKER, M.DAT_SUBUNIT = 0x01, 0x20, 0x08, 0x10
M.STATUS_COMPLETED, M.STATUS_GROUNDED_BUILDING = 0x1, 0x2
M.STATUS_IN_TRANSPORT, M.STATUS_REQUIRES_DETECTION = 0x40, 0x100
M.STATUS_CREEP_RETIRE = 0x4000
M.REVEALERS = { [0x21] = true, [0x65] = true } -- Scanner Sweep and Map Revealer: the special list
M.GAS_BUILDINGS = { [0x6e] = 0x434da0, [0x95] = 0x425270, [0x9d] = 0x425270 } -- Refinery, Assimilator, Extractor
-- Types whose removal tears down links (TeardownUnitTypeLinks 0x4253a0): hangar owners and
-- their children, nuke owners, silos, Nydus Canals, Pylons, Nuclear Missiles.
M.LINKED_TYPES = { [72] = true, [81] = true, [82] = true, [83] = true, [1] = true, [16] = true, [99] = true,
	[100] = true, [104] = true, [14] = true, [73] = true, [85] = true, [108] = true, [134] = true, [156] = true }
M.RESOURCE_FLAG = 0x2000 -- units.dat: resource container
M.SECONDARY_NOTHING = 0x17
M.SECONDARY_TRAIN, M.SECONDARY_BUILD_ADDON = 0x26, 0x3f

function M.install(G)
	-- ClearReferencesToUnit 0x4269b0: every active unit (and its turret) forgets n, as does
	-- every hidden unit when `full`. The status panel's reference (0x4a6e60) is the
	-- interface's.
	function G:clear_references(n, full)
		local u = self.u
		local m = self.lists.active.head
		while m ~= 0 do
			self:forget_target(m, n)
			m = u.next[m]
		end
		if not full then return end
		m = self.lists.hidden.head
		while m ~= 0 do
			self:forget_target(m, n)
			m = u.next[m]
		end
		if bit_band(self.dat.units.special_ability_flags[u.type[n]], M.DAT_WORKER) ~= 0 and u.union_0xd0[n] ~= 0
			and bit_band(u.union_units[n], 16) ~= 0 then
			error("remove: a worker leaves its resource's gatherers (0x424170)", 0)
		end
	end

	-- RemoveUnitFromAllSelectionRows 0x49c8b0: n leaves each player's command selection (its
	-- last occurrence). The dashed circles of team games (0x4230f0) never exist here, and the
	-- interface's last sent selection (0x472100) is the interface's.
	function G:remove_from_selections(n)
		for p = 0, 7 do
			local row = self.selections[p]
			local count, at = 0, nil
			while count < 12 and row[count] ~= 0 do
				if row[count] == n then at = count end
				count = count + 1
			end
			if at then
				for k = at, count - 2 do row[k] = row[k + 1] end
				row[count - 1] = 0
			end
		end
		if bit_band(self.img.spr.flags[self.u.sprite[n]], M.SPRITE_DASHED) ~= 0 then
			error("remove: dashed selection circles of a team game (0x416cd0)", 0)
		end
	end

	-- RemoveUnitFromSelection 0x4268d0: a selected unit leaves the local selection.
	function G:remove_from_local_selection(n)
		local w = self.img
		local s = self.u.sprite[n]
		if bit_band(w.spr.flags[s], M.SPRITE_SELECTED) == 0 then return end
		local slot = w.spr.selection[s]
		local rest = {}
		for k = 0, 11 do
			local m = self.local_selection[k]
			if m == 0 then break end
			if k ~= slot then rest[#rest + 1] = m end
		end
		self:select_locally(rest)
	end

	-- HideUnit 0x422ba0: the unit leaves the map: references, selections, spatial index and
	-- movement go; it moves from the active to the hidden list with its sprite (and its
	-- turret's) hidden and seen by nobody.
	function G:unit_hide(n)
		local u, w = self.u, self.img
		local s = u.sprite[n]
		if bit_band(w.spr.flags[s], M.SPRITE_HIDDEN) ~= 0 then return end
		self:clear_references(n, false)
		if self.bullets_active then error("remove: bullets aimed at a hidden unit (0x40a5a0)", 0) end
		self:remove_from_selections(n)
		self:remove_from_local_selection(n)
		self:unit_deactivate_spatial(n)
		self:reset_movement(n)
		unlink(u.prev, u.next, self.lists.active, n)
		insert_after_head(u.prev, u.next, self.lists.hidden, n)
		w.spr.flags[s] = bor(w.spr.flags[s], M.SPRITE_HIDDEN)
		w:sprite_set_visibility(s, 0)
		local sub = u.subunit[n]
		if sub ~= 0 and bit_band(self.dat.units.special_ability_flags[u.type[sub]], M.DAT_SUBUNIT) ~= 0 then
			local ss = u.sprite[sub]
			w.spr.flags[ss] = bor(w.spr.flags[ss], M.SPRITE_HIDDEN)
			w:sprite_set_visibility(ss, 0)
		end
	end

	local function unlink_owner(self, n)
		local u = self.u
		local prev, nxt = u.player_prev[n], u.player_next[n]
		if prev ~= 0 then u.player_next[prev] = nxt end
		if nxt ~= 0 then u.player_prev[nxt] = prev end
		if self.player_units[u.owner[n]] == n then self.player_units[u.owner[n]] = nxt end
		u.player_prev[n], u.player_next[n] = 0, 0
	end

	-- RemoveSpecialUnitBody 0x425270: a turret goes pending at once (with its live count);
	-- gas buildings revert to geysers (phase 6). True when handled.
	local function remove_special_body(self, n)
		local u = self.u
		local t = u.type[n]
		if bit_band(self.dat.units.special_ability_flags[t], M.DAT_SUBUNIT) ~= 0 then
			insert_after_head(u.prev, u.next, self.lists.pending, n)
			unlink_owner(self, n)
			self:account_live(n, -1)
			u.subunit[n] = 0
			return true
		end
		if bit_band(self.img.spr.flags[u.sprite[n]], M.SPRITE_HIDDEN) ~= 0 then return false end
		if M.GAS_BUILDINGS[t] then error("remove: a gas building reverts to a geyser (0x425270)", 0) end
		return false
	end

	-- MoveUnitToPendingList 0x426530: from its owner's list and its live list to the pending
	-- list.
	local function move_to_pending(self, n)
		local u = self.u
		unlink_owner(self, n)
		local from
		if M.REVEALERS[u.type[n]] then
			from = self.lists.special
		elseif bit_band(self.img.spr.flags[u.sprite[n]], M.SPRITE_HIDDEN) ~= 0 then
			from = self.lists.hidden
		else
			from = self.lists.active
		end
		unlink(u.prev, u.next, from, n)
		insert_after_head(u.prev, u.next, self.lists.pending, n)
	end

	-- RemoveUnitBody 0x425050: the unit leaves every structure of the game (its sprite stays
	-- for a Death animation) and waits in the pending list.
	function G:remove_body(n)
		local u, d = self.u, self.dat.units
		local t = u.type[n]
		local flags = d.special_ability_flags[t]
		if bit_band(flags, M.DAT_BUILDING) ~= 0 then self.ai.terrain_changed = 1 end -- 0x48dd50
		if M.LINKED_TYPES[t] or bit_band(flags, M.RESOURCE_FLAG) ~= 0 then
			error(("remove: links of a removed unit of type %d (0x4253a0)"):format(t), 0)
		end
		-- 0x425d10: a unit inside a transport leaves it; a transport kills its cargo.
		if bit_band(u.status[n], M.STATUS_IN_TRANSPORT) ~= 0 then error("remove: a unit leaves its transport (0x437320)", 0) end
		if self:is_transport(n) then
			for k = 0, 7 do
				if u["loaded" .. k][n] ~= 0 then error("remove: a transport's cargo dies (0x425d10)", 0) end
			end
		end
		self:clear_references(n, true)
		if self.bullets_active then error("remove: bullets aimed at a removed unit (0x40a5a0)", 0) end
		self:remove_unit_ai(n, 1)
		if remove_special_body(self, n) then return end
		self:stop_at_target(n)
		local s = u.sprite[n]
		if bit_band(self.img.spr.flags[s], M.SPRITE_HIDDEN) == 0 then self:unit_deactivate_spatial(n) end
		if bit_band(flags, M.DAT_BUILDING) == 0 then
			u.is_cloaked[n] = 0
			self:cloak_list_remove(n)
			if bit_band(u.status[n], M.STATUS_REQUIRES_DETECTION) ~= 0 then
				error("remove: decloaking a removed unit (0x41d360)", 0)
			end
		end
		if bit_band(u.status[n], M.STATUS_GROUNDED_BUILDING) ~= 0 then
			error("remove: building teardown (0x4251c0)", 0)
		end
		if bit_band(flags, M.DAT_FLYING_BUILDING) ~= 0 and int.shr(u.union_0xc8[n], 24) ~= 0 then
			error("remove: landing occupancy of a lifted building (0x41a190)", 0)
		end
		self:path_free(n)
		local child = u.build_unit[n]
		local second = u.secondary_order[n]
		if child ~= 0 and (second == M.SECONDARY_TRAIN or second == M.SECONDARY_BUILD_ADDON) then
			error("remove: the child of a training or building unit (0x42a4d0)", 0)
		end
		self:set_secondary_order(n, M.SECONDARY_NOTHING)
		if bit_band(flags, M.DAT_WORKER) ~= 0 and u.union_0xc0[n] ~= 0 and bit_band(u.union_units[n], 1) ~= 0 then
			error("remove: a worker drops what it carries (0x4272c0)", 0)
		end
		self:remove_from_selections(n)
		self:remove_from_local_selection(n)
		self.img:sprite_remove_circle(s)
		self:account_live(n, -1)
		if bit_band(u.status[n], M.STATUS_COMPLETED) ~= 0 then self:account_completed(n, -1, false) end
		move_to_pending(self, n)
	end

	-- DestroyUnit 0x425cd0: the turret first, then the body, then the sprite at once.
	function G:unit_destroy(n)
		local u = self.u
		local sub = u.subunit[n]
		if sub ~= 0 and bit_band(self.dat.units.special_ability_flags[u.type[sub]], M.DAT_SUBUNIT) ~= 0 then
			self:unit_destroy(sub)
			u.subunit[n] = 0
		end
		self:remove_body(n)
		self.img:sprite_destroy(u.sprite[n])
		u.sprite[n] = NONE
	end

	-- RecyclePendingUnit 0x4256b0: the CUnit returns after the free list's head with its
	-- orders cleared; the record keeps its other fields until it is allocated again.
	local function recycle(self, n)
		local u = self.u
		self.units_allocated = self.units_allocated - 1
		self:order_clear_queue(n)
		u.sprite[n] = NONE
		unlink(u.prev, u.next, self.lists.pending, n)
		insert_after_head(u.prev, u.next, self.lists.free, n)
	end

	-- The pending pass of UpdateUnits 0x438ef0 (0x438fa0): a pending unit's hidden sprite goes,
	-- its sprite runs, and a unit without a sprite returns to the free list; a sprite that
	-- remains keeps its visibility.
	function G:update_pending_units(set_context)
		local u, w = self.u, self.img
		local n = self.lists.pending.head
		while n ~= 0 do
			local nxt = u.next[n]
			set_context(self, n)
			local s = u.sprite[n]
			if s ~= NONE and bit_band(w.spr.flags[s], M.SPRITE_HIDDEN) ~= 0 then
				w:sprite_destroy(s)
				u.sprite[n] = NONE
			end
			self:step_unit_sprite(n)
			if bit_band(u.status[n], M.STATUS_CREEP_RETIRE) ~= 0 then
				error("remove: creep retirement of a pending unit (0x4b4a10)", 0)
			end
			if u.sprite[n] == NONE then
				recycle(self, n)
			else
				error("remove: the sprite of a pending unit in view (0x46bbb0, 0x438c20)", 0)
			end
			n = nxt
		end
	end
end

return M
