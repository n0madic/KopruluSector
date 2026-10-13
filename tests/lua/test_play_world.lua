-- The viewer's play mode (viewer/play_world.lua; plan §7 phase 3, 3.5 and 3.6) without the
-- engine: a real game of a scenario map, the box selection and right click of the desktop
-- input as the game commands the original sends, the health bars of the selection and the
-- local player's fog. Needs the converter's generated data; skipped without it.
local play_world = require("viewer.play_world")
local mission_data = require("game.data.mission")
local dat = require("game.data.dat")
local grpmeta = require("game.data.grpmeta")
local scripts = require("game.data.scripts")
local terrain = require("game.data.terrain")

local DATA = "generated/data/"
local MAP = "generated/scenarios/marine_group.kpm"

local S = {}

local function scene_of(T)
	local mission = mission_data.parse(T.read(MAP), MAP)
	return play_world.build(mission, {
		dat = dat.parse(T.read(DATA .. "dat.kpd"), "dat.kpd"),
		grp = grpmeta.parse(T.read(DATA .. "grpmeta.kpd"), "grpmeta.kpd"),
		iscript = scripts.parse_iscript(T.read(DATA .. "iscript.kpd"), "iscript.kpd"),
		tileset = terrain.parse_tileset(T.read(DATA .. "tilesets/" .. terrain.TILESETS[mission.tileset] .. ".kpd"), "ts"),
	})
end

function S.a_box_and_a_right_click_move_the_selected_units(T)
	if not (T.has(DATA .. "dat.kpd") and T.has(MAP)) then
		T.env.log("SKIP test_play_world: no generated data")
		return
	end
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
	-- The fog: the tiles around the Marines are seen, a far corner is not.
	local stream = {}
	T.ok(play_world.fog_pixels(scene, stream), "fog of the local player")
	local function alpha(tx, ty) return stream[(ty * g.map_w + tx) * 4 + 4] end
	local n = next(before)
	T.eq(alpha(math.floor(g.u.x[n] / 32), math.floor(g.u.y[n] / 32)), play_world.FOG_SEEN, "a Marine's tile is seen")
	T.ok(alpha(0, 0) ~= play_world.FOG_SEEN, "a far corner is not seen")
end

function S.a_click_on_nothing_keeps_the_selection(T)
	if not (T.has(DATA .. "dat.kpd") and T.has(MAP)) then return end
	local scene = scene_of(T)
	play_world.select(scene, 980, 1080, 1090, 1150)
	local first = scene.game.local_selection[0]
	play_world.select(scene, 100, 100, 100, 100)
	T.eq(scene.game.local_selection[0], first, "an empty click leaves the selection")
	T.eq(play_world.unit_at(scene, 100, 100), 0, "no unit there")
end

return S
