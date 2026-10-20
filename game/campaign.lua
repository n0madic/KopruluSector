-- Campaign progression of the original (uiSingle.cpp, atlas P41 0x4bb5b0..0x4bbc20): the rows of the six campaign
-- tables (game/data/campaign.lua), the next mission after a game, the SetNextScenario resolver and the profile's
-- progress. Pure state functions: the caller keeps the run and the profile (game/save) and starts the games.
--
-- A run: { expansion = 0|1 (the expansion byte 0x536720), row = { table =, index = } or nil (the cached row
-- 0x6ca718), mission = the mission index of the game (0x526524), pending = the next scenario's name ("" none,
-- 0x526526; the game's g.triggers.next_scenario after it) }.
-- A profile: { original = { [0] = Zerg, [1] = Terran, [2] = Protoss }, expansion = { ... } }, the highest mission
-- index reached per race (character record +0x28 / +0x34).
local M = {}

M.TABLES_PER_GROUP = 3 -- Zerg, Terran, Protoss of 0x50e0b8 (group 0) and 0x50e0c4 (group 1)
M.MISSIONS = 65 -- the directory strings 0x51abb8
local ORIGINAL_EXTENSION = ".scm"
-- The glue states and game modes the next step leads to (0x66a274, 0x6662b8).
M.GLUE_MENU, M.GLUE_EXPANSION_MENU = 6, 22
M.GLUE_READY_ROOM = { [0] = 8, [1] = 7, [2] = 9 } -- by race: Zerg, Terran, Protoss (any other race: 9)
M.MODE_EPILOG = 9
-- TODO: StartCampaignEpisode 0x4bb5b0 (a new run from the mission history popup, RunMissionHistoryPopup 0x45fef0) and the
-- popup's list of reached rows come with the campaign menus (plan phase 9).

function M.new_profile()
	return { original = { [0] = 0, [1] = 0, [2] = 0 }, expansion = { [0] = 0, [1] = 0, [2] = 0 } }
end

-- Raises `profile`'s progress to at least `other`'s (a loaded save never takes progress away from the profile).
function M.merge_profile(profile, other)
	for _, group in ipairs({ "original", "expansion" }) do
		for race = 0, 2 do
			profile[group][race] = math.max(profile[group][race] or 0, other[group][race] or 0)
		end
	end
	return profile
end

-- The glue state of the ready room of a race (any other race: Protoss's).
local function ready_room(race)
	return M.GLUE_READY_ROOM[race] or M.GLUE_READY_ROOM[2]
end

function M.row(data, ref)
	return ref and data.tables[ref.table].rows[ref.index]
end

-- FindCampaignRowByMission 0x4bbae0: in the three tables of a group, the first playable row of the mission.
local function find_row(data, group, mission)
	for k = group * M.TABLES_PER_GROUP + 1, group * M.TABLES_PER_GROUP + M.TABLES_PER_GROUP do
		for i, r in ipairs(data.tables[k].rows) do
			if r.mission == 0 then break end
			if r.cinematic == 0 and r.mission == mission then return { table = k, index = i } end
		end
	end
	return nil
end

-- The current row (0x4bb790 step 3): the cached one when it is the game's mission, else looked up in the original's
-- tables, then in the expansion's.
local function current_row(data, run)
	local r = M.row(data, run.row)
	if r and r.mission == run.mission then return run.row end
	return find_row(data, 0, run.mission) or find_row(data, 1, run.mission)
end

-- FindMissionIndexByBasename 0x4bb9d0: from index, the first directory whose basename (after its last "\") starts
-- with name, case-insensitively (SStrCmpI over the name's length).
local function find_index(data, name, index)
	local want = name:lower()
	while index < M.MISSIONS do
		local dir = data.directories[index] or ""
		local slash = dir:match(".*()\\")
		if slash and dir:sub(slash + 1, slash + #name):lower() == want then return index end
		index = index + 1
	end
	return nil
end

-- ResolveNextScenarioRow 0x4bba20: a SetNextScenario file name to a row; without a dot, or no match, the current
-- row (the mission just won is played again).
function M.resolve(data, run, name)
	local dot = name:match(".*()%.")
	if not dot then return run.row end
	local group = name:sub(dot):lower() == ORIGINAL_EXTENSION and 0 or 1
	local base, index = name:sub(1, dot - 1), 0
	while true do
		index = find_index(data, base, index)
		if not index then return run.row end
		local ref = find_row(data, group, index)
		if ref then return ref end
		index = index + 1
	end
end

-- RecordCampaignProgress 0x4bbc20: the row's race's field rises to its mission.
local function record_progress(data, run, profile, ref)
	local r = M.row(data, ref)
	local field = run.expansion ~= 0 and profile.expansion or profile.original
	if (field[r.race] or 0) < r.mission then field[r.race] = r.mission end
end

-- RecordNextMissionReached 0x4bb970 (the score screen, any result): the following row's mission is reached.
function M.record_next_reached(data, run, profile)
	local ref = current_row(data, run)
	if not ref or M.row(data, ref).mission == 0 then return end
	record_progress(data, run, profile, { table = ref.table, index = ref.index + 1 })
end

-- AdvanceCampaignAfterGame 0x4bb790 after a game (won: the local player's victory). Returns what follows, or nil
-- (the main menu, glue state 0):
--   { restart = true } the lost mission again (mode 5); the pending name stays;
--   { mission = index, race = r, glue = ready room } the next game (CreateCampaignMissionGame 0x4bb6c0);
--   { cinematic = id, mission = index } a cinematic (mode 10), after which advance(..., true) continues;
--   { glue = 6 or 22 } or { epilog = true } the campaign's end.
-- A won game shows the mission's finale text first (ShowMissionFinaleText 0x414fc0, the presentation's).
function M.advance(data, run, profile, won)
	if not won then return { restart = true } end
	local ref = current_row(data, run)
	if not ref or M.row(data, ref).mission == 0 then return nil end
	if run.pending ~= "" then
		ref = M.resolve(data, { row = ref }, run.pending)
		run.pending = ""
	else
		ref = { table = ref.table, index = ref.index + 1 }
	end
	run.row = ref
	record_progress(data, run, profile, ref)
	local r = M.row(data, ref)
	if r.mission == 0 then
		run.row = nil
		if run.expansion ~= 0 then
			if r.race ~= 0 then return { glue = M.GLUE_EXPANSION_MENU } end
			return { epilog = true }
		end
		if r.race == 2 then return { epilog = true } end
		return { glue = M.GLUE_MENU }
	end
	run.mission = r.mission
	if r.cinematic ~= 0 then return { cinematic = r.cinematic, mission = r.mission } end
	return { mission = r.mission, race = r.race, glue = ready_room(r.race) }
end

-- A run from a mission played directly (the warp of the Ophelia cheat, HandleMissionWarpCheat 0x40e030, or a
-- mission chosen in a tool): its row and expansion byte from the tables that list it (the original's first, as
-- AdvanceCampaignAfterGame looks it up), nil for a mission of no campaign.
function M.run_of_mission(data, mission)
	for group = 0, 1 do
		local ref = find_row(data, group, mission)
		if ref then return { expansion = group, row = ref, mission = mission, pending = "" } end
	end
	return nil
end

-- What follows a game that ended with `result` (2 defeat, 3 victory, 5 draw) and left the next scenario's name
-- `next_scenario`: the score screen's RecordNextMissionReached, then AdvanceCampaignAfterGame and its cinematics.
-- Returns what advance returns after the cinematics (each cinematic id in `cinematics`).
function M.after_game(data, run, profile, result, next_scenario)
	run.pending = next_scenario or ""
	M.record_next_reached(data, run, profile)
	local step, cinematics = M.advance(data, run, profile, result == 3), {}
	while step and step.cinematic do
		cinematics[#cinematics + 1] = step.cinematic
		step = M.advance(data, run, profile, true)
	end
	return step, cinematics
end

-- The generated mission name of a mission index ("terran/terran01", generated/missions/<name>.kpm).
function M.mission_name(data, index)
	local dir = data.directories[index]
	if not dir or dir == "" then return nil end
	return (dir:lower():gsub("^[^\\]*\\", ""):gsub("\\", "/"))
end

return M
