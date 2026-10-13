-- presentation/health_bar.lua against the original DrawHealthBar 0x464d20 (golden from
-- tools/research/probe_native_health_bar_117.py; needs generated/data). Every unit type with
-- a health bar is drawn at the probe's hit point, shield, energy and timer levels, owned by
-- the local player or not, with the whole frame and cropped frames, on the same baseline
-- bitmap; the bitmap hashes fold per unit type.
local health_bar = require("presentation.health_bar")
local selection = require("game.images.selection")
local dat = require("game.data.dat")
local palettes = require("game.data.palettes")
local trace = require("tests.lua.iscript_trace")
local native_only = require("tests.lua.native_only")
local golden = require("tests.golden.oracle.health_bar.cases")

local DATA = "generated/data/"
-- Scenario: keep equal to the probe.
local HP_LEVELS = { 1, 33, 34, 50, 66, 67, 100 }
local SHIELD_LEVELS = { 0, 50, 100 }
local ENERGY_LEVELS = { 0, 40, 100 }
local CROPS = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 4, 0 }, { 0, 0, -5 }, { 0, 1 }, { 0, 0, nil, -6 } }
local FIXED = health_bar.FIXED

local S = {}

local floor = math.floor

local function states(t, units, max_energy, L)
	local hp, sh = units.hit_points[t], units.shield_amount[t]
	local out = {}
	for _, pct in ipairs(HP_LEVELS) do
		out[#out + 1] = { math.max(1, floor(hp * pct / 100)), sh * FIXED, max_energy, 0, 0, L }
	end
	if units.shield_enabled[t] ~= 0 then
		for _, pct in ipairs(SHIELD_LEVELS) do
			out[#out + 1] = { floor(hp / 2), floor(sh * pct / 100) * FIXED, 0, 0, 0, L }
		end
	end
	if units.special_ability_flags[t] % (2 * selection.SPELLCASTER) >= selection.SPELLCASTER then
		for _, pct in ipairs(ENERGY_LEVELS) do
			for _, owner in ipairs({ L, L + 1 }) do
				out[#out + 1] = { hp, 0, floor(max_energy * pct / 100), 0, 0, owner }
			end
		end
	end
	out[#out + 1] = { hp, 0, 0, floor(health_bar.TIMER_HALLUCINATION / 3), selection.HALLUCINATION, L }
	if t == selection.BROODLING then out[#out + 1] = { hp, 0, 0, floor(health_bar.TIMER_BROODLING / 2), 0, L } end
	return out
end

local function canvas(g, thpbar)
	local c = { px = {}, thpbar = {} }
	for k = 0, #thpbar - 1 do c.thpbar[k] = thpbar:byte(k + 1) end
	function c:reset()
		for i = 0, g.width * g.height - 1 do self.px[i] = i * g.baseline_mul % g.baseline_mod end
	end
	function c:hline(x, y, n, color)
		for k = 0, n - 1 do self.px[y * g.width + x + k] = color end
	end
	function c:vline(x, y, n, color)
		for k = 0, n - 1 do self.px[(y + k) * g.width + x] = color end
	end
	function c:hash()
		local h, px, fold = 0, self.px, trace.fold
		for i = 0, g.width * g.height - 1 do h = fold(h, px[i]) end
		return h
	end
	return c
end

function S.bars_equal_original(T)
	if not T.has(DATA .. "dat.kpd") then return end
	local d = dat.parse(T.read(DATA .. "dat.kpd"), "dat.kpd")
	local pal = palettes.parse(T.read(DATA .. "palettes.kpd"), "palettes.kpd")
	local g = golden
	local c = canvas(g, pal.thpbar)
	local bad = {}
	for _, row in ipairs(g.types) do
		local t, max_energy, count, expected = row[1], row[2], row[3], row[4]
		local sprite_id = d.flingy.sprite[d.units.graphics[t]]
		local width = d.sprites.health_bar[sprite_id]
		local height = d.units.shield_enabled[t] ~= 0 and 13 or 11
		local h, n = 0, 0
		for _, st in ipairs(states(t, d.units, max_energy, g.local_player)) do
			local u = { type = t, hp = st[1], shields = st[2], energy = st[3], timer = st[4], status = st[5], owner = st[6] }
			-- The original reads the energy's high byte (+0xa3).
			local bar = health_bar.from_unit(u, d, sprite_id, g.local_player, max_energy)
			for _, cr in ipairs(CROPS) do
				local left, top = cr[1], cr[2]
				local w = cr[3] and width + cr[3] or width - left
				local hh = cr[4] and height + cr[4] or height - top
				if w > 0 and hh > 0 then
					c:reset()
					health_bar.draw(c, g.x, g.y, { left = left, top = top, w = w, h = hh }, bar)
					h, n = trace.fold(h, c:hash()), n + 1
				end
			end
		end
		if n ~= count or h ~= expected then bad[#bad + 1] = t end
	end
	T.eq(#bad, 0, "unit types differing from the original: " .. table.concat(bad, ","))
end

return native_only.guard(S, "test_health_bar")
