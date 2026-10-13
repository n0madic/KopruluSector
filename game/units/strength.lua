-- Unit strength for the AI's force estimates (statwire.cpp: InitTypeStrengthTables 0x4a98e0,
-- GetUnitCurrentStrength 0x4a96b0). The per-type air/ground tables (0x6ac530, 0x6ac8c0) are
-- built once per game; every completed unit caches its current values at +0x138/+0x13a.
-- Methods of the game.
local int = require("game.simulation.int")

local band = int.band
local bit_band = int.bit_band

local M = {}

M.TYPES = 228
M.NO_WEAPON = 0x82
M.NO_SUBUNIT = 0xe4
M.AIR, M.GROUND = 0, 1
M.BUNKER, M.MARINE = 0x7d, 0
M.SPELLCASTER = 0x200000
M.HALLUCINATION = 0x40000000
local NO_STRENGTH = { [0x23] = true, [0x24] = true, [0x3b] = true, [0x61] = true } -- larva, eggs, cocoon
local WEAPON_UNIT = { [0x48] = 0x49, [0x52] = 0x49, [0x53] = 0x55, [0x51] = 0x55 } -- carriers, reavers
local STRENGTH_SCALE, STRENGTH_DIVISOR = 758, 100

-- ComputeTypeStrength 0x4a9000: sqrt of a damage-rate measure, scaled; 0 for no life.
local function type_strength(d, t, w)
	local units, weapons = d.units, d.weapons
	local shields = units.shield_enabled[t] ~= 0 and units.shield_amount[t] or 0
	local life = int.sar(units.hit_points[t], 8) + shields
	if life == 0 then return 0 end
	local damage, cooldown, factor = weapons.damage_amount[w], weapons.weapon_cooldown[w], weapons.damage_factor[w]
	local rate = int.mul(int.mul(int.udiv(weapons.maximum_range[w], cooldown), factor), damage)
	local burst = 0
	if w ~= M.NO_WEAPON then burst = int.idiv(int.s32(int.shl(int.mul(damage, factor), 11)), cooldown) end
	local total = int.add(int.shr(int.mul(burst, life), 8), rate)
	return math.floor(math.sqrt(total) * STRENGTH_SCALE / STRENGTH_DIVISOR) % 4294967296
end

-- ApplyStrengthTypeModifier 0x4a8f60.
local function type_modifier(t, v)
	if t == 7 or t == 0x29 or t == 0x40 then return int.shr(v, 2) end
	if t == 0xd or t == 0x49 or t == 0x55 then return 0 end
	if t == 0x20 or t == 0x2b or t == 0x41 then return (v * 2) % 4294967296 end
	if t == 0x2f or t == 0x32 then return int.shr(v, 4) end
	if t == 0x53 then return int.udiv(v, 10) end
	return v
end

local function weapon_type_of(d, t)
	if WEAPON_UNIT[t] then return WEAPON_UNIT[t] end
	local sub = d.units.subunit1[t]
	return sub == M.NO_SUBUNIT and t or sub
end

function M.install(G)
	-- InitTypeStrengthTables 0x4a98e0: a type without the weapon kind gets 1, reduced to 0
	-- when it has the other kind with a strength above 1.
	function G:strength_init()
		local d = self.dat
		local air, ground = {}, {}
		for t = 0, M.TYPES - 1 do
			local a, g = 0, 0
			if not NO_STRENGTH[t] then
				local wt = weapon_type_of(d, t)
				local w = d.units.air_weapon[wt]
				a = w == M.NO_WEAPON and 1 or type_modifier(t, type_strength(d, t, w))
				w = d.units.ground_weapon[wt]
				g = w == M.NO_WEAPON and 1 or type_modifier(t, type_strength(d, t, w))
			end
			air[t] = (a == 1 and g > 1) and 0 or a
			ground[t] = (g == 1 and a > 1) and 0 or g
		end
		self.strength = { [M.AIR] = air, [M.GROUND] = ground }
	end

	-- GetUnitTypeStrength 0x4a9630.
	function G:unit_type_strength(n, kind)
		local u, d = self.u, self.dat.units
		local t = u.type[n]
		local v = self.strength[kind][t]
		if t == M.BUNKER then
			-- 0x438350: the space taken by the loaded units, in Marines.
			local space = 0
			for k = 0, 7 do
				local c = self:unit_from_ref(u["loaded" .. k][n])
				if c ~= 0 then space = space + d.space_required[u.type[c]] end
			end
			v = int.mul(space, self.strength[kind][M.MARINE])
		end
		if bit_band(d.special_ability_flags[t], M.SPELLCASTER) ~= 0 and bit_band(u.status[n], M.HALLUCINATION) == 0 then
			return int.shr(int.shr(u.energy[n], 8), 1) + v
		end
		return v
	end

	-- GetCurrentLife 0x41faa0 and GetMaxLife 0x4200e0.
	function G:current_life(n)
		local u, d = self.u, self.dat.units
		local life = int.sar(u.hp[n] + 0xff, 8)
		if d.shield_enabled[u.type[n]] ~= 0 then life = life + int.sar(u.shields[n], 8) end
		return life
	end

	-- GetUnitMaxHitPoints 0x409c90.
	function G:max_hit_points(n)
		local u, d = self.u, self.dat.units
		local v = int.sar(d.hit_points[u.type[n]], 8)
		if v == 0 then
			local cur = int.sar(u.hp[n] + 0xff, 8)
			v = cur ~= 0 and cur or 1
		end
		return v
	end

	function G:max_life(n)
		local d = self.dat.units
		local t = self.u.type[n]
		return self:max_hit_points(n) + (d.shield_enabled[t] ~= 0 and d.shield_amount[t] or 0)
	end

	-- GetUnitCurrentStrength 0x4a96b0.
	function G:unit_current_strength(n, kind)
		local u, d = self.u, self.dat.units
		local t = u.type[n]
		if NO_STRENGTH[t] then return 0 end
		if bit_band(u.status[n], M.HALLUCINATION) ~= 0 and self:current_life(n) < self:max_life(n) then return 0 end
		local s = self:unit_type_strength(n, kind)
		local life = self:current_life(n)
		local shields = d.shield_enabled[t] ~= 0 and d.shield_amount[t] or 0
		local max = self:max_hit_points(n) + shields
		if max == 0 then max = 1 end
		return int.udiv(int.mul(life, s), max)
	end

	-- RefreshUnitStrength 0x4a8f30.
	function G:refresh_unit_strength(n)
		local u = self.u
		u.air_strength[n] = self:unit_current_strength(n, M.AIR) % 65536
		u.ground_strength[n] = self:unit_current_strength(n, M.GROUND) % 65536
	end
end

return M
