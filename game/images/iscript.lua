-- IScript VM: RunIScript 0x410480 over the unchanged iscript.bin of iscript.kpd
-- (docs/research/atlas/p04.md interpreter table, checked against the decompiled handler
-- bodies). Methods of game.images.world's World.
--
-- Contexts: w.unit, w.flingy and w.bullet are the current unit/flingy/bullet (0x543300,
-- 0x543304, 0x543308), nil for none. Opcodes that touch them call w.hooks (bound by the
-- unit, order and combat code of phase 3):
--   unit_status(w, u), unit_owner(w, u), unit_target(w, u) -> target or nil,
--   unit_sprite(w, u), unit_resources(w, u), unit_heading(w, u),
--   turn_ccw(w, u, n), turn_cw(w, u, n), set_spawn_frame(w, u, n), attack_with(w, u, weapon),
--   order_weapon(w, u), spell_target_invalid(w, u), fire_weapon(w, u, weapon), use_weapon(w, u, weapon),
--   move_speed(w, u, v), set_move_speed(w, u, v), clear_repeat_attack(w, u), nobrk_start(w, u),
--   nobrk_end(w, u), ignore_rest_idle(w, u), set_forward_offset(w, u, d), set_flingy_direction(w, u, d),
--   set_top_speed(w, u, s), target_point(w, u) -> x, y, within_distance(w, u, d, x, y),
--   target_heading(w, u), angle_diff(w, a, b), attack_melee(w, u), missile_damage(w, bullet),
--   sprol_goliath(w, bullet), order_signal(w, flingy, set_mask, clear_mask), set_cloak_state(w, u, n),
--   unit_fits(w, unit_type, x, y), hide_order_marker(w), create_unstable_flingy(w, flingy_id, x, y).
-- Where the original dereferences a null context the VM raises an error naming the opcode;
-- where it checks for null, the VM does the same. A missing hook is an error, never a no-op.
local core = require("game.images.core")
local image = require("game.images.image")
local bytes = require("game.data.bytes")

local NONE = core.NONE
local F_MIRROR, SF_NOBRK = core.F_MIRROR, core.SF_NOBRK
local u8, u16 = bytes.u8, bytes.u16
local s8, s16, has = image.s8, image.s16, image.has
local floor, fmod = math.floor, math.fmod

local M = {}

-- RNG sources of the interpreter.
M.RNG_WAITRAND, M.RNG_PLAYSNDRAND, M.RNG_PLAYSNDBTWN, M.RNG_TURNRAND, M.RNG_RANDCONDJMP = 3, 4, 5, 6, 7
M.IGNORE_REST_WAIT = 10
M.GOLIATH_SPRITE = 505
M.GAS_OVERLAY_FULL, M.GAS_OVERLAY_EMPTY = 430, 435
M.SPELL_WEAPON_LIMIT = 130
M.STATUS_AIR = 0x04
M.WEAPON_GROUND, M.WEAPON_AIR = 1, 2

-- Stores the state at a return point of the interpreter.
local function store(img, i, state, pc, wait)
	if state then
		state.pc = pc
		if wait then state.wait = wait end
	else
		img.pc[i] = pc
		if wait then img.wait[i] = wait end
	end
end

local function context_error(op, what)
	error(("iscript: opcode 0x%02x needs a %s context"):format(op, what), 0)
end

function M.install(W)
	-- The bound hook `name`; a missing hook is an error naming it (and the opcode, if any).
	function W:hook(name, op)
		local f = self.hooks[name]
		if not f then
			error(op and ("iscript: opcode 0x%02x needs hook %s"):format(op, name) or ("images: needs hook " .. name), 0)
		end
		return f
	end

	local function hook(w, name, op) return w:hook(name, op) end

	local function unit(w, op)
		local u = w.unit
		if u == nil then context_error(op, "unit") end
		return u
	end

	-- Flingy order-signal byte +0x4f (sigorder/orderdone, warp-in callback).
	function W:flingy_signal(set_mask, clear_mask, op)
		if self.flingy == nil then context_error(op or 0, "flingy") end
		hook(self, "order_signal", op)(self, self.flingy, set_mask, clear_mask)
	end

	-- 0x41d4c0 on the current unit (cloak state bits 0x300).
	function W:unit_set_cloak_state(n)
		local u = unit(self, 0)
		hook(self, "set_cloak_state")(self, u, n)
	end

	-- PlayImageSound 0x40f5a0: positional sound at the sprite position, no game state.
	function W:image_sound(i, id)
		if self.sound then
			local s = self.img.sprite[i]
			self.sound(id % 65536, self.spr.x[s], self.spr.y[s])
		end
	end

	-- Followmaingraphic/engframe/engset tail: direction and mirror bit from the main
	-- image, callbacks for the current method, frame resync.
	local function follow_main(w, i, main)
		local img = w.img
		img.dir[i] = img.dir[main]
		img.flags[i] = image.set(img.flags[i], F_MIRROR, has(img.flags[main], F_MIRROR))
		w:image_set_method(i, img.method[i])
		w:image_sync_frame(i)
	end

	-- Sprite spawn opcodes 0x13/0x14/0x15: on success every image of the new sprite turns
	-- to this image's direction.
	local function spawn_turned(w, i, id, dx, dy, elevation)
		local t = w:image_spawn_sprite(i, id, dx, dy, elevation)
		if t ~= NONE then
			local img = w.img
			local d = img.dir[i]
			if has(img.flags[i], F_MIRROR) then d = 32 - d end
			w:sprite_set_direction(w.thg.sprite[t], d)
		end
	end

	local function cloaked_skip(w, sprite_id)
		local u = w.unit
		return u ~= nil and image.cloaked(hook(w, "unit_status", 0x13)(w, u))
			and w.dat.images.draw_if_cloaked[w.dat.sprites.image[sprite_id]] == 0
	end

	local function play_sound_list(w, i, code, at, noop)
		local n = u8(code, at)
		if not noop then
			if n == 0 then error("iscript: playsndrand with no sounds", 0) end
			local r = w.rng:raw(M.RNG_PLAYSNDRAND) % n
			w:image_sound(i, u16(code, at + 1 + 2 * r))
		end
		return at + 1 + 2 * n
	end

	-- RunIScript 0x410480 on image i. `state` (optional) is a separate { pc, ret, wait,
	-- header } record (walking-speed sampling); by default the image's own state runs.
	-- noop: only the opcodes marked † in the atlas execute. Returns the speed written by
	-- `move` to the output (out = true), else nil.
	function W:run_iscript(i, state, noop, out)
		local img = self.img
		local wait = state and state.wait or img.wait[i]
		if wait ~= 0 then
			if state then state.wait = wait - 1 else img.wait[i] = wait - 1 end
			return nil
		end
		local code = self.script.code
		local pc = state and state.pc or img.pc[i]
		local result
		local budget = self.step_limit -- harness guard against scripts looping without a wait
		while true do
			if budget then
				budget = budget - 1
				if budget < 0 then error(("iscript: no wait after %d opcodes (pc 0x%x)"):format(self.step_limit, pc), 0) end
			end
			local op = u8(code, pc)
			local at = pc + 1
			if op == 0x00 then -- playfram
				if not noop then self:image_set_frameset(i, u16(code, at)) end
				pc = at + 2
			elseif op == 0x01 then -- playframtile
				if not noop then
					local f = (u16(code, at) + self.tileset) % 65536
					if f < self:grp_frames(i) % 32768 then self:image_set_frameset(i, f) end
				end
				pc = at + 2
			elseif op == 0x02 then -- sethorpos
				if not noop then
					local x = s8(u8(code, at))
					if img.xoff[i] ~= x then img.xoff[i] = x end
				end
				pc = at + 1
			elseif op == 0x03 then -- setvertpos
				if not noop then
					local u = self.unit
					if u == nil or hook(self, "unit_status", op)(self, u) % 1024 < 256 then
						local y = s8(u8(code, at))
						if img.yoff[i] ~= y then img.yoff[i] = y end
					end
				end
				pc = at + 1
			elseif op == 0x04 then -- setpos
				if not noop then self:image_set_offset(i, u8(code, at), u8(code, at + 1)) end
				pc = at + 2
			elseif op == 0x05 then -- wait (also in no-op mode)
				store(img, i, state, at + 1, (u8(code, at) - 1) % 256)
				return result
			elseif op == 0x06 then -- waitrand
				if not noop then
					local a, b = u8(code, at), u8(code, at + 1)
					local d = b - a + 1
					if d == 0 then error("iscript: waitrand with an empty range", 0) end
					local r = self.rng:raw(M.RNG_WAITRAND) % 256
					store(img, i, state, at + 2, (fmod(r, d) - 1 + a) % 256)
					return result
				end
				pc = at + 2
			elseif op == 0x07 then -- goto
				pc = u16(code, at)
			elseif op == 0x08 or op == 0x09 then -- imgol / imgul
				if not noop then
					self:image_create_relative(i, u16(code, at), img.xoff[i] + u8(code, at + 2),
						u8(code, at + 3) + img.yoff[i], op == 0x08)
				end
				pc = at + 4
			elseif op == 0x0a or op == 0x0b then -- imgolorig / switchul
				if not noop then
					local n = self:image_create_relative(i, u16(code, at), 0, 0, op == 0x0a)
					if n ~= NONE then self:image_set_follow(n, 1) end
				end
				pc = at + 2
			elseif op == 0x0d or op == 0x0e then -- imgoluselo / imgululelo
				if not noop then
					local x, y = self:lo_offset(i, u8(code, at + 2), s8(u8(code, at + 3)))
					self:image_create_relative(i, u16(code, at), img.xoff[i] + x, img.yoff[i] + y, op == 0x0d)
				end
				pc = at + 4
			elseif op == 0x0f then -- sprol
				if not noop then
					local id = u16(code, at)
					local b = self.bullet
					if b ~= nil and hook(self, "sprol_goliath", op)(self, b) then id = M.GOLIATH_SPRITE end
					self:image_spawn_sprite(i, id, s8(u8(code, at + 2)), s8(u8(code, at + 3)),
						self.spr.elevation[img.sprite[i]] + 1)
				end
				pc = at + 4
			elseif op == 0x10 or op == 0x11 then -- highsprol / lowsprul
				if not noop then
					local elevation = op == 0x10 and self.spr.elevation[img.sprite[i]] - 1 or 1
					self:image_spawn_sprite(i, u16(code, at), s8(u8(code, at + 2)), s8(u8(code, at + 3)), elevation)
				end
				pc = at + 4
			elseif op == 0x12 then -- uflunstable
				if not noop then
					local s = img.sprite[i]
					hook(self, "create_unstable_flingy", op)(self, u16(code, at), s16(self.spr.x[s]) + img.xoff[i],
						s16(self.spr.y[s]) + img.yoff[i])
				end
				pc = at + 2
			elseif op == 0x13 or op == 0x14 then -- spruluselo / sprul
				local id = u16(code, at)
				if not noop and not cloaked_skip(self, id) then
					local e = self.spr.elevation[img.sprite[i]]
					if op == 0x14 then e = (e - 1) % 256 end
					spawn_turned(self, i, id, s8(u8(code, at + 2)), s8(u8(code, at + 3)), e)
				end
				pc = at + 4
			elseif op == 0x15 then -- sproluselo
				if not noop then
					local x, y = self:lo_offset(i, u8(code, at + 2), 0)
					spawn_turned(self, i, u16(code, at), x, y, self.spr.elevation[img.sprite[i]] + 1)
				end
				pc = at + 3
			elseif op == 0x16 then -- end
				if not noop then
					self:image_release(i)
					store(img, i, state, at)
					return result
				end
				pc = at
			elseif op == 0x17 then -- setflipstate
				if not noop then self:image_set_mirror(i, u8(code, at)) end
				pc = at + 1
			elseif op == 0x18 then -- playsnd
				if not noop then self:image_sound(i, u16(code, at)) end
				pc = at + 2
			elseif op == 0x19 then -- playsndrand
				pc = play_sound_list(self, i, code, at, noop)
			elseif op == 0x1a then -- playsndbtwn
				if not noop then
					local a, b = u16(code, at), u16(code, at + 2)
					local span = (b - a + 1) % 4294967296
					if span == 0 then error("iscript: playsndbtwn with an empty range", 0) end
					self:image_sound(i, self.rng:raw(M.RNG_PLAYSNDBTWN) % span + a)
				end
				pc = at + 4
			elseif op == 0x1b or op == 0x44 then -- domissiledmg / dogrddamage
				if not noop then
					if self.bullet == nil then context_error(op, "bullet") end
					hook(self, "missile_damage", op)(self, self.bullet)
				end
				pc = at
			elseif op == 0x1c then -- attackmelee
				if not noop then
					local u = unit(self, op)
					hook(self, "attack_melee", op)(self, u)
				end
				pc = play_sound_list(self, i, code, at, noop)
			elseif op == 0x1d then -- followmaingraphic
				if not noop then
					local main = self.spr.main[img.sprite[i]]
					if main ~= NONE and (img.frame[i] ~= img.frame[main]
						or has(img.flags[i], F_MIRROR) ~= has(img.flags[main], F_MIRROR)) then
						img.frameset[i] = img.frameset[main]
						follow_main(self, i, main)
					end
				end
				pc = at
			elseif op == 0x1e then -- randcondjmp (consumes RNG in no-op mode too)
				local r = self.rng:raw(M.RNG_RANDCONDJMP) % 256
				if r <= u8(code, at) then pc = u16(code, at + 1) else pc = at + 3 end
			elseif op == 0x1f or op == 0x20 then -- turnccwise / turncwise
				if not noop then
					local u = unit(self, op)
					hook(self, op == 0x1f and "turn_ccw" or "turn_cw", op)(self, u, u8(code, at))
				end
				pc = at + 1
			elseif op == 0x21 then -- turn1cwise
				if not noop then
					local u = unit(self, op)
					if hook(self, "unit_target", op)(self, u) == nil then hook(self, "turn_cw", op)(self, u, 1) end
				end
				pc = at
			elseif op == 0x22 then -- turnrand
				if not noop then
					local r = self.rng:raw(M.RNG_TURNRAND)
					local u = unit(self, op)
					if r % 4 == 1 then hook(self, "turn_ccw", op)(self, u, u8(code, at))
					else hook(self, "turn_cw", op)(self, u, u8(code, at)) end
				end
				pc = at + 1
			elseif op == 0x23 then -- setspawnframe
				if not noop then
					local u = unit(self, op)
					hook(self, "set_spawn_frame", op)(self, u, u8(code, at))
				end
				pc = at + 1
			elseif op == 0x24 then -- sigorder
				if not noop then self:flingy_signal(u8(code, at), 0, op) end
				pc = at + 1
			elseif op == 0x25 then -- attackwith
				if not noop then
					local u = unit(self, op)
					hook(self, "attack_with", op)(self, u, u8(code, at))
				end
				pc = at + 1
			elseif op == 0x26 then -- attack
				if not noop then
					local u = unit(self, op)
					local target = hook(self, "unit_target", op)(self, u)
					local air = target ~= nil and floor(hook(self, "unit_status", op)(self, target) / M.STATUS_AIR) % 2 == 1
					hook(self, "attack_with", op)(self, u, air and M.WEAPON_AIR or M.WEAPON_GROUND)
				end
				pc = at
			elseif op == 0x27 then -- castspell
				if not noop then
					local u = unit(self, op)
					local weapon = hook(self, "order_weapon", op)(self, u)
					if weapon < M.SPELL_WEAPON_LIMIT and not hook(self, "spell_target_invalid", op)(self, u) then
						hook(self, "fire_weapon", op)(self, u, weapon)
					end
				end
				pc = at
			elseif op == 0x28 then -- useweapon
				if not noop then
					local u = unit(self, op)
					hook(self, "use_weapon", op)(self, u, u8(code, at))
				end
				pc = at + 1
			elseif op == 0x29 then -- move (the output part runs in no-op mode too)
				local v = u8(code, at) * 256
				if out then result = hook(self, "move_speed", op)(self, unit(self, op), v) end
				if not noop then
					local u = unit(self, op)
					hook(self, "set_move_speed", op)(self, u, hook(self, "move_speed", op)(self, u, v))
				end
				pc = at + 1
			elseif op == 0x2a then -- gotorepeatattk
				if not noop and self.unit ~= nil then hook(self, "clear_repeat_attack", op)(self, self.unit) end
				pc = at
			elseif op == 0x2b then -- engframe
				if not noop then
					img.frameset[i] = u8(code, at)
					local main = self.spr.main[img.sprite[i]]
					if main == NONE then error("iscript: engframe without a main image", 0) end
					follow_main(self, i, main)
				end
				pc = at + 1
			elseif op == 0x2c then -- engset
				if not noop then
					local main = self.spr.main[img.sprite[i]]
					if main == NONE then error("iscript: engset without a main image", 0) end
					img.frameset[i] = ((self:grp_frames(main) % 32768) * u8(code, at) + img.frameset[main]) % 65536
					follow_main(self, i, main)
				end
				pc = at + 1
			elseif op == 0x2d then -- hide the right-click order marker (UI byte 0x5c9988)
				if not noop and self.hooks.hide_order_marker then self.hooks.hide_order_marker(self) end
				pc = at
			elseif op == 0x2e then -- nobrkcodestart
				if not noop then
					local u = unit(self, op)
					hook(self, "nobrk_start", op)(self, u)
					local s = hook(self, "unit_sprite", op)(self, u)
					if not has(self.spr.flags[s], SF_NOBRK) then self.spr.flags[s] = self.spr.flags[s] + SF_NOBRK end
				end
				pc = at
			elseif op == 0x2f then -- nobrkcodeend
				if not noop and self.unit ~= nil then hook(self, "nobrk_end", op)(self, self.unit) end
				pc = at
			elseif op == 0x30 then -- ignorerest
				if not noop then
					local u = unit(self, op)
					if hook(self, "unit_target", op)(self, u) ~= nil then
						store(img, i, state, pc, M.IGNORE_REST_WAIT)
						return result
					end
					hook(self, "ignore_rest_idle", op)(self, u)
				end
				pc = at
			elseif op == 0x31 then -- attkshiftproj
				if not noop then
					local u = unit(self, op)
					hook(self, "set_forward_offset", op)(self, u, u8(code, at))
					hook(self, "attack_with", op)(self, u, M.WEAPON_GROUND)
				end
				pc = at + 1
			elseif op == 0x32 then -- tmprmgraphicstart
				if not noop then self:image_hide(i) end
				pc = at
			elseif op == 0x33 then -- tmprmgraphicend
				if not noop then self:image_show(i) end
				pc = at
			elseif op == 0x34 then -- setfldirect
				if not noop then
					local u = unit(self, op)
					hook(self, "set_flingy_direction", op)(self, u, u8(code, at) * 8)
				end
				pc = at + 1
			elseif op == 0x35 then -- call (one level, also in no-op mode)
				if state then state.ret = at + 2 else img.ret[i] = at + 2 end
				pc = u16(code, at)
			elseif op == 0x36 then -- return
				pc = state and state.ret or img.ret[i]
			elseif op == 0x37 then -- setflspeed
				if not noop then
					local u = unit(self, op)
					hook(self, "set_top_speed", op)(self, u, u16(code, at))
				end
				pc = at + 2
			elseif op == 0x38 then -- creategasoverlays
				if not noop then
					local n = s8(u8(code, at))
					local x, y = self:lo_offset(i, image.LO_SPECIAL, n)
					local u = unit(self, op)
					local base = hook(self, "unit_resources", op)(self, u) ~= 0 and M.GAS_OVERLAY_FULL
						or M.GAS_OVERLAY_EMPTY
					local new = self:image_alloc()
					if new ~= NONE then
						local s = img.sprite[i]
						core.insert_before(img.prev, img.next, self.spr.images[s], i, new)
						self:image_init_record(new, (base + n) % 65536, s, img.xoff[i] + x, img.yoff[i] + y)
						self:image_init_drawing(new)
					end
				end
				pc = at + 1
			elseif op == 0x39 then -- pwrupcondjmp (also in no-op mode)
				local s = img.sprite[i]
				if s ~= NONE and self.spr.main[s] ~= i then pc = u16(code, at) else pc = at + 2 end
			elseif op == 0x3a then -- trgtrangecondjmp
				pc = at + 4
				if not noop then
					local u = unit(self, op)
					if hook(self, "unit_target", op)(self, u) ~= nil then
						local x, y = hook(self, "target_point", op)(self, u)
						if hook(self, "within_distance", op)(self, u, u16(code, at), x, y) then pc = u16(code, at + 2) end
					end
				end
			elseif op == 0x3b then -- trgtarccondjmp
				pc = at + 6
				if not noop then
					local u = unit(self, op)
					if hook(self, "unit_target", op)(self, u) ~= nil then
						local h = hook(self, "target_heading", op)(self, u)
						if hook(self, "angle_diff", op)(self, h, u16(code, at)) < u16(code, at + 2) then
							pc = u16(code, at + 4)
						end
					end
				end
			elseif op == 0x3c then -- curdirectcondjmp
				pc = at + 6
				if not noop then
					local u = unit(self, op)
					local h = hook(self, "unit_heading", op)(self, u)
					if hook(self, "angle_diff", op)(self, h, u16(code, at)) < u16(code, at + 2) then
						pc = u16(code, at + 4)
					end
				end
			elseif op == 0x3d then -- imgulnextid
				if not noop then
					self:image_create_relative(i, img.id[i] + 1, img.xoff[i] + u8(code, at), u8(code, at + 1) + img.yoff[i],
						false)
				end
				pc = at + 2
			elseif op == 0x3f then -- liftoffcondjmp
				pc = at + 2
				if not noop then
					local u = unit(self, op)
					if floor(hook(self, "unit_status", op)(self, u) / M.STATUS_AIR) % 2 == 1 then pc = u16(code, at) end
				end
			elseif op == 0x40 then -- warpoverlay
				if not noop then img.param[i] = u16(code, at) end
				pc = at + 2
			elseif op == 0x41 then -- orderdone
				if not noop then self:flingy_signal(0, u8(code, at), op) end
				pc = at + 1
			elseif op == 0x42 then -- grdsprol
				if not noop then
					local s = img.sprite[i]
					local dx, dy = s8(u8(code, at + 2)), s8(u8(code, at + 3))
					local px = s16(s16(self.spr.x[s]) + img.xoff[i] + dx)
					local py = s16(img.yoff[i] + dy + s16(self.spr.y[s]))
					if hook(self, "unit_fits", op)(self, 0, px, py) then
						self:image_spawn_sprite(i, u16(code, at), dx, dy, self.spr.elevation[s] + 1)
					end
				end
				pc = at + 4
			else -- 0x0c, 0x3e, 0x43 and above 0x44: one-byte no-ops
				pc = at
			end
		end
	end
end

return M
