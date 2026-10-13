-- Cloaking and detection of units (cloak.cpp; atlas P07, docs/research/native-unit-creation.md
-- "Observer cloak"). A unit's +0xe4 holds the players that detect it (0x80000000 = not yet
-- computed). Methods of the game.
local int = require("game.simulation.int")
local spatial_search = require("game.units.spatial").search

local band, bor, bnot = int.band, int.bor, int.bnot
local bit_band = int.bit_band
local s16 = int.s16

local M = {}

M.UNDETERMINED = 0x80000000
M.DETECTION_RADIUS = 0x160
M.DETECTOR = 0x8000 -- units.dat flag
M.STATUS_CLOAKED, M.STATUS_REQUIRES_DETECTION = 0x100, 0x200
M.CLOAK_TIMER = 0x1e
M.ORDER_CLOAK = 0x6d
M.BUILDING_DETECTION_RANGE = 0xe0

-- ApproxDistance 0x405cf0.
function M.approx_distance(dx, dy)
	dx, dy = math.abs(dx), math.abs(dy)
	local big, small = dx, dy
	if big < small then big, small = small, big end
	if int.sar(big, 2) < small then
		big = big - int.sar(big, 4) + int.sar(small * 3, 3) - int.sar(big, 6) + int.sar(small * 3, 8)
	end
	return big
end

function M.install(G)
	-- QueryUnitsInRect 0x422330: units with a y bound in [top, bottom) and an x bound in
	-- [left, right), in x-array order, each once.
	function G:units_in_rect(left, top, right, bottom)
		local sp, u = self.spatial, self.u
		local stamp = sp.stamp + 1
		if stamp >= 4294967296 then stamp = 1 end
		sp.stamp = stamp
		local count, y_at = sp.count, sp.y_at
		local y0 = spatial_search(y_at, count, top, true)
		-- the first entry at or after `bottom`: the native walk up from y0, as a lower bound
		local y1 = spatial_search(y_at, count, bottom, true)
		local out = {}
		if y1 <= y0 then return out end
		local x_at, x_unit, mark = sp.x_at, sp.x_unit, sp.seen
		local bound_bottom, bound_top = u.bound_bottom, u.bound_top
		local x = spatial_search(x_at, count, left, true)
		local k = 0
		while x < count and x_at[x] < right do
			local n = x_unit[x]
			if mark[n] ~= stamp then
				mark[n] = stamp
				if y0 <= bound_bottom[n] and bound_top[n] < y1 then
					k = k + 1
					out[k] = n
				end
			end
			x = x + 1
		end
		return out
	end

	-- IsDetector 0x431a90.
	function G:is_detector(n)
		local u = self.u
		return bit_band(self.dat.units.special_ability_flags[u.type[n]], M.DETECTOR) ~= 0 and bit_band(u.status[n], 1) ~= 0
			and bit_band(u.status[n], 0x400) == 0 and u.lockdown_timer[n] == 0 and u.stasis_timer[n] == 0
			and u.maelstrom_timer[n] == 0 and u.blind[n] == 0
	end

	-- HasStatusEffect 0x434ac0.
	function G:has_status_effect(n)
		local u = self.u
		return u.matrix_damage[n] ~= 0 or u.lockdown_timer[n] ~= 0 or u.maelstrom_timer[n] ~= 0
			or u.irradiate_timer[n] ~= 0 or u.ensnare_timer[n] ~= 0 or u.stasis_timer[n] ~= 0
			or u.plague_timer[n] ~= 0 or u.acid_spores[n] ~= 0
	end

	-- UnitWithinRangeOfUnit 0x4229a0: the gap between the two units' boxes (a subunit uses
	-- its parent's) within `range`.
	function G:unit_box_within(n, range, target)
		local u, ext = self.u, self.dat.units.unit_extents
		local src = n
		if bit_band(self.dat.units.special_ability_flags[u.type[n]], 0x10) ~= 0 then src = u.subunit[n] end
		local function box(m)
			local t, s = u.type[m], u.sprite[m]
			local x, y = s16(self.img.spr.x[s]), s16(self.img.spr.y[s])
			return s16(x - ext[t * 4]), s16(y - ext[t * 4 + 1]), s16(x + ext[t * 4 + 2] + 1), s16(y + ext[t * 4 + 3] + 1)
		end
		local l1, t1, r1, b1 = box(src)
		local l2, t2, r2, b2 = box(target)
		local dx = l1 - r2
		if dx < 0 then
			dx = l2 - r1
			if dx < 0 then dx = 0 end
		end
		local dy = t1 - b2
		if dy < 0 then
			dy = t2 - b1
			if dy < 0 then dy = 0 end
		end
		return M.approx_distance(dx, dy) <= range
	end

	-- DetectorCallback 0x4318f0: detector d of another player that sees `target`.
	local function detect(self, d, target, mask)
		local u = self.u
		if bit_band(u.status[d], 0x40000000) ~= 0 or not self:is_detector(d) or d == target then return mask end
		if bit_band(self.img.spr.visibility[u.sprite[target]], 2 ^ u.owner[d]) == 0 then return mask end
		local range
		if bit_band(u.status[d], 2) == 0 then
			range = self:unit_sight(d, false) * 32
		else
			range = M.BUILDING_DETECTION_RANGE
		end
		if self:unit_box_within(d, range, target) then
			mask = bor(mask, u.parasites[d], self.shared_vision[u.owner[d]], 2 ^ u.owner[d])
		end
		return mask
	end

	-- ComputeDetection 0x4316b0: the players that detect unit n.
	function G:detection_mask(n)
		local u = self.u
		if self:has_status_effect(n) or self:unit_has_visible_cargo(n) then return 0xff end
		local s = u.sprite[n]
		local x, y = s16(self.img.spr.x[s]), s16(self.img.spr.y[s])
		local r = M.DETECTION_RADIUS
		local mask = 0
		for _, d in ipairs(self:units_in_rect(s16(x - r), s16(y - r), s16(x + r), s16(y + r))) do
			mask = detect(self, d, n, mask)
		end
		local c = self.lists.special.head
		while c ~= 0 do
			mask = detect(self, c, n, mask)
			c = u.next[c]
		end
		local owner, parasites = u.owner[n], u.parasites[n]
		mask = bor(mask, 2 ^ owner, self.shared_vision[owner], parasites)
		for p = 0, 11 do
			if bit_band(parasites, 2 ^ p) ~= 0 then mask = bor(mask, self.shared_vision[p]) end
		end
		return mask
	end

	-- UpdateDetection 0x4325a0.
	function G:update_detection(n)
		local u, w = self.u, self.img
		local mask = self:detection_mask(n)
		local old = u.visibility[n]
		if old == mask then return end
		if old == M.UNDETERMINED then old = band(bnot(mask), 0xff) end
		local changed = int.bxor(old, mask)
		u.visibility[n] = mask
		local lost = band(bnot(mask), changed)
		if lost ~= 0 then self:clear_lost_targets(n, lost) end
		if bit_band(changed, self.local_vision) ~= 0 then
			if bit_band(mask, self.local_vision) == 0 then
				local s = u.sprite[n]
				if bit_band(w.spr.flags[s], 0x0f) ~= 0 then
					error("detection: selected sprite losing detection (0x4a7b80/0x416d50)", 0)
				end
				w:sprite_cloak_undetected(s)
				if u.subunit[n] ~= 0 then w:sprite_cloak_undetected(u.sprite[u.subunit[n]]) end
			else
				w:sprite_cloak_detected(u.sprite[n])
				if u.subunit[n] ~= 0 then w:sprite_cloak_detected(u.sprite[u.subunit[n]]) end
			end
		end
	end

	-- ClearTargetReferences 0x426d30: unit m (and its turret) forgets target n.
	function G:forget_target(m, n)
		local u, o = self.u, self.orders
		for _, f in ipairs({ "build_unit", "order_unit", "move_unit", "auto_target", "connected", "irradiated_by" }) do
			if u[f][m] == n then u[f][m] = 0 end
		end
		local flags = self.dat.units.special_ability_flags
		local function word_is(field, bit)
			return bit_band(u.union_units[m], bit) ~= 0 and u[field][m] == n
		end
		if bit_band(flags[u.type[m]], 8) == 0 then
			if bit_band(u.status[m], 2) ~= 0 and word_is("union_0xc0", 1) then
				u.union_0xc0[m], u.union_units[m] = 0, band(u.union_units[m], bnot(1))
			end
		else
			if word_is("union_0xc8", 4) then u.union_0xc8[m], u.union_units[m] = 0, band(u.union_units[m], bnot(4)) end
			if int.shr(u.union_0xcc[m], 16) % 256 ~= 0 and word_is("union_0xd0", 16) then
				u.union_0xcc[m] = band(u.union_0xcc[m], 0xff00ffff)
				u.union_0xd0[m], u.union_units[m] = 0, band(u.union_units[m], bnot(16))
			end
		end
		if self.RALLY_PRODUCERS[u.type[m]] and u.rally_unit[m] == n then
			local s = u.sprite[n]
			u.rally_unit[m], u.rally_x[m], u.rally_y[m] = 0, s16(self.img.spr.x[s]), s16(self.img.spr.y[s])
		end
		local sub = u.subunit[m]
		if sub ~= 0 and bit_band(flags[u.type[sub]], 0x10) ~= 0 then self:forget_target(sub, n) end
		local node = u.queue_head[m]
		while node ~= -1 do
			local nxt = o.next[node]
			if o.unit[node] == n then self:order_remove(m, node) end
			node = nxt
		end
	end

	-- ClearLostTargets 0x426b80: units of the players that lost sight of n forget it.
	function G:clear_lost_targets(n, players)
		for p = 0, 11 do
			if bit_band(players, 2 ^ p) ~= 0 then
				local m = self.player_units[p]
				while m ~= 0 do
					self:forget_target(m, n)
					m = self.u.player_next[m]
				end
			end
		end
	end

	-- BeginCloak 0x41d200 (permanent cloak at completion, the Cloak order).
	function G:begin_cloak(n, sound)
		local u, w = self.u, self.img
		if bit_band(u.status[n], 0x10) == 0 and self.play_sound then self:play_sound(sound, n) end
		if bit_band(u.status[n], 0x10) == 0 then
			w:sprite_begin_cloak(u.sprite[n])
			u.visibility[n] = M.UNDETERMINED
			u.status[n] = bor(band(u.status[n], bnot(M.STATUS_REQUIRES_DETECTION)), M.STATUS_CLOAKED)
			local sub = u.subunit[n]
			if sub ~= 0 then
				w:sprite_begin_cloak(u.sprite[sub])
				u.visibility[sub] = M.UNDETERMINED
				u.status[sub] = bor(band(u.status[sub], bnot(M.STATUS_REQUIRES_DETECTION)), M.STATUS_CLOAKED)
			end
		elseif bit_band(u.status[n], M.STATUS_CLOAKED) == 0 then
			error("detection: cloaking a burrowed unit (0x4340d0)", 0)
		end
		self:update_detection(n)
		u.secondary_timer[n] = M.CLOAK_TIMER
	end
end

return M
