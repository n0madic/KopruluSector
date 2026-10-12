-- Viewer scene placement with stub tables (presentation/viewer_scene.lua).
local scene = require("presentation.viewer_scene")

local function w(v) return string.char(v % 256, math.floor(v / 256) % 256) end
local function dw(v) return w(v % 65536) .. w(math.floor(v / 65536)) end

-- iscript: header at 4 (type 0), Init at 20: playfram 0; wait 1
local code = string.rep("\0", 4) .. "SCPE" .. string.char(0, 0, 0, 0) .. w(20) .. w(0)
code = code .. string.rep("\0", 20 - #code) .. string.char(0x00) .. w(0) .. string.char(0x05, 1)

local function ctx()
	local dat = {
		units = { unit_direction = { [0] = 0, [1] = 20 }, elevation_level = { [0] = 4, [1] = 4 },
			graphics = { [0] = 0, [1] = 1, [2] = 2 }, subunit1 = { [0] = 228, [1] = 2, [2] = 228 } },
		flingy = { sprite = { [0] = 0, [1] = 1, [2] = 2 } },
		sprites = { image = { [0] = 0, [1] = 1, [2] = 2, [9] = 3 } },
		images = { draw_function = { [0] = 0, [1] = 0, [2] = 0, [3] = 10 }, grp_file = { [0] = 1, [1] = 1, [2] = 1, [3] = 1 },
			iscript_id = { [0] = 0, [1] = 0, [2] = 0, [3] = 0 }, gfx_turns = { [0] = 0, [1] = 1, [2] = 1, [3] = 0 } },
	}
	local grpmeta = { grp_of_tbl = { [1] = 1 }, FRAME_RECORD = 8,
		grps = { { frames = 32, width = 64, height = 48, atlas = 1, animation = "unit", first_frame = 0 } } }
	-- every frame: x 10, y 6, 20 x 30
	local frames = {}
	for _ = 1, 32 do frames[#frames + 1] = string.char(10, 6, 20, 30) .. string.rep("\0", 4) end
	grpmeta.frms = table.concat(frames)
	return { dat = dat, grpmeta = grpmeta, iscript = { code = code, headers = { [0] = 4 } }, tileset = 0 }
end

local function record(t, x, y, owner)
	return dw(0) .. w(x) .. w(y) .. w(t) .. w(0) .. w(0) .. w(0) .. string.char(owner, 0, 0, 0) .. dw(0) .. w(0)
		.. w(0) .. dw(0) .. dw(0)
end

local S = {}

function S.placement_and_turret(T)
	local mission = { units = record(0, 100, 200, 2) .. record(1, 300, 50, 0) .. record(214, 1, 1, 0),
		sprites = w(9) .. w(40) .. w(40) .. string.char(0, 0) .. w(0x1000) }
	local out = scene.build(mission, ctx())
	local batch = out[1]
	T.eq(#batch, 3) -- unit 0, unit 1 and its turret; start location skipped; shadow-method sprite skipped
	-- depth: elevation first (the turret, elevation 5, draws last), then map y (unit 1 at y 50 first)
	local a, b, turret = batch[1], batch[2], batch[3]
	-- unit 1 direction 20: frame 32 - 20 = 12, mirrored: left = x + 32 - 20 - 10
	T.eq(a.frame, 12)
	T.eq(a.flip, true)
	T.eq(a.x, 300 + 32 - 20 - 10 + 10)
	T.eq(a.y, 8192 - (50 + 6 - 24 + 15))
	T.eq(turret.elevation, 5)
	-- unit 0: no turns, frame 0, left = x + 10 - 32, top = y + 6 - 24
	T.eq(b.frame, 0)
	T.eq(b.flip, false)
	T.eq(b.x, 100 + 10 - 32 + 10)
	T.eq(b.y, 8192 - (200 + 6 - 24 + 15))
	T.eq(b.row, 3)
end

return S
