-- Draw-effect scene of the viewer autotest (pure Lua): puts every unit sprite meeting a
-- view under one of the runtime draw effects that no images.dat entry carries, so the
-- composition check (tools/viewer_compose_check.py) covers their kernels on real frames.
-- The k-th unit sprite in the view (by unit order) gets EFFECTS[k % #EFFECTS + 1] on its
-- normally drawn images (method 0: the cloak consumers leave shadows and other methods
-- alone); every SELECTION_EVERY-th one also gets a selection circle underlay. The cloak
-- effects then progress through the original callbacks (0x40e570/0x40e5f0) as the scene
-- ticks.
local core = require("game.images.core")
local image = require("game.images.image")

local NONE = core.NONE

local M = {}

M.CLOAK, M.DECLOAK, M.CLOAK_BLEND, M.DECLOAK_BLEND, M.EMP, M.WARP, M.OUTLINE = 1, 2, 3, 4, 5, 6, 7
M.EFFECTS = { M.CLOAK, M.DECLOAK, M.CLOAK_BLEND, M.DECLOAK_BLEND, M.EMP, M.WARP, M.OUTLINE }
M.METHOD_CLOAK_BLEND, M.METHOD_EMP, M.METHOD_WARP, M.METHOD_OUTLINE = 5, 8, 12, 15
M.WARP_FRAMES = 21 -- frames of the warp texture GRP
M.WARP_MAX_SIZE = 192 -- the texture frame's size: larger frames would read native scratch
M.SELECTION_EVERY = 5
M.SELECTION_FIRST, M.SELECTION_SIZES = 561, 10 -- selection circle images 561..570 (method 13)
M.SELECTION_ROWS = 3 -- tselect rows
M.OUTLINE_COLOR = 0xb9 -- the line colour the viewer gives method 15 (a consumer's global)

local function set_low_word(img, i, word)
	local p = img.param[i]
	img.param[i] = p - p % 65536 + word
end

local function apply_image(w, i, effect, k)
	local img = w.img
	if effect == M.CLOAK then
		w:image_begin_cloak(i)
	elseif effect == M.DECLOAK then
		w:image_begin_decloak(i)
	elseif effect == M.CLOAK_BLEND then
		img.method[i] = M.METHOD_CLOAK_BLEND
		set_low_word(img, i, image.CLOAK_START)
	elseif effect == M.DECLOAK_BLEND then
		img.method[i] = image.METHOD_DECLOAK_BLEND
		set_low_word(img, i, image.DECLOAK_START)
	elseif effect == M.EMP then
		img.method[i] = M.METHOD_EMP
	elseif effect == M.WARP then
		local g = img.grp[i]
		if g > 0 and img.frame[i] < w.grp.grps[g].frames then
			local _, _, fw, fh = w:frame_rect(i, img.frame[i])
			if fw <= M.WARP_MAX_SIZE and fh <= M.WARP_MAX_SIZE then
				img.method[i] = M.METHOD_WARP
				img.param[i] = k % M.WARP_FRAMES
			end
		end
	elseif effect == M.OUTLINE then
		img.method[i] = M.METHOD_OUTLINE
	end
end

local function apply(w, s, effect, k)
	local img = w.img
	local i = w.spr.images[s].head
	while i ~= NONE do
		if img.method[i] == 0 then apply_image(w, i, effect, k) end
		i = img.next[i]
	end
end

-- scene: viewer/map_world.lua scene; view: { left, top, right, bottom } in map pixels;
-- warp: true when the warp texture's atlas is loaded. Returns the sprites changed.
function M.apply(scene, view, warp)
	local w = scene.world
	local spr = w.spr
	local k = 0
	for token = 1, #scene.units do
		local s = scene.units[token].sprite
		if s ~= NONE and spr.x[s] >= view.left and spr.x[s] < view.right and spr.y[s] >= view.top
			and spr.y[s] < view.bottom then
			local effect = M.EFFECTS[k % #M.EFFECTS + 1]
			if effect ~= M.WARP or warp then
				apply(w, s, effect, k)
			end
			if k % M.SELECTION_EVERY == 0 then
				local main = spr.main[s]
				local circle = w:image_create_relative(main, M.SELECTION_FIRST + k % M.SELECTION_SIZES, 0, 0, false)
				if circle ~= NONE then
					w.img.param[circle] = k % M.SELECTION_ROWS
				end
			end
			k = k + 1
		end
	end
	return k
end

return M
