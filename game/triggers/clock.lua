-- The trigger step's clocks (RunTriggerStep 0x4be500 with TickResultDialogTimer 0x4be340
-- and TickTriggerClocks 0x4be410; atlas P27): the elapsed game seconds (0x5349d8) that other
-- systems read, and the countdowns of the trigger pass, the result check and the
-- leaderboard. Running triggers, countdown timers, leaderboards and the game result belong
-- to the trigger phase and raise with their native address here. Methods of the game.
local M = {}

M.SECOND_STEPS = 0xf -- 0x6ca744 counts 16 steps per elapsed second
M.TRIGGER_PERIOD = 0x1e -- 0x6ca748
M.RESULT_PERIOD = 0x2d -- 0x6ca74c
M.LEADERBOARD_PERIOD = 0xf -- 0x6ca750
M.RESULT_VICTORY, M.RESULT_DEFEAT, M.RESULT_DRAW = 2, 3, 5

function M.install(G)
	-- The trigger globals that RunGame finds after the trigger setup of StartGame.
	function G:trigger_clock_init(has_triggers)
		self.elapsed = 0
		self.trigger_clock = {
			second = M.SECOND_STEPS, pass = 1, result = 0, leaderboard = M.LEADERBOARD_PERIOD,
			game_timer = 0, -- 0x5349d4: the trigger countdown timer of the game
			countdowns = 0, -- active player countdowns (0x6ca760)
			leaderboard_kind = 0, -- 0x5349ec
		}
		self.has_triggers = has_triggers
		self.results = {}
		for p = 0, 7 do self.results[p] = 0 end -- 0x5349e0
	end

	-- A counter that is tested before its decrement: true when it was 0.
	local function expired(c, field, period, bits)
		local was = c[field]
		c[field] = (was - 1) % bits
		if was == 0 then
			c[field] = period
			return true
		end
		return false
	end

	-- RunTriggerStep 0x4be500 (the game is never paused in the simulation's frames).
	function G:trigger_step()
		local c = self.trigger_clock
		-- TickResultDialogTimer 0x4be340 (not in a replay).
		if expired(c, "result", M.RESULT_PERIOD, 65536) then
			local r = self.results[self.local_player]
			if r == M.RESULT_VICTORY or r == M.RESULT_DEFEAT or r == M.RESULT_DRAW then
				error("triggers: game result dialog (0x4bcf80, 0x4bcff0)", 0)
			end
		end
		-- TickTriggerClocks 0x4be410.
		if c.countdowns ~= 0 then error("triggers: player countdowns (0x4be410)", 0) end
		if expired(c, "leaderboard", M.LEADERBOARD_PERIOD, 65536) and c.leaderboard_kind ~= 0 then
			error("triggers: leaderboard (0x4ba910)", 0)
		end
		if expired(c, "second", M.SECOND_STEPS, 65536) then
			self.elapsed = self.elapsed + 1
			if c.game_timer ~= 0 then error("triggers: game countdown timer (0x4bde30)", 0) end
		end
		if expired(c, "pass", M.TRIGGER_PERIOD, 65536) and self.has_triggers then
			error("triggers: trigger pass (0x4bdfd0)", 0)
		end
	end
end

return M
