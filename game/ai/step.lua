-- The AI step of the object step (StepAi 0x48dd90 with StepAiTimers 0x48df50; atlas P39).
-- Its counters run in every game; the work for computer players (scripts, regions, towns,
-- spending, attacks) belongs to the AI phase and raises with its native address here.
-- Methods of the game.
local int = require("game.simulation.int")

local band, bnot = int.band, int.bnot
local bit_band = int.bit_band

local M = {}

M.CONTROLLER_COMPUTER = 1
M.REGION_STEPS = 0x14d -- regions visited per step by UpdateAiRegions 0x4b0d70
M.REGION_CYCLE = 0x9c4
M.PLAYERS = 8
M.RESOURCE_PERIOD = 0x1c2
M.UNDER_ATTACK_PERIOD, M.UNDER_ATTACK_ROUNDS = 300, 6
M.STATUS_UNDER_ATTACK = 0x80
M.SPENDING_EARLY, M.SPENDING_LATE, M.EARLY_SECONDS = 7, 37, 0x1194
M.ATTACK_EVALUATION_SECONDS = 0x167
M.DAT_RESOURCE_CONTAINER = 0x2000

function M.install(G)
	-- InitAiState 0x48db70 (counter part) and InitAiRegions 0x4b0640 (cursor part).
	function G:ai_init()
		self.ai = {
			terrain_changed = 0, period = 0x3c, cycle = 0xf, rounds = 6, -- 0x69b560 / 564 / 568 / 56c
			region_player = 0, region_cursor = 0, -- 0x6b1a94 / 0x6b1a98
			spending_player = 0, town_player = 0, -- 0x67f7a8 / 0x685574
			scripts = 0, -- running AI scripts (0x6a2e70)
		}
	end

	local function computer(self, p)
		return self.players[p].controller == M.CONTROLLER_COMPUTER
	end

	-- UpdateAiRegions 0x4b0d70.
	local function update_regions(self)
		local ai = self.ai
		local count = self.regions.count
		for _ = 1, M.REGION_STEPS do
			if ai.region_cursor < count and computer(self, ai.region_player) then
				error("ai: region update of a computer player (0x4b0ce0)", 0)
			end
			ai.region_cursor = ai.region_cursor + 1
			if ai.region_cursor == M.REGION_CYCLE then
				ai.region_player = (ai.region_player + 1) % M.PLAYERS
				ai.region_cursor = 1
				-- RebuildEnemyStrengthMaps 0x4abf90 writes only the AI regions of computer players.
				-- TODO: the AI region data itself (phase 5); nothing before the AI reads it.
				for p = 0, M.PLAYERS - 1 do
					if computer(self, p) then error("ai: enemy strength maps (0x4abf90)", 0) end
				end
				if computer(self, ai.region_player) then error("ai: region refresh (0x4831e0, 0x4aa8f0)", 0) end
			end
		end
	end

	-- RefreshResourceAreas 0x496c80 works on the resource areas of GenerateResourceAreas
	-- 0x497a20, which exist on maps with resources.
	local function refresh_resource_areas(self)
		if self.resource_areas == nil then
			local n, flags = self.lists.active.head, self.dat.units.special_ability_flags
			self.resource_areas = false
			while n ~= 0 do
				if bit_band(flags[self.u.type[n]], M.DAT_RESOURCE_CONTAINER) ~= 0 then self.resource_areas = true end
				n = self.u.next[n]
			end
		end
		if self.resource_areas then error("ai: resource areas (0x497a20, 0x496c80)", 0) end
	end

	-- StepAiTimers 0x48df50.
	local function step_timers(self)
		local ai, u = self.ai, self.u
		if ai.cycle == 0 then
			ai.cycle = M.UNDER_ATTACK_PERIOD
			local again = ai.rounds ~= 0
			if again then ai.rounds = ai.rounds - 1 end
			if not again or ai.terrain_changed ~= 0 then
				ai.rounds, ai.terrain_changed = M.UNDER_ATTACK_ROUNDS, 0
				local n = self.lists.active.head
				while n ~= 0 do
					u.status[n] = band(u.status[n], bnot(M.STATUS_UNDER_ATTACK))
					n = u.next[n]
				end
			end
		else
			ai.cycle = ai.cycle - 1
		end
		local due = ai.period == 0
		ai.period = (ai.period - 1) % 4294967296
		if not due then return end
		ai.period = self.elapsed < M.EARLY_SECONDS and M.SPENDING_EARLY or M.SPENDING_LATE
		-- RunAiSpendingPass 0x483400.
		ai.spending_player = (ai.spending_player + 1) % M.PLAYERS
		if computer(self, ai.spending_player) then error("ai: spending pass (0x483400)", 0) end
		-- MaintainNextPlayerTowns 0x486c80: no player has towns before the AI phase.
		ai.town_player = (ai.town_player + 1) % M.PLAYERS
		-- EvaluateGlobalAttackOpportunity 0x4acdf0.
		if self.elapsed > M.ATTACK_EVALUATION_SECONDS then error("ai: attack evaluation (0x4acdf0)", 0) end
	end

	-- StepAi 0x48dd90.
	function G:ai_step()
		if self.ai.scripts ~= 0 then error("ai: scripts (0x4938e0)", 0) end
		update_regions(self)
		refresh_resource_areas(self)
		step_timers(self)
	end
end

return M
