#!/usr/bin/env python3
"""Recover bounded original 1.17 terrain-region generation contracts.

Default mode only disassembles the known executable. Explicit execution uses
private memory, synthetic terrain and reviewed original instructions. No import,
allocator substitute, complete game, Windows API or guest host I/O is executed.
Allocation-dependent VF4/walk/raster routines have explicitly labeled fragments;
the region graph builder and listed graph helpers return through their actual ABI.
"""

import argparse
from collections import Counter
import hashlib
import itertools
import json
from pathlib import Path
import platform
import random
import struct

import capstone
import pefile

TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
MAP, RECT, ARRAY = 0x800000, 0x8F0000, 0x8F1000
PATH_CONTEXT = 0x8F9000
STACK, STOP, WALK = 0x900000, 0xA00000, 0xB00000
FLAGS, TILE_IDS, VF4, CONVERTED = 0xC00000, 0xC40000, 0xC60000, 0xC80000
MEGA_FLAGS = 0xC90000
BLOCKS = {
    "region_builder": (0x48F060, 0x48F0DB),
    "region_cut_ranks": (0x48F0E0, 0x48F1A1),
    "split_cells": (0x48F1B0, 0x48F837),
    "refresh_regions": (0x48F870, 0x48F8DC),
    "initial_rectangles": (0x48F8E0, 0x48FC78),
    "merge_regions": (0x48FC80, 0x48FEAA),
    "reindex_regions": (0x48FEB0, 0x48FFE9),
    "expand_region": (0x48FFF0, 0x490178),
    "fill_rectangle": (0x490180, 0x4901EF),
    "connectivity_groups": (0x4901F0, 0x4902D1),
    "build_neighbor_arrays": (0x4902E0, 0x490329),
    "region_counts_bounds": (0x490330, 0x4904F7),
    "enumerate_region_neighbors": (0x490500, 0x490A47),
    "eight_adjacent_cells": (0x490A60, 0x490B76),
    "find_rectangle": (0x490B80, 0x490D78),
    "outer_region_init": (0x490D80, 0x490E1B),
    "group_recursive_visit": (0x490E20, 0x490E8F),
    "tile_flag_classes": (0x490E90, 0x490F44),
    "bottom_region": (0x490F50, 0x491053),
    "order_neighbor_classes": (0x4910C0, 0x4911D8),
    "region_at_pixel": (0x481B10, 0x481B98),
    "region_literal_1ffd": (0x481BB0, 0x481BD1),
    "vf4_walk_conversion": (0x48AD80, 0x48AEAC),
    "vf4_megatile_flags": (0x445540, 0x44566F),
    "walk_grid_init": (0x48B1C0, 0x48B5F2),
    "native_memset": (0x4D0C60, 0x4D0CF0),
    "native_stack_probe": (0x4D2200, 0x4D222B),
    "region_long_path": (0x491300, 0x4919B0),
    "heap_change_priority": (0x491A70, 0x491B11),
    "heap_sift_down": (0x491B20, 0x491B9E),
    "approximate_distance": (0x405CF0, 0x405D3D),
    "region_group": (0x481C90, 0x481CA4),
}
TABLES = {"split_direction_eight": (0x48F838, 8),
          "split_direction_four": (0x48F858, 4),
          "neighbor_direction": (0x490A48, 4)}


def class_from_flags(value):
    if value & 0x410000 == 0:
        return 0x1FFD
    v = value & 0x6010000
    return {0: 0x1FFD, 0x2010000: 0x1FF9,
            0x4010000: 0x1FFA}.get(v, 0x1FFB)


def megatile_flags_reference(record):
    walk, middle, high, very_high = [sum(bool(value & bit) for value in record)
                                   for bit in (1, 2, 4, 8)]
    result = 0x10000 if walk > 12 else 0x40000
    if 0 < walk < 16:
        result |= 0x20000000
    if high >= 12:
        result |= 0x4000000
    elif middle + high >= 12:
        result |= 0x2000000
    if very_high:
        result |= 0x1000000
    return result


def walk_reference(width, height, ids, records):
    """Independent flag-bit and four-neighbor definition, on valid dimensions."""
    rows = [[0 if records[ids[y // 4 * width + x // 4] & 0x7FFF]
              [(y % 4) * 4 + x % 4] & 1 else 0x80
             for x in range(width * 4)] for y in range(height * 4)]
    for y in range(height * 4 - 8, height * 4):
        for x in range(width * 4):
            if x < 20 or x >= width * 4 - 20 or y >= height * 4 - 4:
                rows[y][x] = 0x80
    for y in range(height * 4):
        for x in range(width * 4):
            if rows[y][x] & 0x80:
                continue
            for dx, dy, bit in [(0, -1, 1), (1, 0, 2), (0, 1, 4), (-1, 0, 8)]:
                nx, ny = x + dx, y + dy
                if not 0 <= nx < width * 4 or not 0 <= ny < height * 4 or rows[ny][nx] & 0x80:
                    rows[y][x] |= bit
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--static-out", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--execute-original-code", action="store_true")
    args = parser.parse_args()
    own_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        parser.error("Unknown executable hash; reviewed addresses do not apply")
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    assert base == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    witnesses = {}
    for name, (lo, hi) in BLOCKS.items():
        rows = list(md.disasm(pe.get_data(lo - base, hi - lo), lo))
        if not rows or rows[-1].address + rows[-1].size != hi:
            parser.error("Invalid instruction boundary: " + name)
        witnesses[name] = [{"va": hex(i.address), "bytes": i.bytes.hex(),
                            "mnemonic": i.mnemonic, "operands": i.op_str} for i in rows]
    tables = {name: [hex(x) for x in struct.unpack("<" + "I" * count,
              pe.get_data(va - base, count * 4))] for name, (va, count) in TABLES.items()}
    imports = [{"dll": group.dll.decode("ascii"), "ordinal": entry.ordinal,
                "name": entry.name.decode("ascii") if entry.name else None,
                "iat_va": hex(entry.address)}
               for group in pe.DIRECTORY_ENTRY_IMPORT for entry in group.imports
               if entry.address in (0x4EB55C, 0x4EB558)]
    if args.static_out:
        args.static_out.parent.mkdir(parents=True, exist_ok=True)
        args.static_out.write_text(json.dumps({"target_sha256": TARGET_HASH,
            "version": "1.17.0.1", "evidence": "static; analytical labels",
            "witnesses": witnesses, "jump_tables_excluded_from_executable_allowlist": tables,
            "outer_allocation_imports_not_executed": imports}, indent=2) + "\n")
    if not args.execute_original_code:
        print(json.dumps({"blocks": len(BLOCKS), "target_sha256": TARGET_HASH,
                          "mode": "static only; execution needs explicit flag"}))
        return
    if not args.out:
        parser.error("--out required for execution evidence")
    import unicorn
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX,
        UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_EBX)
    image = pe.get_memory_mapped_image()
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    reviewed = {(int(row["va"], 16), len(bytes.fromhex(row["bytes"])))
                for rows in witnesses.values() for row in rows}
    entries = {v[0]: name for name, v in BLOCKS.items()}
    cases = []

    def put(uc, address, value, width=4):
        uc.mem_write(address, (value & ((1 << (width * 8)) - 1)).to_bytes(width, "little"))

    def get(uc, address, width=4):
        return int.from_bytes(uc.mem_read(address, width), "little")

    def raster(uc, rows):
        for y, row in enumerate(rows):
            uc.mem_write(MAP + 0xC + y * 512, struct.pack("<" + "H" * len(row), *row))

    def region(index):
        return MAP + 0x449FC + index * 64

    def machine(width=8, height=8):
        assert 5 <= width <= 256 and 2 <= height <= 256
        uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        for address, size in [(base, image_size), (MAP, 0x100000),
                              (STACK, 0x10000), (STOP, 0x1000),
                              (WALK, 0x100000), (FLAGS, 0x100000)]:
            uc.mem_map(address, size)
        uc.mem_write(base, image)
        put(uc, 0x69B584, MAP)
        put(uc, 0x69B55C, WALK)
        put(uc, 0x666454, FLAGS)
        put(uc, 0x54323C, TILE_IDS)
        put(uc, 0x66644C, VF4)
        put(uc, 0x5264B4, width, 2)
        put(uc, 0x5264B6, height, 2)
        # Select the real scalar memset branch; no replacement implementation.
        put(uc, 0x6D319C, 0)
        put(uc, 0x50F310, 0)
        put(uc, RECT, 0)
        put(uc, RECT + 4, width | height << 16)
        return uc

    def execute(uc, address, end, expected_sp, scope):
        count = 0
        trace = Counter()
        branches = Counter()
        interesting = {0x48F7C2: "split_raster_write", 0x48FC29: "region_limit_error",
                       0x48FB69: "small_rectangle_expand_all"}
        def check(cpu, va, size, unused):
            nonlocal count
            count += 1
            if va == end:
                cpu.emu_stop()
                return
            if (va, size) not in reviewed:
                raise RuntimeError("Unreviewed instruction " + hex(va))
            if va in entries:
                trace[entries[va]] += 1
            if va in interesting:
                branches[interesting[va]] += 1
        hook = uc.hook_add(unicorn.UC_HOOK_CODE, check)
        try:
            uc.emu_start(address, end + 1, count=5000000)
            if uc.reg_read(UC_X86_REG_EIP) != end or uc.reg_read(UC_X86_REG_ESP) != expected_sp:
                raise RuntimeError("Original code did not complete expected ABI/fragment: " + hex(address))
        finally:
            uc.hook_del(hook)
        return {"scope": scope, "entry": hex(address), "stop": hex(end),
                "returned": uc.reg_read(UC_X86_REG_EAX), "instructions": count,
                "calls": dict(trace), "branches": dict(branches)}

    def call(uc, address, ecx=MAP, edx=0, args=()):
        sp = STACK + 0xF000
        uc.mem_write(sp, struct.pack("<" + "I" * (1 + len(args)), STOP, *args))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        uc.reg_write(UC_X86_REG_EDX, edx)
        return execute(uc, address, STOP, sp + 4 + len(args) * 4, "complete original function")

    def frame(uc, local_size, saved_registers):
        bp = STACK + 0xE000
        uc.mem_write(bp - 0x500, bytes(0x510))
        put(uc, bp + 4, STOP)
        uc.reg_write(UC_X86_REG_EBP, bp)
        uc.reg_write(UC_X86_REG_ESP, bp - local_size - saved_registers * 4)
        return bp

    def native_vf4_fragment(uc, records):
        assert 0 < len(records) <= 4096 and all(len(row) == 16 for row in records)
        put(uc, 0x666448, len(records), 2)
        uc.mem_write(VF4, b"".join(struct.pack("<16H", *row) for row in records))
        sp = STACK + 0xF000
        uc.mem_write(sp, struct.pack("<4I", 0, 0, 0, STOP))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_EAX, CONVERTED)
        result = execute(uc, 0x48AD9F, STOP, sp + 16, "post-allocation VF4 conversion fragment")
        expected = bytes(0 if value & 1 else 128 for row in records for value in row)
        actual = bytes(uc.mem_read(CONVERTED, len(expected)))
        assert actual == expected
        return result

    def native_walk_fragments(uc, width, height, ids, records):
        uc.mem_write(TILE_IDS, struct.pack("<" + "H" * len(ids), *ids))
        vf4_result = native_vf4_fragment(uc, records)
        bp = frame(uc, 0x10, 3)
        put(uc, bp - 0x10, WALK)
        put(uc, bp - 0xC, 1024 - width * 4)
        uc.reg_write(UC_X86_REG_EBX, WALK)
        uc.reg_write(UC_X86_REG_EAX, CONVERTED)
        copied = execute(uc, 0x48B20C, 0x48B2A4, bp - 0x1C,
                         "post-allocation/pre-free walk-grid copy fragment")
        # Execute a separate original suffix after SFree; never call or fake it.
        finalized = execute(uc, 0x48B2B8, STOP, bp + 8,
                            "post-free walk-boundary/direction fragment")
        expected = walk_reference(width, height, ids, records)
        actual = [list(uc.mem_read(WALK + y * 1024, width * 4)) for y in range(height * 4)]
        assert actual == expected
        return [vf4_result, copied, finalized], actual

    def native_megatile_flags_fragment(uc, records):
        assert len(records) <= 4096 and all(len(row) == 16 for row in records)
        put(uc, 0x666448, len(records), 2)
        uc.mem_write(VF4, b"".join(struct.pack("<16H", *row) for row in records))
        bp = frame(uc, 0x14, 0)
        uc.reg_write(UC_X86_REG_EAX, MEGA_FLAGS)
        result = execute(uc, 0x445562, STOP, bp + 8,
                         "post-allocation megatile flag generation fragment")
        actual = list(struct.unpack("<" + "I" * len(records), uc.mem_read(MEGA_FLAGS, len(records) * 4)))
        assert actual == [megatile_flags_reference(row) for row in records]
        assert get(uc, 0x666450) == MEGA_FLAGS
        return result, actual

    def native_classes_fragment(uc, width, height, flags):
        uc.mem_write(FLAGS, struct.pack("<" + "I" * len(flags), *flags))
        bp = frame(uc, 0xC, 2)
        uc.reg_write(UC_X86_REG_ESI, FLAGS)
        result = execute(uc, 0x490E9F, STOP, bp + 8,
                         "post-walk-initialization tile-class fragment")
        actual = [[get(uc, MAP + 0xC + y * 512 + x * 2, 2)
                   for x in range(width)] for y in range(height)]
        expected = [[class_from_flags(flags[y * width + x]) for x in range(width)] for y in range(height)]
        assert actual == expected
        return result, actual

    def snapshot(uc, width=None, height=None):
        n = get(uc, MAP, 2)
        assert n <= 5000
        result = []
        for index in range(n):
            a = region(index)
            count, pointer = get(uc, a + 7, 1), get(uc, a + 0xC)
            neighbors = list(struct.unpack("<" + "H" * count, uc.mem_read(pointer, count * 2))) if count else []
            result.append({"id": index, "flags": hex(get(uc, a, 2)), "group": get(uc, a + 2, 2),
                "tiles": get(uc, a + 4, 2), "ground_neighbors": get(uc, a + 6, 1),
                "neighbors": neighbors, "center_fixed8": list(struct.unpack("<2i", uc.mem_read(a + 0x10, 8))),
                "bounds_pixels": list(struct.unpack("<4h", uc.mem_read(a + 0x18, 8))),
                "cut_rank": get(uc, a + 0x20, 1), "zero_neighbors": get(uc, a + 0x21, 1)})
        out = {"regions": result, "split_count": (get(uc, MAP + 8) - (MAP + 0x2000C)) // 6}
        if width and height:
            out["raster"] = [[get(uc, MAP + 0xC + y * 512 + x * 2, 2)
                              for x in range(width)] for y in range(height)]
            out["splits"] = [list(struct.unpack("<3H", uc.mem_read(MAP + 0x2000C + i * 6, 6)))
                             for i in range(out["split_count"])]
        return out

    def record(name, runs, observed, expected=None, inputs=None):
        if expected is not None:
            assert observed == expected, (name, observed, expected)
        cases.append({"name": name, "inputs": inputs, "runs": runs, "observed": observed,
                      "expected": expected, "oracle": "independent expected contract" if expected is not None
                      else "structural assertions plus recorded original observation", "passed": True})

    for width, height in [(8, 8), (16, 12), (32, 16), (256, 256)]:
        uc = machine(width, height)
        run = call(uc, 0x490F50)
        a = region(0)
        observed = {"count": get(uc, MAP, 2), "flags": get(uc, a, 2),
                    "group": get(uc, a + 2, 2), "tiles": get(uc, a + 4, 2),
                    "center": list(struct.unpack("<2I", uc.mem_read(a + 0x10, 8))),
                    "bounds": list(struct.unpack("<4H", uc.mem_read(a + 0x18, 8)))}
        record("bottom_{}x{}".format(width, height), [run], observed,
               {"count": 1, "flags": 0x1FFD, "group": 0x4000, "tiles": width,
                "center": [(width * 16 + 16) * 256, (height * 32 - 16) * 256],
                "bounds": [0, (height - 1) * 32, width * 32, height * 32]})

    rng = random.Random(11717)
    for iteration in range(40):
        uc = machine()
        for index, flags in enumerate([0x1FFD, 0x1FFB, 0x1FFD, 0x1FF9, 0x1FFA]):
            put(uc, region(index), flags, 2)
        values = [rng.randrange(5) for _ in range(iteration % 12)]
        uc.mem_write(ARRAY, struct.pack("<" + "H" * len(values), *values))
        put(uc, region(1) + 0xC, ARRAY)
        put(uc, region(1) + 7, len(values), 1)
        put(uc, region(1) + 0x21, 0xCC, 1)
        run = call(uc, 0x4910C0, edx=region(1))
        ground = [x for x in values if x in (1, 3, 4)]
        blocked = [x for x in values if x == 2]
        zeros = [x for x in values if x == 0]
        observed = {"neighbors": list(struct.unpack("<" + "H" * len(values), uc.mem_read(ARRAY, len(values) * 2))),
                    "ground": get(uc, region(1) + 6, 1), "total": get(uc, region(1) + 7, 1),
                    "zeros": get(uc, region(1) + 0x21, 1)}
        record("neighbor_partition_{}".format(iteration), [run], observed,
               {"neighbors": ground + blocked + zeros, "ground": len(ground),
                "total": len(values), "zeros": len(zeros) if zeros else 0xCC}, {"neighbors": values})

    for unique, tile_count, ground_skip in itertools.product(range(9), (0, 3, 4, 5), (0, 1)):
        uc = machine()
        put(uc, MAP, unique + 1, 2)
        values = list(range(1, unique + 1))
        put(uc, region(0) + 6, ground_skip if unique else 0, 1)
        put(uc, region(0) + 7, unique, 1)
        put(uc, region(0) + 0xC, ARRAY)
        uc.mem_write(ARRAY, struct.pack("<" + "H" * unique, *values))
        for index in values:
            put(uc, region(index) + 2, index, 2)
            put(uc, region(index) + 4, tile_count, 2)
        run = call(uc, 0x48F0E0)
        qualifying = unique - (ground_skip if unique else 0) if tile_count >= 4 else 0
        expected = min(qualifying, 5) if qualifying >= 2 else 0
        record("cut_rank_{}_{}_{}".format(unique, tile_count, ground_skip), [run],
               get(uc, region(0) + 0x20, 1), expected)

    for iteration in range(50):
        uc = machine()
        n = 9
        put(uc, MAP, n, 2)
        flags = [rng.choice((0x1FFD, 0x1FFB, 0x1FF9, 0x1FFF)) for _ in range(n)]
        initial = [rng.choice((0, 17, 0x3FFF, 0x4000, 0x4001)) for _ in range(n)]
        counts = [rng.choice((0, 1, 5)) for _ in range(n)]
        graph, ground, cursor = [], [], ARRAY
        for i in range(n):
            adjacent = [j for j in range(n) if j != i and rng.randrange(4) == 0]
            rng.shuffle(adjacent)
            graph.append(adjacent)
            ground.append(rng.randrange(len(adjacent) + 1))
            a = region(i)
            for offset, value, width in [(0, flags[i], 2), (2, initial[i], 2),
                (4, counts[i], 2), (6, ground[i], 1), (7, len(adjacent), 1), (0xC, cursor, 4)]:
                put(uc, a + offset, value, width)
            uc.mem_write(cursor, struct.pack("<" + "H" * len(adjacent), *adjacent))
            cursor += len(adjacent) * 2
        expected = [0 if x < 0x4000 else x for x in initial]
        def visit(index, group):
            expected[index] = group
            adjacent = graph[index][ground[index]:] if flags[index] == 0x1FFD else graph[index][:ground[index]]
            for nxt in adjacent:
                if expected[nxt] == 0:
                    visit(nxt, group)
        nxt_group = 1
        for i in range(n):
            if expected[i] == 0 and counts[i]:
                visit(i, nxt_group)
                nxt_group += 1
        run = call(uc, 0x4901F0)
        record("groups_{}".format(iteration), [run], [get(uc, region(i) + 2, 2) for i in range(n)], expected,
               {"flags": flags, "initial_groups": initial, "tile_counts": counts,
                "neighbors": graph, "ground_prefix_counts": ground})

    # Conversion uses all 16 positions, ignored VF4 bits and mixed map IDs.
    records = [[(mask >> i & 1) | (0xAACE & ~1) for i in range(16)]
               for mask in [0, 0xFFFF, 0x5555, 0xAAAA] + [1 << i for i in range(16)] +
               [(0xFFFF ^ (1 << i)) for i in range(16)]]
    for iteration, (width, height) in enumerate([(8, 8), (12, 9), (16, 12), (24, 10)]):
        uc = machine(width, height)
        ids = [(rng.randrange(len(records)) | (0x8000 if rng.randrange(2) else 0)) for _ in range(width * height)]
        runs, actual = native_walk_fragments(uc, width, height, ids, records)
        record("walk_grid_{}x{}".format(width, height), runs,
               {"sha256": hashlib.sha256(bytes(x for row in actual for x in row)).hexdigest(),
                "walkable_cells": sum(value < 128 for row in actual for value in row),
                "direction_flag_cells": sum(0 < value < 128 for row in actual for value in row)},
               inputs={"megatile_ids": ids, "vf4_records": records})

    class_flags = [x | extra for x in range(0, 0x7000000, 0x1000000)
                   for extra in (0, 0x10000, 0x400000, 0x410000, 0x80000000, 0x20410000)]
    class_flags += [0xFFFFFFFF, 0x6010000, 0x4010000, 0x2010000]
    class_flags += [0] * (8 * 8 - len(class_flags))
    uc = machine()
    run, classes = native_classes_fragment(uc, 8, 8, class_flags)
    record("tile_flag_classification", [run], classes,
           [[class_from_flags(class_flags[y * 8 + x]) for x in range(8)] for y in range(8)], class_flags)

    records = [[(1 if i < walk else 0) | (2 if i < middle else 0) |
                (4 if i < high else 0) | (8 if i < very_high else 0) | 0xAA00
                for i in range(16)] for walk, middle, high, very_high in itertools.product(
                    (0, 1, 12, 13, 15, 16), (0, 11, 12), (0, 11, 12), (0, 1))]
    for selected in ([], records):
        uc = machine()
        run, actual = native_megatile_flags_fragment(uc, selected)
        record("megatile_flags_{}_records".format(len(selected)), [run], actual,
               [megatile_flags_reference(row) for row in selected], {"vf4_records": selected})

    for width, height, start_x, start_y, end_x, end_y in [
        (8, 8, 0, 0, 7, 7), (32, 16, 0, 0, 7, 7),
        (8, 16, 7, 0, 8, 1), (32, 8, 0, 7, 1, 8),
        (8, 8, 5, 5, 8, 8), (16, 16, 10, 10, 16, 16)]:
        uc = machine(width, height)
        raster(uc, [[0x1FFB] * width for _ in range(height)])
        run = call(uc, 0x490B80, edx=start_x, args=(start_y, RECT, 0x1FFB))
        observed = list(struct.unpack("<4H", uc.mem_read(RECT, 8)))
        record("rectangle_{}x{}_{}_{}".format(width, height, start_x, start_y), [run], observed,
               [start_x, start_y, end_x, end_y])

    for x, y in itertools.product((0, 3, 7), repeat=2):
        uc = machine()
        rows = [[1 + sy * 8 + sx for sx in range(8)] for sy in range(8)]
        raster(uc, rows)
        run = call(uc, 0x490A60, edx=x, args=(y, ARRAY))
        observed = list(struct.unpack("<8H", uc.mem_read(ARRAY, 16)))
        expected = [rows[y + dy][x + dx] if 0 <= x + dx < 8 and 0 <= y + dy < 8 else 0x1FFF
                    for dx, dy in [(0, -1), (-1, 0), (1, 0), (0, 1), (-1, -1), (1, -1), (-1, 1), (1, 1)]]
        record("eight_neighbors_{}_{}".format(x, y), [run], observed, expected)

    for iteration in range(15):
        uc = machine()
        put(uc, MAP, 6, 2)
        for i in range(6):
            put(uc, region(i), 0x1FFB, 2)
        rows = [[rng.choice((0, 1, 2, 3, 4, 5000, 0x1FFF)) for x in range(8)] for y in range(8)]
        raster(uc, rows)
        run = call(uc, 0x490330)
        observed, expected = [], []
        for index in range(6):
            points = [(x, y) for y, row in enumerate(rows) for x, value in enumerate(row) if value == index]
            a = region(index)
            observed.append({"flags": get(uc, a, 2), "tiles": get(uc, a + 4, 2),
                             "bounds": list(struct.unpack("<4h", uc.mem_read(a + 0x18, 8)))})
            bounds = [min(p[0] for p in points) * 32, min(p[1] for p in points) * 32,
                      (max(p[0] for p in points) + 1) * 32, (max(p[1] for p in points) + 1) * 32] if points else [32767, 32767, -1, -1]
            expected.append({"flags": 0x1FFB if points else 0x1FFF, "tiles": len(points), "bounds": bounds})
        record("counts_bounds_{}".format(iteration), [run], observed, expected, {"raster": rows})

    for source_count, order, source_reserved, target_reserved in itertools.product(
            (1, 5, 6), ((1, 2), (2, 1)), (False, True), (False, True)):
        width, height = 16, 8
        uc = machine(width, height)
        put(uc, MAP, 4, 2)
        for i in range(4):
            put(uc, region(i), 0x1FFD if i == 0 else 0x1FFB, 2)
            put(uc, region(i) + 2, 0x4000 if i == 0 else 1, 2)
        put(uc, region(3) + 2, 0x4001 if source_reserved else 1, 2)
        put(uc, region(1) + 2, 0x4001 if target_reserved else 1, 2)
        rows = [[0] * width for y in range(height)]
        rows[0][:6], rows[0][6:12], rows[1][:source_count] = [1] * 6, [2] * 6, [3] * source_count
        raster(uc, rows)
        runs = [call(uc, 0x490330)]
        for i, values in enumerate(([], [3], [3], list(order))):
            pointer = ARRAY + i * 16
            put(uc, region(i) + 0xC, pointer)
            put(uc, region(i) + 7, len(values), 1)
            uc.mem_write(pointer, struct.pack("<" + "H" * len(values), *values))
        runs.append(call(uc, 0x48FC80))
        target = next((i for i in order if i != 1 or not target_reserved), None) if source_count < 6 and not source_reserved else None
        counts = [width * height - 12 - source_count, 6, 6, source_count]
        expected_rows = [row[:] for row in rows]
        if target is not None:
            counts[target] += source_count
            counts[3] = 0
            expected_rows[1][:source_count] = [target] * source_count
        observed = {"counts": [get(uc, region(i) + 4, 2) for i in range(4)],
                    "source_flags": get(uc, region(3), 2),
                    "raster": [[get(uc, MAP + 0xC + y * 512 + x * 2, 2) for x in range(width)] for y in range(height)]}
        record("merge_{}_{}_{}_{}".format(source_count, order[0], int(source_reserved), int(target_reserved)), runs,
               observed, {"counts": counts, "source_flags": 0x1FFF if target else 0x1FFB, "raster": expected_rows},
               {"source_count": source_count, "neighbor_order": list(order),
                "source_reserved": source_reserved, "target1_reserved": target_reserved})

    for iteration in range(24):
        uc = machine()
        n = 9
        put(uc, MAP, n, 2)
        live = [True] + [bool(rng.randrange(2)) for i in range(n - 1)]
        mapping = []
        for value in live:
            mapping.append(sum(live[:len(mapping)]))
        live_ids = [i for i, value in enumerate(live) if value]
        graph = []
        for index in range(n):
            values = [i for i in live_ids if i != index and rng.randrange(3) == 0]
            graph.append(values)
            a = region(index)
            put(uc, a, 0x1FFB, 2)
            put(uc, a + 2, index + 1, 2)
            put(uc, a + 4, int(live[index]), 2)
            put(uc, a + 6, len(values), 1)
            put(uc, a + 7, len(values), 1)
            put(uc, a + 0xC, ARRAY + index * 32)
            put(uc, a + 0x10, index * 8192 + 4096)
            put(uc, a + 0x20, index, 1)
            uc.mem_write(ARRAY + index * 32, struct.pack("<" + "H" * len(values), *values))
        rows = [[rng.choice(live_ids) for x in range(8)] for y in range(8)]
        raster(uc, rows)
        run = call(uc, 0x48FEB0)
        observed = {"count": get(uc, MAP, 2), "payload": [], "raster": []}
        for index in range(sum(live)):
            a = region(index)
            count, pointer = get(uc, a + 7, 1), get(uc, a + 0xC)
            observed["payload"].append({"group": get(uc, a + 2, 2), "rank": get(uc, a + 0x20, 1),
                "center_x": get(uc, a + 0x10), "neighbors": list(struct.unpack("<" + "H" * count, uc.mem_read(pointer, count * 2)))})
        observed["raster"] = [[get(uc, MAP + 0xC + y * 512 + x * 2, 2) for x in range(8)] for y in range(8)]
        expected = {"count": sum(live), "payload": [
            {"group": index + 1, "rank": index, "center_x": index * 8192 + 4096,
             "neighbors": [mapping[value] for value in graph[index]]} for index in live_ids],
            "raster": [[mapping[value] for value in row] for row in rows]}
        record("reindex_{}".format(iteration), [run], observed, expected,
               {"live": live, "neighbors": graph, "raster": rows})

    # Complete graph generation, after declared allocation-dependent input stages.
    terrain_cases = []
    for width, height in [(8, 8), (16, 12), (24, 16)]:
        terrain_cases.append(("flat_{}x{}".format(width, height), width, height,
                              [0x10000] * (width * height), [[1] * 16], [0] * (width * height)))
    for pattern in ("vertical_wall", "horizontal_wall", "cross", "checkerboard", "elevation_stripes", "islands"):
        width, height = 16, 12
        values, ids = [], []
        for y in range(height):
            for x in range(width):
                blocked = ((pattern == "vertical_wall" and x == 7) or
                           (pattern == "horizontal_wall" and y == 5) or
                           (pattern == "cross" and (x == 7 or y == 5)) or
                           (pattern == "checkerboard" and (x + y) % 2 == 0) or
                           (pattern == "islands" and (x % 4 == 0 or y % 4 == 0)))
                values.append(0 if blocked else (0x2010000, 0x4010000, 0x6010000)[x // 5 % 3]
                              if pattern == "elevation_stripes" else 0x10000)
                ids.append(1 if blocked else 0)
        if pattern == "elevation_stripes":
            records = [[3] * 16, [5] * 16, [1] * 16]
            ids = [x // 5 % 3 for y in range(height) for x in range(width)]
        else:
            records = [[1] * 16, [0] * 16]
        terrain_cases.append((pattern, width, height, values, records, ids))
    for walk_mask in (0x0001, 0x8000, 0x00FF, 0xFF00, 0x3333, 0xCCCC, 0x5555, 0xAAAA, 0x7FFF, 0xFFFE):
        width, height = 12, 10
        values = [0x10000] * (width * height)
        ids = [0] * (width * height)
        # A partially walkable cell adjoining a solid wall gives distinct sides.
        for y in range(height):
            values[y * width + 6] = 0
            ids[y * width + 6] = 1
        values[4 * width + 6] = megatile_flags_reference([walk_mask >> i & 1 for i in range(16)])
        ids[4 * width + 6] = 2
        terrain_cases.append(("split_mask_{:04x}".format(walk_mask), width, height, values,
            [[1] * 16, [0] * 16, [walk_mask >> i & 1 for i in range(16)]], ids))
    for name, width, height, flags, records, ids in terrain_cases:
        uc = machine(width, height)
        runs, _ = native_walk_fragments(uc, width, height, ids, records)
        generated, megatile_flags = native_megatile_flags_fragment(uc, records)
        runs.append(generated)
        flags = [megatile_flags[index & 0x7FFF] for index in ids]
        classified, classes = native_classes_fragment(uc, width, height, flags)
        runs.append(classified)
        runs.append(call(uc, 0x490F50))
        runs.append(call(uc, 0x48F060, edx=RECT))
        assert runs[-1]["returned"] == 1
        runs.append(call(uc, 0x48F0E0))
        observed = snapshot(uc, width, height)
        rs, grid = observed["regions"], observed["raster"]
        for y, row in enumerate(grid):
            for x, index in enumerate(row):
                assert index < len(rs) or 0x2000 <= index < 0x2000 + len(observed["splits"])
                if index < len(rs):
                    assert rs[index]["flags"] == hex(classes[y][x]) or index == 0
        assert grid[-1] == [0] * width and rs[0]["group"] == 0x4000
        for r in rs:
            assert r["ground_neighbors"] <= len(r["neighbors"])
            assert len(set(r["neighbors"])) == len(r["neighbors"])
            assert all(index < len(rs) for index in r["neighbors"])
            assert all(rs[index]["flags"] != "0x1ffd" and index != 0
                       for index in r["neighbors"][:r["ground_neighbors"]])
            direct_tiles = [(x, y) for y, row in enumerate(grid) for x, value in enumerate(row) if value == r["id"]]
            # Counts/bounds were produced before split IDs replaced some cells.
            assert r["tiles"] >= len(direct_tiles)
        record("builder_" + name, runs, observed,
               inputs={"width": width, "height": height, "tile_flags": flags,
                       "megatile_ids": ids, "vf4_records": records})
        if observed["splits"]:
            assert len(observed["splits"]) == 1
            mask, walk_region, blocked_region = observed["splits"][0]
            vf4_cell = records[ids[4 * width + 6] & 0x7FFF]
            assert mask == sum((1 << i) for i, value in enumerate(vf4_cell) if value & 1 == 0)
            for mini_y, mini_x in itertools.product(range(4), repeat=2):
                pixel = (6 * 32 + mini_x * 8 + 4, 4 * 32 + mini_y * 8 + 4)
                run = call(uc, 0x481B10, ecx=pixel[0], edx=pixel[1])
                expected = blocked_region if mask >> (mini_y * 4 + mini_x) & 1 else walk_region
                # AX is the result; high EAX bits are incidental native register state.
                record("lookup_{}_{}_{}".format(name, mini_x, mini_y), [run],
                       run["returned"] & 0xFFFF, expected,
                       {"pixel": list(pixel), "split_record": observed["splits"][0]})
        pairs = [(a["id"], b["id"]) for a in rs for b in rs
                 if a["id"] < b["id"] and a["flags"] != "0x1ffd" and b["flags"] != "0x1ffd"
                 and a["group"] == b["group"]][:2]
        for source, target in pairs:
            source_xy = next((x * 32 + 16, y * 32 + 16) for y, row in enumerate(grid)
                             for x, value in enumerate(row) if value == source)
            target_xy = next((x * 32 + 16, y * 32 + 16) for y, row in enumerate(grid)
                             for x, value in enumerate(row) if value == target)
            uc.mem_write(PATH_CONTEXT, bytes(0x174))
            for offset, point in ((4, source_xy), (8, target_xy)):
                put(uc, PATH_CONTEXT + offset, point[0], 2)
                put(uc, PATH_CONTEXT + offset + 2, point[1], 2)
            put(uc, PATH_CONTEXT + 0x160, source, 2)
            put(uc, PATH_CONTEXT + 0x162, target, 2)
            run = call(uc, 0x491300, ecx=PATH_CONTEXT)
            count = get(uc, PATH_CONTEXT + 0x86, 2)
            path = [get(uc, PATH_CONTEXT + 0x20 + i * 2, 2) for i in range(count)]
            assert run["returned"] == 1 and get(uc, PATH_CONTEXT + 0x1C, 1) == 0
            assert path[0] == source and path[-1] == target
            assert all(b in rs[a]["neighbors"][:rs[a]["ground_neighbors"]] for a, b in zip(path, path[1:]))
            assert all(get(uc, region(r["id"]) + 8) == 0 for r in rs)
            record("generated_path_{}_{}_{}".format(name, source, target), [run],
                   {"path": path, "partial": get(uc, PATH_CONTEXT + 0x1C, 1)},
                   inputs={"source_region": source, "target_region": target,
                           "source_pixel": list(source_xy), "target_pixel": list(target_xy)})

    complete = sum(run["scope"] == "complete original function" for case in cases for run in case["runs"])
    fragments = sum(run["scope"] != "complete original function" for case in cases for run in case["runs"])
    result = {"target_sha256": TARGET_HASH, "version": "1.17.0.1",
        "probe_script_sha256": own_hash, "case_count": len(cases), "total_native_checks": len(cases),
        "all_expected_results_matched": True, "failed_cases": 0,
        "evidence": "bounded isolated original functions and explicitly labeled fragments; synthetic terrain",
        "original_complete_function_returns": complete, "original_fragment_completions": fragments,
        "runtime": {"python": platform.python_version(), "architecture": platform.machine(),
                    "unicorn": unicorn.__version__, "pefile": pefile.__version__, "capstone": capstone.__version__},
        "maximum_instructions_per_execution": 5000000, "no_helper_stubs": True,
        "no_windows_import_execution": True, "full_game_or_campaign_executed": False,
        "fixtures": "private zeroed region object, synthetic flag/VF4/tile IDs; scalar original memset CPU branch",
        "cases": cases,
        "limits": "Outer Storm allocation/free, contour construction, CV5/MTXM map import and authentic campaign geometry are separate contracts."}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"checks": len(cases), "complete_function_returns": complete,
                      "fragment_completions": fragments, "all_matched": True, "output": str(args.out)}))


if __name__ == "__main__":
    main()
