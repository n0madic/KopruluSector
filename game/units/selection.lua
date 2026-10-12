-- Unit selection (atlas P41, R34): the local player's interface selection (0x6a6070) with
-- its selection circles and health bars, each player's command selection (0x6a5ef0, set
-- by the select command), the control groups (0x527140: 18 per player, groups 10..17 hold
-- the recent multi-unit selections) and the recent-selection turn stamps (0x66cad8).
-- Selection-circle colours follow RefreshAllianceColors 0x41d7b0. Methods of the game.
local core = require("game.images.core")
local int = require("game.simulation.int")

local band, bor = int.band, int.bor
local NONE = core.NONE

local M = {}

M.SLOTS, M.PLAYERS, M.GROUPS, M.RECENT = 12, 8, 18, 8
M.RECENT_FIRST = 10 -- control groups 10..17
M.SPRITE_SELECTED, M.SPRITE_CIRCLE, M.SPRITE_HIDDEN = 0x08, 0x01, 0x20
M.CIRCLE_FIRST = 0x231
M.DAT_SUBUNIT, M.DAT_INVINCIBLE = 0x10, 0x20000000
M.NUCLEAR_MISSILE = 0x0e
M.PYLON = 0x9c
M.COLOR_OWN, M.COLOR_ALLY, M.COLOR_ENEMY = 0, 1, 2
-- IsMultiSelectable 0x4291b0: types that never join a multi-unit selection.
M.SINGLE_ONLY = { [0x0d] = true, [0x24] = true, [0x59] = true, [0x5a] = true, [0x5d] = true, [0x5e] = true,
	[0x5f] = true, [0x60] = true, [0x69] = true, [0xca] = true }
M.DAT_BUILDING, M.DAT_POWERUP = 0x1, 0x800
M.CURSOR_ANIMATION = 2
M.TARGET_FLASH = 31

function M.install(G)
	local function row(n)
		local t = {}
		for k = 0, n - 1 do t[k] = 0 end
		return t
	end

	-- The selection state of a new game, and the circle colours of RefreshAllianceColors
	-- 0x41d7b0 (own units 0, allies 1, enemies 2; no replay).
	function G:selection_init()
		self.order_marker = false
		self.local_selection = row(M.SLOTS)
		self.selections, self.groups, self.recent_stamps = {}, {}, {}
		for p = 0, M.PLAYERS - 1 do
			self.selections[p] = row(M.SLOTS)
			self.recent_stamps[p] = row(M.RECENT)
			self.groups[p] = {}
			for k = 0, M.GROUPS - 1 do self.groups[p][k] = row(M.SLOTS) end
		end
		self.circle_colors = {}
		local me = self.local_player
		for p = 0, 11 do
			if p == me then
				self.circle_colors[p] = M.COLOR_OWN
			else
				self.circle_colors[p] = self.alliance[me][p] == 0 and M.COLOR_ENEMY or M.COLOR_ALLY
			end
		end
		self.img.circle_colors = self.circle_colors -- 0x52904a, read by the fog ghosts' flash
	end

	-- UnitToPackedRef 0x428bd0: record number and uniqueness << 11.
	function G:unit_ref(n)
		if n == 0 then return 0 end
		return bor(n, self.u.uniqueness[n] * 0x800)
	end

	-- ShowSelectionHealthBar 0x417110: the sprite's health bar and its selection circle.
	local function show_health_bar(self, s, slot, color)
		local w = self.img
		local flags = w.spr.flags
		if band(flags[s], M.SPRITE_SELECTED) ~= 0 then return end
		flags[s] = bor(flags[s], M.SPRITE_SELECTED)
		w.spr.selection[s] = slot
		w:health_bar_create(s)
		if band(flags[s], M.SPRITE_CIRCLE) == 0 then
			if w:selection_circle_create(s, color, M.CIRCLE_FIRST) ~= NONE then
				flags[s] = bor(flags[s], M.SPRITE_CIRCLE)
			end
		end
	end

	-- AddSelectionCircle 0x423060.
	function G:add_selection_circle(n, slot)
		local u = self.u
		local t = u.type[n]
		if band(self.dat.units.special_ability_flags[t], M.DAT_INVINCIBLE) ~= 0 then
			error("selection: circle without a health bar (0x415340)", 0)
		end
		show_health_bar(self, u.sprite[n], slot, self.circle_colors[u.owner[n]])
		if u.owner[n] == self.local_player and band(u.status[n], 1) ~= 0 and t == M.PYLON then
			error("selection: pylon power field (0x430320)", 0)
		end
	end

	-- SelectUnitIntoSlot 0x422eb0: a subunit's slot holds its base unit.
	local function select_into_slot(self, n, slot)
		self:add_selection_circle(n, slot)
		if band(self.dat.units.special_ability_flags[self.u.type[n]], M.DAT_SUBUNIT) ~= 0 then
			self.local_selection[slot] = self.u.subunit[n]
		end
	end

	-- FlashSelectionCircle 0x43a580: a visible base unit's sprite flashes in its owner's
	-- circle colour (FlashSprite 0x43a540, inlined there).
	function G:flash_selection(n)
		local u, w = self.u, self.img
		if band(self.dat.units.special_ability_flags[u.type[n]], M.DAT_SUBUNIT) ~= 0 then return end
		local s = u.sprite[n]
		if band(w.spr.flags[s], M.SPRITE_HIDDEN) ~= 0 then return end
		w:sprite_flash(s, self.circle_colors[u.owner[n]])
	end

	-- RemoveSelectionCircle 0x423000: the unit loses its circle (and health bar unless it has
	-- none); a local Pylon's power fields hide.
	function G:remove_selection_circle(n)
		local u, w = self.u, self.img
		local s = u.sprite[n]
		if band(self.dat.units.special_ability_flags[u.type[n]], M.DAT_INVINCIBLE) ~= 0 then
			w:sprite_remove_circle(s)
			w.spr.flags[s] = band(w.spr.flags[s], 0xf7)
			return
		end
		w:sprite_remove_health_bar(s)
		if u.owner[n] == self.local_player and band(u.status[n], 1) ~= 0 and u.type[n] == M.PYLON then
			error("selection: hiding the power fields of a deselected Pylon (0x42fda0)", 0)
		end
	end

	-- RemoveSelectionCircleUnitOrParent 0x422b40: a subunit's base unit loses its circle.
	local function deselect(self, n)
		local u = self.u
		if band(self.dat.units.special_ability_flags[u.type[n]], M.DAT_SUBUNIT) ~= 0 then n = u.subunit[n] end
		self:remove_selection_circle(n)
	end

	-- SelectUnitsLocally 0x49cf80 with SetLocalSelectionArray 0x49cab0: the interface selection
	-- becomes `units` (a list of unit numbers). The selection sound, the command it sends
	-- (0x474130; the command log carries it) and the status panel belong to presentation.
	function G:select_locally(units)
		local sel = self.local_selection
		for k = 0, M.SLOTS - 1 do
			local n = sel[k]
			if n == 0 then break end
			sel[k] = 0
			deselect(self, n)
		end
		for k = 0, #units - 1 do
			sel[k] = units[k + 1]
			select_into_slot(self, sel[k], k)
		end
	end

	-- MovePlacementCursor 0x417ad0: a right click on the map moves the marker thingy (the
	-- placement cursor of InitThingyPools) to the point and plays its animation 2; the marker
	-- shows (byte 0x5c9988) until its IScript hides it (opcode 0x2d, 0x417ef0).
	function G:move_placement_cursor(x, y)
		local w = self.img
		local s = w.thg.sprite[w.cursor]
		self.order_marker = false
		w:sprite_move(s, x + 1, y + 1)
		w:sprite_play(s, M.CURSOR_ANIMATION, true)
		self.order_marker = true
	end

	-- MarkRightClickTarget 0x47fc10: the fog ghost under a right click, else the clicked
	-- unit, flashes (sprite timer 31); with neither the marker moves to the point.
	function G:mark_target(x, y, unit)
		local w = self.img
		local t = w:fog_at_point(x, y)
		if t ~= NONE then
			w.spr.sel_timer[w.fog.sprite[t]] = M.TARGET_FLASH
		elseif unit ~= 0 then
			w.spr.sel_timer[self.u.sprite[unit]] = M.TARGET_FLASH
		else
			self:move_placement_cursor(x, y)
		end
	end

	-- IsMultiSelectable 0x4291b0.
	function G:multi_selectable(n)
		local u = self.u
		local t = u.type[n]
		local flags = self.dat.units.special_ability_flags[t]
		if band(flags, M.DAT_BUILDING) ~= 0 or band(flags, M.DAT_POWERUP) ~= 0 then return false end
		if self:unit_disabled(n) then return false end
		return not (t >= 0xcb and t <= 0xd5) and not M.SINGLE_ONLY[t]
	end

	-- PutUnitInSelectionRow 0x49cbc0 (no team games, so no shared dashed circles).
	local function put_in_row(self, n, p, k)
		if n == 0 or p > 7 or k >= M.SLOTS then return false end
		if band(self.img.spr.flags[self.u.sprite[n]], M.SPRITE_HIDDEN) ~= 0 then return false end
		if k > 0 and (not self:multi_selectable(n) or self.u.owner[n] ~= self.command_player) then return false end
		self.selections[p][k] = n
		return true
	end

	-- AssignControlGroup 0x4643c0 with replacement: the command player's selection, as unit
	-- references, becomes control group g.
	local function assign_group(self, p, g)
		local group = self.groups[p][g]
		for k = 0, M.SLOTS - 1 do group[k] = 0 end
		local count = 0
		for k = 0, M.SLOTS - 1 do
			local n = self.selections[p][k]
			if n == 0 or self.u.owner[n] ~= self.command_player then return end
			local ref = self:unit_ref(n)
			if ref ~= 0 then
				group[count] = ref
				count = count + 1
				if count >= M.SLOTS then return end
			end
		end
	end

	-- RecordSelectionAsRecentGroup 0x464360: the oldest of the 8 recent groups (the highest
	-- slot among equal stamps) takes the selection and the current turn.
	local function record_recent(self, p)
		local stamps = self.recent_stamps[p]
		local slot, oldest = 0xff, 0xffff
		for k = M.RECENT - 1, 0, -1 do
			if stamps[k] < oldest then slot, oldest = k, stamps[k] end
		end
		assign_group(self, p, slot + M.RECENT_FIRST)
		stamps[slot] = self.turn % 65536
	end

	-- The select command 0x09 (0x472fc0): the player's selection becomes the referenced units.
	function G:command_select(p, refs)
		if #refs > M.SLOTS then return end
		-- ClearSelectionRow 0x49c7d0 (no team game).
		local sel = self.selections[p]
		for k = 0, M.SLOTS - 1 do sel[k] = 0 end
		local added = 0
		for _, ref in ipairs(refs) do
			local n = self:unit_from_ref(ref)
			if n ~= 0 then
				local present = false
				for k = 0, M.SLOTS - 1 do
					if sel[k] == 0 then break end
					if sel[k] == n then present = true break end
				end
				if not present and self.u.type[n] ~= M.NUCLEAR_MISSILE and put_in_row(self, n, p, added) then
					added = added + 1
				end
			end
		end
		if added > 1 then record_recent(self, p) end
	end

	-- FirstSelectedUnit 0x49cb10 / NextSelectedUnit 0x49cb20: the command player's selected
	-- units in slot order (a dead unit leaves every selection first: not ported yet).
	function G:selected_units(p)
		local out = {}
		for k = 0, M.SLOTS - 1 do
			local n = self.selections[p][k]
			if n == 0 then break end
			if self:unit_is_dead(n) then error("selection: dead unit in a selection (0x49c9d0, 0x472100)", 0) end
			out[#out + 1] = n
		end
		return out
	end
end

return M
