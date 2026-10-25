-- The console's panels of a replayed game for tools/ui_check.py (plan phase 9B, tools/lua/hud_replay.lua): at each
-- requested frame the open dialogs and the text lines' layer are written out.
--
-- Usage (repository root): luajit tools/lua/hud_check.lua GOLDEN.lua HUD.txt OUT FRAME [FRAME ...] [--run DIR]
--        [--modeless TICK:FRAME,...]
-- HUD.txt: tools/hud_log.py's entries (to_text). With --run, the game's commands and interface events come from
-- DIR's commands.jsonl and ui.jsonl (a newer run logs the screen's draws, "onscreen", which older goldens lack),
-- else from the golden.
-- OUT: per frame "frame F\n", then per open dialog "panel BIN LEFT TOP W H FLAGS\n" and its W * H indices, the
-- cursor's frame as the panel "cursor" (LEFT, TOP: its place relative to the mouse), the tooltip box shown as the
-- panel "tooltip" and the text lines' layer as the panel "lines".
package.path = "./?.lua;" .. package.path
bit = bit or require("bit")

local native = require("game.native")
local hud_replay = require("tools.lua.hud_replay")

local LINES_FLAGS = 0x2000 -- the text lines' layer: index 0 shows the game view (ui_check.py TRANSPARENT)

local function read(path)
	local f = assert(io.open(path, "rb"))
	local s = f:read("*a")
	f:close()
	return s
end

local function load_module(path) return assert((loadstring or load)(read(path)))() end

local frames, run_dir, modeless = {}, nil, {}
do
	local k = 4
	while arg[k] do
		if arg[k] == "--run" then
			run_dir = arg[k + 1]
			k = k + 2
		elseif arg[k] == "--modeless" then -- the modeless captures' ticks and frames: TICK:FRAME,...
			for tick, frame in arg[k + 1]:gmatch("(%d+):(%d+)") do
				modeless[#modeless + 1] = tonumber(tick)
				frames[#frames + 1] = tonumber(frame) + 1 -- the replay runs past the capture's (paused) frame
			end
			k = k + 2
		else
			frames[#frames + 1] = tonumber(arg[k]:match("^(%d+)"))
			k = k + 1
		end
	end
	table.sort(frames)
	for k = #frames, 2, -1 do
		if frames[k] == frames[k - 1] then table.remove(frames, k) end
	end
end

local next_modeless = 1
local hud = hud_replay.parse_text(read(arg[2]))
local cursor_logged = false -- an older run's log has no cursor records: its cursor is not compared
for _, e in ipairs(hud.entries) do cursor_logged = cursor_logged or e[2] == 7 end
local out = assert(io.open(arg[3], "wb"))
hud_replay.run({
	core = assert(native.find("kp_core"), "hud_check: no kp_core module (tools/native/build.sh)"),
	read = read,
	golden = load_module(arg[1]),
	hud = hud,
	frames = frames,
	commands = run_dir and read(run_dir .. "/commands.jsonl"),
	ui = run_dir and read(run_dir .. "/ui.jsonl"),
	input = true, -- the interface takes the run's input itself (its modes and presses show on the panels)
	on_command = function() end,
	-- each modeless capture: at the first repaint at or after its tick, the modeless dialog over the game ("modeless
	-- K", then its panel and the blend row of its translucent colour: "blend COLOUR" and 256 indices)
	on_present = function(tick, ui)
		while modeless[next_modeless] and tick >= modeless[next_modeless] do
			out:write(("modeless %d\n"):format(next_modeless - 1))
			local d = ui:modeless()
			if d >= 0 then
				local k = ui:control(d)
				local bytes, w, h = ui:bitmap(d)
				out:write(("panel %s %d %d %d %d %d\n"):format(k.bin_name, k.left, k.top, w, h, k.flags))
				out:write(bytes)
				local row, colour = ui:blend_row()
				out:write(("blend %d\n"):format(colour))
				out:write(row)
				local art, aw, ah = ui:console_art() -- drawn over a dialog composited onto the backdrop
				if art then
					out:write(("console %d %d\n"):format(aw, ah))
					out:write(art)
				end
			end
			next_modeless = next_modeless + 1
		end
	end,
	on_frame = function(frame, ui)
		out:write(("frame %d\n"):format(frame))
		for _, d in ipairs(ui:open()) do
			local k = ui:control(d)
			local bytes, w, h = ui:bitmap(d)
			if bytes then
				out:write(("panel %s %d %d %d %d %d\n"):format(k.bin_name, k.left, k.top, w, h, k.flags))
				out:write(bytes)
			end
		end
		local cursor, cw, ch, dx, dy = ui:cursor_bitmap()
		if cursor and cursor_logged then
			out:write(("panel cursor %d %d %d %d %d\n"):format(dx, dy, cw, ch, LINES_FLAGS))
			out:write(cursor)
		end
		local tip, tx, ty, tw, th = ui:tooltip()
		if tip then
			out:write(("panel tooltip %d %d %d %d 0\n"):format(tx, ty, tw, th))
			out:write(tip)
		end
		local lines, w, h = ui:lines()
		if lines then
			out:write(("panel lines 0 0 %d %d %d\n"):format(w, h, LINES_FLAGS))
			out:write(lines)
		end
	end,
})
out:close()
