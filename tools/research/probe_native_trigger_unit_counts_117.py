#!/usr/bin/env python3
"""Probe complete original Command/Bring counting and caching, without stubs.

The guest executes only reviewed PE instructions in private memory. UNIT,
terrain and spatial tables are fixtures; retail CHKs supply decoded parameters,
not live campaign state. Original allocation, imports and full game are excluded.
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

HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
PYMS = "bfc5d3aad0b5614a5aff72c223f8efa00afddfa4"
RECORD, SPRITES, TERRAIN, CV5, VF4 = 0x800000, 0x801000, 0x810000, 0x850000, 0x860000
STACK, STOP, MASK = 0x900000, 0xA00000, 0xFFFFFFFF
BLOCKS = {
    "command": (0x4B8ED0, 0x4B8F1F), "bring": (0x4B8F20, 0x4B8F7B),
    "most_command": (0x4B9070, 0x4B90D5), "most_at": (0x4B90E0, 0x4B9154),
    "least_command": (0x4B93B0, 0x4B941E), "least_at": (0x4B9420, 0x4B9496),
    "count_getter": (0x4BA680, 0x4BA7D0),
    "global_cache": (0x4B9C00, 0x4B9C51), "location_cache": (0x4BA350, 0x4BA408),
    "cache_invalidate": (0x4B9820, 0x4B9828),
    "location_visitor": (0x443110, 0x44317F), "count_callback": (0x4BA830, 0x4BA872),
    "elevation_exclusion": (0x4B95F0, 0x4B965B), "height": (0x445690, 0x44571D),
    "all_counter_increment": (0x4B9970, 0x4B99D3),
    "completed_counter_increment": (0x4B9830, 0x4B9968),
    "location_spatial_query": (0x4224C0, 0x422756),
    "sorted_spatial_bound": (0x419500, 0x419555), "spatial_release": (0x4222B0, 0x4222C8),
    "unit_pool_base": (0x4255F0, 0x4255F6), "cargo_capability": (0x4381F0, 0x43822D),
    "cargo_slot": (0x4381D0, 0x4381E7), "cargo_lookup": (0x428B50, 0x428B9C),
    "unit_invalid": (0x41FE20, 0x41FE3B), "upgrade_level": (0x49A470, 0x49A496),
    "hangar_child_type": (0x41CC20, 0x41CC47),
    "player_iterator": (0x4B3160, 0x4B322F), "force": (0x443560, 0x443573),
    "finished_player": (0x4BD590, 0x4BD5C9), "current_player": (0x4BDA70, 0x4BDA76),
    "all_players": (0x4B9660, 0x4B96BE), "allies": (0x4B9730, 0x4B9791),
    "foes": (0x4B9B10, 0x4B9B71), "forces": (0x4B9C60, 0x4B9CBB),
    "non_allied_victory": (0x4B9E60, 0x4B9ED3), "neutrals": (0x4B9F70, 0x4B9FD1),
    "memmove_rep_entry": (0x4D16A0, 0x4D16D1), "memmove_return": (0x4D19E8, 0x4D19EF),
    "memset_rep_entry": (0x4D0C60, 0x4D0C88), "memset_rep_return": (0x4D0CE5, 0x4D0CF0),
}
GROUP = {"all_men": 0x6C4C10, "complete_men": 0x6C4C40,
         "all_buildings": 0x6C4D60, "complete_buildings": 0x6C4D90,
         "all_factories": 0x6C4EB0, "complete_factories": 0x6C4EE0}
COUNTERS = {"complete": 0x6C5150, "all": 0x6C7C10, **GROUP}
ENTRY = {2: 0x4B8ED0, 3: 0x4B8F20, 6: 0x4B9070, 7: 0x4B90E0,
         16: 0x4B93B0, 17: 0x4B9420}
SELECTORS = list(range(12)) + list(range(13, 27))
TYPES = (0, 227, 229, 230, 231, 232)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("exe", "data", "manifest", "pyms"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--out", type=Path)
    p.add_argument("--static-out", type=Path)
    p.add_argument("--execute-original-code", action="store_true")
    a = p.parse_args()
    raw = a.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != HASH:
        p.error("Unreviewed executable hash")
    pe = pefile.PE(data=raw)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    if a.static_out:
        witnesses = {}
        for name, (lo, hi) in BLOCKS.items():
            instructions = list(md.disasm(pe.get_data(lo - 0x400000, hi - lo), lo))
            assert instructions and instructions[-1].address + instructions[-1].size == hi, (name, hex(hi))
            witnesses[name] = [{"va": hex(i.address), "rva": hex(i.address - 0x400000),
                                "bytes": i.bytes.hex(), "mnemonic": i.mnemonic, "operands": i.op_str}
                               for i in instructions]
        a.static_out.parent.mkdir(parents=True, exist_ok=True)
        a.static_out.write_text(json.dumps({"target_sha256": HASH, "evidence": "static instruction inventory; labels analytical",
                                            "blocks": witnesses, "counter_bases": {k: hex(v) for k, v in COUNTERS.items()}}, indent=2) + "\n")
    if not a.execute_original_code:
        print(json.dumps({"target_sha256": HASH, "reviewed_blocks": list(BLOCKS), "stubs": [],
                          "limits": "Synthetic units/counters and campaign parameters; no original unit creation"}, indent=2))
        return
    if not a.out:
        p.error("--out required for execution evidence")
    assert subprocess.check_output(["git", "-C", str(a.pyms), "rev-parse", "HEAD"], text=True).strip() == PYMS
    sys.path.insert(0, str(a.pyms))
    from PyMS.FileFormats.DAT.UnitsDAT import UnitsDAT
    datraw = (a.data / "arr/units.dat").read_bytes()
    dat = UnitsDAT()
    dat.load_data(datraw)
    mraw = a.manifest.read_bytes()
    manifest = json.loads(mraw)
    assert hashlib.sha256(datraw).hexdigest() == manifest["effective"]["arr\\units.dat"]["sha256"]
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(0x400000, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095)
    u.mem_write(0x400000, pe.get_memory_mapped_image())
    u.mem_map(RECORD, 0x70000)
    u.mem_map(STACK, 0x10000)
    u.mem_map(STOP, 4096)

    def write(address, value, size=4):
        u.mem_write(address, (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little"))

    def read(address, size=4):
        return int.from_bytes(u.mem_read(address, size), "little")

    for typ, unit in enumerate(dat.entries):
        write(0x523C40 + typ, unit.staredit_group_flags, 1)
        write(0x521660 + 4 * typ, unit.special_ability_flags)
        write(0x523FE0 + typ, unit.space_provided, 1)
        ext = unit.unit_extents
        u.mem_write(0x522C38 + typ * 8, struct.pack("<HHHH", ext.left, ext.up, ext.right, ext.down))
    write(0x5264B4, 256, 2)
    write(0x66696C, TERRAIN)
    write(0x666458, TERRAIN + 0x20000)
    write(0x6CE650, CV5)
    write(0x66644C, VF4)
    write(0x6D319C, 2)  # Actual native REP memory paths, not substituted functions.
    max_width = max(x.unit_extents.left + x.unit_extents.right + 1 for x in dat.entries)
    max_height = max(x.unit_extents.up + x.unit_extents.down + 1 for x in dat.entries)

    def blank_counts():
        return {name: [0] * (2736 if name in ("all", "complete") else 12) for name in COUNTERS}

    def setup(mode="ordinary", current=2, elevation=0):
        u.mem_write(0x6C4C10, b"\0" * (0x540 + 0x5580 + 8))
        u.mem_write(0x534590, b"\0" * 552)
        u.mem_write(0x53660C, b"\0" * 180)
        controllers, finished = [2] * 8, [0] * 8
        if mode == "sparse":
            controllers, finished = [0, 1, 2, 3, 4, 5, 7, 2], [0, 1, 2, 0, 0, 0, 0, 0]
        elif mode == "empty":
            controllers = [0] * 8
        for k in range(12):
            write(0x6726D0 + 36 * k, controllers[k] if k < 8 else 0, 1)
            write(0x6726D2 + 36 * k, 1 + k % 4, 1)
        u.mem_write(0x5349E0, bytes(finished))
        write(0x6CA754, current)
        write(0x672664, int(mode == "alternate"), 1)
        write(0x67294D, int(mode == "alternate"), 1)
        u.mem_write(0x5264AC, bytes([5, 1, 5, 0]))
        rel = [[(x + y) % 3 for y in range(12)] for x in range(12)]
        for k in range(12):
            rel[current][k] = k % 3
        rel[current][1] = rel[1][current] = 2
        rel[current][5], rel[5][current] = 2, 0
        rel[current][current] = 2
        u.mem_write(0x534914, bytes(v for row in rel for v in row))
        active = [k for k in range(8) if controllers[k] in (1, 2, 3, 4, 7)
                  and not (controllers[k] in (1, 2) and finished[k] in (1, 2))]
        if mode == "alternate":
            active = [k for k in (5, 1, 5, 0) if k in active]
        write(0x5C99B0, 0)
        write(0x5D6E48, 0)
        write(0x5D6E5C, 0)
        write(0x5D6E40, max_width)
        write(0x5D6E44, max_height)
        u.mem_write(VF4, struct.pack("<16H", *([4 if elevation == 2 else 2 if elevation == 1 else 0] * 16)))
        u.mem_write(RECORD, b"\0" * 64)
        return {"mode": mode, "current": current, "active": active, "rel": rel, "elevation": elevation}

    def players(selector, f):
        active, current, rel = f["active"], f["current"], f["rel"]
        if selector < 12:
            return [selector]
        if selector == 13:
            return [current]
        if selector == 17:
            return active + list(range(8, 12))
        if selector in (14, 15, 16):
            relation = {14: 0, 15: 2, 16: 1}[selector]
            return [k for k in active if k != current and rel[current][k] == relation]
        if 18 <= selector <= 21:
            return [k for k in active if k % 4 + 1 == selector - 17]
        if selector == 26:
            return [k for k in active if k != current and not (rel[current][k] == rel[k][current] == 2)]
        return []

    def query(counts, selector, typ, completed, f):
        prefix = "complete" if completed else "all"
        total = 0
        for k in players(selector, f):
            if typ < 228:
                value = counts[prefix][12 * typ + k]
            elif typ == 229:
                value = counts[prefix + "_men"][k] + counts[prefix + "_buildings"][k]
            else:
                value = counts[prefix + "_" + {230: "men", 231: "buildings", 232: "factories"}[typ]][k]
            total = (total + value) & MASK
        return total

    def global_fixture(profile="distinct"):
        counts = blank_counts()
        for name, vector in counts.items():
            for k in range(len(vector)):
                vector[k] = (100 + k * 3 if name.startswith("all") else 7 + k * 2) if profile == "distinct" else (0 if profile == "zero" else MASK if profile == "max" else 5)
        blob = bytearray(b"\x5a" * 0x540)
        for name, address in GROUP.items():
            struct.pack_into("<12I", blob, address - 0x6C4C10, *counts[name])
        u.mem_write(0x5290C4, bytes(blob))
        u.mem_write(0x52C0C4, struct.pack("<2736I", *counts["complete"]))
        u.mem_write(0x529604, struct.pack("<2736I", *counts["all"]))
        return counts, bytes(blob)

    def cache_snapshot(expected, aggregate_blob=None):
        for name, address in COUNTERS.items():
            vector = expected[name]
            observed = bytes(u.mem_read(address, 4 * len(vector)))
            assert observed == struct.pack("<" + "I" * len(vector), *vector), (name, list(struct.unpack("<" + "I" * len(vector), observed)), vector)
        if aggregate_blob is not None:
            assert bytes(u.mem_read(0x6C4C10, 0x540)) == aggregate_blob
        return hashlib.sha256(bytes(u.mem_read(0x6C4C10, 0x540 + 0x5580))).hexdigest()

    def units_fixture(units, location=(32, 32, 96, 96), flags=0, loc=0):
        axes = [[], []]
        for index, item in enumerate(units, 1):
            address = 0x5DA250 + index * 0x150
            u.mem_write(address, b"\0" * 0x150)
            typ, owner, x, y = item["type"], item.get("owner", 2), item.get("x", 64), item.get("y", 64)
            status = item.get("status", 1)
            write(address + 0x64, typ, 2)
            write(address + 0x4C, owner, 1)
            write(address + 0x4D, 1, 1)
            write(address + 0x4E, 1, 1)
            write(address + 0xDC, status)
            sprite = SPRITES + 0x28 * index
            write(address + 0xC, sprite)
            write(sprite + 0x14, x, 2)
            write(sprite + 0x16, y, 2)
            write(address + 0xC8, item.get("hangar", 0), 1)
            write(address + 0xC0, 0x5DA250 + 0x150 * item["powerup"] if item.get("powerup") else 0)
            write(address + 0xD4, item.get("nuke_ready", 0))
            if item.get("cargo"):
                u.mem_write(address + 0xB0, struct.pack("<8H", *(item["cargo"] + [0] * (8 - len(item["cargo"])))))
            if item.get("transport_upgrade"):
                write(0x534590 + 46 * owner + 24, 1, 1)
            if item.get("spatial", True):
                ext = dat.entries[typ].unit_extents
                bounds = item.get("bounds", (x - ext.left, y - ext.up, x + ext.right, y + ext.down))
                for axis in range(2):
                    axes[axis].extend([(bounds[axis], index, False), (bounds[axis + 2], index, True)])
        for axis, base in enumerate((0x5C99B8, 0x5D03F8)):
            axes[axis].sort(key=lambda row: row[0])
            for position, (coordinate, index, high) in enumerate(axes[axis]):
                write(base + 8 * position, index)
                write(base + 8 * position + 4, coordinate)
                write(0x5DA250 + index * 0x150 + (0x144 if axis else 0x13C) + 4 * high, position)
        write(0x5C99B0, len(axes[0]))
        write(0x5D6E48, 0)
        write(0x5D6E5C, 0)
        for i, value in enumerate(location):
            write(0x534F40 + 20 * loc + 4 * i, value)
        write(0x534F52 + 20 * loc, flags, 2)
        return axes

    def count_units(units, included, f, flags):
        counts = blank_counts()

        def add_complete(index):
            item = units[index - 1]
            typ, owner = item["type"], item.get("owner", 2)
            unit = dat.entries[typ]
            group = unit.staredit_group_flags
            counts["complete"][12 * typ + owner] += 1
            can_cargo = unit.space_provided and (typ != 42 or item.get("transport_upgrade")) and not (item.get("status", 1) & 0x40000000)
            if can_cargo:
                for child in item.get("cargo", []):
                    add_complete(child)
            if typ in (72, 82, 81, 83):
                childtyp = 73 if typ in (72, 82) else 85
                counts["complete"][12 * childtyp + owner] += item.get("hangar", 0)
            if unit.special_ability_flags & 8 and item.get("powerup"):
                child = units[item["powerup"] - 1]
                if child["type"] == 215:
                    add_complete(item["powerup"])
                else:
                    counts["complete"][12 * child["type"] + owner] += 1
            if typ == 108 and item.get("nuke_ready"):
                counts["complete"][12 * 14 + owner] += 1
            if group & 0x20:
                counts["complete_factories"][owner] += 1
            if group & 8:
                counts["complete_men"][owner] += 1
            elif group & 16:
                counts["complete_buildings"][owner] += 1

        for index in included:
            item = units[index - 1]
            typ, owner, status = item["type"], item.get("owner", 2), item.get("status", 1)
            unit = dat.entries[typ]
            excluded = bool(flags & (1 << (f["elevation"] + (3 if status & 4 else 0))))
            if excluded or status & 0x40000000 or unit.special_ability_flags & 16:
                continue
            counts["all"][12 * typ + owner] += 1
            group = unit.staredit_group_flags
            if group & 0x20:
                counts["all_factories"][owner] += 1
            if group & 8 or typ in (36, 59, 97):
                counts["all_men"][owner] += 1
            elif group & 16:
                counts["all_buildings"][owner] += 1
            if status & 1:
                add_complete(index)
        return counts

    all_visited = set()
    approved_instructions = set()

    def call(entry, ecx=0, edx=0, arguments=()):
        done, count = False, 0

        def step(machine, pc, n, unused):
            nonlocal done, count
            if pc == STOP:
                done = True
                machine.emu_stop()
                return
            # REP instructions revisit the same immutable hash-pinned address
            # thousands of times. Cache the reviewed (address,size) pair without
            # removing the instruction-level allowlist or execution accounting.
            if (pc, n) not in approved_instructions:
                if not any(lo <= pc and pc + n <= hi for lo, hi in BLOCKS.values()):
                    raise RuntimeError(f"Unapproved instruction {pc:#x}")
                approved_instructions.add((pc, n))
            count += 1
            all_visited.add(pc)

        hook = u.hook_add(UC_HOOK_CODE, step)
        write(STACK + 0x8000, STOP)
        for i, argument in enumerate(arguments):
            write(STACK + 0x8004 + 4 * i, argument)
        u.reg_write(UC_X86_REG_ESP, STACK + 0x8000)
        u.reg_write(UC_X86_REG_ECX, ecx)
        u.reg_write(UC_X86_REG_EDX, edx)
        try:
            u.emu_start(entry, STOP + 1, count=50000)
        finally:
            u.hook_del(hook)
        assert done
        return u.reg_read(UC_X86_REG_EAX), count

    def comparison(value, cmp, amount):
        return int(value >= amount) if cmp == 0 else int(value <= amount) if cmp == 1 else int(value == amount) if cmp == 10 else 0

    def condition_record(kind, location, selector, amount, typ, cmp):
        u.mem_write(RECORD, b"\0" * 20)
        u.mem_write(RECORD, struct.pack("<IIIHBBBBH", location, selector, amount, typ, cmp, kind, 0, 0, 0))

    def command_case(kind, selector, typ, cmp, amount, mode="ordinary", current=2, profile="distinct"):
        f = setup(mode, current)
        counts, blob = global_fixture(profile)
        condition_record(kind, 0, selector, amount, typ, cmp)
        if kind in (6, 16):
            least = kind == 16
            initial = MASK if least else 0
            values = [query(counts, k, typ, True, f) for k in f["active"]]
            extreme = min([initial] + values) if least else max([initial] + values)
            own = query(counts, current, typ, True, f) if current in f["active"] else initial
            expected = int(own <= extreme) if least else int(own >= extreme)
            value = own
        else:
            value = query(counts, selector, typ, cmp != 1, f)
            expected = comparison(value, cmp, amount)
        observed, n = call(ENTRY[kind], RECORD)
        assert observed == expected, (kind, selector, typ, cmp, amount, observed, expected)
        digest = cache_snapshot(counts, blob)
        assert read(0x6CA6D0, 1) == 1
        return {"kind": "command", "condition_id": kind, "selector": selector, "unit_type": typ,
                "comparison": cmp, "amount": amount, "fixture": [mode, current, profile],
                "queried_or_current_value": value, "observed": observed, "instructions": n, "cache_sha256": digest}

    def bring_case(kind, location_id, bounds, flags, selector, typ, cmp, amount, units, included,
                   mode="ordinary", current=2, elevation=0, label=None):
        f = setup(mode, current, elevation)
        loc = (location_id & 255) - 1
        units_fixture(units, bounds, flags, max(0, loc))
        counts = count_units(units, included, f, flags) if location_id & 255 else blank_counts()
        condition_record(kind, location_id, selector, amount, typ, cmp)
        if not (location_id & 255):
            expected, value = 0, 0
        elif kind in (7, 17):
            least = kind == 17
            initial = MASK if least else 0
            values = [query(counts, k, typ, True, f) for k in f["active"]]
            extreme = min([initial] + values) if least else max([initial] + values)
            own = query(counts, current, typ, True, f) if current in f["active"] else initial
            expected = int(own <= extreme) if least else int(own >= extreme)
            value = own
        else:
            value = query(counts, selector, typ, cmp != 1, f)
            expected = comparison(value, cmp, amount)
        observed, n = call(ENTRY[kind], RECORD)
        assert observed == expected, (label, kind, bounds, flags, selector, typ, cmp, amount, units, observed, expected)
        digest = cache_snapshot(counts)
        assert read(0x5D6E48) == read(0x5D6E5C) == 0
        assert read(0x6CA6D0, 1) == (2 if location_id & 255 else 0)
        return {"kind": "bring", "condition_id": kind, "location_id": location_id,
                "location_bounds": list(bounds), "elevation_flags": flags, "selector": selector,
                "unit_type": typ, "comparison": cmp, "amount": amount,
                "fixture": [mode, current, elevation], "label": label, "units": units,
                "spatially_included_indices": included, "queried_or_current_value": value,
                "observed": observed, "instructions": n, "cache_sha256": digest,
                "nonzero_counts": {name: [[i, v] for i, v in enumerate(vector) if v]
                                   for name, vector in counts.items() if any(vector)}}

    synthetic = []
    for selector in SELECTORS:
        for typ in TYPES:
            f = setup()
            counts, blob = global_fixture()
            for cmp in (0, 1, 10, 2):
                value = query(counts, selector, typ, cmp != 1, f)
                for amount in (max(0, value - 1), value, min(MASK, value + 1)):
                    synthetic.append(command_case(2, selector, typ, cmp, amount))
    for mode in ("sparse", "alternate", "empty"):
        for selector in (0, 1, 13, 14, 15, 16, 17, 18, 26):
            for cmp in (0, 1, 10):
                synthetic.append(command_case(2, selector, 229, cmp, 5, mode, current=7))
    for selector in (0, 13, 17, 18, 26):
        for cmp in (0, 1, 10):
            synthetic.append(command_case(2, selector, 229, cmp, 0x80000000, profile="max"))
    print(json.dumps({"phase": "Command comparisons", "passed_so_far": len(synthetic)}), flush=True)
    for kind in (6, 16):
        for mode in ("ordinary", "sparse", "alternate", "empty"):
            for current in (0, 2, 7, 8):
                for profile in ("distinct", "zero", "max", "tie"):
                    synthetic.append(command_case(kind, MASK, 229, 99, 123, mode, current, profile))
    # All 228 DAT classes: complete/incomplete and hallucinated root exclusion.
    for typ in range(228):
        for status in (0, 1, 0x40000001):
            unit = {"type": typ, "owner": 2, "status": status}
            synthetic.append(bring_case(3, 1, (32, 32, 96, 96), 0, 13, typ, 10,
                                        int(status == 1 and not dat.entries[typ].special_ability_flags & 16),
                                        [unit], [1], label="DAT_class"))
    for elevation in range(3):
        for status in (1, 5):
            for flags in range(64):
                synthetic.append(bring_case(3, 1, (32, 32, 96, 96), flags, 13, 0, 10, 1,
                                            [{"type": 0, "status": status}], [1], elevation=elevation,
                                            label="elevation_ground_air"))
    # Positive stored relationships use real lookup/recursive counter helpers.
    relationships = [
        [{"type": 11, "cargo": [2]}, {"type": 0, "owner": 7, "status": 0, "spatial": False}],
        [{"type": 125, "cargo": [2]}, {"type": 0, "owner": 2, "status": 1, "spatial": False}],
        [{"type": 42, "cargo": [2]}, {"type": 37, "spatial": False}],
        [{"type": 42, "cargo": [2], "transport_upgrade": True}, {"type": 37, "spatial": False}],
        [{"type": 72, "hangar": 3}], [{"type": 83, "hangar": 4}],
        [{"type": 7, "powerup": 2}, {"type": 215, "owner": 7, "spatial": False}],
        [{"type": 7, "powerup": 2}, {"type": 216, "owner": 7, "spatial": False}],
        [{"type": 108, "nuke_ready": 1}],
    ]
    for units in relationships:
        for cmp in (0, 1, 10):
            synthetic.append(bring_case(3, 1, (32, 32, 96, 96), 0, 17, 229, cmp, 1, units, [1], label="stored_children"))
    for location in (0, 1, 256, 257):
        for kind in (3, 7, 17):
            synthetic.append(bring_case(kind, location, (32, 32, 96, 96), 0, MASK if kind != 3 else 13,
                                        0, 99 if kind != 3 else 10, 1, [{"type": 0}], [1], label="location_low_byte"))
    for mode in ("ordinary", "sparse", "alternate", "empty"):
        for selector in SELECTORS:
            for cmp in (0, 1, 10):
                units = [{"type": 0, "owner": k, "x": 64 + k} for k in range(12)]
                synthetic.append(bring_case(3, 1, (32, 32, 96, 96), 0, selector, 0, cmp, 1, units, list(range(1, 13)), mode,
                                            label="Bring_selector"))
    # The real location query has edge behavior dependent on the maximum-unit
    # dimensions: short ranges expand and retain upper-bound touching units.
    geometry_cases = []
    for maximum in ((17, 20), (256, 160)):
        for x, y in [(x, 48) for x in (23, 24, 31, 32, 40, 56, 64, 72, 73, 80)] + [(48, y) for y in (21, 22, 31, 32, 48, 64, 73, 74)]:
            setup()
            units_fixture([{"type": 0, "x": x, "y": y}], (32, 32, 64, 64))
            write(0x5D6E40, maximum[0])
            write(0x5D6E44, maximum[1])
            u.mem_write(RECORD, struct.pack("<4H", 32, 32, 64, 64))
            ptr, n = call(0x4224C0, RECORD)
            observed = [read(ptr)] if read(ptr) else []
            expected_x = x + 8 >= 32 and (x - 8 <= 64 if maximum[0] > 33 else x - 8 < 64)
            expected_y = y + 10 >= 32 and (y - 9 <= 64 if maximum[1] > 33 else y - 9 < 64)
            expected = [0x5DA3A0] if expected_x and expected_y else []
            assert observed == expected, (maximum, x, y, observed, expected)
            _, release_n = call(0x4222B0, ptr)
            assert read(0x5D6E48) == read(0x5D6E5C) == 0
            geometry_cases.append({"kind": "location_query_edge", "maximum_size_fixture": list(maximum),
                                   "unit_type": 0, "center": [x, y], "location": [32, 32, 64, 64],
                                   "observed_unit_indices": [1] if observed else [], "instructions": [n, release_n]})
    for typ, bounds, expected in [(202, (96, 96, 32, 32), [1]), (0, (96, 96, 32, 32), []),
                                  (202, (64, 64, 64, 64), [1]), (0, (64, 64, 64, 64), [1])]:
        setup()
        units_fixture([{"type": typ}], bounds)
        u.mem_write(RECORD, struct.pack("<4H", *bounds))
        ptr, n = call(0x4224C0, RECORD)
        observed = [1] if read(ptr) else []
        assert observed == expected, (typ, bounds, observed, expected)
        _, release_n = call(0x4222B0, ptr)
        geometry_cases.append({"kind": "location_query_inverted_or_zero", "maximum_size_fixture": [max_width, max_height],
                               "unit_type": typ, "center": [64, 64], "location": list(bounds),
                               "observed_unit_indices": observed, "instructions": [n, release_n],
                               "scope": "post-load rectangle fixture; CHK normalization is separate"})
    print(json.dumps({"phase": "Bring and location query", "passed_so_far": len(synthetic) + len(geometry_cases)}), flush=True)

    # Cache reuse, source mutation, location changes and the actual invalidator.
    cache_cases = []
    f = setup()
    counts, blob = global_fixture()
    _, n1 = call(0x4B9C00)
    digest1 = cache_snapshot(counts, blob)
    write(0x52C0C4 + 4 * 2, 9999)
    _, n2 = call(0x4B9C00)
    assert read(0x6C5150 + 4 * 2) == counts["complete"][2]
    _, n3 = call(0x4B9820)
    assert read(0x6CA6D0, 1) == 0
    _, n4 = call(0x4B9C00)
    counts["complete"][2] = 9999
    digest2 = cache_snapshot(counts, blob)
    cache_cases.append({"kind": "global_cache_reuse_invalidate", "cache_before": digest1, "cache_after": digest2,
                        "observed_completed_unit0_player2": 9999, "instructions": [n1, n2, n3, n4]})
    f = setup()
    units_fixture([{"type": 0}], loc=0)
    _, n1 = call(0x4BA350, 0)
    assert read(0x6C5150 + 8) == 1
    write(0x5DA3A0 + 0xDC, 0)
    _, n2 = call(0x4BA350, 0)
    assert read(0x6C5150 + 8) == 1
    _, n3 = call(0x4B9820)
    _, n4 = call(0x4BA350, 0)
    assert read(0x6C5150 + 8) == 0 and read(0x6C7C10 + 8) == 1
    cache_cases.append({"kind": "location_cache_reuse_invalidate", "before_completed": 1, "after_completed": 0,
                        "after_all": 1, "instructions": [n1, n2, n3, n4]})
    # Changing the location byte rebuilds, and switching global/local cache modes
    # forces the corresponding native copy or visitor path.
    f = setup()
    units_fixture([{"type": 0}], loc=0)
    for i, v in enumerate((160, 160, 192, 192)):
        write(0x534F40 + 20 + 4 * i, v)
    write(0x534F52 + 20, 0, 2)
    _, n1 = call(0x4BA350, 0)
    before = read(0x6C5150 + 8)
    _, n2 = call(0x4BA350, 1)
    assert before == 1 and read(0x6C5150 + 8) == 0 and read(0x6CA6D1, 1) == 1
    counts, blob = global_fixture()
    _, n3 = call(0x4B9C00)
    cache_snapshot(counts, blob)
    assert read(0x6CA6D0, 1) == 1
    _, n4 = call(0x4BA350, 0)
    assert read(0x6CA6D0, 1) == 2 and read(0x6C5150 + 8) == 1
    cache_cases.append({"kind": "location_key_and_global_mode_transition", "observed_completed_unit0_player2": [1, 0, counts["complete"][2], 1],
                        "instructions": [n1, n2, n3, n4]})

    cargo_lookup_cases = []
    for generation in range(32):
        for match in (False, True):
            for sprite_present in (False, True):
                for order, order_state in ((0, 0), (0, 1), (1, 0), (1, 1)):
                    setup()
                    units_fixture([{"type": 11}, {"type": 0, "spatial": False}])
                    child = 0x5DA250 + 2 * 0x150
                    write(child + 0xA5, generation if match else (generation + 1) % 32, 1)
                    write(child + 0x4D, order, 1)
                    write(child + 0x4E, order_state, 1)
                    if not sprite_present:
                        write(child + 0xC, 0)
                    raw_slot = 2 | (generation << 11)
                    expected = child if match and sprite_present and not (order == 0 and order_state == 1) else 0
                    observed, n = call(0x428B50, raw_slot)
                    assert observed == expected, (generation, match, sprite_present, order, order_state, observed, expected)
                    cargo_lookup_cases.append({"raw_slot_word": raw_slot, "tag": generation, "index": 2,
                                               "matching_generation_byte": match, "sprite_present": sprite_present,
                                               "order_byte": order, "order_state_byte": order_state,
                                               "observed_unit_index": 2 if observed else None, "instructions": n})
    observed, n = call(0x428B50, 0)
    assert observed == 0
    cargo_lookup_cases.append({"raw_slot_word": 0, "observed_unit_index": None, "instructions": n})

    maps, unique, raw_hist, ignored, unresolved = [], {}, Counter(), Counter(), []
    location_masks, referenced_masks, nonzero_mask_refs = Counter(), Counter(), []
    for row in manifest["campaign_found"]:
        data = (a.data / row["path"].replace("\\", "/")).read_bytes()
        assert hashlib.sha256(data).hexdigest() == row["sha256"]
        pos, parts = 0, []
        while pos < len(data):
            tag, n = struct.unpack_from("<4sI", data, pos)
            part = data[pos + 8:pos + 8 + n]
            pos += 8 + n
            assert len(part) == n
            parts.append((tag, part))
        locations = next((part for tag, part in reversed(parts) if tag == b"MRGN"), b"")
        assert len(locations) % 20 == 0
        location_masks.update(struct.unpack_from("<H", locations, i + 18)[0] for i in range(0, len(locations), 20))
        counts, index = Counter(), 0
        for tag, part in parts:
            if tag != b"TRIG":
                continue
            assert len(part) % 2400 == 0
            for offset in range(0, len(part), 2400):
                for slot in range(16):
                    v = part[offset + 20 * slot:offset + 20 * slot + 20]
                    kind = v[15]
                    if kind not in ENTRY:
                        continue
                    location, selector, amount, typ = struct.unpack_from("<IIIH", v)
                    cmp = v[14]
                    bounds, flags = (32, 32, 96, 96), 0
                    if kind in (3, 7, 17) and location & 255:
                        loc = (location & 255) - 1
                        if len(locations) < 20 * (loc + 1):
                            unresolved.append({"map": row["path"], "location": location, "reason": "MRGN record absent"})
                            continue
                        bounds = struct.unpack_from("<IIII", locations, loc * 20)
                        flags = struct.unpack_from("<H", locations, loc * 20 + 18)[0]
                        referenced_masks[flags] += 1
                        if flags:
                            nonzero_mask_refs.append([row["path"], index, slot, kind, location, flags, typ, bool(v[17] & 2)])
                    key = (kind, location, selector, amount, typ, cmp, tuple(bounds), flags)
                    unique.setdefault(key, []).append([row["path"], index, slot, bool(v[17] & 2)])
                    raw_hist[kind] += 1
                    counts[kind] += 1
                    ignored[kind] += bool(v[17] & 2)
                index += 1
        maps.append({"path": row["path"], "sha256": row["sha256"], "selected_condition_histogram": dict(counts),
                     "MRGN_bytes": len(locations), "trigger_count": index})
    campaign = []
    for key, references in sorted(unique.items()):
        kind, location, selector, amount, typ, cmp, bounds, flags = key
        if selector not in SELECTORS or not (typ < 228 or typ in (229, 230, 231, 232)):
            unresolved.append({"parameters": list(key), "reason": "outside selected normal domain", "references": references})
            continue
        if kind in (2, 6, 16):
            case = command_case(kind, selector, typ, cmp, amount)
        else:
            f = setup()
            selected = players(selector, f)
            owner = selected[0] if selected else 2
            unit_type = typ if typ < 228 else 0 if typ in (229, 230) else 106 if typ == 231 else 111
            x1, y1, x2, y2 = bounds
            positive_rectangle = x1 < x2 and y1 < y2
            units = []
            if positive_rectangle:
                x, y = (x1 + x2 - 1) // 2, (y1 + y2 - 1) // 2
                units = [{"type": unit_type, "owner": owner, "x": x, "y": y,
                          "status": 1 | (4 if dat.entries[unit_type].special_ability_flags & 4 else 0)}]
            case = bring_case(kind, location, bounds, flags, selector, typ, cmp, amount, units,
                              [1] if units else [], label="campaign_parameters_one_unit_fixture")
        case["references"] = references
        campaign.append(case)
    assert not unresolved, unresolved
    witnesses = [{"va": hex(pc), "rva": hex(pc - 0x400000), "bytes": i.bytes.hex(),
                  "mnemonic": i.mnemonic, "operands": i.op_str}
                 for pc in sorted(all_visited) for i in list(md.disasm(pe.get_data(pc - 0x400000, 15), pc))[:1]]
    result = {"target_sha256": HASH, "probe_script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "runtime_architecture": platform.machine(), "python_version": platform.python_version(),
              "unicorn_version": unicorn.__version__, "pefile_version": pefile.__version__, "capstone_version": capstone.__version__,
              "native_code_executed": True, "complete_game_executed": False, "stubs": [], "imports_executed": [],
              "execution_scope": "Complete original functions and actual selected-path callees",
              "pyms_commit": PYMS, "units_dat_sha256": hashlib.sha256(datraw).hexdigest(),
              "manifest_sha256": hashlib.sha256(mraw).hexdigest(), "fixture_max_unit_size": [max_width, max_height],
              "synthetic_cases": synthetic, "location_query_cases": geometry_cases,
              "cache_sequence_cases": cache_cases, "cargo_lookup_cases": cargo_lookup_cases, "campaign_parameter_cases": campaign,
              "passed_cases": len(synthetic) + len(geometry_cases) + len(cache_cases) + len(cargo_lookup_cases) + len(campaign), "failed_cases": 0,
              "campaign_audit": {"physical_maps": len(maps), "maps": maps, "raw_condition_histogram": dict(raw_hist),
                                 "ignored_condition_histogram": dict(ignored), "distinct_parameter_tuples": len(unique),
                                 "MRGN_elevation_mask_histogram": dict(location_masks), "referenced_elevation_mask_histogram": dict(referenced_masks),
                                 "nonzero_elevation_mask_references": nonzero_mask_refs,
                                 "unresolved_parameters": unresolved},
              "executed_instruction_witnesses": witnesses,
              "limits": ["Inert unit/spatial/terrain fixtures; no original unit creation or whole mission execution",
                         "Global producer arrays are seeded; Command copy/query, not producer maintenance, is executed",
                         "Bring campaign inputs use one matching fixture unit in positive rectangles and flat elevation zero",
                         "Stored cargo/hangar/powerup/nuke counting uses real helpers but does not create these objects",
                         "Normal selectors only; None12 and out-of-domain EUD unit IDs excluded"]}
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"passed_cases": result["passed_cases"], "synthetic": len(synthetic), "location_query": len(geometry_cases), "cache_sequences": len(cache_cases), "cargo_lookup": len(cargo_lookup_cases),
                      "campaign": len(campaign), "raw_histogram": dict(raw_hist), "unresolved": len(unresolved)}))


if __name__ == "__main__":
    main()
