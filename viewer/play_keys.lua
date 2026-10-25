-- The viewer's keys as the original's window gets them (plan phase 9C): each Defold key action of
-- viewer/viewer.input_binding to its Windows virtual-key code, for kp_ui's window_key (the accelerators, the scroll
-- keys, the message line). The viewer's own actions on the same keys (left, next, fog, ...) map to their keys too.
local M = {}

local VK = {}
for c in ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"):gmatch(".") do VK["vk_" .. c:lower()] = c:byte() end
for i = 1, 12 do VK["vk_f" .. i] = 0x6f + i end
for i = 0, 9 do VK["vk_kp_" .. i] = 0x60 + i end
VK.vk_tab, VK.vk_backspace, VK.vk_insert, VK.vk_del = 0x09, 0x08, 0x2d, 0x2e
VK.vk_lshift, VK.vk_rshift, VK.vk_lctrl, VK.vk_rctrl, VK.vk_lalt, VK.vk_ralt = 0x10, 0x10, 0x11, 0x11, 0x12, 0x12
VK.vk_pageup, VK.vk_pagedown, VK.vk_end, VK.vk_home = 0x21, 0x22, 0x23, 0x24
-- the viewer's named actions (viewer.input_binding)
local NAMED = {
	escape = 0x1b, left = 0x25, up = 0x26, right = 0x27, down = 0x28, restart = 0x20, play = 0x0d, next = 0x4e,
	previous = 0x50, scale_mode = 0x4d, overlay_walk = 0x57, overlay_regions = 0x52, brightness = 0x42, grayscale = 0x47,
	browser = 0x41, fog = 0x46, continue = 0x43, quick_save = 0x74, quick_load = 0x78,
}
for name, vk in pairs(NAMED) do VK[name] = vk end

M.BY_HASH = {}
for name, vk in pairs(VK) do M.BY_HASH[hash(name)] = vk end

M.SHIFT, M.CTRL, M.ALT = 0x10, 0x11, 0x12

-- The modifiers' two keys share one virtual key: a release goes to the window only when the other side is up too.
local SIDES = { vk_lshift = "shift", vk_rshift = "shift", vk_lctrl = "ctrl", vk_rctrl = "ctrl", vk_lalt = "alt",
	vk_ralt = "alt" }
local SIDE = {}
for name, group in pairs(SIDES) do SIDE[hash(name)] = group end
local held = {}

-- Notes each key action the viewer gets, whoever takes it (the modifiers' sides held stay true when no window took
-- a release).
function M.track(action_id, action)
	if SIDE[action_id] and (action.pressed or action.released) then held[action_id] = not action.released or nil end
end

-- Forgets the modifiers' sides held: the window's activity changed (its key bytes are cleared, WM_ACTIVATEAPP), and a
-- side released meanwhile would keep the other side's release from the window.
function M.reset() held = {} end

-- Whether a key action goes to the window: Defold sends a held key's action every frame, the window gets its press,
-- its repeats and its release (WM_KEYDOWN only at the typematic rate); a modifier's release only when its other side
-- is up too (after M.track).
function M.forward(action_id, action)
	if not (action.pressed or action.repeated or action.released) then return false end
	local group = SIDE[action_id]
	if not group or not action.released then return true end
	for id, g in pairs(SIDE) do
		if g == group and held[id] then return false end
	end
	return true
end

return M
