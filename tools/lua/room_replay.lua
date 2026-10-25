-- A ready room of the emulator's chains through kp_ui (plan phase 9E, tools/room_check.py): the room opens over the
-- game's mission at the tick the original's dialog set its first timer; kp_ui's timers run at the original's passes
-- and the input events at the original's ticks; then the timer records (with their owners as the dialogs' BIN and
-- record index), the briefing VM's actions and the movies begun are compared with the original's in order.
-- Usage (repository root): luajit tools/lua/room_replay.lua ROOM.lua   (written by tools/room_check.py)
package.path = "./?.lua;" .. package.path
bit = bit or require("bit")

local native = require("game.native")
local oracle = require("tools.lua.oracle")
local trace = require("tools.lua.ui_trace")

local IDLE = 13
local read = trace.read

-- the room's lines (tools/room_check.py): the header, then the original's records in order
local room, records = trace.parse(read(arg[1]), { expansion = true, mission = true, race = true, start = true, room = true })
local core = assert(native.find("kp_core"), "room_replay: no kp_core module (tools/native/build.sh)")
local game = oracle.start_mission(read, room.kpm, 0, room.expansion, {})
local ui = core.ui.new(read("generated/data/ui.kpd"), read("generated/data/fonts.kpd"))
ui:strings(read("generated/data/strings.kpd"))
ui:portraits(read("generated/data/portraits.kpd"))
ui:sounds(read("generated/data/sounds.kpd"))
ui:voices(false)
ui:trace(true)

local want, clocks = trace.want(records)
local have = trace.streams()
local take = trace.taker(ui, have)

-- the run: the room opens at its first record's tick, the menus' CRT state from the first draw's
ui:now(room.start)
for _, r in ipairs(records) do
	if r.word == "rand" then ui:glue_crt(tonumber(r.rest)) break end
end
-- the glue captures taken while the room ran: the room's dialog at their tick against their indices
local caps, next_cap, pixel_differences = trace.captures(records), 1, 0
local function check_captures(before)
	while caps[next_cap] and caps[next_cap].tick < before do
		local cap = caps[next_cap]
		next_cap = next_cap + 1
		local state = ui:room_state()
		if state.dialog >= 0 and state.open then
			local mine, w, h = ui:bitmap(state.dialog) -- as the last repaint of the invalid region left it
			local function pixel(x, y) return mine:byte(y * w + x + 1) end
			if trace.compare_pixels(cap, pixel, 0, 0, w - 1, h - 1, os.getenv("ROOM_DUMP")) > 0 then
				pixel_differences = pixel_differences + 1
			end
			-- the screen's palette while the room's movies play (SVid writes their indices, the room's palette stays)
			local theirs, rgb = trace.read((cap.path:gsub("%.raw$", ".pal"))), ui:glue_palette()
			local differ = 0
			for k = 0, 255 do
				for c = 1, 3 do
					if theirs:byte(4 * k + c) ~= rgb:byte(3 * k + c) then differ = differ + 1 end
				end
			end
			if differ > 0 then
				print(("capture %s at %d: %d palette bytes differ"):format(cap.name, cap.tick, differ))
				pixel_differences = pixel_differences + 1
			end
		end
	end
end

local has_turns = false
for _, r in ipairs(records) do has_turns = has_turns or r.word == "turn" end
trace.clocks(ui, clocks)
-- the establishing text's first page is shown when the credits screen has loaded: its record's tick is the clock's
for _, r in ipairs(records) do
	if r.word == "page" then ui:now(tonumber(r.rest:match("^(%S+)"))) end
	if r.word == "page" or r.word == "pass" or r.word == "ev" then break end
end
ui:ready_room(game, room.race)
take()
local room_open, page_tick = false, nil
local function open_room(tick)
	if room_open or tick < room.room then return end
	room_open = true
	ui:now(room.room)
	ui:ready_continue()
	take()
end
for _, r in ipairs(records) do
	local tick = (r.word == "pass" or r.word == "turn") and tonumber(r.rest) or r.word == "page" and tonumber(r.rest:match("^(%S+)"))
		or (r.word == "ev" or r.word == "credits") and tonumber(r.rest:match("^(%S+)"))
	if tick then
		check_captures(tick)
		open_room(tick)
	end
	if r.word == "pass" then
		ui:tick(tonumber(r.rest))
		take()
	elseif r.word == "page" then -- the credits' handler runs inside the event's dispatch, before the event's tick
		page_tick = tonumber(r.rest:match("^(%S+)"))
	elseif r.word == "ev" then
		local t, kind, a, b = r.rest:match("^(%S+) (%S+) (%S+) (%S+)$")
		t, kind, a, b = tonumber(t), tonumber(kind), tonumber(a), tonumber(b)
		ui:now(page_tick or t)
		page_tick = nil
		ui:input(kind, a, b)
		if kind == IDLE and not has_turns then ui:ready_idle() end
		take()
	elseif r.word == "turn" then -- the original's turn steps, where the log has them
		ui:now(tonumber(r.rest))
		ui:ready_turn()
		take()
	end
	-- the glue loop repaints the invalid region after its timers and its messages (PaintDialogLayer 0x4c4d90): a
	-- control nothing invalidated keeps what the screen had
	if r.word == "pass" or r.word == "ev" or r.word == "turn" then ui:present() end
end

local differences = trace.compare(want, have)
differences = differences + pixel_differences
print(("room_replay: mission %d race %d: %d timer records, %d actions, %d videos, %d captures, %d differences"):format(
	room.mission, room.race, #want.timers, #want.brief, #want.videos, #caps, differences))
os.exit(differences == 0 and 0 or 1)
