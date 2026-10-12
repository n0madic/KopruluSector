local pool = require("game.simulation.pool")

local S = {}

local function same(T, actual, expected, label)
	T.eq(#actual, #expected, label .. " length")
	for i = 1, #expected do T.eq(actual[i], expected[i], label .. "[" .. i .. "]") end
end

-- CUnit init (P08 0x425960 InitializeUnitSystem): units 1..1700 inserted in order with
-- 0x411930 give the free order 1, 1700, 1699, ..., 2.
function S.cunit_initial_free_order(T)
	local p = pool.new(1700, 1)
	p:init_free_after_head()
	local order = p:indices(p.free)
	T.eq(#order, 1700)
	T.eq(order[1], 1); T.eq(order[2], 1700); T.eq(order[3], 1699); T.eq(order[1700], 2)
	T.eq(p:alloc(), 1)
	T.eq(p:alloc(), 1700)
end

-- Lone thingy pool: record 0 first, then 1..499 after the head (0, 499, ..., 1).
function S.zero_based_pool(T)
	local p = pool.new(500, 0)
	p:init_free_after_head()
	local order = p:indices(p.free)
	T.eq(order[1], 0); T.eq(order[2], 499); T.eq(order[500], 1)
	T.eq(p:alloc(), 0)
	T.eq(p:alloc(), 499)
	p:release(0)
	T.eq(p:indices(p.free)[2], 0) -- released records go directly after the head
end

function S.insert_primitives(T)
	local p = pool.new(6, 1)
	local l = { head = 0, tail = 0 }
	p:insert_after_head(l, 3)              -- 3
	p:insert_after_head(l, 4)              -- 3 4
	p:insert_after(l, 4, 5)                -- 3 4 5 (tail)
	p:insert_before(l, 3, 1)               -- 1 3 4 5 (head)
	p:insert_before(l, 4, 2)               -- 1 3 2 4 5
	same(T, p:indices(l), { 1, 3, 2, 4, 5 }, "list")
	T.eq(p.next[5], 0)
	p:unlink(l, 1); p:unlink(l, 5); p:unlink(l, 2)
	same(T, p:indices(l), { 3, 4 }, "after unlink")
	T.eq(p:head(l), 3)
	p:unlink(l, 3); p:unlink(l, 4)
	T.eq(l.head, 0); T.eq(l.tail, 0)
end

function S.empty_pool_and_count(T)
	local p = pool.new(2, 1)
	p:init_free_after_head()
	T.ok(p:alloc() and p:alloc())
	T.eq(p:alloc(), nil)
	T.eq(p.count, 2)
	p:release(2)
	T.eq(p.count, 1)
	T.eq(p:alloc(), 2)
end

function S.handles_detect_reuse(T)
	local p = pool.new(4, 1)
	p:init_free_after_head()
	local i = p:alloc()
	local h = p:handle(i)
	T.eq(p:resolve(h), i)
	p:release(i)
	T.eq(p:resolve(h), nil)
	T.eq(p:alloc(), 4)         -- released record went after the free head (0x411930)
	local j = p:alloc()
	T.eq(j, i)                 -- the next allocation reuses it
	T.eq(p:resolve(h), nil)    -- old handle stays stale
	T.eq(p:resolve(p:handle(j)), j)
	T.eq(p:resolve(99), nil)
end

function S.snapshot_restore(T)
	local p = pool.new(10, 0)
	p:init_free_after_head()
	for _ = 1, 6 do p:alloc() end
	p:release(8); p:release(0)
	local s = p:snapshot()
	local q = pool.new(10, 0)
	q:restore(s)
	same(T, q:indices(q.free), p:indices(p.free), "free")
	same(T, q:indices(q.active), p:indices(p.active), "active")
	T.eq(q.count, p.count)
	for _ = 1, 4 do T.eq(q:alloc(), p:alloc()) end
end

return S
