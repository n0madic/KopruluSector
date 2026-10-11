#!/usr/bin/env python3
"""Hash-gated original trigger ownership transfer and count effects.

Only reviewed x86 instructions run in private Unicorn memory. No stubs, imports,
Windows process, game loop or guest file/network operations. Stock DAT and CHK
parameters are distinguished from private units/lists/counters, zero child/AI
links, movement-control guards and researched-tech/upgrade width fixtures.
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
BLOCKS.update({k: counts_source.BLOCKS[k] for k in (
    "count_getter", "global_cache", "memmove_rep_entry", "memmove_return")})
BLOCKS.update({k: lifecycle_source.BLOCKS[k] for k in (
    "completed_unit_add", "completed_unit_account", "medic_ai_reference",
    "guard_ai_find_or_allocate", "ui_refresh_flags", "ui_dirty_command",
    "ui_dirty_selection", "ui_dirty_portrait")})
BLOCKS.update({
    "give_transfer_wrapper": (0x428DF0, 0x428F61),
    "change_owner_core": (0x426ED0, 0x42706C),
    "unit_tech_recipe_iterator": (0x4BBCB0, 0x4BBD3C),
    "unit_upgrade_recipe_iterator": (0x4BBD40, 0x4BBDCC),
    "copy_tech_recipe": (0x429490, 0x4294CD),
    "copy_upgrade_recipe": (0x4294D0, 0x429500),
    "apply_upgrade_recipe": (0x429500, 0x42951D),
    "tech_level_get": (0x49A420, 0x49A43F),
    "upgrade_level_get": (0x49A470, 0x49A496),
    "tech_level_set": (0x49A300, 0x49A33F),
    "upgrade_level_set": (0x49A370, 0x49A3B3),
    "apply_completed_upgrade": (0x43B0B0, 0x43B1B6),
    "selection_groups_remove": (0x49C8B0, 0x49C9CF),
    "selection_remove_unit": (0x4268D0, 0x4269A5),
    "sprite_circle_remove_wrapper": (0x4230F0, 0x4230F8),
    "sprite_circle_remove": (0x416CD0, 0x416D18),
    "client_selection_remove": (0x472100, 0x47218C),
    "morph_temporarily_complete": (0x43C040, 0x43C084),
    "live_unit_add": (0x414070, 0x41407C),
    "live_unit_remove": (0x414590, 0x41459A),
    "live_unit_account": (0x414760, 0x4148A5),
    "completed_unit_remove": (0x414580, 0x41458C),
    "target_ownership_sweep": (0x41DD40, 0x41DE06),
    "global_flying_unit_head": (0x425600, 0x425606),
    "global_ground_unit_head": (0x4255E0, 0x4255E6),
    "race_group": (0x428D60, 0x428D83),
    "unit_ai_owner_changed": (0x4B1FE0, 0x4B2009),
    "worker_building_ai_owner_changed": (0x484F40, 0x484F69),
    "map_region_count": (0x481B00, 0x481B09),
    "player_unit_enemy": (0x429110, 0x429130),
    "unit_group_upgrade_effects": (0x43A8E0, 0x43AA65),
    "unit_sprite_owner_changed": (0x426E80, 0x426EC4),
    "sprite_owner_changed": (0x417160, 0x417185),
    "constructing_building_predicate": (0x435990, 0x4359BE),
    "gas_building_predicate": (0x424300, 0x42432F),
    "hangar_parent_predicate": (0x41B3A0, 0x41B3C1),
    "loaded_unit_visitor": (0x437DA0, 0x437DF5),
    "speed_from_flingy": (0x428CC0, 0x428CD8),
    "acceleration_from_flingy": (0x428C10, 0x428C61),
    "turn_from_flingy": (0x428C70, 0x428CBF),
    "speed_status_modifier": (0x4293A0, 0x429413),
    "enable_speed_upgrade": (0x43AEF0, 0x43AF3B),
    "iscript_speed_control_zero_guard": (0x42DFF0, 0x42E0CB),
})
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
                                         ("inspect_native_training_lifecycle_117.py", lifecycle_source))}
    assert dependencies["probe_native_trigger_unit_actions_117.py"] == "0f970a224dfb573f813af0a590588322c31b277961dd22119e3a2d75eb02927d"
    assert dependencies["inspect_native_training_lifecycle_117.py"] == "6c17063b98c6bb730a3906c7694c191c10228b7b41a971fe15a5bb1d90bc6b61"
    mraw = a.manifest.read_bytes()
    manifest = json.loads(mraw)
    tuples, refs, unused_histogram, unused_supported = campaign_inputs(a.data, manifest)
    tuples = [x for x in tuples if x["fields"]["kind"] == 48]
    refs = [x for x in refs if x["fields"]["kind"] == 48]
    histogram = {"48": len(refs)}
    supported = {"48": sum(x["version"] in (59, 205) for x in refs)}
    if a.static_out:
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        witnesses = {}
        for name, (lo, hi) in BLOCKS.items():
            ins = list(md.disasm(pe.get_data(lo - 0x400000, hi - lo), lo))
            assert ins and ins[-1].address + ins[-1].size == hi, (name, hex(hi))
            witnesses[name] = [{"va": hex(x.address), "rva": hex(x.address - 0x400000),
                                "bytes": x.bytes.hex(), "mnemonic": x.mnemonic, "operands": x.op_str} for x in ins]
        a.static_out.write_text(json.dumps({"target_sha256": HASH, "source_dependencies_sha256": dependencies,
            "evidence": "Static original instructions; names and dependency boundaries are analytical",
            "blocks": witnesses, "action_handlers": {str(k): hex(v) for k, v in ENTRY.items()},
            "alliance_selector_targets": [hex(v) for v in struct.unpack("<14I", pe.get_data(0xB6FA0, 56))],
            "kill_selector_targets": [hex(v) for v in struct.unpack("<14I", pe.get_data(0xB8DC0, 56))],
            "kill_group_type_targets": [hex(v) for v in struct.unpack("<4I", pe.get_data(0xB8DF8, 16))],
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
    cases, visited, approved = [], set(), set()

    def w(address, value, width=4):
        u.mem_write(address, (value & ((1 << (8 * width)) - 1)).to_bytes(width, "little"))

    def r(address, width=4):
        return int.from_bytes(u.mem_read(address, width), "little")

    for typ, unit in enumerate(dat.entries):
        for base, value, width in ((0x521660, unit.special_ability_flags, 4), (0x523C40, unit.staredit_group_flags, 1),
                (0x523FE0, unit.space_provided, 1), (0x521E78, unit.right_click_action, 1),
                (0x5211D8, unit.ground_weapon, 1), (0x5213A8, unit.air_weapon, 1), (0x521578, unit.ai_internal, 1),
                (0x520D50, unit.comp_ai_idle, 1), (0x520E38, unit.human_ai_idle, 1),
                (0x523D28, (unit.supply_provided.whole << 1) | int(unit.supply_provided.half), 1),
                (0x523E10, (unit.supply_required.whole << 1) | int(unit.supply_required.half), 1),
                (0x5240C8, unit.build_score, 2)):
            w(base + typ * width, value, width)
        w(0x51FAA8 + typ, unit.graphics, 1)
        ext = unit.unit_extents
        u.mem_write(0x522C38 + 8 * typ, struct.pack("<4H", ext.left, ext.up, ext.right, ext.down))
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
        return dict(current=current, controllers=controllers, active=active, rel=rel, mode=mode)

    def call(entry, ecx=RECORD, edx=0, args=()):
        done, inscount, entries = False, 0, []
        interesting = set(ENTRY.values()) | {0x4B6E90, 0x4B7020, 0x4261F0, 0x4B8CC0, 0x4B73A0,
                       0x4B7C70, 0x42A4D0, 0x422BA0, 0x420950, 0x407BB0, 0x407570, 0x42A340,
                       0x4B75D0, 0x4B7750, 0x4B81A0, 0x4B7D00,
                       0x424900, 0x424860, 0x425020, 0x478200, 0x428DF0, 0x426ED0,
                       0x414760, 0x4145A0, 0x4B1FE0}

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
        u.emu_start(entry, STOP + 1, count=300000)
        u.hook_del(h)
        assert done, (hex(entry), "Original return not reached")
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
            for offset in (0x18, 0x1C, 0x20):
                w(sprite + offset, image)
            w(image + 0x16, 0, 1)
            if owner in heads:
                previous = heads[owner][-1]
                w(previous + 0x6C, address)
                w(address + 0x68, previous)
            heads.setdefault(owner, []).append(address)
            ext = dat.entries[typ].unit_extents
            x, y = item.get("x", 64), item.get("y", 64)
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
            w(0x5DA250 + 0x150 * index + 4, 0x5DA250 + 0x150 * (index + 1) if index < len(items) else 0)
        if items:
            w(0x665AF8, 0x5DA3A0)
        w(0x5C99B0, len(axes[0]))
        return [0x5DA250 + 0x150 * i for i in range(1, len(items) + 1)]

    def count_hashes():
        return [hashlib.sha256(bytes(u.mem_read(base, 2736 * 4))).hexdigest()
                for base in (0x529604, 0x52C0C4, 0x6C5150, 0x6C7C10)]

    # Accepted Civilian transfer uses every real callee. Nonempty images and
    # native global/player lists remain private fixtures, not a played mission.
    for recipient in (0, 5, 11):
        setup()
        subject, other = units([dict(type=15, owner=2), dict(type=15, owner=recipient)])
        action(48, player=2, number=recipient, unit=15, modifier=1)
        ex = call(ENTRY[48])
        actual = dict(owner=r(subject + 0x4C, 1), old_head=r(0x665B08 + 4 * 2),
                      new_head=r(0x665B08 + 4 * recipient), next=r(subject + 0x6C),
                      previous=r(subject + 0x68), old_new_previous=r(other + 0x68),
                      live=[r(0x529604 + 4 * (15 * 12 + x)) for x in (2, recipient)],
                      completed=[r(0x52C0C4 + 4 * (15 * 12 + x)) for x in (2, recipient)],
                      cached=[r(0x6C5150 + 4 * (15 * 12 + x)) for x in (2, recipient)],
                      cache_valid=r(0x6CA6D0, 1), sprite_owner=r(SPRITES + 0x28 + 0xA, 1),
                      image_flags=r(IMAGES + 0x40 + 0xC, 2), order=r(subject + 0x4D, 1),
                      user_action=r(subject + 0x93, 1), ai=r(subject + 0x134),
                      ui_dirty=[r(0x6AAAFC), r(0x6AB3A0, 1), r(0x6AAB30, 1)],
                      ui_ptrs=[r(0x6AAB38), r(0x6AAB3C)], birth=r(0x531644 + 4 * (15 * 12 + recipient)))
        expected = dict(owner=recipient, old_head=0, new_head=subject, next=other,
                        previous=0, old_new_previous=subject, live=[16, 18], completed=[16, 18],
                        cached=[17, 17], cache_valid=2, sprite_owner=recipient if recipient < 8 else 2,
                        image_flags=int(recipient < 8),
                        order=dat.entries[15].human_ai_idle, user_action=0x82, ai=0,
                        ui_dirty=[1, 1, 1], ui_ptrs=[0, 0], birth=1000)
        record("give_accepted_civilian_owner_lists_counts_and_order", dict(type=15, source_owner=2, recipient=recipient,
               recipient_controller=2 if recipient < 8 else 0), actual, expected, ex)

    def expected_order(typ, controller, initial=3, status=1):
        props = dat.entries[typ].special_ability_flags
        if not status & 1 or props & 0x800 or typ in (14, 73, 85) or status & 0x40:
            return initial
        return {1: dat.entries[typ].comp_ai_idle, 3: 161, 7: 162}.get(controller, dat.entries[typ].human_ai_idle)

    scalar_bases = dict(live_men=0x5290C4, completed_men=0x5290F4, produced_men=0x529124,
                        live_buildings=0x529214, completed_buildings=0x529244, produced_buildings=0x529274,
                        live_factories=0x529364, completed_factories=0x529394, produced_factories=0x5293C4,
                        zerg_provided=0x529424, zerg_required=0x529454, terran_provided=0x5294B4,
                        terran_required=0x5294E4, protoss_provided=0x529544, protoss_required=0x529574,
                        men_score=0x5291B4, building_score=0x529304)

    def scalar_model(typ, owners, changed, complete=True):
        result = {name: [1000, 1000] for name in scalar_bases}
        if not changed or dat.entries[typ].special_ability_flags & 16:
            return result
        row = dat.entries[typ]
        group = row.staredit_group_flags
        for name, match in (("men", bool(group & 8) or typ in (36, 59, 97)),
                            ("buildings", bool(group & 16) and not group & 8),
                            ("factories", bool(group & 32))):
            if match:
                result["live_" + name] = [999, 1001]
        race = "zerg" if group & 1 else "terran" if group & 2 else "protoss" if group & 4 else None
        if race:
            required = (row.supply_required.whole << 1) | int(row.supply_required.half)
            if row.special_ability_flags & 0x400 and not complete:
                required = (required * 2) & 255
            result[race + "_required"] = [1000 - required, 1000 + required]
        if complete:
            for name, match in (("men", bool(group & 8)), ("buildings", bool(group & 16) and not group & 8),
                                ("factories", bool(group & 32))):
                if match:
                    result["completed_" + name] = [999, 1001]
            if race:
                provided = (row.supply_provided.whole << 1) | int(row.supply_provided.half)
                result[race + "_provided"] = [1000 - provided, 1000 + provided]
            if owners[1] != 11:
                if group & 8:
                    increment_total = not 44 <= typ <= 103 or pe.get_data(0x14704 + typ - 44, 1)[0] == 1
                    result["produced_men"][1] += int(increment_total)
                    result["men_score"][1] += row.build_score
                elif group & 16:
                    increment_total = not 132 <= typ <= 146 or pe.get_data(0x14748 + typ - 132, 1)[0] == 1
                    result["produced_buildings"][1] += int(increment_total)
                    result["building_score"][1] += row.build_score
        return result

    # Every stored Give parameter tuple is used with a selected stock unit type.
    # Dynamic state is a private ordinary/completed/no-linked-child fixture.
    for number, item in enumerate(tuples):
        f = item["fields"]
        recipient = 2 if f["number"] == 13 else f["number"]
        owner = f["player"] if f["player"] < 12 else 2
        if f["player"] == 17 and recipient == owner:
            owner = 1
        typ = {229: 15, 230: 15, 231: 106}.get(f["unit"], f["unit"])
        setup()
        subject, other = units([dict(type=typ, owner=owner), dict(type=typ, owner=recipient)])
        action(**f)
        ex = call(ENTRY[48])
        props = dat.entries[typ].special_ability_flags
        transferred = owner != recipient and not props & (2 | 0x2000) and typ not in (73, 85)
        old = owner
        actual = dict(owner=r(subject + 0x4C, 1), owner_heads=[r(0x665B08 + 4 * x) for x in (old, recipient)],
                      previous=r(subject + 0x68), next=r(subject + 0x6C), other_previous=r(other + 0x68),
                      live=[r(0x529604 + 4 * (typ * 12 + x)) for x in (old, recipient)],
                      completed=[r(0x52C0C4 + 4 * (typ * 12 + x)) for x in (old, recipient)],
                      completed_cached=[r(0x6C5150 + 4 * (typ * 12 + x)) for x in (old, recipient)],
                      all_cached=[r(0x6C7C10 + 4 * (typ * 12 + x)) for x in (old, recipient)],
                      cache_valid=r(0x6CA6D0, 1), sprite_owner=r(SPRITES + 0x28 + 0xA, 1),
                      image_flags=r(IMAGES + 0x40 + 0xC, 2), order=r(subject + 0x4D, 1),
                      user_action=r(subject + 0x93, 1), birth=[r(0x531644 + 4 * (typ * 12 + x)) for x in (old, recipient)],
                      transfer_calls=sum(x["va"] == "0x426ed0" for x in ex["calls"]),
                      scalars={name: [r(base + 4 * x) for x in (old, recipient)] for name, base in scalar_bases.items()})
        changes_counts = transferred and not props & 16
        expected = dict(owner=recipient if transferred else owner,
                        owner_heads=[0, subject] if transferred else [subject, subject if owner == recipient else other],
                        previous=0, next=other if transferred or owner == recipient else 0,
                        other_previous=subject if transferred or owner == recipient else 0,
                        live=[16, 18] if changes_counts else [17, 17],
                        completed=[16, 18] if changes_counts else [17, 17],
                        all_cached=[17, 17], completed_cached=[17, 17], cache_valid=2,
                        sprite_owner=recipient if transferred and recipient < 8 else owner,
                        image_flags=int(transferred and recipient < 8),
                        order=expected_order(typ, 2) if transferred else 3,
                        user_action=0x82, birth=[1000, 1000], transfer_calls=int(transferred),
                        scalars=scalar_model(typ, (old, recipient), transferred))
        record("campaign_give_tuple_stock_type_ordinary_state", dict(tuple_index=number, fields=f, fixture_type=typ,
               source_owner=owner, recipient=recipient, linked_units_absent=True, unit_movement_control=0,
               reference_count=len(item["references"])), actual, expected, ex,
               "Complete original Give call tree in private ordinary/no-linked-child state; stock Units/Flingy/Orders DAT, zero tech/upgrades; iscript speed helper takes real unit-control-zero guard")

    for controller in range(8):
        for status in (0, 1, 0x41, 0x10000001):
            setup()
            w(0x6726D0, controller, 1)
            subject, other = units([dict(type=15, owner=2, status=status), dict(type=15, owner=0)])
            action(48, player=2, number=0, unit=15, modifier=1)
            ex = call(ENTRY[48])
            complete = bool(status & 1)
            actual = dict(owner=r(subject + 0x4C, 1), order=r(subject + 0x4D, 1), status=r(subject + 0xDC),
                          user_action=r(subject + 0x93, 1), ai=r(subject + 0x134),
                          live=[r(0x529604 + 4 * (15 * 12 + x)) for x in (2, 0)],
                          completed=[r(0x52C0C4 + 4 * (15 * 12 + x)) for x in (2, 0)],
                          scalars={name: [r(base + 4 * x) for x in (2, 0)] for name, base in scalar_bases.items()})
            record("give_recipient_controller_and_completion_state", dict(type=15, controller=controller, status=status,
                   town_head=0, guard_free_head=0), actual,
                   dict(owner=0, order=expected_order(15, controller, status=status), status=status,
                        user_action=0x82, ai=0, live=[16, 18], completed=[16, 18] if complete else [17, 17],
                        scalars=scalar_model(15, (2, 0), True, complete)), ex,
                   "Complete original ordinary transfer/order/AI controller guards; empty town/guard pools and zero tech/upgrades; dynamic completion/status values are fixtures")

    for selector in (2, 13, 17):
        for quantity in (0, 1, 2, 255):
            for reject_first in (False, True):
                setup()
                items = [dict(type=15, owner=2, x=60, status=0x40000001 if reject_first else 1),
                         dict(type=15, owner=2, x=64), dict(type=15, owner=0, x=68), dict(type=15, owner=4, x=72)]
                addresses = units(items)
                action(48, player=selector, number=4, unit=15, modifier=quantity)
                ex = call(ENTRY[48])
                candidates = [i for i in range(3) if (selector == 17 or items[i]["owner"] == 2)
                              and not items[i].get("status", 1) & 0x40000000]
                moved = candidates[:quantity or 65535]
                final_owners = [4 if i in moved else items[i]["owner"] for i in range(4)]
                model_lists = {owner: [addresses[i] for i in range(4) if items[i]["owner"] == owner] for owner in (2, 0, 4)}
                for i in moved:
                    model_lists[items[i]["owner"]].remove(addresses[i])
                    model_lists[4].insert(0, addresses[i])
                expected_links = {}
                for owner, owner_list in model_lists.items():
                    for index, address in enumerate(owner_list):
                        expected_links[address] = [owner_list[index - 1] if index else 0,
                                                   owner_list[index + 1] if index + 1 < len(owner_list) else 0]
                actual = dict(owners=[r(x + 0x4C, 1) for x in addresses],
                              heads=[r(0x665B08 + 4 * x) for x in (2, 0, 4)],
                              links=[[r(x + 0x68), r(x + 0x6C)] for x in addresses],
                              transfer_order=[addresses.index(x["ecx"]) for x in ex["calls"] if x["va"] == "0x426ed0"],
                              live=[r(0x529604 + 4 * (15 * 12 + x)) for x in (2, 0, 4)],
                              completed=[r(0x52C0C4 + 4 * (15 * 12 + x)) for x in (2, 0, 4)],
                              cache_valid=r(0x6CA6D0, 1))
                count_by_owner = Counter(items[i]["owner"] for i in moved)
                expected = dict(owners=final_owners, heads=[model_lists[x][0] if model_lists[x] else 0 for x in (2, 0, 4)],
                                links=[expected_links[x] for x in addresses], transfer_order=moved,
                                live=[17 - count_by_owner[2], 17 - count_by_owner[0], 17 + len(moved)],
                                completed=[17 - count_by_owner[2], 17 - count_by_owner[0], 17 + len(moved)], cache_valid=2)
                record("give_shared_location_limit_and_list_order", dict(selector=selector, quantity=quantity,
                       first_candidate_hallucinated=reject_first, initial_owners=[2, 2, 0, 4], x=[60, 64, 68, 72]),
                       actual, expected, ex,
                       "Complete original spatial visitor/Give/list/counter paths; group quantity shared across owners; rejected candidate does not consume quantity")

    for complete in (0, 1):
        for quantity in (0, 1, 2, 255):
            setup()
            units([dict(type=15, owner=2), dict(type=15, owner=0)])
            cache_before = call(0x4B9C00)
            action(48, player=2, number=0, unit=15, modifier=quantity)
            give = call(ENTRY[48])
            cache_reused = call(0x4B9C00)
            stale = call(0x4BA680, ecx=2, edx=15, args=(complete,))
            mode_before_invalidate = r(0x6CA6D0, 1)
            invalidate = call(0x4B9820)
            cache_rebuilt = call(0x4B9C00)
            fresh = call(0x4BA680, ecx=2, edx=15, args=(complete,))
            record("give_global_cache_remains_stale_until_native_invalidation", dict(type=15, source_owner=2, recipient=0,
                   quantity=quantity, completed_family=complete, source_count_fixture=17),
                   [stale["return_eax"], fresh["return_eax"], mode_before_invalidate, r(0x6CA6D0, 1)], [17, 16, 1, 1],
                   dict(complete_return=True, native_steps=[cache_before, give, cache_reused, stale, invalidate, cache_rebuilt, fresh]),
                   "Complete original global-cache/counter/Give/reuse/invalidation/rebuild sequence; source counts are host fixture inputs")

    # Original per-unit recipes copy nonzero researched tech as boolean1 if the
    # recipient has zero, and raise upgrade levels only when the source is larger.
    # Medic recipes exercise the actual expansion setter gate. Recipe IDs come
    # from native static tables, not an application-side replacement table.
    for typ, technology, upgrade in ((0, 0, 16), (34, 34, 51)):
        for expansion in (0, 1):
            for source_tech in (0, 1, 3, 255):
                for destination_tech in (0, 1, 3, 255):
                    for source_upgrade in (0, 1, 3, 255):
                        for destination_upgrade in (0, 1, 3, 255):
                            setup()
                            w(0x536720, expansion, 1)
                            subject, other = units([dict(type=typ, owner=2), dict(type=typ, owner=0)])
                            tech_base = (lambda player: 0x534224 + 24 * player + technology) if technology < 24 else \
                                        (lambda player: 0x536408 + 20 * player + technology)
                            upgrade_base = (lambda player: 0x534590 + 46 * player + upgrade) if upgrade < 46 else \
                                           (lambda player: 0x5365DE + 15 * player + upgrade)
                            for address, value in ((tech_base(2), source_tech), (tech_base(0), destination_tech),
                                                   (upgrade_base(2), source_upgrade), (upgrade_base(0), destination_upgrade)):
                                w(address, value, 1)
                            action(48, player=2, number=0, unit=typ, modifier=1)
                            ex = call(ENTRY[48])
                            tech_writable = technology < 24 or expansion
                            upgrade_writable = upgrade < 46 or expansion
                            expected_tech = 1 if source_tech and not destination_tech and tech_writable else destination_tech
                            expected_upgrade = max(source_upgrade, destination_upgrade) if upgrade_writable else destination_upgrade
                            record("give_native_recipe_tech_upgrade_copy_and_expansion_gate", dict(type=typ, technology=technology,
                                   upgrade=upgrade, expansion=expansion, source_tech=source_tech, recipient_tech=destination_tech,
                                   source_upgrade=source_upgrade, recipient_upgrade=destination_upgrade),
                                   [r(subject + 0x4C, 1), r(tech_base(2), 1), r(tech_base(0), 1),
                                    r(upgrade_base(2), 1), r(upgrade_base(0), 1), r(subject + 0x134)],
                                   [0, source_tech, expected_tech, source_upgrade, expected_upgrade, 0], ex,
                                   "Complete original Give/recipe iterators/getters/setters/apply-guards; values3/255 are byte-width fixtures, not stock researched campaign states; nonnull AI/linked units absent")

    result = dict(target_sha256=HASH, target_version="1.17.0.1", original_code_executed=True, complete_game_executed=False,
                  probe_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), source_dependencies_sha256=dependencies,
                  manifest_sha256=hashlib.sha256(mraw).hexdigest(), units_dat_sha256=hashlib.sha256(datraw).hexdigest(),
                  orders_dat_sha256=hashlib.sha256(orderraw).hexdigest(), pyms_commit=PYMS,
                  flingy_dat_sha256=hashlib.sha256(flingyraw).hexdigest(),
                  runtime=dict(architecture=platform.machine(), python=sys.version, unicorn=unicorn.__version__,
                               capstone=capstone.__version__, pefile=pefile.__version__), helper_stubs=[], imports_executed=[],
                  instruction_bound=300000, reviewed_blocks={k: [hex(x) for x in v] for k, v in BLOCKS.items()},
                  executed_instruction_witnesses=len(visited), cases_passed=len(cases), cases=cases,
                  campaign=dict(raw_give_actions=len(refs), supported_give_actions=supported["48"],
                                physical_maps_with_give=len({x["path"] for x in refs}),
                                ignored_records=sum(x["ignored"] for x in refs), tuples=tuples),
                  limits=["Private state fixtures, not played campaigns", "Ordinary unlinked transfer of selected stock unit types",
                          "Positive linked gas/addon/cargo/hangar/AI record transfer and selected-sprite cleanup remain dependencies"])
    a.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(dict(cases_passed=len(cases), case_families=dict(Counter(x["case"] for x in cases)),
                         executed_instruction_witnesses=len(visited), probe_script_sha256=result["probe_script_sha256"])))


if __name__ == "__main__":
    main()
