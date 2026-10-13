-- The saved state of a game at a step boundary (plan D17; §7 phase 3 item 1): every mutable
-- field of the game (game/simulation/game.lua) and of its image world (game/images/world.lua),
-- in the deterministic encoding of game/simulation/snapshot.lua. The static data that the
-- start of a mission rebuilds (DAT tables, the mission's regions and tileset, vision
-- templates, GRP metadata, IScript, callbacks) is left out; a load starts the same mission
-- and restores the saved fields in place, so tables shared between the game and the world
-- (the generator, the tile flags, the circle colours) stay shared. List and free-list order,
-- pools, path records, timers and the generator's counters are fields like any other.
local snapshot = require("game.simulation.snapshot")

local M = {}

M.VERSION = 2

-- Game fields rebuilt by the start of the mission, or callbacks. The region graph (rg), the
-- local search's geometry context (lg) and its swept areas (visited) are rebuilt too and only
-- hold scratch between searches, which the native search (native/kp_path) does not use; the
-- reveal cache (game/vision/fog.lua) only replays what a reveal computes.
-- TODO: the region changes of buildings and doors (phase 6) make parts of rg game state.
M.STATIC_GAME = {
	dat = true, regions = true, tileset_data = true, vision_templates = true, img = true,
	on_error = true, on_text = true, play_sound = true, rg = true, lg = true, visited = true, reveal_cache = true,
	reveal_cache_count = true,
}
-- World fields rebuilt by the start, or callbacks. overlay_slots caches DAT-derived counts.
M.STATIC_WORLD = {
	dat = true, grp = true, script = true, hooks = true, sound = true, overlay_slots = true,
}

local function fields(t, static)
	local out = {}
	for k, v in pairs(t) do
		if not static[k] then
			if type(v) == "function" then
				error(("save: field %s is a function; list it as static or keep it out of the state"):format(k), 0)
			end
			out[k] = v
		end
	end
	return out
end

-- The save of game g with the identity of its mission: `mission` names the mission (as
-- loaded) and `data` the hash of the converted data, both checked by load.
function M.capture(g, mission, data)
	return { version = M.VERSION, mission = mission, data = data, game = fields(g, M.STATIC_GAME),
		world = fields(g.img, M.STATIC_WORLD) }
end

function M.encode(g, mission, data)
	return snapshot.encode(M.capture(g, mission, data))
end

-- Copies src into dst in place (tables keep their identity and metatables); returns the
-- value to store.
local function merge(dst, src)
	if type(src) ~= "table" then return src end
	if type(dst) ~= "table" then dst = {} end
	for k in pairs(dst) do
		if src[k] == nil then dst[k] = nil end
	end
	for k, v in pairs(src) do dst[k] = merge(dst[k], v) end
	return dst
end

local function restore(target, saved, static)
	for k in pairs(target) do
		if not static[k] and saved[k] == nil then target[k] = nil end
	end
	for k, v in pairs(saved) do target[k] = merge(target[k], v) end
end

-- Restores the encoded save `bytes` into g, a game of the same mission and data fresh from
-- its start.
function M.load(g, bytes, mission, data)
	local s = snapshot.decode(bytes)
	if s.version ~= M.VERSION then error(("save: version %s, expected %d"):format(tostring(s.version), M.VERSION), 0) end
	if s.mission ~= mission then error(("save: mission %s, expected %s"):format(tostring(s.mission), mission), 0) end
	if s.data ~= data then error("save: made with other game data", 0) end
	restore(g, s.game, M.STATIC_GAME)
	restore(g.img, s.world, M.STATIC_WORLD)
	return g
end

return M
