-- Palette state of the indexed pipeline (plan §7 phase 2 item 5, plan D10), pure Lua: the
-- tileset palette with the original's colour cycling, brightness (gamma) option, grayscale
-- and fades (docs/research/native-palette-bootstrap.md, native-pixel-effects.md). The
-- render script receives rgb() as its palette constant (presentation/lookup_textures.lua).
--
-- Clock: the original runs the cycle driver 0x4ccd30 from its game loop whenever the tick
-- counter advanced more than 9 ms (0x445020: GetTickCount), not per game frame; with the
-- Windows tick counter's 15.625 ms resolution that is once per counter step. Cycling stops
-- while the game is paused (0x47cb60).
local M = {}

M.ENTRIES = 256
M.RECORDS = 8 -- cycle records at 0x6d2f88
M.CYCLE_PERIOD_MS = 15.625 -- one tick counter step (see above)
M.FIRST_PASS_LIMIT, M.SECOND_PASS_LIMIT = 0x80, 0x100 -- low-entry limits of the two driver passes
M.BRIGHTNESS_MIN, M.BRIGHTNESS_MAX, M.BRIGHTNESS_DEFAULT = 60, 140, 100 -- 0x4cf930
M.GAMMA_BASE, M.GAMMA_SCALE = 200, 100 -- exponent = (GAMMA_BASE - brightness) / GAMMA_SCALE (0x4cf760)
M.GRAY_WEIGHTS = { 77, 151, 28 } -- 0x4ccf40, each channel truncated separately
M.FADE_MAX_STEPS = 10 -- 0x4ccd60 caps its argument
M.FADE_WAIT_MS = 10 -- the fade's one wait, measured from its start
-- Cycle steps one advance may run. The original runs the driver at most once per pass of
-- its game loop, so a stall (a hidden browser tab, a long frame) is not caught up; the cap
-- covers frames somewhat longer than a counter step and drops the rest.
M.MAX_CYCLES_PER_ADVANCE = 2

local floor = math.floor

local Palette = {}
Palette.__index = Palette

-- rgb: the tileset palette, 768 bytes (R, G, B per entry); cycles: the tileset's stock
-- simple cycles { speed =, { low, high }, ... } (game/data/terrain.lua, tileset.kpd CYCL:
-- the definitions at 0x5089b0 + tileset * 4, all enabled by their masks, none with a
-- pattern).
function M.new(rgb, cycles)
	assert(#rgb == 3 * M.ENTRIES, "palette: 768 bytes expected")
	local self = setmetatable({ r = {}, g = {}, b = {}, brightness = M.BRIGHTNESS_DEFAULT, gamma = {},
		clock = 0, version = 1 }, Palette)
	for i = 0, M.ENTRIES - 1 do
		self.r[i], self.g[i], self.b[i] = rgb:byte(i * 3 + 1, i * 3 + 3)
	end
	self:bootstrap(cycles)
	return self
end

-- 0x46a3b0: clears the eight records (0x4ccf00), sets one per definition entry
-- (0x4cd000: speed, counter = speed, low, high, phase 0) and enables them (0x4cd070).
function Palette:bootstrap(cycles)
	assert(#cycles <= M.RECORDS, "palette: more cycles than records")
	local records = {}
	for k = 0, M.RECORDS - 1 do records[k] = { active = false, speed = 0, counter = 0, low = 0, high = 0 } end
	for k, c in ipairs(cycles) do
		local rec = records[k - 1]
		rec.speed, rec.counter, rec.low, rec.high, rec.active = cycles.speed, cycles.speed, c[1], c[2], true
	end
	self.records = records
end

-- 0x4cccf0: entries low..high rotate right by one (high moves to low).
function Palette:rotate(low, high)
	for _, ch in ipairs({ self.r, self.g, self.b }) do
		local last = ch[high]
		for i = high, low + 1, -1 do ch[i] = ch[i - 1] end
		ch[low] = last
	end
end

-- 0x4ccbc0 from record `first`: stops at record 8, a zero speed or a low entry above
-- `limit`; an active record's BYTE counter steps down and at zero reloads its speed and
-- rotates. Returns the record it stopped at and whether a range changed.
function Palette:schedule(first, limit)
	local records, k, changed = self.records, first, false
	while k < M.RECORDS and records[k].speed ~= 0 do
		local rec = records[k]
		if limit < rec.low then break end
		if rec.active then
			rec.counter = (rec.counter - 1) % 256
			if rec.counter == 0 then
				rec.counter = rec.speed
				self:rotate(rec.low, rec.high)
				changed = true
			end
		end
		k = k + 1
	end
	return k, changed
end

-- 0x4ccd30: the low pass (entries up to 0x80), then the rest.
function Palette:cycle()
	local k, changed = self:schedule(0, M.FIRST_PASS_LIMIT)
	if k < M.RECORDS then
		local _, more = self:schedule(k, M.SECOND_PASS_LIMIT)
		changed = changed or more
	end
	if changed then self.version = self.version + 1 end
	return changed
end

-- 0x4cf930: 60..140, anything else 100.
function Palette:set_brightness(v)
	if v < M.BRIGHTNESS_MIN or v > M.BRIGHTNESS_MAX then v = M.BRIGHTNESS_DEFAULT end
	if v ~= self.brightness then
		self.brightness = v
		self.version = self.version + 1
	end
end

-- 0x4cf760 per channel byte: trunc(256 * pow(c / 256, (200 - brightness) / 100)).
function M.gamma_table(brightness)
	local t = {}
	local e = (M.GAMMA_BASE - brightness) / M.GAMMA_SCALE
	for c = 0, 255 do
		t[c] = brightness == M.BRIGHTNESS_DEFAULT and c or floor(256 * (c / 256) ^ e) % 256
	end
	return t
end

-- 0x4ccf40: each component floor(77 R / 256) + floor(151 G / 256) + floor(28 B / 256).
function M.gray(r, g, b)
	local w = M.GRAY_WEIGHTS
	return floor(w[1] * r / 256) + floor(w[2] * g / 256) + floor(w[3] * b / 256)
end

-- Current palette as a 768-byte string (R, G, B per entry).
function Palette:current()
	local out = {}
	for i = 0, M.ENTRIES - 1 do out[i + 1] = string.char(self.r[i], self.g[i], self.b[i]) end
	return table.concat(out)
end

-- The palette the device receives (0x4cf8c0: gamma unless brightness is 100), cached per
-- version.
function Palette:rgb()
	if self.rgb_version == self.version then return self.rgb_cache end
	local gamma = self.gamma[self.brightness]
	if not gamma then
		gamma = M.gamma_table(self.brightness)
		self.gamma[self.brightness] = gamma
	end
	local out = {}
	for i = 0, M.ENTRIES - 1 do
		out[i + 1] = string.char(gamma[self.r[i]], gamma[self.g[i]], gamma[self.b[i]])
	end
	self.rgb_cache, self.rgb_version = table.concat(out), self.version
	return self.rgb_cache
end

-- 0x4ccd60 from the current palette to `target` (768 bytes). Step u of n submits
-- from + (to - from) * u / n; the original submits step 0 (n above 0) or the target at once
-- (n = 0), waits until FADE_WAIT_MS after its start, then submits the remaining steps
-- without waiting: the palette shows `from` for FADE_WAIT_MS, then the target (the last
-- step is exactly the target). The original's wait blocks its game loop; this one does not.
function Palette:fade_to(target, steps)
	assert(#target == 3 * M.ENTRIES, "palette: 768 bytes expected")
	steps = math.min(steps, M.FADE_MAX_STEPS)
	self.fade = { to = target, at = self.clock + (steps == 0 and 0 or M.FADE_WAIT_MS) }
	if steps == 0 then self:finish_fade() end
end

function Palette:finish_fade()
	local to = self.fade.to
	for i = 0, M.ENTRIES - 1 do
		self.r[i], self.g[i], self.b[i] = to:byte(i * 3 + 1, i * 3 + 3)
	end
	self.fade = nil
	self.version = self.version + 1
end

-- 0x4ccf40: the grayscale of the current palette as the fade target, faded at once.
function Palette:grayscale()
	local out = {}
	for i = 0, M.ENTRIES - 1 do
		local v = M.gray(self.r[i], self.g[i], self.b[i])
		out[i + 1] = string.char(v, v, v)
	end
	self:fade_to(table.concat(out), 0)
end

-- Advances the palette clock by `ms` of real time: the cycle driver once per elapsed
-- CYCLE_PERIOD_MS, at most MAX_CYCLES_PER_ADVANCE times (not while paused), and a pending
-- fade's last steps. Returns true when the
-- palette changed.
function Palette:advance(ms, paused)
	local before = self.version
	self.clock = self.clock + ms
	self.cycle_acc = (self.cycle_acc or 0) + ms
	local steps = 0
	while self.cycle_acc >= M.CYCLE_PERIOD_MS and steps < M.MAX_CYCLES_PER_ADVANCE do
		self.cycle_acc, steps = self.cycle_acc - M.CYCLE_PERIOD_MS, steps + 1
		if not paused then self:cycle() end
	end
	if self.cycle_acc >= M.CYCLE_PERIOD_MS then self.cycle_acc = 0 end
	if self.fade and self.clock >= self.fade.at then self:finish_fade() end
	return self.version ~= before
end

return M
