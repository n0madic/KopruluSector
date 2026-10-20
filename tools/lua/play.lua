-- Headless mission runs (plan phase 8 item 3): a native game of a generated mission replays its input (a golden's
-- logs, or the emulator's commands.jsonl and ui.jsonl) for N object steps or until the local player's result, and
-- prints what the game reached; a chain runs its games in order and takes each next mission from the campaign's
-- progression (game/campaign.lua) after the game the player won or lost.
--
-- Usage (repository root):
--   luajit tools/lua/play.lua GOLDEN.lua [STEPS]                 a golden run (tests/golden/oracle/{runs,chains,...})
--   luajit tools/lua/play.lua --mission M.kpm SEED EXPANSION STEPS [COMMANDS.jsonl [UI.jsonl]] [--carry-out FILE]
--   luajit tools/lua/play.lua --chain GOLDEN1.lua GOLDEN2.lua ...  a campaign chain's games, in order, each started
--                                                                  with what the game before left (KpCarry)
-- STEPS: the last object step (a golden's: its last compared step); "result": until the local player's result.
-- Output: "step <n> frame <f>", "result <value> step <n> frame <f>" when the result is set, "next_scenario <name>",
-- "fingerprint <crc>" (oracle.fingerprint of the last step's state); a chain adds "next <mission> <name>" after each
-- game and fails when the next game is not that mission. --mission takes the chain context of the environment
-- (KP_CARRY, KP_TURN: oracle.chain_opts) and --carry-out writes the game's g:carry() for the next game of a chain.
package.path = "./?.lua;" .. package.path
bit = bit or require("bit")

local oracle = require("tools.lua.oracle")
local campaign = require("game.campaign")

local RESULT_LIMIT = 100000 -- the steps a run "until the result" may take

local function read(path)
	local f = assert(io.open(path, "rb"))
	local s = f:read("*a")
	f:close()
	return s
end

local function load_golden(path)
	return assert((loadstring or load)(read(path)))()
end

-- Runs a game to `steps` (a number, or "result"); prints the lines; returns the local player's result (0 none).
local function play(game, run, steps)
	local result, limit = 0, steps == "result" and RESULT_LIMIT or steps
	for step = 2, limit do
		run.to(step)
		local r = game.results[game.local_player]
		if r ~= 0 and result == 0 then
			result = r
			print(("result %d step %d frame %d"):format(r, step, game.frame))
			if steps == "result" then break end
		end
	end
	if steps == "result" and result == 0 then
		io.stderr:write(("play: no result within %d steps\n"):format(RESULT_LIMIT))
		os.exit(1)
	end
	print(("step %d frame %d"):format(run.step, game.frame))
	local name = game:next_scenario()
	if name ~= "" then print("next_scenario " .. name) end
	print(("fingerprint %d"):format(oracle.fingerprint(oracle.state_crcs(game, run.step >= oracle.DRAWN_STEP))))
	return result, name
end

local function golden_run(path, steps)
	local g = load_golden(path)
	local game, run = oracle.start_golden(read, g)
	return g, play(game, run, steps or oracle.last_step(g))
end

if arg[1] == "--mission" then
	local carry_out
	for k = #arg - 1, 1, -1 do
		if arg[k] == "--carry-out" then
			carry_out = table.remove(arg, k + 1)
			table.remove(arg, k)
		end
	end
	local game = oracle.start_mission(read, arg[2], tonumber(arg[3]), tonumber(arg[4]), oracle.chain_opts(read))
	local commands = arg[6] and oracle.parse_commands(read(arg[6])) or nil
	local ui = arg[7] and oracle.parse_ui(read(arg[7])) or nil
	play(game, oracle.runner(game, commands, ui), arg[5] == "result" and "result" or tonumber(arg[5]))
	if carry_out then
		local f = assert(io.open(carry_out, "wb"))
		f:write(game:carry())
		f:close()
	end
elseif arg[1] == "--chain" then
	local games = {}
	for k = 2, #arg do games[#games + 1] = { g = load_golden(arg[k]), name = arg[k] } end
	local failed, why = oracle.campaign_chain(read, games, function(g, name, opts)
		print("game " .. name)
		local game, r = oracle.start_golden(read, g, opts) -- what the game before left
		play(game, r, "result")
		return game
	end, function(_, step, expected, data)
		print(("next %s %s"):format(tostring(expected), expected and campaign.mission_name(data, expected) or
			(step and step.epilog and "epilog" or step and ("glue " .. step.glue) or "menu")))
	end)
	if failed then
		io.stderr:write(("play: %s: %s\n"):format(failed, why))
		os.exit(1)
	end
else
	golden_run(arg[1], arg[2] == "result" and "result" or tonumber(arg[2]))
end
