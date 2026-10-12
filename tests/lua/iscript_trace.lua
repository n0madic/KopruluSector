-- IScript trace scenario shared with the original-code probe
-- (tools/research/probe_native_iscript_117.py): one image animation run on a lone sprite
-- without unit, flingy or bullet context, every frame stepped by UpdateThingies.
--
-- Case (image id, animation):
--   1. new-game pools (InitImagePools main pool, InitSpritePool, InitThingyPools with the
--      placement cursor);
--   2. lone thingy of sprite ROOT_SPRITE whose sprites.dat image is temporarily the case's
--      image, at ROOT_X/ROOT_Y, owner = image id % 8, tileset = image id % 8;
--   3. for animation > 0, PlaySpriteAnimation(animation, force);
--   4. FRAMES calls of UpdateThingies.
-- After step 3 and after every step the state is folded into a 32-bit hash
-- h = (h * 31 + v) mod 2^32 over the integers below (signed values as two's complement):
--   events of the step: sound (SOUND, id, x, y), unit fit check (FIT, type, x, y);
--   frame, rng state, rng total, rng source checksum (sum of counter[i] * (i + 1) mod 2^32), free heads (images, sprites, lone thingies),
--   per active lone thingy (head to tail): thingy, sprite, sprite fields, image count,
--   per image (head to tail): key, IMAGE_FIELDS with flags & ~1 (no dirty bit).
-- A raised error ends the case: the frame counts as faulted.
local world = require("game.images.world")
local rng = require("game.simulation.rng")

local M = {}

M.FRAMES = 80
M.ROOT_SPRITE = 0
M.ROOT_X, M.ROOT_Y = 4096, 4096
M.MAP = 256
M.SEED = 0x2A2A2A2A
M.SOUND, M.FIT = 0x534e44, 0x464954
M.TWO32 = 4294967296
-- Opcodes one VM run may execute before the case counts as hung (the probe bounds the
-- original code by instructions instead).
M.STEP_LIMIT = 100000

local SPRITE_FIELDS = { "id", "owner", "selection", "visibility", "elevation", "flags", "sel_timer", "index",
	"width", "height", "x", "y", "main" }
local IMAGE_FIELDS = { "id", "method", "dir", "flags", "xoff", "yoff", "header", "pc", "ret", "anim", "wait",
	"frameset", "frame", "x", "y", "param" }
M.SPRITE_FIELDS, M.IMAGE_FIELDS = SPRITE_FIELDS, IMAGE_FIELDS

local TWO32 = M.TWO32

local function fold(h, v)
	return (h * 31 + v % TWO32) % TWO32
end

-- Appends the integers of a frame to `out` (array) and returns it.
function M.frame_values(w, frame, events, out)
	out = out or {}
	for _, v in ipairs(events) do out[#out + 1] = v end
	local r = w.rng
	out[#out + 1] = frame
	out[#out + 1] = r.state
	out[#out + 1] = r.total
	local sources = 0
	for i = 0, 255 do sources = (sources + r.counters[i] * (i + 1)) % TWO32 end
	out[#out + 1] = sources
	out[#out + 1] = w.free_images.head
	out[#out + 1] = w.free_sprites.head
	out[#out + 1] = w.free_lone.head
	local thg, spr, img = w.thg, w.spr, w.img
	local t = w.active_lone.head
	while t ~= world.NONE do
		local s = thg.sprite[t]
		out[#out + 1] = t
		out[#out + 1] = s
		for _, f in ipairs(SPRITE_FIELDS) do out[#out + 1] = spr[f][s] end
		local count, i = 0, spr.images[s].head
		while i ~= world.NONE do count, i = count + 1, img.next[i] end
		out[#out + 1] = count
		i = spr.images[s].head
		while i ~= world.NONE do
			out[#out + 1] = i
			for _, f in ipairs(IMAGE_FIELDS) do
				local v = img[f][i]
				if f == "flags" then v = v - v % 2 end
				out[#out + 1] = v
			end
			i = img.next[i]
		end
		t = thg.next[t]
	end
	return out
end

function M.hash(values, h)
	h = h or 0
	for k = 1, #values do h = fold(h, values[k]) end
	return h
end

-- data: { dat =, grp =, iscript = } (parsed .kpd). Returns the case's world (one per
-- data set, reused: the pool initializers zero every record) and the event list its hooks
-- append to.
function M.new_world(data, image_id)
	local w, events = data.trace_world, data.trace_events
	if not w then
		events = {}
		w = world.new({
			dat = data.dat, grp = data.grp, iscript = data.iscript, map_width = M.MAP, map_height = M.MAP,
			sound = function(id, x, y)
				events[#events + 1] = M.SOUND; events[#events + 1] = id
				events[#events + 1] = x; events[#events + 1] = y
			end,
			hooks = {
				unit_fits = function(_, unit_type, x, y)
					-- The probe records the WORDs the original passes (EDX and the stack word).
					events[#events + 1] = M.FIT; events[#events + 1] = unit_type
					events[#events + 1] = x % 65536; events[#events + 1] = y % 65536
					return true
				end,
			},
		})
		data.trace_world, data.trace_events = w, events
	end
	for k = #events, 1, -1 do events[k] = nil end
	local g = rng.game(M.SEED)
	g:seed(M.SEED)
	g:set_enabled(1)
	w.rng = g
	w.tileset = image_id % 8
	w.step_limit = M.STEP_LIMIT
	return w, events
end

-- Raw iscript.bin -> { code =, headers = { [id] = header offset } } (the index walk of
-- SetImageScriptHeader 0x40f340).
function M.raw_iscript(code)
	local bytes = require("game.data.bytes")
	local headers, at = {}, bytes.u32(code, 0)
	while true do
		local id = bytes.u16(code, at)
		if id == 0xFFFF then break end
		headers[id] = bytes.u16(code, at + 2)
		at = at + 4
	end
	return { code = code, headers = headers }
end

-- Runs one case; `script_id` (optional) replaces the root image's images.dat IScript id
-- while its sprite is created (synthetic programs). Returns { frames = completed frames (after the setup frame 0), fault =
-- error message or nil, hash =, per_frame = { hash after each frame } when `detail`,
-- values = { per-frame integer arrays } when `detail` }.
function M.run(data, image_id, anim, frames, detail, script_id)
	frames = frames or M.FRAMES
	local w, events = M.new_world(data, image_id)
	local result = { frames = -1, hash = 0 }
	if detail then result.per_frame, result.values = {}, {} end
	local function record(frame)
		local values = M.frame_values(w, frame, events)
		for k = #events, 1, -1 do events[k] = nil end
		result.hash = M.hash(values, result.hash)
		result.frames = frame
		if detail then
			result.per_frame[#result.per_frame + 1] = result.hash
			result.values[#result.values + 1] = values
		end
	end
	local ok, err = pcall(function()
		w:init_pools()
		w:init_thingies()
		local sprites, images = data.dat.sprites, data.dat.images
		local saved, saved_script = sprites.image[M.ROOT_SPRITE], images.iscript_id[image_id]
		sprites.image[M.ROOT_SPRITE] = image_id
		if script_id then images.iscript_id[image_id] = script_id end
		local okc, t = pcall(w.lone_create, w, M.ROOT_SPRITE, M.ROOT_X, M.ROOT_Y, image_id % 8)
		sprites.image[M.ROOT_SPRITE], images.iscript_id[image_id] = saved, saved_script
		if not okc then error(t, 0) end
		if anim ~= 0 and t ~= world.NONE then w:sprite_play(w.thg.sprite[t], anim, true) end
		record(0)
		for f = 1, frames do
			w:update_thingies()
			record(f)
		end
	end)
	if not ok then result.fault = tostring(err) end
	return result
end

return M
