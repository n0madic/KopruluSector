-- The binary heaps of the path searches (atlas P30): the region search's (0x69b588,
-- RegionHeapSiftDown 0x491b20, RegionHeapChangePriority 0x491a70) and the local search's
-- (0x69d6c8, 0x4929b0, 0x492900, insertion 0x492a30) are the same code over the node's
-- priority. Index 0 holds a sentinel whose priority is -1. Insertion and upward repair move
-- parents on equal priorities; downward repair prefers the left child on a tie and stops when
-- the node is no worse than the chosen child.
local int = require("game.simulation.int")

local M = {}

local SENTINEL = { prio = -1 }

function M.new()
	return { count = 0, [0] = SENTINEL }
end

local function sift_up(h, i, node)
	h[0] = SENTINEL
	local p = int.idiv(i, 2)
	while h[p].prio >= node.prio do
		h[i] = h[p]
		i = p
		p = int.idiv(i, 2)
	end
	h[i] = node
end

function M.insert(h, node)
	h.count = h.count + 1
	sift_up(h, h.count, node)
end

function M.sift_down(h, i)
	local node = h[i]
	while i <= int.idiv(h.count, 2) do
		local c = i * 2
		if c < h.count and h[c].prio > h[c + 1].prio then c = c + 1 end
		if node.prio <= h[c].prio then
			h[i] = node
			return
		end
		h[i] = h[c]
		i = c
	end
	h[i] = node
end

-- The best node out: the last node moves to the root and sifts down.
function M.pop(h)
	local top = h[1]
	h[1] = h[h.count]
	h.count = h.count - 1
	M.sift_down(h, 1)
	return top
end

-- The change of a node's priority; a node that is not in the heap only gets the new value.
function M.change(h, node, prio)
	local old = node.prio
	if old == prio then return end
	node.prio = prio
	local i = h.count
	while i > 0 and h[i] ~= node do i = i - 1 end
	if i == 0 then return end
	if old <= prio then
		M.sift_down(h, i)
	else
		sift_up(h, i, node)
	end
end

return M
