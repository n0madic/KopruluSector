-- Terrain tilemap fill for a mission template collection (generated/templates/
-- mission_<tileset>.collection, loaded through a collection proxy, ADR 0004).
--
-- tilemap.set_tile only reaches components of the calling script's collection, so the
-- template carries presentation/terrain.script; the owner of the data queues a job here
-- (modules are shared, game.project script.shared_state = 1) and posts "kp_terrain_fill".
--
-- Coordinates: map row 0 is the top row and goes to template row TEMPLATE (Defold tile y
-- grows upwards), so map pixel (px, py) is world (px, TEMPLATE * 32 - py).
local world = require("presentation.world")

local M = {}

M.TEMPLATE = world.TEMPLATE
M.TILE = world.TILE
M.FILL = hash("kp_terrain_fill")
M.FILLED = hash("kp_terrain_filled")

local pending = {}
-- Map extent last filled per template collection (tiles); the fresh template holds only its
-- two corner cells (converter/emit/terrain.py).
local filled = {}

-- ids: megatile ids from game.data.terrain.import (0-based, row-major; bit 15 is the
-- tile high bit and is not part of the megatile index). `token` comes back in FILLED so the
-- owner can drop the reply of a fill it no longer wants.
function M.queue(collection, width, height, ids, reply_to, token)
	pending[collection] = { width = width, height = height, ids = ids, reply_to = reply_to, token = token }
end

-- The template collection is unloaded (terrain.script final): its next load is fresh.
function M.forget(collection)
	filled[collection] = nil
end

-- Calls clear(x, y) (tilemap coordinates from 1) for every cell the previous fill of the
-- template set outside the new w x h map. `previous` = { width =, height = }, or nil for a
-- fresh template, whose only set cells are its two corners.
function M.stale_cells(previous, w, h, clear)
	local top = M.TEMPLATE
	if not previous then
		if h < top then clear(1, 1) end -- template cell (0, 0): bottom-left, below the map
		if w < top then clear(top, top) end -- template cell (255, 255): top-right, right of it
		return
	end
	for y = 0, previous.height - 1 do
		for x = (y < h) and w or 0, previous.width - 1 do clear(x + 1, top - y) end
	end
end

M.world_y = world.world_y

-- Runs inside the template collection (terrain.script).
function M.run(collection, url)
	local job = pending[collection]
	if not job then return end
	pending[collection] = nil
	local t0 = socket.gettime()
	local set_tile = tilemap.set_tile
	local w, h, ids, top = job.width, job.height, job.ids, M.TEMPLATE
	for y = 0, h - 1 do
		local ty, base = top - y, y * w
		for x = 0, w - 1 do
			set_tile(url, "terrain", x + 1, ty, ids[base + x] % 32768 + 1)
		end
	end
	-- Clear what a previous, larger map set outside this one.
	M.stale_cells(filled[collection], w, h, function(x, y) set_tile(url, "terrain", x, y, 0) end)
	filled[collection] = { width = w, height = h }
	msg.post(job.reply_to, M.FILLED, { ms = (socket.gettime() - t0) * 1000, token = job.token })
end

return M
