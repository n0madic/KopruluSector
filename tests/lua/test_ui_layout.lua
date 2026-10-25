-- The interface's layout engine (presentation/ui/layout.lua, ADR 0011) on the aspect ratios and resolutions of plan
-- §7 phase 9 and docs/phase9.md: the logical screen holds the original's 640 x 480, every panel lies inside it, the
-- panels do not overlap, and the console groups and the fillers between them leave no seam.
local layout = require("presentation.ui.layout")

local S = {}

-- The original's dialog rectangles of the panels the layouts place (a Protoss console, rez\*.bin as loaded).
local PANELS = {
	["rez\\minimap.bin"] = { 0, 315, 137, 479 },
	["rez\\statdata.bin"] = { 138, 388, 407, 479 },
	["rez\\statport.bin"] = { 408, 408, 495, 479 },
	["rez\\stat_f10.bin"] = { 408, 388, 495, 407 },
	["rez\\statbtnp.bin"] = { 496, 354, 639, 479 },
	["rez\\statres.bin"] = { 220, 0, 639, 19 },
	["rez\\statlb.bin"] = { 0, 0, 199, 111 },
	["rez\\ptextbox.bin"] = { 190, 338, 447, 355 },
}

-- 4:3, 16:10, 16:9, 19.5:9, 21:9, 32:9, 9:16, 3:4 from small to 4K and 5120 x 1440
local WINDOWS = {
	{ 1024, 768 }, { 1600, 1200 }, { 1280, 800 }, { 1920, 1200 }, { 1280, 720 }, { 1366, 768 }, { 1920, 1080 },
	{ 2560, 1440 }, { 3840, 2160 }, { 2340, 1080 }, { 2560, 1080 }, { 3440, 1440 }, { 5120, 1440 }, { 3840, 1080 },
	{ 1080, 1920 }, { 720, 1280 }, { 768, 1024 }, { 1536, 2048 }, { 640, 480 }, { 800, 600 },
}

local function overlap(a, b)
	return not (a[3] < b[1] or b[3] < a[1] or a[4] < b[2] or b[4] < a[2])
end

function S.layouts_fit_every_screen(T)
	for _, win in ipairs(WINDOWS) do
		for _, mode in ipairs({ "integer", "sharp", "auto" }) do
			local m, scale, w, h = layout.screen(win[1], win[2], mode)
			local where = ("%dx%d %s"):format(win[1], win[2], m)
			assert(w >= layout.MIN_W and h >= layout.MIN_H, where .. ": the logical screen is smaller than 640 x 480")
			assert(scale > 0 and w * scale <= win[1] + scale and h * scale <= win[2] + scale, where .. ": scale")
			for _, name in ipairs({ "classic", "wide", false }) do
				local a = layout.arrange(w, h, name or nil)
				local placed = {}
				for bin, r in pairs(PANELS) do
					local x, y = layout.place(a, bin, r)
					local rect = { x, y, x + r[3] - r[1], y + r[4] - r[2] }
					assert(rect[1] >= 0 and rect[2] >= 0 and rect[3] < w and rect[4] < h,
						("%s %s: %s at %d,%d outside %dx%d"):format(where, a.name, bin, x, y, w, h))
					for other, o in pairs(placed) do
						if bin ~= "rez\\ptextbox.bin" and other ~= "rez\\ptextbox.bin" then -- the text line lies over the console
							assert(not overlap(rect, o), ("%s %s: %s overlaps %s"):format(where, a.name, bin, other))
						end
					end
					placed[bin] = rect
				end
				-- the text line, a backdrop dialog under the console art, never lies under a filler's opaque column
				local tb = placed["rez\\ptextbox.bin"]
				local filler_top = a.dy + require("presentation.ui.layouts").console.tops.left
				for _, f in ipairs(a.fillers) do
					if tb[1] < f.x + f.w and tb[3] >= f.x then
						assert(tb[4] < filler_top, ("%s %s: the text line under a filler"):format(where, a.name))
					end
				end
				-- the console groups and fillers: one span without a gap, inside the screen
				local spans = {}
				for _, g in pairs(a.groups) do spans[#spans + 1] = { g.left, g.right } end
				for _, f in ipairs(a.fillers) do spans[#spans + 1] = { f.x, f.x + f.w } end
				table.sort(spans, function(p, q) return p[1] < q[1] end)
				for k = 2, #spans do
					T.eq(spans[k][1], spans[k - 1][2])
				end
				assert(spans[1][1] >= 0 and spans[#spans][2] <= w, where .. ": the console leaves the screen")
			end
		end
	end
end

-- A game on a portrait window narrows its logical screen (layout.minimum) to the narrow layout: the console in two rows
-- (the minimap above the status panel, portrait and command card), every panel inside the screen and none overlapping,
-- the second row on the first's art; a fixed screen (glue) keeps the original's 640 x 480.
function S.narrow_layout_fits_portrait_screens(T)
	local narrowed = 0
	for _, win in ipairs(WINDOWS) do
		for _, mode in ipairs({ "integer", "sharp", "auto" }) do
			local min_w, min_h = layout.minimum(win[1], win[2], false)
			local m, _, w, h = layout.screen(win[1], win[2], mode, min_w, min_h)
			local where = ("%dx%d %s"):format(win[1], win[2], m)
			assert(w >= min_w and h >= min_h, where .. ": the logical screen is smaller than its minimum")
			local fw, fh = select(3, layout.screen(win[1], win[2], mode, layout.minimum(win[1], win[2], true)))
			assert(fw >= layout.MIN_W and fh >= layout.MIN_H, where .. ": a fixed screen's logical screen")
			local a = layout.arrange(w, h)
			if a.name == "narrow" then
				narrowed = narrowed + 1
				local placed = {}
				for bin, r in pairs(PANELS) do
					local x, y = layout.place(a, bin, r)
					local rect = { x, y, x + r[3] - r[1], y + r[4] - r[2] }
					assert(rect[1] >= 0 and rect[2] >= 0 and rect[3] < w and rect[4] < h,
						("%s narrow: %s at %d,%d outside %dx%d"):format(where, bin, x, y, w, h))
					for other, o in pairs(placed) do
						assert(not overlap(rect, o), ("%s narrow: %s overlaps %s"):format(where, bin, other))
					end
					placed[bin] = rect
				end
				T.eq(a.groups.right.left, 0)
				T.eq(a.groups.left.left, 0)
				T.eq(#a.fillers, 0)
				assert(a.groups.right.right <= w, where .. ": the console leaves the screen")
			end
		end
	end
	assert(narrowed > 0, "no portrait window narrowed")
end

function S.wide_layout_pins_the_groups(T)
	local a = layout.arrange(1920, 480, "wide")
	T.eq(a.groups.left.left, 0)
	T.eq(a.groups.right.right, 1920)
	T.eq(#a.fillers, 1)
	T.eq(a.fillers[1].x, 138)
	local c = layout.arrange(640, 480, "classic")
	T.eq(#c.fillers, 0)
	local x, y = layout.place(c, "rez\\stat_f10.bin", PANELS["rez\\stat_f10.bin"])
	T.eq(x, 408)
	T.eq(y, 388) -- the classic layout of a 640 x 480 screen is the original's
end

return S
