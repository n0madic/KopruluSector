-- Health, shield, energy and timed-life bars (draw method 11, DrawHealthBar 0x464d20,
-- docs/research/native-health-bars.md), pure Lua: the original's segment arithmetic and line
-- drawing, checked against the original code by tests/lua/test_health_bar.lua (golden from
-- tools/research/probe_native_health_bar_117.py).
--
-- The bar is the frame of a health-bar image: `width` columns (sprites.dat health bar),
-- 5 hit point rows (7 with shields, the shields over the top 4), and the energy or timer
-- bar 6 (8) rows below. Colours index thpbar.pcx (0x66cb5c, 19 entries). The original
-- draws a part only when the visible crop holds all its rows.
local M = {}

M.HP_ROWS, M.SHIELD_HP_ROWS, M.SHIELD_ROWS = 5, 7, 4
M.ENERGY_OFFSET, M.SHIELD_ENERGY_OFFSET = 6, 8 -- rows from the bar top to the energy bar
M.SEGMENT = 3 -- columns per segment; fills round to it
M.MIN_FILL = 3
M.SEPARATOR = 0x12 -- thpbar entry of the segment separators (0x66cb6e)
M.FULL = 100 -- percent of a zero maximum
M.HALLUCINATION = 0x40000000 -- unit status bit
M.SPELLCASTER = 0x200000 -- units.dat special ability flag
M.BROODLING = 40
M.TIMER_HALLUCINATION, M.TIMER_BROODLING = 1350, 1800 -- 0x434a90 timer maximums
M.FIXED = 256 -- hit points and shields are 8.8 fixed point

-- Colour entries: { threshold percent, thpbar entry per row }; the first entry whose
-- threshold the percentage reaches applies (0x5085d0, 0x5085f4, 0x5085e4).
M.HP_COLORS = { { 66, { 0x12, 0x00, 0x01, 0x02, 0x12 } }, { 33, { 0x12, 0x03, 0x04, 0x05, 0x12 } },
	{ 0, { 0x12, 0x06, 0x07, 0x08, 0x12 } } }
M.SHIELD_HP_COLORS = { { 66, { 0x12, 0x00, 0x00, 0x01, 0x01, 0x02, 0x12 } },
	{ 33, { 0x12, 0x03, 0x03, 0x04, 0x04, 0x05, 0x12 } }, { 0, { 0x12, 0x06, 0x06, 0x07, 0x07, 0x08, 0x12 } } }
M.ENERGY_COLORS = { { 0, { 0x12, 0x0c, 0x0d, 0x0e, 0x12 } } }
M.EMPTY = { 0x12, 0x0f, 0x10, 0x11, 0x12 } -- 0x5085ec
M.SHIELD_HP_EMPTY = { 0x12, 0x0f, 0x0f, 0x10, 0x10, 0x11, 0x12 } -- 0x50860c
M.SHIELD_FILL = { 0x12, 0x0a, 0x0b, 0x12 } -- 0x508614
M.SHIELD_EMPTY = { 0x12, 0x10, 0x11, 0x12 } -- 0x508618

local floor = math.floor

-- C division of two integers (toward zero).
local function idiv(a, b)
	local q = a / b
	return q >= 0 and floor(q) or -floor(-q)
end

-- 0x4648a0 (span = width - 1) / 0x464920 (span = width): percentage and filled columns,
-- at least MIN_FILL and rounded to a segment (remainder 1 down, 2 up).
local function fill(max, cur, span)
	local pct = max == 0 and M.FULL or idiv(cur * 100, max)
	local n = idiv(pct * span, 100)
	if n < M.MIN_FILL then n = M.MIN_FILL end
	local rem = n % M.SEGMENT
	if rem == 1 then n = n - 1 elseif rem == 2 then n = n + 1 end
	return pct, n
end
M.fill = fill

local function colors(entries, pct)
	for _, e in ipairs(entries) do
		if pct >= e[1] then return e[2] end
	end
	error("health_bar: percentage below every threshold", 2)
end

-- Rows of a fill bar (0x4649a0 with 5 rows, 0x464ab0 with 7): the filled columns from the
-- frame's left, the rest empty, then the separators every SEGMENT frame columns. x, y:
-- the visible crop's top-left on the canvas.
local function bar_rows(c, x, y, crop, filled, row_colors, empty, rows)
	if crop.h ~= rows then return end
	local f = filled - crop.left
	if f > crop.w then f = crop.w elseif f < 0 then f = 0 end
	local e = crop.w - f
	for r = 0, rows - 1 do
		if f > 0 then c:hline(x, y + r, f, c.thpbar[row_colors[r + 1]]) end
		if e > 0 then c:hline(x + f, y + r, e, c.thpbar[empty[r + 1]]) end
	end
	local sx = x
	local rem = crop.left % M.SEGMENT
	if crop.left ~= 0 and rem ~= 0 then sx = x - rem + M.SEGMENT end
	while sx < x + crop.w do
		c:vline(sx, y, rows, c.thpbar[M.SEPARATOR])
		sx = sx + M.SEGMENT
	end
end

-- 0x464bc0: the shield rows over the top of the hit points; with the frame's left column
-- visible the left end slants (rows start 0, 2, 3, 5 columns in) and the slant pixels take
-- the separator colour.
local function shield_rows(c, x, y, crop, filled)
	if crop.h ~= M.SHIELD_HP_ROWS then return end
	local f = filled - crop.left
	if f > crop.w then f = crop.w elseif f < 0 then f = 0 end
	local e = crop.w - f
	local fx, ex = x, x + f
	for r = 0, M.SHIELD_ROWS - 1 do
		if f > 0 then c:hline(fx, y + r, f, c.thpbar[M.SHIELD_FILL[r + 1]]) end
		if e > 0 then c:hline(ex, y + r, e, c.thpbar[M.SHIELD_EMPTY[r + 1]]) end
		if crop.left == 0 then
			local d = r ~= 1 and 2 or 1
			if f ~= 0 then
				f, fx = f - d, fx + d
			elseif e ~= 0 then
				e, ex = e - d, ex + d
			end
		end
	end
	local sep = c.thpbar[M.SEPARATOR]
	local sx = x
	local rem = crop.left % M.SEGMENT
	if crop.left ~= 0 and rem ~= 0 then sx = x - rem + M.SEGMENT end
	sx = sx + M.SEGMENT
	local h = math.min(crop.h, M.SHIELD_ROWS)
	while sx < x + crop.w do
		c:vline(sx, y, h, sep)
		sx = sx + M.SEGMENT
	end
	if crop.left == 0 then
		local px = x
		for r = 0, M.SHIELD_ROWS - 1 do
			c:hline(px, y + r, 2, sep)
			px = px + (r ~= 1 and 2 or 1)
		end
	end
end

-- Bar values of a unit as 0x464d20 reads them: u = { type =, hp =, shields = (8.8 fixed),
-- energy = (8.8), timer =, status =, owner = }; dat: game.data.dat; sprite_id: the unit's
-- sprite; local_player; max_energy: the unit's energy maximum (8.8; upgrades decide it,
-- 0x434940). Returns { width =, shields =, hp =, hp_max =, sh =, sh_max =, energy = nil
-- or { cur, max } }.
function M.from_unit(u, dat, sprite_id, local_player, max_energy)
	local units = dat.units
	local t = u.type
	local hp_max = floor(units.hit_points[t] / M.FIXED)
	local hp = floor((u.hp + 0xff) / M.FIXED)
	if hp_max == 0 then hp_max = hp ~= 0 and hp or 1 end
	local bar = { width = dat.sprites.health_bar[sprite_id], shields = units.shield_enabled[t] ~= 0, hp = hp,
		hp_max = hp_max, sh = floor(u.shields / M.FIXED), sh_max = units.shield_amount[t] }
	local caster = units.special_ability_flags[t] % (2 * M.SPELLCASTER) >= M.SPELLCASTER
	local hallucination = u.status % (2 * M.HALLUCINATION) >= M.HALLUCINATION
	if ((caster and not hallucination) or hallucination or t == M.BROODLING) and u.owner == local_player then
		if caster and not hallucination then
			bar.energy = { floor(u.energy / M.FIXED), floor(max_energy / M.FIXED) }
		else
			local max = hallucination and M.TIMER_HALLUCINATION or (t == M.BROODLING and M.TIMER_BROODLING or 0)
			bar.energy = { u.timer, max }
		end
	end
	return bar
end

-- 0x464d20: draws `bar` on canvas c (c:hline(x, y, n, color), c:vline(x, y, n, color),
-- c.thpbar = { [entry] = palette index }) at x, y with the visible crop { left, top, w, h }
-- of the frame.
function M.draw(c, x, y, crop, bar)
	local local_crop = { left = crop.left, top = crop.top, w = crop.w, h = crop.h }
	local offset
	if not bar.shields then
		local_crop.h = math.min(crop.h, M.HP_ROWS)
		local pct, filled = fill(bar.hp_max, bar.hp, bar.width - 1)
		bar_rows(c, x, y, local_crop, filled, colors(M.HP_COLORS, pct), M.EMPTY, M.HP_ROWS)
		offset = M.ENERGY_OFFSET
	else
		local_crop.h = math.min(crop.h, M.SHIELD_HP_ROWS)
		local pct, filled = fill(bar.hp_max, bar.hp, bar.width - 1)
		local _, shield = fill(bar.sh_max, bar.sh, bar.width)
		bar_rows(c, x, y, local_crop, filled, colors(M.SHIELD_HP_COLORS, pct), M.SHIELD_HP_EMPTY, M.SHIELD_HP_ROWS)
		shield_rows(c, x, y, local_crop, shield)
		offset = M.SHIELD_ENERGY_OFFSET
	end
	if bar.energy then
		local_crop.top, local_crop.h = crop.top + offset, crop.h - offset
		if local_crop.h > 0 then
			local pct, filled = fill(bar.energy[2], bar.energy[1], bar.width)
			bar_rows(c, x, y + offset, local_crop, filled, colors(M.ENERGY_COLORS, pct), M.EMPTY, M.HP_ROWS)
		end
	end
end

return M
