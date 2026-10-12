-- Static map objects for the viewer (plan §7 phase 1 item 7): preplaced units (UNIT), THG2
-- doodad sprites and units, each as its main image (plus the turret of units with a
-- subunit) in the static pose of presentation/static_pose.lua, placed by the native frame
-- bounds rule (0x40ee70, docs/research/native-image-geometry.md) and drawn through the
-- mesh_texture backend, one batch per atlas.
--
-- Viewer scope: only normal draw functions (images.dat 0 and 1) are drawn; shadows, blends
-- and other effects are phase 2 materials. Player colours use the remap rows of
-- palettes.kpd tunit (row = owner + 1; row 0 is the identity).
local bytes = require("game.data.bytes")
local grpmeta_data = require("game.data.grpmeta")
local unit_records = require("game.data.unit_records")
local pose = require("presentation.static_pose")
local world = require("presentation.world")

local u16 = bytes.u16

local M = {}

M.NO_SUBUNIT = 228
M.THG2_SPRITE = 0x1000
M.DRAWN_FUNCTIONS = { [0] = true, [1] = true }


local function add_image(out, ctx, image, x, y, owner, direction, elevation, order)
	local d, g = ctx.dat, ctx.grpmeta
	if not M.DRAWN_FUNCTIONS[d.images.draw_function[image]] then return end
	local entry_index = g.grp_of_tbl[d.images.grp_file[image]]
	if not entry_index then return end
	local entry = g.grps[entry_index]
	local header = ctx.iscript.headers[d.images.iscript_id[image]]
	local frame, flip = 0, false
	if header then
		frame, flip = pose.resolve(ctx.iscript.code, header, direction, d.images.gfx_turns[image], ctx.tileset)
	end
	if frame >= entry.frames then frame = 0 end
	local fx, fy, fw, fh = grpmeta_data.frame(g, entry_index, frame)
	if fw == 0 or fh == 0 then return end
	local half_w, half_h = math.floor(entry.width / 2), math.floor(entry.height / 2)
	local left = flip and (x + half_w - fw - fx) or (x + fx - half_w)
	local top = y + fy - half_h
	local batch = out[entry.atlas] or {}
	out[entry.atlas] = batch
	batch[#batch + 1] = {
		x = left + fw / 2, y = world.world_y(top + fh / 2), w = fw, h = fh, animation = entry.animation, frame = frame,
		flip = flip, row = owner + 1, elevation = elevation, map_y = y, order = order,
	}
end

local function add_unit(out, ctx, unit_type, x, y, owner, order)
	local d = ctx.dat
	if unit_type >= M.NO_SUBUNIT then return end
	local direction = d.units.unit_direction[unit_type] % 32
	local elevation = d.units.elevation_level[unit_type]
	local sprite = d.flingy.sprite[d.units.graphics[unit_type]]
	add_image(out, ctx, d.sprites.image[sprite], x, y, owner, direction, elevation, order)
	local sub = d.units.subunit1[unit_type]
	if sub ~= M.NO_SUBUNIT then
		local sub_sprite = d.flingy.sprite[d.units.graphics[sub]]
		add_image(out, ctx, d.sprites.image[sub_sprite], x, y, owner, direction, elevation + 1, order + 0.5)
	end
end

-- ctx: { dat =, grpmeta = (game.data.grpmeta.parse), iscript = (game.data.scripts.parse_iscript),
-- tileset = era & 7 }. Returns batches keyed by atlas index (grpmeta.atlases),
-- each sorted in depth order (elevation, then map y, then record order).
function M.build(mission, ctx)
	local out, order = {}, 0
	for i = 1, unit_records.count(mission.units) do
		local r = unit_records.record(mission.units, i)
		if r.type ~= unit_records.START_LOCATION then
			order = order + 1
			add_unit(out, ctx, r.type, r.x, r.y, r.owner, order)
		end
	end
	local s = mission.sprites
	for at = 0, #s - 10, 10 do
		local id, x, y, owner, flags = u16(s, at), u16(s, at + 2), u16(s, at + 4), s:byte(at + 7), u16(s, at + 8)
		order = order + 1
		if flags % (2 * M.THG2_SPRITE) >= M.THG2_SPRITE then
			add_image(out, ctx, ctx.dat.sprites.image[id], x, y, owner, 0, 0, order)
		else
			add_unit(out, ctx, id, x, y, owner, order)
		end
	end
	for _, batch in pairs(out) do
		table.sort(batch, function(a, b)
			if a.elevation ~= b.elevation then return a.elevation < b.elevation end
			if a.map_y ~= b.map_y then return a.map_y < b.map_y end
			return a.order < b.order
		end)
	end
	return out
end

return M
