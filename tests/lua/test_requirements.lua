-- The two requirement interpreters against the original's code (plan decision D16): the general
-- order/research/tech-use/upgrade evaluator 0x407570 with its wrappers 0x407bb0, 0x407dc0, 0x407f90,
-- 0x408140 and its offset initializers, the in-progress bits 0x4a0560/0x4a0590, and the unit-production gate
-- CanTrainUnit 0x4b32a0 (docs/research/native-dat-requirements.md, native-requirements.md).
--
-- tests/golden/oracle/requirements_general.lua (482 cases) and requirements_units.lua (182 cases) hold every
-- isolated original-code probe of tools/research/inspect_native_dat_requirements_117.py and
-- inspect_native_requirements_117.py, exported by tools/oracle/export_requirement_cases.py. Each case is rebuilt on
-- a native game started from a scenario map: its first unit is the probe's unit record and its second the probe's
-- zeroed `target` record; the header's `fixture` is the probe's new() (counts, techs, upgrades, cheats, expansion,
-- the producer's DAT values, the offsets 0xffff) and the case's `patch` its case builder. The probes' synthetic
-- requirement tables run through the evaluator's program override (g:requirement_program), which replaces the
-- word stream of every domain as the probes wrote theirs at a fixed address. Each case checks the return value and
-- the requirement error (0x6be654); offsets cases check every offset of a domain, in-progress cases the bit.
-- No case is skipped. Needs the converter's generated data and the kp_core module; skipped without them.
local oracle = require("tools.lua.oracle")
local native_only = require("tests.lua.native_only")

local GENERAL = "tests/golden/oracle/requirements_general.lua"
local UNITS = "tests/golden/oracle/requirements_units.lua"
local MAP = "generated/scenarios/idle_marines.kpm"
local SEED = 946684819
local PLAYERS = 12
local UNIT_TYPES = 228
local TECHS = 44
local UPGRADES = 61
local TECH_PROGRESS_BYTES = 6
local UPGRADE_PROGRESS_BYTES = 8
local PLAYER = 0 -- the probes' player unless a case names one
local SHOWN = 20 -- failures listed in a failing test's message
-- Cases between full collections: a game's C state is far larger than the Lua collector sees.
local COLLECT_EVERY = 8

-- The requirement domains (kp_game.h KP_REQ_*) with their offset columns and capacities.
local DOMAIN = {
	orders = { id = 0, table = "orders", column = "requirements", count = 189 },
	research = { id = 1, table = "techdata", column = "research_requirements", count = TECHS },
	tech_use = { id = 2, table = "techdata", column = "use_requirements", count = TECHS },
	upgrades = { id = 3, table = "upgrades", column = "requirements", count = UPGRADES },
	units = { id = 4, table = "units", column = "requirements", count = UNIT_TYPES },
}

-- The unit fields the requirement code reads, zero in the probes' unit records unless their new() sets them.
local UNIT_FIELDS = { "owner", "order", "status", "type", "queue0", "queue1", "queue2", "queue3", "queue4",
	"queue_slot", "secondary_order", "union_0xc0", "union_0xc8", "union_0xd0", "build_unit", "order_unit", "ai",
	"subunit", "lockdown_timer", "stasis_timer", "maelstrom_timer" }

local S = {}

local function load_golden(T, path) return assert((loadstring or load)(T.read(path)))() end

local function ready(T)
	return native_only.ready(T, "test_requirements", { oracle.DATA .. "dat.kpd", MAP })
end

local function expansion_of(f, c)
	for _, op in ipairs(c.patch or {}) do
		if op[1] == "expansion" then return op[2] end
	end
	return f.expansion
end

-- Writes value v into byte b of a unit field (the probes' w8 into a wider field).
local function set_byte(array, n, b, v)
	local scale = 256 ^ b
	local old = array[n]
	array[n] = old + (v - math.floor(old / scale) % 256) * scale
end

-- The probe's new(): fixture f (the golden's header) for case c.
local function fixture(blobs, f, c)
	local g = native_only.core.game.new(blobs, { seed = SEED, expansion = expansion_of(f, c) })
	local u = g.u
	local unit = g.lists.active.head
	local other = u.next[unit]
	local producer = c.producer or f.producer
	for _, field in ipairs(UNIT_FIELDS) do
		u[field][unit], u[field][other] = 0, 0
	end
	u.type[unit], u.status[unit], u.order[unit] = producer, f.status, f.order
	u.union_0xc8[unit] = f.tech_byte + f.upgrade_byte * 256
	for k = 0, 4 do u["queue" .. k][unit] = f.queue end
	for i = 0, UNIT_TYPES * PLAYERS - 1 do
		g.count_completed[i], g.count_all[i] = f.count_completed, f.count_all
	end
	for p = 0, PLAYERS - 1 do
		local techs = g.techs[p]
		for t = 0, TECHS - 1 do
			if f.tech_available then techs.available[t] = f.tech_available end
			if f.tech_researched then techs.researched[t] = f.tech_researched end
		end
		for id = 0, UPGRADES - 1 do
			g.upgrades[p][id] = f.upgrade_level
			if f.upgrade_max then g.upgrade_max[p][id] = f.upgrade_max end
		end
		if f.unit_available then
			for t = 0, UNIT_TYPES - 1 do g.unit_available[p][t] = f.unit_available end
		end
		if f.research_progress then
			for b = 0, TECH_PROGRESS_BYTES - 1 do g.research_progress[p][b] = f.research_progress end
		end
		if f.upgrade_progress then
			for b = 0, UPGRADE_PROGRESS_BYTES - 1 do g.upgrade_progress[p][b] = f.upgrade_progress end
		end
	end
	for column, v in pairs(f.producer_dat) do g.dat.units[column][producer] = v end
	for _, d in pairs(DOMAIN) do
		local column = g.dat[d.table][d.column]
		for id = 0, d.count - 1 do column[id] = f.offsets end
	end
	g.cheats, g.requirement_error = f.cheats, 0
	return g, unit, other
end

-- The case's patch over the fixture.
local function apply(g, unit, other, c)
	local u = g.u
	for _, op in ipairs(c.patch or {}) do
		local kind = op[1]
		if kind == "u" then
			u[op[2]][unit] = op[3]
		elseif kind == "u8" then
			set_byte(u[op[2]], unit, op[3], op[4])
		elseif kind == "u_other" then
			u[op[2]][unit] = other
		elseif kind == "other" then
			u[op[2]][other] = op[3]
		elseif kind == "dat" then
			g.dat[op[2]][op[3]][op[4]] = op[5]
		elseif kind == "count_completed" or kind == "count_all" then
			g[kind][op[2] * PLAYERS + PLAYER] = op[3]
		elseif kind == "available" or kind == "researched" then
			g.techs[PLAYER][kind][op[2]] = op[3]
		elseif kind == "upgrade" then
			g.upgrades[PLAYER][op[2]] = op[3]
		elseif kind == "unit_available" then
			g.unit_available[PLAYER][op[2]] = op[3]
		elseif kind == "research_progress" or kind == "upgrade_progress" then
			g[kind][op[2]][op[3]] = op[4]
		elseif kind == "cheats" then
			g.cheats = op[2]
		elseif kind == "offset" then
			local d = DOMAIN[c.call]
			g.dat[d.table][d.column][op[2]] = op[3]
		elseif kind ~= "expansion" then -- the game's start took the expansion
			error("unknown patch operation " .. tostring(kind))
		end
	end
	if c.table then g:requirement_program(c.table) end
end

local WRAPPER = { orders = "can_issue_order", research = "can_research", tech_use = "can_use_tech",
	upgrades = "can_upgrade", units = "can_train_unit" }

-- Runs case c; nil when it equals the original, else the difference.
local function run(blobs, f, c)
	local g, unit, other = fixture(blobs, f, c)
	apply(g, unit, other, c)
	local player = c.player or PLAYER
	if c.call == "offsets" then
		local d = DOMAIN[c.domain]
		for id = 0, d.count - 1 do
			local got = g:requirement_offset(d.id, id)
			if got ~= c.offsets[id + 1] then return ("offset %d: got %d, original %d"):format(id, got, c.offsets[id + 1]) end
		end
		return nil
	end
	if c.call == "research_progress" or c.call == "upgrade_progress" then
		local got = g:in_progress(player, c.id, c.call == "upgrade_progress")
		if got ~= (c.ret ~= 0) then return ("in progress %s, original bit %d"):format(tostring(got), c.ret) end
		return nil
	end
	local ret
	if c.call == "evaluate" then
		ret = g:evaluate_requirements(c.id, player, unit, DOMAIN.orders.id, c.at)
	else
		ret = g[WRAPPER[c.call]](g, c.id, player, unit)
	end
	local err = g.requirement_error
	if ret ~= c.ret or err ~= c.err then
		return ("got %d error %d, original %d error %d"):format(ret, err, c.ret, c.err)
	end
	return nil
end

local function check(T, path)
	if not ready(T) then return end
	local golden = load_golden(T, path)
	local blobs = oracle.blobs(T.read, MAP)
	local bad = {}
	for k, c in ipairs(golden.cases) do
		local ok, diff = pcall(run, blobs, golden.fixture, c)
		if not ok or diff then bad[#bad + 1] = ("%d %s (id %s): %s"):format(k, c.kind, tostring(c.id), tostring(diff)) end
		if k % COLLECT_EVERY == 0 then collectgarbage() end
	end
	local shown = {}
	for k = 1, math.min(#bad, SHOWN) do shown[k] = bad[k] end
	T.ok(#bad == 0, ("%d of %d cases differ:\n%s"):format(#bad, #golden.cases, table.concat(shown, "\n")))
end

function S.general_evaluator_equals_the_original(T) check(T, GENERAL) end

function S.unit_gate_equals_the_original(T) check(T, UNITS) end

return S
