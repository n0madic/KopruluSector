#!/usr/bin/env python3
"""Hash-gated original creep geometry, sustaining queries, receding pools and Move prefixes.

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
import inspect_native_creep_117 as creep_source
import probe_native_trigger_grounded_move_117 as grounded_source

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
BLOCKS.update(creep_source.BLOCKS)
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
                                         ("probe_native_trigger_ownership_117.py", ownership_source),
                                         ("inspect_native_creep_117.py", creep_source),
                                         ("probe_native_trigger_grounded_move_117.py", grounded_source))}
    assert dependencies["probe_native_trigger_unit_actions_117.py"] == "0f970a224dfb573f813af0a590588322c31b277961dd22119e3a2d75eb02927d"
    assert dependencies["inspect_native_training_lifecycle_117.py"] == "6c17063b98c6bb730a3906c7694c191c10228b7b41a971fe15a5bb1d90bc6b61"
    assert dependencies["inspect_native_unit_creation_117.py"] == "991cd331c288f3a0351729407801eebccf11d6db0516624235382cbdc8fb758e"
    assert dependencies["probe_native_trigger_move_117.py"] == "50f3087b82de7b8b0e2fe6d0c391bc03a0b46a9144060fd6907e1e6bf86dcb2a"
    assert dependencies["probe_native_placement_117.py"] == "f027748ec61c44313ef42fc00a33a402108e447bb3c4f202e5f71838f72374ea"
    assert dependencies["probe_native_trigger_ownership_117.py"] == "c3f7a5ee2372c70450b8e2d5cda15271affc2706f8a6a7c5cfbfe99b383219c3"
    assert dependencies["probe_native_trigger_grounded_move_117.py"] == "a82eb2e8b728f0268ce897022fac18b4a8ef0576052664f957501afbfbeac390"
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
    u.mem_map(0xD00000,0x40000)
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

    def call(entry, ecx=RECORD, edx=0, args=(), eax=0, prefix=False):
        done, inscount, entries, boundary = False, 0, [], None
        interesting = set(ENTRY.values()) | {0x4B6E90, 0x4B7020, 0x4261F0, 0x4B8CC0, 0x4B73A0,
                       0x4B7C70, 0x42A4D0, 0x422BA0, 0x420950, 0x407BB0, 0x407570, 0x42A340,
                       0x4B75D0, 0x4B7750, 0x4B81A0, 0x4B7D00,
                       0x424900, 0x424860, 0x425020, 0x478200, 0x428DF0, 0x426ED0,
                       0x4B7B20, 0x4B7A80, 0x4B7810, 0x430390, 0x430430, 0x419460,
                       0x419D00, 0x419D90, 0x43A270, 0x47DC30, 0x47E340, 0x4195A0,
                       0x423260, 0x42A5A0, 0x4269B0, 0x40A5A0, 0x481830, 0x481760,
                       0x466990, 0x466BD0, 0x412E20, 0x412560, 0x412360, 0x41A190, 0x419FD0,
                       0x437180, 0x431AE0, 0x437290, 0x4B46E0, 0x4C04F0, 0x4C0790,
                       0x4C0CA0,0x4C0DC0,0x4C0E60,0x4B4920,0x4D14D4}

        def hook(machine, pc, size, unused):
            nonlocal done, inscount, boundary
            if prefix and pc==0x4D7466:
                boundary=pc;machine.emu_stop();return
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
        u.reg_write(UC_X86_REG_EAX, eax)
        try:
            u.emu_start(entry, STOP + 1, count=1000000)
        finally:
            u.hook_del(h)
        if boundary is not None:
            return dict(complete_return=False,original_instructions=inscount,calls=entries,boundary_va=hex(boundary),boundary="Before first GetLastError import; no import executed")
        assert done, (hex(entry), "Original return not reached")
        assert u.reg_read(UC_X86_REG_ESP) == sp + 4 + 4*len(args), (hex(entry), "Stack cleanup mismatch")
        return dict(return_eax=u.reg_read(UC_X86_REG_EAX), original_instructions=inscount,
                    complete_return=True, complete_original_function=entry!=0x4D14D9, calls=entries)

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

    def creep_fixture(width=128,height=128,fill=0):
        setup();u.mem_write(0xD00000,bytes(0x40000))
        w(0x5264B4,width,2);w(0x5264B6,height,2)
        w(0x543230,width*32,2);w(0x543234,height*32,2)
        w(0x6CA7A0,FOW+0x60000);w(0x6CA7A4,0xD00000);w(0x6CA7A8,0xD20000)
        w(0x6CA7AC,width);w(0x6CA7B0,height)
        for address in (0x6CA7B8,0x6CA7BC,0x6CA7C0,0x6CA7C4):w(address,0)
        u.mem_write(FOW+0x60000,struct.pack("<H",fill)*(width*height))
        return call(0x4B44A0,ecx=0)

    def tile(x,y,value=0x10):
        width=r(0x6CA7AC);w(FOW+0x60000+2*(y*width+x),value,2)
        w(FOW+4*(y*width+x),0x40010000 if value&0x7FF0==0x10 else 0x10000)

    def pool_state():
        buckets=[];allnodes=set()
        for bucket in range(10):
            nodes=[];node=r(0x6C4698+bucket*4);previous=0
            while node:
                assert node not in allnodes and len(allnodes)<1024
                allnodes.add(node);assert r(node+4)==previous and r(node+0x12,1)==bucket
                nodes.append(dict(address=node,x=r(node+0x10,1),y=r(node+0x11,1),bucket=bucket));previous=node;node=r(node)
            assert len(nodes)==r(0x6C46C0+2*bucket,2),(bucket,len(nodes),r(0x6C46C0+2*bucket,2),hex(r(0x6C4698+bucket*4)))
            buckets.append(nodes)
        assert len(allnodes)==1024
        hashed=set()
        for key in range(1024):
            node=r(0x6C3698+4*key);previous=0
            while node:
                assert node not in hashed and node in allnodes and r(node+0x12,1)<9 and r(node+0xC)==previous
                hashed.add(node)
                assert (r(node+0x10,1)+17*r(node+0x11,1))&1023==key
                previous=node;node=r(node+8)
        assert hashed=={v["address"] for b in buckets[:9] for v in b}
        return dict(counts=[len(b) for b in buckets],active=[[v['x'],v['y'],v['bucket']] for b in buckets[:9] for v in b],free_head=r(0x6C46BC))

    ex=creep_fixture();state=pool_state()
    record("creep_life_native_initialize",dict(native_node_pool=1024),[state['counts'],state['free_head']],[([0]*9)+[1024],0x6C3684],ex)

    # Signed, wrapping integer ellipse predicate, with boundary and overflow vectors.
    def s32(v):v&=MASK;return v-0x100000000 if v&0x80000000 else v
    vectors={(x,y) for x in range(-336,337,16) for y in range(-208,209,16)}
    vectors.update((x,y) for x,y in ((320,0),(321,0),(-320,0),(-321,0),(0,200),(0,201),(0,-200),(0,-201),(2147483647,0),(-2147483648,0),(0,65536),(65536,0)))
    for x,y in sorted(vectors):
        ex=call(0x4C0A80,ecx=x,edx=y)
        record("creep_ellipse_signed_integer",dict(dx=x,dy=y),ex['return_eax'],int(s32(x*x*100+y*y*256)>10240000),ex)
    # All type dispatch inputs establish provider completion versus constant return.
    always={132,133,144,146};conditional={131,143}
    for typ in range(228):
        for complete in (0,1,2,MASK):
            ex=call(0x4C0790,ecx=typ,edx=complete)
            record("creep_provider_native_stock_id_dispatch",dict(type=typ,completion_argument=complete),ex['return_eax'],1 if typ in always else complete if typ in conditional else 0,ex)
    offsets=[(-1,-1),(0,-1),(1,-1),(-1,0),(1,0),(-1,1),(0,1),(1,1)]
    for x,y in ((2,2),(0,0),(0,2),(4,2),(4,4)):
        for mask in range(256):
            exinit=creep_fixture(5,5)
            for bit,(dx,dy) in enumerate(offsets):
                if mask&(1<<bit) and 0<=x+dx<5 and 0<=y+dy<5:tile(x+dx,y+dy,0x801D)
            tile(x,y,0x20)
            ex=call(0x4C0060,ecx=x,edx=y)
            expected=sum(bool(mask&(1<<bit)) and 0<=x+dx<5 and 0<=y+dy<5 for bit,(dx,dy) in enumerate(offsets))
            record("creep_all_neighbor_patterns_and_map_edges",dict(center=[x,y],width=5,height=5,mask=mask,creep_word=0x801D),ex['return_eax'],expected,dict(complete_return=True,native_steps=[exinit,ex]))
    def trunc_div(value, divisor):return value//divisor if value>=0 else -((-value)//divisor)
    def creep_bounds_model(typ,x,y,complete,width=128,height=128):
        large=typ in (132,133,144,146) or typ in (131,143) and bool(complete)
        if large:left,right=trunc_div(x-320,32),trunc_div(x+320,32);top,bottom=trunc_div(y-200,32),trunc_div(y+200,32)
        else:
            box=dat.entries[typ].staredit_placement_size;tw,th=trunc_div(box.width,32),trunc_div(box.height,32)
            left=trunc_div(x,32)-trunc_div(tw,2);top=trunc_div(y,32)-trunc_div(th,2)
            right=left+tw-1;bottom=top+th-1
        if left<0:left=0
        elif right>=width:right=width-1
        if top<0:top=0
        elif bottom>=height:bottom=height-1
        return [left,top,right,bottom],int(large)

    types=(0,41,106,131,132,133,135,143,144,146,147,148,149,150,151,152,154,176,188)
    for typ in types:
        for complete in (0,1):
            for x,y in ((400,416),(48,48),(4032,4022)):
                init=creep_fixture();out=0xD30000
                ex=call(0x4C04F0,ecx=typ,edx=x,args=(y,out,complete))
                wanted,large=creep_bounds_model(typ,x,y,complete)
                record("creep_bounds_stock_classes_and_clipped_edges",dict(type=typ,center=[x,y],completed=complete),
                    [list(struct.unpack('<4i',u.mem_read(out,16))),ex['return_eax']],[wanted,large],dict(complete_return=True,native_steps=[init,ex]))
    for typ in types:
        for complete in (0,1):
            for tx,ty in ((12,13),(20,13),(12,19),(2,13),(22,13),(127,127)):
                init=creep_fixture();x,y=400,416;bounds,unused=creep_bounds_model(typ,x,y,complete)
                valid=bool(dat.entries[typ].special_ability_flags&1 and dat.entries[typ].staredit_group_flags&1)
                valid=valid and bounds[0]<=tx<=bounds[2] and bounds[1]<=ty<=bounds[3]
                valid=valid and s32((tx*32+16-x)**2*100+(ty*32+16-y)**2*256)<=10240000
                ex=call(0x4C0830,ecx=tx,edx=ty,args=(typ,x,y,complete))
                record("creep_sustaining_unit_stock_gate_and_geometry",dict(type=typ,center=[x,y],tile=[tx,ty],completed=complete),
                    ex['return_eax'],int(valid),dict(complete_return=True,native_steps=[init,ex]))
    for typ in (0,41,106,131,132,133,135,143,144,146,149,154):
        for status in (0,1,2,3,5):
            for owner in (2,11):
                for tx,ty in ((12,13),(20,13)):
                    init=creep_fixture();addresses=units([dict(type=typ,status=status,owner=owner,x=400,y=416)])
                    bounds,unused=creep_bounds_model(typ,400,416,status&1)
                    valid=bool(status&2 and dat.entries[typ].special_ability_flags&1 and dat.entries[typ].staredit_group_flags&1)
                    valid=valid and bounds[0]<=tx<=bounds[2] and bounds[1]<=ty<=bounds[3]
                    valid=valid and s32((tx*32+16-400)**2*100+(ty*32+16-416)**2*256)<=10240000
                    ex=call(0x4C0250,ecx=tx,edx=ty,args=(0x4B4170,))
                    record("creep_real_spatial_provider_query_owner_independence",dict(type=typ,status=status,owner=owner,tile=[tx,ty]),
                        [ex['return_eax'],r(0x5D6E48),r(0x5D6E5C)],[int(valid),0,0],dict(complete_return=True,native_steps=[init,ex]))

    # Allocator collision, cancellation, no-node guard, and finite capacity.
    exinit=creep_fixture();allocations=[]
    for x,y in ((10,10),(27,9),(44,8)):
        tile(x,y);allocations.append(call(0x4B3F30,ecx=x,edx=y))
    state=pool_state()
    record("creep_life_hash_collision_allocate",dict(tiles=[[10,10],[27,9],[44,8]],hash_key=180),[state['counts'],sorted(state['active'])],[[3]+[0]*8+[1021],[[10,10,0],[27,9,0],[44,8,0]]],dict(complete_return=True,native_steps=[exinit]+allocations))
    for removed,(x,y) in enumerate(((27,9),(44,8),(10,10))):
        ex=call(0x4B4930,ecx=x,edx=y);state=pool_state()
        record("creep_life_hash_collision_cancel",dict(tile=[x,y]),[ex['return_eax'],sum(state['counts'][:9]),r(FOW+4*(y*128+x))&0x10000000],[1,2-removed,0],ex)
    ex=call(0x4B4930,ecx=10,edx=10)
    record("creep_life_cancel_absent",dict(tile=[10,10]),ex['return_eax'],0,ex)
    exinit=creep_fixture();calls=[]
    for index in range(1024):tile(index%128,index//128)
    for index in range(1024):
        x,y=index%128,index//128;calls.append(call(0x4B3F30,ecx=x,edx=y))
    state=pool_state();before=bytes(u.mem_read(0x6BE698,0x603C));ex=call(0x4B3F30,ecx=127,edx=127)
    assert before==bytes(u.mem_read(0x6BE698,0x603C))
    record("creep_life_complete_finite_capacity_exhaustion",dict(allocated_tiles=1024,extra_tile=[127,127]),[ex['return_eax'],state['counts'][9],sum(state['counts'][:9]),r(FOW+4*(127*128+127))&0x10000000],[0,0,1024,0],dict(complete_return=True,native_steps=[exinit]+calls+[ex]))
    # Positive old-provider removal, including an actual remaining provider query.
    for typ,complete in ((131,0),(131,1),(132,0),(133,1),(143,1),(144,0),(146,1),(135,1),(149,1)):
        for remaining in (False,True):
            for occupied in (False,True):
                for preexisting in (False,True):
                    init=creep_fixture();x,y=1600,1600;box,large=creep_bounds_model(typ,x,y,complete)
                    seedtiles={(tx,ty) for ty in range(box[1],box[3]+1) for tx in range(box[0],box[2]+1)
                        if not large or (tx*32+16-x)**2*100+(ty*32+16-y)**2*256<=10240000}
                    for tx,ty in seedtiles:
                        tile(tx,ty,0x17)
                        if occupied and tx%3==0:w(FOW+4*(ty*128+tx),r(FOW+4*(ty*128+tx))|0x8000000)
                    if remaining:units([dict(type=131,status=3,owner=11,x=x+128,y=y)])
                    allocations=[];existing=set()
                    if preexisting:
                        existing={v for v in seedtiles if v[0]%2==0 and v[1]%2==0}
                        for tx,ty in sorted(existing):allocations.append(call(0x4B3F30,ecx=tx,edx=ty))
                    wanted=set(existing)
                    for tx,ty in seedtiles:
                        if (tx,ty) in existing or occupied and tx%3==0 and typ!=149:continue
                        if remaining:
                            rb,unused=creep_bounds_model(131,x+128,y,1)
                            if rb[0]<=tx<=rb[2] and rb[1]<=ty<=rb[3] and (tx*32+16-x-128)**2*100+(ty*32+16-y)**2*256<=10240000:continue
                        wanted.add((tx,ty))
                    w(0x6C46D8,77)
                    ex=call(0x4B46E0,ecx=typ,edx=x,args=(y,complete));state=pool_state()
                    actualtiles={tuple(v[:2]) for v in state['active']}
                    for tx,ty,neighbors in state['active']:
                        assert neighbors==sum((tx+dx,ty+dy) in seedtiles for dx,dy in offsets)
                    record("creep_remove_provider_positive_nodes_and_live_provider",dict(type=typ,complete=complete,old_center=[x,y],
                        remaining_provider=dict(type=131,owner=11,center=[x+128,y]) if remaining else None,
                        occupied_stripes=occupied,preexisting_receding=preexisting,stock_units_dat=True),
                        [sorted(actualtiles),state['counts'][9],r(0x6C46D8),ex['return_eax'],r(0x5D6E48),r(0x5D6E5C)],
                        [sorted(wanted),1024-len(wanted),0,1,0,0],dict(complete_return=True,native_steps=[init]+allocations+[ex]))
    # Positive restoration removes only receding nodes inside this unit's native area.
    for typ in (131,132,133,135,143,144,146,149,154):
        for complete in (0,1):
            for x,y in ((1600,1600),(48,48)):
                init=creep_fixture();bounds,large=creep_bounds_model(typ,x,y,complete)
                seeded={(tx,ty) for ty in range(bounds[1],bounds[3]+1) for tx in range(bounds[0],bounds[2]+1)}
                seeded.update(((100,100),(101,100),(100,101)))
                calls=[]
                for tx,ty in sorted(seeded):tile(tx,ty,0x17)
                for tx,ty in sorted(seeded):calls.append(call(0x4B3F30,ecx=tx,edx=ty))
                address=units([dict(type=typ,status=complete,owner=11,x=x,y=y)])[0]
                removed={(tx,ty) for tx,ty in seeded if bounds[0]<=tx<=bounds[2] and bounds[1]<=ty<=bounds[3]
                    and (not large or (tx*32+16-x)**2*100+(ty*32+16-y)**2*256<=10240000)}
                ex=call(0x4B4DD0,ecx=address);state=pool_state()
                record('creep_restore_provider_positive_area_cancellation',dict(type=typ,completed=complete,center=[x,y],stock_units_dat=True,owner=11),
                    [sorted(tuple(v[:2]) for v in state['active']),state['counts'][9],sum(bool(r(FOW+4*(ty*128+tx))&0x10000000) for tx,ty in removed)],
                    [sorted(seeded-removed),1024-len(seeded-removed),0],dict(complete_return=True,native_steps=[init]+calls+[ex]))
    # Old-provider removal stops immediately when the genuine finite pool is full.
    init=creep_fixture();calls=[]
    for index in range(1024):tile(index%128,index//128)
    for index in range(1024):calls.append(call(0x4B3F30,ecx=index%128,edx=index//128))
    tile(50,50,0x17);w(0x6C46D8,77);before=bytes(u.mem_read(0x6BE698,0x603C))
    ex=call(0x4B46E0,ecx=143,edx=1600,args=(1600,0));state=pool_state()
    record('creep_remove_provider_finite_pool_failure_preserves_timer',dict(pool_free=0,type=143,completed=0,old_center=[1600,1600]),
        [ex['return_eax'],r(0x6C46D8),before==bytes(u.mem_read(0x6BE698,0x603C)),state['counts'][9]],[0,77,True,0],dict(complete_return=True,native_steps=[init]+calls+[ex]))
    # Both border tables are produced by the unchanged original initializers.
    def transformed(mask):
        if mask&0x100:return 127
        value=0
        for bit,flag in ((0x10,9),(8,0x24),(0x40,2),(2,0x10)):
            if mask&bit:value|=flag
        for pair,flag in ((0xC0,1),(0x60,4),(6,8),(3,0x20)):
            if mask&pair==pair:value|=flag
        if value&0x21==0x21 or value&0xC==0xC:value|=0x40
        return value
    lookup=bytes(transformed(i) for i in range(512));represented=sorted(set(lookup))
    graphics=bytes(represented.index(i) if i in represented else 0 for i in range(128))
    init=creep_fixture();first=call(0x4BFF60);second=call(0x4C0010)
    record('creep_complete_original_border_table_initializers',dict(neighbor_patterns=512,graphic_ranks=128),
        [list(u.mem_read(0x6CA7C8,512)),list(u.mem_read(0x6CA9C8,128))],[list(lookup),list(graphics)],
        dict(complete_return=True,native_steps=[init,first,second]))
    native_offsets=[(1,1),(0,1),(-1,1),(1,0),(-1,0),(1,-1),(0,-1),(-1,-1)]
    profiles=[(2,2,m) for m in range(256)]+[(x,y,m) for x,y in ((0,0),(0,2),(4,2),(4,4)) for m in (0,15,165,255)]
    for x,y,mask in profiles:
        init=creep_fixture(5,5)
        assert bytes(u.mem_read(0x6CA7C8,512))==lookup and bytes(u.mem_read(0x6CA9C8,128))==graphics
        realmask=0
        for bit,(dx,dy) in enumerate(native_offsets):
            if mask&(1<<bit) and 0<=x+dx<5 and 0<=y+dy<5:tile(x+dx,y+dy,0x801D);realmask|=1<<bit
        tile(x,y,0x20);w(0xD20000+y*5+x,77,1)
        ex=call(0x4C0470,args=(0,0,x,y,0))
        record('creep_border_native_neighbor_mask_and_two_stage_lookup',dict(center=[x,y],neighbor_mask=mask,clipped_mask=realmask,
            original_initialized_lookup=True,callbacks_null=True),
            [r(0xD20000+y*5+x,1),r(FOW+0x60000+2*(y*5+x),2),r(0xD00000+2*(y*5+x),2),ex['return_eax']],
            [graphics[lookup[realmask]],0x20,0,1],dict(complete_return=True,native_steps=[init,ex]))
    # Native spreading ranks candidates by neighboring creep and consumes source26 before CRT.
    for typ in (131,132,143,135):
        for seed in (0,12345,MASK):
            for enabled in (0,1):
                init=creep_fixture();x,y=1600,1600;bounds,large=creep_bounds_model(typ,x,y,1)
                seedtiles={(50,50),(51,50),(50,51),(49,49)}
                for tx,ty in seedtiles:tile(tx,ty,0x17)
                wanted=[[] for unused in range(8)]
                for ty in range(bounds[1],bounds[3]+1):
                    for tx in range(bounds[0],bounds[2]+1):
                        if (tx,ty) in seedtiles:continue
                        n=sum((tx+dx,ty+dy) in seedtiles for dx,dy in offsets)
                        if not n or large and (tx*32+16-x)**2*100+(ty*32+16-y)**2*256>10240000:continue
                        wanted[n-1].append((tx,ty))
                assert any(wanted)
                rank=max(i for i,row in enumerate(wanted) if row)
                new=(seed*0x15A4E35+1)&MASK if enabled else seed
                rand=((new>>16)&0x7FFF) if enabled else 0
                selected=wanted[rank][(rand>>4)%len(wanted[rank])]
                w(0x673930,enabled);w(0x67393C,seed);w(0x673938,0);u.mem_write(0x673940,bytes(50*4))
                ex=call(0x4C0E60,ecx=typ,edx=x,args=(y,),prefix=True);assert not ex['complete_return']
                updates=[v for v in ex['calls'] if v['va']=='0x4c0ca0'];assert len(updates)==1
                actual=[]
                for rankidx,row in enumerate(wanted):
                    actual.append([list(struct.unpack('<2I',u.mem_read(0x6CAA50+8*(rankidx*240+j),8))) for j in range(len(row))])
                record('creep_spread_ranked_candidates_and_rng26_original_prefix',dict(type=typ,center=[x,y],seed=seed,rng_enabled=enabled,seed_creep=sorted(seedtiles)),
                    [actual,[updates[0]['ecx'],updates[0]['edx']],r(0x67393C),r(0x673940+26*4),r(0x673938),
                     r(0xD00000+2*(selected[1]*128+selected[0]),2),ex['boundary_va']],
                    [[[list(v) for v in row] for row in wanted],list(selected),new,enabled,enabled,0,'0x4d7466'],ex,
                    'Original candidate collector, ranking, source26 RNG and map-update prefix; no variant write, full function return or imports')

    # Connected grounded Move stops before its first actual Windows import.
    for typ in (131,132,133,135,143,144,146,149):
        init=creep_fixture();box=dat.entries[typ].staredit_placement_size
        oldx,oldy=1600+box.width//2,1600+box.height//2
        bounds,large=creep_bounds_model(typ,oldx,oldy,1)
        seeded={(tx,ty) for ty in range(bounds[1],bounds[3]+1) for tx in range(bounds[0],bounds[2]+1)
            if not large or (tx*32+16-oldx)**2*100+(ty*32+16-oldy)**2*256<=10240000}
        for tx,ty in seeded:tile(tx,ty,0x17)
        addresses=units([dict(type=typ,status=3,owner=2,x=oldx,y=oldy)]);unit=addresses[0]
        for ty in range((oldy-box.height//2)//32,(oldy+box.height//2)//32):
            for tx in range((oldx-box.width//2)//32,(oldx+box.width//2)//32):w(FOW+4*(ty*128+tx),r(FOW+4*(ty*128+tx))|0x8000000)
        w(0x5C9970,0);w(0x665B60,0);w(0x6C46D8,77)
        w(0x673930,1);w(0x67393C,12345);w(0x673938,0);u.mem_write(0x673940,bytes(50*4))
        vision=initialize_vision(dat.entries[typ].sight_range)
        u.mem_write(0x534F40,struct.pack('<4I2H',0,0,2500,2500,0,0))
        u.mem_write(0x534F40+20,struct.pack('<4I2H',3000,3000,3000,3000,0,0))
        action(39,location=1,player=2,number=2,unit=typ,modifier=1)
        ex=call(ENTRY[39],prefix=True)
        if typ==149:
            assert ex['complete_return']
            state=pool_state();spatial=snapshot(addresses)
            record('grounded_zerg_extractor_move_resource_guard',dict(type=149,stock_units_dat=True,resource_container_flag=0x2000),
                [spatial['positions'],state['counts'][9],r(0x6C4C0C),r(0x6C46D8)],[[[oldx,oldy]],1024,1,77],dict(complete_return=True,native_steps=[init,vision,ex]))
            continue
        assert not ex['complete_return'],(typ,ex,snapshot(addresses))
        state=pool_state();spatial=snapshot(addresses)
        newx=((3000-box.width//2)&~31)+box.width//2;newy=((3000-box.height//2)&~31)+box.height//2
        record("grounded_zerg_move_original_prefix_before_crt_context_import",dict(type=typ,old_center=[oldx,oldy],target=[3000,3000],
            seeded_old_creep_tiles=len(seeded),terrain="private flat walkable",stock_units_dat=True,null_ai=True,null_path=True,null_child=True),
            [spatial['positions'],sorted(tuple(v[:2]) for v in state['active']),state['counts'][9],r(0x6C46D8),r(0x6C4C0C),
             r(0x67393C),r(0x673940+26*4),ex['boundary_va']],
            [[[newx,newy]],sorted(seeded),1024-len(seeded),0,1,12345,0,'0x4d7466'],
            dict(complete_return=False,native_steps=[init,vision,ex],boundary_va=ex['boundary_va']),
            "Original Move prefix with positive old-creep removal and relocation; stops before GetLastError import, no complete action return or new-creep stamping claim")

    # Exact CRT arithmetic tail: context acquisition is deliberately not executed.
    for seed in (0,1,12345,MASK,0x80000000,0x12345678):
        context=0xD30000;w(context+0x14,seed)
        ex=call(0x4D14D9,ecx=0,eax=context);new=(seed*214013+2531011)&MASK
        record("crt_rand_original_arithmetic_tail",dict(seed=seed,context_pointer=context,entry="after original thread-context call"),[r(context+0x14),ex['return_eax']],[new,(new>>16)&32767],ex,
            "Complete arithmetic suffix returning to caller; full rand wrapper/thread context and imports are excluded")
    result=dict(target_sha256=HASH,target_version="1.17.0.1",original_code_executed=True,complete_game_executed=False,
        probe_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=dependencies,
        retail_manifest_sha256=hashlib.sha256(mraw).hexdigest(),units_dat_sha256=hashlib.sha256(datraw).hexdigest(),
        orders_dat_sha256=hashlib.sha256(orderraw).hexdigest(),flingy_dat_sha256=hashlib.sha256(flingyraw).hexdigest(),
        runtime=dict(architecture=platform.machine(),python=sys.version,unicorn=unicorn.__version__,capstone=capstone.__version__,pefile=pefile.__version__),
        helper_stubs=[],imports_executed=[],instruction_bound=1000000,
        complete_original_function_cases=sum(v["execution"]["complete_return"] and v["case"]!="crt_rand_original_arithmetic_tail" for v in cases),
        pure_original_suffix_cases=6,prefix_cases=sum(not v["execution"]["complete_return"] for v in cases),executed_instruction_witnesses=len(visited),cases_passed=len(cases),cases=cases,
        limits=["Private creep map/pool and unit/list fixtures; no campaign trace or pixels",
                "CRT rand arithmetic suffix is not its complete context/import wrapper",
                "New-provider full tile stamping and imported context acquisition remain explicit dependencies"])
    a.out.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps(dict(cases_passed=len(cases),case_families=dict(Counter(v['case'] for v in cases)),executed_instruction_witnesses=len(visited),probe_script_sha256=result['probe_script_sha256'])))

if __name__=="__main__":main()
