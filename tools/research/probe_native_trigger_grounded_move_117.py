#!/usr/bin/env python3
"""Hash-gated original grounded trigger Move and placement wrapper.

Only reviewed original x86 and real callees execute in private Unicorn memory.
No stubs, Windows process, game loop, imported functions or guest file/network
operations. Ground footprint flags, empty addon/fog-sprite/AI guards and terrain
records remain explicit fixtures separate from stock DAT/campaign parameters.
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
import probe_native_trigger_move_117 as move_source
import probe_native_placement_117 as placement_source
import probe_native_trigger_ownership_117 as ownership_source

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
BLOCKS = dict(move_source.BLOCKS)
BLOCKS.update({k: placement_source.BLOCKS[k] for k in (
    "unit_occupancy_grid", "tile_occupancy_predicate", "builder_overlaps_tile", "resource_container_predicate")})
BLOCKS.update({k: ownership_source.BLOCKS[k] for k in (
    "race_group", "constructing_building_predicate", "global_flying_unit_head")})
BLOCKS["creation_map_bounds"] = lifecycle_source.BLOCKS["creation_map_bounds"]
BLOCKS.update({
    "grounded_move_placement_wrapper": (0x412E20,0x412F10),
    "grounded_move_physical_tile_grid": (0x412560,0x41271B),
    "ground_occupancy_remove": (0x41A190,0x41A209),
    "ground_occupancy_add": (0x419FD0,0x41A049),
    "tile_flags_clear": (0x4134A0,0x4134EC),
    "tile_flags_set": (0x412C40,0x412C8C),
    "terran_post_relocation": (0x437180,0x437196),
    "find_adjacent_addon": (0x431AE0,0x431B62),
    "adjacent_addon_callback": (0x4317D0,0x4318DE),
    "find_linked_addon_parent": (0x437290,0x4372B3),
    "old_position_creep_candidate_update": (0x4B46E0,0x4B47EF),
    "building_creep_bounds": (0x4C04F0,0x4C066D),
    "creep_producing_type": (0x4C0790,0x4C07B7),
    "map_tile_group_one": (0x4C07E0,0x4C0808),
    "mark_building_status_update": (0x42FFC0,0x42FFCB),
    "create_fog_unit_guard": (0x417B10,0x417B78),
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
                                         ("inspect_native_unit_creation_117.py", creation_source),
                                         ("probe_native_trigger_move_117.py", move_source),
                                         ("probe_native_placement_117.py", placement_source),
                                         ("probe_native_trigger_ownership_117.py", ownership_source))}
    assert dependencies["probe_native_trigger_unit_actions_117.py"] == "0f970a224dfb573f813af0a590588322c31b277961dd22119e3a2d75eb02927d"
    assert dependencies["inspect_native_training_lifecycle_117.py"] == "6c17063b98c6bb730a3906c7694c191c10228b7b41a971fe15a5bb1d90bc6b61"
    assert dependencies["inspect_native_unit_creation_117.py"] == "991cd331c288f3a0351729407801eebccf11d6db0516624235382cbdc8fb758e"
    assert dependencies["probe_native_trigger_move_117.py"] == "50f3087b82de7b8b0e2fe6d0c391bc03a0b46a9144060fd6907e1e6bf86dcb2a"
    assert dependencies["probe_native_placement_117.py"] == "f027748ec61c44313ef42fc00a33a402108e447bb3c4f202e5f71838f72374ea"
    assert dependencies["probe_native_trigger_ownership_117.py"] == "c3f7a5ee2372c70450b8e2d5cda15271affc2706f8a6a7c5cfbfe99b383219c3"
    mraw = a.manifest.read_bytes()
    manifest = json.loads(mraw)
    tuples, refs, unused_histogram, unused_supported = campaign_inputs(a.data, manifest)
    tuples = [x for x in tuples if x["fields"]["kind"] == 39 and x["fields"]["unit"] in (106,111,113,114,122,229,231)]
    refs = [x for x in refs if x["fields"]["kind"] == 39 and x["fields"]["unit"] in (106,111,113,114,122,229,231)]
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
                       0x466990, 0x466BD0, 0x412E20, 0x412560, 0x412360, 0x41A190, 0x419FD0,
                       0x437180, 0x431AE0, 0x437290, 0x4B46E0, 0x4C04F0, 0x4C0790}

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

    def location(index,corners,flags=0):
        u.mem_write(0x534F40+20*index,struct.pack("<4I2H",*corners,0,flags))

    def footprint(typ,x,y):
        box=dat.entries[typ].staredit_placement_size; width,height=box.width//32,box.height//32
        left,top=x//32-width//2,y//32-height//2
        return [(left+dx,top+dy) for dy in range(height) for dx in range(width)]

    setup(); addresses=units([dict(type=106,status=3,x=256,y=240)])
    mover=addresses[0];vision=initialize_vision(dat.entries[106].sight_range)
    u.mem_write(FOW,struct.pack("<I",0x10000)*16384)
    for x,y in footprint(106,256,240):w(FOW+4*(128*y+x),0x8010000)
    w(0x6CA7A0,FOW+0x60000);w(0x6CA7AC,128);w(0x6CA7B0,128)
    w(0x5C9970,0);w(0x665B60,0);w(0x6C46D8,77)
    location(0,(128,128,384,384));location(1,(384,384,416,416))
    action(39,number=2,unit=106)
    ex=call(ENTRY[39]);actual=snapshot(addresses)
    old_flags=[r(FOW+4*(128*y+x))&0x8000000 for x,y in footprint(106,256,240)]
    new_flags=[r(FOW+4*(128*y+x))&0x8000000 for x,y in footprint(106,384,400)]
    record('grounded_CommandCenter_connected_Move',dict(type=106,status=3,old=[256,240],target=[400,400],
           empty_addons=True,empty_ai=True,empty_linked_images=True,old_creep_map_groups=0),
           [actual['positions'],actual['orders'],r(0x6C4C0C),old_flags,new_flags,r(0x665B60),r(0x6C46D8)],
           [[[384,400]],[3],0,[0]*12,[0x8000000]*12,1,0],dict(complete_return=True,native_steps=[vision,ex]))
    cases[-1]['native_final_state']=actual
    def reset_grounded(typ,owner=2,complete=True,target=(400,400),tile_flags=0x10000,extra=()):
        setup(current=owner);box=dat.entries[typ].staredit_placement_size
        old=[160+box.width//2,160+box.height//2]
        items=[dict(type=typ,status=3 if complete else 2,owner=owner,x=old[0],y=old[1])]+list(extra)
        addresses=units(items);vision=initialize_vision(dat.entries[typ].sight_range)
        u.mem_write(FOW,struct.pack('<I',tile_flags)*16384)
        for x,y in footprint(typ,*old):w(FOW+4*(128*y+x),tile_flags|0x8000000)
        w(0x6CA7A0,FOW+0x60000);w(0x6CA7AC,128);w(0x6CA7B0,128)
        w(0x5C9970,0);w(0x665B60,0);w(0x6C46D8,77);w(0x665B54,0)
        location(0,(0,0,512,512));location(1,(target[0],target[1],target[0],target[1]))
        action(39,number=2,unit=typ,player=owner)
        return addresses,vision,old

    def aligned_destination(typ,x,y):
        ext=dat.entries[typ].unit_extents;box=dat.entries[typ].staredit_placement_size
        x=max(ext.left,min(x,4096-ext.right-1));y=max(ext.up,min(y,4096-ext.down-1))
        x-=box.width//2;y-=box.height//2
        if (x&0xFFFFFFFF)>=4096:x=4095
        if (y&0xFFFFFFFF)>=4096:y=4095
        return [(x&~31)+box.width//2,(y&~31)+box.height//2]

    for typ in (106,111,113,114,122,154,156,160,166,171):
        for target in ((400,400),(399,401),(0,0),(32,32),(4095,4095),(4064,4064),(2048,1024)):
            for complete in (False,True):
                for blocked in (False,True):
                    tile_flags=0x10000|(0x00800000 if blocked else 0)
                    addresses,vision,old=reset_grounded(typ,complete=complete,target=target,tile_flags=tile_flags)
                    aligned=aligned_destination(typ,*target)
                    ext=dat.entries[typ].unit_extents
                    eligible=complete and not blocked and aligned[0]>=ext.left and aligned[1]>=ext.up \
                             and aligned[0]+ext.right<4096 and aligned[1]+ext.down<4096
                    ex=call(ENTRY[39]);actual=snapshot(addresses)
                    # Validation precedes relocation; the latter separately
                    # reserves32 bottom pixels through41a4b0.
                    position=[max(ext.left,min(aligned[0],4096-ext.right-1)),
                              max(ext.up,min(aligned[1],4096-ext.down-33))] if eligible else old
                    old_mask=[r(FOW+4*(128*y+x))&0x8000000 for x,y in footprint(typ,*old)]
                    new_mask=[r(FOW+4*(128*y+x))&0x8000000 for x,y in footprint(typ,*position)]
                    record('grounded_stock_Terran_Protoss_Move_alignment_rejection_and_footprints',dict(type=typ,complete=complete,
                           requested=list(target),expected_alignment=aligned,physical_block=blocked,old=old,null_power_sources=True),
                           [actual['positions'],actual['orders'],r(0x6C4C0C),old_mask,new_mask,r(0x665B60),r(0x6C46D8)],
                           [[position],[3],0,[0 if eligible else 0x8000000]*len(old_mask),[0x8000000]*len(new_mask),
                            int(eligible),0 if eligible else 77],dict(complete_return=True,native_steps=[vision,ex]))
                    cases[-1]['native_final_state']=actual
                    if complete and not blocked and dat.entries[typ].special_ability_flags&0x80000 and eligible:
                        assert not any(v['va']=='0x430000' for v in ex['calls'])

    # Complete wrapper with every original stock placement size/property row.
    # Empty spatial candidates and no nearby resource units are explicit inputs.
    for typ,row in enumerate(dat.entries):
        for flag in (0,0x00400000,0x00800000,0x08000000,0x20000000,0x80000000,0x80800000,0xA8800000):
            setup();u.mem_write(FOW,struct.pack('<I',flag)*16384)
            u.mem_write(0x675188,b'\x77'*48)
            ex=call(0x412E20,ecx=2,edx=10,args=(12,typ,0))
            width,height=row.staredit_placement_size.width//32,row.staredit_placement_size.height//32
            mask=0x28800000 if row.staredit_group_flags&1 else 0x28C00000
            blocked=bool(flag&mask) and not (194<=typ<=199 and flag&0x80000000)
            # Occupancy rechecks the building bit even when the physical
            # grid's signed-flag exception194..199 accepted this cell.
            code=4 if (blocked or flag&0x08000000) and width and height else 0
            expected=[0x77]*48
            for y in range(height):
                for x in range(width):expected[y*6+x]=code
            record('grounded_Move_placement_wrapper_all_stock_DAT_rows',dict(type=typ,physical_flags=flag,
                   stock_placement_pixels=[row.staredit_placement_size.width,row.staredit_placement_size.height],
                   property_flags=row.special_ability_flags,group_flags=row.staredit_group_flags,empty_spatial=True),
                   [ex['return_eax'],list(u.mem_read(0x675188,48))],[code,expected],ex,
                   'Complete original412e20/412560/412360 wrapper with stock DAT properties/sizes and private empty spatial/tile inputs; does not admit every type to Move action')

    for typ in (106,114,154,160):
        row=dat.entries[typ];width,height=row.staredit_placement_size.width//32,row.staredit_placement_size.height//32
        for tile_x,tile_y in ((-1,12),(10,-1),(128-width+1,12),(10,128-height+1),(128-width,128-height)):
            setup();u.mem_write(FOW,struct.pack('<I',0x10000)*16384);u.mem_write(0x675188,b'\x77'*48)
            ex=call(0x412E20,ecx=2,edx=tile_x&0xFFFFFFFF,args=(tile_y&0xFFFFFFFF,typ,0))
            out=tile_x<0 or tile_y<0 or tile_x+width>128 or tile_y+height>128
            expected=[3]*48 if out else [0 if y<height and x<width else 0x77 for y in range(8) for x in range(6)]
            record('grounded_Move_placement_wrapper_bounds_and_memset',dict(type=typ,tile_x=tile_x,tile_y=tile_y,dimensions=[width,height]),
                   [ex['return_eax'],list(u.mem_read(0x675188,48))],[3 if out else 0,expected],ex)

    for typ in (106,154):
        for near in (False,True):
            extra=[dict(type=188,status=3,owner=11,x=500 if near else 1600,y=400)]
            addresses,vision,old=reset_grounded(typ,extra=extra)
            ex=call(ENTRY[39]);actual=snapshot(addresses);aligned=aligned_destination(typ,400,400)
            record('grounded_depot_Move_resource_neighborhood_rejection',dict(type=typ,resource_type=188,resource_class='VespeneGeyser',resource_owner=11,
                   resource_position=[500 if near else 1600,400],near=near),
                   [actual['positions'],r(0x6C4C0C),r(0x665B60)],
                   [[old if near else aligned,[500 if near else 1600,400]],0,0 if near else 1],
                   dict(complete_return=True,native_steps=[vision,ex]))
            cases[-1]['native_final_state']=actual

    for ref in refs:
        fields=ref['fields'];raw_map=(a.data/ref['path'].replace('\\','/')).read_bytes()
        assert hashlib.sha256(raw_map).hexdigest()==ref['map_sha256']
        parts=dict(sections(raw_map));width,height=struct.unpack('<2H',parts[b'DIM '])
        source_index,destination_index=(fields['location']&255)-1,(fields['number']&255)-1
        source=struct.unpack_from('<4I2H',parts[b'MRGN'],20*source_index)
        destination=struct.unpack_from('<4I2H',parts[b'MRGN'],20*destination_index)
        typ=fields['unit'] if fields['unit']<228 else 106
        current=next((k for k,v in enumerate(ref['owners'][:8]) if v),2)
        owner=fields['player'] if fields['player']<12 else current
        old=[(source[0]+source[2])//2,(source[1]+source[3])//2]
        target=[(destination[0]+destination[2])//2,(destination[1]+destination[3])//2]
        for blocked in (False,True):
            setup(current=current);w(0x543230,width*32,2);w(0x543234,height*32,2)
            w(0x5264B4,width,2);w(0x5264B6,height,2)
            addresses=units([dict(type=typ,status=3,owner=owner,x=old[0],y=old[1])])
            vision=initialize_vision(dat.entries[typ].sight_range,width)
            flags=0x10000|(0x00800000 if blocked else 0)
            u.mem_write(FOW,struct.pack('<I',flags)*(width*height))
            for x,y in footprint(typ,*old):w(FOW+4*(width*y+x),flags|0x8000000)
            w(0x6CA7A0,FOW+0x60000);w(0x6CA7AC,width);w(0x6CA7B0,height)
            w(0x5C9970,0);w(0x665B60,0);w(0x6C46D8,77)
            u.mem_write(0x534F40+20*source_index,struct.pack('<4I2H',*source))
            u.mem_write(0x534F40+20*destination_index,struct.pack('<4I2H',*destination))
            action(39,location=fields['location'],player=fields['player'],number=fields['number'],unit=fields['unit'],modifier=fields['modifier'])
            ex=call(ENTRY[39]);actual=snapshot(addresses)
            # All selected stock rectangles place these non-edge target boxes
            # inside map bounds; alignment is distinct from authored center.
            box=dat.entries[typ].staredit_placement_size
            aligned=[((target[0]-box.width//2)&~31)+box.width//2,
                     ((target[1]-box.height//2)&~31)+box.height//2]
            ext=dat.entries[typ].unit_extents
            final=[max(ext.left,min(aligned[0],width*32-ext.right-1)),
                   max(ext.up,min(aligned[1],height*32-ext.down-33))]
            record('campaign_grounded_eligible_Move_with_authored_DIM_MRGN',dict(reference=ref,fixture_type=typ,status=3,
                   source_location=list(source),destination_location=list(destination),dimensions=[width,height],
                   current=current,owner=owner,physical_block=blocked,null_ai_addon_path=True,old_creep_map_groups=0),
                   [actual['positions'],r(0x6C4C0C),r(0x665B60),r(0x6C46D8)],
                   [[old if blocked else final],(fields['modifier'] or 999999)-1,int(not blocked),77 if blocked else 0],
                   dict(complete_return=True,native_steps=[vision,ex]),
                   'Exact authored grounded-eligible action/DIM/MRGN; full original selected grounded path on private completed unlinked Terran building/terrain/vision state, not mission state')
            cases[-1]['native_final_state']=actual

    result=dict(target_sha256=HASH,target_version="1.17.0.1",original_code_executed=True,complete_game_executed=False,
        probe_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=dependencies,
        manifest_sha256=hashlib.sha256(mraw).hexdigest(),units_dat_sha256=hashlib.sha256(datraw).hexdigest(),
        orders_dat_sha256=hashlib.sha256(orderraw).hexdigest(),flingy_dat_sha256=hashlib.sha256(flingyraw).hexdigest(),pyms_commit=PYMS,
        runtime=dict(architecture=platform.machine(),python=sys.version,unicorn=unicorn.__version__,capstone=capstone.__version__,pefile=pefile.__version__),
        helper_stubs=[],imports_executed=[],instruction_bound=300000,reviewed_blocks={k:[hex(x) for x in v] for k,v in BLOCKS.items()},
        executed_instruction_witnesses=len(visited),cases_passed=len(cases),cases=cases,
        campaign=dict(raw_grounded_eligible_move_actions=len(refs),physical_maps=len({v["path"] for v in refs}),
                      distinct_tuples=len(tuples),supported_records=sum(v["version"] in (59,205) for v in refs),
                      ignored_records=sum(v["ignored"] for v in refs),tuples=tuples),
        limits=["Private fixtures, not played campaigns","Selected grounded Terran/Protoss completed/incomplete profiles; null addon/construction/AI/path links",
                "Original fog-unit allocator empty free-list and old-creep map group-one guards; positive Zerg creep/linked addon/construction/fog-record consumers remain dependencies"])
    a.out.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps(dict(cases_passed=len(cases),case_families=dict(Counter(x["case"] for x in cases)),
        executed_instruction_witnesses=len(visited),probe_script_sha256=result["probe_script_sha256"])))

if __name__=="__main__":main()
