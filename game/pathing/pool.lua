-- The path record pool (Sai_path.cpp; atlas P30, docs/research/native-path-storage.md): 1024
-- records of 128 bytes that units point to through CUnit +0x100. Records are reused without
-- clearing, so the pool is kept byte for byte: a record's stale bytes read the same as the
-- original's. Record numbers are 1-based (0 is null); a free record's first DWORD is the next
-- free record's number and its second its own. Methods of the game.
local int = require("game.simulation.int")

local M = {}

M.RECORDS, M.SIZE = 1024, 0x80
M.MAX_REQUEST = 0x80
-- Header offsets.
M.SRC, M.NEXT, M.DEST, M.FRAME = 0x00, 0x04, 0x08, 0x0c
M.FLAGS, M.RETRY = 0x18, 0x19
M.STALE = 1 -- flag: the move target changed after the path was made
M.FULL_REGIONS, M.REGIONS, M.REGION_INDEX, M.POINTS, M.POINT_INDEX = 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
M.PAYLOAD = 0x20
M.MAX_POINTS = 0x17 -- 0x42c840 keeps at most 23 points with one region

function M.install(G)
	-- CreatePathPool 0x48f000: zeroed records, each free record linked to the next.
	function G:paths_init()
		local b = {}
		for k = 0, M.RECORDS * M.SIZE - 1 do b[k] = 0 end
		self.path_bytes = b
		for k = 1, M.RECORDS do
			self:path_w32(k, 0, k < M.RECORDS and k + 1 or 0)
			self:path_w32(k, 4, k)
		end
		self.path_free_head = 1
		-- The collision subject (0x69b57c) and the all-units switch (0x69b578) of
		-- IsPathObstacleUnit 0x42e490, set by movement states that are not ported yet.
		self.path_subject, self.path_all_units = 0, 0
	end

	-- Byte access to record k at offset off.
	function G:path_u8(k, off) return self.path_bytes[(k - 1) * M.SIZE + off] end
	function G:path_s8(k, off) return int.s8(self.path_bytes[(k - 1) * M.SIZE + off]) end
	function G:path_w8(k, off, v) self.path_bytes[(k - 1) * M.SIZE + off] = v % 256 end
	function G:path_u16(k, off)
		local b, at = self.path_bytes, (k - 1) * M.SIZE + off
		return b[at] + b[at + 1] * 256
	end
	function G:path_s16(k, off) return int.s16(self:path_u16(k, off)) end
	function G:path_w16(k, off, v)
		local b, at = self.path_bytes, (k - 1) * M.SIZE + off
		v = v % 65536
		b[at], b[at + 1] = v % 256, int.shr(v, 8)
	end
	function G:path_u32(k, off) return self:path_u16(k, off) + self:path_u16(k, off + 2) * 65536 end
	function G:path_w32(k, off, v)
		v = v % 4294967296
		self:path_w16(k, off, v % 65536)
		self:path_w16(k, off + 2, math.floor(v / 65536))
	end
	-- A point (two signed WORDs) at offset off.
	function G:path_point(k, off) return self:path_s16(k, off), self:path_s16(k, off + 2) end
	function G:path_set_point(k, off, x, y)
		self:path_w16(k, off, x)
		self:path_w16(k, off + 2, y)
	end

	-- AllocPathRecord 0x48e8d0: the free head, or 0 (requests above 128 bytes fail).
	function G:path_alloc(size)
		local k = self.path_free_head
		if size > M.MAX_REQUEST or k == 0 then return 0 end
		self.path_free_head = self:path_u32(k, 0)
		return k
	end

	-- FreePathRecord 0x48e910: record k becomes the free head.
	function G:path_release(k)
		self:path_w32(k, 4, k)
		self:path_w32(k, 0, self.path_free_head)
		self.path_free_head = k
	end

	-- ReleaseUnitPath 0x48e880 / FreeUnitPath 0x42c8a0.
	function G:path_free(n)
		local k = self.u.path[n]
		if k == 0 then return end
		self:path_release(k)
		self.u.path[n] = 0
	end

	-- ClampPathSizeAndAllocate 0x42c840: the region and point counts that fit one record
	-- (written back before the allocation), then the record or 0.
	function G:path_alloc_counts(regions, points)
		if int.band((regions + points * 2) * 2 + 0x23, 0xfffc) > M.SIZE then
			if points < 0x18 then
				regions = (0x18 - points) * 2
			else
				points, regions = M.MAX_POINTS, 1
			end
		end
		return self:path_alloc(int.band((regions + points * 2) * 2 + 0x23, 0xfffc)), regions, points
	end

	-- FlagPathMoveTargetChanged 0x42c790: the path's goal is stale (+0x18 bit 0).
	function G:path_mark_stale(n)
		local k = self.u.path[n]
		if k ~= 0 then self:path_w8(k, M.FLAGS, int.bor(self:path_u8(k, M.FLAGS), M.STALE)) end
	end

	-- SetPathRetryTimer 0x48e970.
	function G:path_set_retry(n, v)
		local k = self.u.path[n]
		if k ~= 0 then self:path_w8(k, M.RETRY, v) end
	end

	-- PollPathRetryTimer 0x48e950: true when the old timer was 0; a positive timer counts down.
	function G:path_poll_retry(n)
		local k = self.u.path[n]
		if k == 0 then return false end
		local t = self:path_u8(k, M.RETRY)
		if t == 0 then return true end
		self:path_w8(k, M.RETRY, t - 1)
		return false
	end

	-- The record's next point (+4) as a packed DWORD, as the original compares it.
	function G:path_next_packed(k) return self:path_u32(k, M.NEXT) end
end

return M
