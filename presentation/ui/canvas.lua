-- The interface's canvas (ADR 0011): kp_ui composes the console art, the fillers and the panels of a layout into one
-- canvas of the logical screen (kp_core.ui.canvas); in Defold the canvas's changed rectangle is uploaded into an
-- RGBA texture (palette index in R, opacity in A; Defold creates no two-channel texture at run time) that the render script writes into the index target (kp_ui_canvas).
local layout = require("presentation.ui.layout")

local M = {}

M.TEXTURE_PATH = "/kp/ui_canvas.texturec"
M.BLEND_PATH = "/kp/ui_blend.texturec"

local Canvas = {}
Canvas.__index = Canvas

-- core: the kp_core module; ui: a kp_core.ui.new interface.
function M.new(core, ui)
	return setmetatable({ core = core, ui = ui, canvas = core.ui.canvas(), w = 0, h = 0, fresh = true }, Canvas)
end

-- Composes a frame for a w x h logical screen: the game view's text lines, drag box and command marker, the console
-- groups and fillers of the layout, then the panels
-- ({ { dialog =, bin =, rect = { left, top, right, bottom } }, ... } in drawing order, each dialog painted; a panel
-- { lines = true, bin = "lines", rect = { 0, 0, 639, 479 } } is the interface's text lines) at the
-- places the layout gives them, and the tooltip box over them where its panel went. Returns the changed rectangle (x0, y0, x1, y1 exclusive) or nil, and the
-- arrangement (the panels' places for the input).
function Canvas:compose(w, h, panels, name)
	local c = self.canvas
	if w ~= self.w or h ~= self.h then
		c:resize(w, h)
		self.w, self.h, self.resized = w, h, true
	end
	local a = layout.arrange(w, h, name)
	a.places = {}
	c:begin()
	-- the game screen's own drawing after the sprites and the fog (its layer's callback 0x445ea0: the text lines, the
	-- drag box, the command marker) lies under everything else; the view's top-left is the logical screen's
	for _, p in ipairs(panels) do
		if p.lines then
			local x, y = layout.place(a, p.bin, p.rect)
			if x then c:lines(self.ui, x, y) end
		end
	end
	c:game_view(self.ui, 0, 0)
	-- a dialog composited onto the backdrop (BlitDialogToScreen 0x4c5630: the in-game menus) lies under the console
	for _, p in ipairs(panels) do
		if p.backdrop then
			local x, y = layout.place(a, p.bin, p.rect)
			if x then
				c:dialog(self.ui, p.dialog, x, y)
				a.places[p.dialog] = { x = x, y = y }
			end
		end
	end
	for _, g in pairs(a.groups) do c:console(self.ui, g.x, g.y, g.x0, g.x1) end
	for _, f in ipairs(a.fillers) do c:filler(self.ui, f.x, f.y, f.w, f.column) end
	for _, p in ipairs(panels) do
		local x, y = layout.place(a, p.bin, p.rect)
		if p.backdrop or p.lines then -- drawn above
		elseif x then
			c:dialog(self.ui, p.dialog, x, y)
			a.places[p.dialog] = { x = x, y = y }
		end
	end
	-- the tooltip over the panels: moved with the open dialog its anchor (the box's bottom left) lies on
	local _, tx, ty, tw, th = self.ui:tooltip()
	if tx then
		local ax, ay = tx, ty + th - 1
		for k = #panels, 1, -1 do -- the topmost first
			local p = panels[k]
			local place, r = p.dialog and a.places[p.dialog], p.rect
			if place and ax >= r[1] and ax <= r[3] and ay >= r[2] and ay <= r[4] then
				tx, ty = tx + place.x - r[1], ty + place.y - r[2]
				break
			end
		end
		c:tooltip(self.ui, math.max(0, math.min(w - tw, tx)), math.max(0, math.min(h - th, ty)))
	end
	local changed, x0, y0, x1, y1 = c:finish()
	if self.resized then
		self.resized = false
		return 0, 0, w, h, a
	end
	if changed then return x0, y0, x1, y1, a end
	return nil, nil, nil, nil, a
end

-- Composes a fixed screen (a glue screen: its dialog with the popups composited into it) at (x, y) of a w x h
-- logical screen; the margins widen its edges, dimmed (plan section 2a, kp_ui_canvas_margins), or are index 0 without
-- a dialog. Returns the changed rectangle (x0, y0, x1, y1 exclusive) or nil.
function Canvas:compose_screen(w, h, dialog, x, y)
	local c = self.canvas
	if w ~= self.w or h ~= self.h then
		c:resize(w, h)
		self.w, self.h, self.resized = w, h, true
	end
	c:begin()
	if dialog then
		c:margins(self.ui, dialog, x, y)
		c:dialog(self.ui, dialog, x, y)
	else
		c:fill(0, 0, w, h, 0)
	end
	local changed, x0, y0, x1, y1 = c:finish()
	if self.resized then
		self.resized = false
		return 0, 0, w, h
	end
	if changed then return x0, y0, x1, y1 end
	return nil
end

local function params(w, h, x, y)
	return { type = graphics.TEXTURE_TYPE_2D, width = w, height = h, format = graphics.TEXTURE_FORMAT_RGBA,
		x = x, y = y }
end

-- Defold: uploads the changed rectangle (from compose) and tells the render script (kp_ui_canvas).
function Canvas:upload(x0, y0, x1, y1)
	if x0 then
		local w, h = x1 - x0, y1 - y0
		local buf = buffer.create(w * h, { { name = hash("data"), type = buffer.VALUE_TYPE_UINT8, count = 4 } })
		self.core.copy_to_buffer(self.canvas:rect(x0, y0, x1, y1), buf)
		if x0 == 0 and y0 == 0 and w == self.w and h == self.h then
			if self.texture then resource.release(M.TEXTURE_PATH) end
			resource.create_texture(M.TEXTURE_PATH, params(w, h), buf)
			self.texture, self.fresh = true, true
		else
			resource.set_texture(M.TEXTURE_PATH, params(w, h, x0, y0), buf)
		end
	end
	-- the translucent texels over the world (the in-game dialogs' border colour): the render pass maps the world's
	-- index under each through the interface's blend row
	local blend
	if self.canvas.blends > 0 then
		local row = self.ui:blend_row()
		if row ~= self.blend_row then
			local buf = buffer.create(256, { { name = hash("data"), type = buffer.VALUE_TYPE_UINT8, count = 4 } })
			local texels = {}
			for i = 1, 256 do texels[i] = row:sub(i, i) .. "\0\0\255" end
			self.core.copy_to_buffer(table.concat(texels), buf)
			if self.blend_row then resource.release(M.BLEND_PATH) end
			resource.create_texture(M.BLEND_PATH, params(256, 1, 0, 0), buf)
			self.blend_row, self.blend_fresh = row, true
		end
		blend = M.BLEND_PATH
	end
	if self.texture then
		msg.post("@render:", "kp_ui_canvas", { texture = M.TEXTURE_PATH, fresh = self.fresh, blend = blend,
			blend_fresh = self.blend_fresh })
		self.fresh, self.blend_fresh = false, false
	end
end

-- Defold: stops drawing the canvas and releases its texture.
function Canvas:release()
	msg.post("@render:", "kp_ui_canvas", {})
	if self.texture then resource.release(M.TEXTURE_PATH) end
	if self.blend_row then resource.release(M.BLEND_PATH) end
	self.texture, self.blend_row = nil, nil
end

return M
