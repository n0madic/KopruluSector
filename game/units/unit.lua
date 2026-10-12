-- Unit pool, lists, creation and completion (atlas P08/P09; docs/research/native-unit-creation.md,
-- native-mission-units.md). Methods of the game (game/simulation/game.lua).
--
-- CUnit records 1..1700 live in SoA arrays G.u[field][n] named as in tools/native_state.py.
-- Unit references hold the record number (0 = none), sprite references the sprite record
-- (-1), order queue links the order node (-1). The type-dependent union +0xc0..+0xdb is kept
-- as seven DWORDs (union_0xc0 .. union_0xd8); words that hold a unit are flagged in
-- union_units (bit k for word k) so the state model can print them as unit references.
local core = require("game.images.core")
local int = require("game.simulation.int")

local band, bor, bnot = int.band, int.bor, int.bnot
local s16 = int.s16
local NONE = core.NONE
local unlink, _, insert_after_head = core.list_helpers(0)

local M = {}

M.UNITS = 1700
M.PLAYERS = 12
M.NO_UNIT = 228

-- Fields and their zero value (0 unless listed): the memset of InitializeUnitSystem.
M.FIELDS = {
	"prev", "next", "hp", "move_x", "move_y", "move_unit", "waypoint_x", "waypoint_y", "target_waypoint_x",
	"target_waypoint_y", "movement_flags", "heading", "turn_speed", "velocity_direction", "flingy",
	"flingy_0x26", "movement_type", "x", "y", "halt_x", "halt_y", "top_speed", "speed1", "speed2", "speed_x",
	"speed_y", "acceleration", "heading2", "velocity_direction2", "owner", "order", "order_state",
	"order_signal", "order_type", "order_timer", "ground_cooldown", "air_cooldown", "spell_cooldown",
	"order_x", "order_y", "order_unit", "shields", "type", "player_prev", "player_next", "subunit",
	"auto_target", "connected", "queue_count", "queue_timer", "unknown_0x86", "attack_notify_timer",
	"previous_type", "last_event_timer", "last_event_color", "unknown_0x8c", "rank", "kills", "last_attacker",
	"secondary_timer", "ai_action", "user_action", "button_set", "is_cloaked", "movement_state", "queue0",
	"queue1", "queue2", "queue3", "queue4", "energy", "queue_slot", "uniqueness", "secondary_order",
	"overlay_state", "hp_gain", "shield_gain", "build_time", "previous_hp", "loaded0", "loaded1", "loaded2",
	"loaded3", "loaded4", "loaded5", "loaded6", "loaded7", "union_0xc0", "union_0xc4", "union_0xc8",
	"union_0xcc", "union_0xd0", "union_0xd4", "union_0xd8", "union_units", "status", "resource_type",
	"wireframe", "secondary_state", "recent_order_timer", "visibility", "secondary_x", "secondary_y",
	"build_unit", "burrow_prev", "burrow_next", "rally_x", "rally_y", "rally_unit", "path",
	"collision_interval", "pathing_flags", "unknown_0x106", "being_healed", "contour_left", "contour_top",
	"contour_right", "contour_bottom", "remove_timer", "matrix_damage", "matrix_timer", "stim_timer",
	"ensnare_timer", "lockdown_timer", "irradiate_timer", "stasis_timer", "plague_timer", "storm_timer",
	"irradiated_by", "irradiate_player", "parasites", "cycle_counter", "blind", "maelstrom_timer",
	"unknown_0x125", "acid_spores", "acid_time0", "acid_time1", "acid_time2", "acid_time3", "acid_time4",
	"acid_time5", "acid_time6", "acid_time7", "acid_time8", "attack_sequence", "unknown_0x132", "ai",
	"air_strength", "ground_strength", "bound_left", "bound_right", "bound_top", "bound_bottom",
	"repulse_flags", "repulse_angle", "repulse_x", "repulse_y",
}
M.NULL = { sprite = NONE, queue_head = NONE, queue_tail = NONE }
M.FIELDS[#M.FIELDS + 1] = "sprite"
M.FIELDS[#M.FIELDS + 1] = "queue_head"
M.FIELDS[#M.FIELDS + 1] = "queue_tail"

-- Unit status (+0xdc) bits.
M.ST_COMPLETED, M.ST_GROUNDED_BUILDING, M.ST_AIR = 0x1, 0x2, 0x4
M.ST_BURROWED = 0x10
M.ST_CLOAKED = 0x300
M.ST_NOT_AIR = 0x100000
M.ST_BEACON = 0x200000
M.ST_INVINCIBLE = 0x4000000
M.ST_INTERCEPTED = 0x80000
M.RECENT_ORDER_TIME = 0xf
M.ORDER_AI_TRANSPORT = 0x60
M.ST_HALLUCINATION = 0x40000000
-- units.dat special ability flags.
M.DAT_BUILDING, M.DAT_ADDON, M.DAT_FLYER, M.DAT_SUBUNIT = 0x1, 0x2, 0x4, 0x10
M.DAT_HERO, M.DAT_DOUBLE_SUPPLY, M.DAT_POWERUP = 0x40, 0x400, 0x800
M.DAT_RESOURCE_CONTAINER, M.DAT_SPELLCASTER = 0x2000, 0x200000
M.DAT_PERMANENT_CLOAK, M.DAT_INVINCIBLE = 0x400000, 0x20000000

M.ORDER_NOTHING = 23
M.ORDER_RESCUE, M.ORDER_NEUTRAL = 161, 162
M.ORDER_THINGY_DEFAULT = 0xbc -- 0x4178e0 writes order 188 and state 0 as one WORD
M.BUTTONS_RALLY, M.BUTTONS_DEFAULT = 0xe8, 0xe7
M.SPIDER_MINES = 3
M.UNKNOWN_0XC8_BUILDING = 0x3d2c
M.ENERGY_HERO, M.ENERGY_NORMAL, M.ENERGY_FULL = 64000, 0xc800, 0x6400
M.VISIBILITY_ALL = 0xffffffff
M.ERROR_UNIT_CAP, M.ERROR_NO_UNIT = 0x3d, 0x3e
M.RANDOM_UNIT, M.RANDOM_WIREFRAME, M.RANDOM_HEADING = 14, 15, 36 -- 0x424900, 0x425710, 0x424f40
M.RANDOM_DIRECTION = 32 -- units.dat unit_direction: random
M.REVEALERS = { [33] = true, [101] = true } -- Scanner Sweep, Map Revealer
M.HANGAR_CHILDREN = { [73] = true, [85] = true } -- Interceptor, Scarab
M.TRAPS = { [203] = true, [209] = true, [210] = true, [211] = true, [212] = true, [213] = true }
M.RALLY_PRODUCERS = { [0x6a] = true, [0x6f] = true, [0x71] = true, [0x72] = true, [0x82] = true, [0x83] = true,
	[0x84] = true, [0x85] = true, [0x9a] = true, [0xa0] = true, [0xa7] = true, [0x9b] = true }
M.CONTROLLER_COMPUTER, M.CONTROLLER_RESCUE, M.CONTROLLER_NEUTRAL = 1, 3, 7
M.SOUND_CLOAK = 0x111 -- sfxdata entry of the permanent cloak at completion

local function is_beacon(t) return (t >= 0xc2 and t <= 199) or t == 0xcc end

function M.install(G)
	-- InitializeUnitSystem 0x425960: every record zeroed, every list empty, free list 1,
	-- 1700, 1699, ..., 2; the spatial index cleared.
	function G:units_init()
		local u = {}
		for _, f in ipairs(M.FIELDS) do
			local v, t = M.NULL[f] or 0, {}
			for n = 1, M.UNITS do t[n] = v end
			u[f] = t
		end
		self.u = u
		self.lists = { active = { head = 0, tail = 0 }, pending = { head = 0, tail = 0 },
			free = { head = 0, tail = 0 }, hidden = { head = 0, tail = 0 }, special = { head = 0, tail = 0 } }
		self.player_units = {}
		for p = 0, M.PLAYERS - 1 do self.player_units[p] = 0 end
		self.units_allocated = 0
		for n = 1, M.UNITS do insert_after_head(u.prev, u.next, self.lists.free, n) end
		self:spatial_clear()
		self.error_code = 0
	end

	local function flags(self, t) return self.dat.units.special_ability_flags[t] end

	-- A unit reference stored in a union word (union_units bit of the word; 0 clears it).
	local UNION_BITS = { union_0xc0 = 1, union_0xc4 = 2, union_0xc8 = 4, union_0xcc = 8, union_0xd0 = 16,
		union_0xd4 = 32, union_0xd8 = 64 }
	function G:set_union_unit(n, word, target)
		local u = self.u
		local bit = UNION_BITS[word]
		u[word][n] = target
		if target ~= 0 then
			u.union_units[n] = bor(u.union_units[n], bit)
		else
			u.union_units[n] = band(u.union_units[n], bnot(bit))
		end
	end

	-- HideUnit 0x422ba0 (only reached for units already hidden so far).
	function G:unit_hide(n)
		local w = self.img
		if band(w.spr.flags[self.u.sprite[n]], core.SF_HIDDEN) == 0 then
			error(("unit: hiding visible unit %d (0x422ba0)"):format(n), 0)
		end
	end

	-- UnitFromReference 0x428b50: a WORD reference (record & 0x7ff, uniqueness << 11) to a
	-- live unit, or 0.
	function G:unit_from_ref(ref)
		if ref == 0 then return 0 end
		local n = ref % 0x800
		if n == 0 or self:unit_is_dead(n) or self.u.uniqueness[n] ~= int.shr(ref, 11) then return 0 end
		return n
	end

	-- IsUnitPlacementInMapBounds 0x424d30.
	function G:unit_fits_map(t, x, y)
		local ext = self.dat.units.unit_extents
		return x - s16(ext[t * 4]) >= 0 and s16(ext[t * 4 + 2]) + x < self.map_w_px
			and y - s16(ext[t * 4 + 1]) >= 0 and s16(ext[t * 4 + 3]) + y < self.map_h_px
	end

	-- GetMaxEnergy 0x434940 (fixed point 1/256).
	function G:max_energy(n)
		local t = self.u.type[n]
		if band(flags(self, t), M.DAT_HERO) ~= 0 then return M.ENERGY_HERO end
		local upgrade = self.ENERGY_UPGRADES[t]
		if upgrade and self:upgrade_level(self.u.owner[n], upgrade) ~= 0 then return M.ENERGY_HERO end
		return M.ENERGY_NORMAL
	end

	-- SetUnitButtonSet 0x422f00.
	function G:set_button_set(n, set)
		local u = self.u
		if (band(u.status[n], 0x400) == 0 and u.lockdown_timer[n] == 0 and u.stasis_timer[n] == 0
				and u.maelstrom_timer[n] == 0) or band(flags(self, u.type[n]), M.DAT_BUILDING) ~= 0
			or set == M.NO_UNIT then
			u.button_set[n] = set
		end
	end

	-- GetDamageOverlaySlotCount2x 0x416050: doubled count of the valid damage-overlay
	-- slots of the sprite's main image (0x543310, see game/images/image.lua).
	function G:damage_overlay_slots(n)
		local w = self.img
		return w:damage_overlay_slots(w.img.id[w.spr.main[self.u.sprite[n]]])
	end

	-- InitializeUnitType 0x425a70.
	function G:unit_initialize_type(n, t, x, y, owner)
		local u, d, w = self.u, self.dat.units, self.img
		local saved = w.unit
		w.unit = n
		local ok = self:thingy_init(n, d.graphics[t], x, y, owner, 0)
		if not ok then
			w.unit = saved
			return false
		end
		u.union_0xc0[n], u.union_0xc4[n], u.union_0xc8[n], u.union_0xcc[n] = 0, 0, 0, 0
		u.union_units[n] = band(u.union_units[n], bnot(0xf))
		u.resource_type[n], u.secondary_timer[n] = 0, 0
		u.type[n] = t
		self:step_unit_sprite(n)
		w.unit = saved
		u.last_attacker[n] = 8
		u.shields[n] = d.shield_amount[t] * 256
		if t == 0xac then
			u.energy[n] = M.ENERGY_FULL
		else
			u.energy[n] = int.idiv(self:max_energy(n), 4) % 65536
		end
		u.ai_action[n] = 0
		w.spr.elevation[u.sprite[n]] = d.elevation_level[t]
		local f = d.special_ability_flags[t]
		local status = u.status[n]
		status = band(status, bnot(2)) + band(f * 2, 2)
		status = band(status, bnot(4)) + band(f, 4)
		status = band(status, bnot(0x10000)) + band(int.shr(f, 12), 0x10000)
		status = band(status, bnot(0x20000)) + band(int.shr(f, 10), 0x20000)
		status = band(status, bnot(M.ST_NOT_AIR)) + (band(status, M.ST_AIR) == 0 and M.ST_NOT_AIR or 0)
		u.status[n] = status
		u.pathing_flags[n] = band(u.pathing_flags[n], 0xfe) + (d.elevation_level[t] < 12 and 1 or 0)
		if is_beacon(t) then u.status[n] = bor(u.status[n], M.ST_BEACON) end
		if band(f, M.DAT_BUILDING) ~= 0 then
			u.union_0xc8[n] = band(u.union_0xc8[n], 0xffff0000) + M.UNKNOWN_0XC8_BUILDING
		end
		u.path[n], u.movement_state[n], u.recent_order_timer[n] = 0, 0, 0
		if band(f, M.DAT_INVINCIBLE) ~= 0 then
			u.status[n] = bor(u.status[n], M.ST_INVINCIBLE)
		else
			u.status[n] = band(u.status[n], bnot(M.ST_INVINCIBLE))
		end
		u.overlay_state[n] = self:damage_overlay_slots(n)
		local build = d.build_time[t]
		u.build_time[n] = build ~= 0 and build or 1
		self:init_build_hp_gain(n)
		self:init_build_shield_gain(n)
		self:apply_creation_upgrades(n)
		self:recalculate_speed(n)
		return true
	end

	-- InitThingyHeader 0x4178e0 (the flingy, then the thingy's order fields).
	function G:thingy_init(n, flingy, x, y, owner, heading)
		if not self:flingy_init(n, flingy, x, y, owner, heading) then return false end
		local u = self.u
		u.owner[n], u.order_type[n], u.order_x[n], u.order_y[n] = owner, M.NO_UNIT, 0, 0
		u.order[n], u.order_state[n], u.order_signal[n] = M.ORDER_THINGY_DEFAULT, 0, 0
		u.order_timer[n], u.ground_cooldown[n], u.air_cooldown[n], u.spell_cooldown[n] = 0, 0, 0, 0
		u.order_unit[n] = 0
		return true
	end

	-- StepObjectSprite 0x418b20: the sprite's images run once; a sprite left without images
	-- is dropped from the unit.
	function G:step_unit_sprite(n)
		local s = self.u.sprite[n]
		if s == NONE then return end
		self.img:sprite_update(s)
		if self.img.spr.images[s].head == NONE then self.u.sprite[n] = NONE end
	end

	-- InitBuildHpGain 0x427d90 and InitBuildShieldGain 0x427de0.
	function G:init_build_hp_gain(n)
		local u, d = self.u, self.dat.units
		local t = u.type[n]
		local time, hp = d.build_time[t], d.hit_points[t]
		local gain = 0
		if time ~= 0 then gain = int.udiv((time - 1) + (hp - int.udiv(hp, 10)), time) % 65536 end
		u.hp_gain[n] = gain ~= 0 and gain or 1
	end

	function G:init_build_shield_gain(n)
		local u, d = self.u, self.dat.units
		local t = u.type[n]
		if d.shield_enabled[t] == 0 or band(u.status[n], M.ST_GROUNDED_BUILDING) == 0 then return end
		local start = int.udiv(d.shield_amount[t] * 256, 10)
		u.shields[n] = start
		local time = d.build_time[t]
		local gain = 0
		if time ~= 0 then gain = int.udiv(d.shield_amount[t] * 256 - start, time) % 65536 end
		u.shield_gain[n] = gain ~= 0 and gain or 1
	end

	-- InitializeUnit 0x425710.
	function G:unit_initialize(n, t, x, y, owner)
		local u = self.u
		for _, f in ipairs({ "union_0xd0", "union_0xd4", "union_0xd8", "rally_x", "rally_y", "rally_unit",
			"burrow_prev", "burrow_next", "auto_target", "connected", "queue_count", "queue_timer",
			"unknown_0x86", "attack_notify_timer", "previous_type", "last_event_timer", "last_event_color",
			"unknown_0x8c", "rank", "kills", "remove_timer", "matrix_damage", "matrix_timer", "stim_timer",
			"ensnare_timer", "lockdown_timer", "irradiate_timer", "stasis_timer", "plague_timer", "storm_timer",
			"irradiated_by", "irradiate_player", "parasites", "cycle_counter", "blind", "maelstrom_timer",
			"unknown_0x125", "acid_spores", "acid_time0", "acid_time1", "acid_time2", "acid_time3", "acid_time4",
			"acid_time5", "acid_time6", "acid_time7", "acid_time8", "status", "user_action", "pathing_flags",
			"ai" }) do
			u[f][n] = 0
		end
		u.queue_head[n], u.queue_tail[n] = NONE, NONE
		u.union_units[n] = band(u.union_units[n], 0xf)
		u.previous_hp[n] = 1
		if not self:unit_initialize_type(n, t, x, y, owner) then return false end
		for k = 0, 4 do u["queue" .. k][n] = M.NO_UNIT end
		u.queue_slot[n] = 0
		u.uniqueness[n] = (u.uniqueness[n] + 1) % 32
		self:set_button_set(n, M.RALLY_PRODUCERS[t] and M.BUTTONS_RALLY or M.BUTTONS_DEFAULT)
		u.wireframe[n] = self.rng:raw(M.RANDOM_WIREFRAME) % 256
		local d = self.dat.units
		if band(flags(self, t), M.DAT_SUBUNIT) ~= 0 then
			u.hp[n] = 1
		else
			u.hp[n] = int.udiv(d.hit_points[t], 10)
		end
		if band(u.status[n], M.ST_GROUNDED_BUILDING) == 0 then
			self:set_order(n, M.ORDER_NOTHING)
		else
			self:set_order(n, d.human_ai_idle[t])
		end
		self:set_secondary_order(n, M.ORDER_NOTHING)
		u.bound_left[n], u.bound_right[n], u.bound_top[n], u.bound_bottom[n] = -1, -1, -1, -1
		u.player_prev[n] = 0
		local head = self.player_units[owner]
		u.player_next[n] = head
		if head ~= 0 then u.player_prev[head] = n end
		self.player_units[owner] = n
		self:account_live(n, 1)
		local w = self.img
		local s = u.sprite[n]
		if band(u.status[n], M.ST_GROUNDED_BUILDING) == 0 then
			if t == 2 or t == 0x13 then
				u.union_0xc0[n] = band(u.union_0xc0[n], 0xffffff00) + M.SPIDER_MINES
			end
			w.spr.flags[s] = bor(w.spr.flags[s], core.SF_HIDDEN)
			w:sprite_set_visibility(s, 0)
		else
			self:unit_activate_spatial(n)
			self:replace_unit_images(n, true)
			w:sprite_set_visibility(s, 0)
			if self.placement_active then error("unit: building creation during placement (0x47d530)", 0) end
		end
		u.visibility[n] = M.VISIBILITY_ALL
		if band(flags(self, t), M.DAT_SUBUNIT) ~= 0 then
			w.spr.flags[s] = bor(w.spr.flags[s], core.SF_DEPTH)
			return true
		end
		if band(w.spr.flags[s], core.SF_HIDDEN) == 0 then self:unit_update_vision(n) end
		return true
	end

	-- AllocateUnit 0x424860: the free head initialised, or 0 with the error word set.
	function G:unit_allocate(t, x, y, owner)
		if self.units_allocated == M.UNITS then
			self.error_code = M.ERROR_UNIT_CAP
			return 0
		end
		if not self:unit_fits_map(t, x, y) then
			self.error_code = 0
			return 0
		end
		local n = self.lists.free.head
		if n ~= 0 and self:unit_initialize(n, t, x, y, owner) then
			self.units_allocated = self.units_allocated + 1
			unlink(self.u.prev, self.u.next, self.lists.free, n)
			return n
		end
		self.error_code = M.ERROR_NO_UNIT
		return 0
	end

	-- CreateUnit 0x424900: the unit (active list for grounded buildings, else hidden) and its
	-- subunit, whose main image takes the parent's special LO point 0.
	function G:unit_create(t, x, y, owner)
		self.rng:raw(M.RANDOM_UNIT)
		local n = self:unit_allocate(t, x, y, owner)
		if n == 0 then return 0 end
		local u = self.u
		local lst = band(u.status[n], M.ST_GROUNDED_BUILDING) ~= 0 and self.lists.active or self.lists.hidden
		insert_after_head(u.prev, u.next, lst, n)
		local sub = t <= 0x69 and self.dat.units.subunit1[t] or M.NO_UNIT
		if sub == M.NO_UNIT then
			u.subunit[n] = 0
			return n
		end
		local child = self:unit_allocate(sub, x, y, owner)
		if child == 0 then
			error(("unit: subunit allocation failure (0x425cd0) for type %d"):format(t), 0)
		end
		u.subunit[n], u.subunit[child] = child, n
		local w = self.img
		local ox, oy = w:lo_offset(w.spr.main[u.sprite[n]], 2, 0)
		w:image_set_offset(w.spr.main[u.sprite[child]], ox % 256, oy % 256)
		return n
	end

	-- ReplaceUnitImages 0x423330: the main image becomes the construction image (or the
	-- sprite's own), keeping a cloak state.
	function G:replace_unit_images(n, construction)
		local u, w = self.u, self.img
		local s = u.sprite[n]
		if band(u.status[n], M.ST_CLOAKED) ~= 0 then
			error("unit: replacing the images of a cloaked unit (0x416030/0x415e50)", 0)
		end
		local id = construction and self.dat.units.construction_animation[u.type[n]] or 0
		if id == 0 then id = self.dat.sprites.image[w.spr.id[s]] end
		local saved = w.unit
		w.unit = n
		self:sprite_replace_main(s, id, u.heading[n])
		w.unit = saved
		self:rebuild_status_overlays(n)
		u.overlay_state[n] = self:damage_overlay_slots(n)
	end

	-- ReplaceSpriteMainImage 0x415590 for unselected sprites (selection circles and health
	-- bars belong to the selection code).
	function G:sprite_replace_main(s, id, heading)
		local w = self.img
		local f = w.spr.flags[s]
		if band(f, 0x0f) ~= 0 then
			error("unit: replacing the main image of a selected sprite (0x416d50/0x416cd0)", 0)
		end
		local i = w.spr.images[s].head
		while i ~= NONE do
			local nxt = w.img.next[i]
			w:image_release(i)
			i = nxt
		end
		local main = w:image_alloc()
		if main ~= NONE then
			local list = w.spr.images[s]
			if list.head == NONE then
				w.spr.main[s] = main
				core.insert_after_head(w.img.prev, w.img.next, list, main)
			else
				core.insert_before(w.img.prev, w.img.next, list, list.head, main)
			end
			w:image_init_record(main, id, s, 0, 0)
			w:image_init_drawing(main)
			w:image_set_direction(main, 0)
		end
		i = w.spr.images[s].head
		while i ~= NONE do
			w:image_direction_from_angle(i, heading)
			i = w.img.next[i]
		end
	end

	-- RebuildStatusOverlays 0x434d10: overlays of active status effects.
	function G:rebuild_status_overlays(n)
		local u = self.u
		for _, f in ipairs({ "matrix_timer", "lockdown_timer", "maelstrom_timer", "irradiate_timer", "ensnare_timer",
			"stasis_timer", "plague_timer", "acid_spores" }) do
			if u[f][n] ~= 0 then error("unit: status overlays (0x434d10, " .. f .. ")", 0) end
		end
	end

	-- ActivateUnitSpatial 0x41a050.
	function G:unit_activate_spatial(n)
		local u = self.u
		self:spatial_insert(n)
		if band(u.status[n], M.ST_GROUNDED_BUILDING) ~= 0 then self:add_building_occupancy(n) end
		self:update_overlap_flags(n)
		if band(u.status[n], M.ST_AIR) ~= 0 then self:air_density_add(n) end
	end

	-- AddUnitOccupancy 0x419fd0: tile flag 0x08000000 over the placement size.
	function G:add_building_occupancy(n)
		local u, d = self.u, self.dat.units
		local t, s = u.type[n], u.sprite[n]
		local w, h = s16(d.staredit_placement_size[t * 2]), s16(d.staredit_placement_size[t * 2 + 1])
		local x, y = s16(self.img.spr.x[s]), s16(self.img.spr.y[s])
		local tx = int.sar(x - int.idiv(w, 2), 5)
		local ty = int.sar(y - int.idiv(h, 2), 5)
		self:tiles_or(tx, ty, int.idiv(w, 32), int.idiv(h, 32), 0x08000000)
	end

	-- RemoveUnitOccupancy 0x41a190.
	function G:remove_building_occupancy(n)
		local u, d = self.u, self.dat.units
		local t, s = u.type[n], u.sprite[n]
		local w, h = s16(d.staredit_placement_size[t * 2]), s16(d.staredit_placement_size[t * 2 + 1])
		local x, y = s16(self.img.spr.x[s]), s16(self.img.spr.y[s])
		self:tiles_clear(int.sar(x - int.idiv(w, 2), 5), int.sar(y - int.idiv(h, 2), 5), int.idiv(w, 32),
			int.idiv(h, 32), 0x08000000)
	end

	-- DeactivateUnitSpatial 0x41a210.
	function G:unit_deactivate_spatial(n)
		local u = self.u
		self:spatial_remove(n)
		if band(u.status[n], M.ST_GROUNDED_BUILDING) ~= 0 then self:remove_building_occupancy(n) end
		if band(u.status[n], M.ST_AIR) ~= 0 then self:air_density_remove(n) end
	end

	-- ResetMovementState 0x42c7e0 (with FreeUnitPath 0x42c8a0).
	function G:reset_movement(n)
		local u = self.u
		if u.path[n] ~= 0 then self:path_free(n) end
		u.movement_state[n] = 0
		u.pathing_flags[n] = band(u.pathing_flags[n], 0xfe)
			+ (self.img.spr.elevation[u.sprite[n]] < 12 and 1 or 0)
	end

	-- ResetToIdleAnimation 0x420910.
	function G:reset_to_idle_animation(n)
		local u, w = self.u, self.img
		u.status[n] = band(u.status[n], bnot(0x2000))
		local s = u.sprite[n]
		w.spr.flags[s] = band(w.spr.flags[s], 0x7f)
		local su, sf = w.unit, w.flingy
		w.unit, w.flingy = n, n
		self:flingy_to_idle(n)
		w.unit, w.flingy = su, sf
	end

	-- ShowUnit 0x422f60.
	function G:unit_show(n)
		local u, w = self.u, self.img
		local s = u.sprite[n]
		if band(w.spr.flags[s], core.SF_HIDDEN) == 0 then return end
		w.spr.flags[s] = band(w.spr.flags[s], bnot(core.SF_HIDDEN))
		local sub = u.subunit[n]
		local sub_indexed = sub ~= 0 and band(flags(self, u.type[sub]), M.DAT_SUBUNIT) ~= 0
		if sub_indexed then
			local ss = u.sprite[sub]
			w.spr.flags[ss] = band(w.spr.flags[ss], bnot(core.SF_HIDDEN))
		end
		self:unit_update_vision(n)
		self:unit_update_sprite_visibility(n)
		self:unit_activate_spatial(n)
		self:reset_movement(n)
		if sub_indexed then self:reset_movement(sub) end
		unlink(u.prev, u.next, self.lists.hidden, n)
		insert_after_head(u.prev, u.next, self.lists.active, n)
	end

	-- PrefinishUnit 0x424f40.
	function G:unit_prefinish(n)
		local u, d = self.u, self.dat.units
		local t = u.type[n]
		if u.build_time[n] ~= 0 then
			u.hp[n] = d.hit_points[t]
			u.shields[n] = d.shield_amount[t] * 256
			u.build_time[n] = 0
		end
		self:set_button_set(n, t)
		if band(u.status[n], M.ST_GROUNDED_BUILDING) == 0 then
			if band(u.status[n], 0x10000) ~= 0 then
				local dir = d.unit_direction[t]
				if dir == M.RANDOM_DIRECTION then dir = self.rng:raw(M.RANDOM_HEADING) % 32 end
				self:flingy_set_heading(n, dir * 8)
			end
			if t > 0xca and t < 0xd6 then self:unit_show(n) end
		else
			u.parasites[n], u.blind[n] = 0, 0
			self:replace_unit_images(n, false)
			self.img:sprite_play(u.sprite[n], 0x10, true)
		end
	end

	-- ClampPointToMapForType 0x41a4b0.
	function G:clamp_to_map(t, x, y)
		local ext = self.dat.units.unit_extents
		local left, up, right, down = s16(ext[t * 4]), s16(ext[t * 4 + 1]), s16(ext[t * 4 + 2]), s16(ext[t * 4 + 3])
		if x < left then
			x = left
		elseif self.map_w_px - right <= x then
			x = s16(self.map_w_px - right - 1)
		end
		if y < up then
			y = up
		elseif self.map_h_px - down - 32 <= y then
			y = s16(self.map_h_px - down - 33)
		end
		return x, y
	end

	-- RelocateUnit 0x43a270.
	function G:unit_relocate(n, x, y)
		local u, w = self.u, self.img
		x, y = self:clamp_to_map(u.type[n], x, y)
		local s = u.sprite[n]
		local ox, oy = s16(w.spr.x[s]), s16(w.spr.y[s])
		self:flingy_set_position(n, x, y)
		if not (u.sprite[n] == NONE or u.order[n] == 0) then
			self:set_move_target_point(n, x, y)
			self:flingy_set_target_waypoint(n, x, y)
		end
		if band(flags(self, u.type[n]), M.DAT_SUBUNIT) == 0 then
			self:spatial_move(n, x - ox, y - oy)
			if band(w.spr.flags[s], core.SF_HIDDEN) == 0 then self:update_overlap_flags(n) end
		end
	end

	-- The shared tail of the move-target setters: not intercepted, the recent-order timer,
	-- and movement flag 0x20 unless the next queued order slows down at waypoints.
	local function move_target_set(self, n)
		local u = self.u
		u.status[n] = band(u.status[n], bnot(M.ST_INTERCEPTED))
		u.recent_order_timer[n] = M.RECENT_ORDER_TIME
		local head = u.queue_head[n]
		if head ~= NONE and self.dat.orders.waypoints_slowdown[self.orders.order[head]] == 0 then
			u.movement_flags[n] = bor(u.movement_flags[n], 0x20)
		else
			u.movement_flags[n] = band(u.movement_flags[n], 0xdf)
		end
	end

	-- SetMoveTargetPoint 0x43a0d0.
	function G:set_move_target_point(n, x, y)
		local u = self.u
		if u.move_x[n] == x and u.move_y[n] == y then return true end
		if u.ai[n] ~= 0 then error("unit: AI goal reaction on a move target (0x437540)", 0) end
		if u.path[n] ~= 0 then self:path_mark_stale(n) end
		x, y = self:clamp_to_map(u.type[n], x, y)
		self:flingy_set_move_target(n, x, y)
		move_target_set(self, n)
		return true
	end

	-- OnContactBoundary 0x42e520: (x, y) lies on the boundary of the positions where unit n's
	-- box touches the target's. The original adds n's up extent to the right side and nothing
	-- to the bottom side; both are kept.
	local function on_contact_boundary(self, n, target, x, y)
		local u, ext = self.u, self.dat.units.unit_extents
		local s = u.sprite[target]
		local tx, ty = self.img.spr.x[s], self.img.spr.y[s]
		local t, m = u.type[target] * 4, u.type[n] * 4
		local left = s16(tx - ext[t] - 1 - ext[m + 2])
		local right = s16(tx + ext[t + 2] + ext[m] + 1 + ext[m + 1] + 1)
		local top = s16(ty - ext[t + 1] - 1 - ext[m + 3])
		local bottom = s16(ty + ext[t + 3])
		x, y = s16(x), s16(y)
		if x == left or x == right then return y >= top and y <= bottom end
		if y == top or y == bottom then return x >= left and x <= right end
		return false
	end

	-- SetMoveTargetUnit 0x43a180: the unit moves to the target's position, unless it already
	-- goes there or to the boundary of contact with it.
	function G:set_move_target_unit(n, target)
		local u = self.u
		local cur = u.move_unit[n]
		if target == cur then
			local s = u.sprite[cur]
			if (u.move_x[n] == s16(self.img.spr.x[s]) and u.move_y[n] == s16(self.img.spr.y[s]))
				or on_contact_boundary(self, n, cur, u.move_x[n], u.move_y[n]) then
				u.status[n] = band(u.status[n], bnot(M.ST_INTERCEPTED))
				return true
			end
		end
		if u.ai[n] ~= 0 and u.order[n] ~= M.ORDER_AI_TRANSPORT then
			error("unit: AI goal reaction on a move target unit (0x437540)", 0)
		end
		if u.path[n] ~= 0 then self:path_mark_stale(n) end
		self:flingy_set_move_target_unit(n, target)
		move_target_set(self, n)
		return true
	end

	-- PlaceCompletedUnit 0x426600: a hidden unit is placed by the placement search.
	function G:unit_place(n)
		local u, w = self.u, self.img
		local s = u.sprite[n]
		if band(w.spr.flags[s], core.SF_HIDDEN) == 0 then return true end
		local x, y = self:find_unit_placement(n, s16(w.spr.x[s]), s16(w.spr.y[s]), nil, false, nil)
		if not x then
			if self.error_code ~= 0 then
				self:report_error(u.owner[n], self.error_code)
				self.error_code = 0
			end
			return false
		end
		self:unit_relocate(n, x, y)
		local sub = u.subunit[n]
		if sub ~= 0 and band(flags(self, u.type[n]), M.DAT_SUBUNIT) == 0 then
			self:account_completed(sub, 1, false)
			u.status[sub] = bor(u.status[sub], M.ST_COMPLETED)
			u.hp[sub] = self.dat.units.hit_points[u.type[sub]]
			self:unit_relocate(sub, x, y)
		end
		return true
	end

	-- FinishUnitCompletion 0x424de0.
	function G:unit_finish_completion(n)
		local u, d = self.u, self.dat.units
		local t = u.type[n]
		if band(flags(self, t), M.DAT_DOUBLE_SUPPLY) ~= 0 then
			self:account_live(n, -1)
			u.status[n] = bor(u.status[n], M.ST_COMPLETED)
			self:account_live(n, 1)
		else
			u.status[n] = bor(u.status[n], M.ST_COMPLETED)
		end
		self:account_completed(n, 1, true)
		if band(flags(self, t), M.DAT_PERMANENT_CLOAK) ~= 0 then self:begin_cloak(n, M.SOUND_CLOAK) end
		if M.REVEALERS[t] then
			unlink(u.prev, u.next, self.lists.hidden, n)
			insert_after_head(u.prev, u.next, self.lists.special, n)
			self:unit_update_vision(n)
		elseif band(self.img.spr.flags[u.sprite[n]], core.SF_HIDDEN) ~= 0 and not M.HANGAR_CHILDREN[t] then
			self:unit_show(n)
		end
		if M.TRAPS[t] then
			self:unit_set_cloak_state(n, 3)
			u.visibility[n] = 0x80000000
			u.secondary_timer[n] = 0
		end
		local controller = self.players[u.owner[n]].controller
		local order
		if controller == M.CONTROLLER_RESCUE then
			order = M.ORDER_RESCUE
		elseif controller == M.CONTROLLER_NEUTRAL then
			order = M.ORDER_NEUTRAL
		elseif controller == M.CONTROLLER_COMPUTER then
			order = d.comp_ai_idle[t]
		else
			order = d.human_ai_idle[t]
		end
		self:issue_order(n, order)
		if band(flags(self, t), M.DAT_POWERUP) ~= 0 then
			local s = u.sprite[n]
			u.union_0xd0[n] = (self.img.spr.x[s] % 65536) + (self.img.spr.y[s] % 65536) * 65536
			self:issue_order(n, d.human_ai_idle[t])
		end
		self:refresh_unit_strength(n)
	end

	-- CompleteUnitWithPlacement 0x424db0.
	function G:unit_complete(n)
		self:unit_prefinish(n)
		if not self:unit_place(n) then return false end
		self:unit_finish_completion(n)
		return true
	end

	-- SetCloakState 0x41d4c0: status bits 0x300 = state << 8.
	function G:unit_set_cloak_state(n, state)
		local u = self.u
		u.status[n] = band(u.status[n], bnot(M.ST_CLOAKED)) + band(state * 256, M.ST_CLOAKED)
	end

	-- SetUnitHp 0x41bb50.
	function G:set_unit_hp(n, hp)
		local u, d = self.u, self.dat.units
		local max = d.hit_points[u.type[n]]
		if hp > max then hp = max end
		u.hp[n] = hp
		local s = u.sprite[n]
		if band(self.img.spr.flags[s], 8) ~= 0 and band(self.img.spr.visibility[s], self.local_vision) ~= 0 then
			error("unit: health bar refresh (0x416bb0)", 0)
		end
		if band(u.status[n], M.ST_COMPLETED) ~= 0 then
			if self.dat.images.damage_overlay[self.img.img.id[self.img.spr.main[s]]] ~= 0 then
				self:update_damage_overlays(n)
			end
			if band(u.status[n], M.ST_COMPLETED) ~= 0 then self:refresh_unit_strength(n) end
		end
	end

	-- UpdateBuildingDamageOverlays 0x422dd0: one overlay step per lost part of the hit
	-- points above a third, each added (0x415860) or removed (0x4157a0) in turn.
	function G:update_damage_overlays(n)
		local u, d = self.u, self.dat.units
		local max = d.hit_points[u.type[n]]
		local third = int.idiv(max, 3)
		local slots = self:damage_overlay_slots(n) % 256
		local step = int.udiv(max - third, slots + 1)
		local target = int.idiv(u.hp[n] - int.urem(max - third, step) - third - 1, step)
		if target < 0 then target = 0 end
		target = target % 256
		while u.overlay_state[n] < target do
			self:damage_overlay_remove(n)
			u.overlay_state[n] = u.overlay_state[n] + 1
		end
		while u.overlay_state[n] > target do
			self:damage_overlay_add(n)
			u.overlay_state[n] = u.overlay_state[n] - 1
		end
	end

	-- AddDamageOverlay 0x415860: a small flame (images 450..471) grows to a large one
	-- (472..493); otherwise a new small flame takes the k-th free valid slot, k random.
	M.SMALL_FLAMES, M.LARGE_FLAMES, M.FLAME_SLOTS = 450, 472, 22
	M.RANDOM_FLAME = 8
	function G:damage_overlay_add(n)
		local w = self.img
		local s = self.u.sprite[n]
		local main = w.spr.main[s]
		if self.dat.images.damage_overlay[w.img.id[main]] == 0 then return end
		local i = w.spr.images[s].head
		while i ~= NONE do
			local id = w.img.id[i]
			if id >= M.SMALL_FLAMES and id < M.LARGE_FLAMES then
				local x, y = w:lo_offset(main, 1, id - M.SMALL_FLAMES)
				w:image_release(i)
				w:sprite_image_on_top(s, id + M.FLAME_SLOTS, x, y, 0)
				return
			end
			i = w.img.next[i]
		end
		local count = int.shr(w:damage_overlay_slots(w.img.id[main]), 1)
		local k = int.urem(self.rng:raw(M.RANDOM_FLAME), count) + 1
		local seen, slot = 0, 0
		while true do
			if slot > M.FLAME_SLOTS - 1 then slot = 0 end
			local x, y = w:lo_offset(main, 1, slot)
			if x ~= 127 or y ~= 127 then
				seen = seen + 1
				local taken = false
				local j = w.spr.images[s].head
				while j ~= NONE do
					if w.img.id[j] == slot + M.LARGE_FLAMES then
						taken = true
						break
					end
					j = w.img.next[j]
				end
				if not taken and seen >= k then
					w:sprite_image_on_top(s, slot + M.SMALL_FLAMES, x, y, 0)
					return
				end
			end
			slot = slot + 1
		end
	end

	-- RemoveDamageOverlay 0x4157a0: a large flame shrinks, else a small one goes.
	function G:damage_overlay_remove(n)
		local w = self.img
		local s = self.u.sprite[n]
		local main = w.spr.main[s]
		if self.dat.images.damage_overlay[w.img.id[main]] == 0 then return end
		local i = w.spr.images[s].head
		while i ~= NONE do
			local id = w.img.id[i]
			if id >= M.LARGE_FLAMES and id < M.LARGE_FLAMES + M.FLAME_SLOTS then
				local x, y = w:lo_offset(main, 1, id - M.LARGE_FLAMES)
				w:image_release(i)
				w:sprite_image_on_top(s, id - M.FLAME_SLOTS, x, y, 0)
				return
			end
			i = w.img.next[i]
		end
		i = w.spr.images[s].head
		while i ~= NONE do
			local id = w.img.id[i]
			if id >= M.SMALL_FLAMES and id < M.LARGE_FLAMES then
				w:image_release(i)
				return
			end
			i = w.img.next[i]
		end
	end
end

return M
