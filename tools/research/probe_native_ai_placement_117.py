#!/usr/bin/env python3
"""Inspect and probe original 1.17 AI placement grids in private x86 memory.

All execution is hash gated, instruction bounded and allowlisted. Actual
reviewed callees run; no Windows import or substituted helper is available.
Synthetic terrain, regions and unit lists are explicit inputs.
"""

import argparse
from collections import Counter
import hashlib
from itertools import groupby
import json
from pathlib import Path
import platform
import struct
import subprocess
import sys

import capstone
import pefile

import probe_native_placement_117 as placement

TARGET_HASH = placement.TARGET_HASH
GRID, UNIT, SPRITE, TILES = 0x800000, 0x814000, 0x815000, 0x820000
MAP, REGIONS, NEIGHBORS = 0x840000, 0x830000, 0x832000
CV5, TILE_IDS, OVERLAY_IDS, VF4 = 0x8A0000, 0x8B0000, 0x8B8000, 0x8C0000
STACK, STOP, OUT = 0x900000, 0xA00000, 0x816000
MASK, SEED, MAP_SIZE = 0xFFFFFFFF, 1536 | (1536 << 16), 96
UNITS_HASH = "da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba"
PYMS_COMMIT = "bfc5d3aad0b5614a5aff72c223f8efa00afddfa4"
BLOCKS = dict(placement.BLOCKS)
BLOCKS.update({
    "ai_base_grid": (0x495D40, 0x495E55),
    "ai_connectivity_grid": (0x497630, 0x4976C5),
    "connectivity_group": (0x481C90, 0x481CA4),
    "region_ground_neighbors": (0x481AD0, 0x481AF7),
    "neighbor_ai_region_gate": (0x4AE700, 0x4AE75F),
    "ai_neighbor_filter": (0x497910, 0x4979A9),
    "native_stack_probe": (0x4D2200, 0x4D222B),
    "native_memcopy_rep_prefix": (0x4D16A0, 0x4D16D1),
    "native_memcopy_rep_return": (0x4D19E8, 0x4D19EF),
    "terrain_elevation": (0x445690, 0x44571D),
    "ai_elevation_filter": (0x498430, 0x498515),
    "ai_power_coverage_filter": (0x498520, 0x4985F9),
    "ai_mask_rectangle": (0x4960B0, 0x496191),
    "ai_mask_unit": (0x4961A0, 0x496204),
    "grid_is_empty": (0x496210, 0x496232),
    "unit_inactive": (0x41FE40, 0x41FE55),
    "player_unit_list": (0x4256F0, 0x4256F8),
    "zerg_building_morph_predicate": (0x43C040, 0x43C084),
    "ai_resource_spacing_filter": (0x498230, 0x498424),
    "ai_addon_space_filter": (0x497D90, 0x497EE4),
    "ai_building_spacing_filter": (0x497F00, 0x4981A5),
    "ai_defense_pattern_filter": (0x498600, 0x4987CD),
    "tile_mask_predicate": (0x413270, 0x4132A8),
    "ai_walkable_filter": (0x498820, 0x498925),
    "ai_directional_filter": (0x498930, 0x4989F0),
    "ai_radius_filter": (0x4976D0, 0x497906),
    "same_or_disconnected_path_distance": (0x496930, 0x4969AF),
    "approximate_distance": (0x405CF0, 0x405D3D),
    "ai_grid_pipeline": (0x495BB0, 0x495CF5),
    "campaign_ai_flag": (0x482C90, 0x482CA3),
    "defense_anchor_selection": (0x495480, 0x4955A9),
    "ai_placement_search": (0x494FC0, 0x4953E2),
    "ai_unit_centered_placement": (0x496240, 0x4962C0),
    "placement_throttle_get": (0x482EC0, 0x482ED2),
    "placement_throttle_set": (0x482EA0, 0x482EAE),
    "region_center": (0x481A60, 0x481A86),
    "nearest_disabled_own_unit": (0x433220, 0x43323F),
    "nearest_position_wrapper": (0x442D40, 0x442D8C),
    "nearest_border_search": (0x442D90, 0x44310C),
    "disabled_owner_predicate": (0x433F20, 0x433F3D),
    "unit_pool_base": (0x4255F0, 0x4255F6),
})
ALLOW = list(BLOCKS.values())


def trunc(value, divisor):
    return (abs(value) // divisor) * (-1 if value < 0 else 1)


def main():
    own_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    dependency_hash = hashlib.sha256(Path(placement.__file__).read_bytes()).hexdigest()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--static-out", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--units-dat", type=Path)
    parser.add_argument("--pyms", type=Path)
    parser.add_argument("--execute-original-code", action="store_true")
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        parser.error("Unknown executable hash: reviewed addresses do not apply")
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    assert base == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    stock = None
    stock_profile = None
    if args.units_dat or args.pyms:
        if not args.units_dat or not args.pyms:
            parser.error("--units-dat and --pyms must be provided together")
        dat = args.units_dat.read_bytes()
        if hashlib.sha256(dat).hexdigest() != UNITS_HASH:
            parser.error("Unknown effective units.dat hash")
        if subprocess.check_output(["git", "-C", str(args.pyms), "rev-parse", "HEAD"], text=True).strip() != PYMS_COMMIT:
            parser.error("Unexpected PyMS reader commit")
        sys.path.insert(0, str(args.pyms))
        from PyMS.FileFormats.DAT.UnitsDAT import UnitsDAT
        units = UnitsDAT()
        units.load_data(dat)
        assert len(units.entries) == 228
        stock = units.entries
        stock_profile = {"units_dat_sha256": UNITS_HASH, "pyms_commit": PYMS_COMMIT,
            "fields": "special_ability_flags, placement_size, group_flags, addon_position",
            "power_fixture": "synthetic 2x2 all-enabled mask and owner-matching supplied source"}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    witnesses = {}
    for name, (lo, hi) in BLOCKS.items():
        offset = pe.get_offset_from_rva(lo - base)
        rows = list(md.disasm(data[offset:offset + hi - lo], lo))
        if not rows or rows[-1].address + rows[-1].size != hi:
            parser.error(f"Invalid instruction boundary: {name}")
        witnesses[name] = [{"va": hex(i.address), "bytes": i.bytes.hex(),
                            "mnemonic": i.mnemonic, "operands": i.op_str} for i in rows]

    def table(first, last, map_va, targets_va, target_count):
        off = pe.get_offset_from_rva(map_va - base)
        indices = list(data[off:off + last - first + 1])
        off = pe.get_offset_from_rva(targets_va - base)
        targets = struct.unpack_from("<" + "I" * target_count, data, off)
        return {str(first + i): hex(targets[x]) for i, x in enumerate(indices)}

    dispatch = {
        "base_addon_room": table(106, 116, 0x495E60, 0x495E58, 2),
        "building_spacing_existing": table(111, 162, 0x4981B4, 0x4981A8, 3),
        "building_spacing_requested": table(111, 162, 0x4981F0, 0x4981E8, 2),
        "addon_space_existing": table(106, 116, 0x497EEC, 0x497EE4, 2),
        "defense_existing": table(124, 162, 0x4987DC, 0x4987D0, 3),
        "pipeline_requested": table(124, 162, 0x495D0C, 0x495CF8, 5),
        "search_requested": table(106, 156, 0x495408, 0x4953E4, 9),
    }
    directional = []
    for index in range(14):
        off = pe.get_offset_from_rva(0x509040 + index * 16 - base)
        values = list(struct.unpack_from("<4i", data, off))
        if values[0] == -1:
            break
        directional.append(values)
    patterns = {}
    for va, side in [(0x509120, 17), (0x509248, 13), (0x5092F8, 9), (0x50934C, 5)]:
        off = pe.get_offset_from_rva(va - base)
        pattern = data[off:off + side * side].decode("ascii")
        patterns[str(side)] = [pattern[row * side:(row + 1) * side] for row in range(side)]
    if args.static_out:
        args.static_out.parent.mkdir(parents=True, exist_ok=True)
        args.static_out.write_text(json.dumps({"target_sha256": TARGET_HASH,
            "version": "1.17.0.1", "evidence": "static; analytical labels",
            "witnesses": witnesses, "dispatch": dispatch,
            "directional_lines": directional, "defense_patterns": patterns}, indent=2) + "\n")
    if not args.execute_original_code:
        print(json.dumps({"target_sha256": TARGET_HASH, "blocks": len(BLOCKS),
            "plan": "Isolated original AI candidate-grid functions; actual callees, no stubs/imports"}))
        return
    if args.out is None:
        parser.error("--out required for execution evidence")

    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP
    image = pe.get_memory_mapped_image()
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    reviewed_instructions = {(int(row["va"], 16), len(bytes.fromhex(row["bytes"])))
                             for rows in witnesses.values() for row in rows}
    cases = []

    def put(uc, address, value, size=4):
        uc.mem_write(address, (value & ((1 << (size * 8)) - 1)).to_bytes(size, "little"))

    def get(uc, address, size=4):
        return int.from_bytes(uc.mem_read(address, size), "little")

    def machine():
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(base, image_size)
        uc.mem_write(base, image)
        uc.mem_map(GRID, 0x100000)
        uc.mem_map(STACK, 0x10000)
        uc.mem_map(STOP, 0x1000)
        put(uc, 0x6D319C, 2)
        put(uc, 0x5264B4, MAP_SIZE, 2)
        put(uc, 0x5264B6, MAP_SIZE, 2)
        put(uc, 0x666454, TILES)
        put(uc, 0x69B584, MAP)
        uc.mem_write(MAP + 0xC, struct.pack("<H", 7) * 65536)
        put(uc, MAP + 0x449FE + 7 * 64, 10, 2)
        put(uc, MAP + 0x449FE + 8 * 64, 10, 2)
        put(uc, 0x6B1A74 + 3 * 4, REGIONS)
        put(uc, 0x6726D0 + 3 * 36, 1, 1)
        put(uc, 0x5C99B0, 0)
        put(uc, 0x5D6E48, 0)
        put(uc, 0x5D6E5C, 0)
        put(uc, 0x66696C, OVERLAY_IDS)
        put(uc, 0x666458, TILE_IDS)
        put(uc, 0x6CE650, CV5)
        put(uc, 0x66644C, VF4)
        for unit_id in range(228):
            put(uc, 0x521660 + unit_id * 4, 0)
            put(uc, 0x522728 + unit_id * 4, 32, 2)
            put(uc, 0x52272A + unit_id * 4, 32, 2)
            put(uc, 0x523C40 + unit_id, 2, 1)
        put(uc, UNIT + 0xC, SPRITE)
        put(uc, UNIT + 0x4C, 3, 1)
        put(uc, UNIT + 0x4D, 1, 1)
        put(uc, UNIT + 0x64, 7, 2)
        put(uc, UNIT + 0xDC, 1)
        put(uc, SPRITE + 0x14, 1536, 2)
        put(uc, SPRITE + 0x16, 1536, 2)
        return uc

    def call(uc, address, ecx=0, edx=0, arguments=(), limit=3000000):
        count, returned, calls = 0, False, Counter()
        starts = {lo for lo, _ in ALLOW}

        def instruction(vm, va, size, _):
            nonlocal count, returned
            if va == STOP:
                returned = True
                vm.emu_stop()
                return
            if (va, size) not in reviewed_instructions:
                raise RuntimeError(f"Unreviewed original instruction {va:#x}")
            count += 1
            if va in starts:
                calls[hex(va)] += 1

        hook = uc.hook_add(UC_HOOK_CODE, instruction)
        sp = STACK + 0xF000
        uc.mem_write(sp, struct.pack("<" + "I" * (len(arguments) + 1), STOP,
                                   *(v & MASK for v in arguments)))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx & MASK)
        uc.reg_write(UC_X86_REG_EDX, edx & MASK)
        uc.emu_start(address, STOP + 1, count=limit)
        uc.hook_del(hook)
        if not returned:
            raise RuntimeError(f"Original function {address:#x} exceeded {limit} instructions")
        return uc.reg_read(UC_X86_REG_EAX), count, dict(calls)

    def grid(uc, cells, fill=0):
        expected = bytearray([fill] * 4096)
        for col, row, value in cells:
            expected[row * 64 + col] = value
        uc.mem_write(GRID, bytes(expected))
        put(uc, GRID + 4096, 0xA5, 1)
        return expected

    def record(kind, inputs, uc, expected, run, output=None):
        observed = bytes(uc.mem_read(GRID, 4096))
        if observed != expected:
            delta = [(i, observed[i], expected[i]) for i in range(4096) if observed[i] != expected[i]]
            raise AssertionError((kind, inputs, delta[:20]))
        assert get(uc, GRID + 4096, 1) == 0xA5
        result, instructions, calls = run
        nonzero = [[i % 64, i // 64, value] for i, value in enumerate(observed) if value]
        item = {"kind": kind, "inputs": inputs, "grid_sha256": hashlib.sha256(observed).hexdigest(),
            "nonzero_count": len(nonzero),
            "instructions": instructions, "calls": calls,
            "evidence": "complete original function and actual reviewed callees"}
        if len(nonzero) <= 128:
            item["nonzero_cells"] = nonzero
        else:
            item["grid_runs"] = [[value, len(list(run))] for value, run in groupby(observed)]
        if output is not None:
            item["output"] = output
        cases.append(item)

    for size in [0, 1, 2, 3, 4, 7, 64, 65, 66, MASK]:
        uc = machine()
        expected = grid(uc, [])
        start = (32 - (size >> 1)) & MASK
        end = (start + size) & MASK
        for row in range(64):
            for col in range(64):
                if start <= row < end and start <= col < end:
                    expected[row * 64 + col] = 1
        record("base_window", {"size": size, "seed": [1536, 1536]}, uc, expected,
               call(uc, 0x495D40, 109, GRID, [3, SEED, size]))

    for unit_id in range(106, 117):
        for block_addon in [False, True]:
            uc = machine()
            expected = grid(uc, [], fill=0x37)
            # All cells outside the selected 1x1 window are preserved. Failure
            # does not clear a preexisting grid byte; the caller initializes it.
            put(uc, TILES + 4 * (49 * MAP_SIZE + 52), 0x20000000 if block_addon else 0)
            has_addon_check = unit_id in {106, 113, 114, 116}
            if not (block_addon and has_addon_check) and unit_id != 110:
                expected[32 * 64 + 32] = 1
            record("base_addon_dispatch", {"unit_id": unit_id, "addon_tile_blocked": block_addon},
                   uc, expected, call(uc, 0x495D40, unit_id, GRID, [3, SEED, 1]))

    for group in [10, 11]:
        uc = machine()
        cells = [(32, 32, 7), (33, 32, 9), (32, 33, 13), (0, 0, 17), (63, 63, 23)]
        expected = grid(uc, cells)
        put(uc, MAP + 0xC + 2 * (48 * 256 + 49), 8, 2)
        put(uc, MAP + 0x449FE + 8 * 64, group, 2)
        if group != 10:
            expected[32 * 64 + 33] = 0
        record("connectivity_filter", {"candidate_region_group": group}, uc, expected,
               call(uc, 0x497630, GRID, arguments=[SEED]))

    for state, strength1, strength2, flags in [(0, 0, 0, 0), (3, 0, 0, 0),
            (2, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 0x20), (2, 0, 0, 0x40)]:
        for unit_id in [106, 109, 131, 154, 156]:
            uc = machine()
            expected = grid(uc, [(32, 32, 5), (33, 32, 7)])
            put(uc, MAP + 7 * 64 + 0x44A08, NEIGHBORS)
            put(uc, MAP + 7 * 64 + 0x44A03, 1, 1)
            put(uc, MAP + 7 * 64 + 0x44A1D, 0, 1)
            put(uc, NEIGHBORS, 8, 2)
            put(uc, REGIONS + 8 * 0x34 + 5, state, 1)
            put(uc, REGIONS + 8 * 0x34 + 0x1C, strength1)
            put(uc, REGIONS + 8 * 0x34 + 0x20, strength2)
            put(uc, REGIONS + 8 * 0x34 + 8, flags, 1)
            if unit_id not in {106, 131, 154} and (state == 3 or strength1 or strength2 or flags & 0x20):
                expected = bytearray(4096)
            record("neighbor_threat_filter", [unit_id, state, strength1, strength2, flags],
                   uc, expected, call(uc, 0x497910, unit_id, GRID, [3, SEED]))

    for only_mismatch in [False, True]:
        uc = machine()
        cells = [(33, 32, 9)] if only_mismatch else [(32, 32, 7), (33, 32, 9)]
        expected = grid(uc, cells)
        # Tile group0/variant1 refers to megatile1, whose minitiles have height1.
        put(uc, TILE_IDS + 2 * (48 * MAP_SIZE + 49), 1, 2)
        put(uc, CV5 + 0x14 + 2, 1, 2)
        uc.mem_write(VF4 + 32, struct.pack("<H", 2) * 16)
        if not only_mismatch:
            expected[32 * 64 + 33] = 0
        record("elevation_filter_rollback", {"only_mismatching_candidate": only_mismatch},
               uc, expected, call(uc, 0x498430, GRID, arguments=[SEED]))

    # The rectangular removal helper uses object placement dimensions and
    # requested dimensions, not collision borders or a circular distance.
    for obj_width, obj_height in [(1, 1), (2, 2), (4, 3)]:
        for req_width, req_height in [(1, 1), (2, 2), (3, 2)]:
            for margin in [0, 1, 2]:
                uc = machine()
                expected = grid(uc, [], fill=1)
                put(uc, 0x522728 + 109 * 4, req_width * 32, 2)
                put(uc, 0x52272A + 109 * 4, req_height * 32, 2)
                left = 48 - obj_width // 2 - req_width - margin + 1 - 48 + 32
                top = 48 - obj_height // 2 - req_height - margin + 1 - 48 + 32
                width = req_width + obj_width + 2 * margin - 1
                height = req_height + obj_height + 2 * margin - 1
                for row in range(top, top + height):
                    for col in range(left, left + width):
                        if 0 <= row < 64 and 0 <= col < 64:
                            expected[row * 64 + col] = 0
                record("rectangle_mask", [obj_width, obj_height, req_width, req_height, margin],
                       uc, expected, call(uc, 0x4960B0, obj_width, obj_height,
                                         [48, 48, 109, GRID, SEED, margin]))

    for covered, only_covered in [(False, False), (True, False), (True, True)]:
        uc = machine()
        expected = grid(uc, [(32, 32, 7)] if only_covered else [(32, 32, 7), (40, 40, 9)])
        put(uc, 0x521660 + 162 * 4, 0x80000)
        put(uc, 0x665B54, UNIT if covered else 0)
        put(uc, 0x665B50, OUT)
        put(uc, 0x665B58, 32)
        put(uc, 0x665B5C, 32)
        uc.mem_write(OUT, b"\x01" * 4)
        if covered and not only_covered:
            expected[32 * 64 + 32] = 0
        record("power_coverage_filter_rollback", [covered, only_covered], uc, expected,
               call(uc, 0x498520, GRID, 3, [SEED]))

    for unit_id in [106, 111, 113, 114, 116, 117]:
        for only_blocked in [False, True]:
            uc = machine()
            expected = grid(uc, [(34, 33, 7)] if only_blocked else [(34, 33, 7), (32, 32, 9)])
            put(uc, UNIT + 0x64, unit_id, 2)
            put(uc, 0x665B08 + 3 * 4, UNIT)
            if unit_id in {106, 113, 114, 116} and not only_blocked:
                expected[33 * 64 + 34] = 0
            record("addon_space_filter_rollback", [unit_id, only_blocked], uc, expected,
                   call(uc, 0x497D90, 109, GRID, [3, SEED]))

    for walkable in [False, True]:
        for only_blocked in [False, True]:
            uc = machine()
            expected = grid(uc, [(32, 32, 7)] if only_blocked else [(32, 32, 7), (40, 40, 9)])
            # Every tile except the seed has 0x10000; the filter visits all
            # in-map grid coordinates, even if the original grid byte is0.
            uc.mem_write(TILES, struct.pack("<I", 0x10000) * MAP_SIZE * MAP_SIZE)
            put(uc, TILES + 4 * (48 * MAP_SIZE + 48), 0x10000 if walkable else 0)
            if not walkable and not only_blocked:
                expected[32 * 64 + 32] = 0
            record("walkable_filter_rollback", [walkable, only_blocked], uc, expected,
                   call(uc, 0x498820, 109, GRID, [SEED]))

    # Resource/depot spacing distinguishes grounded/completed buildings,
    # currently morphing Hatcheries and neutral resources. Empty output rolls
    # back the entire input snapshot, rather than retaining the last step.
    for existing_id, status, existing_properties, queued in [
            (106, 3, 0x1000, 228), (106, 2, 0x1000, 228),
            (106, 1, 0x1000, 228), (106, 3, 0, 228),
            (132, 2, 0x1000, 228), (133, 2, 0x1000, 228),
            (131, 2, 0x1000, 132), (131, 2, 0x1000, 228),
            (176, 0, 0x2000, 228)]:
        for requested in [109, 125]:
            for owner in [3, 11]:
                uc = machine()
                expected = grid(uc, [(32, 32, 7), (40, 40, 9)])
                put(uc, UNIT + 0x64, existing_id, 2)
                put(uc, UNIT + 0xDC, status)
                put(uc, UNIT + 0x98, queued, 2)
                put(uc, 0x521660 + existing_id * 4, existing_properties)
                put(uc, 0x665B08 + owner * 4, UNIT)
                depot = status & 2 and (status & 1 or existing_id in {132, 133}
                                        or existing_id == 131 and queued in {132, 133, 137, 144})
                affects = bool(existing_properties & 0x2000 or depot and existing_properties & 0x1000)
                if owner == 11:
                    affects = bool(existing_properties & 0x2000 and requested != 125)
                if affects:
                    expected[32 * 64 + 32] = 0
                record("resource_spacing_eligibility", [existing_id, status, existing_properties,
                       queued, requested, owner], uc, expected,
                       call(uc, 0x498230, requested, GRID, [3, SEED]))

    for requested_property in [0, 2]:
        uc = machine()
        expected = grid(uc, [(32, 32, 7)])
        put(uc, UNIT + 0x64, 176, 2)
        put(uc, 0x521660 + 176 * 4, 0x2000)
        put(uc, 0x521660 + 109 * 4, requested_property)
        put(uc, 0x665B08 + 11 * 4, UNIT)
        record("resource_spacing_rollback_or_addon_bypass", [requested_property], uc, expected,
               call(uc, 0x498230, 109, GRID, [3, SEED]))

    for requested in range(111, 163):
        uc = machine()
        cells = [(col, row, 7) for row in range(31, 34) for col in range(31, 34)]
        expected = grid(uc, cells)
        if requested in {111, 113, 124, 125, 155, 160, 162}:
            expected = bytearray(4096)
            expected[32 * 64 + 32] = 7
        record("building_spacing_requested_dispatch", [requested], uc, expected,
               call(uc, 0x497F00, requested, GRID, [3, SEED]))

    for existing in range(111, 163):
        uc = machine()
        expected = grid(uc, [(32, 32, 7), (40, 40, 9)])
        put(uc, UNIT + 0x64, existing, 2)
        put(uc, 0x665B08 + 3 * 4, UNIT)
        if existing in {111, 113, 124, 125, 143, 155, 160, 162}:
            expected[32 * 64 + 32] = 0
        record("building_spacing_existing_dispatch", [existing], uc, expected,
               call(uc, 0x497F00, 109, GRID, [3, SEED]))

    for missing_neighbor in range(8):
        uc = machine()
        neighbors = [(31, 31), (32, 31), (33, 31), (31, 32), (33, 32), (31, 33), (32, 33), (33, 33)]
        cells = [(32, 32, 7), (48, 48, 9)] + [(c, r, 1) for i, (c, r) in enumerate(neighbors) if i != missing_neighbor]
        expected = grid(uc, cells)
        # The unrelated singleton also fails erosion. The filter restores the
        # whole original snapshot because every candidate becomes zero.
        record("eight_neighbor_erosion_rollback", [missing_neighbor], uc, expected,
               call(uc, 0x497F00, 111, GRID, [3, SEED]))

    for existing, requested in [(124, 124), (125, 124), (162, 124), (143, 143),
            (144, 143), (146, 143), (143, 124), (131, 124)]:
        for level, side in enumerate([17, 13, 9, 5]):
            uc = machine()
            expected = grid(uc, [], fill=1)
            put(uc, UNIT + 0x64, existing, 2)
            put(uc, 0x665B08 + 3 * 4, UNIT)
            applies = existing in {124, 125, 162} and existing == requested or existing in {143, 144, 146}
            if applies:
                origin = 31 - side // 2
                for row, text in enumerate(patterns[str(side)]):
                    for col, char in enumerate(text):
                        if char == "x":
                            expected[(origin + row) * 64 + origin + col] = 0
            record("defense_pattern", [existing, requested, level, side], uc, expected,
                   call(uc, 0x498600, GRID, 3, [SEED, requested, level]))

    for level in range(4):
        uc = machine()
        # The center of all four masks is cell(31,31), one cell before the
        # integer center index32. All levels erase it, then restore on exhaustion.
        expected = grid(uc, [(31, 31, 7)])
        put(uc, UNIT + 0x64, 124, 2)
        put(uc, 0x665B08 + 3 * 4, UNIT)
        record("defense_pattern_recursive_rollback", [level], uc, expected,
               call(uc, 0x498600, GRID, 3, [SEED, 124, level]))

    def directional_model(before):
        current = bytearray(before)
        for col, row, dc, dr in directional:
            for _ in range(64):
                current[row * 64 + col] = 0
                col, row = col + dc, row + dr
            if not any(current):
                return bytearray(before)
        return current

    for cells in [[], [(32, 32, 7)], [(40, 40, 9)], [(32, 32, 7), (40, 40, 9)],
            [(c, r, 1) for r in range(29, 36) for c in range(29, 36)]]:
        uc = machine()
        original = grid(uc, cells)
        expected = directional_model(original)
        record("directional_line_rollback", cells, uc, expected,
               call(uc, 0x498930, GRID, arguments=[SEED]))

    def approximate(dx, dy):
        large, small = max(abs(dx), abs(dy)), min(abs(dx), abs(dy))
        if small <= large >> 2:
            return large
        a, b = large >> 4, (small * 3) >> 3
        return large - a + b - (a >> 2) + (b >> 5)

    def pipeline_model(requested, size):
        current = bytearray(4096)
        if requested in {110, 149, 157}:
            return current
        start, end = 32 - size // 2, 32 - size // 2 + size
        for row in range(max(0, start), min(64, end)):
            for col in range(max(0, start), min(64, end)):
                if approximate((col - 32) * 32, (row - 32) * 32) // 32 <= size:
                    current[row * 64 + col] = 1
        if requested in {111, 113, 155, 160}:
            before = bytearray(current)
            for row in range(64):
                for col in range(64):
                    if before[row * 64 + col] and not (0 < row < 63 and 0 < col < 63 and
                            all(before[(row + dr) * 64 + col + dc] for dr in [-1, 0, 1]
                                for dc in [-1, 0, 1] if dc or dr)):
                        current[row * 64 + col] = 0
            if not any(current):
                current = before
        return current

    for requested in list(range(106, 157)) + [157, 162]:
        uc = machine()
        grid(uc, [])
        uc.mem_write(TILES, struct.pack("<I", 0x10000) * MAP_SIZE * MAP_SIZE)
        expected = pipeline_model(requested, 3)
        record("connected_grid_pipeline", {"requested": requested, "size": 3,
               "profile": "synthetic ordinary properties; empty indexed/player lists; one region"},
               uc, expected, call(uc, 0x495BB0, requested, GRID, [3, SEED, 3]))

    for requested in [109, 111, 124, 125, 131, 143, 154, 156, 162]:
        for size in [0, 1, 2, 4, 7]:
            uc = machine()
            grid(uc, [])
            uc.mem_write(TILES, struct.pack("<I", 0x10000) * MAP_SIZE * MAP_SIZE)
            expected = pipeline_model(requested, size)
            record("pipeline_window", [requested, size], uc, expected,
                   call(uc, 0x495BB0, requested, GRID, [3, SEED, size]))

    for requested in [106, 107, 108, 109, 110, 111, 113, 115, 117, 118, 120, 124,
                      125, 131, 143, 154, 162]:
        for size in [0, 3, 7]:
            uc = machine()
            grid(uc, [])
            uc.mem_write(TILES, struct.pack("<I", 0x10000) * MAP_SIZE * MAP_SIZE)
            normalized = {107: 106, 108: 106, 115: 114, 117: 116, 118: 116, 120: 113}.get(requested, requested)
            candidates = pipeline_model(normalized, size)
            if requested not in {106, 131, 154}:
                candidates = directional_model(candidates)
            ranked = [(approximate((col - 32) * 32, (row - 32) * 32) // 32, row, col)
                      for row in range(64) for col in range(64) if candidates[row * 64 + col]]
            expected_result = 1 if ranked else 0
            expected_output = [0, 0]
            if ranked:
                _, row, col = min(ranked)
                expected_output = [(16 + col) * 32 + 16, (16 + row) * 32 + 16]
            run = call(uc, 0x494FC0, UNIT, requested, [SEED, OUT, size])
            result, _, _ = run
            observed_output = [get(uc, OUT, 2), get(uc, OUT + 2, 2)]
            assert result == expected_result and observed_output == expected_output
            assert get(uc, 0x6A2E80) == 0
            # The internal candidate grid lives on the original stack; the
            # external GRID remains untouched and serves as a memory guard.
            record("complete_placement_search", [requested, normalized, size], uc, bytearray(4096),
                   run, {"returned": result, "position": observed_output,
                         "expected_position": expected_output})

    for throttle in [0, 1]:
        for already_blocked in [0, 1]:
            for requested, all_blocked in [(109, False), (109, True), (124, True)]:
                uc = machine()
                grid(uc, [])
                uc.mem_write(TILES, struct.pack("<I", 0x20000000 if all_blocked else 0x10000) * MAP_SIZE * MAP_SIZE)
                put(uc, 0x67D279 + 3 * 0x4E8, already_blocked, 1)
                expected_result = int(not all_blocked and not (throttle and already_blocked))
                run = call(uc, 0x496240, UNIT, requested, [OUT, throttle])
                assert run[0] == expected_result
                expected_block = already_blocked or throttle and not expected_result
                assert get(uc, 0x67D279 + 3 * 0x4E8, 1) == expected_block
                record("unit_centered_throttle", [requested, throttle, already_blocked, all_blocked],
                       uc, bytearray(4096), run, {"returned": run[0], "throttle_after": int(expected_block)})

    def indexed_units(uc, entries):
        xs, ys = [], []
        for index, (x, y, status, owner) in enumerate(entries, 1):
            address, sprite = 0x5DA250 + index * 0x150, SPRITE + index * 0x28
            put(uc, address + 0xC, sprite)
            put(uc, address + 0xDC, status)
            put(uc, address + 0x4C, owner, 1)
            put(uc, address + 0x4D, 1, 1)
            put(uc, address + 0x64, 160, 2)
            put(uc, sprite + 0x14, x, 2)
            put(uc, sprite + 0x16, y, 2)
            xs.extend([(x - 16, index, 0), (x + 16, index, 1)])
            ys.extend([(y - 16, index, 0), (y + 16, index, 1)])
        for axis, (table_va, values) in enumerate([(0x5C99B8, xs), (0x5D03F8, ys)]):
            for slot, (coordinate, index, side) in enumerate(sorted(values)):
                uc.mem_write(table_va + slot * 8, struct.pack("<Ii", index, coordinate))
                put(uc, 0x5DA250 + index * 0x150 + 0x13C + axis * 8 + side * 4, slot)
        put(uc, 0x5C99B0, len(xs))
        uc.mem_write(0x543228, struct.pack("<4H", 0, 0, 3072, 3072))

    for status in [0, 1, 0x400, 0x401, 0x20400]:
        for owner in [3, 4]:
            uc = machine()
            grid(uc, [])
            indexed_units(uc, [(1600, 1600, status, owner)])
            run = call(uc, 0x433220, 3, arguments=[SEED])
            expected = 0x5DA3A0 if status & 0x400 and owner == 3 else 0
            assert run[0] == expected
            record("nearest_disabled_unit_gate", [status, owner], uc, bytearray(4096),
                   run, {"returned_unit": hex(run[0])})

    for entries, expected_index in [
            ([], 0), ([(1472, 1536, 0x400, 3), (1600, 1536, 0x400, 3)], 1),
            ([(1472, 1536, 0x400, 4), (1600, 1536, 0x400, 3)], 2),
            ([(1472, 1536, 0, 3), (1600, 1536, 0x400, 3)], 2),
            ([(1408, 1536, 0x400, 3), (1568, 1536, 0x400, 3)], 2),
            ([(1536, 1472, 0x400, 3), (1536, 1600, 0x400, 3)], 2),
            ([(1536, 1536, 0x400, 3), (1536, 1536, 0x400, 3)], 2),
        ]:
        uc = machine()
        grid(uc, [])
        indexed_units(uc, entries)
        run = call(uc, 0x433220, 3, arguments=[SEED])
        expected = 0x5DA250 + expected_index * 0x150 if expected_index else 0
        assert run[0] == expected, (entries, hex(run[0]), hex(expected))
        record("nearest_disabled_unit_order", entries, uc, bytearray(4096), run,
               {"returned_unit_index": expected_index})

    for found in [False, True]:
        uc = machine()
        grid(uc, [])
        uc.mem_write(TILES, struct.pack("<I", 0x10000) * MAP_SIZE * MAP_SIZE)
        indexed_units(uc, [(1600, 1600, 0x20400, 3)] if found else [])
        run = call(uc, 0x494FC0, UNIT, 156, [SEED, OUT, 3])
        expected = [1616, 1616] if found else [1552, 1552]
        assert run[0] == 1 and [get(uc, OUT, 2), get(uc, OUT + 2, 2)] == expected
        assert get(uc, 0x6A5DA0) == (0x5DA3A0 if found else 0)
        record("pylon_recenter_complete_search", [found], uc, bytearray(4096), run,
               {"position": expected, "recenter_unit": hex(get(uc, 0x6A5DA0))})

    for anchor_tile in [48, 52, 60]:
        uc = machine()
        grid(uc, [])
        uc.mem_write(TILES, struct.pack("<I", 0x10000) * MAP_SIZE * MAP_SIZE)
        put(uc, 0x6A2E84, 1)
        put(uc, 0x6A5D70, 7, 2)
        put(uc, MAP + 7 * 64 + 0x44A0C, anchor_tile * 32 * 256)
        put(uc, MAP + 7 * 64 + 0x44A10, anchor_tile * 32 * 256)
        run = call(uc, 0x494FC0, UNIT, 125, [SEED, OUT, 3])
        tile = 48 if anchor_tile == 48 else 49
        expected = [tile * 32 + 16] * 2
        assert run[0] == 1 and [get(uc, OUT, 2), get(uc, OUT + 2, 2)] == expected
        record("bunker_one_anchor_search", [anchor_tile], uc, bytearray(4096), run,
               {"position": expected, "all_points_use_region": 7})

    for anchor_count in [0, 1, 2, 3, 24]:
        uc = machine()
        grid(uc, [])
        put(uc, 0x6A2E84, anchor_count)
        for index in range(anchor_count):
            put(uc, 0x6A5D70 + index * 2, 7 + index, 2)
            put(uc, MAP + (7 + index) * 64 + 0x44A0C, (1536 + index * 32) * 256)
            put(uc, MAP + (7 + index) * 64 + 0x44A10, 1536 * 256)
        put(uc, OUT, 0xAABBCCDD)
        run = call(uc, 0x495480, 3, OUT)
        assert run[0] == int(anchor_count != 0)
        assert get(uc, OUT) == (7 if anchor_count else 0xAABBCCDD)
        record("defense_anchor_empty_list", [anchor_count], uc, bytearray(4096),
               run, {"returned": run[0], "selected_region": get(uc, OUT)})

    for existing in [124, 125, 143, 144, 146, 162]:
        for position in [1536, 2048]:
            uc = machine()
            grid(uc, [])
            put(uc, 0x6A2E84, 3)
            for index, x in enumerate([1536, 1792, 2048]):
                put(uc, 0x6A5D70 + index * 2, 7 + index, 2)
                put(uc, MAP + (7 + index) * 64 + 0x44A0C, x * 256)
                put(uc, MAP + (7 + index) * 64 + 0x44A10, 1536 * 256)
            put(uc, 0x665B08 + 3 * 4, UNIT)
            put(uc, UNIT + 0x64, existing, 2)
            put(uc, SPRITE + 0x14, position, 2)
            run = call(uc, 0x495480, 3, OUT)
            expected_region = 9 if existing in {125, 143, 146} and position == 1536 else 7
            assert run[0] == 1 and get(uc, OUT) == expected_region
            record("defense_anchor_type_and_maximum", [existing, position], uc, bytearray(4096),
                   run, {"selected_region": get(uc, OUT), "all_points_resolve_to_region": 7})

    uc = machine()
    grid(uc, [])
    put(uc, 0x6A2E84, 3)
    for index, x in enumerate([1536, 1792, 2048]):
        put(uc, 0x6A5D70 + index * 2, 7 + index, 2)
        put(uc, MAP + (7 + index) * 64 + 0x44A0C, x * 256)
        put(uc, MAP + (7 + index) * 64 + 0x44A10, 1536 * 256)
    second, second_sprite = UNIT + 0x150, SPRITE + 0x28
    put(uc, 0x665B08 + 3 * 4, UNIT)
    put(uc, UNIT + 0x64, 125, 2)
    put(uc, UNIT + 0x6C, second)
    put(uc, second + 0xC, second_sprite)
    put(uc, second + 0x4D, 1, 1)
    put(uc, second + 0x64, 146, 2)
    put(uc, second_sprite + 0x14, 2048, 2)
    put(uc, second_sprite + 0x16, 1536, 2)
    run = call(uc, 0x495480, 3, OUT)
    assert run[0] == 1 and get(uc, OUT) == 8
    record("defense_anchor_minimum_then_maximum", [125, 146], uc, bytearray(4096),
           run, {"nearest_defense_distances": [0, 8, 0], "selected_region": 8})

    def load_stock(uc):
        for index, entry in enumerate(stock):
            put(uc, 0x521660 + index * 4, entry.special_ability_flags)
            put(uc, 0x522728 + index * 4, entry.staredit_placement_size.width, 2)
            put(uc, 0x52272A + index * 4, entry.staredit_placement_size.height, 2)
            put(uc, 0x523C40 + index, entry.staredit_group_flags, 1)
            if 106 <= index <= 201:
                put(uc, 0x522910 + index * 4, entry.addon_position.x, 2)
                put(uc, 0x522912 + index * 4, entry.addon_position.y, 2)

    if stock is not None:
        for requested in range(106, 202):
            for tile_flags, powered in [(0x10000, False), (0x10000, True),
                    (0x40410000, False), (0x40410000, True), (0x20000000, True)]:
                uc = machine()
                load_stock(uc)
                expected = grid(uc, [])
                uc.mem_write(TILES, struct.pack("<I", tile_flags) * MAP_SIZE * MAP_SIZE)
                # Center matches the requested building center, making the
                # supplied power byte unambiguous for each actual DAT footprint.
                put(uc, SPRITE + 0x14, 1536 + stock[requested].staredit_placement_size.width // 2, 2)
                put(uc, SPRITE + 0x16, 1536 + stock[requested].staredit_placement_size.height // 2, 2)
                put(uc, 0x665B54, UNIT if powered else 0)
                put(uc, 0x665B50, OUT)
                put(uc, 0x665B58, 32)
                put(uc, 0x665B5C, 32)
                uc.mem_write(OUT, b"\x01" * 4)
                props = stock[requested].special_ability_flags
                if requested in {110, 149, 157} or props & 0x80000 and not powered:
                    valid = False
                elif props & 0x20000:
                    valid = bool(tile_flags & 0x40000000)
                else:
                    mask = 0x20800000 | (0 if stock[requested].staredit_group_flags & 1 else 0x40000000)
                    valid = not bool(tile_flags & mask)
                if requested in {106, 113, 114, 116} and tile_flags & 0x60800000:
                    valid = False
                if valid:
                    expected[32 * 64 + 32] = 1
                record("stock_building_base_grid", [requested, tile_flags, powered,
                       stock[requested].staredit_placement_size.width,
                       stock[requested].staredit_placement_size.height, props], uc, expected,
                       call(uc, 0x495D40, requested, GRID, [3, SEED, 1]))

        for requested, parent in [(107, 106), (108, 106), (115, 114),
                                 (117, 116), (118, 116), (120, 113)]:
            for blocked in [False, True]:
                uc = machine()
                load_stock(uc)
                grid(uc, [])
                uc.mem_write(TILES, struct.pack("<I", 0x20000000 if blocked else 0x10000) * MAP_SIZE * MAP_SIZE)
                put(uc, UNIT + 0x64, parent, 2)
                put(uc, UNIT + 0xDC, 3)
                run = call(uc, 0x494FC0, UNIT, requested, [SEED, OUT, 3])
                half_width = stock[parent].staredit_placement_size.width // 2
                half_height = stock[parent].staredit_placement_size.height // 2
                expected_output = [0, 0] if blocked else [
                    (48 - trunc(half_width, 32)) * 32 + half_width,
                    (48 + trunc(-half_height, 32)) * 32 + half_height]
                observed_output = [get(uc, OUT, 2), get(uc, OUT + 2, 2)]
                assert run[0] == int(not blocked) and observed_output == expected_output
                record("stock_addon_fixed_placement", [requested, parent, blocked], uc,
                       bytearray(4096), run, {"returned": run[0], "position": observed_output,
                                            "expected_position": expected_output})

    result = {"target_sha256": TARGET_HASH, "version": "1.17.0.1",
        "probe_script_sha256": own_hash,
        "dependency_hashes": {Path(placement.__file__).name: dependency_hash},
        "runtime": {"python": platform.python_version(), "host_machine": platform.machine(),
                    "unicorn": unicorn.__version__, "capstone": capstone.__version__},
        "scope": "isolated complete original AI grid calls with actual callees; synthetic world inputs",
        "allowlist": [[hex(lo), hex(hi)] for lo, hi in ALLOW],
        "instruction_limit_per_call": 3000000,
        "case_count": len(cases), "passed_cases": len(cases), "failed_cases": 0,
        "all_expected_results_matched": True, "cases": cases,
        "kind_counts": dict(Counter(x["kind"] for x in cases)),
        "stock_profile": stock_profile,
        "complete_original_game_executed": False,
        "limits": "No terrain/index/list production, arbitrary connected-region paths or construction order progression."}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"cases": len(cases), "all_matched": True, "out": str(args.out)}))


if __name__ == "__main__":
    main()
