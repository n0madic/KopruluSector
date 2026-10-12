-- Structural save/load and deterministic fingerprints of an identical run.
local snapshot = require("game.simulation.snapshot")
local rng = require("game.simulation.rng")
local pool = require("game.simulation.pool")

local S = {}

-- A miniature world: a CUnit-sized pool driven by the game RNG.
local function new_world()
	local w = { rng = rng.game(1234), units = pool.new(1700, 1), hp = {}, step = 0 }
	w.units:init_free_after_head()
	w.rng:seed(0x13579BDF)
	return w
end

local function step(w)
	local r = w.rng
	r:set_enabled(1)
	local roll = r:range(1, 0, 99)
	if roll < 60 then
		local i = w.units:alloc()
		if i then w.hp[i] = r:range(2, 1, 0x7FFF) * 256 end
	else
		local i = w.units:head(w.units.active)
		if i then w.units:release(i); w.hp[i] = nil end
	end
	r:set_enabled(0)
	w.step = w.step + 1
end

local function state(w)
	return { step = w.step, rng = w.rng:snapshot(), units = w.units:snapshot(), hp = w.hp }
end

local function load(s)
	local w = new_world()
	w.step = s.step
	w.rng:restore(s.rng)
	w.units:restore(s.units)
	w.hp = s.hp
	return w
end

-- Reference fingerprint; identical on LuaJIT (JIT on/off), PUC Lua 5.1 and Defold.
local EXPECTED_FINGERPRINT = 0xc7d3a80d

function S.round_trip_values(T)
	local v = { 1, -1, 0, 2 ^ 53, -(2 ^ 53), 4294967295, "a\0b", true, false,
		{ x = 1, [3] = "c", y = { 1, 2 } }, {} }
	local out = snapshot.decode(snapshot.encode(v))
	T.eq(out[1], 1); T.eq(out[2], -1); T.eq(out[4], 2 ^ 53); T.eq(out[5], -(2 ^ 53))
	T.eq(out[6], 4294967295); T.eq(out[7], "a\0b"); T.eq(out[8], true); T.eq(out[9], false)
	T.eq(out[10].x, 1); T.eq(out[10][3], "c"); T.eq(out[10].y[2], 2)
	T.eq(snapshot.encode(out), snapshot.encode(v))
	T.raises(function() snapshot.encode({ 0.5 }) end, "non%-integral")
	T.raises(function() snapshot.decode(snapshot.encode(v) .. "x") end, "trailing")
end

function S.key_order_independent(T)
	local a, b = {}, {}
	for i = 1, 50 do a["k" .. i] = i end
	for i = 50, 1, -1 do b["k" .. i] = i end
	T.eq(snapshot.encode(a), snapshot.encode(b))
end

function S.fnv_known(T)
	T.eq(snapshot.fnv1a(""), 0x811C9DC5)
	T.eq(snapshot.fnv1a("a"), 0xE40C292C)
	T.eq(snapshot.fnv1a("foobar"), 0xBF9CF968)
end

function S.save_load_continuation(T)
	local a = new_world()
	for _ = 1, 3000 do step(a) end
	local saved = snapshot.encode(state(a))
	for _ = 1, 3000 do step(a) end

	local b = load(snapshot.decode(saved))
	for _ = 1, 3000 do step(b) end
	T.eq(snapshot.fingerprint(state(b)), snapshot.fingerprint(state(a)))

	local c = new_world()
	for _ = 1, 6000 do step(c) end
	local fp = snapshot.fingerprint(state(c))
	T.eq(fp, snapshot.fingerprint(state(a)))
	if EXPECTED_FINGERPRINT then
		T.eq(fp, EXPECTED_FINGERPRINT, "cross-backend fingerprint")
	else
		T.env.log(("fingerprint after 6000 steps: 0x%08x"):format(fp))
	end
end

return S
