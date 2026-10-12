-- Runs the IScript trace scenario (tests/lua/iscript_trace.lua) on generated/data.
-- Usage (repository root): luajit tools/lua/iscript_trace.lua [image [animation [frames]]]
--   without arguments: every image animation, one line per case "image anim frames hash [fault]";
--   with an image and animation: the per-frame hashes and integers of that case.
package.path = "./?.lua;" .. package.path
local trace = require("tests.lua.iscript_trace")
local dat = require("game.data.dat")
local grpmeta = require("game.data.grpmeta")
local scripts = require("game.data.scripts")
local bytes = require("game.data.bytes")

local function read(path)
	local f = assert(io.open(path, "rb"))
	local d = f:read("*a")
	f:close()
	return d
end

local data = {
	dat = dat.parse(read("generated/data/dat.kpd"), "dat.kpd"),
	grp = grpmeta.parse(read("generated/data/grpmeta.kpd"), "grpmeta.kpd"),
	iscript = scripts.parse_iscript(read("generated/data/iscript.kpd"), "iscript.kpd"),
}

-- Animation slots per header type (defs/iscript, header_animations).
local SLOTS = { [0] = 2, [1] = 2, [2] = 4, [12] = 14, [13] = 14, [14] = 16, [15] = 16, [20] = 22, [21] = 22,
	[23] = 24, [24] = 26, [26] = 28, [27] = 28, [28] = 28, [29] = 28 }

local function cases()
	local out = {}
	local code = data.iscript.code
	for id = 0, 998 do
		local h = data.iscript.headers[data.dat.images.iscript_id[id]]
		if h then
			local n = SLOTS[bytes.u8(code, h + 4)]
			for a = 0, n - 1 do
				if bytes.u16(code, h + 8 + 2 * a) ~= 0 then out[#out + 1] = { id, a } end
			end
		end
	end
	return out
end

local image, anim, frames = tonumber(arg[1]), tonumber(arg[2]), tonumber(arg[3])
if image then
	local r = trace.run(data, image, anim or 0, frames, true)
	for f, values in ipairs(r.values) do
		print(("frame %d hash %d: %s"):format(f - 1, r.per_frame[f], table.concat(values, " ")))
	end
	print("fault:", r.fault)
else
	local t0 = os.clock()
	local faults = 0
	for _, c in ipairs(cases()) do
		local r = trace.run(data, c[1], c[2], frames)
		if r.fault then faults = faults + 1 end
		print(("%d %d %d %d %s"):format(c[1], c[2], r.frames, r.hash, r.fault or ""))
	end
	io.stderr:write(("%d faults, %.1f s\n"):format(faults, os.clock() - t0))
end
