-- The simulation state of one game (plan §7 phase 3, docs/phase3.md): the images world of
-- phase 2 plus units, orders, the spatial index, counters, tile flags and players, with the
-- native subsystems installed as methods. Everything is advanced sequentially in the
-- original's order (ADR 0007).
local core = require("game.images.core")
local images = require("game.images.world")
local int = require("game.simulation.int")

local band = int.band

local M = {}

local Game = {}
Game.__index = Game
M.Game = Game

M.PLAYERS = 12
M.NONE = core.NONE

-- Upgrades read by the energy maximum 0x434940, the sight range 0x423150 and the transport
-- check 0x4381f0 (jump tables of those functions).
Game.ENERGY_UPGRADES = { [1] = 0x15, [8] = 0x16, [9] = 0x13, [12] = 0x17, [34] = 0x33, [45] = 0x1f,
	[46] = 0x20, [60] = 0x2f, [63] = 0x31, [67] = 0x28, [71] = 0x2c }
Game.SIGHT_UPGRADES = { [1] = 0x14, [42] = 0x19, [70] = 0x29, [84] = 0x26 }
Game.OVERLORD_TRANSPORT = 0x18
Game.RALLY_PRODUCERS = require("game.units.unit").RALLY_PRODUCERS
Game.NONE = core.NONE

-- opts: dat (effective game.data.dat tables), grp, iscript, rng (game generator),
-- map_width, map_height (tiles), tileset, tileset_data (game.data.terrain.parse_tileset),
-- expansion (0/1), local_player, players = { [p] = { controller =, race =, force = } },
-- contours (game/units/placement.lua M.contours), report_error (function(player, id)).
function M.new(opts)
	local g = setmetatable({
		dat = opts.dat, rng = opts.rng, expansion = opts.expansion or 0,
		local_player = opts.local_player or 0, players = opts.players or {},
		map_w = opts.map_width, map_h = opts.map_height,
		map_w_px = opts.map_width * core.TILE, map_h_px = opts.map_height * core.TILE,
		tileset_data = opts.tileset_data, contours = opts.contours,
		tiles = {}, megatiles = {}, raw_tiles = {},
		extent_ctx = {}, upgrades = {}, shared_vision = {},
		placement_active = false, on_error = opts.report_error, density = {},
	}, Game)
	g.local_vision = 2 ^ g.local_player
	for p = 0, M.PLAYERS - 1 do
		g.players[p] = g.players[p] or { controller = 0, race = 0, force = 0 }
		g.shared_vision[p] = 2 ^ p
		g.upgrades[p] = {}
	end
	g.img = images.new({
		dat = opts.dat, grp = opts.grp, iscript = opts.iscript, rng = opts.rng, hooks = g:image_hooks(),
		sound = opts.sound, map_width = opts.map_width, map_height = opts.map_height, tiles = g.tiles,
		tileset = opts.tileset, expansion = g.expansion, local_player = g.local_player,
		local_vision = g.local_vision,
	})
	return g
end

-- Native error notifications (0x46ae50 and its callers): only the local player's reach
-- the interface, through opts.report_error.
function Game:report_error(player, id)
	if player == self.local_player and self.on_error then self.on_error(player, id) end
end

-- GetUpgradeLevel 0x49a470.
function Game:upgrade_level(player, id)
	return self.upgrades[player][id] or 0
end

-- Megatile of tile index i (MTXM through CV5, 0x445690's lookup) and the VF4 mini-tile
-- flags of a megatile cell.
function Game:megatile_at(i)
	return band(self.megatiles[i], 0x7fff)
end

function Game:minitile_flags(mega, mx, my)
	local vf4 = self.tileset_data.vf4
	local at = ((mega * 4 + my) * 4 + mx) * 2
	return vf4:byte(at + 1) + vf4:byte(at + 2) * 256
end

-- IsMorphingIntoAdvancedZergBuilding 0x43c040.
local ZERG_MORPHS = { [0x85] = true, [0x84] = true, [0x89] = true, [0x90] = true, [0x92] = true }
function Game:is_morphing_zerg_building(n)
	local u = self.u
	if band(u.status[n], 1) ~= 0 then return false end
	return ZERG_MORPHS[u["queue" .. u.queue_slot[n]][n]] == true
end

-- Paths do not exist before units move (phase 3.4).
function Game:path_free(n)
	error(("game: path of unit %d (0x48e910)"):format(n), 0)
end

function Game:path_mark_stale(n)
	error(("game: path of unit %d (0x42c790)"):format(n), 0)
end

-- AddAirDensity 0x481760: flyers mark the 48-pixel grid of the repulsion code.
Game.DENSITY_COLUMNS = 0xab
Game.RANDOM_DENSITY = 37
function Game:air_density_add(n)
	local u = self.u
	if band(u.status[n], 0x20000) == 0 then return end
	local t = u.type[n]
	if band(self.dat.units.special_ability_flags[t], 1) ~= 0 or t == 0x49 then return end
	if band(u.repulse_flags[n], 0xf0) == 0 then
		local r = self.rng:raw(Game.RANDOM_DENSITY)
		u.repulse_angle[n] = r % 256
		local high = band(int.shr(r, 8), 0xf0)
		if high == 0 then high = 0xf0 end
		u.repulse_flags[n] = int.bor(u.repulse_flags[n], high)
	end
	u.repulse_flags[n] = band(u.repulse_flags[n], 0xf8)
	local s = u.sprite[n]
	u.repulse_x[n] = int.idiv(int.s16(self.img.spr.x[s]), 48) % 256
	u.repulse_y[n] = int.idiv(int.s16(self.img.spr.y[s]), 48) % 256
	local cell = u.repulse_x[n] + u.repulse_y[n] * Game.DENSITY_COLUMNS
	local v = self.density[cell] or 0
	if v ~= 0xff then self.density[cell] = v + 1 end
end

-- ApplyCreationSpeedUpgrades 0x43a8e0 (upgrade ids from its jump table).
local SPEED_UPGRADES = { [2] = 0x11, [19] = 0x11, [38] = 0x1d, [39] = 0x35, [42] = 0x1a, [65] = 0x22,
	[69] = 0x25, [70] = 0x2a, [84] = 0x27 }
local ALWAYS_FAST = { [53] = true, [57] = true, [77] = true, [80] = true, [88] = true, [103] = true }
function Game:apply_creation_upgrades(n)
	local u = self.u
	local t, p = u.type[n], u.owner[n]
	if not (SPEED_UPGRADES[t] or ALWAYS_FAST[t] or t == 37 or t == 54 or t == 48) then return end
	if SPEED_UPGRADES[t] then
		if self:upgrade_level(p, SPEED_UPGRADES[t]) ~= 0 then self:set_speed_upgrade(n) end
	elseif t == 37 then -- Zergling: Metabolic Boost, then Adrenal Glands
		if self:upgrade_level(p, 0x1b) ~= 0 then self:set_speed_upgrade(n) end
		if self:upgrade_level(p, 0x1c) ~= 0 then u.status[n] = int.bor(u.status[n], 0x20000000) end
	elseif t == 54 then -- Devouring One
		self:set_speed_upgrade(n)
		u.status[n] = int.bor(u.status[n], 0x20000000)
	elseif t == 48 then
		if self.expansion ~= 0 then self:set_speed_upgrade(n) end
	elseif ALWAYS_FAST[t] then
		self:set_speed_upgrade(n)
	end
	self:recalculate_speed(n)
end

require("game.units.spatial").install(Game)
require("game.units.flingy").install(Game)
require("game.units.counts").install(Game)
require("game.units.orders").install(Game)
require("game.units.unit").install(Game)
require("game.units.strength").install(Game)
require("game.units.placement").install(Game)
require("game.units.hooks").install(Game)
require("game.units.map_units").install(Game)
require("game.ai.guard").install(Game)
require("game.vision.fog").install(Game)
require("game.vision.detection").install(Game)

return M
