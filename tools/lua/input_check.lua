-- The interface's behaviour against the original's (plan phase 9C, level 1 of docs/phase9.md): a run's input events
-- go through kp_ui over the replayed game (tools/lua/hud_replay.lua with input = true); the local selections it makes
-- must equal the ones the original's interface made, and the commands it queues the local player's commands of the
-- run (commands.jsonl; the game's own sync command 0x37 left out), in order and in the same frame (commands.jsonl
-- stamps a command with the frame whose turn sends it: the frame after the input's).
--
-- Usage (repository root): luajit tools/lua/input_check.lua GOLDEN.lua HUD.txt RUN_DIR [LAST_FRAME]
-- Prints the first differences and a summary line; exit status 1 when anything differs.
package.path = "./?.lua;" .. package.path
bit = bit or require("bit")

local native = require("game.native")
local oracle = require("tools.lua.oracle")
local hud_replay = require("tools.lua.hud_replay")

local SYNC = 0x37
local SHOWN = 8 -- the differences printed

local function read(path)
	local f = assert(io.open(path, "rb"))
	local s = f:read("*a")
	f:close()
	return s
end

local function hex(bytes) return (bytes:gsub(".", function(c) return ("%02x"):format(c:byte()) end)) end
-- a command as compared: the save command's time stamp (SendSaveGameCommand 0x4740e0: time(NULL), the host's clock)
-- left out
local SAVE = 0x06
local function masked(bytes) return bytes:byte(1) == SAVE and bytes:sub(1, 1) .. bytes:sub(6) or bytes end

local golden = assert((loadstring or load)(read(arg[1])))()
local hud = hud_replay.parse_text(read(arg[2]))
local dir = arg[3]
local last = tonumber(arg[4])

local want, local_player = {}, nil
for _, c in ipairs(oracle.parse_commands(read(dir .. "/commands.jsonl"))) do
	if c.bytes:byte(1) ~= SYNC and (not last or c.frame <= last) then want[#want + 1] = c end
end

local got, differences, selections, cursors, scrolls, releases, tips, sounds, talks = {}, 0, 0, 0, 0, 0, 0, 0, 0
local SCROLL_FIELDS = { "x", "y", "step", "dx", "dy", "suppressed", "cursor_x", "cursor_y" } -- entry fields 3..10
local function differ(msg)
	differences = differences + 1
	if differences <= SHOWN then print(msg) end
end

local frame_end = last or hud.entries[#hud.entries][1]
local hud_ui -- the interface (on_entry's)
hud_replay.run({
	core = assert(native.find("kp_core"), "input_check: no kp_core module (tools/native/build.sh)"),
	read = read, golden = golden, hud = hud, frames = { frame_end },
	commands = read(dir .. "/commands.jsonl"), ui = read(dir .. "/ui.jsonl"),
	input = true,
	on_select = function(frame, units, game)
		selections = selections + 1
		local have = {}
		for k = 1, 12 do have[k] = game.local_selection[k - 1] end -- a 0-based array
		for k = 1, 12 do
			if (units[k] or 0) ~= (have[k] or 0) then
				differ(("frame %d: the local selection %s, the original's %s"):format(frame, table.concat(have, ","),
					table.concat(units, ",")))
				break
			end
		end
	end,
	-- a command goes out with the next frame's turn, or with the same frame's while the game is paused (a paused
	-- game's turns do not advance the frame counter: RunPendingFrames 0x444e30)
	on_command = function(frame, bytes, game)
		got[#got + 1] = { frame = frame + ((game and game.paused == 1) and 0 or 1), bytes = bytes }
	end,
	on_cursor = function(frame, have, id, game)
		-- TODO: the result dialog (its own cursor) that the game's end opens
		if game.results[game.local_player] ~= 0 then return end
		cursors = cursors + 1
		if have ~= id then differ(("frame %d: the cursor %d, the original's %d"):format(frame, have, id)) end
	end,
	on_scroll = function(frame, state, e)
		scrolls = scrolls + 1
		for k, name in ipairs(SCROLL_FIELDS) do
			if state[name] ~= e[k + 2] then
				local have = {}
				for j, n in ipairs(SCROLL_FIELDS) do have[j] = state[n] end
				differ(("frame %d: the scroll %s, the original's %s"):format(frame, table.concat(have, ","),
					table.concat(e, ",", 3, 10)))
				break
			end
		end
	end,
	on_screen = function(frame, x, y)
		local s = hud_ui and hud_ui:scroll_state()
		if s and (s.x ~= x or s.y ~= y) then
			differ(("frame %d: an input event in the view %d,%d, the original's %d,%d"):format(frame, s.x, s.y, x, y))
		end
	end,
	on_release = function(frame, released)
		releases = releases + 1
		if not released then differ(("frame %d: the trigger wait's release, not the interface's"):format(frame)) end
	end,
	on_tip = function(frame, have, want)
		tips = tips + 1
		local h = have and ("%d,%d-%d,%d"):format(have[1], have[2], have[1] + have[3] - 1, have[2] + have[4] - 1) or "none"
		local w = want and ("%d,%d-%d,%d"):format(want[1], want[2], want[3], want[4]) or "none"
		if h ~= w then differ(("frame %d: the tooltip %s, the original's %s"):format(frame, h, w)) end
	end,
	on_sound = function(frame, have, e)
		sounds = sounds + 1
		local want = { e[3], e[4], e[5], e[6], e[7], e[8] } -- id, attenuation, pan, unit type, path, result
		if table.concat(have, ",") ~= table.concat(want, ",") then
			differ(("frame %d: the sound %s, the original's %s"):format(frame, table.concat(have, ","), table.concat(want, ",")))
		end
	end,
	on_talk = function(frame, have, e)
		talks = talks + 1
		local want = { e[3], e[4], e[5], e[6], e[7] } -- unit, duration, sound, ping, colour
		if table.concat(have, ",") ~= table.concat(want, ",") then
			differ(("frame %d: the talking request %s, the original's %s"):format(frame, table.concat(have, ","),
				table.concat(want, ",")))
		end
	end,
	on_sound_end = function(sw, sh, tw, th)
		for _, w in ipairs(sw) do differ(("frame %d: the original's sound %s, not the interface's"):format(w.frame,
			table.concat(w.entry, ",", 3, 8))) end
		for _, h in ipairs(sh) do differ(("the interface's sound %s, not the original's"):format(table.concat(h, ","))) end
		for _, w in ipairs(tw) do differ(("frame %d: the original's talking request %s, not the interface's")
			:format(w.frame, table.concat(w.entry, ",", 3, 7))) end
		for _, h in ipairs(th) do differ(("the interface's talking request %s, not the original's"):format(table.concat(h, ","))) end
	end,
	on_entry = function(_, ui) hud_ui = ui end,
	on_frame = function(_, _, game) local_player = game.local_player end,
})

local mine = {}
for _, c in ipairs(want) do
	if c.player == local_player then mine[#mine + 1] = c end
end
for k = 1, math.max(#mine, #got) do
	local w, g = mine[k], got[k]
	if not w then
		differ(("command %d: Koprulu queued %s at frame %d, the original nothing more"):format(k, hex(g.bytes), g.frame))
	elseif not g then
		differ(("command %d: the original queued %s at frame %d, Koprulu nothing more"):format(k, hex(w.bytes), w.frame))
	elseif masked(g.bytes) ~= masked(w.bytes) or g.frame ~= w.frame then
		differ(("command %d: Koprulu %s at frame %d, the original %s at frame %d"):format(k, hex(g.bytes), g.frame,
			hex(w.bytes), w.frame))
	end
end
print(("input_check: %d selections, %d cursors, %d scrolls, %d releases, %d tooltips, %d sounds, %d talks, %d commands "
	.. "(the original's %d), %d differences"):format(selections, cursors, scrolls, releases, tips, sounds, talks, #got, #mine,
	differences))
os.exit(differences == 0 and 0 or 1)
