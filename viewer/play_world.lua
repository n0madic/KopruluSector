-- The viewer's play mode (plan §7 phase 3: 3.5 fog presentation, 3.6 play mode, item 6
-- desktop input): a real game of a mission (the native core kp_core, ADR 0008) under the mission manager
-- (game/simulation/manager.lua: the scene is the manager) at the "fastest" speed.
-- The game's image world is the scene's world, so the presentation draws it as it draws the
-- animated map scene, health bars and selection circles included. The desktop input of the
-- phase becomes the local player's interface selection and the game commands the original
-- sends: a click or a box selects units (the select command 0x09), a right click moves them
-- or acts on the unit under the cursor (command 0x14), each run in the next turn.
--
-- Picking and box selection here are the viewer's: a unit's placement box contains the
-- point or meets the box (TODO: the original's UnitAtPoint 0x4bcd00 and box selection rules,
-- plan phase 9 item 3). A contract the simulation does not implement yet raises: the game
-- stops with the error, which the viewer shows, and keeps its last state on screen.
local health_bar = require("presentation.health_bar")
local int = require("game.simulation.int")
local manager = require("game.simulation.manager")

local band = int.band

local M = {}

M.SEED = 0x12345678
M.MAX_SELECTION = 12
M.SELECT, M.RIGHT_CLICK = 0x09, 0x14
M.NO_FOW_TYPE = 0xe4
M.SPRITE_HIDDEN = 0x20
M.STATUS_AIR = 0x4
-- Fog of the local player per tile (RGBA alpha): unexplored, explored but not seen, seen.
M.FOG_UNEXPLORED, M.FOG_EXPLORED, M.FOG_SEEN = 255, 128, 0

local function u16(v)
	v = v % 65536
	return string.char(v % 256, math.floor(v / 256))
end

-- ctx: { dat = (game.data.dat tables, for the unit extents), blobs = { dat =, grp =, iscript =, mission =,
-- tileset = } (the .kpd/.kpm file contents), name = (the mission's path: its saves carry it), expansion = }.
function M.build(ctx)
	local scene = manager.new(ctx.blobs, { seed = M.SEED, expansion = ctx.expansion or 0, name = ctx.name })
	scene.dat, scene.units, scene.errors = ctx.dat, {}, 0
	return scene
end

-- One step of the game with the commands of the last frame's input (the manager's step).
-- TODO: each draw should refresh the remembered terrain in view (g:refresh_terrain_onscreen with the camera's top-left
-- tile, RefreshRememberedTerrainOnscreen 0x40b220) once the viewer draws creep that changes during play.
function M.step(scene)
	return scene:step()
end

-- Bar values of game unit n (presentation/health_bar.lua) for the local player.
function M.bar_of(scene, n)
	local g = scene.game
	local u, w = g.u, g.img
	local s = u.sprite[n]
	return health_bar.from_unit({ type = u.type[n], hp = u.hp[n], shields = u.shields[n], energy = u.energy[n],
		timer = u.remove_timer[n], status = u.status[n], owner = u.owner[n] }, scene.dat, w.spr.id[s], g.local_player,
		g:max_energy(n))
end

-- The units of the active list whose placement box meets the rectangle (map pixels), seen by
-- the local player, flyers first.
local function units_in(scene, left, top, right, bottom)
	local g = scene.game
	local u, w, ext = g.u, g.img, scene.dat.units.unit_extents
	local air, ground = {}, {}
	local n = g.lists.active.head
	while n ~= 0 do
		local s = u.sprite[n]
		if band(w.spr.flags[s], M.SPRITE_HIDDEN) == 0 and band(w.spr.visibility[s], g.local_vision) ~= 0 then
			local t = u.type[n]
			local x, y = int.s16(w.spr.x[s]), int.s16(w.spr.y[s])
			if x - ext[t * 4] <= right and x + ext[t * 4 + 2] >= left and y - ext[t * 4 + 1] <= bottom
				and y + ext[t * 4 + 3] >= top then
				local list = band(u.status[n], M.STATUS_AIR) ~= 0 and air or ground
				list[#list + 1] = n
			end
		end
		n = u.next[n]
	end
	for _, m in ipairs(ground) do air[#air + 1] = m end
	return air
end

-- The unit under a map point, or 0.
function M.unit_at(scene, x, y)
	if not scene.game then return 0 end
	return units_in(scene, x, y, x, y)[1] or 0
end

-- A click (left == right, top == bottom) or a box: the local player's units become the
-- interface selection, and the select command goes out; a click on another player's unit
-- selects it alone without a command.
function M.select(scene, left, top, right, bottom)
	local g = scene.game
	if not g or scene.failed then return end
	local found = units_in(scene, left, top, right, bottom)
	local own, chosen = {}, {}
	for _, n in ipairs(found) do
		if g.u.owner[n] == g.local_player then own[#own + 1] = n end
	end
	if #own > 1 then
		for _, n in ipairs(own) do
			if #chosen < M.MAX_SELECTION and g:multi_selectable(n) then chosen[#chosen + 1] = n end
		end
	elseif #own == 1 then
		chosen = own
	elseif found[1] and left == right and top == bottom then
		g:select_locally({ found[1] })
		return
	end
	if #chosen == 0 then return end
	g:select_locally(chosen)
	local refs = {}
	for k, n in ipairs(chosen) do refs[k] = u16(g:unit_ref(n)) end
	scene:command(string.char(M.SELECT, #chosen) .. table.concat(refs))
end

-- A right click at a map point: the target mark (HighlightRightClickTarget 0x47fc10) and the
-- right-click command for the command selection.
function M.right_click(scene, x, y)
	local g = scene.game
	if not g or scene.failed then return end
	if x < 0 or y < 0 or x >= g.map_w_px or y >= g.map_h_px then return end
	local target = M.unit_at(scene, x, y)
	g:mark_target(x, y, target)
	scene:command(string.char(M.RIGHT_CLICK) .. u16(x) .. u16(y) .. u16(g:unit_ref(target)) .. u16(M.NO_FOW_TYPE)
		.. string.char(0))
end

-- The local player's fog per tile into an RGBA buffer stream (map width x height, row 0 at
-- the top): black with the alpha of FOG_UNEXPLORED, FOG_EXPLORED or FOG_SEEN.
function M.fog_pixels(scene, stream)
	local g = scene.game
	if not g then return false end
	local tiles, p = g.tiles, g.local_player
	local unseen, unexplored = 2 ^ p, 2 ^ (p + 8)
	local o = 0
	for i = 0, g.map_w * g.map_h - 1 do
		local f = tiles[i] or 0
		local a
		if band(f, unexplored) ~= 0 then
			a = M.FOG_UNEXPLORED
		elseif band(f, unseen) ~= 0 then
			a = M.FOG_EXPLORED
		else
			a = M.FOG_SEEN
		end
		stream[o + 1], stream[o + 2], stream[o + 3], stream[o + 4] = 0, 0, 0, a
		o = o + 4
	end
	return true
end

return M
