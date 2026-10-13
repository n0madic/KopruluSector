-- Unit references after slots are freed and reused (plan §7 phase 3 acceptance): at every
-- step of every scenario of tests/golden/oracle/scenarios/, the unit references held by live
-- units (targets, turrets, union words that hold units, queued order targets) and by the
-- selections name live units only; a packed reference (UnitToPackedRef 0x428bd0) to a unit
-- whose record was recycled and allocated again resolves to no unit. Needs the converter's
-- generated data and the native core (kp_core); skipped without them.
local native_only = require("tests.lua.native_only")
local oracle = require("tools.lua.oracle")

local DIR = "tests/golden/oracle/scenarios/"
local UNITS = 1700 -- the unit record pool
local REF_FIELDS = { "move_unit", "order_unit", "auto_target", "connected", "build_unit", "irradiated_by", "subunit" }
local UNION_WORDS = { "union_0xc0", "union_0xc4", "union_0xc8", "union_0xcc", "union_0xd0", "union_0xd4", "union_0xd8" }

local S = {}

local function load(T, name)
	return assert((loadstring or load)(T.read(DIR .. name .. ".lua")))()
end

local function commands_of(rows)
	local out = {}
	for _, r in ipairs(rows) do
		out[#out + 1] = { turn = r.turn, player = r.player,
			bytes = (r.bytes:gsub("%x%x", function(h) return string.char(tonumber(h, 16)) end)) }
	end
	return out
end

-- The units of the active, hidden and revealer lists and their turrets.
local function live_units(g)
	local u, live = g.u, {}
	for _, name in ipairs({ "active", "hidden", "special" }) do
		local n = g.lists[name].head
		while n ~= 0 do
			live[n] = true
			if u.subunit[n] ~= 0 then live[u.subunit[n]] = true end
			n = u.next[n]
		end
	end
	return live
end

-- The dangling references of game g, as text.
local function dangling(g)
	local u, o, bad = g.u, g.orders, {}
	local live = live_units(g)
	local function check(holder, what, v)
		if v ~= 0 and not live[v] then bad[#bad + 1] = ("%s %s -> %d"):format(holder, what, v) end
	end
	for n in pairs(live) do
		for _, f in ipairs(REF_FIELDS) do check(n, f, u[f][n]) end
		for k, f in ipairs(UNION_WORDS) do
			if bit.band(u.union_units[n], 2 ^ (k - 1)) ~= 0 then check(n, f, u[f][n]) end
		end
		local node = u.queue_head[n]
		while node ~= -1 do
			check(n, "queued order target", o.unit[node])
			node = o.next[node]
		end
	end
	for k = 0, 11 do check("local selection", k, g.local_selection[k]) end
	for p = 0, 7 do
		for k = 0, 11 do check("selection of player " .. p, k, g.selections[p][k]) end
	end
	return bad
end

function S.live_units_reference_live_units(T)
	if not native_only.ready(T, "test_references", { oracle.DATA .. "dat.kpd" }) then return end
	local bad = {}
	local index = load(T, "index")
	for k = 1, #index do
		local name = index[k]
		local g = load(T, name)
		local game = oracle.start_mission(T.read, "generated/" .. g.mission .. ".kpm", g.seed, g.expansion)
		local run = oracle.runner(game, commands_of(g.commands), g.ui)
		local last = 0
		for step in pairs(g.steps) do last = math.max(last, step) end
		for step = 1, last do
			run.to(step)
			local d = dangling(game)
			if #d > 0 then
				bad[#bad + 1] = ("%s step %d: %s"):format(name, step, d[1])
				break
			end
		end
	end
	T.ok(#bad == 0, "dangling unit references: " .. table.concat(bad, "; "))
end

function S.packed_references_to_reused_records_resolve_to_no_unit(T)
	if not native_only.ready(T, "test_references.packed", { oracle.DATA .. "dat.kpd" }) then return end
	local game = oracle.start_mission(T.read, "generated/scenarios/placement_fail.kpm", 946684819, 0)
	-- The units on unwalkable ground die at once; remember their packed references.
	local refs = {}
	local n = game.lists.hidden.head
	while n ~= 0 do
		refs[#refs + 1] = { n = n, ref = game:unit_ref(n) }
		n = game.u.next[n]
	end
	T.ok(#refs > 0, "the scenario has units that cannot be placed")
	oracle.advance(game, 4)
	for _, r in ipairs(refs) do
		T.eq(game:unit_from_ref(r.ref), 0, "a recycled unit's reference resolves to no unit")
	end
	-- Allocations take the recycled records again, with a new uniqueness.
	local reused = 0
	for _ = 1, #refs do
		local m = game:unit_create(0, 1000, 1150, 0)
		for _, r in ipairs(refs) do
			if m == r.n then
				reused = reused + 1
				T.eq(game:unit_from_ref(r.ref), 0, "the old reference to a reused record resolves to no unit")
				T.eq(game:unit_from_ref(game:unit_ref(m)), m, "the new reference resolves to the new unit")
			end
		end
	end
	T.ok(reused > 0, "recycled records are allocated again")
	T.eq(#dangling(game), 0, "no dangling references after the reuse")
	-- Every record of the pool is on exactly one of the unit lists, and there are UNITS of them.
	local records = 0
	for _, list in pairs(game.lists) do
		local n = list.head
		while n ~= 0 do
			records, n = records + 1, game.u.next[n]
		end
	end
	T.eq(records, UNITS, "the pool size")
end

return S
