-- The viewer's play mode (viewer/play_world.lua; plan §7 phase 3, 3.5 and 3.6) without the
-- engine: a real game of a scenario map, the box selection and right click of the desktop
-- input as the game commands the original sends, the health bars of the selection and the
-- local player's fog. Needs the converter's generated data and the native core (kp_core); skipped without them.
local native = require("game.native")
local play_world = require("viewer.play_world")
local dat = require("game.data.dat")
local native_only = require("tests.lua.native_only")
local oracle = require("tools.lua.oracle")

local DATA = oracle.DATA
local MAP = "generated/scenarios/marine_group.kpm"

local S = {}

local function scene_of(T)
	return play_world.build({ dat = dat.parse(T.read(DATA .. "dat.kpd"), "dat.kpd"), blobs = oracle.blobs(T.read, MAP),
		name = MAP })
end

-- The game's saves carry the mission's path, and a load counts for the presentation.
function S.saves_carry_the_mission(T)
	if not native_only.ready(T, "test_play_world.saves", { DATA .. "dat.kpd", MAP }) then return end
	local scene = scene_of(T)
	scene:step()
	local bytes = scene:save()
	T.ok(bytes:find(MAP, 1, true), "the mission's path in the save")
	scene:load(bytes)
	T.eq(scene.loads, 1)
end

function S.a_box_and_a_right_click_move_the_selected_units(T)
	if not native_only.ready(T, "test_play_world", { DATA .. "dat.kpd", MAP }) then return end
	local scene = scene_of(T)
	T.ok(scene.failed == nil, "the game starts: " .. tostring(scene.failed))
	local g = scene.game
	for _ = 1, 5 do play_world.step(scene) end
	-- The five Marines stand at (1000..1064, 1100..1132).
	play_world.select(scene, 980, 1080, 1090, 1150)
	local selected = 0
	for k = 0, 11 do
		if g.local_selection[k] ~= 0 then selected = selected + 1 end
	end
	T.eq(selected, 5, "the box selects the five Marines")
	T.eq(#scene.pending, 1, "the selection goes out as a command")
	T.eq(scene.pending[1].bytes:byte(1), play_world.SELECT, "the select command")
	local bar = play_world.bar_of(scene, g.local_selection[0])
	T.ok(bar.hp > 0 and bar.hp == bar.hp_max, "a selected Marine's health bar is full")
	play_world.step(scene)
	local commanded = 0
	for k = 0, 11 do
		if g.selections[g.local_player][k] ~= 0 then commanded = commanded + 1 end
	end
	T.eq(commanded, 5, "the select command ran: the player's command selection")
	local before = {}
	for k = 0, 4 do
		local n = g.local_selection[k]
		before[n] = g.u.x[n]
	end
	play_world.right_click(scene, 1300, 1110)
	T.eq(scene.pending[1].bytes:byte(1), play_world.RIGHT_CLICK, "the right-click command")
	for _ = 1, 120 do play_world.step(scene) end
	T.ok(scene.failed == nil, "the game runs: " .. tostring(scene.failed))
	for n, x in pairs(before) do
		T.ok(g.u.x[n] > x + 150, ("Marine %d walked east: %d -> %d"):format(n, x, g.u.x[n]))
	end
	-- The bounds of a walked Marine's image follow it once the draw refreshes them (the viewer does before its draw
	-- list, as the original's redraw: 0x415700); the game's steps alone leave them where the Marine stood.
	local w, walker = g.img, next(before)
	local main = w.spr.main[g.u.sprite[walker]]
	local stale = w.img.x[main]
	w:refresh_bounds()
	T.ok(w.img.x[main] > stale + 150, ("the image's bounds move with the Marine: %d -> %d"):format(stale, w.img.x[main]))
	T.ok(math.abs(w.img.x[main] - g.u.x[walker]) < 32, "the bounds are at the Marine")
	-- The fog of war (kp_core.game.fog_view): around the Marines the levels are seen (31); a view in the map's far
	-- corner is unexplored (0).
	local core = native.find("kp_core")
	local n = next(before)
	local function level_under(x, y)
		local view = core.game.fog_view()
		local cam_x, cam_y = math.max(0, x - 320), math.max(0, y - 200)
		view:update(g, cam_x, cam_y, 640, 400)
		local levels, w, _, bx, by = view:levels()
		return levels:byte((math.floor(y / 8) - by) * w + math.floor(x / 8) - bx + 1)
	end
	T.eq(level_under(g.u.x[n], g.u.y[n]), 31, "a Marine's block is seen")
	T.eq(level_under(g.map_w_px - 400, g.map_h_px - 300), 0, "a far corner is unexplored")
end

function S.a_click_on_nothing_keeps_the_selection(T)
	if not native_only.ready(T, "test_play_world.click", { DATA .. "dat.kpd", MAP }) then return end
	local scene = scene_of(T)
	play_world.select(scene, 980, 1080, 1090, 1150)
	local first = scene.game.local_selection[0]
	play_world.select(scene, 100, 100, 100, 100)
	T.eq(scene.game.local_selection[0], first, "an empty click leaves the selection")
	T.eq(play_world.unit_at(scene, 100, 100), 0, "no unit there")
end

return S
