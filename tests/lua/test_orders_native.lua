-- The order queue and its issuers against the original's order functions
-- (docs/research/native-primary-orders.md; plan §7 phase 3 item 5).
--
-- tests/golden/oracle/orders_primary.lua holds 535 cases that
-- tools/research/probe_native_primary_orders_117.py ran on the original code with a real order
-- pool: issuers, allocation and freeing, insertion, pruning append, clearing, activation and
-- its locks, EndOrder, Cloak, the burrowed AI's Unburrow, the AI's highlight limit and the
-- subunit's order mapping. Each case is rebuilt here on a game started from a scenario map:
-- one unit takes the probe's unit fields (owner 3, order 3, state 7, order point 10, 20 with FoW
-- type 17, timers 15 and 23, user flags 0x82, flingy flags 15, auto target = the second unit,
-- sprite at 300, 400 with flags 255), a second unit is the probe's OTHER and a third its
-- subunit; orders.dat and units.dat take the probe's table bytes (stock orders.dat for the
-- stock_flags cases), and the order pool keeps only the probe's free nodes. The games are native
-- (kp_core); the probe's table bytes go into each game's own DAT. Needs the converter's generated data and the
-- kp_core module; skipped without them.
local oracle = require("tools.lua.oracle")
-- A unit's AI record (u.ai): guard record 0 (kp_game.h KP_AI_REF(KP_AI_GUARD, 0)), the probes' nonnull pointer.
local AI_GUARD_0 = 0x10001
local native_only = require("tests.lua.native_only")
local core = require("game.images.core")
local int = require("game.simulation.int")

local GOLDEN = "tests/golden/oracle/orders_primary.lua"
local SPECIAL = "tests/golden/oracle/orders_special.lua"
local MAP = "generated/scenarios/idle_marines.kpm"
local SEED = 946684819
local NONE = core.NONE
local OTHER = 0x800200 -- the probe's second unit record, as targets and auto targets show it
local NO_TYPE = 228
local CLOAK = 109

local S = {}

local function load_golden(T, path)
	return assert((loadstring or load)(T.read(path or GOLDEN)))()
end

-- The files shared by all cases (each game copies what it changes).
local function data(T)
	return { core = native_only.core, blobs = oracle.blobs(T.read, MAP) }
end

local function ready(T)
	return native_only.ready(T, "test_orders_native", { oracle.DATA .. "dat.kpd", MAP })
end

-- The probe's fixture (probe new(): f.unit_id, f.current_order, f.status, f.ai, f.pool_count,
-- f.animation; stock = the stock orders.dat flags).
local function fixture(d, f, stock)
	local g = d.core.game.new(d.blobs, { seed = SEED, expansion = 0 })
	local u, w = g.u, g.img
	local units = {}
	local n = g.lists.active.head
	while n ~= 0 and #units < 3 do
		units[#units + 1] = n
		n = u.next[n]
	end
	local unit, other, sub = units[1], units[2], units[3]
	local orders, types = g.dat.orders, g.dat.units
	if not stock then
		for i = 0, 188 do
			orders.interruptable[i], orders.highlight_icon[i], orders.changes_subunit_order[i] = 1, 0xffff, 0
		end
	end
	local uid = f.unit_id or 15
	for _, t in ipairs({ uid, 16, 17 }) do
		types.special_ability_flags[t], types.return_to_idle[t], types.attack_unit[t], types.attack_move[t] = 0, 3, 10, 14
	end
	g:order_clear_queue(unit)
	for _, m in ipairs({ unit, other, sub }) do
		u.type[m] = m == unit and uid or (m == other and 16 or 17)
		u.owner[m] = 3
		u.order[m] = m == unit and (f.current_order or 3) or 3
		u.order_state[m], u.order_type[m], u.order_x[m], u.order_y[m], u.order_unit[m] = 7, 17, 10, 20, 0
		u.status[m] = m == unit and (f.status or 1) or 1
		u.queue_timer[m], u.recent_order_timer[m], u.user_action[m], u.movement_flags[m] = 15, 23, 0x82, 15
		u.auto_target[m], u.queue_count[m], u.subunit[m], u.ai[m] = other, 0, 0, 0
		local s = u.sprite[m]
		w.spr.x[s], w.spr.y[s] = 300, 400
		w.img.anim[w.spr.main[s]] = f.animation or 0
	end
	w.spr.flags[u.sprite[unit]] = 255
	if f.ai then u.ai[unit] = AI_GUARD_0 end
	-- The pool keeps its first pool_count free nodes (the probe's POOL + 20 i, in order).
	local o = g.orders
	local pool = f.pool_count or 16
	local kept = {}
	local i = o.free.head
	while i ~= NONE do
		local nx = o.next[i]
		if #kept < pool then
			kept[#kept + 1] = i
		else
			core.unlink(o.prev, o.next, o.free, i)
		end
		i = nx
	end
	o.allocated = 0
	w.unit, w.flingy = 0xabcdef, 0x234567
	return { g = g, unit = unit, other = other, sub = sub, pool = kept }
end

local function ref(fx, m)
	if m == fx.other then return OTHER end
	return m
end

local function state(fx, m)
	local g = fx.g
	local u = g.u
	m = m or fx.unit
	return {
		order = u.order[m], state = u.order_state[m], fow = u.order_type[m], x = u.order_x[m] % 65536,
		y = u.order_y[m] % 65536, target = ref(fx, u.order_unit[m]), status = u.status[m], highlight = u.queue_count[m],
		wait = u.queue_timer[m], move_update_timer = u.recent_order_timer[m], user_flags = u.user_action[m],
		auto_target = ref(fx, u.auto_target[m]), flingy_flags = u.movement_flags[m],
		sprite_flags = g.img.spr.flags[u.sprite[m]],
	}
end

local function queue(fx)
	local g = fx.g
	local o = g.orders
	local out = {}
	local i = g.u.queue_head[fx.unit]
	while i ~= NONE do
		out[#out + 1] = { id = o.order[i], fow = o.type[i], position = o.x[i] % 65536 + (o.y[i] % 65536) * 65536,
			target = ref(fx, o.unit[i]) }
		i = o.next[i]
	end
	return out
end

local function ids(fx)
	local out = {}
	for k, e in ipairs(queue(fx)) do out[k] = e.id end
	return out
end

local function counts(fx)
	local o = fx.g.orders
	local free = 0
	local i = o.free.head
	while i ~= NONE do
		free = free + 1
		i = o.next[i]
	end
	return { free, o.allocated }
end

-- The probe's POOL + 20 k address of node i (0 for none).
local function address(fx, i)
	if i == NONE then return 0 end
	for k, j in ipairs(fx.pool) do
		if j == i then return 0x808000 + (k - 1) * 20 end
	end
	error("node outside the fixture pool", 2)
end

local function free_chain(fx)
	local o = fx.g.orders
	local out = {}
	local i = o.free.head
	while i ~= NONE do
		out[#out + 1] = address(fx, i)
		i = o.next[i]
	end
	return out
end

local function active(a)
	a = a or {}
	return {
		order = a.order or 10, state = 0, fow = a.fow or NO_TYPE, x = (a.x or 0) % 65536, y = (a.y or 0) % 65536,
		target = a.target or 0, status = a.status or 1, highlight = a.highlight or 0, wait = 0, move_update_timer = 0,
		user_flags = 0x82, auto_target = a.ai and OTHER or 0, flingy_flags = 7, sprite_flags = 127,
	}
end

local function enqueue(fx, list)
	for _, order in ipairs(list) do fx.g:order_insert_after(fx.unit, order, 0, 0, 0, NO_TYPE, NONE) end
end

-- Packed position low/high words as the issuers and queue functions take them.
local function split(packed) return packed % 65536, math.floor(packed / 65536) % 65536 end

local function target_of(fx, t)
	if t == OTHER then return fx.other end
	return t
end

-- Builds the fixture of case c, runs it and returns what the probe recorded.
local RUN = {}

RUN.issue_no_target = function(d, c)
	local i = c.inputs
	local fx = fixture(d, {})
	fx.g.dat.orders.interruptable[i.order] = i.interruptible
	fx.g:issue_order(fx.unit, i.order)
	return { state(fx), queue(fx), counts(fx), fx.g.img.unit, fx.g.img.flingy }
end

RUN.stock_flags_issue_no_target = function(d, c)
	local fx = fixture(d, {}, true)
	fx.g:issue_order(fx.unit, c.inputs.id)
	return { state(fx), queue(fx), counts(fx) }
end

RUN.position = function(d, c)
	local i = c.inputs
	local fx = fixture(d, {})
	if i.entry == "0x42a340" then
		fx.g:issue_order_at(fx.unit, 10, i.x, i.y)
	else
		fx.g:issue_order_fow(fx.unit, 10, 65, i.x, i.y)
	end
	return { state(fx), queue(fx), counts(fx) }
end

RUN.target = function(d, c)
	local i = c.inputs
	local fx = fixture(d, {})
	local t = target_of(fx, i.target)
	if i.entry == "0x42a380" then
		fx.g:issue_order_target(fx.unit, 10, t)
	else
		fx.g:issue_order_packed(fx.unit, 10, 123, 456, t)
	end
	return { state(fx), queue(fx), counts(fx) }
end

-- Allocation, freeing and reuse through the queue entries that allocate and free one node.
RUN.allocator_payload_widths = function(d, c)
	local i = c.inputs
	local fx = fixture(d, { pool_count = i.pool_count })
	local x, y = split(i.packed_position)
	fx.g:order_insert_after(fx.unit, i.cl_input, x, y, fx.other, i.dx_input, NONE)
	local node = fx.g.u.queue_head[fx.unit]
	local out = { address(fx, node), counts(fx) }
	if node ~= NONE then
		local e = queue(fx)[1]
		out[#out + 1], out[#out + 2], out[#out + 3], out[#out + 4] = e.id, e.fow, e.position, e.target
	end
	return out
end

RUN.allocator_free_roundtrip = function(d, c)
	local fx = fixture(d, { pool_count = c.inputs.pool_count })
	fx.g:order_insert_after(fx.unit, 0xbc, 0, 0, 0, NO_TYPE, NONE)
	fx.g:order_remove(fx.unit, fx.g.u.queue_head[fx.unit])
	return { counts(fx), free_chain(fx) }
end

RUN.allocator_reuse_preserves_existing_head = function(d, c)
	local fx = fixture(d, { pool_count = c.inputs.pool_count })
	fx.g:order_insert_after(fx.unit, 0xbc, 0, 0, 0, NO_TYPE, NONE)
	fx.g:order_remove(fx.unit, fx.g.u.queue_head[fx.unit])
	fx.g:order_insert_after(fx.unit, 10, 0, 0, 0, NO_TYPE, NONE)
	return address(fx, fx.g.u.queue_head[fx.unit])
end

RUN.activation_preserves_primary_timer_and_secondary_order = function(d)
	local fx = fixture(d, {})
	local u = fx.g.u
	u.order_timer[fx.unit], u.order_signal[fx.unit] = 33, 6
	fx.g:issue_order(fx.unit, 10)
	return { u.order_timer[fx.unit], u.order_signal[fx.unit] }
end

RUN.highlight_roundtrip = function(d, c)
	local i = c.inputs
	local fx = fixture(d, {})
	fx.g.u.queue_count[fx.unit] = i.initial
	fx.g.dat.orders.highlight_icon[10] = i.highlight_descriptor
	fx.g:issue_order(fx.unit, 10)
	return fx.g.u.queue_count[fx.unit]
end

RUN.activation_lock = function(d, c)
	local i = c.inputs
	local fx = fixture(d, { status = i.status })
	fx.g.dat.orders.interruptable[i.order] = i.interruptible
	fx.g:issue_order(fx.unit, i.order)
	local u = fx.g.u
	return { u.order[fx.unit], u.status[fx.unit], u.user_action[fx.unit], #queue(fx), counts(fx) }
end

RUN.tail_pruning = function(d, c)
	local i = c.inputs
	local fx = fixture(d, {})
	enqueue(fx, i.queue)
	for k, oid in ipairs(i.queue) do fx.g.dat.orders.interruptable[oid] = i.interruptible[k] end
	fx.g:order_append(fx.unit, i.new_order, 123, 456, 0, 65, i.clear_others ~= 0)
	return { ids(fx), counts(fx), fx.g.u.order[fx.unit] }
end

RUN.clear_queue = function(d, c)
	local i = c.inputs
	local fx = fixture(d, {})
	fx.g.dat.orders.highlight_icon[10] = i.highlight
	for _ = 1, i.queue_size do fx.g:order_insert_after(fx.unit, 10, 0, 0, 0, NO_TYPE, NONE) end
	fx.g:order_clear_queue(fx.unit)
	return { queue(fx), counts(fx), fx.g.u.queue_count[fx.unit] }
end

RUN.current_death_append_gate = function(d, c)
	local fx = fixture(d, { current_order = c.inputs.current_order })
	fx.g:issue_order(fx.unit, 10)
	local u = fx.g.u
	return { u.order[fx.unit], queue(fx), counts(fx), u.user_action[fx.unit] }
end

RUN.pool_exhaustion = function(d, c)
	local fx = fixture(d, { pool_count = c.inputs.pool_count })
	fx.g:issue_order(fx.unit, 10)
	local u = fx.g.u
	return { u.order[fx.unit], queue(fx), counts(fx), u.user_action[fx.unit] }
end

-- The local player's unit reports "queue full" (the probe stops at the notification 0x46ae70).
RUN.local_pool_exhaustion_notification_prefix = function(d)
	local fx = fixture(d, { pool_count = 0 })
	local reported
	fx.g.local_player = 3
	fx.g:issue_order(fx.unit, 10)
	for _, e in ipairs(oracle.events(fx.g)) do
		if e.kind == 2 then reported = { e.a, e.b } end
	end
	assert(reported and reported[1] == 3, "the queue-full notification of the local player")
	local u = fx.g.u
	return { u.order[fx.unit], queue(fx), counts(fx), u.user_action[fx.unit] }
end

RUN.retained_head_activation = function(d, c)
	local fx = fixture(d, { pool_count = c.inputs.pool_count })
	enqueue(fx, { 10 })
	fx.g.dat.orders.interruptable[10] = 0
	fx.g:issue_order(fx.unit, 13)
	local u = fx.g.u
	return { u.order[fx.unit], u.status[fx.unit], ids(fx), counts(fx) }
end

RUN.activate_after_released_lock = function(d, c)
	local fx = fixture(d, { status = c.inputs.previous_status })
	fx.g:issue_order(fx.unit, 10)
	fx.g.u.status[fx.unit] = 1
	fx.g:order_activate(fx.unit)
	return { state(fx), queue(fx), counts(fx) }
end

RUN.order_done_empty_queue = function(d, c)
	local i = c.inputs
	local fx = fixture(d, { ai = i.ai, status = i.status })
	fx.g:order_done(fx.unit)
	local u = fx.g.u
	return { u.order[fx.unit], ids(fx), counts(fx), u.user_action[fx.unit] }
end

RUN.activate_empty_queue_is_noop = function(d)
	local fx = fixture(d, {})
	fx.g:order_activate(fx.unit)
	local u, n = fx.g.u, fx.unit
	return { u.order[n], u.order_state[n], u.queue_timer[n], u.recent_order_timer[n], u.user_action[n], counts(fx) }
end

RUN.cloak_pool = function(d, c)
	local i = c.inputs
	local fx = fixture(d, { pool_count = i.pool_count })
	fx.g.dat.orders.interruptable[CLOAK] = i.cloak_interruptible
	fx.g:issue_order(fx.unit, CLOAK)
	local u = fx.g.u
	return { u.order[fx.unit], u.status[fx.unit], ids(fx), counts(fx) }
end

RUN.cloak_lock = function(d, c)
	local fx = fixture(d, { status = c.inputs.status })
	fx.g.dat.orders.interruptable[CLOAK] = 0
	fx.g:issue_order(fx.unit, CLOAK)
	local u = fx.g.u
	return { u.order[fx.unit], u.status[fx.unit], ids(fx) }
end

RUN.cloak_prepend_ai_limit = function(d, c)
	local fx = fixture(d, { ai = c.inputs.ai })
	fx.g.u.queue_count[fx.unit] = 9
	fx.g.dat.orders.interruptable[CLOAK] = 0
	fx.g:issue_order(fx.unit, CLOAK)
	return { fx.g.u.order[fx.unit], ids(fx), counts(fx) }
end

RUN.burrow = function(d, c)
	local i = c.inputs
	local fx = fixture(d, { unit_id = i.unit_id, ai = i.ai, status = i.status })
	fx.g:issue_order(fx.unit, i.new_order)
	local u = fx.g.u
	return { u.order[fx.unit], ids(fx), counts(fx), ref(fx, u.auto_target[fx.unit]) }
end

RUN.prepend_ai_highlight_limit = function(d, c)
	local i = c.inputs
	local fx = fixture(d, { ai = i.ai })
	enqueue(fx, { 10 })
	fx.g.u.queue_count[fx.unit] = i.highlight_count
	fx.g:order_insert_before(fx.unit, 11, 0, 0, 0, NO_TYPE, fx.g.u.queue_head[fx.unit])
	return { ids(fx), counts(fx) }
end

RUN.insert_middle = function(d, c)
	local fx = fixture(d, {})
	enqueue(fx, { 10, 11, 12 })
	local o = fx.g.orders
	local anchor = o.next[fx.g.u.queue_head[fx.unit]]
	if c.inputs.before then
		fx.g:order_insert_before(fx.unit, 13, 123, 456, fx.other, 65, anchor)
	else
		fx.g:order_insert_after(fx.unit, 13, 123, 456, fx.other, 65, anchor)
	end
	return { ids(fx), counts(fx) }
end

RUN.prepend_ai_limit_counts_highlights_not_nodes = function(d)
	local fx = fixture(d, { ai = true })
	enqueue(fx, { 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10 })
	fx.g.u.queue_count[fx.unit] = 0
	fx.g:order_insert_before(fx.unit, 11, 0, 0, 0, NO_TYPE, fx.g.u.queue_head[fx.unit])
	return { #queue(fx), counts(fx) }
end

local CHILD = { [3] = "idle", [10] = "attack_unit", [14] = "attack_move", [34] = "inherit", [35] = "noninherit" }

RUN.subunit_mapping = function(d, c)
	local order = c.inputs.parent_order
	local fx = fixture(d, {})
	local g, u = fx.g, fx.g.u
	u.subunit[fx.unit] = fx.sub
	local units = g.dat.units
	units.special_ability_flags[17], units.return_to_idle[17] = 0x10, 4
	units.attack_unit[17], units.attack_move[17] = 11, 15
	if CHILD[order] == "inherit" then g.dat.orders.changes_subunit_order[order] = 1 end
	g:issue_order_at(fx.unit, order, 123, 456)
	return { u.order[fx.unit], u.order[fx.sub], u.order_x[fx.sub] % 65536, u.order_y[fx.sub] % 65536, counts(fx) }
end

RUN.subunit_target_order_propagation = function(d)
	local fx = fixture(d, {})
	local g, u = fx.g, fx.g.u
	u.subunit[fx.unit] = fx.sub
	g.dat.units.special_ability_flags[17], g.dat.units.attack_unit[17] = 0x10, 11
	g:issue_order_target(fx.unit, 10, fx.other)
	return { u.order[fx.sub], ref(fx, u.order_unit[fx.sub]), u.order_x[fx.sub] % 65536, u.order_y[fx.sub] % 65536,
		counts(fx) }
end

-- Active main animations: the probe stops before the original's first iscript call
-- (0x411730) and records the animation it would play; here the first image play call stops
-- the issuer the same way.
local STOP = {}

RUN.animation_boundary = function(d, c)
	local i = c.inputs
	local fx = fixture(d, { animation = i.main_animation })
	local played
	fx.g.img.play_probe = function(_, _, anim)
		played = anim
		error(STOP)
	end
	local ok, err = pcall(fx.g.issue_order, fx.g, fx.unit, i.order)
	if ok or err ~= STOP then error(ok and "no iscript call" or err, 0) end
	local u, n = fx.g.u, fx.unit
	return { u.order[n], u.order_state[n], u.queue_timer[n], u.recent_order_timer[n], false, played }
end

-- The family of a case name (the probe's label prefix).
local function family(name)
	for key in pairs(RUN) do
		if name == key or name:sub(1, #key + 1) == key .. "_" then
			-- the longest matching key wins (stock_flags_issue_no_target before issue_no_target)
			local best = key
			for other in pairs(RUN) do
				if #other > #best and (name == other or name:sub(1, #other + 1) == other .. "_") then best = other end
			end
			return best
		end
	end
	return nil
end

local function deep_equal(a, b)
	if type(a) ~= type(b) then return false end
	if type(a) ~= "table" then return a == b end
	for k, v in pairs(a) do
		if not deep_equal(v, b[k]) then return false end
	end
	for k in pairs(b) do
		if a[k] == nil then return false end
	end
	return true
end

local function show(v)
	if type(v) ~= "table" then return tostring(v) end
	local keys = {}
	for k in pairs(v) do keys[#keys + 1] = k end
	table.sort(keys, function(x, y) return tostring(x) < tostring(y) end)
	local parts = {}
	for _, k in ipairs(keys) do parts[#parts + 1] = tostring(k) .. "=" .. show(v[k]) end
	return "{" .. table.concat(parts, ",") .. "}"
end

function S.order_functions_equal_the_original(T)
	if not ready(T) then return end
	local golden = load_golden(T)
	local d = data(T)
	local bad, checked = {}, 0
	for k, c in ipairs(golden.cases) do
		do
			local f = family(c.case)
			T.ok(f, "no fixture for case " .. c.case)
			local ok, actual = pcall(RUN[f], d, c)
			checked = checked + 1
			if not ok then
				bad[#bad + 1] = c.case .. ": " .. tostring(actual)
			elseif not deep_equal(actual, c.expected) then
				bad[#bad + 1] = ("%s: got %s, original %s"):format(c.case, show(actual), show(c.expected))
			end
			if #bad >= 5 then break end
		end
	end
	T.ok(#bad == 0, table.concat(bad, "\n"))
	T.ok(checked > 0, "no case checked")
end

-- The special order consumers of phase 3 (tests/golden/oracle/orders_special.lua; probe
-- tools/research/probe_native_special_orders_117.py): RescuePassive 0x48d120 with the rescuer
-- search and a resource depot's whole owner list, and the trap's hiding and empty retry
-- 0x428490. The probe runs on a flat one-region map with no units; here the scenario map
-- starts without its units and every probe point moves by SHIFT onto its open ground (the
-- cases depend only on the units' relative positions). The probe's global query depth
-- (0x5d6e48) has no counterpart: Lua queries return fresh lists, so it is 0 as there.
local SHIFT_X, SHIFT_Y = 232, 332
local UNIT_ADDRESS, UNIT_STRIDE = 0x5da3a0, 0x150
local RESCUE_PASSIVE, TRAP = 0xa1, 0xa7

local function empty_game(d, controllers)
	local g = d.core.game.new(d.blobs, { seed = SEED, expansion = 1, no_units = true })
	for p = 0, 11 do g.players[p].controller = controllers[p + 1] end
	return g
end

local function create(g, created, t, x, y, owner)
	local n = g:create_map_unit(t, x + SHIFT_X, y + SHIFT_Y, owner)
	assert(n ~= 0, "the unit is created")
	created[#created + 1] = n
	return n
end

-- A player's unit list as the probe's record addresses (creation order).
local function chain(g, created, p)
	local out = {}
	local n = g.player_units[p]
	while n ~= 0 do
		for k, m in ipairs(created) do
			if m == n then out[#out + 1] = UNIT_ADDRESS + (k - 1) * UNIT_STRIDE end
		end
		n = g.u.player_next[n]
	end
	return out
end

local SPECIAL_RUN = {}

SPECIAL_RUN.trap_initialize_empty_retry = function(d, c)
	local g = empty_game(d, { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 })
	local n = create(g, {}, c.inputs.type, 768, 768, 3)
	local handler = g:order_handler(TRAP)
	handler(g, n)
	handler(g, n)
	local u = g.u
	return { u.order_state[n], u.order_timer[n], int.band(u.status[n], 0x200300), g.img.spr.elevation[u.sprite[n]],
		u.visibility[n], u.secondary_timer[n], 0 }
end

SPECIAL_RUN.rescue_human_selector = function(d, c)
	local i = c.inputs
	local controllers = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 }
	controllers[4], controllers[1] = i.rescue_controller, i.candidate_controller
	local g = empty_game(d, controllers)
	local created = {}
	local rescued = create(g, created, 0, i.xy[1][1], i.xy[1][2], i.rescue_owner)
	create(g, created, 0, i.xy[2][1], i.xy[2][2], i.candidate_owner)
	local u = g.u
	u.order_timer[rescued] = i.timer
	g:order_handler(RESCUE_PASSIVE)(g, rescued)
	return { u.owner[rescued], u.order_timer[rescued], u.order[rescued], chain(g, created, 0), chain(g, created, 3), 0,
		g.img.spr.owner[u.sprite[rescued]], g.count_all[0], g.count_all[3], g.count_completed[0], g.count_completed[3] }
end

SPECIAL_RUN.rescue_completed_depot_whole_owner_list = function(d, c)
	local i = c.inputs
	local g = empty_game(d, { 2, 2, 2, 3, 2, 2, 2, 2, 2, 2, 2, 2 })
	local created = {}
	local depot = create(g, created, i.depot_type, 768, 768, i.old_owner)
	local other = create(g, created, i.other_type, 1200, 1200, i.old_owner)
	create(g, created, i.human_type, 828, 768, i.new_owner)
	g:order_handler(RESCUE_PASSIVE)(g, depot)
	local u = g.u
	return { u.owner[depot], u.owner[other], chain(g, created, 0), chain(g, created, 3), 0 }
end

function S.special_order_consumers_equal_the_original(T)
	if not ready(T) then return end
	local golden = load_golden(T, SPECIAL)
	local d = data(T)
	local bad = {}
	for _, c in ipairs(golden.cases) do
		local f
		for key in pairs(SPECIAL_RUN) do
			if c.case == key or c.case:sub(1, #key + 1) == key .. "_" then f = key end
		end
		T.ok(f, "no fixture for case " .. c.case)
		local ok, actual = pcall(SPECIAL_RUN[f], d, c)
		if not ok then
			bad[#bad + 1] = c.case .. ": " .. tostring(actual)
		elseif not deep_equal(actual, c.expected) then
			bad[#bad + 1] = ("%s: got %s, original %s"):format(c.case, show(actual), show(c.expected))
		end
	end
	T.ok(#bad == 0, table.concat(bad, "\n"))
end

return S
