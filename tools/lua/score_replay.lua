-- A score screen of the emulator's chains through kp_ui (plan phase 9E, tools/room_check.py --kind score): the screen
-- opens over the data the original's read (uilog.py's "score" record: the roster, the score arrays, the colour
-- tables, and its elapsed seconds) at the tick it did; kp_ui's timers run at the original's passes and the input
-- events at the original's ticks; then the screen's timer records (gluScore.bin's) and its pixels in the glue
-- captures are compared with the original's.
-- Usage (repository root): luajit tools/lua/score_replay.lua SCORE.txt   (written by tools/room_check.py)
package.path = "./?.lua;" .. package.path
bit = bit or require("bit")

local native = require("game.native")
local oracle = require("tools.lua.oracle")
local trace = require("tools.lua.ui_trace")

local read = trace.read
local h, records = trace.parse(read(arg[1]), { expansion = true, mission = true, start = true })
local data
for _, r in ipairs(records) do
	if r.word == "data" then data = assert((loadstring or load)(r.rest))() end
end
local core = assert(native.find("kp_core"), "score_replay: no kp_core module (tools/native/build.sh)")
local game = oracle.start_mission(read, h.kpm, 0, h.expansion, {})
local ui = core.ui.new(read("generated/data/ui.kpd"), read("generated/data/fonts.kpd"))
ui:strings(read("generated/data/strings.kpd"))
ui:sounds(read("generated/data/sounds.kpd"))
ui:voices(false)
ui:trace(true)

local function score_owner(line) return line:find("gluscore%.bin#") ~= nil end
local want, clocks = trace.want(records, score_owner)
want.videos, want.brief, want.flc, want.bpc, want.pages = {}, {}, {}, {}, {} -- the game's and the next screen's
local have = trace.streams()
local take = trace.taker(ui, have, score_owner)
trace.clocks(ui, clocks)

local caps, next_cap, pixel_differences = trace.captures(records), 1, 0
local function check_captures(before)
	while caps[next_cap] and caps[next_cap].tick < before do
		local cap = caps[next_cap]
		next_cap = next_cap + 1
		local d = ui:score_state().dialog
		if d >= 0 then
			ui:paint(d)
			local mine, w, hh = ui:bitmap(d)
			local function pixel(x, y) return mine:byte(y * w + x + 1) end
			if trace.compare_pixels(cap, pixel, 0, 0, w - 1, hh - 1, os.getenv("ROOM_DUMP")) > 0 then
				pixel_differences = pixel_differences + 1
			end
		end
	end
end

ui:now(h.start)
ui:score(data)
take()
for _, r in ipairs(records) do
	local tick = r.word == "pass" and tonumber(r.rest) or r.word == "ev" and tonumber(r.rest:match("^(%S+)"))
	if tick then check_captures(tick) end
	if r.word == "pass" then
		ui:tick(tonumber(r.rest))
		take()
	elseif r.word == "ev" then
		local t, kind, a, b = r.rest:match("^(%S+) (%S+) (%S+) (%S+)$")
		ui:now(tonumber(t))
		ui:input(tonumber(kind), tonumber(a), tonumber(b))
		take()
	end
end
check_captures(math.huge)

local differences = trace.compare(want, have) + pixel_differences
print(("score_replay: mission %d state %d: %d timer records, %d captures, %d differences"):format(
	h.mission, data.glue_state, #want.timers, #caps, differences))
os.exit(differences == 0 and 0 or 1)
