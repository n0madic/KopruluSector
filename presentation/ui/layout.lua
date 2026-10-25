-- The interface's layout engine (ADR 0011, plan §2.1): the logical screen of a window and where each panel goes.
--
-- The logical screen is the index target the world and the panels are drawn into: at least 640 x 480 logical pixels
-- (the original's screen), so the console always fits; one logical pixel is `scale` window pixels. "integer" mode
-- keeps a whole scale (nearest), "sharp" a fractional one with the sharp-bilinear upscale, "auto" the first from a
-- 2x scale up. Positions are in logical pixels with the origin at the top-left; panels keep their own (dialog)
-- coordinates inside them, so input maps back to the original's coordinates by subtracting a panel's offset.
local layouts = require("presentation.ui.layouts")

local M = {}

M.MIN_W, M.MIN_H = 640, 480

-- The smallest logical screen of a window: the original's 640 x 480, which a fixed screen (a glue screen, `fixed`)
-- always needs; a game on a portrait window may narrow to the narrow layout's width (its console in two rows).
function M.minimum(win_w, win_h, fixed)
	if not fixed and win_h > win_w then return layouts.layouts.narrow.min_w, M.MIN_H end
	return M.MIN_W, M.MIN_H
end

-- The logical screen of a window: mode, scale (window pixels per logical pixel), width and height; it holds at least
-- min_w x min_h (default 640 x 480).
function M.screen(win_w, win_h, mode, min_w, min_h)
	win_w, win_h = math.max(1, win_w), math.max(1, win_h)
	min_w, min_h = min_w or M.MIN_W, min_h or M.MIN_H
	local fit = math.min(win_w / min_w, win_h / min_h)
	if mode == nil or mode == "auto" then mode = fit >= 2 and "integer" or "sharp" end
	if mode == "integer" then
		local scale = math.max(1, math.floor(fit))
		return mode, scale, math.ceil(win_w / scale), math.ceil(win_h / scale)
	end
	if win_w / win_h >= min_w / min_h then
		return mode, win_h / min_h, math.ceil(min_h * win_w / win_h), min_h
	end
	return mode, win_w / min_w, min_w, math.ceil(min_w * win_h / win_w)
end

-- The x offset of a console group on a logical screen w wide.
local function group_dx(g, place, w)
	local cw = layouts.console.width
	if place == "left" or place == "above" then return -g.x0 end
	if place == "right" then return w - g.x1 end
	return math.floor((w - cw) / 2)
end

-- How far a group placed "above" rises: onto the other groups' art top.
local function raise() return layouts.console.height - layouts.console.tops.right end

-- The arrangement of a w x h logical screen: { name =, groups = { [name] = { x =, y =, x0 =, x1 = } } (where each
-- console group's art is drawn: the console column x0 at screen x + x0), fillers = { { x =, y =, w =, h =, column = } },
-- dy = the console's top on the screen }. `name` forces a layout.
function M.arrange(w, h, name)
	name = name or layouts.choose(w, h)
	local def = layouts.layouts[name] or error("layout: no layout " .. tostring(name), 2)
	local dy = h - layouts.console.height
	local out = { name = name, def = def, groups = {}, fillers = {}, dy = dy, w = w, h = h, top = dy }
	local previous
	for _, g in ipairs(layouts.console.groups) do
		local place = def.groups[g.name] or "centre"
		local dx = group_dx(g, place, w)
		local gy = place == "above" and dy - raise() or dy
		local placed = { x = dx, y = gy, x0 = g.x0, x1 = g.x1, left = dx + g.x0, right = dx + g.x1, above = place == "above" }
		out.groups[g.name] = placed
		local art_top = gy + (layouts.console.tops[g.name] or 0)
		if g.name == layouts.console.groups[1].name or art_top < out.top then out.top = art_top end
		if place == "above" then
			-- a second row: no filler joins it to the row below
		elseif previous and not previous.above and placed.left > previous.right then
			out.fillers[#out.fillers + 1] = { x = previous.right, y = dy, w = placed.left - previous.right,
				h = layouts.console.height, column = layouts.console.filler_column }
		end
		if place ~= "above" then previous = placed end
	end
	return out
end

-- Where a panel goes: its top-left on the screen for a dialog of `bin` whose original rectangle is rect
-- ({left, top, right, bottom} on the original's 640 x 480 screen); nil for a dialog the layouts do not place.
local CENTRED = {}
local ORIGINAL_TOP -- the original's console art top (classic, 640 x 480)
for _, bin in ipairs(layouts.centred) do CENTRED[bin:lower()] = true end

function M.place(arrangement, bin, rect)
	local own = arrangement.def and arrangement.def.panels
	local p = own and own[bin] or layouts.panels[bin]
	local w = rect[3] - rect[1] + 1
	if not p and CENTRED[bin:lower()] then
		if arrangement.w < M.MIN_W then -- narrower than the original's screen: the dialog itself centred
			return math.floor((arrangement.w - w) / 2), math.floor((arrangement.h - M.MIN_H) / 2) + rect[2]
		end
		return math.floor((arrangement.w - M.MIN_W) / 2) + rect[1], math.floor((arrangement.h - M.MIN_H) / 2) + rect[2]
	end
	if not p then return nil end
	if p.group then
		local g = arrangement.groups[p.group] or error("layout: no group " .. p.group, 2)
		return rect[1] + g.x, rect[2] + g.y
	end
	if p.anchor == "top_left" then
		local y = rect[2] + (p.dy or 0)
		-- a console whose art rises higher than the original's over the panel (a short narrow screen) lifts it as much
		if p.lift then
			ORIGINAL_TOP = ORIGINAL_TOP or M.arrange(M.MIN_W, M.MIN_H, "classic").top
			y = y + math.min(0, arrangement.top - ORIGINAL_TOP)
		end
		return rect[1], y
	end
	if p.anchor == "top_right" then return arrangement.w - (M.MIN_W - rect[1]), rect[2] + (p.dy or 0) end
	if p.anchor == "bottom_centre" then
		local x = math.floor((arrangement.w - w) / 2) + rect[1] - math.floor((M.MIN_W - w) / 2)
		if arrangement.w < M.MIN_W then x = math.floor((arrangement.w - w) / 2) end
		-- over the console's top as the original's, or above a second row of the console; a backdrop dialog lies under
		-- the console art, so over a filler (whose column is opaque from the left group's art top) it rises above it
		local y = rect[2] + arrangement.dy
		local h = rect[4] - rect[2] + 1
		local filler_top = arrangement.dy + layouts.console.tops.left
		if y + h > arrangement.top and arrangement.top < filler_top then
			y = arrangement.top - h
		end
		for _, f in ipairs(arrangement.fillers) do
			if x < f.x + f.w and x + w > f.x and y + h > filler_top then y = filler_top - h end
		end
		return x, y
	end
	error("layout: unknown anchor " .. tostring(p.anchor), 2)
end

-- A screen point in window pixels (top-left origin) as a logical point.
function M.to_logical(scale, wx, wy)
	return math.floor(wx / scale), math.floor(wy / scale)
end

return M
