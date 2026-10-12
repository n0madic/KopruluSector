#!/usr/bin/env python3
"""Run every image animation of the retail IScript through the original 1.17 code.

The scenario is the one of tests/lua/iscript_trace.lua (plan §7 phase 2, IScript VM):
new-game image and sprite pools, InitThingyPools 0x4183e0 (with the placement cursor), a
lone thingy 0x417b80 whose sprite shows the case's image, PlaySpriteAnimation 0x417220 for
animations other than Init, then FRAMES calls of UpdateThingies 0x418990. No unit, flingy or
bullet context exists, so opcodes that need one fault in the original as in the Lua VM.

Everything between those calls is original code: the IScript interpreter 0x410480, image
and sprite creation, overlays, thingy visibility. Two leaf consumers outside the image
system are replaced by recorders, as the Lua harness's hooks are: positional sound 0x446a20
(no game state) and the unit-type fit test 0x42ed50 (returns 1). The state after the setup
and after every frame is folded into the 32-bit hash of the Lua harness.

Inputs: the retail StarCraft.exe and archives (converter profile resolver), read in place.
Outputs:
- --golden: tests/golden/oracle/iscript/cases.lua, per case (image, animation, completed
  frames, hash); no retail bytes;
- --evidence: JSON with per-frame hashes and the fault of every case (private research
  evidence, docs/research/evidence/).
--case IMAGE ANIM prints the per-frame integers of one case for debugging.

Usage (private venv with unicorn 2.1.4 and pefile):
  python tools/research/probe_native_iscript_117.py --data DIR --golden OUT.lua --evidence OUT.json
"""
from __future__ import annotations

import argparse
import hashlib
import json
import logging
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

import pefile  # noqa: E402
from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP,  # noqa: E402
                               UC_X86_REG_ESP)

from converter.analyze import iscript as iscript_table  # noqa: E402
from converter.formats import dat, tbl  # noqa: E402
from converter.kpf.kpd import LO_COLUMNS  # noqa: E402
from converter.mpq.profile import Profile, Resolver  # noqa: E402

log = logging.getLogger("probe_native_iscript")

TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
GENERATOR = "tools/research/probe_native_iscript_117.py"

# Scenario constants: keep equal to tests/lua/iscript_trace.lua.
FRAMES = 80
ROOT_SPRITE = 0
ROOT_X, ROOT_Y = 4096, 4096
MAP = 256
SEED = 0x2A2A2A2A
SOUND, FIT = 0x534E44, 0x464954
TWO32 = 1 << 32
MASK = TWO32 - 1
INSTRUCTION_LIMIT = 20_000_000  # per native call; a script looping without a wait hangs

# Native addresses (docs/research/atlas/p04.md, p05.md, p06.md).
IMAGES, IMAGE_SIZE, IMAGE_COUNT = 0x544698, 0x40, 5000
IMAGE_FREE_HEAD, IMAGE_FREE_TAIL = 0x544694, 0x592898
VIRTUAL_POOLS = ((0x5928A0, 12), (0x592C50, 80), (0x594070, 64), (0x595078, 64))  # health, circles, ghosts, boxes
SPRITES, SPRITE_SIZE, SPRITE_COUNT = 0x5AF238, 0x24, 2500
SPRITE_FREE_HEAD, SPRITE_FREE_TAIL = 0x5C51C8, 0x5C51CC
ROW_HEADS, ROW_TAILS = 0x5C51D0, 0x5C55D0
LONE, LONE_SIZE = 0x5C5AE0, 0x10
LONE_FREE_HEAD, LONE_ACTIVE_HEAD = 0x5C7A20, 0x5C7A28
SCRIPT_PTR, UNIT_CTX, FLINGY_CTX, BULLET_CTX = 0x54330C, 0x543300, 0x543304, 0x543308
GRP_TABLE, LO_TABLE, SHIELD_LO_TABLE = 0x5436F8, 0x596400, 0x59B210
REMAP_TABLE = 0x508828  # 8 records of {index, data pointer, name[12]}
RNG_ENABLED, RNG_TOTAL, RNG_STATE, RNG_COUNTERS = 0x673930, 0x673938, 0x67393C, 0x673940
TILESET, MAP_W, MAP_H, MAP_W_PX, MAP_H_PX = 0x5264BC, 0x5264B4, 0x5264B6, 0x543230, 0x543234
TILE_FLAGS = 0x666454
SPRITE_IMAGE_COLUMN = 0x51EAA8
ISCRIPT_ID_COLUMN = 0x513E58
INIT_THINGIES, CREATE_LONE, PLAY_SPRITE, UPDATE_THINGIES = 0x4183E0, 0x417B80, 0x417220, 0x418990
SOUND_FN, FIT_FN = 0x446A20, 0x42ED50
REMAP_FIXTURE = 0x07000000  # fake remap data pointers, mapped back to indices 1..7

STACK, STOP, SCRIPT, TILES, ASSETS = 0x900000, 0xA00000, 0xB00000, 0xC00000, 0x1000000
ASSET_SIZE = 0x4000000


class ProbeError(RuntimeError):
    """The probe setup does not match the expected inputs."""


class Fault(Exception):
    """The original code faulted or did not return."""


def fold(h: int, v: int) -> int:
    return (h * 31 + (v & MASK)) & MASK


class Machine:
    def __init__(self, exe: bytes, files: dict[str, bytes], read) -> None:
        if hashlib.sha256(exe).hexdigest() != TARGET_HASH:
            raise ProbeError("StarCraft.exe is not the 1.17.0.1 target")
        pe = pefile.PE(data=exe)
        self.u = u = Uc(UC_ARCH_X86, UC_MODE_32)
        u.mem_map(0x400000, (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF)
        self.image0 = pe.get_memory_mapped_image()
        u.mem_write(0x400000, self.image0)
        for address, size in ((STACK, 0x10000), (STOP, 0x1000), (SCRIPT, 0x20000), (TILES, MAP * MAP * 4),
                              (ASSETS, ASSET_SIZE)):
            u.mem_map(address, size)
        self.events: list[int] = []
        self._load_data(files, read)
        u.hook_add(UC_HOOK_CODE, self._sound, begin=SOUND_FN, end=SOUND_FN)
        u.hook_add(UC_HOOK_CODE, self._fit, begin=FIT_FN, end=FIT_FN)

    # Memory helpers.
    def w(self, address: int, value: int, size: int = 4) -> None:
        self.u.mem_write(address, (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little"))

    def r(self, address: int, size: int = 4) -> int:
        return int.from_bytes(self.u.mem_read(address, size), "little")

    def rs(self, address: int, size: int) -> int:
        v = self.r(address, size)
        return v - (1 << (8 * size)) if v >> (8 * size - 1) else v

    def _load_data(self, files: dict[str, bytes], read) -> None:
        for layout in dat.load_layouts():
            data = files[layout.path]
            dat.check(layout, data)
            for c in layout.columns:
                self.u.mem_write(c.destination_va, data[c.offset:c.offset + c.size])
        images = next(l for l in dat.load_layouts() if l.table == "images")
        names = [n.decode("latin1") for n in tbl.decode_tbl(files["arr\\images.tbl"])]
        at, loaded = ASSETS, {}

        def place(index: int) -> int:
            nonlocal at
            if index == 0:
                return 0
            path = "unit\\" + names[index - 1]
            key = path.lower()
            if key not in loaded:
                blob = read(path)
                if blob is None:
                    loaded[key] = 0
                else:
                    if at + len(blob) > ASSETS + ASSET_SIZE:
                        raise ProbeError("asset region too small")
                    self.u.mem_write(at, blob)
                    loaded[key] = at
                    at += (len(blob) + 15) & ~15
            return loaded[key]

        data = files["arr\\images.dat"]
        for image_id, index in enumerate(dat.values(images, data, "grp_file")):
            self.w(GRP_TABLE + 4 * image_id, place(index))
        for kind, column in enumerate(LO_COLUMNS):
            for image_id, index in enumerate(dat.values(images, data, column)):
                base = SHIELD_LO_TABLE + 4 * image_id if column == "shield_overlay" else \
                    LO_TABLE + 4 * (kind_of(column) * 999 + image_id)
                self.w(base, place(index))
        self.u.mem_write(SCRIPT, files["scripts\\iscript.bin"])
        self.w(SCRIPT_PTR, SCRIPT)
        for k in range(1, 8):
            self.w(REMAP_TABLE + 4 + 20 * k, REMAP_FIXTURE + k)
        self.w(MAP_W, MAP, 2)
        self.w(MAP_H, MAP, 2)
        self.w(MAP_W_PX, MAP * 32, 2)
        self.w(MAP_H_PX, MAP * 32, 2)
        self.w(TILE_FLAGS, TILES)

    # Leaf consumer recorders (ABI from the callee: __fastcall, callee pops the stack).
    def _return(self, value: int, pop: int) -> None:
        u = self.u
        sp = u.reg_read(UC_X86_REG_ESP)
        ret = self.r(sp)
        u.reg_write(UC_X86_REG_EAX, value)
        u.reg_write(UC_X86_REG_ESP, sp + 4 + pop)
        u.reg_write(UC_X86_REG_EIP, ret)

    def _sound(self, u, address, size, _) -> None:
        sp = u.reg_read(UC_X86_REG_ESP)
        packed = self.r(sp + 4)
        self.events += [SOUND, u.reg_read(UC_X86_REG_ECX) & 0xFFFF, packed & 0xFFFF, packed >> 16]
        self._return(0, 8)

    def _fit(self, u, address, size, _) -> None:
        sp = u.reg_read(UC_X86_REG_ESP)
        self.events += [FIT, u.reg_read(UC_X86_REG_ECX), u.reg_read(UC_X86_REG_EDX) & 0xFFFF, self.r(sp + 4) & 0xFFFF]
        self._return(1, 4)

    def call(self, entry: int, ecx: int = 0, edx: int = 0, args: tuple[int, ...] = ()) -> int:
        u = self.u
        sp = STACK + 0xF000
        u.mem_write(sp, struct.pack("<" + "I" * (len(args) + 1), STOP, *(a & MASK for a in args)))
        u.reg_write(UC_X86_REG_ESP, sp)
        u.reg_write(UC_X86_REG_ECX, ecx & MASK)
        u.reg_write(UC_X86_REG_EDX, edx & MASK)
        u.reg_write(UC_X86_REG_EAX, 0)
        try:
            u.emu_start(entry, STOP, count=INSTRUCTION_LIMIT)
        except UcError as e:
            raise Fault(f"{e} at {u.reg_read(UC_X86_REG_EIP):#x}") from e
        if u.reg_read(UC_X86_REG_EIP) != STOP:
            raise Fault(f"no return within {INSTRUCTION_LIMIT} instructions (eip {u.reg_read(UC_X86_REG_EIP):#x})")
        return u.reg_read(UC_X86_REG_EAX)

    # Scenario.
    def reset(self, image_id: int) -> None:
        """New-game pools (records zeroed, free lists 0, n-1, ..., 1), RNG, contexts, map."""
        zero = bytes
        self.u.mem_write(IMAGES, zero(IMAGE_SIZE * IMAGE_COUNT))
        for base, count in VIRTUAL_POOLS:
            self.u.mem_write(base, zero(IMAGE_SIZE * count))
        self.u.mem_write(SPRITES, zero(SPRITE_SIZE * SPRITE_COUNT))
        self.u.mem_write(ROW_HEADS, zero(0x400))
        self.u.mem_write(ROW_TAILS, zero(0x400))
        self._chain(IMAGES, IMAGE_SIZE, IMAGE_COUNT, IMAGE_FREE_HEAD, IMAGE_FREE_TAIL)
        self._chain(SPRITES, SPRITE_SIZE, SPRITE_COUNT, SPRITE_FREE_HEAD, SPRITE_FREE_TAIL)
        for i in range(1, SPRITE_COUNT):
            self.w(SPRITES + i * SPRITE_SIZE + 0x10, i, 2)
        self.u.mem_write(TILES, zero(MAP * MAP * 4))
        self.w(RNG_ENABLED, 1)
        self.w(RNG_STATE, SEED)
        self.w(RNG_TOTAL, 0)
        self.u.mem_write(RNG_COUNTERS, zero(1024))
        for ctx in (UNIT_CTX, FLINGY_CTX, BULLET_CTX):
            self.w(ctx, 0)
        self.w(TILESET, image_id % 8, 2)
        self.events.clear()

    def _chain(self, base: int, size: int, count: int, head: int, tail: int) -> None:
        order = [0] + list(range(count - 1, 0, -1))
        for k, rec in enumerate(order):
            address = base + rec * size
            self.w(address, base + order[k - 1] * size if k else 0)
            self.w(address + 4, base + order[k + 1] * size if k + 1 < count else 0)
        self.w(head, base)
        self.w(tail, base + size)

    def image_key(self, ptr: int) -> int:
        if ptr == 0:
            return -1
        if IMAGES <= ptr < IMAGES + IMAGE_SIZE * IMAGE_COUNT:
            return (ptr - IMAGES) // IMAGE_SIZE
        key = IMAGE_COUNT
        for base, count in VIRTUAL_POOLS:
            if base <= ptr < base + IMAGE_SIZE * count:
                return key + (ptr - base) // IMAGE_SIZE
            key += count
        raise ProbeError(f"image pointer {ptr:#x} outside the pools")

    def frame_values(self, frame: int) -> list[int]:
        out = list(self.events)
        self.events.clear()
        free_sprite = self.r(SPRITE_FREE_HEAD)
        free_lone = self.r(LONE_FREE_HEAD)
        counters = struct.unpack_from("<256I", self.u.mem_read(RNG_COUNTERS, 1024))
        sources = sum(c * (i + 1) for i, c in enumerate(counters)) & MASK
        out += [frame, self.r(RNG_STATE), self.r(RNG_TOTAL), sources, self.image_key(self.r(IMAGE_FREE_HEAD)),
                (free_sprite - SPRITES) // SPRITE_SIZE if free_sprite else -1,
                (free_lone - LONE) // LONE_SIZE if free_lone else -1]
        t = self.r(LONE_ACTIVE_HEAD)
        while t:
            s = self.r(t + 0xC)
            out += [(t - LONE) // LONE_SIZE, (s - SPRITES) // SPRITE_SIZE if s else -1]
            out += [self.r(s + 8, 2), self.r(s + 0xA, 1), self.r(s + 0xB, 1), self.r(s + 0xC, 1), self.r(s + 0xD, 1),
                    self.r(s + 0xE, 1), self.r(s + 0xF, 1), self.r(s + 0x10, 2), self.r(s + 0x12, 1),
                    self.r(s + 0x13, 1), self.r(s + 0x14, 2), self.r(s + 0x16, 2), self.image_key(self.r(s + 0x18))]
            images, i = [], self.r(s + 0x1C)
            while i:
                images.append(i)
                i = self.r(i + 4)
            out.append(len(images))
            for i in images:
                method = self.r(i + 0xA, 1)
                param = self.r(i + 0x30)
                if method == 9:
                    param = next((k for k in range(7, 0, -1) if self.r(REMAP_TABLE + 4 + 20 * k) == param), 0)
                out += [self.image_key(i), self.r(i + 8, 2), method, self.r(i + 0xB, 1), self.r(i + 0xC, 2) & ~1,
                        self.rs(i + 0xE, 1), self.rs(i + 0xF, 1), self.r(i + 0x10, 2), self.r(i + 0x12, 2),
                        self.r(i + 0x14, 2), self.r(i + 0x16, 1), self.r(i + 0x17, 1), self.r(i + 0x18, 2),
                        self.r(i + 0x1A, 2), self.rs(i + 0x1C, 2), self.rs(i + 0x1E, 2), param]
            t = self.r(t + 4)
        return out

    def run(self, image_id: int, anim: int, frames: int = FRAMES, detail: bool = False,
            script_id: int | None = None) -> dict:
        self.reset(image_id)
        result = {"frames": -1, "hash": 0, "fault": None, "per_frame": []}
        values_out = []

        def record(frame: int) -> None:
            values = self.frame_values(frame)
            h = result["hash"]
            for v in values:
                h = fold(h, v)
            result["hash"], result["frames"] = h, frame
            result["per_frame"].append(h)
            if detail:
                values_out.append(values)

        try:
            self.call(INIT_THINGIES)
            column = SPRITE_IMAGE_COLUMN + 2 * ROOT_SPRITE
            script_column = ISCRIPT_ID_COLUMN + 4 * image_id
            saved, saved_script = self.r(column, 2), self.r(script_column)
            self.w(column, image_id, 2)
            if script_id is not None:
                self.w(script_column, script_id)
            try:
                t = self.call(CREATE_LONE, ROOT_SPRITE, ROOT_X, (ROOT_Y, image_id % 8))
            finally:
                self.w(column, saved, 2)
                self.w(script_column, saved_script)
            if anim and t:
                self.call(PLAY_SPRITE, self.r(t + 0xC), 0, (anim, 1))
            record(0)
            for f in range(1, frames + 1):
                self.call(UPDATE_THINGIES)
                record(f)
        except Fault as e:
            result["fault"] = str(e)
        if detail:
            result["values"] = values_out
        return result


def kind_of(column: str) -> int:
    """LO table kind of an images.dat overlay column (shield LO has its own table)."""
    return ("attack_overlay", "damage_overlay", "special_overlay", "landing_dust_overlay",
            "lift_off_dust_overlay").index(column)


# Synthetic programs for opcodes and operand ranges that no stock animation reaches without a
# unit context. They are appended to the retail iscript.bin under new script ids; a case
# patches the root image's images.dat IScript id while its sprite is created.
SYNTHETIC_ID = 1000
SYNTHETIC_ROOTS = (250, 17)  # tank body and drone: directional, with a special LO (follow-main)
SYNTHETIC_SPRITES = (330, 380)  # dust and a spell flash
FOLLOW_IMAGE = 501  # dust overlay: a short script without unit opcodes


def synthetic_script(retail: bytes) -> tuple[bytes, list[int]]:
    """Retail iscript.bin plus the synthetic headers; returns the file and the new script ids."""
    base = len(retail)
    w = lambda v: struct.pack("<H", v & 0xFFFF)  # noqa: E731
    dust, flash = SYNTHETIC_SPRITES
    code = bytearray()
    labels: dict[str, int] = {}
    fixups: list[tuple[int, str]] = []

    def emit(*parts) -> None:
        for part in parts:
            if isinstance(part, str):
                fixups.append((len(code), part))
                code.extend(b"\0\0")
            else:
                code.extend(part)

    def label(name: str) -> None:
        labels[name] = base + len(code)

    label("init")
    emit(b"\x02\x03", b"\x03\xfa", b"\x04\x01\x02", b"\x0c\x3e\x43\x50")  # sethorpos, setvertpos, setpos, no-ops
    emit(b"\x35", "sub")  # call
    emit(b"\x01", w(0xFFF0), b"\x01", w(1))  # playframtile beyond and within the GRP
    emit(b"\x17\x03")  # setflipstate 3 (compared as a whole byte)
    emit(b"\x06\x05\x02")  # waitrand with high < low
    emit(b"\x1a", w(10), w(5))  # playsndbtwn with a reversed range
    emit(b"\x19\x03", w(1), w(2), w(3))  # playsndrand
    emit(b"\x1e\x64", "l1", b"\x02\x07")  # randcondjmp 100; sethorpos 7
    label("l1")
    emit(b"\x10", w(dust), b"\x00\x00", b"\x11", w(flash), b"\x05\xfb")  # highsprol, lowsprul
    emit(b"\x42", w(dust), b"\x04\xfc")  # grdsprol
    emit(b"\x0b", w(FOLLOW_IMAGE), b"\x0a", w(FOLLOW_IMAGE))  # switchul, imgolorig
    emit(b"\x3d\x01\x01", b"\x05\x02")  # imgulnextid; wait 2
    emit(b"\x2c\x01", b"\x2b\x02", b"\x32", b"\x33", b"\x40", w(5), b"\x05\x01")  # engset, engframe, hide/show, warpoverlay, wait
    emit(b"\x07", "init")
    label("sub")
    emit(b"\x00", w(2), b"\x36")  # playfram 2; return
    label("death")
    emit(b"\x00", w(3), b"\x05\x01", b"\x16")  # playfram 3; wait 1; end
    for at, name in fixups:
        code[at:at + 2] = w(labels[name])
    header_at = base + len(code)
    # Header type 0: two animations (Init, Death).
    header = b"SCPE" + bytes([0, 0, 0, 0]) + w(labels["init"]) + w(labels["death"])
    index_at = header_at + len(header)
    old_index = struct.unpack_from("<I", retail, 0)[0]
    entries = bytearray()
    at = old_index
    while True:
        sid, offset = struct.unpack_from("<HH", retail, at)
        if sid == 0xFFFF:
            break
        entries += struct.pack("<HH", sid, offset)
        at += 4
    entries += struct.pack("<HH", SYNTHETIC_ID, header_at) + struct.pack("<HH", 0xFFFF, 0)
    blob = bytearray(retail) + code + header + entries
    struct.pack_into("<I", blob, 0, index_at)
    if len(blob) > 0xFFFF:
        raise ProbeError("synthetic iscript.bin exceeds 16-bit program counters")
    return bytes(blob), [SYNTHETIC_ID]


def cases(code: bytes, iscript_ids: tuple[int, ...]) -> list[tuple[int, int]]:
    table = iscript_table.Table.load()
    index = struct.unpack_from("<I", code, 0)[0]
    headers = {}
    while True:
        sid, offset = struct.unpack_from("<HH", code, index)
        if sid == 0xFFFF:
            break
        headers[sid] = offset
        index += 4
    out = []
    for image_id, sid in enumerate(iscript_ids):
        h = headers.get(sid)
        if h is None:
            continue
        count = table.header_animations[code[h + 4]]
        for anim, pc in enumerate(struct.unpack_from(f"<{count}H", code, h + 8)):
            if pc:
                out.append((image_id, anim))
    return out


def lua_golden(rows: list[tuple[int, int, int, int]], meta: dict, synthetic_hex: str,
               synthetic_rows: list[tuple[int, int, int, int, int]]) -> str:
    lines = [f"-- Generated by {GENERATOR}; do not edit.",
             "-- Original-code IScript traces (scenario: tests/lua/iscript_trace.lua): per case",
             "-- { image id, animation, completed frames (-1: setup faulted), hash }.",
             "return {"]
    for k in sorted(meta):
        lines.append(f"\t{k} = {json.dumps(meta[k])},")
    lines.append("\t-- Retail iscript.bin plus synthetic programs (SYNTHETIC_ID); cases")
    lines.append("\t-- { root image id, script id, animation, completed frames, hash }.")
    lines.append(f'\tsynthetic_iscript = "{synthetic_hex[2 * meta["retail_size"]:]}",')
    lines.append("\tsynthetic_index = " + str(struct.unpack_from("<I", bytes.fromhex(synthetic_hex[:8]), 0)[0]) + ",")
    lines.append("\tsynthetic = {")
    lines += [f"\t\t{{ {a}, {b}, {c}, {d}, {e} }}," for a, b, c, d, e in synthetic_rows]
    lines.append("\t},")
    lines.append("\tcases = {")
    lines += [f"\t\t{{ {a}, {b}, {c}, {d} }}," for a, b, c, d in rows]
    lines += ["\t},", "}", ""]
    return "\n".join(lines)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--data", type=Path, required=True, help="retail directory (StarCraft.exe and archives)")
    p.add_argument("--golden", type=Path)
    p.add_argument("--evidence", type=Path)
    p.add_argument("--case", type=int, nargs=2, metavar=("IMAGE", "ANIM"))
    p.add_argument("--frames", type=int, default=FRAMES)
    a = p.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    exe = (a.data / "StarCraft.exe").read_bytes()
    with Resolver(a.data, Profile.load(), full_hash=False) as resolver:
        files = {f.path: resolver.read(f.path)[1] for f in dat.load_layouts()}
        files["arr\\images.tbl"] = resolver.read("arr\\images.tbl")[1]
        files["scripts\\iscript.bin"] = resolver.read("scripts\\iscript.bin")[1]

        def read(path: str) -> bytes | None:
            return resolver.read(path)[1] if resolver.has(path) else None

        m = Machine(exe, files, read)
        synthetic, synthetic_ids = synthetic_script(files["scripts\\iscript.bin"])
        sm = Machine(exe, {**files, "scripts\\iscript.bin": synthetic}, read)
    code = files["scripts\\iscript.bin"]
    images = next(l for l in dat.load_layouts() if l.table == "images")
    if a.case:
        r = m.run(a.case[0], a.case[1], a.frames, detail=True)
        for f, values in enumerate(r["values"]):
            log.info("frame %d hash %d: %s", f, r["per_frame"][f], " ".join(map(str, values)))
        log.info("fault: %s", r["fault"])
        return 0
    all_cases = cases(code, dat.values(images, files["arr\\images.dat"], "iscript_id"))
    rows, evidence = [], []
    for n, (image_id, anim) in enumerate(all_cases):
        r = m.run(image_id, anim, a.frames)
        rows.append((image_id, anim, r["frames"], r["hash"]))
        evidence.append({"image": image_id, "animation": anim, "frames": r["frames"], "hash": r["hash"],
                         "fault": r["fault"], "per_frame": r["per_frame"]})
        if n % 250 == 0:
            log.info("%d/%d cases", n, len(all_cases))
    synthetic_rows = []
    for script_id in synthetic_ids:
        for image_id in SYNTHETIC_ROOTS:
            for anim in (0, 1):
                r = sm.run(image_id, anim, a.frames, script_id=script_id)
                synthetic_rows.append((image_id, script_id, anim, r["frames"], r["hash"]))
                evidence.append({"image": image_id, "script": script_id, "animation": anim, "frames": r["frames"],
                                 "hash": r["hash"], "fault": r["fault"], "per_frame": r["per_frame"]})
    meta = {"frames": a.frames, "root_sprite": ROOT_SPRITE, "root_x": ROOT_X, "root_y": ROOT_Y, "map": MAP,
            "seed": SEED, "target_sha256": TARGET_HASH,
            "iscript_sha256": hashlib.sha256(code).hexdigest(), "retail_size": len(code)}
    faults = sum(1 for e in evidence if e["fault"])
    log.info("%d cases, %d ended by a fault or hang", len(rows), faults)
    if a.golden:
        a.golden.parent.mkdir(parents=True, exist_ok=True)
        a.golden.write_text(lua_golden(rows, meta, synthetic.hex(), synthetic_rows))
    if a.evidence:
        a.evidence.parent.mkdir(parents=True, exist_ok=True)
        a.evidence.write_text(json.dumps({"generator": GENERATOR, **meta, "cases": evidence}, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
