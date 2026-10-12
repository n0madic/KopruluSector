-- The viewer's animated map scene (plan §7 phase 2 item 5): the mission's preplaced units
-- and THG2 sprites as game/images sprites whose images run the IScript VM every tick.
--
-- Viewer approximation, not the simulation: units have no CUnit, orders or movement
-- (phase 3). Each unit is a sprite (flingy sprite of its type, units.dat elevation, facing
-- from units.dat direction) plus its turret sprite; IScript opcodes that need a unit run
-- against VIEWER_HOOKS, which only turn the sprite and otherwise do nothing. THG2 sprites
-- are lone thingies, stepped by UpdateThingies.
local world = require("game.images.world")
local rng = require("game.simulation.rng")
local unit_records = require("game.data.unit_records")
local bytes = require("game.data.bytes")

local u16 = bytes.u16
local NONE = world.NONE

local M = {}

M.NO_SUBUNIT = 228
M.THG2_SPRITE = 0x1000
M.SEED = 0x12345678
M.TURN_STEP = 8 -- heading units per direction step

-- Units of the viewer: unit token -> { sprite =, heading = }.
local function viewer_hooks(units)
	local function face(w, u, heading)
		local unit = units[u]
		unit.heading = heading % 256
		w:sprite_set_facing(unit.sprite, unit.heading)
	end
	local nothing = function() end
	return {
		unit_status = function() return 0 end,
		unit_owner = function(w, u) return w.spr.owner[units[u].sprite] end,
		unit_target = function() return nil end,
		unit_sprite = function(_, u) return units[u].sprite end,
		unit_resources = function() return 1 end,
		unit_heading = function(_, u) return units[u].heading end,
		turn_ccw = function(w, u, n) face(w, u, units[u].heading - n * M.TURN_STEP) end,
		turn_cw = function(w, u, n) face(w, u, units[u].heading + n * M.TURN_STEP) end,
		set_flingy_direction = function(w, u, d) face(w, u, d) end,
		move_speed = function() return 0 end,
		target_point = function() return 0, 0 end,
		within_distance = function() return false end,
		target_heading = function() return 0 end,
		angle_diff = function() return 0 end,
		spell_target_invalid = function() return true end,
		order_weapon = function() return 255 end,
		unit_fits = function() return true end,
		sprol_goliath = function() return false end,
		set_spawn_frame = nothing, attack_with = nothing, fire_weapon = nothing, use_weapon = nothing,
		set_move_speed = nothing, clear_repeat_attack = nothing, nobrk_start = nothing, nobrk_end = nothing,
		ignore_rest_idle = nothing, set_forward_offset = nothing, set_top_speed = nothing, attack_melee = nothing,
		missile_damage = nothing, order_signal = nothing, set_cloak_state = nothing, hide_order_marker = nothing,
	}
end

-- mission: game.data.mission; ctx: { dat =, grp =, iscript = }. Returns the scene.
function M.build(mission, ctx)
	local units = {}
	local g = rng.game(M.SEED)
	g:seed(M.SEED)
	g:set_enabled(1)
	local w = world.new({ dat = ctx.dat, grp = ctx.grp, iscript = ctx.iscript, rng = g, hooks = viewer_hooks(units),
		map_width = mission.width, map_height = mission.height, tileset = mission.tileset })
	w:init_pools()
	w:init_thingies()
	local scene = { world = w, units = units, errors = 0 }
	local d = ctx.dat

	local function add_unit(unit_type, x, y, owner, elevation_add)
		if unit_type >= M.NO_SUBUNIT then return end
		local sprite_id = d.flingy.sprite[d.units.graphics[unit_type]]
		local token = #units + 1
		local unit = { heading = 0, sprite = NONE }
		units[token] = unit
		w.unit, w.flingy = token, token
		local ok, s = pcall(w.sprite_create, w, sprite_id, x, y, owner)
		w.unit, w.flingy = nil, nil
		if not ok or s == NONE then
			scene.errors = scene.errors + 1
			return
		end
		unit.sprite = s
		w.spr.elevation[s] = (d.units.elevation_level[unit_type] + elevation_add) % 256
		local direction = d.units.unit_direction[unit_type]
		if direction < 32 then
			unit.heading = direction * M.TURN_STEP
			w:sprite_set_facing(s, unit.heading)
		end
		local sub = d.units.subunit1[unit_type]
		if sub ~= M.NO_SUBUNIT then add_unit(sub, x, y, owner, 1) end
	end

	for i = 1, unit_records.count(mission.units) do
		local r = unit_records.record(mission.units, i)
		if r.type ~= unit_records.START_LOCATION then add_unit(r.type, r.x, r.y, r.owner, 0) end
	end
	local s = mission.sprites
	for at = 0, #s - 10, 10 do
		local id, x, y, owner, flags = u16(s, at), u16(s, at + 2), u16(s, at + 4), s:byte(at + 7), u16(s, at + 8)
		if flags % (2 * M.THG2_SPRITE) >= M.THG2_SPRITE then
			local ok, t = pcall(w.lone_create, w, id, x, y, owner)
			if not ok or t == NONE then scene.errors = scene.errors + 1 end
		else
			add_unit(id, x, y, owner, 0)
		end
	end
	return scene
end

-- One simulation tick of the scene: lone thingies, then every unit sprite under its
-- viewer context. Errors (a script needing more than the viewer hooks give) drop the unit.
function M.step(scene)
	local w = scene.world
	if not pcall(w.update_thingies, w) then scene.errors = scene.errors + 1 end
	for token = 1, #scene.units do
		local unit = scene.units[token]
		if unit.sprite ~= NONE then
			w.unit, w.flingy = token, token
			local ok = pcall(w.sprite_update, w, unit.sprite)
			w.unit, w.flingy = nil, nil
			if not ok then
				scene.errors = scene.errors + 1
				unit.sprite = NONE
			elseif w.spr.images[unit.sprite].head == NONE then
				unit.sprite = NONE
			end
		end
	end
end

return M
