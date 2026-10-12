-- Game start: the parts of StartGame 0x443f40 / InitGameWorld 0x443790 that build the
-- simulation state, in the original's order, up to the first object step (docs/phase3.md;
-- the native call order comes from the emulator call trace of StartGame).
local game = require("game.simulation.game")
local mission_data = require("game.data.mission")
local placement = require("game.units.placement")
local rules = require("game.data.rules")
local terrain = require("game.data.terrain")
local unit_records = require("game.data.unit_records")
local int = require("game.simulation.int")
local bytes = require("game.data.bytes")

local band, bor = int.band, int.bor
local u8, u16 = bytes.u8, bytes.u16

local M = {}

M.CHK_HUMAN, M.CHK_COMPUTER = 6, 5
M.CONTROLLER_HUMAN, M.CONTROLLER_COMPUTER, M.CONTROLLER_RESCUE, M.CONTROLLER_NEUTRAL = 2, 1, 3, 7
M.FORCES = 4
M.FORCE_ALLIED, M.FORCE_ALLIED_VICTORY, M.FORCE_VISION = 0x2, 0x4, 0x8
M.ALLIED, M.ALLIED_COMPUTERS = 1, 2
M.THG2_RECORD, M.THG2_SPRITE, M.THG2_DISABLED = 10, 0x1000, 0x8000
M.NEUTRAL = 11
M.NEUTRAL_DOODADS = { [0xcd] = true, [0xce] = true, [0xcf] = true, [0xd0] = true }
M.EDGE_CLEAR, M.EDGE_SET = 0x20410000, 0x800000
M.UMS_ONLY_GROUP = 0x80
M.NYDUS, M.TERRAN = 134, 1
M.ADDON = 0x2 -- units.dat flag

-- The effective DAT tables of a mission: copies of the columns that the UMS override
-- sections (game/data/rules.lua effects) replace.
local UNIT_COLUMNS = { health = "hit_points", shields = "shield_amount", armor = "armor",
	build_time = "build_time", mineral_cost = "mineral_cost", gas_cost = "vespene_cost",
	name = "unit_map_string" }

local function copy(t)
	local c = {}
	for k, v in pairs(t) do c[k] = v end
	return c
end

local function effective_dat(base)
	local d = copy(base)
	for _, name in ipairs({ "units", "weapons", "upgrades", "techdata" }) do d[name] = copy(base[name]) end
	return d
end

-- NormalizeAcquisitionRanges 0x425610: a type's acquisition range covers its weapons'
-- maximum ranges (in tiles, >> 5), weapons of its subunit for turrets.
local function normalize_acquisition(d)
	local units, ranges = d.units, d.weapons.maximum_range
	local out = {}
	for t = 0, 227 do
		local w = units.subunit1[t] == 228 and t or units.subunit1[t]
		local r = units.target_acquisition_range[w]
		for _, weapon in ipairs({ units.ground_weapon[w], units.air_weapon[w] }) do
			if weapon < 0x82 and r < int.shr(ranges[weapon], 5) then r = int.shr(ranges[weapon], 5) end
		end
		out[t] = r % 256
	end
	units.target_acquisition_range = out
end

local function apply_rules(g, effects)
	local d = g.dat
	for field, column in pairs(UNIT_COLUMNS) do
		local values = effects.units[field]
		if next(values) then
			local c = copy(d.units[column])
			for t, v in pairs(values) do c[t] = v end
			d.units[column] = c
		end
	end
	if next(effects.weapon_damage) then
		local dmg, bonus = copy(d.weapons.damage_amount), copy(d.weapons.damage_bonus)
		for w, v in pairs(effects.weapon_damage) do dmg[w] = v end
		for w, v in pairs(effects.weapon_bonus) do bonus[w] = v end
		d.weapons.damage_amount, d.weapons.damage_bonus = dmg, bonus
	end
	for p = 0, 11 do
		for id, level in pairs(effects.upgrade_levels[1][p]) do g.upgrades[p][id] = level end
		for id, v in pairs(effects.tech_states[0][p]) do g.techs[p].available[id] = v end
		for id, v in pairs(effects.tech_states[1][p]) do g.techs[p].researched[id] = v end
	end
end

-- Players of a campaign mission: the CHK owners become runtime controllers (human 2,
-- computer 1, others unchanged); the local player is the human slot.
function M.players(mission)
	local players, local_player = {}, nil
	for p = 0, 11 do
		local owner = mission.owners[p + 1]
		local controller = owner
		if owner == M.CHK_HUMAN then
			controller = M.CONTROLLER_HUMAN
			local_player = local_player or p
		elseif owner == M.CHK_COMPUTER then
			controller = M.CONTROLLER_COMPUTER
		end
		if p >= 8 then controller = 0 end
		-- The player record's force byte is the FORC entry plus one (LoadForceChunk 0x443320)
		-- for the slots in the game; empty slots keep force 0.
		local force = 0
		if p < 8 and controller ~= 0 then force = (mission.forces.player[p + 1] + 1) % 256 end
		players[p] = { controller = controller, race = mission.races[p + 1], force = force }
	end
	return players, local_player or 0
end

-- InitAlliancesAndVision 0x477610 for a single-player game with the map's force settings
-- (0x4474d0 and 0x447db0 hold, 0x447e70's team game does not): every player allied with
-- itself (players 0..7) and with player 11, computers allied with each other (2), rescuable
-- and neutral players allied both ways with everyone; shared vision is the player's own bit;
-- then each force's alliance (SetForceAlliance 0x443180) and vision (SetForceVision
-- 0x443220) among its human players.
local function init_alliances(g, mission)
	local players, a, vision = g.players, {}, g.shared_vision
	for p = 0, 11 do
		a[p] = {}
		for q = 0, 11 do a[p][q] = 0 end
	end
	for p = 0, 7 do a[p][p] = M.ALLIED end
	for p = 11, 0, -1 do
		a[11][p], a[p][11] = M.ALLIED, M.ALLIED
		for q = 7, 0, -1 do
			if q ~= p and players[p].controller == M.CONTROLLER_COMPUTER
				and players[q].controller == M.CONTROLLER_COMPUTER then
				a[p][q], a[q][p] = M.ALLIED_COMPUTERS, M.ALLIED_COMPUTERS
			end
		end
	end
	for p = 0, 11 do
		vision[p] = 2 ^ p
		local c = players[p].controller
		if c == M.CONTROLLER_RESCUE or c == M.CONTROLLER_NEUTRAL then
			for q = 0, 11 do a[p][q], a[q][p] = M.ALLIED, M.ALLIED end
		end
	end
	local function humans_of(f)
		local out = {}
		for p = 0, 7 do
			if players[p].force == f and players[p].controller == M.CONTROLLER_HUMAN then out[#out + 1] = p end
		end
		return out
	end
	for f = 1, M.FORCES do
		local flags = mission.forces.flags[f]
		local allied = 0
		if band(flags, M.FORCE_ALLIED) ~= 0 then
			allied = band(flags, M.FORCE_ALLIED_VICTORY) ~= 0 and 2 or 1
		end
		local members = humans_of(f)
		for _, p in ipairs(members) do
			for _, q in ipairs(members) do
				if allied ~= 0 or p ~= q then a[p][q] = allied end
			end
		end
		for _, p in ipairs(members) do
			for _, q in ipairs(members) do
				if p ~= q then
					if band(flags, M.FORCE_VISION) ~= 0 then
						vision[p] = bor(vision[p], 2 ^ q)
					else
						vision[p] = band(vision[p], int.bnot(2 ^ q))
					end
				end
			end
		end
	end
	g.alliance = a
end

-- ImportMapTiles 0x445720 into the game's tile flags and megatiles, then the edge strips
-- of LoadTileMapSection 0x469170.
local function load_tiles(g, mission, ts)
	local ids, flags = terrain.import(mission, ts)
	if not ids then error(("start: tile %d selects a missing CV5 group"):format(flags), 0) end
	for i = 0, mission.width * mission.height - 1 do
		g.megatiles[i] = ids[i]
		g.raw_tiles[i] = u16(mission.tiles, 2 * i)
		-- import() starts from InitFogOfWar's low word; the high word replaces the tile's.
		g.tiles[i] = bor(band(g.tiles[i], 0xffff), band(flags[i], 0xffff0000))
	end
	local w, h = mission.width, mission.height
	g:tiles_clear(0, h - 2, 5, 2, M.EDGE_CLEAR)
	g:tiles_or(0, h - 2, 5, 2, M.EDGE_SET)
	g:tiles_clear(w - 5, h - 2, 5, 2, M.EDGE_CLEAR)
	g:tiles_or(w - 5, h - 2, 5, 2, M.EDGE_SET)
	g:tiles_clear(0, h - 1, w, 1, M.EDGE_CLEAR)
	g:tiles_or(0, h - 1, w, 1, M.EDGE_SET)
end

-- LoadFogMaskSection 0x469260: low word = mask byte in both bytes.
local function load_mask(g, mask)
	for i = 0, #mask - 1 do
		local m = u8(mask, i)
		g.tiles[i] = bor(band(g.tiles[i], 0xffff0000), m * 257)
	end
end

-- LoadDoodadSection 0x468ba0: sprites become lone thingies, units are created (UMS or
-- neutral); the disabled flag applies 0x428f70.
local function load_doodads(g, s)
	for at = 0, #s - M.THG2_RECORD, M.THG2_RECORD do
		local id, x, y, owner, flags = u16(s, at), u16(s, at + 2), u16(s, at + 4), u8(s, at + 6), u16(s, at + 8)
		if band(flags, M.THG2_SPRITE) ~= 0 then
			g.img:lone_create(id, x, y, owner)
		else
			if M.NEUTRAL_DOODADS[id] then owner = M.NEUTRAL end
			local n = g:create_map_unit(id, x, y, owner)
			if n ~= 0 and band(flags, M.THG2_DISABLED) ~= 0 then g:unit_disable_doodad(n) end
		end
	end
end

-- The relation records of LoadUnitSection (0x4693d0 Nydus, 0x469360 Terran buildings):
-- resolved newest first, each with the nearest older record whose related serial is its
-- serial (0x4698e0, 0x469810); a matched older record leaves the list (0x46a320).
local function resolve_relations(nodes, pair)
	while #nodes > 0 do
		local node = table.remove(nodes)
		for k = #nodes, 1, -1 do
			if nodes[k].related == node.serial then
				pair(node, table.remove(nodes, k))
				break
			end
		end
	end
end

-- LoadUnitSection 0x468f30.
local function load_units(g, s)
	local nydus, addons = {}, {}
	for i = 1, unit_records.count(s) do
		local r = unit_records.record(s, i)
		local x, y = unit_records.start_location(r, g.local_player)
		if x then
			g.start_locations[r.owner] = { x, y }
		else
			local controller = r.owner <= 7 and g.players[r.owner].controller or 0
			local admitted = r.owner > 7 or (controller ~= 0 and (controller < 5 or controller == 7))
			if admitted and not unit_records.excluded_by_melee_filter(g.melee, r.owner, r.type)
				and (not g.user_select[r.owner] or band(g.dat.units.staredit_group_flags[r.type], M.UMS_ONLY_GROUP) ~= 0) then
				local n = g:create_map_unit(r.type, r.x, r.y, r.owner)
				if n ~= 0 then
					g:apply_unit_record(n, r)
					local node = { serial = r.serial, related = r.related_serial, unit = n }
					if r.related_serial ~= 0 and r.type == M.NYDUS then nydus[#nydus + 1] = node end
					if r.related_serial ~= 0 and g:unit_race(n) == M.TERRAN and band(g.u.status[n], 2) ~= 0 then
						addons[#addons + 1] = node
					end
				end
			end
		end
	end
	resolve_relations(nydus, function(a, b)
		g:set_union_unit(a.unit, "union_0xd0", b.unit)
		g:set_union_unit(b.unit, "union_0xd0", a.unit)
	end)
	resolve_relations(addons, function(a, b)
		local building, addon = a.unit, b.unit
		if band(g.dat.units.special_ability_flags[g.u.type[building]], M.ADDON) ~= 0 then
			building, addon = addon, building
		end
		if g.u.type[building] == g:required_parent_type(g.u.type[addon]) then g:attach_addon(building, addon) end
	end)
	local n = g.lists.active.head
	while n ~= 0 do
		g:unit_update_sprite_visibility(n)
		n = g.u.next[n]
	end
	g.power_dirty = true
end

-- opts: dat, grp, iscript, mission (game.data.mission), tileset (terrain.parse_tileset),
-- rng (game generator) and crt (CRT generator), both already seeded with the game's
-- initial seed, expansion, report_error. Returns the game at its first object step.
function M.start(opts)
	local mission = opts.mission
	local players, local_player = M.players(mission)
	local d = effective_dat(opts.dat)
	local g = game.new({ dat = d, grp = opts.grp, iscript = opts.iscript, rng = opts.rng, crt = opts.crt,
		map_width = mission.width, map_height = mission.height, tileset = mission.tileset,
		tileset_data = opts.tileset, expansion = opts.expansion, local_player = local_player, players = players,
		contours = placement.contours(mission.contours, mission_data), regions = mission.regions,
		report_error = opts.report_error })
	g.start_locations, g.user_select, g.melee = {}, {}, 0
	init_alliances(g, mission)
	g:selection_init()
	g:step_init()
	g:trigger_clock_init(#mission.triggers > 0)
	g:ai_init()
	g:guard_init()
	g:fog_init()
	g:creep_init()
	g.img:init_pools()
	g.img:init_thingies()
	g:orders_init()
	g:counts_init()
	normalize_acquisition(d)
	g:units_init()
	g:strength_init()
	load_tiles(g, mission, opts.tileset)
	load_doodads(g, mission.sprites)
	load_mask(g, mission.mask)
	apply_rules(g, rules.effects(mission.rules, d.units, opts.expansion ~= 0))
	load_units(g, mission.units)
	return g
end

return M
