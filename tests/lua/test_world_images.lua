-- presentation/backend/world_images.lua with the Defold buffer, resource and game object
-- calls stubbed for the duration of each test: slot writes, released slots, full families
-- and the upload on the frame after a write.
local S = {}

local STUBBED = { "buffer", "resource", "go", "vmath", "graphics" }

-- Runs fn(uploads) with stubs in place of the Defold modules the backend calls; uploads()
-- counts resource.set_texture calls. The originals (nil outside Defold) are restored.
local function with_defold_stubs(fn)
	local saved = {}
	for _, name in ipairs(STUBBED) do saved[name] = rawget(_G, name) end
	local saved_hash = rawget(_G, "hash")
	local uploads = 0
	rawset(_G, "buffer", {
		VALUE_TYPE_FLOAT32 = "float32", VALUE_TYPE_UINT8 = "uint8",
		create = function(count, format)
			local b = { streams = {} }
			for _, st in ipairs(format) do
				local t = {}
				for k = 1, count * st.count do t[k] = 0 end
				b.streams[st.name] = t
			end
			return b
		end,
		get_stream = function(b, name) return b.streams[name] end,
	})
	rawset(_G, "resource", {
		create_texture = function(path) return path end,
		set_texture = function() uploads = uploads + 1 end,
		set_buffer = function() end,
		release = function() end,
	})
	rawset(_G, "go", { get = function() return "vertices" end, set = function() end })
	rawset(_G, "vmath", { vector4 = function(x, y, z, w) return { x = x, y = y, z = z, w = w } end })
	rawset(_G, "graphics", { TEXTURE_TYPE_2D = 1, TEXTURE_FORMAT_RGBA32F = 2, TEXTURE_FORMAT_RGBA = 3 })
	if not saved_hash then rawset(_G, "hash", function(s) return s end) end
	local ok, err = pcall(fn, function() return uploads end)
	for _, name in ipairs(STUBBED) do rawset(_G, name, saved[name]) end
	rawset(_G, "hash", saved_hash)
	if not ok then error(err, 0) end
end

local EMPTY = 7

local function backend()
	local world_images = require("presentation.backend.world_images")
	return world_images, world_images.new({ tag = "test", empty = EMPTY, families = {
		{ name = "overwrite", mesh = "images", capacity = 2 },
		{ name = "dest", mesh = "images_dest", capacity = 1 },
	} })
end

-- A virtual image (health bar) carries its frame size in the layer field; once its slot is
-- freed the slot must not keep that size, or world.vp would still draw it.
function S.released_slots_draw_nothing(T)
	with_defold_stubs(function()
		local world_images, b = backend()
		local size = 19 * 256 + 5
		b:begin()
		T.ok(b:put("overwrite", 3, 5000, 10, 20, 10, 20, EMPTY, false, 1, 4, size))
		b:finish({}, 0, 0)
		T.eq(b.data[8], size)
		b:begin() -- the bar is not drawn this tick
		b:finish({}, 0, 0)
		T.eq(b.data[5], EMPTY * world_images.KINDS * 2)
		T.eq(b.data[8], 0)
		b:destroy()
	end)
end

function S.full_family_leaves_images_out(T)
	with_defold_stubs(function()
		local _, b = backend()
		b:begin()
		T.ok(b:put("dest", 0, 1, 0, 0, 0, 0, 1, false, 1, 0, 1))
		T.eq(b:put("dest", 0, 2, 0, 0, 0, 0, 1, false, 2, 0, 1), false)
		T.ok(b:put("overwrite", 0, 2, 0, 0, 0, 0, 1, false, 3, 0, 0)) -- other families unaffected
		b:finish({}, 0, 0)
		b:destroy()
	end)
end

function S.uploads_on_the_frame_after_a_write(T)
	with_defold_stubs(function(uploads)
		local _, b = backend()
		b:begin()
		b:put("overwrite", 0, 1, 8, 8, 4, 8, 1, false, 1, 0, 0)
		local windows = { { rank = 1, last = 1, layers = 0 } }
		b:finish(windows, 1, 0)
		T.eq(b.plan.count, 0) -- the uploaded data has no plan yet
		T.eq(b:sync(0.25), 1) -- write frame: the texture still holds the previous tick
		T.eq(uploads(), 0)
		T.eq(b:sync(0.25), 0.25)
		T.eq(uploads(), 1)
		T.eq(b.plan.count, 1)
		b:begin()
		b:put("overwrite", 0, 1, 8, 8, 4, 8, 1, false, 1, 0, 0) -- unchanged: nothing to upload
		b:finish(windows, 1, 0)
		T.eq(b:sync(0.5), 0.5)
		T.eq(uploads(), 1)
		b:destroy()
	end)
end

return S
