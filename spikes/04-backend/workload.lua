-- Deterministic image workload for the presentation backend benchmark (spike 4).
-- Simulation-side state in SoA arrays, advanced per logical tick (42 ms):
--   * 40% of images move (positions in 1/256 px, interpolated by presentation);
--   * every image changes frame each tick (17-frame flipbook) and may flip;
--   * each image has a player remap row; 20% use a destination-reading material;
--   * 2% of the live images are despawned and the same number spawned per tick;
--   * draw order is the native depth key (plan D13a) with sprite index tie-break.
local rng = require("game.simulation.rng")

local M = {}

M.FRAMES = 17
M.WORLD_W, M.WORLD_H = 1280, 720

function M.new(capacity)
	local w = {
		capacity = capacity,
		alive = {}, x = {}, y = {}, px = {}, py = {}, vx = {}, vy = {},
		frame = {}, flip = {}, row = {}, dest = {}, elevation = {},
		order = {}, live = 0, tick = 0,
		spawned = {}, despawned = {}, -- slots changed during the last tick
		rand = rng.crt(12345),
	}
	for i = 1, capacity do w.alive[i] = false end
	return w
end

local function r(w, n) return w.rand:rand() % n end

local function spawn(w, i)
	w.alive[i] = true
	w.x[i] = r(w, M.WORLD_W) * 256
	w.y[i] = r(w, M.WORLD_H) * 256
	w.px[i], w.py[i] = w.x[i], w.y[i]
	local moving = r(w, 100) < 40
	w.vx[i] = moving and (r(w, 512) - 256) or 0
	w.vy[i] = moving and (r(w, 512) - 256) or 0
	w.frame[i] = r(w, M.FRAMES)
	w.flip[i] = r(w, 2) == 1
	w.row[i] = r(w, 13)
	w.dest[i] = r(w, 100) < 20
	w.elevation[i] = r(w, 100) < 10 and 12 or 4
	w.live = w.live + 1
	w.spawned[#w.spawned + 1] = i
end

local function despawn(w, i)
	w.alive[i] = false
	w.live = w.live - 1
	w.despawned[#w.despawned + 1] = i
end

-- Fills the world to `count` live images.
function M.populate(w, count)
	for i = 1, count do spawn(w, i) end
	w.spawned = {}
	M.sort(w)
end

-- Native depth key: elevation <= 4 sorts by y; the image index breaks ties.
local function depth_key(w, i)
	local key = w.elevation[i] * 8192
	if w.elevation[i] <= 4 then key = key + math.floor(w.y[i] / 256) % 8192 end
	return key * 16384 + i
end

function M.sort(w)
	local order, keys, n = w.order, {}, 0
	for i = 1, w.capacity do
		if w.alive[i] then
			n = n + 1
			order[n] = i
			keys[i] = depth_key(w, i)
		end
	end
	for k = n + 1, #order do order[k] = nil end
	table.sort(order, function(a, b) return keys[a] < keys[b] end)
end

function M.tick(w)
	w.spawned, w.despawned = {}, {}
	w.tick = w.tick + 1
	for i = 1, w.capacity do
		if w.alive[i] then
			w.px[i], w.py[i] = w.x[i], w.y[i]
			local x, y = w.x[i] + w.vx[i], w.y[i] + w.vy[i]
			if x < 0 or x >= M.WORLD_W * 256 then w.vx[i] = -w.vx[i]; x = w.x[i] end
			if y < 0 or y >= M.WORLD_H * 256 then w.vy[i] = -w.vy[i]; y = w.y[i] end
			w.x[i], w.y[i] = x, y
			w.frame[i] = (w.frame[i] + 1) % M.FRAMES
		end
	end
	local churn = math.floor(w.live * 0.02)
	local removed = 0
	for i = 1, w.capacity do
		if removed >= churn then break end
		if w.alive[i] and r(w, 50) == 0 then despawn(w, i); removed = removed + 1 end
	end
	for _, i in ipairs(w.despawned) do spawn(w, i) end
	M.sort(w)
end

return M
