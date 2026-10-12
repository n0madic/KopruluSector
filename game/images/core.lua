-- Constants and the native intrusive list helpers shared by the game/images modules.
local M = {}

M.NONE = -1
M.MAIN, M.HEALTH, M.CIRCLES, M.GHOSTS, M.BOXES = 5000, 12, 80, 64, 64
M.HEALTH_FIRST = M.MAIN
M.CIRCLES_FIRST = M.HEALTH_FIRST + M.HEALTH
M.GHOSTS_FIRST = M.CIRCLES_FIRST + M.CIRCLES
M.BOXES_FIRST = M.GHOSTS_FIRST + M.GHOSTS
M.IMAGES = M.BOXES_FIRST + M.BOXES
M.SPRITES = 2500
M.LONE = 500
M.ROWS = 256
M.TILE = 32

-- Image flags (image +0x0c).
M.F_MIRROR, M.F_HOLD_Y, M.F_TURNS, M.F_FULL_ISCRIPT = 0x02, 0x04, 0x08, 0x10
M.F_CLICKABLE, M.F_HIDDEN, M.F_FOLLOW = 0x20, 0x40, 0x80
-- Sprite flags (sprite +0x0e).
M.SF_DEPTH, M.SF_HIDDEN, M.SF_NOBRK = 0x10, 0x20, 0x80

local NONE = M.NONE

-- Native list helpers over shared prev/next arrays; `list` is { head =, tail = }.
local function unlink(prev, next, list, node)
	if list.head == node then list.head = next[node] end
	if list.tail == node then list.tail = prev[node] end
	if prev[node] ~= NONE then next[prev[node]] = next[node] end
	if next[node] ~= NONE then prev[next[node]] = prev[node] end
	next[node], prev[node] = NONE, NONE
end

-- 0x411900: `node` after `existing`, updating the tail.
local function insert_after(prev, next, list, existing, node)
	if list.tail == existing then list.tail = node end
	prev[node] = existing
	next[node] = next[existing]
	if next[existing] ~= NONE then prev[next[existing]] = node end
	next[existing] = node
end

-- 0x411930: directly after the head, or head and tail of an empty list.
local function insert_after_head(prev, next, list, node)
	if list.head == NONE then
		list.head, list.tail = node, node
		return
	end
	insert_after(prev, next, list, list.head, node)
end

-- 0x411970: `node` before `existing`, updating the head.
local function insert_before(prev, next, list, existing, node)
	if list.head == existing then list.head = node end
	prev[node] = prev[existing]
	next[node] = existing
	if prev[existing] ~= NONE then next[prev[existing]] = node end
	prev[existing] = node
end

M.unlink, M.insert_after, M.insert_after_head, M.insert_before = unlink, insert_after, insert_after_head, insert_before

local function list() return { head = NONE, tail = NONE } end
M.list = list

return M
