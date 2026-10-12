-- The viewer's animated map scene (plan §7 phase 2 item 5): the mission's preplaced units
-- and THG2 sprites as game/images sprites whose images run the IScript VM every tick.
--
-- Viewer approximation, not the simulation: units have no CUnit, orders or movement
-- (phase 3). Each unit is a sprite (flingy sprite of its type, units.dat elevation, facing
-- from units.dat direction) plus its turret sprite; IScript opcodes that need a unit run
-- against VIEWER_HOOKS, which only turn the sprite and otherwise do nothing. THG2 sprites
-- are lone thingies, stepped like UpdateThingies.
--
-- A sprite whose script raises (it needs more than the viewer hooks give) is removed from the
-- world, so it is neither drawn half-updated nor retried on every tick.
local world = require("game.images.world")
local core = require("game.images.core")
local events = require("game.simulation.events")
local health_bar = require("presentation.health_bar")
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

-- Units of the viewer: unit token -> { sprite =, heading =, type =, hp =, shields =, energy = }
-- (8.8 fixed values; the scene starts them full); `selected` maps a sprite to the unit its
-- selection images show.
function M.viewer_hooks(units, selected)
	local function face(w, u, heading)
		local unit = units[u]
		unit.heading = heading % 256
		w:sprite_set_facing(unit.sprite, unit.heading)
	end
	local nothing = function() end
	return {
		unit_status = function() return 0 end,
		unit_type = function(_, u) return units[u].type end,
		selection_unit = function(_, s) return selected[s] end,
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

-- Removes sprite s after a raised error: its images go back to the free list and the
-- sprite to the free sprites. The error may have struck between two steps of an image
-- creation, so images are unlinked from s's own list rather than through their (possibly
-- stale) sprite field, and s is removed from its row only when it is linked there.
local function drop_sprite(w, s)
	local img, spr = w.img, w.spr
	local lst = spr.images[s]
	while lst.head ~= NONE do
		local i = lst.head
		core.unlink(img.prev, img.next, lst, i)
		img.grp[i] = 0
		core.insert_after_head(img.prev, img.next, w.free_images, i)
	end
	spr.main[s] = NONE
	local row = w.rows[w:sprite_row(s)]
	if row.head == s or spr.prev[s] ~= NONE or spr.next[s] ~= NONE then
		core.unlink(spr.prev, spr.next, row, s)
	end
	core.insert_after_head(spr.prev, spr.next, w.free_sprites, s)
end
M.drop_sprite = drop_sprite

-- mission: game.data.mission; ctx: { dat =, grp =, iscript = }. Returns the scene; its
-- `events` ring (game/simulation/events.lua) receives the IScript sounds of every tick.
function M.build(mission, ctx)
	local units = {}
	local g = rng.game(M.SEED)
	g:seed(M.SEED)
	g:set_enabled(1)
	local ring = events.new()
	local selected = {}
	local scene = { units = units, selected = selected, errors = 0, events = ring, tick = 0, dat = ctx.dat }
	local w = world.new({ dat = ctx.dat, grp = ctx.grp, iscript = ctx.iscript, rng = g,
		hooks = M.viewer_hooks(units, selected),
		map_width = mission.width, map_height = mission.height, tileset = mission.tileset,
		sound = function(id, x, y) ring:emit(events.SOUND, scene.tick, id, x, y) end })
	w:init_pools()
	w:init_thingies()
	scene.world = w
	local d = ctx.dat

	local function add_unit(unit_type, x, y, owner, elevation_add)
		if unit_type >= M.NO_SUBUNIT then return end
		local sprite_id = d.flingy.sprite[d.units.graphics[unit_type]]
		local token = #units + 1
		-- CreateSprite takes the free head: the hooks of the Init scripts see the sprite.
		local predicted = w.free_sprites.head
		if predicted == NONE then
			scene.errors = scene.errors + 1
			return
		end
		local unit = { heading = 0, sprite = predicted, type = unit_type, hp = d.units.hit_points[unit_type],
			shields = d.units.shield_amount[unit_type] * health_bar.FIXED, energy = M.max_energy(d, unit_type) }
		units[token] = unit
		w.unit, w.flingy = token, token
		local ok, s = pcall(w.sprite_create, w, sprite_id, x, y, owner)
		w.unit, w.flingy = nil, nil
		if not ok or s == NONE then
			if not ok then drop_sprite(w, predicted) end
			unit.sprite = NONE
			scene.errors = scene.errors + 1
			return
		end
		w.spr.elevation[s] = (d.units.elevation_level[unit_type] + elevation_add) % 256
		local direction = d.units.unit_direction[unit_type]
		if direction < 32 then
			unit.heading = direction * M.TURN_STEP
			w:sprite_set_facing(s, unit.heading)
		end
		local sub = d.units.subunit1[unit_type]
		if sub ~= M.NO_SUBUNIT then add_unit(sub, x, y, owner, 1) end
	end

	-- CreateLoneThingy with the same recovery: on an error the thingy (already unlinked from
	-- the free list) goes back to it and its sprite is dropped.
	local function add_thingy(id, x, y, owner)
		local t, predicted = w.free_lone.head, w.free_sprites.head
		local ok, created = pcall(w.lone_create, w, id, x, y, owner)
		if ok and created ~= NONE then return end
		scene.errors = scene.errors + 1
		if not ok and t ~= NONE and predicted ~= NONE then
			drop_sprite(w, predicted)
			w.thg.sprite[t] = NONE
			core.insert_after_head(w.thg.prev, w.thg.next, w.free_lone, t)
		end
	end

	for i = 1, unit_records.count(mission.units) do
		local r = unit_records.record(mission.units, i)
		if r.type ~= unit_records.START_LOCATION then add_unit(r.type, r.x, r.y, r.owner, 0) end
	end
	local s = mission.sprites
	for at = 0, #s - 10, 10 do
		local id, x, y, owner, flags = u16(s, at), u16(s, at + 2), u16(s, at + 4), s:byte(at + 7), u16(s, at + 8)
		if flags % (2 * M.THG2_SPRITE) >= M.THG2_SPRITE then
			add_thingy(id, x, y, owner)
		else
			add_unit(id, x, y, owner, 0)
		end
	end
	return scene
end

M.HERO = 0x40 -- units.dat special ability flag
M.ENERGY_MAX, M.HERO_ENERGY_MAX = 200 * health_bar.FIXED, 250 * health_bar.FIXED

-- The energy maximum 0x434940 gives without upgrades: 250 for heroes, 200 otherwise.
function M.max_energy(d, unit_type)
	return d.units.special_ability_flags[unit_type] % (2 * M.HERO) >= M.HERO and M.HERO_ENERGY_MAX or M.ENERGY_MAX
end

-- Bar values of unit token u (presentation/health_bar.lua) for the local player.
function M.bar_of(scene, u)
	local unit = scene.units[u]
	local w = scene.world
	return health_bar.from_unit({ type = unit.type, hp = unit.hp, shields = unit.shields, energy = unit.energy,
		timer = 0, status = 0, owner = w.spr.owner[unit.sprite] }, scene.dat, w.spr.id[unit.sprite], w.local_player,
		M.max_energy(scene.dat, unit.type))
end

-- Removes lone thingy t (and its sprite) after its step raised.
local function drop_thingy(w, t)
	local thg = w.thg
	local s = thg.sprite[t]
	if s ~= NONE then drop_sprite(w, s) end
	thg.sprite[t] = NONE
	core.unlink(thg.prev, thg.next, w.active_lone, t)
	core.insert_after_head(thg.prev, thg.next, w.free_lone, t)
end

-- One simulation tick of the scene: the lone thingies (the UpdateThingies walk, each step
-- guarded), then every unit sprite under its viewer context. A raising thingy or unit is
-- removed and counted in scene.errors.
function M.step(scene)
	local w = scene.world
	scene.tick = scene.tick + 1
	local nxt = w.thg.next
	local t = w.active_lone.head
	while t ~= NONE do
		local n = nxt[t]
		if not pcall(w.lone_update, w, t) then
			scene.errors = scene.errors + 1
			drop_thingy(w, t)
		end
		t = n
	end
	for token = 1, #scene.units do
		local unit = scene.units[token]
		local s = unit.sprite
		if s ~= NONE then
			w.unit, w.flingy = token, token
			local ok = pcall(w.sprite_update, w, s)
			w.unit, w.flingy = nil, nil
			if not ok then
				scene.errors = scene.errors + 1
				drop_sprite(w, s)
				unit.sprite = NONE
			elseif w.spr.images[s].head == NONE then
				unit.sprite = NONE
			end
		end
	end
end

return M
