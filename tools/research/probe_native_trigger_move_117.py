#!/usr/bin/env python3
"""Hash-gated original trigger Move action, relocation and spatial reinsertion.

Reviewed original x86 and actual callees run only in private Unicorn memory.
Stock DAT and campaign parameters are separate from private unit/list/fog/terrain
fixtures. No helper stubs, Windows process, game loop, imports or guest I/O.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import platform
import struct
import subprocess
import sys

import capstone
import pefile
import probe_native_trigger_unit_counts_117 as counts_source
import inspect_native_primary_orders_117 as orders_source
import inspect_native_dat_requirements_117 as requirements_source
import probe_native_trigger_unit_actions_117 as actions_source
import inspect_native_training_lifecycle_117 as lifecycle_source
import inspect_native_unit_creation_117 as creation_source

HASH = counts_source.HASH
PYMS = counts_source.PYMS
RECORD, SPRITES, IMAGES, POOL = 0x800000, 0x802000, 0x803000, 0x808000
STACK, STOP, MASK = 0x900000, 0xA00000, 0xFFFFFFFF
ACTION_NAMES = {1: "Victory", 2: "Defeat", 11: "CreateUnitWithProperties", 22: "KillUnit",
                23: "KillUnitAtLocation", 24: "RemoveUnit", 25: "RemoveUnitAtLocation",
                39: "MoveUnit", 44: "CreateUnit", 46: "Order", 48: "GiveUnitsToPlayer",
                56: "Draw", 57: "SetAllianceStatus"}
ENTRY = {1: 0x4B5EF0, 2: 0x4B5F20, 11: 0x4B64D0, 22: 0x4B6840,
         23: 0x4B6870, 24: 0x4B68C0, 25: 0x4B68F0, 39: 0x4B5840,
         44: 0x4B64D0, 46: 0x4B65A0, 48: 0x4B5A50, 56: 0x4B5F70, 57: 0x4B5E90}
BLOCKS = dict(actions_source.BLOCKS)
BLOCKS.update(creation_source.BLOCKS)
BLOCKS.update({k: counts_source.BLOCKS[k] for k in ("count_getter", "global_cache", "memmove_rep_entry", "memmove_return")})
BLOCKS["rally_producer_type"] = lifecycle_source.BLOCKS["rally_producer_type"]
BLOCKS.update({
    "move_ordinary_unit": (0x4B7A80, 0x4B7B1A),
    "move_grounded_building": (0x4B7810, 0x4B7A7A),
    "hide_or_detach_unit": (0x430390, 0x43042C),
    "show_or_reattach_unit": (0x430430, 0x4304C1),
    "clear_unit_references": (0x4269B0, 0x426B75),
    "clear_bullet_references": (0x40A5A0, 0x40A5E6),
    "remove_unit_occupancy": (0x41A210, 0x41A248),
    "spatial_remove": (0x419460, 0x4194FE),
    "spatial_remove_x_slot": (0x419D00, 0x419D8D),
    "spatial_remove_y_slot": (0x419D90, 0x419E1D),
    "sprite_dirty_rectangles": (0x415F30, 0x415F8C),
    "screen_dirty_rectangle": (0x43EB50, 0x43EBF4),
    "resume_queued_or_idle_order": (0x42A5A0, 0x42A60D),
    "set_next_flingy_waypoint": (0x40CBD0, 0x40CBF7),
    "air_occupancy_remove": (0x481830, 0x481876),
    "remove_queued_order": (0x429D20, 0x429D67),
    "point_connectivity_group": (0x481D00, 0x481D50),
    "region_at_pixel": (0x481B10, 0x481B98),
    "native_memset": (0x4D0C60, 0x4D0CF0),
    "first_filtered_border_query": (0x442B60, 0x442BBA),
    "spatial_rectangle_query": (0x422330, 0x422454),
})
TERRAIN, FOW, GEOMETRY = 0xB00000, 0xC00000, 0x80A000
sections = actions_source.sections
campaign_inputs = actions_source.campaign_inputs


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("exe", "data", "manifest", "pyms"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--out", type=Path)
    p.add_argument("--static-out", type=Path)
    p.add_argument("--execute-original-code", action="store_true")
    a = p.parse_args()
    raw = a.exe.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == HASH, "Unknown executable hash"
    pe = pefile.PE(data=raw)
    assert pe.OPTIONAL_HEADER.ImageBase == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    dependencies = {name: hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest()
                    for name, module in (("probe_native_trigger_unit_counts_117.py", counts_source),
                                         ("inspect_native_primary_orders_117.py", orders_source),
                                         ("inspect_native_dat_requirements_117.py", requirements_source),
                                         ("probe_native_trigger_unit_actions_117.py", actions_source),
                                         ("inspect_native_training_lifecycle_117.py", lifecycle_source),
                                         ("inspect_native_unit_creation_117.py", creation_source))}
    assert dependencies["probe_native_trigger_unit_actions_117.py"] == "0f970a224dfb573f813af0a590588322c31b277961dd22119e3a2d75eb02927d"
    assert dependencies["inspect_native_training_lifecycle_117.py"] == "6c17063b98c6bb730a3906c7694c191c10228b7b41a971fe15a5bb1d90bc6b61"
    assert dependencies["inspect_native_unit_creation_117.py"] == "991cd331c288f3a0351729407801eebccf11d6db0516624235382cbdc8fb758e"
    mraw = a.manifest.read_bytes()
    manifest = json.loads(mraw)
    tuples, refs, unused_histogram, unused_supported = campaign_inputs(a.data, manifest)
    tuples = [x for x in tuples if x["fields"]["kind"] == 39]
    refs = [x for x in refs if x["fields"]["kind"] == 39]
    histogram = {"39": len(refs)}
    supported = {"39": sum(x["version"] in (59, 205) for x in refs)}
    if a.static_out:
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        witnesses = {}
        for name, (lo, hi) in BLOCKS.items():
            ins = list(md.disasm(pe.get_data(lo - 0x400000, hi - lo), lo))
            assert ins and ins[-1].address + ins[-1].size == hi, (name, hex(hi))
            witnesses[name] = [{"va": hex(x.address), "rva": hex(x.address - 0x400000),
                                "bytes": x.bytes.hex(), "mnemonic": x.mnemonic, "operands": x.op_str} for x in ins]
        a.static_out.write_text(json.dumps({"target_sha256": HASH, "target_version": "1.17.0.1", "original_code_executed": False,
            "extractor_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), "source_dependencies_sha256": dependencies,
            "evidence": "Static original instructions; names and dependency boundaries are analytical",
            "blocks": witnesses, "action_handlers": {"39": hex(ENTRY[39])},
            "move_group_type_targets": [hex(v) for v in struct.unpack("<4I", pe.get_data(0xB7C58, 16))],
            "campaign_actions": refs, "raw_action_histogram": histogram, "supported_action_histogram": supported}, indent=2) + "\n")
    if not a.execute_original_code:
        print(json.dumps({"target_sha256": HASH, "blocks": len(BLOCKS), "campaign_tuples": len(tuples), "stubs": []}))
        return
    assert a.out and platform.machine().lower() in ("x86_64", "amd64")
    assert subprocess.check_output(["git", "-C", str(a.pyms), "rev-parse", "HEAD"], text=True).strip() == PYMS
    sys.path.insert(0, str(a.pyms))
    from PyMS.FileFormats.DAT.UnitsDAT import UnitsDAT
    from PyMS.FileFormats.DAT.FlingyDAT import FlingyDAT
    datraw = (a.data / "arr/units.dat").read_bytes()
    assert hashlib.sha256(datraw).hexdigest() == manifest["effective"]["arr\\units.dat"]["sha256"]
    dat = UnitsDAT()
    dat.load_data(datraw)
    flingyraw = (a.data / "arr/flingy.dat").read_bytes()
    assert hashlib.sha256(flingyraw).hexdigest() == manifest["effective"]["arr\\flingy.dat"]["sha256"]
    flingy = FlingyDAT()
    flingy.load_data(flingyraw)
    orderraw = (a.data / "arr/orders.dat").read_bytes()
    orderrows = orders_source.stock_orders(orderraw)
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(0x400000, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095)
    u.mem_write(0x400000, pe.get_memory_mapped_image())
    u.mem_map(RECORD, 0x70000)
    u.mem_map(STACK, 0x10000)
    u.mem_map(STOP, 4096)
    u.mem_map(TERRAIN, 0xA0000)
    u.mem_map(FOW, 0x100000)
    cases, visited, approved = [], set(), set()

    def w(address, value, width=4):
        u.mem_write(address, (value & ((1 << (8 * width)) - 1)).to_bytes(width, "little"))

    def r(address, width=4):
        return int.from_bytes(u.mem_read(address, width), "little")

    for typ, unit in enumerate(dat.entries):
        for base, value, width in ((0x521660, unit.special_ability_flags, 4), (0x523C40, unit.staredit_group_flags, 1),
                (0x523FE0, unit.space_provided, 1), (0x521E78, unit.right_click_action, 1),
                (0x5211D8, unit.ground_weapon, 1), (0x5213A8, unit.air_weapon, 1), (0x521578, unit.ai_internal, 1),
                (0x521AD8, unit.sight_range, 1), (0x520F20, unit.return_to_idle, 1),
                (0x520D50, unit.comp_ai_idle, 1), (0x520E38, unit.human_ai_idle, 1),
                (0x523D28, (unit.supply_provided.whole << 1) | int(unit.supply_provided.half), 1),
                (0x523E10, (unit.supply_required.whole << 1) | int(unit.supply_required.half), 1),
                (0x5240C8, unit.build_score, 2)):
            w(base + typ * width, value, width)
        w(0x51FAA8 + typ, unit.graphics, 1)
        ext = unit.unit_extents
        u.mem_write(0x522C38 + 8 * typ, struct.pack("<4H", ext.left, ext.up, ext.right, ext.down))
        box = unit.staredit_placement_size
        u.mem_write(0x522728 + 4 * typ, struct.pack("<2H", box.width, box.height))
    for index, row in enumerate(flingy.entries):
        w(0x510C88 + 4 * index, row.speed)
        w(0x510FD0 + 2 * index, row.acceleration, 2)
        w(0x5114C0 + index, row.turn_radius, 1)
        w(0x511670 + index, row.movement_control, 1)
    for row in orderrows:
        w(0x51B200 + row["id"], row["interruptible"], 1)
        w(0x51B980 + row["id"] * 2, row["highlight"], 2)
        w(0x51B080 + row["id"], row["subunit_inheritance"], 1)
    # Native lazy initializer consumes the original requirement recipes.
    u.mem_write(0x51BB00, b"\xff\xff" * 189)
    u.mem_write(0x524708, b"\0" * (228 * 2))  # Explicit unavailable-unit rejection fixture for Create.
    w(0x6D319C, 2)
    w(0x5D6E40, max(x.unit_extents.left + x.unit_extents.right + 1 for x in dat.entries))
    w(0x5D6E44, max(x.unit_extents.up + x.unit_extents.down + 1 for x in dat.entries))
    w(0x5264B4, 256, 2)
    w(0x66696C, 0x810000)
    w(0x666458, 0x830000)
    w(0x6CE650, 0x850000)
    w(0x66644C, 0x860000)

    def setup(current=2, mode="ordinary", local=None):
        u.mem_write(RECORD, b"\0" * 0x10000)
        u.mem_write(TERRAIN, bytes(0xA0000))
        u.mem_write(FOW, bytes(0x100000))
        u.mem_write(0x5C51D0, bytes(1024))
        u.mem_write(0x5C55D0, bytes(1024))
        w(0x540750, 0)
        u.mem_write(0x5DA3A0, b"\0" * (0x150 * 24))
        u.mem_write(0x665B08, b"\0" * 48)
        u.mem_write(0x6A6070, b"\0" * 48)
        u.mem_write(0x6A5EF0, b"\0" * 384)
        u.mem_write(0x6CA758, b"\0" * 8)
        u.mem_write(0x6CA788, b"\0" * 8)
        u.mem_write(0x50E0D8, b"\0" * (12 * 8))
        u.mem_write(0x5349E0, b"\0" * 8)
        controllers = [2] * 8 if mode != "sparse" else [0, 1, 2, 3, 4, 5, 7, 2]
        finished = [0] * 8 if mode != "sparse" else [0, 1, 2, 0, 0, 0, 0, 0]
        for k in range(12):
            w(0x6726D0 + 36 * k, controllers[k] if k < 8 else 0, 1)
            w(0x6726D2 + 36 * k, 1 + k % 4, 1)
        u.mem_write(0x5349E0, bytes(finished))
        u.mem_write(0x665AF8, b"\0" * 8)
        u.mem_write(0x665AE0, b"\0" * 8)
        u.mem_write(0x67261C, b"\0" * 48)
        u.mem_write(0x534224, b"\0" * (12 * 24))
        u.mem_write(0x536408, b"\0" * (12 * 20 + 24))
        u.mem_write(0x534590, b"\0" * (12 * 46))
        u.mem_write(0x5365DE, b"\0" * (12 * 15 + 46))
        w(0x536720, 0, 1)
        for player in range(12):
            w(0x6BE610 + 8 * player, 0x840000)
            w(0x69B51C + 8 * player, 0)
        w(0x847D00, 0)
        w(0x69B584, 0x841000)
        w(0x841000, 0, 2)
        w(0x6CA754, current)
        w(0x508D50, current if local is None else local)
        w(0x508D54, current)
        w(0x67CC90, 0)
        w(0x672664, int(mode == "alternate"), 1)
        w(0x67294D, int(mode == "alternate"), 1)
        u.mem_write(0x5264AC, bytes([5, 1, 5, 0]))
        rel = [[(x + y) % 3 for y in range(12)] for x in range(12)]
        rel[current] = [k % 3 for k in range(12)]
        rel[current][current] = 2
        u.mem_write(0x534914, bytes(x for row in rel for x in row))
        u.mem_write(0x52904A, b"\x77" * 12)
        w(0x5C99B0, 0)
        w(0x5D6E48, 0)
        w(0x5D6E5C, 0)
        w(0x6CA6D0, 2, 1)
        w(0x6CA6D1, 7, 1)
        w(0x6C4C00, 0x77)
        w(0x6C4C04, 0x77)
        w(0x6C4C08, 0x77)
        w(0x6C4C0C, 0x77)
        w(0x67393C, 0x12345678)
        w(0x673934, 0x31415926)
        w(0x673930, 0)
        w(0x673938, 0)
        w(0x673940 + 4 * 14, 0)
        w(0x665B38, 0)
        w(0x665AF0, 0)
        w(0x5DA398, 0)
        w(0x543230, 8192, 2)
        w(0x543234, 8192, 2)
        u.mem_write(0x524708, b"\0" * (228 * 2))
        w(0x6CA74C, 0x7777, 2)
        w(0x6CA740, 0, 1)
        w(0x543300, 0xABCDEF)
        w(0x543304, 0x234567)
        w(0x6BE654, 0)
        w(0x5432FC, 0)
        for offset in (0x75, 0x79, 0x7F):
            w(0x6728D0 + offset, 0, 1)
        w(0x672945, 4, 1)  # Selects actual defeat cleanup's death-order branch; owner lists are empty.
        w(0x6CA73C, 1)  # The real UI wrapper returns when a result UI is already active.
        u.mem_write(0x5290C4, struct.pack("<336I", *([1000] * 336)))
        u.mem_write(0x531644, struct.pack("<2736I", *([1000] * 2736)))
        # Keep real source/cache count arrays as explicit witnesses to immediate effects.
        for base in (0x529604, 0x52C0C4, 0x6C5150, 0x6C7C10):
            u.mem_write(base, struct.pack("<2736I", *([17] * 2736)))
        for index in range(255):
            u.mem_write(0x534F40 + 20 * index, struct.pack("<4I2H", 32, 32, 96, 96, 0, 0))
        w(0x5AEF00, POOL)
        w(0x5AEF04, POOL + 20 * 31)
        w(0x5AEF08, 0)
        for i in range(32):
            w(POOL + 20 * i, POOL + 20 * (i - 1) if i else 0)
            w(POOL + 20 * i + 4, POOL + 20 * (i + 1) if i < 31 else 0)
        active = [k for k in range(8) if controllers[k] in (1, 2, 3, 4, 7)
                  and not (controllers[k] in (1, 2) and finished[k] in (1, 2))]
        if mode == "alternate":
            active = [k for k in (5, 1, 5, 0) if k in active]
        w(0x543230, 4096, 2); w(0x543234, 4096, 2)
        w(0x5264B4, 128, 2); w(0x5264B6, 128, 2)
        w(0x69B584, TERRAIN); w(TERRAIN, 4, 2); w(TERRAIN + 0x97A1C, GEOMETRY)
        w(TERRAIN + 0x44A0C, 64 * 256); w(TERRAIN + 0x44A10, 64 * 256)
        w(0x666454, FOW); w(0x666458, FOW + 0x40000); w(0x54323C, FOW + 0x40000)
        w(0x66696C, FOW + 0x60000); w(0x6CE650, FOW + 0x80000); w(0x66644C, FOW + 0x90000)
        u.mem_write(FOW, struct.pack("<I", 0x1FFFF) * 16384)
        for owner in range(12): w(0x5264CC + 4 * owner, 1 << owner)
        w(0x672668, 1); w(0x67266C, 1); w(0x6D3140, 0)
        return dict(current=current, controllers=controllers, active=active, rel=rel, mode=mode)

    def call(entry, ecx=RECORD, edx=0, args=()):
        done, inscount, entries = False, 0, []
        interesting = set(ENTRY.values()) | {0x4B6E90, 0x4B7020, 0x4261F0, 0x4B8CC0, 0x4B73A0,
                       0x4B7C70, 0x42A4D0, 0x422BA0, 0x420950, 0x407BB0, 0x407570, 0x42A340,
                       0x4B75D0, 0x4B7750, 0x4B81A0, 0x4B7D00,
                       0x424900, 0x424860, 0x425020, 0x478200, 0x428DF0, 0x426ED0,
                       0x4B7B20, 0x4B7A80, 0x4B7810, 0x430390, 0x430430, 0x419460,
                       0x419D00, 0x419D90, 0x43A270, 0x47DC30, 0x47E340, 0x4195A0,
                       0x423260, 0x42A5A0, 0x4269B0, 0x40A5A0, 0x481830, 0x481760,
                       0x466990, 0x466BD0}

        def hook(machine, pc, size, unused):
            nonlocal done, inscount
            if pc == STOP:
                done = True
                machine.emu_stop()
                return
            if (pc, size) not in approved:
                if not any(lo <= pc and pc + size <= hi for lo, hi in BLOCKS.values()):
                    raise RuntimeError(f"Unreviewed original instruction {pc:#x}")
                approved.add((pc, size))
            visited.add(pc)
            inscount += 1
            if pc in interesting:
                sp = machine.reg_read(UC_X86_REG_ESP)
                entries.append(dict(va=hex(pc), ecx=machine.reg_read(UC_X86_REG_ECX),
                                    edx=machine.reg_read(UC_X86_REG_EDX), stack=[r(sp + 4 + 4 * i) for i in range(3)]))
                if pc == 0x4261F0:
                    entries[-1]["native_main_rng_state"] = r(0x67393C)
                    entries[-1]["native_auxiliary_state"] = r(0x673934)

        h = u.hook_add(UC_HOOK_CODE, hook)
        sp = STACK + 0xF000
        u.mem_write(sp, struct.pack("<" + "I" * (len(args) + 1), STOP, *args))
        u.reg_write(UC_X86_REG_ESP, sp)
        u.reg_write(UC_X86_REG_ECX, ecx)
        u.reg_write(UC_X86_REG_EDX, edx)
        u.reg_write(UC_X86_REG_EAX, 0)
        try:
            u.emu_start(entry, STOP + 1, count=300000)
        finally:
            u.hook_del(h)
        assert done, (hex(entry), "Original return not reached")
        assert u.reg_read(UC_X86_REG_ESP) == sp + 4 + 4*len(args), (hex(entry), "Stack cleanup mismatch")
        return dict(return_eax=u.reg_read(UC_X86_REG_EAX), original_instructions=inscount,
                    complete_return=True, calls=entries)

    def record(label, inputs, actual, expected, execution, boundary="complete original function/callees on fixture-selected path"):
        assert actual == expected, (label, inputs, actual, expected)
        cases.append(dict(case=label, inputs=inputs, actual=actual, expected=expected, passed=True,
                          evidence_boundary=boundary, execution=execution))

    def action(kind, location=1, player=2, number=1, unit=15, modifier=1):
        u.mem_write(RECORD, struct.pack("<6IH4BH", location, 0, 0, 0, player, number, unit, kind, modifier, 0, 0, 0))

    def units(items, hidden=False):
        axes = [[], []]
        heads = {}
        for index, item in enumerate(items, 1):
            address = 0x5DA250 + 0x150 * index
            typ, owner = item.get("type", 15), item.get("owner", 2)
            sprite, image = SPRITES + 0x28 * index, IMAGES + 0x40 * index
            for offset, value, width in ((0x64, typ, 2), (0x4C, owner, 1), (0x4D, item.get("order", 3), 1),
                    (0x4E, 7, 1), (0xDC, item.get("status", 1), 4), (0xC, sprite, 4),
                    (0x58, 10, 2), (0x5A, 20, 2), (0x50, 17, 2), (0x93, 0x82, 1),
                    (0xC8, 44, 1), (0xC9, 61, 1), (0x85, 15, 1), (0xE3, 23, 1)):
                w(address + offset, value, width)
            w(sprite + 0x14, item.get("x", 64), 2)
            w(sprite + 0x16, item.get("y", 64), 2)
            w(sprite + 0xE, 0x20 if hidden else 0, 1)
            w(sprite + 0xD, dat.entries[typ].elevation_level, 1)
            w(sprite + 0x1C, 0)
            for offset in (0x18, 0x20):
                w(sprite + offset, image)
            w(image + 0x16, 0, 1)
            if owner in heads:
                previous = heads[owner][-1]
                w(previous + 0x6C, address)
                w(address + 0x68, previous)
            heads.setdefault(owner, []).append(address)
            ext = dat.entries[typ].unit_extents
            x, y = item.get("x", 64), item.get("y", 64)
            for offset, value in ((0x10, x), (0x12, y), (0x1C, x), (0x1E, y), (0x28, x), (0x2A, y)):
                w(address + offset, value, 2)
            w(address + 0x2C, x * 256); w(address + 0x30, y * 256)
            w(address + 0x105, 0 if item.get("status", 1) & 4 else 1, 1)
            for offset in (0x13C, 0x140, 0x144, 0x148): w(address + offset, 0xFFFFFFFF)
            if item.get("spatial", True):
                for axis, lo, hi in ((0, x - ext.left, x + ext.right), (1, y - ext.up, y + ext.down)):
                    axes[axis].extend([(lo, index, False), (hi, index, True)])
        for owner, addresses in heads.items():
            w(0x665B08 + 4 * owner, addresses[0])
        for axis, base in enumerate((0x5C99B8, 0x5D03F8)):
            axes[axis].sort()
            for pos, (coordinate, index, high) in enumerate(axes[axis]):
                w(base + 8 * pos, index)
                w(base + 8 * pos + 4, coordinate)
                w(0x5DA250 + 0x150 * index + (0x144 if axis else 0x13C) + 4 * high, pos)
        for index, item in enumerate(items, 1):
            w(SPRITES + 0x28 * index + 0xA, item.get("owner", 2), 1)
            w(0x5DA250 + 0x150 * index, 0x5DA250 + 0x150 * (index - 1) if index > 1 else 0)
            w(0x5DA250 + 0x150 * index + 4, 0x5DA250 + 0x150 * (index + 1) if index < len(items) else 0)
        if items:
            w(0x665AE0, 0x5DA3A0); w(0x665AE4, 0x5DA250 + 0x150 * len(items))
        rows = {}
        for index, item in enumerate(items, 1):
            rows.setdefault(item.get("y", 64) >> 5, []).append(SPRITES + 0x28 * index)
        for row, sprites in rows.items():
            w(0x5C51D0 + 4 * row, sprites[0]); w(0x5C55D0 + 4 * row, sprites[-1])
            for i, sprite in enumerate(sprites):
                w(sprite, sprites[i - 1] if i else 0)
                w(sprite + 4, sprites[i + 1] if i + 1 < len(sprites) else 0)
        w(0x5C99B0, len(axes[0]))
        return [0x5DA250 + 0x150 * i for i in range(1, len(items) + 1)]

    def initialize_vision(sight, stride=128):
        descriptor = 0x508A50 + sight * 28; grid = TERRAIN + 0x60000; records = TERRAIN + 0x64000
        inner = r(descriptor + 8) * r(descriptor + 12); w(descriptor + 0x10, inner)
        grid_ex = call(0x466990, ecx=descriptor, edx=grid); total = grid_ex["return_eax"]
        w(descriptor + 0x14, total - inner); w(descriptor + 0x18, records)
        u.mem_write(records, bytes(total * 20))
        records_ex = call(0x466BD0, ecx=descriptor, edx=grid, args=(stride,))
        return dict(sight=sight, stride=stride, inner=inner, total=total, native_steps=[grid_ex, records_ex], complete_return=True)

    def snapshot(addresses):
        spatial = []
        count = r(0x5C99B0)
        assert count == 2 * len(addresses)
        for axis, base in enumerate((0x5C99B8, 0x5D03F8)):
            entries = [[r(base + 8 * i), r(base + 8 * i + 4)] for i in range(count)]
            assert [v[1] for v in entries] == sorted(v[1] for v in entries), entries
            for i, address in enumerate(addresses, 1):
                low, high = r(address + (0x144 if axis else 0x13C)), r(address + (0x148 if axis else 0x140))
                assert 0 <= low < count and 0 <= high < count and low != high
                assert entries[low][0] == i and entries[high][0] == i
                typ = r(address + 0x64, 2); ext = dat.entries[typ].unit_extents
                sprite = r(address + 0xC); coordinate = r(sprite + (0x16 if axis else 0x14), 2)
                assert entries[low][1] == coordinate - (ext.up if axis else ext.left)
                assert entries[high][1] == coordinate + (ext.down if axis else ext.right)
            spatial.append(entries)
        positions = [[r(r(address + 0xC) + 0x14, 2), r(r(address + 0xC) + 0x16, 2)] for address in addresses]
        for address, (x, y) in zip(addresses, positions):
            assert [r(address + 0x28, 2), r(address + 0x2A, 2), r(address + 0x2C), r(address + 0x30)] == [x, y, x*256, y*256]
        sprite_rows = []
        found = []
        for row in range(r(0x5264B6, 2)):
            sprite, previous, chain = r(0x5C51D0 + 4 * row), 0, []
            while sprite:
                assert sprite not in found and len(found) < len(addresses)
                assert r(sprite) == previous and r(sprite + 0x16, 2) >> 5 == row
                found.append(sprite); chain.append(sprite); previous = sprite; sprite = r(sprite + 4)
            assert previous == r(0x5C55D0 + 4 * row)
            if chain: sprite_rows.append(dict(row=row, sprites=chain))
        assert set(found) == {r(address + 0xC) for address in addresses}
        return dict(positions=positions, spatial_count=count, indexes=spatial,
                    sprite_rows=sprite_rows,
                    orders=[r(address + 0x4D, 1) for address in addresses],
                    visible_flags=[r(r(address + 0xC) + 0xE, 1) & 32 for address in addresses],
                    active_head=r(0x665AE0), hidden_head=r(0x665AF8))

    setup(); addresses = units([dict(type=15, owner=2)])
    vision = initialize_vision(dat.entries[15].sight_range)
    u.mem_write(0x534F40 + 20, struct.pack("<4I2H", 300,300,500,500,0,0))
    action(39, location=1, player=2, number=2, unit=15, modifier=1)
    ex = call(ENTRY[39]); actual = snapshot(addresses)
    record("move_Civilian_accepted", dict(source=[32,32,96,96], destination=[300,300,500,500], source_owner=2, type=15,
           stock_units_dat=True, fixture_terrain="flat walkable", linked_images=[], ai=None, vision_shape=vision["total"]),
           [actual["positions"], actual["orders"], actual["spatial_count"], actual["visible_flags"], actual["active_head"], actual["hidden_head"],r(0x6C4C0C)],
           [[[400,400]], [dat.entries[15].return_to_idle], 2, [0], addresses[0], 0, 0],
           dict(complete_return=True, native_steps=[vision,ex]))
    cases[-1]["native_final_state"] = actual

    def location(index, corners, flags=0):
        u.mem_write(0x534F40 + 20 * index, struct.pack("<4I2H", *corners, 0, flags))

    def run_fixture(items, destination=(400,400), player=2, quantity=1, unit=15, mask=0, walkable=True):
        setup(); addresses = units(items)
        vision = initialize_vision(dat.entries[items[0].get("type", 15)].sight_range)
        location(0, (0,0,192,192), mask)
        location(1, (destination[0]-16,destination[1]-16,destination[0]+16,destination[1]+16))
        if not walkable: u.mem_write(FOW, struct.pack("<I", 0xFFFF)*16384)
        action(39, location=1, player=player, number=2, unit=unit, modifier=quantity)
        before = [hashlib.sha256(bytes(u.mem_read(base,2736*4))).hexdigest() for base in (0x529604,0x52C0C4)]
        w(0x6CA6D0, 1, 1)
        ex = call(ENTRY[39]); actual = snapshot(addresses)
        assert before == [hashlib.sha256(bytes(u.mem_read(base,2736*4))).hexdigest() for base in (0x529604,0x52C0C4)]
        assert r(0x6CA6D0, 1) == 0 and r(0x665AF8) == 0
        return addresses, actual, dict(complete_return=True, native_steps=[vision,ex])

    for typ, status in ((0,1),(7,1),(15,1),(16,1),(61,1),(65,1),(8,5),(11,5),(114,5)):
        for complete in (False,True):
            for destination in ((400,400),(80,80),(64,64)):
                for quantity in (0,1):
                    selected_status = status if complete else status & ~1
                    addresses, actual, ex = run_fixture([dict(type=typ,owner=2,status=selected_status)], destination,
                                                       quantity=quantity, unit=typ)
                    record("move_stock_ground_flyer_floating_building", dict(type=typ,status=selected_status,destination=list(destination),
                           quantity=quantity,stock_units_dat=True,null_ai=True,empty_linked_images=True),
                           [actual["positions"],actual["orders"],r(0x6C4C0C),r(0x6CA6D0,1)],
                           [[list(destination)],[dat.entries[typ].return_to_idle if complete else 3],999998 if quantity==0 else 0,0],ex)
                    cases[-1]["native_final_state"] = actual

    for typ in (0,7,15,16,61,65):
        for complete in (False,True):
            for quantity in (0,1,2,255):
                addresses, actual, ex = run_fixture([dict(type=typ,owner=2,status=int(complete))], quantity=quantity,
                                                   unit=typ,walkable=False)
                record("move_unwalkable_target_rolls_back_but_spends_budget",dict(type=typ,complete=complete,quantity=quantity,
                       terrain="all fixture tile flags have walkability bit16 clear"),
                       [actual["positions"],actual["orders"],r(0x6C4C0C),r(0x5DA398,2)],
                       [[[64,64]],[3],999998 if quantity==0 else quantity-1,60],ex)
                cases[-1]["native_final_state"] = actual

    # The source visitor first gathers candidates. These masks use the actual
    # native height/exclusion helper, with flat zero-height terrain tables.
    for airborne in (False,True):
        for mask in range(64):
            typ,status = (8,5) if airborne else (15,1)
            addresses, actual, ex = run_fixture([dict(type=typ,status=status)], unit=typ,mask=mask)
            excluded = bool(mask & (8 if airborne else 1))
            record("move_source_elevation_masks",dict(type=typ,status=status,mask=mask,terrain_height=0),
                   [actual["positions"],r(0x6C4C0C)],[[[64,64]] if excluded else [[400,400]],int(excluded)],ex)

    for quantity in (0,1,2,3,255):
        for same_owner in (False,True):
            # Status0x200000 is an explicit non-colliding unit-state fixture;
            # each real query/index update still executes and handles ties.
            items = [dict(type=8,status=0x200005,owner=2 if same_owner else i%3,x=48+16*i,y=64) for i in range(4)]
            addresses, actual, ex = run_fixture(items,player=17,quantity=quantity,unit=8)
            moved = sum(pos == [400,400] for pos in actual["positions"])
            expected_count = min(quantity or 999999,4)
            record("move_quantity_shared_across_players_and_tied_spatial_entries",dict(quantity=quantity,owners=[i["owner"] for i in items],
                   selector=17,status=0x200005,collision_disabled_fixture=True),
                   [moved,r(0x6C4C0C),actual["spatial_count"]],
                   [expected_count,(quantity or 999999)-expected_count,8],ex)
            cases[-1]["native_final_state"] = actual

    # Exact authored source/destination records and map dimensions, rather than
    # treating an MRGN index as a fixed globally shared rectangle.
    for ref in refs:
        fields = ref["fields"]
        raw_map = (a.data / ref["path"].replace("\\","/")).read_bytes()
        assert hashlib.sha256(raw_map).hexdigest() == ref["map_sha256"]
        parts = dict(sections(raw_map))
        width,height = struct.unpack("<2H",parts[b"DIM "])
        source_index,destination_index = (fields["location"]&255)-1,(fields["number"]&255)-1
        source = struct.unpack_from("<4I2H",parts[b"MRGN"],20*source_index)
        destination = struct.unpack_from("<4I2H",parts[b"MRGN"],20*destination_index)
        typ = fields["unit"] if fields["unit"] < 228 else 114 if fields["unit"]==231 else 15
        status = 5 if typ in (8,11,106,111,113,114,122) else 1
        current = next((k for k,v in enumerate(ref["owners"][:8]) if v),2)
        owner = fields["player"] if fields["player"] < 12 else current
        x,y = (source[0]+source[2])//2,(source[1]+source[3])//2
        target = [(destination[0]+destination[2])//2,(destination[1]+destination[3])//2]
        setup(current=current); w(0x543230,width*32,2); w(0x543234,height*32,2)
        w(0x5264B4,width,2); w(0x5264B6,height,2)
        u.mem_write(FOW,struct.pack("<I",0x1FFFF)*(width*height))
        addresses = units([dict(type=typ,status=status,owner=owner,x=x,y=y)])
        vision = initialize_vision(dat.entries[typ].sight_range,width)
        u.mem_write(0x534F40+20*source_index,struct.pack("<4I2H",*source))
        u.mem_write(0x534F40+20*destination_index,struct.pack("<4I2H",*destination))
        action(39,location=fields["location"],player=fields["player"],number=fields["number"],unit=fields["unit"],modifier=fields["modifier"])
        ex = call(ENTRY[39]); actual = snapshot(addresses)
        excluded = bool(source[5] & (8 if status&4 else 1))
        record("campaign_Move_record_with_authored_MRGN_and_dimensions",dict(reference=ref,source_location=list(source),
               destination_location=list(destination),dimensions=[width,height],fixture_unit_type=typ,fixture_status=status,
               fixture_owner=owner,fixture_current_player=current,terrain="flat walkable, height0",null_ai=True,empty_linked_images=True),
               [actual["positions"],r(0x6C4C0C)],
               [[[x,y]] if excluded else [target],(fields["modifier"] or 999999)-(0 if excluded else 1)],
               dict(complete_return=True,native_steps=[vision,ex]),
               "Exact stored action/DIM/MRGN parameters; complete selected original Move path on private unit/terrain/vision state; not a played campaign")
        cases[-1]["native_final_state"] = actual

    for queued in (False,True):
        for blocked in (False,True):
            setup(); addresses = units([dict(type=15),dict(type=7,owner=3,x=800,y=800),dict(type=114,status=5,owner=4,x=832,y=864)])
            mover,worker,factory = addresses
            vision = initialize_vision(dat.entries[15].sight_range)
            for off in (0xEC,0x5C,0x14,0x7C,0x80,0x11C,0xC8,0xD0): w(worker+off,mover)
            w(worker+0xCE,1,1); w(factory+0xFC,mover)
            enqueue = call(0x4295D0,ecx=worker,args=(6,0,mover,228,0))
            assert r(worker+0x74) != 0
            resume = call(0x4295D0,ecx=mover,args=(6,999|(222<<16),0,228,0)) if queued else None
            bullets = [0x850000,0x850070]
            w(0x540750,bullets[0]); w(bullets[0]+4,bullets[1])
            w(bullets[0]+0x5C,mover); w(bullets[0]+0x64,mover)
            w(bullets[1]+0x5C,worker); w(bullets[1]+0x64,worker)
            if blocked: u.mem_write(FOW,struct.pack('<I',0xFFFF)*16384)
            location(1,(384,384,416,416)); action(39,number=2)
            ex = call(ENTRY[39]); actual = snapshot(addresses)
            record('move_clears_live_worker_rally_queue_and_bullet_references',dict(queued_move=queued,blocked_target=blocked,
                   watcher_type=7,rally_factory=114,bullet_links='first mover, second worker'),
                   [[r(worker+off) for off in (0xEC,0x5C,0x14,0x7C,0x80,0x11C,0xC8,0xD0)],r(worker+0xCE,1),
                    r(worker+0x74),r(factory+0xFC),r(factory+0xF8,2),r(factory+0xFA,2),
                    [r(bullets[0]+off) for off in (0x5C,0x64)],[r(bullets[0]+off,2) for off in (0x58,0x5A)],
                    [r(bullets[1]+off) for off in (0x5C,0x64)],actual['positions']],
                   [[0]*8,0,0,0,64,64,[0,0],[64,64],[worker,worker],
                    [[64,64] if blocked else [400,400],[800,800],[832,864]]],
                   dict(complete_return=True,native_steps=[vision,enqueue]+([resume] if resume else [])+[ex]))
            cases[-1]['native_final_state']=actual
            record('move_success_resumes_queue_but_rollback_preserves_it',dict(queued=queued,blocked_target=blocked),
                   [r(mover+0x4D,1),r(mover+0x74)!=0,r(mover+0x58,2),r(mover+0x5A,2),r(0x5AEF08),r(mover+0x93,1)],
                   [3 if blocked else 6 if queued else dat.entries[15].return_to_idle,queued and blocked,
                    10 if blocked else 999 if queued else 0,20 if blocked else 222 if queued else 0,
                    int(queued and blocked),0x82],ex)

    for old_density in (0,1,5,255):
        for new_density in (0,1,7,254,255):
            for randomize in (False,True):
                setup(); addresses=units([dict(type=8,status=0x20005)])
                mover=addresses[0]; vision=initialize_vision(dat.entries[8].sight_range)
                density=0x854000; w(0x67CF60,density)
                old_cell=density+0xAB*(64//48)+(64//48)
                new_cell=density+0xAB*(400//48)+(400//48)
                w(old_cell,old_density,1); w(new_cell,new_density,1)
                w(mover+0x14E,64//48,1); w(mover+0x14F,64//48,1)
                w(mover+0x14C,0 if randomize else 0xF7,1)
                w(0x673930,1); w(0x67393C,12345); w(0x673940+37*4,0)
                location(1,(384,384,416,416)); action(39,number=2,unit=8)
                ex=call(ENTRY[39]); actual=snapshot(addresses)
                next_rng=(12345*0x15A4E35+1)&0xFFFFFFFF
                record('move_flyer_density_removal_reinsertion_and_rng_guard',dict(old_density=old_density,new_density=new_density,
                       randomize=randomize,type=8,status=0x20005,initial_rng=12345),
                       [actual['positions'],r(old_cell,1),r(new_cell,1),r(mover+0x14E,1),r(mover+0x14F,1),
                        r(mover+0x14C,1)&7,r(0x67393C),r(0x673940+37*4),r(0x673938)],
                       [[[400,400]],max(0,old_density-1),min(255,new_density+1),400//48,400//48,0,
                        next_rng if randomize else 12345,int(randomize),int(randomize)],
                       dict(complete_return=True,native_steps=[vision,ex]))
                cases[-1]['native_final_state']=actual

    for owner in range(8):
        for airborne in (False,True):
            for shared in (False,True):
                setup(); typ,status=(8,5) if airborne else (15,1)
                addresses=units([dict(type=typ,status=status,owner=owner)])
                vision=initialize_vision(dat.entries[typ].sight_range)
                vision_mask=(1<<owner)|(1<<((owner+1)%8)) if shared else 1<<owner
                w(0x5264CC+owner*4,vision_mask)
                location(1,(384,384,416,416)); action(39,player=owner,number=2,unit=typ)
                ex=call(ENTRY[39]); actual=snapshot(addresses)
                destination_flags=r(FOW+4*(12*128+12)); old_flags=r(FOW+4*(2*128+2))
                record('move_reveals_destination_with_native_shared_vision_mask',dict(owner=owner,airborne=airborne,
                       vision_mask=vision_mask,initial_tile_flags=0x1FFFF,native_shape=vision['total']),
                       [actual['positions'],destination_flags,old_flags,r(r(addresses[0]+0xC)+0xC,1)],
                       [[[400,400]],0x1FFFF&~vision_mask&~(vision_mask<<8),0x1FFFF,vision_mask],
                       dict(complete_return=True,native_steps=[vision,ex]))

    # Native ring ordering starts on the lower edge. With Civilian's stock
    # 23-pixel box, the width48 > 2*23 branch47e424 advances the first X
    # candidate from376 to400; the aligned lower Y edge is424.
    for same_axis in (False,True):
        setup(); addresses=units([dict(type=15,x=64,y=64),dict(type=15,owner=3,x=400,y=400),
                                 dict(type=15,owner=4,x=400 if same_axis else 800,y=800)])
        vision=initialize_vision(dat.entries[15].sight_range)
        location(0,(32,32,96,96));location(1,(384,384,416,416));action(39,number=2)
        ex=call(ENTRY[39]);actual=snapshot(addresses)
        record('move_occupied_destination_runs_original_ring_search',dict(blocker=[400,400],tied_neighbor_axis=same_axis),
               [actual['positions'],r(0x6C4C0C)],
               [[[400,424],[400,400],[400 if same_axis else 800,800]],0],
               dict(complete_return=True,native_steps=[vision,ex]))
        cases[-1]['native_final_state']=actual

    for source_field in (0,1,255,256,257,0x10001):
        for destination_field in (0,2,255,258,0x10002):
            setup();addresses=units([dict(type=15)]);vision=initialize_vision(dat.entries[15].sight_range)
            location(0,(32,32,96,96));location(1,(384,384,416,416))
            location(254,(32,32,96,96))
            action(39,location=source_field,number=destination_field)
            w(0x6C4C0C,77);w(0x6CA6D0,1,1)
            ex=call(ENTRY[39]);actual=snapshot(addresses)
            rejected=(source_field&255)==0 or destination_field==0
            destination_center=[64,64] if destination_field==255 else [400,400]
            record('move_source_byte_destination_dword_guard_and_byte_index',dict(source_field=source_field,
                   destination_field=destination_field,private_locations='1 and255 source64;2 destination400'),
                   [actual['positions'],r(0x6C4C0C),r(0x6CA6D0,1),ex['return_eax']],
                   [[[64,64]] if rejected else [destination_center],77 if rejected else 0,1 if rejected else 0,1],
                   dict(complete_return=True,native_steps=[vision,ex]),
                   'Complete original field-width guard/index paths on explicitly initialized private MRGN records; high-bit values are not stock campaign parameters')

    for width in (0,16,0xFFFF):
        for height in (0,16):
            for full_redraw in (False,True):
                setup();addresses=units([dict(type=15)]);vision=initialize_vision(dat.entries[15].sight_range)
                sprite=r(addresses[0]+0xC);first=r(sprite+0x18);second=first+0x40
                w(sprite+0x1C,first);w(first+4,second)
                w(first+0x20,32,2);w(first+0x22,48,2);w(first+0x28,width,2);w(first+0x2A,height,2)
                w(second+0x20,80,2);w(second+0x22,96,2);w(second+0x28,16,2);w(second+0x2A,32,2)
                u.mem_write(0x6D06D0,bytes(40*30));w(0x6D0BF5,int(full_redraw),1)
                location(1,(384,384,416,416));action(39,number=2)
                ex=call(ENTRY[39]);actual=snapshot(addresses)
                dirty=[i for i in range(40*30) if r(0x6D06D0+i,1)]
                expected=[]
                if not full_redraw:
                    expected=[40*y+x for y in (6,7,8) for x in (5,6)]
                    if width==16 and height==16:expected += [40*y+x for y in (3,4) for x in (2,3)]
                record('move_linked_images_dirty_screen_rectangles_with_native_memset',dict(width=width,height=height,
                       full_redraw=full_redraw,linked_images=2,image_animation=0,screen_coordinates='private clipped rectangles'),
                       [actual['positions'],dirty,r(first+0xC,2)&1,r(second+0xC,2)&1,r(0x6D0BF5,1)],
                       [[[400,400]],sorted(expected),1,1,1 if full_redraw else 2],
                       dict(complete_return=True,native_steps=[vision,ex]),
                       'Complete original Move/dirty-rectangle/memset path with private linked images; animation0 selects original idle-animation guard, no complete IScript/render-frame claim')

    result = dict(target_sha256=HASH, target_version="1.17.0.1", original_code_executed=True, complete_game_executed=False,
                  probe_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), source_dependencies_sha256=dependencies,
                  manifest_sha256=hashlib.sha256(mraw).hexdigest(), units_dat_sha256=hashlib.sha256(datraw).hexdigest(),
                  orders_dat_sha256=hashlib.sha256(orderraw).hexdigest(), pyms_commit=PYMS,
                  flingy_dat_sha256=hashlib.sha256(flingyraw).hexdigest(),
                  runtime=dict(architecture=platform.machine(), python=sys.version, unicorn=unicorn.__version__,
                               capstone=capstone.__version__, pefile=pefile.__version__), helper_stubs=[], imports_executed=[],
                  instruction_bound=300000, reviewed_blocks={k: [hex(x) for x in v] for k, v in BLOCKS.items()},
                  executed_instruction_witnesses=len(visited), cases_passed=len(cases), cases=cases,
                  campaign=dict(raw_move_actions=len(refs), supported_move_actions=supported["39"],
                                physical_maps_with_move=len({x["path"] for x in refs}),
                                ignored_records=sum(x["ignored"] for x in refs), tuples=tuples),
                  limits=["Private state fixtures, not played campaigns",
                          "Ordinary visible ground units/flyers/floating buildings; authored action/DIM/MRGN coverage has null AI and child links",
                          "Connected reference/queue/density/vision/redraw guards; linked redraw images are synthetic and use original animation0 guard",
                          "Grounded buildings, nonnull AI, subunits and complete IScript/render-frame behavior remain dependencies"])
    a.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(dict(cases_passed=len(cases), case_families=dict(Counter(x["case"] for x in cases)),
                         executed_instruction_witnesses=len(visited), probe_script_sha256=result["probe_script_sha256"])))


if __name__ == "__main__":
    main()
