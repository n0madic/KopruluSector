-- Terrain tilemap fill for a mission template collection (generated/templates/
-- mission_<tileset>.collection, loaded through a collection proxy, ADR 0004).
--
-- tilemap.set_tile only reaches components of the calling script's collection, so the
-- template carries presentation/terrain.script; the owner of the data queues a job here
-- (modules are shared, game.project script.shared_state = 1) and posts "kp_terrain_fill".
--
-- Coordinates: map row 0 is the top row and goes to template row TEMPLATE (Defold tile y
-- grows upwards), so map pixel (px, py) is world (px, TEMPLATE * 32 - py).
local M = {}

M.TEMPLATE = 256
M.TILE = 32
M.FILL = hash("kp_terrain_fill")
M.FILLED = hash("kp_terrain_filled")

local pending = {}

-- ids: megatile ids from game.data.terrain.import (0-based, row-major; bit 15 is the
-- tile high bit and is not part of the megatile index).
function M.queue(collection, width, height, ids, reply_to)
	pending[collection] = { width = width, height = height, ids = ids, reply_to = reply_to }
end

function M.world_y(map_y)
	return M.TEMPLATE * M.TILE - map_y
end

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
	-- Clear template cells outside the map (a previous, larger map may have set them).
	for y = 1, top - h do
		for x = 1, top do set_tile(url, "terrain", x, y, 0) end
	end
	for y = top - h + 1, top do
		for x = w + 1, top do set_tile(url, "terrain", x, y, 0) end
	end
	msg.post(job.reply_to, M.FILLED, { ms = (socket.gettime() - t0) * 1000 })
end

return M
