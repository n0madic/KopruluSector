-- Unit counters, supply and score statistics (docs/research/atlas/p05.md, package item 3).
-- Per-type tables of 228 x 12 DWORDs: all units 0x529604, completed 0x52c0c4, kills
-- 0x52eb84, deaths 0x531644. Per-player arrays of 12 DWORDs from 0x5290c4: G.pstat[k][p]
-- is the array at 0x5290c4 + 0x30 * k. Methods of the game (game/simulation/game.lua).
local int = require("game.simulation.int")

local band = int.band
local bit_band = int.bit_band

local M = {}

M.TYPES, M.PLAYERS = 228, 12
M.PSTAT_BASE, M.PSTAT_ARRAYS = 0x5290c4, 28
M.NEUTRAL = 11
M.STATUS_HALLUCINATION = 0x40000000
M.SUBUNIT = 0x10
M.DOUBLE_SUPPLY = 0x400 -- units.dat flag: two units per egg (Zergling, Scourge)
M.SUPPLY_MAX = 400 -- 0x414400 outside team games

-- Per-player arrays by native address.
local function k(addr) return (addr - M.PSTAT_BASE) / 0x30 end
M.MEN_LIVE, M.MEN_COMPLETED, M.MEN_PRODUCED = k(0x5290c4), k(0x5290f4), k(0x529124)
M.UNIT_SCORE, M.BUILDINGS_LIVE, M.BUILDINGS_COMPLETED = k(0x5291b4), k(0x529214), k(0x529244)
M.BUILDINGS_PRODUCED, M.BUILDING_SCORE = k(0x529274), k(0x529304)
M.FACTORIES_LIVE, M.FACTORIES_COMPLETED = k(0x529364), k(0x529394)
M.SUPPLY_PROVIDED = { [1] = k(0x529424), [2] = k(0x5294b4), [4] = k(0x529544) } -- by StarEdit group bit
M.SUPPLY_USED = { [1] = k(0x529454), [2] = k(0x5294e4), [4] = k(0x529574) }
M.SUPPLY_MAX_ARRAYS = { k(0x529484), k(0x529514), k(0x5295a4) }

-- Types whose completion does not count as produced (0x4145a0's switch tables).
local NOT_PRODUCED_MEN = { [0x2c] = true, [0x3e] = true, [0x3f] = true, [0x44] = true, [0x67] = true }
local NOT_PRODUCED_BUILDINGS = { [0x84] = true, [0x85] = true, [0x89] = true, [0x90] = true, [0x92] = true }
local EGG_TYPES = { [0x24] = true, [0x3b] = true, [0x61] = true } -- Egg, Cocoon, Lurker Egg

function M.install(G)
	-- InitUnitCountsAndSupplyMax 0x414400 (single-player: no team game).
	function G:counts_init()
		self.count_all, self.count_completed, self.count_kills, self.count_deaths = {}, {}, {}, {}
		for i = 0, M.TYPES * M.PLAYERS - 1 do
			self.count_all[i], self.count_completed[i], self.count_kills[i], self.count_deaths[i] = 0, 0, 0, 0
		end
		self.pstat = {}
		for a = 0, M.PSTAT_ARRAYS - 1 do
			local row = {}
			for p = 0, M.PLAYERS - 1 do row[p] = 0 end
			self.pstat[a] = row
		end
		for _, a in ipairs(M.SUPPLY_MAX_ARRAYS) do
			for p = 0, M.PLAYERS - 1 do self.pstat[a][p] = M.SUPPLY_MAX end
		end
	end

	local function add(self, a, p, v)
		self.pstat[a][p] = int.s32(self.pstat[a][p] + v)
	end

	-- AccountLiveUnit 0x414760 (AddLiveUnitCounts 0x414070 = +1, RemoveLiveUnitCounts
	-- 0x414590 = -1).
	function G:account_live(n, delta)
		local u, d = self.u, self.dat.units
		if bit_band(u.status[n], M.STATUS_HALLUCINATION) ~= 0 then return end
		local t = u.type[n]
		if bit_band(d.special_ability_flags[t], M.SUBUNIT) ~= 0 then return end
		local p = u.owner[n]
		local i = t * M.PLAYERS + p
		self.count_all[i] = int.s32(self.count_all[i] + delta)
		local group = d.staredit_group_flags[t]
		local supply
		if group % 2 == 1 then
			if EGG_TYPES[t] then
				local queued = u["queue" .. u.queue_slot[n]][n]
				supply = d.supply_required[queued]
				if bit_band(d.special_ability_flags[queued], M.DOUBLE_SUPPLY) ~= 0 then supply = supply * 2 % 256 end
			else
				supply = d.supply_required[t]
				if bit_band(d.special_ability_flags[t], M.DOUBLE_SUPPLY) ~= 0 and bit_band(u.status[n], 1) == 0 then
					supply = supply * 2 % 256
				end
			end
			add(self, M.SUPPLY_USED[1], p, supply * delta)
		elseif bit_band(group, 2) ~= 0 then
			add(self, M.SUPPLY_USED[2], p, d.supply_required[t] * delta)
		elseif bit_band(group, 4) ~= 0 then
			add(self, M.SUPPLY_USED[4], p, d.supply_required[t] * delta)
		end
		if bit_band(group, 0x20) ~= 0 then add(self, M.FACTORIES_LIVE, p, delta) end
		if bit_band(group, 8) ~= 0 then
			add(self, M.MEN_LIVE, p, delta)
		elseif bit_band(group, 0x10) ~= 0 then
			add(self, M.BUILDINGS_LIVE, p, delta)
		elseif EGG_TYPES[t] then
			add(self, M.MEN_LIVE, p, delta)
		end
		if self.count_all[i] < 0 then self.count_all[i] = 0 end
	end

	-- AccountCompletedUnit 0x4145a0 (AddCompletedUnitCounts 0x414060 = +1); `score` adds the
	-- produced counts and build score except for the neutral player.
	function G:account_completed(n, delta, score)
		local u, d = self.u, self.dat.units
		if bit_band(u.status[n], M.STATUS_HALLUCINATION) ~= 0 then return end
		local t = u.type[n]
		if bit_band(d.special_ability_flags[t], M.SUBUNIT) ~= 0 then return end
		local p = u.owner[n]
		local i = t * M.PLAYERS + p
		self.count_completed[i] = int.s32(self.count_completed[i] + delta)
		local group = d.staredit_group_flags[t]
		if group % 2 == 1 then
			add(self, M.SUPPLY_PROVIDED[1], p, d.supply_provided[t] * delta)
		elseif bit_band(group, 2) ~= 0 then
			add(self, M.SUPPLY_PROVIDED[2], p, d.supply_provided[t] * delta)
		elseif bit_band(group, 4) ~= 0 then
			add(self, M.SUPPLY_PROVIDED[4], p, d.supply_provided[t] * delta)
		end
		if bit_band(group, 0x20) ~= 0 then add(self, M.FACTORIES_COMPLETED, p, delta) end
		if bit_band(group, 8) ~= 0 then
			add(self, M.MEN_COMPLETED, p, delta)
		elseif bit_band(group, 0x10) ~= 0 then
			add(self, M.BUILDINGS_COMPLETED, p, delta)
		end
		if score and p ~= M.NEUTRAL then
			if bit_band(group, 8) ~= 0 then
				if not NOT_PRODUCED_MEN[t] then add(self, M.MEN_PRODUCED, p, delta) end
				add(self, M.UNIT_SCORE, p, d.build_score[t] * delta)
			elseif bit_band(group, 0x10) ~= 0 then
				if not NOT_PRODUCED_BUILDINGS[t] then add(self, M.BUILDINGS_PRODUCED, p, delta) end
				add(self, M.BUILDING_SCORE, p, d.build_score[t] * delta)
			end
		end
		if self.count_completed[i] < 0 then self.count_completed[i] = 0 end
	end
end

return M
