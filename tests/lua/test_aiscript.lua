-- The AIScript VM against the original's code (plan phase 7A): the interpreter ExecuteAiScript 0x493a60, the
-- scheduler RunAiScripts 0x4938e0, the pool (CreateAiScript 0x494d90, FreeAiScript 0x493240), the directories'
-- lookup FindAiScriptOffset 0x494d20 and the helpers AddAttackUnits 0x4839f0, the defense lists (0x482550..0x482e40)
-- and FindScriptSpellcaster 0x4932a0 (docs/research/native-ai-opcodes.md).
--
-- tests/golden/oracle/aiscript_cases.lua holds the 89 isolated original-code probes of
-- tools/research/inspect_ai_opcodes_117.py, exported by tools/oracle/export_aiscript_cases.py. Each case is rebuilt on
-- a native game started from a scenario map: the probes' synthetic binaries go through the binaries' override
-- (g:ai_program), their zeroed script records are pool slots the case creates, and the helpers the probes called on
-- their own run through the opcodes that call them (attack_add, defensebuild/defenseuse) on the probe's player. No
-- case is skipped. Needs the converter's generated data and the kp_core module; skipped without them.
local oracle = require("tools.lua.oracle")
local native_only = require("tests.lua.native_only")

local CASES = "tests/golden/oracle/aiscript_cases.lua"
local MAP = "generated/scenarios/idle_marines.kpm"
local SEED = 946684819
local PLAYERS = 12
local ROSTER = 64 -- kp_game.h KP_AI_ROSTER
local DEFENSE = 20 -- KP_AI_DEFENSE
local DEFENSE_LISTS = 8 -- KP_AI_DEFENSE_LISTS
local TANK, TANK_SIEGED = 5, 30
local OP_ATTACK_ADD, OP_DEFENSEBUILD_GG = 0x0c, 0x13
local WAIT_7 = { 0x02, 0x07, 0x00 }
local SHOWN = 20
local COLLECT_EVERY = 8

local S = {}

local function ready(T)
	return native_only.ready(T, "test_aiscript", { oracle.DATA .. "dat.kpd", MAP })
end

local function bytes(list)
	local out = {}
	for k, b in ipairs(list) do out[k] = string.char(b) end
	return table.concat(out)
end

local function u32(v)
	return string.char(v % 256, math.floor(v / 256) % 256, math.floor(v / 65536) % 256, math.floor(v / 16777216) % 256)
end

local function concat(...)
	local out = {}
	for _, list in ipairs({ ... }) do
		for _, b in ipairs(list) do out[#out + 1] = b end
	end
	return out
end

local function game(blobs)
	return native_only.core.game.new(blobs, { seed = SEED })
end

-- A game running the probe's binaries with a zeroed script of player p at PC 0 of binary bin (the probes' record).
local function scripted(blobs, code, other, p, bin)
	local g = game(blobs)
	g:ai_program(bytes(code), bytes(other or {}))
	local s = g:ai_create(0, 0, nil, bin or 0)
	g.scripts.player[s] = p or 0
	return g, s
end

local function list_of(array, first, count)
	local out = {}
	for k = 1, count do out[k] = array[first + k - 1] end
	return out
end

local function same(a, b)
	if #a ~= #b then return false end
	for k = 1, #a do
		if a[k] ~= b[k] then return false end
	end
	return true
end

local function differ(got, want)
	return ("got {%s}, original {%s}"):format(table.concat(got, ","), table.concat(want, ","))
end

local function word_bytes(v) return v % 256, math.floor(v / 256) % 256 end

local RUN = {}

function RUN.attack_roster(blobs, c)
	local lo, hi = word_bytes(c.unit)
	local g, s = scripted(blobs, concat({ OP_ATTACK_ADD, c.count, lo, hi }, WAIT_7), nil, c.player)
	local p = c.player < 8 and c.player or 0 -- a rejected player: the lists stay as they were
	for k = 0, c.occupied - 1 do g.pai.roster[p * ROSTER + k] = 7 end
	g:ai_execute(s)
	local got = list_of(g.pai.roster, p * ROSTER, ROSTER)
	return not same(got, c.expected) and differ(got, c.expected) or nil
end

function RUN.defense_roster(blobs, c)
	local lo, hi = word_bytes(c.unit)
	local g, s = scripted(blobs, concat({ OP_DEFENSEBUILD_GG + c.list, c.count, lo, hi }, WAIT_7))
	for k = 0, c.occupied - 1 do g.pai.defense[c.list * DEFENSE + k] = 7 end
	g:ai_execute(s)
	local got = list_of(g.pai.defense, c.list * DEFENSE, DEFENSE)
	return not same(got, c.expected) and differ(got, c.expected) or nil
end

function RUN.training_opcode(blobs, c)
	local g, s = scripted(blobs, c.code)
	g.count_all[TANK * PLAYERS], g.count_all[TANK_SIEGED * PLAYERS] = c.all5, c.all30
	g.count_completed[TANK * PLAYERS], g.count_completed[TANK_SIEGED * PLAYERS] = c.complete5, c.complete30
	g:ai_execute(s)
	local got = { g.scripts.pc[s], g.scripts.wait[s], g.pai.wanted_unit[0] }
	return not same(got, c.expected) and differ(got, c.expected) or nil
end

function RUN.seed_opcode(blobs, c)
	local g, s = scripted(blobs, c.code)
	local snap = g.rng:snapshot()
	snap[2] = c.state
	g.rng:restore(snap)
	g:ai_execute(s)
	local got = { g.rng.initial_seed, g.rng.state, g.scripts.pc[s] }
	return not same(got, c.expected) and differ(got, c.expected) or nil
end

-- The probe's dispatch log ends at the PC of the last opcode run: here the wait the shared return lands on.
function RUN.cross_bin_shared_return(blobs, c)
	local g, a = scripted(blobs, c.code, c.other)
	local b = g:ai_create(0, 0, nil, 1)
	g:ai_execute(a)
	local a_saved = g.scripts.ret
	g:ai_execute(b)
	local b_saved = g.scripts.ret
	g:ai_execute(a)
	local got = { a_saved, b_saved, g.scripts.ret, g.scripts.pc[a], g.scripts.pc[a] - 3 }
	return not same(got, c.expected) and differ(got, c.expected) or nil
end

-- The first two units of the active list get the probe's owners; the others become the script's player's.
function RUN.enemy_resource_first_owner(blobs, c)
	local g, s = scripted(blobs, c.code)
	local u, n, k = g.u, g.lists.active.head, 0
	while n ~= 0 do
		k = k + 1
		u.owner[n] = c.owners[k] or 0
		n = u.next[n]
	end
	g.alliance[0][0], g.alliance[0][1], g.alliance[0][2] = 1, 0, 0 -- the owners are the script's player's enemies
	for p, v in pairs(c.minerals) do g.minerals[p] = v end
	for p, v in pairs(c.gas) do g.gas[p] = v end
	g:ai_execute(s)
	local got = { g.scripts.pc[s], g.scripts.wait[s] }
	return not same(got, c.expected) and differ(got, c.expected) or nil
end

-- Player 0's unit list holds the given units only, in order, each a Corsair with the given energy and order.
local function casters(g, units)
	local u, n, picked = g.u, g.lists.active.head, {}
	while n ~= 0 and #picked < #units do
		picked[#picked + 1] = n
		n = u.next[n]
	end
	g.player_units[0] = picked[1]
	for k, m in ipairs(picked) do
		u.player_next[m] = picked[k + 1] or 0
		u.type[m], u.energy[m], u.order[m] = units[k].type or 60, units[k].energy, units[k].order
	end
	return picked
end

function RUN.spell_unit_eligibility(blobs, c)
	local g = game(blobs)
	local picked = casters(g, { { type = c.type, energy = c.energy, order = c.order } })
	local got = g:ai_script_caster(0, 60, c.minimum)
	if (got == picked[1]) ~= c.expected or (got ~= 0 and got ~= picked[1]) then
		return ("got unit %d, original %s"):format(got, tostring(c.expected))
	end
	return nil
end

function RUN.spell_unit_first_eligible(blobs, c)
	local g = game(blobs)
	local picked = casters(g, c.units)
	local got = g:ai_script_caster(0, 60, c.minimum)
	if got ~= picked[c.first + 1] then return ("got unit %d, original the record %d"):format(got, c.first + 1) end
	return nil
end

-- The probe's directories: each binary's table at offset 64 holds one record, the zeroed bytes after it end it.
function RUN.bin_id_resolution(blobs, c)
	local g = game(blobs)
	local pad = string.rep("\0", 60)
	local ai = u32(64) .. pad .. c.ai_id .. u32(c.ai_offset) .. u32(7) .. u32(0xffffffff) .. string.rep("\0", 16)
	local bw = u32(64) .. pad .. c.bw_id .. u32(c.bw_offset) .. string.rep("\0", 8)
	g:ai_program(ai, bw)
	local off, bin = g:ai_find(c.requested)
	local got = { off, bin }
	return not same(got, c.expected) and differ(got, c.expected) or nil
end

-- The probe created its parent on a fresh pool and ran the scheduler twice.
function RUN.multirun_scheduler(blobs, c)
	local g = game(blobs)
	g:ai_program(bytes(c.code), "")
	g:ai_init()
	local parent = g:ai_create(0, 0, nil, 0)
	g:ai_run()
	local sc = g.scripts
	local child = sc.head
	local first = { sc.pc[child], sc.wait[child], sc.next[child] }
	g:ai_run()
	local got = { first[1], first[2], first[3] == parent and "parent" or first[3], sc.pc[child], sc.wait[child],
		sc.head == child and "child" or sc.head }
	return not same(got, c.expected) and differ(got, c.expected) or nil
end

function RUN.flagged_slot_reuse(blobs, c)
	local g = game(blobs)
	g:ai_init()
	local first = g:ai_create(0, 0, nil, 0)
	g.scripts.flags[first] = 6
	g:ai_free(first)
	local reused = g:ai_create(1, 10, nil, 1)
	local flags = g.scripts.flags[reused]
	local pc = g.scripts.pc[reused]
	g:ai_run()
	-- the killed script is freed without running: its PC stays where the creation put it
	local got = { first == reused, flags, g.scripts.head, g.scripts.pc[reused] == pc and 0 or 1 }
	return not same(got, c.expected) and differ(got, c.expected) or nil
end

-- The probe's list: the script (kill_thread, then a long wait), a killable victim of another player waiting 2, and an
-- unmarked one waiting long.
function RUN.delayed_cross_player_kill(blobs, c)
	local g = game(blobs)
	g:ai_program(bytes(c.code), "")
	g:ai_init()
	local sc = g.scripts
	local unmarked = g:ai_create(0, 0, nil, 0)
	local victim = g:ai_create(c.victim_player, 0, nil, 0)
	local script = g:ai_create(0, 0, nil, 0)
	sc.flags[victim], sc.wait[victim] = c.victim_flags, c.victim_wait
	g:ai_execute(script)
	local got = { sc.flags[victim] }
	sc.wait[unmarked] = c.unmarked_wait
	for _ = 1, 3 do
		g:ai_run()
		got[#got + 1] = sc.next[script] == victim
	end
	return not same(got, c.expected) and differ(got, c.expected) or nil
end

function S.vm_equals_the_original(T)
	if not ready(T) then return end
	local golden = assert((loadstring or load)(T.read(CASES)))()
	local blobs = oracle.blobs(T.read, MAP)
	local bad = {}
	for k, c in ipairs(golden.cases) do
		local ok, diff = pcall(assert(RUN[c.kind], c.kind), blobs, c)
		if not ok or diff then bad[#bad + 1] = ("%d %s: %s"):format(k, c.kind, tostring(diff)) end
		if k % COLLECT_EVERY == 0 then collectgarbage() end
	end
	local shown = {}
	for k = 1, math.min(#bad, SHOWN) do shown[k] = bad[k] end
	T.ok(#bad == 0, ("%d of %d cases differ:\n%s"):format(#bad, #golden.cases, table.concat(shown, "\n")))
end

-- The launch gate (LaunchAiScript 0x493970): a script any player may start runs for a human, a computer's does not,
-- Rscu runs for a neutral player; the binaries' own directories resolve them.
function S.launch_gate(T)
	if not ready(T) then return end
	local g = game(oracle.blobs(T.read, MAP))
	local off, bin = g:ai_find("Ter3")
	T.ok(off > 0 and bin == 0, "Ter3 is in aiscript.bin")
	off, bin = g:ai_find("PB1A")
	T.ok(off > 0 and bin == 1, "PB1A is in bwscript.bin")
	T.eq(select(1, g:ai_find("Nope")), 0, "an unknown id")
end


-- Binaries too small for a directory record hold no script: the lookup reads none of their bytes (a directory offset
-- of 0xffffffff in them would point far outside).
function S.small_binaries_hold_no_scripts(T)
	if not ready(T) then return end
	local g = game(oracle.blobs(T.read, MAP))
	for size = 0, 7 do
		g:ai_program(string.rep("\255", size), string.rep("\255", size))
		T.eq(select(1, g:ai_find("Ter3")), 0, size .. " bytes")
	end
	g:ai_program(nil)
	T.ok(select(1, g:ai_find("Ter3")) > 0, "the game's binaries again")
end

-- The script methods refuse a slot outside the pool and a player that does not exist.
function S.script_slots_are_checked(T)
	if not ready(T) then return end
	local g = game(oracle.blobs(T.read, MAP))
	T.ok(not pcall(function() g:ai_free(-1) end), "slot -1 freed")
	T.ok(not pcall(function() g:ai_execute(100) end), "slot 100 run")
	T.ok(not pcall(function() g:ai_create(12, 0, nil, 0) end), "player 12's script")
end

return S
