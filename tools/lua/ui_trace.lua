-- The interface's records against the emulator's (plan phase 9E, tools/room_check.py): the lines room_check.py writes
-- from an input log (the timer records with their owners as the dialogs' BIN and record index, the briefing VM's
-- actions, the movies begun, the flic and briefing portrait continuations, the credits' pages), the same records
-- taken from kp_ui's trace, their comparison and the captures' pixels.
local M = {}

local SHOWN = 8
local KINDS = { "set", "del", "delall", "fire", "pass" }

function M.read(path)
	local f = assert(io.open(path, "rb"))
	local s = f:read("*a")
	f:close()
	return s
end

-- The file's header (words with a number, kpm with its path) and its records in order.
function M.parse(text, header_words)
	local header, records = {}, {}
	for line in text:gmatch("[^\n]+") do
		local word, rest = line:match("^(%S+)%s*(.*)$")
		if word == "kpm" then header.kpm = rest
		elseif header_words[word] then header[word] = tonumber(rest)
		else records[#records + 1] = { word = word, line = line, rest = rest } end
	end
	return header, records
end

-- Empty streams.
function M.streams() return { timers = {}, brief = {}, videos = {}, flc = {}, pages = {}, bpc = {}, rands = {} } end

-- The original's streams and the clocks kp_ui replays them at: timers (sets with their ticks), brief, videos, flc,
-- pages, bpc. keep(line) (optional) chooses the timer records compared.
function M.want(records, keep)
	local want = M.streams()
	local clocks = { set_ticks = {}, video_ticks = {}, page_deadlines = {} }
	local screen_pages = 0 -- the pages of the credits screen on show
	for _, r in ipairs(records) do
		if r.word == "set" then -- the set's tick is the clock's, not compared
			local line, tick = r.line:match("^(.-) @(%d+)$")
			line = line:gsub("%?%d+", "?")
			if not keep or keep(line) then
				want.timers[#want.timers + 1] = line
				clocks.set_ticks[#clocks.set_ticks + 1] = tonumber(tick)
			end
		elseif r.word == "del" or r.word == "delall" or r.word == "pass" or r.word == "fire" then
			local line = r.line:gsub("%?%d+", "?") -- an owner of no BIN, numbered by the log
			if not keep or keep(line) then want.timers[#want.timers + 1] = line end
		elseif r.word == "brief" then
			want.brief[#want.brief + 1] = r.rest
		elseif r.word == "rand" then
			want.rands[#want.rands + 1] = r.rest
		elseif r.word == "svid" then
			want.videos[#want.videos + 1] = r.rest
		elseif r.word == "flc" or r.word == "bpc" then -- the continuation's tick is the clock's, not compared
			local line, tick = r.rest:match("^(.-) @(%d+)$")
			want[r.word][#want[r.word] + 1] = line or r.rest
			if tick then clocks.video_ticks[#clocks.video_ticks + 1] = tonumber(tick) end
		elseif r.word == "credits" then
			screen_pages = 0
		elseif r.word == "page" then -- the page before's deadline is the clock's (none before a screen's first)
			local tick, deadline = r.rest:match("^(%S+) (%S+)$")
			want.pages[#want.pages + 1] = tick
			screen_pages = screen_pages + 1
			if screen_pages > 1 then clocks.page_deadlines[#clocks.page_deadlines + 1] = tonumber(deadline) end
		end
	end
	return want, clocks
end

-- Sets kp_ui's oracle clocks.
function M.clocks(ui, clocks)
	ui:set_ticks(clocks.set_ticks)
	if #clocks.video_ticks > 0 then ui:video_ticks(clocks.video_ticks) end
	if #clocks.page_deadlines > 0 then ui:page_deadlines(clocks.page_deadlines) end
end

-- A function that moves kp_ui's trace into have (the streams of M.want), keep as there.
function M.taker(ui, have, keep)
	return function()
		local timers, videos, flcs, pages, bpcs, rands = ui:take_trace()
		for _, v in ipairs(rands) do have.rands[#have.rands + 1] = ("%d"):format(v) end
		for _, b in ipairs(bpcs) do have.bpc[#have.bpc + 1] = table.concat(b, " ") end
		for _, p in ipairs(pages) do have.pages[#have.pages + 1] = tostring(p) end
		for _, f in ipairs(flcs) do have.flc[#have.flc + 1] = table.concat(f, " ") end
		for _, t in ipairs(timers) do
			local kind, line = KINDS[t[1]]
			local function koprulu_ref()
				local name = t[7] >= 0 and ui:bin_name(t[7])
				return name and (name:lower() .. "#" .. t[8]) or "?" -- a control of no BIN (a list's scroll bar)
			end
			if kind == "set" then line = ("set %s %d %d %d"):format(koprulu_ref(), t[3], t[4], t[5])
				if os.getenv("TRACE_SET_TICKS") then io.stderr:write(line, " @", t[6], "\n") end
			elseif kind == "del" then line = ("del %s %d"):format(koprulu_ref(), t[3])
			elseif kind == "delall" then line = ("delall %s"):format(koprulu_ref())
			elseif kind == "fire" then line = ("fire %s %d"):format(koprulu_ref(), t[3])
			else line = "pass " .. t[6] end
			if not keep or keep(line) then have.timers[#have.timers + 1] = line end
		end
		for _, v in ipairs(videos) do have.videos[#have.videos + 1] = v:lower() end
		for _, b in ipairs(ui:take_brief()) do
			have.brief[#have.brief + 1] = ("%d %d %d %d"):format(b[1], b[2], b[3], b[4])
		end
	end
end

-- The streams compared in order: the first difference of each is printed (with ROOM_CONTEXT lines around it).
-- Returns the number of streams that differ.
function M.compare(want, have)
	local differences, printed = 0, 0
	local context = tonumber(os.getenv("ROOM_CONTEXT") or "0")
	local function stream(name, w, h)
		local n = math.max(#w, #h)
		for k = 1, n do
			if w[k] ~= h[k] then
				differences = differences + 1
				printed = printed + 1
				if printed <= SHOWN then
					print(("%s %d: Koprulu %s, the original %s"):format(name, k, h[k] or "nothing", w[k] or "nothing"))
					for j = math.max(1, k - context), math.min(n, k + context) do
						print(("  %d  %-40s %s"):format(j, w[j] or "-", h[j] or "-"))
					end
				end
				return
			end
		end
	end
	stream("timer", want.timers, have.timers)
	stream("brief", want.brief, have.brief)
	stream("video", want.videos, have.videos)
	stream("flc", want.flc, have.flc)
	stream("page", want.pages, have.pages)
	stream("bpc", want.bpc, have.bpc)
	stream("rand", want.rands, have.rands)
	return differences
end

-- The capture lines ("cap TICK PATH NAME CX CY CW CH [DL DT DR DB]"), sorted by tick.
function M.captures(records)
	local caps = {}
	for _, r in ipairs(records) do
		if r.word == "cap" then
			local f = {}
			for w in r.rest:gmatch("%S+") do f[#f + 1] = w end
			local cx, cy, cw, ch = tonumber(f[4]), tonumber(f[5]), tonumber(f[6]), tonumber(f[7])
			caps[#caps + 1] = { tick = tonumber(f[1]), path = f[2], name = f[3], cursor = { cx, cy, cx + cw - 1, cy + ch - 1 },
				rect = f[8] and { tonumber(f[8]), tonumber(f[9]), tonumber(f[10]), tonumber(f[11]) } or nil,
				under = f[12] }
		end
	end
	table.sort(caps, function(a, b) return a.tick < b.tick end)
	return caps
end

-- Compares an expected picture (pixel(x, y) -> index, or nil to skip) with a capture's indices (640 wide), the cursor
-- masked; prints and returns the number of differing pixels.
function M.compare_pixels(cap, pixel, x0, y0, x1, y1, dump)
	local theirs, w = M.read(cap.path), 640
	local c, differ, first = cap.cursor, 0, nil
	local mine = {}
	for y = y0, y1 do
		for x = x0, x1 do
			local v = pixel(x, y)
			if dump then mine[#mine + 1] = string.char(v or theirs:byte(y * w + x + 1)) end
			if v and v ~= theirs:byte(y * w + x + 1) and not (x >= c[1] and x <= c[3] and y >= c[2] and y <= c[4]) then
				differ = differ + 1
				first = first or { x, y, v, theirs:byte(y * w + x + 1) }
			end
		end
	end
	if differ > 0 then
		print(("capture %s at %d: %d pixels differ, the first at (%d, %d): Koprulu %d, the original %d"):format(
			cap.name, cap.tick, differ, first[1], first[2], first[3], first[4]))
		if dump then
			local f = assert(io.open(dump .. "/" .. cap.name .. ".koprulu.raw", "wb"))
			f:write(table.concat(mine))
			f:close()
		end
	end
	return differ
end

return M
