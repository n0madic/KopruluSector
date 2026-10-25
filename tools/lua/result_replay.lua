-- A result dialog of the emulator's chains through kp_ui (plan phase 9E, tools/room_check.py --kind result): the
-- victory or defeat dialog opens over the game's mission at the tick the original's set its first timer; kp_ui's
-- timers run at the original's passes and the input events at the original's ticks; then the dialog's timer records
-- (the other dialogs' are not compared), the exit code its answer left and its pixels in the modeless captures (the
-- dialog painted and blended over the capture's game view) are compared with the original's.
-- Usage (repository root): luajit tools/lua/result_replay.lua RESULT.txt   (written by tools/room_check.py)
package.path = "./?.lua;" .. package.path
bit = bit or require("bit")

local native = require("game.native")
local oracle = require("tools.lua.oracle")
local trace = require("tools.lua.ui_trace")
local colours = require("presentation.ui.colours")
local terrain = require("game.data.terrain")
local mission_data = require("game.data.mission")

local RACE_THEMES = { [0] = "dlgs\\zerg.grp", [1] = "dlgs\\terran.grp", [2] = "dlgs\\protoss.grp" }
local read = trace.read

local h, records = trace.parse(read(arg[1]), { expansion = true, mission = true, start = true, result = true,
	exit = true })
local core = assert(native.find("kp_core"), "result_replay: no kp_core module (tools/native/build.sh)")
local game = oracle.start_mission(read, h.kpm, 0, h.expansion, {})
local ui = core.ui.new(read("generated/data/ui.kpd"), read("generated/data/fonts.kpd"))
ui:strings(read("generated/data/strings.kpd"))
ui:sounds(read("generated/data/sounds.kpd"))
ui:voices(false)
ui:trace(true)
do
	local mission = mission_data.parse(read(h.kpm), h.kpm)
	local name = terrain.TILESETS[mission.tileset]
	colours.apply(ui, terrain.parse_tileset(read("generated/data/tilesets/" .. name .. ".kpd"), name))
end
ui:font_set(true)
ui:theme(RACE_THEMES[game.players[game.local_player].race] or RACE_THEMES[1])

local function result_owner(line) return line:find("mission%.bin#") ~= nil end
local want, clocks = trace.want(records, result_owner)
want.videos, want.brief, want.flc, want.bpc, want.pages = {}, {}, {}, {}, {} -- the game's and the next screen's
local have = trace.streams()
local take = trace.taker(ui, have, result_owner)
trace.clocks(ui, clocks)

-- The modeless captures: the dialog's opaque pixels must equal the capture's; a translucent one (the border colour)
-- must be a value of the blend row (the original blends it into the screen bitmap at every repaint, and the surface
-- keeps the blend over the game view's last redraw there, which no capture records), and over the view the dialog
-- first blended over ("under") it is counted when it differs.
local caps, next_cap, pixel_differences, moved = trace.captures(records), 1, 0, 0
local function check_captures(before)
	while caps[next_cap] and caps[next_cap].tick < before do
		local cap = caps[next_cap]
		next_cap = next_cap + 1
		local d = ui:modeless()
		if d >= 0 and cap.rect and cap.under then
			ui:paint(d)
			local mine, w = ui:bitmap(d)
			local row, colour = ui:blend_row()
			local under, theirs = read(cap.under), read(cap.path)
			local blends = {}
			for i = 1, 256 do blends[row:byte(i)] = true end
			local l, t = cap.rect[1], cap.rect[2]
			local function pixel(x, y)
				local v, u = mine:byte((y - t) * w + (x - l) + 1), under:byte(y * 640 + x + 1)
				if v == 0 then return nil end -- the view: not recorded
				if v == colour then
					local c = theirs:byte(y * 640 + x + 1)
					if c ~= row:byte(u + 1) then moved = moved + 1 end
					return blends[c] and c or -1
				end
				return v
			end
			if trace.compare_pixels(cap, pixel, l, t, cap.rect[3], cap.rect[4], os.getenv("ROOM_DUMP")) > 0 then
				pixel_differences = pixel_differences + 1
			end
		end
	end
end

local ended = false
ui:now(h.start)
ui:show_result(h.result)
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
		if ui:game_exit() ~= 0 and not ended then -- the game loop ends after this message: EndGame
			ended = true
			ui:end_game()
		end
		take()
	end
end
check_captures(math.huge)

local differences = trace.compare(want, have) + pixel_differences
if h.exit >= 0 and ui:game_exit() ~= h.exit then
	differences = differences + 1
	print(("exit: Koprulu %d, the original %d"):format(ui:game_exit(), h.exit))
end
print(("result_replay: mission %d result %d: %d timer records, %d captures, exit %d, %d translucent pixels over a "
	.. "redrawn view, %d differences"):format(h.mission, h.result, #want.timers, #caps, ui:game_exit(), moved, differences))
os.exit(differences == 0 and 0 or 1)
