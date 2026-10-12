-- Target acquisition of idle and guarding units (AcquireBestTarget 0x431c40 with its
-- candidate callback 0x432280; atlas P11) and the unit relations it reads: alliances
-- (0x534914), weapons, the seek range and CanAttackTarget 0x41da50. A candidate that passes
-- every filter reaches the scoring of the original (0x41fe60, 0x48d2e0, 0x4319d0), which is
-- not ported yet and raises. Methods of the game.
local int = require("game.simulation.int")

local band = int.band
local s16 = int.s16

local M = {}

M.NO_WEAPON = 0x82
M.NEUTRAL_OWNER = 11
M.STATUS_INVINCIBLE = 0x4000000
M.STATUS_AIR, M.STATUS_IN_BUILDING, M.STATUS_CLOAKED, M.STATUS_BURROWED = 0x4, 0x20, 0x300, 0x10
M.SPRITE_HIDDEN = 0x20
M.DAT_SUBUNIT = 0x10
M.CARRIER, M.GANTRITHOR, M.INTERCEPTOR = 0x48, 0x52, 0x47
M.REAVERS = { [0x53] = true, [0x51] = true } -- 0x41b3d0
M.QUEENS = { [0x2d] = true, [0x31] = true } -- 0x41ff30
M.LURKER = 0x67
M.QUERY_MARGIN = 0x40 -- 0x431c40 searches the seek range plus 64 pixels
-- GetSeekRange 0x41f320: cloaked units of these types do not seek (unless ordered 0x6b).
M.CLOAKED_NO_SEEK = { [1] = true, [16] = true, [100] = true, [99] = true, [104] = true }
M.CLOAKED_SEEK_ORDER = 0x6b
-- Seek range bonuses (its jump table): type -> { upgrade or "expansion", bonus }.
M.SEEK_BONUS = {
	[0] = { 0x10, 1 }, [3] = { 0x36, 3 }, [4] = { 0x36, 3 }, [17] = { "expansion", 3 }, [18] = { "expansion", 3 },
	[38] = { 0x1e, 1 }, [66] = { 0x21, 2 }, [78] = { "expansion", 2 },
}

function M.install(G)
	local function subunit_type(self, n)
		return n ~= 0 and band(self.dat.units.special_ability_flags[self.u.type[n]], M.DAT_SUBUNIT) ~= 0
	end

	-- IsEnemyUnit 0x4290e0: player 11's units count as their sprite's owner.
	function G:is_enemy(n, target)
		local owner = self.u.owner[target]
		if owner == M.NEUTRAL_OWNER then owner = self.img.spr.owner[self.u.sprite[target]] end
		return self.alliance[self.u.owner[n]][owner] == 0
	end

	-- IsUnitUndetectedBy 0x40a180: a cloaked unit that the viewer's player does not detect.
	function G:is_undetected_by(n, viewer)
		local u = self.u
		return band(u.status[n], M.STATUS_CLOAKED) ~= 0 and band(u.visibility[n], 2 ^ u.owner[viewer]) == 0
	end

	-- GetGroundWeapon 0x41fc20 (an unburrowed Lurker has none).
	function G:ground_weapon(n)
		local u = self.u
		if u.type[n] == M.LURKER and band(u.status[n], M.STATUS_BURROWED) == 0 then return M.NO_WEAPON end
		return self.dat.units.ground_weapon[u.type[n]]
	end

	-- GetGroundWeaponWithSubunit 0x41fbe0 and GetAirWeaponWithSubunit 0x41fb40.
	function G:ground_weapon_with_subunit(n)
		local w = self:ground_weapon(n)
		if w == M.NO_WEAPON and self.u.type[n] ~= M.LURKER then
			local sub = self.u.subunit[n]
			if sub == 0 then return M.NO_WEAPON end
			return self.dat.units.ground_weapon[self.u.type[sub]]
		end
		return w
	end

	function G:air_weapon_with_subunit(n)
		local units = self.dat.units
		local w = units.air_weapon[self.u.type[n]]
		if w == M.NO_WEAPON then
			local sub = self.u.subunit[n]
			if sub == 0 then return M.NO_WEAPON end
			return units.air_weapon[self.u.type[sub]]
		end
		return w
	end

	-- GetSeekRange 0x41f320 (tiles).
	function G:seek_range(n)
		local u = self.u
		local t = u.type[n]
		if M.CLOAKED_NO_SEEK[t] and band(u.status[n], M.STATUS_CLOAKED) ~= 0 and u.order[n] ~= M.CLOAKED_SEEK_ORDER then
			return 0
		end
		local range = self.dat.units.target_acquisition_range[t]
		local bonus = M.SEEK_BONUS[t]
		if bonus then
			local applies
			if bonus[1] == "expansion" then
				applies = self.expansion ~= 0
			else
				applies = self:upgrade_level(u.owner[n], bonus[1]) ~= 0
			end
			if applies then range = range + bonus[2] end
		end
		return range % 256
	end

	-- CanAttackTarget 0x41da50.
	function G:can_attack(n, target, check_detection)
		local u = self.u
		if self:unit_disabled(n) or target == 0 then return false end
		local ts = u.status[target]
		if band(ts, M.STATUS_INVINCIBLE) ~= 0 then return false end
		if band(self.img.spr.flags[u.sprite[target]], M.SPRITE_HIDDEN) ~= 0 then return false end
		if check_detection and self:is_undetected_by(target, n) then return false end
		local attacker = subunit_type(self, u.subunit[n]) and u.subunit[n] or n
		local t = u.type[n]
		if t == M.CARRIER or t == M.GANTRITHOR then return true end
		if M.REAVERS[t] then
			if band(ts, M.STATUS_AIR) ~= 0 then return false end
			error("targets: Reaver ground group check (0x481cb0)", 0)
		end
		if M.QUEENS[t] then error("targets: Queen target check (0x41fed0)", 0) end
		if t == M.INTERCEPTOR and u.ai[n] ~= 0 then return false end
		if band(ts, M.STATUS_AIR) == 0 then return self:ground_weapon(attacker) ~= M.NO_WEAPON end
		return self.dat.units.air_weapon[u.type[attacker]] ~= M.NO_WEAPON
	end

	-- SetMinimumAttackRange 0x433c50: the smaller minimum range of the two weapons (0 for
	-- a unit without weapons).
	local function minimum_range(self, n)
		local min = self.dat.weapons.minimum_range
		local g, a = self:ground_weapon_with_subunit(n), self:air_weapon_with_subunit(n)
		if g == M.NO_WEAPON then
			if a ~= M.NO_WEAPON then return min[a] end
			return 0
		end
		if a ~= M.NO_WEAPON then return math.min(min[g], min[a]) end
		return min[g]
	end

	-- The candidate callback 0x432280 of unit n.
	local function consider(self, n, c, range, min_range)
		local u = self.u
		if c == n then return end
		if band(self.img.spr.visibility[u.sprite[c]], 2 ^ u.owner[n]) == 0 then return end
		if not self:can_attack(n, c, true) then return end
		if not self:is_enemy(n, c) then return end
		if min_range ~= 0 and self:unit_box_within(n, min_range, c) then return end
		if not self:unit_box_within(n, range, c) then return end
		error("targets: scoring of an attack candidate (0x41fe60, 0x48d2e0, 0x4319d0)", 0)
	end

	-- The candidates around unit n (FindUnitInRadiusBox 0x442b20 with 0x432280).
	local function scan(self, n, range, min_range)
		local s = self.u.sprite[n]
		local x, y = s16(self.img.spr.x[s]), s16(self.img.spr.y[s])
		local r = range + M.QUERY_MARGIN
		for _, c in ipairs(self:units_in_rect(s16(x - r), s16(y - r), s16(x + r), s16(y + r))) do
			consider(self, n, c, range, min_range)
		end
		return 0
	end

	-- AcquireBestTarget 0x431c40: the best enemy in reach of unit n, or 0.
	function G:acquire_target(n)
		local u = self.u
		local range = self:seek_range(n)
		if band(u.status[n], M.STATUS_IN_BUILDING) ~= 0 then
			range = range + 2
		elseif u.ai[n] ~= 0 then
			local sight = self.dat.units.sight_range[u.type[n]]
			if range < sight then range = sight end
		end
		return scan(self, n, range * 32, minimum_range(self, n))
	end

	-- AcquireRandomTarget 0x431b70 (Hold Position): any enemy within the seek range, no
	-- minimum range.
	function G:acquire_random_target(n)
		local range = self:seek_range(n) * 32
		if band(self.u.status[n], M.STATUS_IN_BUILDING) ~= 0 then range = range + 0x40 end
		return scan(self, n, range, 0)
	end
end

return M
