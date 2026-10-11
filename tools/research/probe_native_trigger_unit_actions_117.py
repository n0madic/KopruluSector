#!/usr/bin/env python3
"""Hash-gated original trigger unit actions, alliances and pending outcomes.

Only reviewed x86 instructions run in private Unicorn memory. No stubs, imports,
Windows process, game loop or guest file/network operations. Fixtures distinguish
pending results, selection, real order issuance and rejected creation from later
unit destruction, positive allocation or mission progression.
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
BASE_HELPERS = ("current_player", "player_iterator", "finished_player", "force",
                "all_players", "allies", "foes", "neutrals", "forces", "non_allied_victory",
                "cache_invalidate", "location_visitor", "location_spatial_query",
                "sorted_spatial_bound", "spatial_release", "elevation_exclusion", "height",
                "cargo_capability", "cargo_slot", "cargo_lookup", "unit_invalid", "unit_pool_base",
                "upgrade_level")
BLOCKS = {k: counts_source.BLOCKS[k] for k in BASE_HELPERS}
BLOCKS.update(orders_source.BLOCKS)
BLOCKS.update(requirements_source.WITNESSES)
BLOCKS.update({
    "victory": (0x4B5EF0, 0x4B5F1D), "defeat": (0x4B5F20, 0x4B5F70),
    "draw": (0x4B5F70, 0x4B5FC0), "pending_getter": (0x4BD8F0, 0x4BD8F7),
    "pending_setter": (0x4BD900, 0x4BD919), "outcome_delay_one": (0x4BE1C0, 0x4BE1CC),
    "outcome_delay_45": (0x4BE3D0, 0x4BE3DC), "action_vm": (0x4BDF10, 0x4BDFC8),
    "set_alliance": (0x4B5E90, 0x4B5EC2), "alliance_mutator": (0x4B6E90, 0x4B6FA0),
    "cancel_allied_target_sweep": (0x4721C0, 0x472206), "player_unit_head": (0x4256F0, 0x4256F8),
    "cancel_nonhostile_target": (0x420950, 0x420964), "target_enemy": (0x4290E0, 0x429110),
    "player_colors_empty_selection": (0x41D7B0, 0x41D8F0), "team_mode": (0x4B2D90, 0x4B2DAB),
    "kill": (0x4B6840, 0x4B686B), "kill_at": (0x4B6870, 0x4B68B1),
    "remove": (0x4B68C0, 0x4B68EB), "remove_at": (0x4B68F0, 0x4B6931),
    "kill_selector": (0x4B8CC0, 0x4B8DC0), "kill_owner_list": (0x4B73A0, 0x4B743C),
    "kill_location": (0x4B7C70, 0x4B7CF7), "kill_location_callback": (0x4B7D00, 0x4B7E73),
    "kill_any": (0x4B8C40, 0x4B8C55), "kill_men": (0x4B8E10, 0x4B8E3A),
    "kill_buildings": (0x4B8C60, 0x4B8C8A), "kill_factories": (0x4B8C90, 0x4B8CBA),
    "kill_type": (0x4B8E40, 0x4B8E66), "unit_dead": (0x41FE40, 0x41FE55),
    "all_players_two_args": (0x4B96C0, 0x4B972B), "allies_two_args": (0x4B97A0, 0x4B9811),
    "foes_two_args": (0x4B9B80, 0x4B9BF1), "forces_two_args": (0x4B9CC0, 0x4B9D2B),
    "non_allied_two_args": (0x4B9EE0, 0x4B9F63), "neutrals_two_args": (0x4B9FE0, 0x4BA051),
    "death_order": (0x42A4D0, 0x42A529), "remove_transport": (0x425700, 0x42570C),
    "drop_powerup_zero_carry": (0x4272C0, 0x4273B5),
    "remove_hidden_unit": (0x422BA0, 0x422C17), "clear_unit_ai": (0x48DBC0, 0x48DBEB),
    "remove_military_ai_null": (0x4B0770, 0x4B0884), "remove_town_ai_null": (0x486AF0, 0x486B9C),
    "clear_town_resource_refs": (0x486000, 0x486069), "clear_guard_ai_null": (0x4B26F0, 0x4B271D),
    "clear_queen_selection": (0x483220, 0x483268),
    "move": (0x4B5840, 0x4B58F9), "order": (0x4B65A0, 0x4B6669),
    "give": (0x4B5A50, 0x4B5AFD), "create": (0x4B64D0, 0x4B6546),
    "create_selector": (0x4B7020, 0x4B712F),
    "create_frontend_guard_or_nonbuilding_allocation_failure": (0x4261F0, 0x4263B2),
    "create_unit_subunit_wrapper": (0x424900, 0x4249E0),
    "create_unit_allocator_cap_failure": (0x424860, 0x4248F5),
    "creation_error_notification": (0x425020, 0x42504C),
    "stat_text_lookup": (0x43F0C0, 0x43F0CD),
    "creation_notification_nonlocal_or_index_mismatch": (0x46AE50, 0x46AE6E),
    "main_rng": (0x478200, 0x478232),
    "neutral_container_type": (0x424420, 0x424451),
    "auxiliary_state_get": (0x4781E0, 0x4781E6), "auxiliary_state_set": (0x4781F0, 0x4781F7),
    "unit_selector_predicate": (0x4BA260, 0x4BA30E),
    "order_callback": (0x4B75D0, 0x4B773F), "hangar_child_predicate": (0x41BF90, 0x41BFA7),
    "neutral_controller": (0x4292D0, 0x4292EF),
    "give_callback": (0x4B7750, 0x4B77F9), "give_recipient_rejections": (0x4B81A0, 0x4B82BB),
    "pending_result_commit": (0x4BE020, 0x4BE188), "defeat_notification_gate": (0x4BD4A0, 0x4BD54D),
    "zero_mode_fields": (0x447DB0, 0x447DD5), "finished_player_cleanup": (0x4BD7A0, 0x4BD8E3),
    "mode_is_4": (0x447CB0, 0x447CBD), "mode_is_5": (0x447E60, 0x447E6D),
    "clear_empty_trigger_list": (0x4609C0, 0x460A14), "outcome_timer": (0x4BE340, 0x4BE394),
    "defeat_ui_already_active": (0x4BCF80, 0x4BCFE1), "victory_ui_already_active": (0x4BCFF0, 0x4BD053),
    "move_callback": (0x4B7B20, 0x4B7C56), "resource_container_unit": (0x4243E0, 0x424415),
    "flag_powerup_type": (0x425CA0, 0x425CC1),
    "static_initial_participant_mask": (0x4BE240, 0x4BE2E4),
})


def sections(data):
    pos = 0
    while pos < len(data):
        tag, size = struct.unpack_from("<4sI", data, pos)
        pos += 8
        assert pos + size <= len(data)
        yield tag, data[pos:pos + size]
        pos += size


def campaign_inputs(data, manifest):
    tuples, refs, histogram, supported_histogram = {}, [], Counter(), Counter()
    for meta in manifest["campaign_found"]:
        path = meta["path"]
        raw = (data / path.replace("\\", "/")).read_bytes()
        assert hashlib.sha256(raw).hexdigest() == meta["sha256"]
        parts = list(sections(raw))
        version = struct.unpack("<H", next(x for tag, x in parts if tag == b"VER "))[0]
        index = 0
        for tag, part in parts:
            if tag != b"TRIG":
                continue
            assert len(part) % 2400 == 0
            for off in range(0, len(part), 2400):
                owners = list(part[off + 2372:off + 2399])
                for slot in range(64):
                    a = off + 320 + 32 * slot
                    values = struct.unpack_from("<6IH4BH", part, a)
                    location, string, sound, time, player, number, unit, kind, modifier, flags, padding, internal = values
                    if kind not in ENTRY:
                        continue
                    fields = dict(location=location, player=player, number=number, unit=unit, kind=kind, modifier=modifier)
                    key = tuple(fields.values())
                    tuples.setdefault(key, {"fields": fields, "references": []})
                    ref = dict(path=path, map_sha256=meta["sha256"], version=version, trigger_index=index,
                               action_index=slot, ignored=bool(flags & 2), action_flags=flags,
                               execution_flags=struct.unpack_from("<I", part, off + 2368)[0], owners=owners)
                    tuples[key]["references"].append(ref)
                    refs.append(dict(ref, fields=fields))
                    histogram[str(kind)] += 1
                    if version in (59, 205):
                        supported_histogram[str(kind)] += 1
                index += 1
    return list(tuples.values()), refs, dict(histogram), dict(supported_histogram)


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
                                         ("inspect_native_dat_requirements_117.py", requirements_source))}
    mraw = a.manifest.read_bytes()
    manifest = json.loads(mraw)
    tuples, refs, histogram, supported = campaign_inputs(a.data, manifest)
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
    datraw = (a.data / "arr/units.dat").read_bytes()
    assert hashlib.sha256(datraw).hexdigest() == manifest["effective"]["arr\\units.dat"]["sha256"]
    dat = UnitsDAT()
    dat.load_data(datraw)
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
                (0x5211D8, unit.ground_weapon, 1), (0x5213A8, unit.air_weapon, 1)):
            w(base + typ * width, value, width)
        ext = unit.unit_extents
        u.mem_write(0x522C38 + 8 * typ, struct.pack("<4H", ext.left, ext.up, ext.right, ext.down))
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
                       0x424900, 0x424860, 0x425020, 0x478200}

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
        w(0x5C99B0, len(axes[0]))
        return [0x5DA250 + 0x150 * i for i in range(1, len(items) + 1)]

    def count_hashes():
        return [hashlib.sha256(bytes(u.mem_read(base, 2736 * 4))).hexdigest()
                for base in (0x529604, 0x52C0C4, 0x6C5150, 0x6C7C10)]

    # Complete pending-result wrappers, including guards/controller and global delay widths.
    for kind, outcome in ((1, 3), (2, 2), (56, 5)):
        for current in range(8):
            for controller in range(8):
                for pending in (0, 2, 3, 4, 5, 255):
                    for delay_mode in (0, 1):
                        setup(current)
                        w(0x6726D0 + 36 * current, controller, 1)
                        w(0x6CA758 + current, pending, 1)
                        w(0x6CA740, delay_mode, 1)
                        ex = call(ENTRY[kind])
                        blocked = kind != 1 and pending in (3, 4)
                        expected_pending = outcome if controller in (1, 2) and not blocked else pending
                        record("pending_outcome", dict(kind=kind, current=current, controller=controller, pending=pending, immediate_mode=delay_mode),
                               [ex["return_eax"], r(0x6CA758 + current, 1), r(0x6CA74C, 2), list(u.mem_read(0x5349E0, 8))],
                               [int(not blocked), expected_pending, 0x7777 if blocked else 1 if delay_mode else 45, [0] * 8], ex)
    # The original action VM observes the returned zero and does not consume the action.
    for kind in (1, 2, 56):
        for pending in (0, 3, 4):
            setup()
            node = RECORD + 0x100
            w(node + 0x948, 0)
            w(node + 0x967, 0, 1)
            w(node + 0x162, kind, 1)
            w(0x6CA758 + 2, pending, 1)
            ex = call(0x4BDF10, node)
            blocked = kind != 1 and pending in (3, 4)
            record("outcome_action_vm", dict(kind=kind, pending=pending),
                   [r(node + 0x967, 1), r(node + 0x948)], [0, 1] if blocked else [64, 9], ex)

    pending_profiles = ([0] * 8, [3] + [0] * 7, [5] + [0] * 7, [4] + [0] * 7,
                        [2] + [0] * 7, [3] * 8, [3, 3, 2, 0, 5, 4, 0, 1])
    masks = ([0] * 8, [1] + [0] * 7, [1, 1] + [0] * 6, [1] * 8)
    for pending in pending_profiles:
        for mask in masks:
            for local in (0, 2):
                for cheat in (0, 64, 128, 192, 256, 384):
                    f = setup(local, "ordinary")
                    # Computer controllers take the original empty notification
                    # branch, so no text/UI import is executed. Human retirement
                    # is checked separately through the real cleanup function.
                    for k in range(8):
                        w(0x6726D0 + 36 * k, 3, 1)
                    u.mem_write(0x6CA758, bytes(pending))
                    u.mem_write(0x6CA788, bytes(mask))
                    w(0x5432FC, cheat)
                    expected = pending[:]
                    if cheat & 128:
                        expected[local] = 3
                    elif cheat & 64:
                        expected[local] = 2
                    if cheat & 256 and expected[local] >= 2:
                        expected[local] = 0
                    delay = 0x7777
                    if any(x >= 3 for x in expected):
                        for k in range(8):
                            if expected[k] == 0:
                                expected[k] = 2
                                if k == local:
                                    delay = 45
                        if sum(bool(x) for x in mask) == sum(bool(mask[k]) and expected[k] >= 3 for k in range(8)):
                            for k in range(8):
                                if mask[k]:
                                    expected[k] = 5
                    ex = call(0x4BE020)
                    controllers = [3] * 8
                    record("pending_outcome_commit", dict(pending=pending, raw_victory_mask=mask, local=local, cheat=cheat,
                           mode_byte_672945=4, controllers=controllers, empty_unit_and_trigger_lists=True),
                           [list(u.mem_read(0x6CA758, 8)), list(u.mem_read(0x5349E0, 8)),
                            [r(0x6726D0 + 36 * k, 1) for k in range(8)], r(0x6CA74C, 2)],
                           [expected, expected, controllers, delay], ex,
                           "Complete result resolver and actual native selected-mode cleanup; no notification text, live units or allocated trigger nodes")
    for controller in range(8):
        for player in (0, 2, 7):
            for team in (0, 1):
                setup(player, "alternate" if team else "ordinary")
                for k in range(8):
                    w(0x6726D0 + 36 * k, controller, 1)
                ex = call(0x4BD7A0, player)
                expected = [controller] * 8
                for k in range(8):
                    if k == player or team and k % 4 == player % 4:
                        expected[k] = {1: 11, 2: 10}.get(controller, controller)
                record("finished_player_controller_retirement", dict(controller=controller, player=player, team_mode=team, empty_unit_lists=True),
                       [r(0x6726D0 + 36 * k, 1) for k in range(8)], expected, ex)
    for delay in (0, 1, 45, 65535):
        for finished in (0, 2, 3, 5):
            for replay in (0, 1):
                setup()
                w(0x6CA74C, delay, 2)
                w(0x5349E0 + 2, finished, 1)
                w(0x67CC90, replay)
                ex = call(0x4BE340)
                expected = delay if replay else 45 if delay == 0 else (delay - 1) & 65535
                record("outcome_postdecrement_timer", dict(delay=delay, finished=finished, replay=replay, result_ui_already_active=True),
                       r(0x6CA74C, 2), expected, ex,
                       "Complete timer and already-active original UI-return guard; no dialog display")

    # Independently simulate sequential alliance changes, rather than freezing group membership.
    def alliance_model(selector, mode, f):
        rel, current = [row[:] for row in f["rel"]], f["current"]
        value = mode if mode in (1, 2) else 0
        targets = []

        def change(k):
            if k == current:
                return
            targets.append(k)
            rel[current][k] = value
            if value and value != rel[current][current]:
                rel[current] = [value if old else 0 for old in rel[current]]

        if selector < 12:
            change(selector)
        elif selector == 17:
            for k in f["active"] + list(range(8, 12)):
                change(k)
        elif selector in (14, 15, 16, 26) or 18 <= selector <= 21:
            for k in f["active"]:
                eligible = k != current
                if selector in (14, 15, 16):
                    eligible &= rel[current][k] == {14: 0, 15: 2, 16: 1}[selector]
                elif selector == 26:
                    eligible &= not (rel[current][k] == rel[k][current] == 2)
                else:
                    eligible &= k % 4 + 1 == selector - 17
                if eligible:
                    change(k)
        return rel, targets

    for current in (0, 2, 7):
        for profile in ("ordinary", "sparse", "alternate"):
            for selector in list(range(12)) + list(range(13, 29)):
                for mode in (0, 1, 2, 3, 65535):
                    f = setup(current, profile)
                    action(57, player=selector, unit=mode)
                    expected, targets = alliance_model(selector, mode, f)
                    ex = call(ENTRY[57])
                    colors = [0 if k == current else 2 if expected[current][k] == 0 else 1 for k in range(12)]
                    record("alliance_sequential_mutation", dict(current=current, profile=profile, selector=selector, mode=mode, expected_mutated_targets=targets),
                           [list(u.mem_read(0x534914, 144)), list(u.mem_read(0x52904A, 12)), ex["return_eax"]],
                           [sum(expected, []), colors, 1], ex)
    # Real target cancellation; owner11 uses the target sprite's allegiance byte.
    for target_owner in (0, 1, 2, 7, 11):
        for relation in (0, 1, 2):
            for order_targets_enemy in (0, 1):
                setup()
                addresses = units([dict(owner=2), dict(owner=target_owner)])
                subject, target = addresses
                w(subject + 0x5C, target)
                w(0x51AE40 + 3, order_targets_enemy, 1)
                w(0x534914 + 12 * 2 + target_owner, relation, 1)
                w(SPRITES + 0x28 * 2 + 0xA, 1, 1)
                if target_owner == 11:
                    w(0x534914 + 12 * 2 + 1, relation, 1)
                ex = call(0x4721C0, 2)
                record("alliance_target_cancellation", dict(target_owner=target_owner, relation=relation, order_targets_enemy=order_targets_enemy),
                       r(subject + 0x5C), 0 if relation and order_targets_enemy else target, ex)
    for neutral_relation in (0, 1, 2):
        for sprite_relation in (0, 1, 2):
            setup()
            subject, target = units([dict(owner=2), dict(owner=11)])
            w(subject + 0x5C, target)
            w(0x51AE40 + 3, 1, 1)
            w(0x534914 + 12 * 2 + 11, neutral_relation, 1)
            w(SPRITES + 0x28 * 2 + 0xA, 1, 1)
            w(0x534914 + 12 * 2 + 1, sprite_relation, 1)
            ex = call(0x4721C0, 2)
            record("neutral_target_two_stage_allegiance", dict(owner11_relation=neutral_relation, sprite_allegiance_player=1, sprite_relation=sprite_relation),
                   r(subject + 0x5C), 0 if neutral_relation and sprite_relation else target, ex)

    for kind in (11, 44):
        for quantity in (0, 1, 2, 255):
            for owner, typ in ((2, 15), (2, 176), (11, 176)):
                setup()
                action(kind, player=owner, unit=typ, modifier=quantity)
                ex = call(ENTRY[kind])
                creation = [x for x in ex["calls"] if x["va"] == "0x4261f0"]
                count = (quantity or 1) if owner < 8 else 0
                expected_owner = 11 if typ == 176 else owner
                actual = [[x["ecx"], x["edx"], x["stack"][:2], x["native_main_rng_state"], x["native_auxiliary_state"]] for x in creation]
                record("create_count_neutral_redirect_and_auxiliary_state", dict(kind=kind, quantity=quantity, source_owner=owner, type=typ, availability_bit2_clear=True),
                       [actual, r(0x67393C), r(0x673934)], [[[typ, 64, [64, expected_owner], 0x12345678, 123456789]] * count, 0x12345678, 0x31415926], ex,
                       "Complete native wrapper/selector and real unavailable-type return; positive allocation excluded")
    setup()
    action(44, player=17, unit=176, modifier=0)
    ex = call(ENTRY[44])
    creation = [x for x in ex["calls"] if x["va"] == "0x4261f0"]
    record("create_resource_all_players_eight_neutral_attempts", dict(source_selector=17, quantity=0, type=176, availability_bit2_clear=True),
           [[x["stack"][1] for x in creation], r(0x67393C), r(0x673934)], [[11] * 8, 0x12345678, 0x31415926], ex)

    # Accept the stock nonbuilding type, then use the real allocator's exact
    # 1,700-unit cap. Allocation failure still consumes source14 simulation RNG;
    # the trigger wrapper restores only its separate auxiliary dword. Both real
    # notification guards return before UI code, without replacing any callee.
    for kind in (11, 44):
        for quantity in (0, 1, 2, 255):
            for rng_enabled in (0, 1):
                for notification in ("nonlocal_owner", "local_index_mismatch"):
                    setup(local=7 if notification == "nonlocal_owner" else 2)
                    w(0x508D48, 0x999)
                    w(0x665DE0, RECORD + 0x9000)
                    w(RECORD + 0x9000, 0, 2)  # Real TBL lookup returns original fallback string.
                    w(0x524708 + 2 * 15, 2, 2)
                    w(0x665B38, 1700)
                    w(0x673930, rng_enabled)
                    action(kind, player=2, unit=15, modifier=quantity)
                    before = count_hashes()
                    ex = call(ENTRY[kind])
                    attempts = quantity or 1
                    main = 0x12345678
                    for unused in range(attempts if rng_enabled else 0):
                        main = (main * 0x015A4E35 + 1) & MASK
                    called = Counter(x["va"] for x in ex["calls"])
                    record("create_accepted_type_real_cap_failure_rng_and_auxiliary", dict(kind=kind, quantity=quantity,
                           type=15, availability_bit2_set=True, live_unit_count=1700, rng_enabled=rng_enabled,
                           notification_fixture=notification),
                           [ex["return_eax"], r(0x67393C), r(0x673934), r(0x673938), r(0x673940 + 4 * 14),
                            r(0x665B38), r(0x665AF0), r(0x5DA398, 2), r(0x6CA6D0, 1), count_hashes(),
                            [called[hex(v)] for v in (0x4261F0, 0x424900, 0x424860, 0x478200, 0x425020)]],
                           [1, main, 0x31415926, attempts * rng_enabled, attempts * rng_enabled,
                            1700, 0, 0, 2, before, [attempts] * 5], ex,
                           "Complete original trigger/frontend/subunit-wrapper/allocator-cap/error-notification path; no unit allocation, graphics or UI imports")

    # All decoded campaign tuples invoke real complete wrappers in explicit rejecting/empty state.
    for number, item in enumerate(tuples):
        f = item["fields"]
        setup()
        action(**f)
        before = count_hashes()
        ex = call(ENTRY[f["kind"]])
        location_valid = bool(f["location"] & 255)
        invalidates = f["kind"] in (22, 24) or f["kind"] in (23, 25) and location_valid or f["kind"] == 39 and location_valid and f["number"] != 0
        record("campaign_action_tuple_empty_or_unavailable", dict(tuple_index=number, fields=f, reference_count=len(item["references"]),
                   ignored_references=sum(x["ignored"] for x in item["references"])),
               [ex["return_eax"], r(0x6CA6D0, 1), r(0x67393C), count_hashes()],
               [1, 0 if invalidates else 2, 0x12345678, before], ex,
               "All 68 physical CHKs supply parameters; no live mission state; Create uses explicit unavailable-unit guard; other unit actions have empty lists")

    # Nonempty unit selection and real Kill/Remove death-order issuance. Remove uses already hidden sprites.
    for kind in (22, 23, 24, 25):
        for selector in (0, 1, 2, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 26):
            for limit in (0, 1, 2, 255):
                f = setup()
                items = [dict(owner=k) for k in range(3) for _ in range(3)]
                addresses = units(items, hidden=kind in (24, 25))
                action(kind, player=selector, unit=15, modifier=limit)
                before = count_hashes()
                ex = call(ENTRY[kind])
                if selector < 12:
                    targets = [selector]
                elif selector == 13:
                    targets = [2]
                elif selector == 17:
                    targets = list(range(12))
                elif selector in (14, 15, 16):
                    targets = [k for k in range(8) if k != 2 and f["rel"][2][k] == {14: 0, 15: 2, 16: 1}[selector]]
                elif 18 <= selector <= 21:
                    targets = [k for k in range(8) if k % 4 + 1 == selector - 17]
                elif selector == 26:
                    targets = [k for k in range(8) if k != 2 and not (f["rel"][2][k] == f["rel"][k][2] == 2)]
                else:
                    targets = []
                maximum = 999999 if kind in (22, 24) or not limit else limit
                expected = [3] * len(items)
                for target in targets:
                    n = 0
                    for i, item in enumerate(items):
                        if item["owner"] == target and n < maximum:
                            expected[i] = 0
                            n += 1
                record("kill_remove_real_death_order", dict(kind=kind, selector=selector, limit=limit, unit_type=15,
                       owners=[x["owner"] for x in items], sprites_already_hidden=kind in (24, 25)),
                       [[r(x + 0x4D, 1) for x in addresses], r(0x6CA6D0, 1), count_hashes(), r(0x5AEF08)],
                       [expected, 0, before, 0], ex,
                       "Complete action and actual death-order/queue/AI callees; unit destruction tick and visible-to-hidden Remove cleanup excluded")

    for kind in (22, 23, 24, 25):
        for requested in (0, 15, 106, 111, 228, 229, 230, 231, 232):
            setup()
            types = [0, 15, 106, 111, 36, 59, 97, 176]
            addresses = units([dict(type=typ) for typ in types], hidden=kind in (24, 25))
            action(kind, unit=requested, modifier=0)
            before = count_hashes()
            ex = call(ENTRY[kind])
            def selected(typ):
                group = dat.entries[typ].staredit_group_flags
                return typ == requested or requested == 229 or requested == 230 and bool(group & 8) or requested == 231 and bool(group & 16) or requested == 232 and bool(group & 32)
            record("kill_remove_stock_type_groups", dict(kind=kind, requested=requested, stock_types=types, sprites_already_hidden=kind in (24, 25)),
                   [[r(x + 0x4D, 1) for x in addresses], count_hashes()],
                   [[0 if selected(typ) else 3 for typ in types], before], ex)
    for kind in (23, 25):
        for limit in (0, 1, 2):
            setup()
            addresses = units([dict(type=11), dict(type=15, spatial=False), dict(type=0, spatial=False)], hidden=kind == 25)
            u.mem_write(addresses[0] + 0xB0, struct.pack("<8H", 2, 3, 0, 0, 0, 0, 0, 0))
            action(kind, unit=229, modifier=limit)
            before = count_hashes()
            ex = call(ENTRY[kind])
            record("kill_remove_loaded_children_do_not_consume_root_limit", dict(kind=kind, limit=limit, transport_type=11, cargo_types=[15, 0], cargo_spatial=False),
                   [[r(x + 0x4D, 1) for x in addresses], count_hashes()], [[0, 0, 0], before], ex,
                   "Complete recursive location callback, actual cargo lookup and child Death order; later cargo/unit destruction excluded")

    # Original Order callback issues stock Move/Patrol/Attack orders through stock requirements and real pools.
    for typ in (0, 15):
        for selector in (2, 13, 17, 0, 22):
            for modifier, order_id in ((0, 6), (1, 152), (2, 14)):
                setup()
                addresses = units([dict(type=typ, status=0x20001)])
                u.mem_write(0x534F40 + 20, struct.pack("<4I2H", 200, 300, 400, 500, 0, 0))
                action(46, player=selector, number=2, unit=typ, modifier=modifier)
                before = count_hashes()
                ex = call(ENTRY[46])
                selected = selector in (2, 13, 17)
                record("order_stock_requirements_and_actual_queue", dict(type=typ, selector=selector, modifier=modifier, target_center=[300, 400]),
                       [r(addresses[0] + 0x4D, 1), r(addresses[0] + 0x58, 2), r(addresses[0] + 0x5A, 2),
                        r(0x6CA6D0, 1), count_hashes(), r(0x5AEF08), r(0x6BE654)],
                       [order_id if selected else 3, 300 if selected else 10, 400 if selected else 20, 2, before, 0, 0], ex)

    for profile in (dict(type=15, owner=2, order=0), dict(type=15, owner=0),
                    dict(type=14), dict(type=202), dict(type=176), dict(type=203)):
        for quantity in (0, 1, 255):
            setup()
            addresses = units([profile])
            action(39, player=2, unit=profile["type"], number=2, modifier=quantity)
            ex = call(ENTRY[39])
            record("move_nonempty_original_rejection", dict(unit=profile, quantity=quantity),
                   [r(SPRITES + 0x28 + 0x14, 2), r(SPRITES + 0x28 + 0x16, 2), r(0x6CA6D0, 1), r(0x6C4C0C)],
                   [64, 64, 0, quantity or 999999], ex,
                   "Complete original Move callback/rejection; accepted teleport and spatial-list mutation excluded")
    for typ, status, recipient, owner in ((15, 0x40000001, 0, 2), (15, 1, 13, 2),
                                          (15, 1, 2, 2), (176, 1, 0, 11)):
        for quantity in (0, 1, 255):
            setup()
            addresses = units([dict(type=typ, status=status, owner=owner)])
            action(48, player=owner, number=recipient, unit=typ, modifier=quantity)
            before = count_hashes()
            ex = call(ENTRY[48])
            record("give_nonempty_original_rejection", dict(type=typ, status=status, recipient=recipient, owner=owner, quantity=quantity),
                   [r(addresses[0] + 0x4C, 1), r(0x6CA6D0, 1), count_hashes()], [owner, 2, before], ex,
                   "Complete original Give callback/recipient rejection; accepted ownership transfer excluded")

    result = dict(target_sha256=HASH, target_version="1.17.0.1", original_code_executed=True, complete_game_executed=False,
                  probe_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), source_dependencies_sha256=dependencies,
                  manifest_sha256=hashlib.sha256(mraw).hexdigest(), units_dat_sha256=hashlib.sha256(datraw).hexdigest(),
                  orders_dat_sha256=hashlib.sha256(orderraw).hexdigest(), pyms_commit=PYMS,
                  runtime=dict(architecture=platform.machine(), python=sys.version, unicorn=unicorn.__version__,
                               capstone=capstone.__version__, pefile=pefile.__version__), helper_stubs=[], imports_executed=[],
                  instruction_bound=300000, reviewed_blocks={k: [hex(x) for x in v] for k, v in BLOCKS.items()},
                  executed_instruction_witnesses=len(visited), cases_passed=len(cases), cases=cases,
                  campaign=dict(physical_maps=68, supported_catalog_maps=64, tuples=tuples, raw_action_histogram=histogram,
                                supported_action_histogram=supported),
                  limits=["Private state fixtures, not played campaigns", "Positive Create allocation is owned by native lifecycle research",
                          "Kill proves death-order issuance, not later unit destruction/counter decrement",
                          "Remove positive cases start already hidden; visible cleanup is not inferred",
                          "Move/Give campaign tuple probes have empty unit lists; accepted movement/ownership transfer not inferred",
                          "Pending outcome is not committed campaign victory or menu/progression execution"])
    a.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(dict(cases_passed=len(cases), case_families=dict(Counter(x["case"] for x in cases)),
                         campaign_actions=sum(histogram.values()), campaign_histogram=histogram,
                         executed_instruction_witnesses=len(visited), probe_script_sha256=result["probe_script_sha256"])))


if __name__ == "__main__":
    main()
