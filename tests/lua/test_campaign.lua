-- Campaign progression (game/campaign) against the original: the emulator's campaign chains
-- (tests/golden/oracle/campaign_chains.lua, tools/oracle/export_campaign_chains.py) and the resolver's contract
-- (atlas P41 0x4bba20). Needs the converter's generated/data/campaign.kpd; skipped without it.
local campaign = require("game.campaign")
local campaign_data = require("game.data.campaign")

local DATA = "generated/data/campaign.kpd"
local CHAINS = "tests/golden/oracle/campaign_chains.lua"
local RESULT_VICTORY = 3
local MODE_MENU = 4 -- the game mode the campaign's end leaves to when it names a glue state (no epilog)
-- The profile progress as the emulator saves it: the original's Zerg, Terran, Protoss, then the expansion's.
local PROGRESS_FIELDS = { { "original", 0 }, { "original", 1 }, { "original", 2 }, { "expansion", 0 },
	{ "expansion", 1 }, { "expansion", 2 } }

local function load_data(T)
	if not T.has(DATA) then return nil end
	return campaign_data.parse(T.read(DATA), DATA)
end

local function progress(profile)
	local out = {}
	for k, f in ipairs(PROGRESS_FIELDS) do out[k] = profile[f[1]][f[2]] end
	return table.concat(out, ",")
end

local S = {}

function S.tables(T)
	local data = load_data(T)
	if not data then return end
	T.eq(#data.tables, 6)
	local selectable, hidden = 0, 0
	for k = 1, 6 do
		for _, r in ipairs(data.tables[k].rows) do
			if r.mission ~= 0 and r.cinematic == 0 then
				if r.hidden ~= 0 then hidden = hidden + 1 else selectable = selectable + 1 end
			end
		end
	end
	T.eq(selectable, 57)
	T.eq(hidden, 7)
	T.eq(campaign.mission_name(data, 1), "terran/tutorial")
	T.eq(campaign.mission_name(data, 12), "zerg/zerg01")
	T.eq(data.carried, 0x28000200)
end

-- ResolveNextScenarioRow 0x4bba20: the extension picks the tables, the basename is a case-insensitive prefix, an
-- index without a playable row in the picked tables goes on to the next (Zerg10.scx: the original's zerg10 first),
-- a name without a dot or without a match keeps the current row.
function S.resolver(T)
	local data = load_data(T)
	if not data then return end
	local cur = { row = { table = 1, index = 1 } }
	local function mission(name)
		return campaign.row(data, campaign.resolve(data, cur, name)).mission
	end
	T.eq(mission("Zerg10.scx"), 64)
	T.eq(mission("zerg10.SCM"), 21)
	T.eq(mission("Terran05a.scx"), 44)
	T.eq(mission("terran05b.scx"), 45)
	T.eq(mission("Zerg04F.SCX"), 57)
	T.eq(mission("Zerg05"), campaign.row(data, cur.row).mission) -- no dot
	T.eq(mission("nowhere.scx"), campaign.row(data, cur.row).mission)
end

-- Every name a campaign map's Set Next Scenario names: an expansion map's ".scx" resolves to the expansion
-- directory of that basename (its own campaign's), not to the original campaign's map of the same name.
function S.next_scenario_names(T)
	local data = load_data(T)
	if not data then return end
	local golden = assert((loadstring or load)(T.read(CHAINS)))()
	T.eq(#golden.next_scenarios, 31)
	local cur = { row = { table = 1, index = 1 } }
	for _, n in ipairs(golden.next_scenarios) do
		local ref = campaign.resolve(data, cur, n.name)
		T.ok(ref ~= cur.row, n.name .. " resolves")
		local want = n.map:match("^(.*\\)") .. n.name:match("^(.*)%."):lower()
		T.eq(data.directories[campaign.row(data, ref).mission], want, n.map .. " " .. n.name)
	end
end

-- A loaded save never takes progress away from the profile.
function S.merge_profile(T)
	local profile = campaign.new_profile()
	profile.original[1], profile.expansion[2] = 12, 3
	local saved = campaign.new_profile()
	saved.original[1], saved.original[2], saved.expansion[2] = 4, 7, 9
	campaign.merge_profile(profile, saved)
	T.eq(progress(profile), "0,12,7,0,0,9")
end

-- Each chain from its first mission: after every game the score screen's RecordNextMissionReached 0x4bb970, then
-- AdvanceCampaignAfterGame 0x4bb790 (with the name the mission's SetNextScenario left); a cinematic is played and
-- the advance runs again. The next mission, the profile's progress and the chain's end are the original's.
function S.chains(T)
	local data = load_data(T)
	if not data then return end
	local chains = assert((loadstring or load)(T.read(CHAINS)))().cases
	for _, c in ipairs(chains) do
		local profile = campaign.new_profile()
		local run = { expansion = c.expansion, mission = c.missions[1].mission, pending = "" }
		for k, m in ipairs(c.missions) do
			local what = c.name .. " mission " .. m.mission
			T.eq(run.mission, m.mission, what)
			local step = campaign.after_game(data, run, profile, m.result, m.next_scenario)
			if m.result ~= RESULT_VICTORY then
				T.ok(step and step.restart, what .. ": a lost mission restarts")
			elseif m.next_mission then
				T.eq(step and step.mission, m.next_mission, what)
			elseif k == #c.missions and c.end_mode then
				T.ok(step ~= nil, what .. ": the campaign ends with a step")
				if step then
					T.eq(step.epilog and campaign.MODE_EPILOG or MODE_MENU, c.end_mode, what .. ": the end's mode")
					if not step.epilog then T.eq(step.glue, c.end_glue, what .. ": the end's glue state") end
				end
			end
			if m.progress and c.name:find("^exp") or m.progress and c.name:find("^orig") then
				T.eq(progress(profile), table.concat(m.progress, ","), what .. ": progress")
			end
		end
	end
end

return S
