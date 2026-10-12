-- Fixed-capacity entity pools with the native intrusive list primitives.
--
-- Records live in SoA arrays indexed by native record index. Links are stored in
-- `prev`/`next` arrays (0 = none) shared by every list of the pool, so a record is
-- in at most one list at a time, as in the original. List primitives match the
-- helpers used by every 1.17 pool (docs/research/atlas/p05.md, "List helpers"):
--   0x4119a0 unlink, 0x411930 insert after head, 0x411900 insert after,
--   0x411970 insert before.
-- Handles add a generation counter so stale references from events and saved
-- targets can be detected; the original uses raw pointers and has no such check.
local M = {}

local GENERATION_SCALE = 65536 -- handle = generation * 65536 + index

local Pool = {}
Pool.__index = Pool

-- capacity: number of records; first: native index of the first record (0 or 1).
function M.new(capacity, first)
	first = first or 1
	assert(capacity >= 1 and capacity < GENERATION_SCALE, "pool capacity out of range")
	local self = setmetatable({
		capacity = capacity,
		first = first,
		last = first + capacity - 1,
		prev = {},
		next = {},
		generation = {},
		count = 0,
		free = { head = 0, tail = 0 },
		active = { head = 0, tail = 0 },
	}, Pool)
	for i = first, self.last do
		self.prev[i], self.next[i], self.generation[i] = 0, 0, 0
	end
	return self
end

-- Native index 0 is a valid record in zero-based pools, so links and list heads
-- store a 1-based key (index - first + 1) and reserve 0 for "none".
local function key(pool, index) return index - pool.first + 1 end
local function index_of(pool, k) return k + pool.first - 1 end

-- Public API takes native indices; `prev`/`next` are indexed by native index.
function Pool:unlink(list, index)
	local p, n = self.prev[index], self.next[index]
	if p ~= 0 then self.next[index_of(self, p)] = n else list.head = n end
	if n ~= 0 then self.prev[index_of(self, n)] = p else list.tail = p end
	self.prev[index], self.next[index] = 0, 0
end

-- 0x411930: empty list -> node becomes head and tail; otherwise directly after head.
function Pool:insert_after_head(list, index)
	local k = key(self, index)
	if list.head == 0 then
		list.head, list.tail = k, k
		self.prev[index], self.next[index] = 0, 0
		return
	end
	self:insert_after(list, index_of(self, list.head), index)
end

-- 0x411900: insert `index` after `existing`, updating the tail.
function Pool:insert_after(list, existing, index)
	local k, e = key(self, index), key(self, existing)
	local n = self.next[existing]
	self.prev[index], self.next[index] = e, n
	self.next[existing] = k
	if n ~= 0 then self.prev[index_of(self, n)] = k else list.tail = k end
end

-- 0x411970: insert `index` before `existing`, updating the head.
function Pool:insert_before(list, existing, index)
	local k, e = key(self, index), key(self, existing)
	local p = self.prev[existing]
	self.prev[index], self.next[index] = p, e
	self.prev[existing] = k
	if p ~= 0 then self.next[index_of(self, p)] = k else list.head = k end
end

function Pool:head(list)
	if list.head == 0 then return nil end
	return index_of(self, list.head)
end

function Pool:next_of(index)
	local n = self.next[index]
	if n == 0 then return nil end
	return index_of(self, n)
end

-- Native index order of a list, for tests and fingerprints.
function Pool:indices(list)
	local out, i = {}, self:head(list)
	while i do
		out[#out + 1] = i
		i = self:next_of(i)
	end
	return out
end

-- Typical new-game initialization: records inserted in ascending order with
-- insert-after-head, giving free order first, last, last-1, ..., first+1
-- (CUnit init: 1, 1700, 1699, ..., 2).
function Pool:init_free_after_head()
	for i = self.first, self.last do self:insert_after_head(self.free, i) end
end

-- Takes the free head (unlink) and links it into the active list after its head.
-- Returns the index, or nil when the free list is empty.
function Pool:alloc()
	local index = self:head(self.free)
	if not index then return nil end
	self:unlink(self.free, index)
	self:insert_after_head(self.active, index)
	self.count = self.count + 1
	return index
end

function Pool:release(index)
	self:unlink(self.active, index)
	self:insert_after_head(self.free, index)
	self.count = self.count - 1
	-- Invalidates every handle to the released record.
	self.generation[index] = self.generation[index] + 1
end

function Pool:handle(index)
	return self.generation[index] * GENERATION_SCALE + index
end

-- Returns the index for a live handle, or nil if the record was released since.
function Pool:resolve(handle)
	local index = handle % GENERATION_SCALE
	if index < self.first or index > self.last then return nil end
	if self.generation[index] * GENERATION_SCALE + index ~= handle then return nil end
	return index
end

function Pool:snapshot()
	local prev, nxt, gen = {}, {}, {}
	for i = self.first, self.last do
		local k = key(self, i)
		prev[k], nxt[k], gen[k] = self.prev[i], self.next[i], self.generation[i]
	end
	return {
		count = self.count,
		free = { self.free.head, self.free.tail },
		active = { self.active.head, self.active.tail },
		prev = prev, next = nxt, generation = gen,
	}
end

function Pool:restore(s)
	self.count = s.count
	self.free.head, self.free.tail = s.free[1], s.free[2]
	self.active.head, self.active.tail = s.active[1], s.active[2]
	for i = self.first, self.last do
		local k = key(self, i)
		self.prev[i], self.next[i], self.generation[i] = s.prev[k], s.next[k], s.generation[k]
	end
end

return M
