#!/usr/bin/env python3
"""Isolated original Move path disposal, AI destination guards and linked-subunit consumers.
Private states are separate from stock DAT columns and authored campaign tuples.
Original code and real callees run unchanged; no stubs, imports or game process.
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
import inspect_native_image_initialization_117 as image_source

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
BLOCKS["clear_hidden_unit_references"]=image_source.BLOCKS["unit_clear_hidden_references"]
BLOCKS.update({k: counts_source.BLOCKS[k] for k in ("count_getter", "global_cache", "memmove_rep_entry", "memmove_return")})
BLOCKS["rally_producer_type"] = lifecycle_source.BLOCKS["rally_producer_type"]
BLOCKS.update({
    "path_dispose_free_list":(0x48E910,0x48E944),
    "path_timer_poll":(0x48E950,0x48E96F),
    "path_timer_set":(0x48E970,0x48E97E),
    "unit_ai_destination_guard":(0x437540,0x4376C7),
    "region_is_unwalkable":(0x481BB0,0x481BD1),
    "subunit_fixed_position_and_sprite":(0x40CC00,0x40CC33),
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
                                         ("inspect_native_unit_creation_117.py", creation_source),
                                         ("probe_native_trigger_move_117.py",move_source),
                                         ("inspect_native_image_initialization_117.py",image_source))}
    assert dependencies["probe_native_trigger_unit_actions_117.py"] == "0f970a224dfb573f813af0a590588322c31b277961dd22119e3a2d75eb02927d"
    assert dependencies["inspect_native_training_lifecycle_117.py"] == "6c17063b98c6bb730a3906c7694c191c10228b7b41a971fe15a5bb1d90bc6b61"
    assert dependencies["inspect_native_unit_creation_117.py"] == "991cd331c288f3a0351729407801eebccf11d6db0516624235382cbdc8fb758e"
    assert dependencies["probe_native_trigger_move_117.py"]=="50f3087b82de7b8b0e2fe6d0c391bc03a0b46a9144060fd6907e1e6bf86dcb2a"
    assert dependencies["inspect_native_image_initialization_117.py"]=="cb3cc883fe51a3109910928a51fd0c2a65251bf23782bec1cebb093c722342a6"
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
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_READ
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
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
        ai_reads=[]
        interesting = set(ENTRY.values()) | {0x4B6E90, 0x4B7020, 0x4261F0, 0x4B8CC0, 0x4B73A0,
                       0x4B7C70, 0x42A4D0, 0x422BA0, 0x420950, 0x407BB0, 0x407570, 0x42A340,
                       0x4B75D0, 0x4B7750, 0x4B81A0, 0x4B7D00,
                       0x424900, 0x424860, 0x425020, 0x478200, 0x428DF0, 0x426ED0,
                       0x4B7B20, 0x4B7A80, 0x4B7810, 0x430390, 0x430430, 0x419460,
                       0x419D00, 0x419D90, 0x43A270, 0x47DC30, 0x47E340, 0x4195A0,
                       0x423260, 0x42A5A0, 0x4269B0, 0x40A5A0, 0x481830, 0x481760,
                       0x466990, 0x466BD0,0x48E910,0x42C8A0,0x437540,0x43A0D0,0x40CC00,0x4B0580,0x4AB450,0x437F80}

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
                if pc==0x437540:
                    unit=machine.reg_read(UC_X86_REG_ECX);sprite=r(unit+0xC)
                    entries[-1]['native_current_sprite_position']=[r(sprite+0x14,2),r(sprite+0x16,2)]
                if pc == 0x4261F0:
                    entries[-1]["native_main_rng_state"] = r(0x67393C)
                    entries[-1]["native_auxiliary_state"] = r(0x673934)

        def read_hook(machine,access,address,size,value,unused):
            if address<0x80E040 and address+size>0x80E000:ai_reads.append([address,size])
        h = u.hook_add(UC_HOOK_CODE, hook)
        hm = u.hook_add(UC_HOOK_MEM_READ,read_hook)
        sp = STACK + 0xF000
        u.mem_write(sp, struct.pack("<" + "I" * (len(args) + 1), STOP, *args))
        u.reg_write(UC_X86_REG_ESP, sp)
        u.reg_write(UC_X86_REG_ECX, ecx)
        u.reg_write(UC_X86_REG_EDX, edx)
        u.reg_write(UC_X86_REG_EAX, 0)
        try:
            u.emu_start(entry, STOP + 1, count=300000)
        except Exception as err:
            raise RuntimeError((hex(entry),hex(u.reg_read(UC_X86_REG_EIP)),ecx,edx,args,entries[-8:])) from err
        finally:
            u.hook_del(h)
            u.hook_del(hm)
        assert done, (hex(entry), "Original return not reached")
        assert u.reg_read(UC_X86_REG_ESP) == sp + 4 + 4*len(args), (hex(entry), "Stack cleanup mismatch")
        return dict(return_eax=u.reg_read(UC_X86_REG_EAX), original_instructions=inscount,
                    complete_return=True, calls=entries,ai_record_reads=ai_reads)

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
        u.mem_write(0x534F40+20*index,struct.pack('<4I2H',*corners,0,flags))

    def path_fixture(unit,index=3,head_index=None):
        base=0x80B000;path=base+128*index
        w(0x69B574,base);w(0x69B570,0 if head_index is None else base+128*head_index)
        u.mem_write(base,bytes(128*16));w(path,0x55555555);w(path+4,0x77777777)
        w(unit+0x100,path)
        return path,base

    for index in (0,1,3,15):
        for head in (None,0,7,14):
            setup();unit=units([dict(type=15,status=0)])[0];path,base=path_fixture(unit,index,head)
            ex=call(0x42C8A0,ecx=unit)
            record('move_path_dispose_original_index_encoded_free_list',dict(path_index=index,previous_head_index=head,record_stride=128),
                [r(unit+0x100),r(0x69B570),r(path),r(path+4)],
                [0,path,0 if head is None else head+1,index+1],ex)
    for present in (False,True):
        for delay in (0,1,2,127,255):
            setup();unit=units([dict(type=15,status=0)])[0];path,base=path_fixture(unit)
            if not present:w(unit+0x100,0)
            w(path+0x19,delay,1);ex=call(0x48E950,ecx=unit)
            record('move_path_timer_native_poll',dict(path_present=present,timer=delay),
                [ex['return_eax'],r(path+0x19,1)],[int(present and delay==0),delay-1 if present and delay else delay],ex)
    for present in (False,True):
        for value in (0,1,255,256,511,MASK):
            setup();unit=units([dict(type=15,status=0)])[0];path,base=path_fixture(unit)
            if not present:w(unit+0x100,0)
            w(path+0x19,77,1);ex=call(0x48E970,ecx=unit,edx=value)
            record('move_path_timer_native_low_byte_set',dict(path_present=present,value=value),r(path+0x19,1),value&255 if present else 77,ex)
    # Hide frees the old path before placement; a rollback never resurrects it.
    for typ in (0,7,15,8):
        for complete in (0,1):
            for walkable in (False,True):
                for head in (None,7):
                    setup();status=complete|(4 if typ==8 else 0)
                    unit=units([dict(type=typ,status=status)])[0];path,base=path_fixture(unit,3,head)
                    vision=initialize_vision(dat.entries[typ].sight_range)
                    if not walkable:u.mem_write(FOW,struct.pack('<I',0xFFFF)*16384)
                    location(0,(0,0,192,192));location(1,(400,400,400,400));action(39,location=1,player=2,number=2,unit=typ,modifier=1)
                    ex=call(ENTRY[39]);actual=snapshot([unit]);accepted=walkable or typ==8
                    record('move_connected_path_freed_on_success_and_rollback',dict(type=typ,completed=complete,walkable=walkable,previous_free_head=head),
                        [actual['positions'],r(unit+0x100),r(0x69B570),r(path),r(path+4),r(0x6C4C0C)],
                        [[[400,400] if accepted else[64,64]],0,path,0 if head is None else head+1,4,0],dict(complete_return=True,native_steps=[vision,ex]))
    # Real nonnull AI gate branches that return before AI strategic helpers.
    profiles=[dict(name='already_move_to_ai',type=0,status=0,order=96,target=[400,400],ret=1),
              dict(name='air',type=8,status=5,order=3,target=[400,400],ret=0),
              dict(name='subunit',type=4,status=1,order=3,target=[400,400],ret=0),
              dict(name='same_sprite_point',type=0,status=0x20001,order=3,target=[64,64],ret=0),
              dict(name='ai_status_bit_absent',type=0,status=1,order=3,target=[400,400],ret=0),
              dict(name='worker_constructing_flying_target',type=7,status=0x20001,order=34,target=[400,400],ret=0,flying_target=True),
              dict(name='blocked_target_region',type=0,status=0x20001,order=3,target=[400,400],ret=0,blocked=True)]
    for profile in profiles:
        setup();unit=units([dict(type=profile['type'],status=profile['status'],order=profile['order'])])[0]
        w(unit+0x134,0x80E000);u.mem_write(0x80E000,b'\xA5'*64)
        if profile.get('flying_target'):w(unit+0x5C,0x80E100);w(0x80E100+0xDC,4)
        if profile.get('blocked'):w(TERRAIN+0x449FC,0x1FFD,2)
        before=bytes(u.mem_read(unit,0x150));ai=bytes(u.mem_read(0x80E000,64))
        ex=call(0x437540,ecx=unit,args=profile['target'])
        record('move_nonnull_ai_destination_gate_original_guard',profile,
            [ex['return_eax'],before==bytes(u.mem_read(unit,0x150)),ai==bytes(u.mem_read(0x80E000,64))],[profile['ret'],True,True],ex)
    # Relocation updates Sprite first; its AI destination gate sees the same point.
    for typ in (0,7,8):
        for order in (3,6,34,96):
            for ai_status in (False,True):
                for walkable in (False,True):
                    setup();status=1|(4 if typ==8 else 0)|(0x20000 if ai_status else 0)
                    unit=units([dict(type=typ,status=status,order=order)])[0]
                    w(unit+0x134,0x80E000);u.mem_write(0x80E000,b'\xA5'*64);ai=bytes(u.mem_read(0x80E000,64))
                    w(0x67CF60,0x860000);u.mem_write(0x860000,bytes(171*171))
                    if ai_status:w(unit+0x14E,1,1);w(unit+0x14F,1,1);w(unit+0x14C,0xF0,1);w(0x860000+172,1,1)
                    vision=initialize_vision(dat.entries[typ].sight_range)
                    if not walkable:u.mem_write(FOW,struct.pack('<I',0xFFFF)*16384)
                    location(0,(0,0,192,192));location(1,(400,400,400,400));action(39,location=1,player=2,number=2,unit=typ,modifier=1)
                    ex=call(ENTRY[39]);actual=snapshot([unit]);accepted=walkable or typ==8
                    strategic=[v['va'] for v in ex['calls'] if v['va'] in ('0x4b0580','0x4ab450','0x437f80')]
                    gates=[v for v in ex['calls'] if v['va']=='0x437540']
                    assert all(v['native_current_sprite_position']==v['stack'][:2] for v in gates)
                    assert ex['ai_record_reads']==[]
                    record('move_connected_nonnull_ai_after_sprite_relocation_guard',dict(type=typ,initial_order=order,walkable=walkable,can_move_status=ai_status,
                        opaque_ai_record=True,guard_calls=len(gates)),
                        [actual['positions'],r(unit+0x134),ai==bytes(u.mem_read(0x80E000,64)),strategic],
                        [[[400,400] if accepted else[64,64]],0x80E000,True,[]],dict(complete_return=True,native_steps=[vision,ex]))
    # Proper subunits have sprites/owner/global links but no sorted spatial borders.
    for typ,childtype in ((3,4),(5,6)):
        for complete in (0,1):
            for walkable in (False,True):
                for child_path in (False,True):
                    setup();parent,child=units([dict(type=typ,status=complete),dict(type=childtype,status=complete)])
                    w(parent+0x70,child);w(child+0x70,parent)
                    w(0x665B08+2*4,child);w(child+0x68,0);w(child+0x6C,parent);w(parent+0x68,child);w(parent+0x6C,0)
                    w(parent,0);w(parent+4,0);w(child,0);w(child+4,0);w(0x665AE0,parent);w(0x665AE4,parent)
                    w(r(child+0xC)+0xE,0x10,1)
                    w(0x5C99B0,2)
                    ext=dat.entries[typ].unit_extents
                    u.mem_write(0x5C99B8,struct.pack('<4I',1,64-ext.left,1,64+ext.right+1))
                    u.mem_write(0x5D03F8,struct.pack('<4I',1,64-ext.up,1,64+ext.down+1))
                    for offset,value in ((0x13C,0),(0x140,1),(0x144,0),(0x148,1)):w(parent+offset,value)
                    for offset in (0x13C,0x140,0x144,0x148):w(child+offset,0)
                    path=None
                    if child_path:path,base=path_fixture(child,3,7)
                    vision=initialize_vision(dat.entries[typ].sight_range)
                    if not walkable:u.mem_write(FOW,struct.pack('<I',0xFFFF)*16384)
                    location(0,(0,0,192,192));location(1,(400,400,400,400));action(39,location=1,player=2,number=2,unit=typ,modifier=1)
                    ex=call(ENTRY[39]);expectedpos=[400,400] if walkable else[64,64]
                    positions=[[r(v+0x28,2),r(v+0x2A,2)] for v in (parent,child)]
                    spritepos=[[r(r(v+0xC)+0x14,2),r(r(v+0xC)+0x16,2)] for v in (parent,child)]
                    record('move_connected_stock_parent_and_unindexed_subunit',dict(parent_type=typ,child_type=childtype,completed=complete,walkable=walkable,
                        child_old_path=child_path,stock_units_dat=True,private_existing_units=True),
                        [positions,spritepos,r(parent+0x70),r(child+0x70),r(0x5C99B0),r(child+0x100),r(0x6C4C0C)],
                        [[expectedpos,expectedpos],[expectedpos,expectedpos],child,parent,2,0,0],dict(complete_return=True,native_steps=[vision,ex]))
                    assert [r(0x665B08+2*4),r(child+0x6C),r(parent+0x6C)]==[child,parent,0]
                    assert [r(0x665AE0),r(0x665AE4),r(parent),r(parent+4)]==[parent,parent,0,0]
                    if child_path:
                        assert [r(0x69B570),r(path),r(path+4)]==[path,8,4]
    # Existing non-subunit links do not receive the final fixed-position copy.
    for walkable in (False,True):
        setup();parent,child=units([dict(type=3,status=1),dict(type=15,status=1,x=96,y=96)])
        w(parent+0x70,child);vision=initialize_vision(dat.entries[3].sight_range)
        if not walkable:u.mem_write(FOW,struct.pack('<I',0xFFFF)*16384)
        location(0,(0,0,192,192));location(1,(400,400,400,400));action(39,location=1,player=2,number=2,unit=3,modifier=1)
        ex=call(ENTRY[39])
        record('move_linked_non_subunit_type_guard',dict(parent_type=3,child_type=15,walkable=walkable,synthetic_non_subunit_link=True),
            [[r(parent+0x28,2),r(parent+0x2A,2)],[r(child+0x28,2),r(child+0x2A,2)],r(0x5C99B0)],
            [[400,400] if walkable else[64,64],[96,96],4],dict(complete_return=True,native_steps=[vision,ex]))
    # Fixed-point subunit relocation preserves fractions while Sprite uses arithmetic >>8.
    for typ in (4,6):
        for x,y in ((0,0),(31,32),(64,64),(399,400),(4095,4063)):
            for fracx,fracy in ((0,0),(255,255),(1,127),(127,1)):
                setup();unit=units([dict(type=typ,status=1,spatial=False)])[0]
                fx,fy=x*256+fracx,y*256+fracy;sprite=r(unit+0xC)
                ex=call(0x40CC00,ecx=unit,args=(fx,fy))
                record('move_subunit_fixed_point_original_sprite_row_relocation',dict(type=typ,fixed_target=[fx,fy],private_existing_subunit=True),
                    [r(unit+0x2C),r(unit+0x30),r(unit+0x28,2),r(unit+0x2A,2),r(sprite+0x14,2),r(sprite+0x16,2),
                     r(0x5C51D0+4*(y>>5)),r(0x5C55D0+4*(y>>5)),r(sprite),r(sprite+4)],
                    [fx,fy,x,y,x,y,sprite,sprite,0,0],ex)
    result=dict(target_sha256=HASH,target_version='1.17.0.1',original_code_executed=True,complete_game_executed=False,
        probe_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=dependencies,
        manifest_sha256=hashlib.sha256(mraw).hexdigest(),units_dat_sha256=hashlib.sha256(datraw).hexdigest(),
        orders_dat_sha256=hashlib.sha256(orderraw).hexdigest(),flingy_dat_sha256=hashlib.sha256(flingyraw).hexdigest(),
        runtime=dict(architecture=platform.machine(),python=sys.version,unicorn=unicorn.__version__,capstone=capstone.__version__,pefile=pefile.__version__),
        helper_stubs=[],imports_executed=[],instruction_bound=300000,reviewed_blocks={k:[hex(x) for x in v] for k,v in BLOCKS.items()},
        executed_instruction_witnesses=len(visited),cases_passed=len(cases),cases=cases,
        limits=['Synthetic path pools and AI records; no campaign runtime trace',
                'Generic AI destination strategic branches are unexecuted; Trigger Move sprite-first guards keep them unreachable in this local relocation call',
                'Path search and creation are not executed by disposal cases'])
    a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(cases_passed=len(cases),case_families=dict(Counter(v['case'] for v in cases)),executed_instruction_witnesses=len(visited),probe_script_sha256=result['probe_script_sha256'])))
if __name__=='__main__':main()
