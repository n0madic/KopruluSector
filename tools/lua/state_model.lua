-- The normalized state model of a game (the format of tools/native_state.py): one line per
-- record, `<category> <key> <field>=<value> ...` with the fields in sorted order, so the
-- original's dumps and Koprulu's state compare line by line and hash the same way.
--
-- Categories: global, list, unit, order, order_free, path, path_free, density, sprite, image, counter, pstat,
-- spatial, player, step, selection, guard, tiles, terrain. EXCLUDED lists fields that the comparison leaves out, each
-- with its reason (docs/phase3.md).
local core = require("game.images.core")
local dat = require("game.data.dat")
local grpmeta = require("game.data.grpmeta")
local int = require("game.simulation.int")
local mission_data = require("game.data.mission")
local pool = require("game.pathing.pool")
local rng = require("game.simulation.rng")
local scripts = require("game.data.scripts")
local start = require("game.simulation.start")
local terrain = require("game.data.terrain")
local unit = require("game.units.unit")

local M = {}

M.CATEGORIES = { "global", "list", "unit", "order", "order_free", "path", "path_free", "density", "sprite", "image",
	"counter", "pstat", "spatial", "player", "step", "selection", "guard", "tiles", "terrain" }

-- Fields the model does not compare yet: name -> reason.
M.EXCLUDED = {}

local NONE = core.NONE

local function line(out, category, key, fields)
	local names = {}
	local drawn = out.drawn and M.DRAWN[category]
	for name in pairs(fields) do
		if not (M.EXCLUDED[category] and M.EXCLUDED[category][name]) and not (drawn and drawn[name]) then
			names[#names + 1] = name
		end
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

-- The header, points and region ids of path record k (tools/native_state.py PATH).
local function path_fields(g, k)
	local f = {
		record = k, frame = g:path_u32(k, pool.FRAME), blocker = g:path_u32(k, pool.BLOCKER),
		speed = g:path_u32(k, pool.SPEED), flags = g:path_u8(k, pool.FLAGS),
		retry = g:path_u8(k, pool.RETRY), slide = g:path_u8(k, pool.SLIDE),
		full_regions = g:path_u8(k, pool.FULL_REGIONS), regions = g:path_s8(k, pool.REGIONS),
		region_index = g:path_s8(k, pool.REGION_INDEX), points = g:path_s8(k, pool.POINTS),
		point_index = g:path_s8(k, pool.POINT_INDEX),
	}
	f.src_x, f.src_y = g:path_point(k, pool.SRC)
	f.next_x, f.next_y = g:path_point(k, pool.NEXT)
	f.dest_x, f.dest_y = g:path_point(k, pool.DEST)
	local pts, regions = {}, {}
	for i = 0, f.points - 1 do
		local x, y = g:path_point(k, pool.PAYLOAD + 4 * i)
		pts[#pts + 1] = x .. ":" .. y
	end
	local at = pool.PAYLOAD + 4 * math.max(f.points, 0)
	for i = 0, f.regions - 1 do regions[#regions + 1] = g:path_u16(k, at + 2 * i) end
	f.point_list, f.region_list = table.concat(pts, ","), table.concat(regions, ",")
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
	local crt = rng.crt(1)
	crt:srand(seed)
	return start.start({
		dat = dat.parse(read(M.DATA .. "dat.kpd"), "dat.kpd"),
		grp = grpmeta.parse(read(M.DATA .. "grpmeta.kpd"), "grpmeta.kpd"),
		iscript = scripts.parse_iscript(read(M.DATA .. "iscript.kpd"), "iscript.kpd"),
		tileset = terrain.parse_tileset(read(M.DATA .. "tilesets/" .. terrain.TILESETS[mission.tileset] .. ".kpd"),
			"tileset"),
		mission = mission, rng = r, crt = crt, expansion = expansion,
	})
end

-- A run of game g from its start, advanced to the entry of an object step (1 = the start
-- itself). Steps 1 and 2 are PrimeGameLoop's; then RunGame runs the first turn, and each
-- game loop iteration (game/simulation/step.lua run_frame) the interface events of the
-- current frame, the turn, the frame count and the steps; a step's dump is taken after the
-- count. commands: { { turn =, player =, bytes = }, ... } and ui: { { frame =, select =
-- { units } } or { frame =, cursor = { x, y } }, ... } in log order (the emulator's
-- commands.jsonl and ui.jsonl).
function M.runner(g, commands, ui)
	commands, ui = commands or {}, ui or {}
	local run = { g = g, step = 1, next_command = 1, next_ui = 1 }
	local function turn()
		local list = {}
		while commands[run.next_command] and commands[run.next_command].turn <= g.turn do
			list[#list + 1] = commands[run.next_command]
			run.next_command = run.next_command + 1
		end
		g:run_turn(list)
	end
	local function loop_head()
		while ui[run.next_ui] and ui[run.next_ui].frame <= g.frame do
			local e = ui[run.next_ui]
			if e.select then
				g:select_locally(e.select)
			else
				g:move_placement_cursor(e.cursor[1], e.cursor[2])
			end
			run.next_ui = run.next_ui + 1
		end
		turn()
		g.frame = g.frame + 1
	end
	function run.to(step)
		while run.step < step do
			if run.step == 1 then
				g:step_objects()
			elseif run.step == 2 then
				g:step_objects()
				turn()
				loop_head()
			else
				g:step_frame()
				loop_head()
			end
			run.step = run.step + 1
		end
		return g
	end
	return run
end

function M.advance(g, step, commands, ui)
	return M.runner(g, commands, ui).to(step)
end

-- Parses the emulator's commands.jsonl text.
function M.parse_commands(text)
	local out = {}
	for line in text:gmatch("[^\n]+") do
		local frame = tonumber(line:match('"frame":%s*(%d+)'))
		local turn = tonumber(line:match('"turn":%s*(%d+)'))
		local player = tonumber(line:match('"player":%s*(%d+)'))
		local hex = line:match('"bytes":%s*"(%x*)"')
		out[#out + 1] = { frame = frame, turn = turn, player = player,
			bytes = (hex:gsub("%x%x", function(h) return string.char(tonumber(h, 16)) end)) }
	end
	return out
end

-- Parses the emulator's ui.jsonl text (local selections and right-click markers).
function M.parse_ui(text)
	local out = {}
	for line in text:gmatch("[^\n]+") do
		local e = { frame = tonumber(line:match('"frame":%s*(%d+)')) }
		local units = line:match('"select":%s*%[([%d,%s]*)%]')
		if units then
			e.select = {}
			for n in units:gmatch("%d+") do e.select[#e.select + 1] = tonumber(n) end
		else
			local x, y = line:match('"cursor":%s*%[%s*(%-?%d+),%s*(%-?%d+)%s*%]')
			e.cursor = { tonumber(x), tonumber(y) }
		end
		out[#out + 1] = e
	end
	return out
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

-- Fields of the original that its drawing refreshes from object step 3 on (the image bounds
-- of the rows in view, 0x445de0); tools/state_check.py leaves them out there.
M.DRAWN_STEP = 3
M.DRAWN = { image = { x = true, y = true } }

-- Returns the model lines of game g; `drawn` leaves out the M.DRAWN fields.
function M.lines(g, drawn)
	local out = { drawn = drawn }
	local u, w, o = g.u, g.img, g.orders
	line(out, "global", 0, {
		rng = g.rng.state, units_allocated = g.units_allocated, orders_allocated = o.allocated,
		spatial_count = g.spatial.count,
		active_tail = g.lists.active.tail, pending_tail = g.lists.pending.tail, free_tail = g.lists.free.tail,
		hidden_tail = g.lists.hidden.tail, special_tail = g.lists.special.tail, cloaked_head = g.cloaked_head,
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
	for _, name in ipairs({ "active", "hidden", "special", "pending" }) do
		for _, n in ipairs(lists[name]) do
			if u.path[n] ~= 0 then line(out, "path", n, path_fields(g, u.path[n])) end
		end
	end
	local free, k = {}, g.path_free_head
	while k ~= 0 do
		free[#free + 1] = k
		k = g:path_u32(k, 0)
	end
	line(out, "path_free", 0, { records = table.concat(free, ",") })
	local cells = {}
	for c, v in pairs(g.density) do
		if v ~= 0 then cells[#cells + 1] = c end
	end
	table.sort(cells)
	for i, c in ipairs(cells) do cells[i] = c .. ":" .. g.density[c] end
	line(out, "density", 0, { cells = table.concat(cells, ",") })
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
		local alliance = {}
		for q = 0, 11 do alliance[#alliance + 1] = string.format("%02x", g.alliance[p][q]) end
		line(out, "player", p, { controller = g.players[p].controller, force = g.players[p].force,
			alliance = table.concat(alliance), shared_vision = g.shared_vision[p] })
	end
	local ai, clock, recede = g.ai, g.trigger_clock, g.recede
	local buckets = {}
	for b = 0, 8 do buckets[#buckets + 1] = string.format("%02x", recede.buckets[b]) end
	line(out, "step", 0, {
		tile_countdown = g.tile_countdown % 4294967296, order_spread = g.order_spread,
		secondary_spread = g.secondary_spread, recede_timer = recede.timer, recede_free = recede.free,
		recede_retry = recede.retry, recede_buckets = table.concat(buckets), ai_attack_flag = ai.attack_flag,
		ai_period = ai.period % 4294967296, ai_cycle = ai.cycle, ai_rounds = ai.rounds,
		ai_region_player = ai.region_player, ai_region_cursor = ai.region_cursor,
		ai_spending_player = ai.spending_player, ai_town_player = ai.town_player, hit_slot = g.hit_slot,
		power_dirty = g.power_dirty and 1 or 0, elapsed = g.elapsed, clock_second = clock.second,
		clock_pass = clock.pass, clock_result = clock.result, clock_leaderboard = clock.leaderboard,
		turn = g.turn,
	})
	local function list12(t)
		local out = {}
		for k = 0, 11 do out[#out + 1] = t[k] end
		return table.concat(out, ",")
	end
	line(out, "selection", "local", { units = list12(g.local_selection) })
	for p = 0, 7 do
		local f = { units = list12(g.selections[p]) }
		local stamps = {}
		for k = 0, 7 do stamps[#stamps + 1] = g.recent_stamps[p][k] end
		f.recent = table.concat(stamps, ",")
		for k = 0, 17 do
			local group = g.groups[p][k]
			local any = false
			for j = 0, 11 do if group[j] ~= 0 then any = true end end
			if any then f["group" .. k] = list12(group) end
		end
		line(out, "selection", p, f)
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
	for ty = 0, g.map_h - 1 do
		local mtxm, backup, border, display = {}, {}, {}, {}
		for tx = 0, g.map_w - 1 do
			local i = ty * g.map_w + tx
			mtxm[#mtxm + 1] = string.format("%04x", g.raw_tiles[i])
			backup[#backup + 1] = string.format("%04x", g.creep_backup[i])
			border[#border + 1] = string.format("%02x", g.creep_border[i])
			display[#display + 1] = string.format("%04x", g.megatiles[i])
		end
		line(out, "terrain", ty, { mtxm = table.concat(mtxm), backup = table.concat(backup),
			border = table.concat(border), display = table.concat(display) })
	end
	return out
end

return M
