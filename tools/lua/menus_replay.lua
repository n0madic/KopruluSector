-- The glue menus of the emulator's menus run through kp_ui (plan phase 9E, tools/menus_check.py): the main menu opens
-- at the tick the original's did; kp_ui's glue loop picks the next screens (a ready room for the mission an episode
-- starts) while its timers run at the original's passes and the input at the original's ticks; then the timer
-- records, the movies, the flic buttons' and portraits' frames, the menus' CRT draws and the captures (the screen's
-- dialog with its popups composited) are compared with the original's.
-- Usage (repository root): luajit tools/lua/menus_replay.lua MENUS.txt   (written by tools/menus_check.py)
package.path = "./?.lua;" .. package.path
bit = bit or require("bit")

local native = require("game.native")
local oracle = require("tools.lua.oracle")
local trace = require("tools.lua.ui_trace")
local kpf = require("game.data.kpf")

local MISSION, EXIT = 5, 2
local ROOM_RACES = { [7] = 1, [8] = 0, [9] = 2 } -- glue state -> race (terran, zerg, protoss)
local read = trace.read

local h, records = trace.parse(read(arg[1]), { start = true })
local core = assert(native.find("kp_core"), "menus_replay: no kp_core module (tools/native/build.sh)")
local ui = core.ui.new(read("generated/data/ui.kpd"), read("generated/data/fonts.kpd"))
ui:strings(read("generated/data/strings.kpd"))
ui:portraits(read("generated/data/portraits.kpd"))
ui:sounds(read("generated/data/sounds.kpd"))
ui:voices(false)
ui:trace(true)
ui:campaign(kpf.parse(read("generated/data/campaign.kpd"), "campaign.kpd"):section("TABL"))
ui:profiles({})

local want, clocks = trace.want(records)
local have = trace.streams()
local take = trace.taker(ui, have)
trace.clocks(ui, clocks)
for _, r in ipairs(records) do
	if r.word == "rand" then ui:glue_crt(tonumber(r.rest)) break end
end
-- the font keys' bursts of CRT draws end with srand(time(NULL)): the clock is the next draw's state
do
	local seeds, run = {}, 0
	for i, r in ipairs(records) do
		if r.word == "rand" then
			if run == -1 then
				seeds[#seeds] = tonumber(r.rest)
				run = 0
			end
			run = run + 1
		elseif run >= 100 then
			seeds[#seeds + 1] = 0
			run = -1
		elseif run > 0 then
			run = 0
		end
	end
	ui:time_seeds(seeds)
end

-- the profile's progress StartCampaignEpisode read in a run (menus.py's hook into its copy of the profile): kp_ui's
-- profile has it while the key that starts the episode is handled, until the episode's mission starts (the original's
-- profile file keeps its own)
local progress_at = {}
do
	local last_ev
	for i, r in ipairs(records) do
		if r.word == "ev" then last_ev = i end
		if r.word == "progress" and last_ev then
			local field, value = r.rest:match("^(%S+) (%S+)$")
			progress_at[last_ev] = { field = tonumber(field) + 1, value = tonumber(value) }
		end
	end
end
local saved_progress

local function set_progress(p)
	local profiles = ui:profiles()
	for _, q in ipairs(profiles) do
		if q.name == "Koprulu" then
			saved_progress = saved_progress or { field = p.field, value = q.progress[p.field] }
			q.progress[p.field] = p.value
		end
	end
	ui:profiles(profiles)
end

local missions = {}
for line in read("generated/missions/index.txt"):gmatch("[^\n]+") do missions[#missions + 1] = line:match("^(%S+)") end

-- the screen on show: the bottom open dialog, the glue screen the popups are composited into
local function screen_dialog()
	local open = ui:open()
	return open[#open]
end

local caps, next_cap, pixel_differences = trace.captures(records), 1, 0
local function check_captures(before)
	while caps[next_cap] and caps[next_cap].tick < before do
		local cap = caps[next_cap]
		next_cap = next_cap + 1
		local d = screen_dialog()
		if d then
			ui:invalidate_all()
			ui:present()
			local mine, w, hh = ui:bitmap(d)
			if w == 640 and hh == 480 then
				local function pixel(x, y) return mine:byte(y * w + x + 1) end
				if trace.compare_pixels(cap, pixel, 0, 0, w - 1, hh - 1, os.getenv("ROOM_DUMP")) > 0 then
					pixel_differences = pixel_differences + 1
				end
			end
		end
	end
end

local states, room, expansion, at = {}, nil, 0, 0
local function follow()
	-- the glue loop: an ended screen's next state, a ready room's mission
	local p = ui:menus_poll()
	if p then
		states[#states + 1] = p.state
		expansion = p.expansion
		if p.action == EXIT then return end
		if p.action == MISSION and ROOM_RACES[p.state] then
			if saved_progress then
				set_progress(saved_progress)
				saved_progress = nil
			end
			local path = "generated/missions/" .. missions[p.mission] .. ".kpm"
			local game = oracle.start_mission(read, path, 0, p.expansion, {})
			-- the establishing text's first page is shown when the credits screen has loaded: its record's tick
			for j = at + 1, #records do
				local w = records[j].word
				if w == "page" then ui:now(tonumber(records[j].rest:match("^(%S+)"))) end
				if w == "page" or w == "pass" or w == "ev" then break end
			end
			ui:ready_room(game, ROOM_RACES[p.state])
			room = { state = p.state, game = game }
		else
			ui:menus_enter(p.state)
		end
		take()
	end
	if room then
		ui:ready_continue()
		local st = ui:room_state()
		if st.open then room.opened = true
		elseif room.opened then -- the room ended: Cancel goes back to the campaign menu
			room = nil
			ui:menus_enter(expansion == 0 and 6 or 22)
			take()
		end
	end
end

ui:now(h.start)
ui:menus_enter(0)
take()
local page_tick
for i, r in ipairs(records) do
	at = i
	local tick = (r.word == "pass" or r.word == "turn") and tonumber(r.rest) or r.word == "page" and tonumber(r.rest:match("^(%S+)"))
		or (r.word == "ev" or r.word == "credits") and tonumber(r.rest:match("^(%S+)"))
	if tick then check_captures(tick) end
	if r.word == "pass" then
		if os.getenv("DUMP_AT") and tonumber(r.rest) >= tonumber(os.getenv("DUMP_AT")) and not dumped then
			dumped = true
			for _, n in ipairs(ui:timer_order()) do io.stderr:write(("%s#%s/%d@%d "):format(n.owner, tostring(n.record), n.id, n.due)) end
			io.stderr:write("\n")
		end
		ui:tick(tonumber(r.rest))
		take()
	elseif r.word == "page" then
		page_tick = tonumber(r.rest:match("^(%S+)"))
	elseif r.word == "ev" then
		local t, kind, a, b = r.rest:match("^(%S+) (%S+) (%S+) (%S+)$")
		ui:now(page_tick or tonumber(t))
		page_tick = nil
		if progress_at[i] then set_progress(progress_at[i]) end
		ui:input(tonumber(kind), tonumber(a), tonumber(b))
		take()
	elseif r.word == "turn" then
		ui:now(tonumber(r.rest))
		ui:ready_turn()
		take()
	end
	follow()
end
check_captures(math.huge)

local differences = trace.compare(want, have) + pixel_differences
print(("menus_replay: %d timer records, %d videos, %d captures, states %s, %d differences"):format(#want.timers,
	#want.videos, #caps, table.concat(states, " "), differences))
os.exit(differences == 0 and 0 or 1)
