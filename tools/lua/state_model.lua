-- The normalized state model of a game (the format of tools/native_state.py): one line per
-- record, `<category> <key> <field>=<value> ...` with the fields in sorted order, so the
-- original's dumps and Koprulu's state compare line by line and hash the same way.
--
-- Categories: global, list, unit, order, order_free, sprite, image, counter, pstat,
-- spatial, player, guard, tiles. EXCLUDED lists fields that the comparison leaves out, each
-- with its reason (docs/phase3.md).
local core = require("game.images.core")
local dat = require("game.data.dat")
local grpmeta = require("game.data.grpmeta")
local int = require("game.simulation.int")
local mission_data = require("game.data.mission")
local rng = require("game.simulation.rng")
local scripts = require("game.data.scripts")
local start = require("game.simulation.start")
local terrain = require("game.data.terrain")
local unit = require("game.units.unit")

local M = {}

M.CATEGORIES = { "global", "list", "unit", "order", "order_free", "sprite", "image", "counter", "pstat",
	"spatial", "player", "guard", "tiles" }

-- Fields the model does not compare yet: name -> reason.
M.EXCLUDED = {
	unit = {
		path = "path records arrive with movement (3.4); always 0 at the first step",
	},
}

local NONE = core.NONE

local function line(out, category, key, fields)
	local names = {}
	for name in pairs(fields) do
		if not (M.EXCLUDED[category] and M.EXCLUDED[category][name]) then names[#names + 1] = name end
	end
	table.sort(names)
	local parts = { category, tostring(key) }
	for _, name in ipairs(names) do parts[#parts + 1] = name .. "=" .. tostring(fields[name]) end
	out[#out + 1] = table.concat(parts, " ")
end

local function chain(head, next_of, none)
	local out, n = {}, head
	while n ~= none do
		out[#out + 1] = n
		n = next_of[n]
	end
	return out
end

local RESOURCE_CONTAINER = 0x2000
local UNION_WORDS = { "union_0xc0", "union_0xc4", "union_0xc8", "union_0xcc", "union_0xd0", "union_0xd4",
	"union_0xd8" }

local function unit_fields(g, n)
	local u = g.u
	local f = {}
	for _, name in ipairs(unit.FIELDS) do
		if name ~= "prev" and name ~= "next" and name ~= "union_units" then f[name] = u[name][n] end
	end
	for k, name in ipairs(UNION_WORDS) do
		if int.band(u.union_units[n], 2 ^ (k - 1)) ~= 0 then f[name] = "u" .. u[name][n] end
	end
	f.ai = u.ai[n] ~= 0 and ("g" .. (u.ai[n] - 1)) or 0
	if int.band(g.dat.units.special_ability_flags[u.type[n]], RESOURCE_CONTAINER) ~= 0 then
		-- TODO: resource area tags of GenerateResourceAreas 0x497a20 (byte +0xd8 of resources);
		-- the generator needs the region graph and connected distances of phase 3.4.
		f.union_0xd8 = nil
	end
	return f
end

M.DATA = "generated/data/"

-- Starts a generated mission (path of its .kpm) with the game's initial seed and expansion
-- byte; read(path) returns a file's bytes. Returns the game at its first object step.
function M.start_mission(read, path, seed, expansion)
	local mission = mission_data.parse(read(path), path)
	local r = rng.game(seed)
	r:seed(seed)
	r:set_enabled(1)
	return start.start({
		dat = dat.parse(read(M.DATA .. "dat.kpd"), "dat.kpd"),
		grp = grpmeta.parse(read(M.DATA .. "grpmeta.kpd"), "grpmeta.kpd"),
		iscript = scripts.parse_iscript(read(M.DATA .. "iscript.kpd"), "iscript.kpd"),
		tileset = terrain.parse_tileset(read(M.DATA .. "tilesets/" .. terrain.TILESETS[mission.tileset] .. ".kpd"),
			"tileset"),
		mission = mission, rng = r, expansion = expansion,
	})
end

-- CRC-32 of each category's lines joined by newlines (tools/state_check.py golden_lines).
function M.crcs(lines)
	local crc32 = require("game.data.crc32")
	local by = {}
	for _, l in ipairs(lines) do
		local c = l:match("^(%S+)")
		by[c] = by[c] or {}
		by[c][#by[c] + 1] = l
	end
	local out = {}
	for c, ls in pairs(by) do out[c] = crc32.of(table.concat(ls, "\n")) end
	return out
end

-- Returns the model lines of game g.
function M.lines(g)
	local out = {}
	local u, w, o = g.u, g.img, g.orders
	line(out, "global", 0, {
		rng = g.rng.state, units_allocated = g.units_allocated, orders_allocated = o.allocated,
		spatial_count = g.spatial.count,
		active_tail = g.lists.active.tail, pending_tail = g.lists.pending.tail, free_tail = g.lists.free.tail,
		hidden_tail = g.lists.hidden.tail, special_tail = g.lists.special.tail,
	})
	local lists = {}
	for _, name in ipairs({ "active", "pending", "free", "hidden", "special" }) do
		lists[name] = chain(g.lists[name].head, u.next, 0)
		line(out, "list", name, { units = table.concat(lists[name], ",") })
	end
	for p = 0, 11 do
		line(out, "list", "player" .. p, { units = table.concat(chain(g.player_units[p], u.player_next, 0), ",") })
	end
	local orders = {}
	for _, name in ipairs({ "active", "hidden", "special", "pending" }) do
		for _, n in ipairs(lists[name]) do
			local f = unit_fields(g, n)
			f.list = name
			line(out, "unit", n, f)
			local k = 0
			for _, node in ipairs(chain(u.queue_head[n], o.next, NONE)) do
				orders[#orders + 1] = { n .. "." .. k, { node = node, order = o.order[node], type = o.type[node],
					x = o.x[node], y = o.y[node], unit = o.unit[node] } }
				k = k + 1
			end
		end
	end
	for _, r in ipairs(orders) do line(out, "order", r[1], r[2]) end
	line(out, "order_free", 0, { nodes = table.concat(chain(o.free.head, o.next, NONE), ",") })
	local images = {}
	for r = 0, core.ROWS - 1 do
		for _, s in ipairs(chain(w.rows[r].head, w.spr.next, NONE)) do
			local ims = chain(w.spr.images[s].head, w.img.next, NONE)
			line(out, "sprite", s, { id = w.spr.id[s], owner = w.spr.owner[s], selection = w.spr.selection[s],
				visibility = w.spr.visibility[s], elevation = w.spr.elevation[s], flags = w.spr.flags[s],
				sel_timer = w.spr.sel_timer[s], index = w.spr.index[s], width = w.spr.width[s],
				height = w.spr.height[s], x = int.s16(w.spr.x[s]), y = int.s16(w.spr.y[s]), row = r,
				main = w.spr.main[s], images = table.concat(ims, ",") })
			for _, i in ipairs(ims) do images[#images + 1] = i end
		end
	end
	local im = w.img
	for _, i in ipairs(images) do
		line(out, "image", i, { id = im.id[i], method = im.method[i], dir = im.dir[i], flags = im.flags[i],
			xoff = int.s8(im.xoff[i]), yoff = int.s8(im.yoff[i]), header = im.header[i], pc = im.pc[i],
			ret = im.ret[i], anim = im.anim[i], wait = im.wait[i], frameset = im.frameset[i], frame = im.frame[i],
			x = int.s16(im.x[i]), y = int.s16(im.y[i]), sprite = im.sprite[i] })
	end
	for _, c in ipairs({ { "all", g.count_all }, { "completed", g.count_completed }, { "kills", g.count_kills },
		{ "deaths", g.count_deaths } }) do
		for t = 0, 227 do
			local row, any = {}, false
			for p = 0, 11 do
				row[p + 1] = c[2][t * 12 + p]
				if row[p + 1] ~= 0 then any = true end
			end
			if any then line(out, "counter", c[1] .. "." .. t, { players = table.concat(row, ",") }) end
		end
	end
	for a = 0, #g.pstat do
		local row = {}
		for p = 0, 11 do row[p + 1] = g.pstat[a][p] % 4294967296 end
		line(out, "pstat", string.format("0x%x", 0x5290c4 + 0x30 * a), { players = table.concat(row, ",") })
	end
	local sp = g.spatial
	for _, axis in ipairs({ { "x", sp.x_unit, sp.x_at }, { "y", sp.y_unit, sp.y_at } }) do
		local e = {}
		for k = 0, sp.count - 1 do e[#e + 1] = axis[2][k] .. ":" .. axis[3][k] end
		line(out, "spatial", axis[1], { entries = table.concat(e, ",") })
	end
	for p = 0, 11 do
		line(out, "player", p, { controller = g.players[p].controller, shared_vision = g.shared_vision[p] })
	end
	local gd = g.guards
	for p = 0, 7 do
		local recs = {}
		for _, k in ipairs(chain(gd.heads[p], gd.next, -1)) do
			recs[#recs + 1] = table.concat({ k, gd.kind[k], gd.unit[k], gd.type[k], gd.x1[k], gd.y1[k], gd.x2[k],
				gd.y2[k] }, ":")
		end
		line(out, "guard", p, { records = table.concat(recs, ",") })
	end
	line(out, "guard", "free", { head = gd.free })
	local rows = {}
	for ty = 0, g.map_h - 1 do
		local row = {}
		for tx = 0, g.map_w - 1 do row[#row + 1] = string.format("%08x", g.tiles[ty * g.map_w + tx] % 4294967296) end
		rows[#rows + 1] = table.concat(row)
	end
	for ty, r in ipairs(rows) do line(out, "tiles", ty - 1, { flags = r }) end
	return out
end

return M
