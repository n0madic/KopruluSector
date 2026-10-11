#!/usr/bin/env python3
"""Execute original local route construction with private explicit geometry.

Default mode records static witnesses only. Execution uses reviewed unchanged
functions, stock extents, finite native node/heap arrays and no imported calls.
"""

import argparse
from collections import Counter
from fractions import Fraction
import json
from pathlib import Path
import platform
import struct
import subprocess
import sys

import capstone
import pefile
import inspect_terrain_assets as assets
import probe_native_path_storage_117 as storage
import probe_native_movement_collision_117 as collision
import probe_native_placement_117 as placement
import inspect_native_unit_creation_117 as creation
import probe_native_contours_117 as contours

TARGET_HASH = storage.TARGET_HASH
CONTEXT, UNIT, SPRITE, CONTOURS, ARRAYS, POOL = 0x800000, 0x810000, 0x810200, 0x820000, 0x830000, 0x880000
STACK, STOP, MAP = 0x900000, 0xA00000, 0xB00000
BLOCKS = dict(storage.BLOCKS)
BLOCKS.update({name: storage.regions.BLOCKS[name] for name in
              ("region_long_path", "heap_change_priority", "heap_sift_down", "region_group", "native_stack_probe")})
BLOCKS.update({name: collision.BLOCKS[name] for name in
              ("collision_pair", "collision_hostility", "spatial_release", "unit_pool_base")})
BLOCKS.update({name: placement.BLOCKS[name] for name in ("sorted_spatial_bound", "spatial_rectangle_query")})
BLOCKS.update({name: creation.BLOCKS[name] for name in
              ("memmove_rep_forward_entry", "memmove_forward_return", "memmove_aligned_backward_branch",
               "memmove_aligned_backward_small_tail", "memmove_aligned_backward_return")})
BLOCKS.update({"local_path_initialize": (0x491210, 0x4912F8),
              "unit_local_path": (0x48EC50, 0x48EE39),
              "continue_local_initialize": (0x4919B0, 0x491A69),
              "next_local_path": (0x491C10, 0x491F4D),
              "local_search": (0x491F50, 0x492851),
              "expand_local_node": (0x491BA0, 0x491C0D),
              "local_path_reverse": (0x492860, 0x4928ED),
              "local_heap_priority": (0x492900, 0x4929A1),
              "local_heap_down": (0x4929B0, 0x492A2E),
              "local_heap_insert": (0x492A30, 0x492A8F),
              "adjust_unreachable_destination": (0x492A90, 0x492BF9),
              "adjust_target_footprint": (0x492C00, 0x492DDC),
              "visited_area_insert": (0x492DE0, 0x4930AE),
              "visited_area_filter": (0x4930B0, 0x4931FD),
              "visited_area_initialize": (0x493200, 0x49323F),
              "region_neighbors": (0x481AB0, 0x481ACC),
              "region_unwalkable": (0x481C10, 0x481C3E),
              "region_prefer_walkable": (0x481C40, 0x481C81),
              "region_literal_1ffd": storage.regions.BLOCKS["region_literal_1ffd"],
              "region_center": storage.regions.BLOCKS["region_center"],
              "candidate_point_query": (0x421C80, 0x421DB5),
              "special_beacon": (0x421620, 0x421641),
              "path_collision_policy": (0x42E490, 0x42E51E),
              "door_predicate": (0x4290A0, 0x4290D5),
              "neighbor_append": (0x488220, 0x48831F),
              "local_geometry_collect": (0x488320, 0x4885FE),
              "local_top_units": (0x488600, 0x488910),
              "local_other_units": (0x488910, 0x488DE8),
              "import_contours": (0x488E10, 0x488ED8),
              "clear_local_lists": (0x488EE0, 0x488EF8),
              "local_contour_insert": (0x488F00, 0x489062),
              "local_shift_a": (0x489070, 0x4890DE),
              "local_shift_b": (0x4890E0, 0x48910D),
              "reset_contours": (0x489110, 0x489160),
              "unit_extents": collision.BLOCKS["unit_extents"],
              "local_target_initialize": (0x4892D0, 0x48931F),
              "expand_contour": (0x489320, 0x489359),
              "local_left_neighbors": (0x489360, 0x489879),
              "local_top_neighbors": (0x489880, 0x489D4E),
              "local_bottom_neighbors": (0x489D50, 0x48A239),
              "local_right_neighbors": (0x48A240, 0x48A74A),
              "local_neighbor_dispatch": (0x48A750, 0x48A794),
              "normalize_rectangle": (0x48A7A0, 0x48A7E8),
              "contour_bound_search": (0x48B720, 0x48B778),
              "local_top_bound": (0x48C300, 0x48C3F6),
              "local_right_bound": (0x48C400, 0x48C4D8),
              "local_bottom_bound": (0x48C4E0, 0x48C5D6),
              "local_left_bound": (0x48C5E0, 0x48C6B5)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("exe", "units-dat", "pyms", "out", "static-out"):
        parser.add_argument("--" + name, type=Path, required=name == "exe")
    parser.add_argument("--execute-original-code", action="store_true")
    args = parser.parse_args()
    raw = args.exe.read_bytes()
    if assets.sha(raw) != TARGET_HASH:
        parser.error("Unknown executable hash")
    pe = pefile.PE(data=raw)
    base = pe.OPTIONAL_HEADER.ImageBase
    assert base == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    reviewed, witnesses = {}, {}
    for name, (lo, hi) in BLOCKS.items():
        rows = list(md.disasm(pe.get_data(lo - base, hi - lo), lo))
        assert rows[-1].address + rows[-1].size == hi, (name, hex(rows[-1].address + rows[-1].size), hex(hi))
        reviewed.update({row.address: row.size for row in rows})
        witnesses[name] = [{"va": hex(row.address), "bytes": row.bytes.hex(), "mnemonic": row.mnemonic,
                            "operands": row.op_str} for row in rows]
    if args.static_out:
        args.static_out.write_text(json.dumps({"target_sha256": TARGET_HASH, "version": "1.17.0.1",
            "evidence": "static; analytical labels", "witnesses": witnesses}, indent=2) + "\n")
    if not args.execute_original_code:
        print(json.dumps({"mode": "static only", "blocks": len(BLOCKS)}))
        return
    assert all((args.units_dat, args.pyms, args.out))
    assert subprocess.check_output(["git", "-C", str(args.pyms), "rev-parse", "HEAD"], text=True).strip() == collision.counts.PYMS
    sys.path.insert(0, str(args.pyms))
    from PyMS.FileFormats.DAT.UnitsDAT import UnitsDAT
    dat_raw = args.units_dat.read_bytes()
    assert assets.sha(dat_raw) == "da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba"
    dat = UnitsDAT()
    dat.load_data(dat_raw)
    import unicorn
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
    uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
    for address, size in ((base, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095), (CONTEXT, 0x100000),
                          (STACK, 0x10000), (STOP, 0x1000), (MAP, 0x100000)):
        uc.mem_map(address, size)
    uc.mem_write(base, pe.get_memory_mapped_image())
    entries = {lo: name for name, (lo, hi) in BLOCKS.items()}

    def put(address, value, width=4):
        uc.mem_write(address, (value & ((1 << (width * 8)) - 1)).to_bytes(width, "little"))

    def get(address, width=4):
        return int.from_bytes(uc.mem_read(address, width), "little")

    for uid, row in enumerate(dat.entries):
        e = row.unit_extents
        uc.mem_write(0x522C38 + uid * 8, struct.pack("<4H", e.left, e.up, e.right, e.down))
    put(0x6D319C, 2)
    put(0x69B584, MAP)
    put(MAP, 2, 2)
    put(MAP + 0x97A1C, CONTOURS)
    uc.mem_write(MAP + 0xC, struct.pack("<H", 1) * 65536)
    region = MAP + 0x449FC + 64
    put(region, 0, 2)
    put(region + 2, 1, 2)
    put(region + 0xC, region + 0x2C)
    for field in (0x5264B4, 0x5264B6):
        put(field, 32, 2)
    for field in (0x543230, 0x543234):
        put(field, 1024, 2)
    put(UNIT + 0xC, SPRITE)
    put(UNIT + 0xDC, 0x100001)
    put(UNIT + 0x105, 1, 1)
    put(0x5C99B0, 0, 2)
    put(0x6A28F0, 0)
    cases = []

    def segment_hits_rectangle(a, b, rectangle):
        """Exact rational slab intersection, independent of native contours."""
        low, high = Fraction(0), Fraction(1)
        for axis in (0, 1):
            delta = b[axis] - a[axis]
            if delta == 0:
                if not rectangle[axis] <= a[axis] <= rectangle[axis + 2]:
                    return False
            else:
                t0, t1 = sorted((Fraction(rectangle[axis] - a[axis], delta),
                                 Fraction(rectangle[axis + 2] - a[axis], delta)))
                low, high = max(low, t0), min(high, t1)
                if low > high:
                    return False
        return low <= high

    def call(entry, ecx=CONTEXT, edx=0, arguments=()):
        sp = STACK + 0xF000
        uc.mem_write(sp, struct.pack("<" + "I" * (len(arguments) + 1), STOP,
                                    *(value & 0xFFFFFFFF for value in arguments)))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        uc.reg_write(UC_X86_REG_EDX, edx)
        count, trace = 0, Counter()
        def gate(cpu, address, size, unused):
            nonlocal count
            if address == STOP:
                cpu.emu_stop()
                return
            if reviewed.get(address) != size:
                raise RuntimeError("Unreviewed original instruction " + hex(address))
            count += 1
            if address in entries:
                trace[entries[address]] += 1
        hook = uc.hook_add(unicorn.UC_HOOK_CODE, gate)
        try:
            uc.emu_start(entry, STOP, count=1000000)
            assert uc.reg_read(UC_X86_REG_EIP) == STOP
            assert uc.reg_read(UC_X86_REG_ESP) == sp + 4 + len(arguments) * 4
        finally:
            uc.hook_del(hook)
        return {"returned": uc.reg_read(UC_X86_REG_EAX), "instructions": count, "entry_visits": dict(trace)}

    def fixture(uid, source, destination, edges):
        uc.mem_write(CONTEXT, bytes(0x16C))
        put(CONTEXT, UNIT)
        uc.mem_write(CONTEXT + 4, struct.pack("<4H", *source, *destination))
        put(UNIT + 0x64, uid, 2)
        put(UNIT + 0x100, 0)
        put(UNIT + 0x20, 0, 1)
        uc.mem_write(SPRITE + 0x14, struct.pack("<2H", *source))
        put(UNIT + 0x10, source[0] | source[1] << 16)
        for side, group in enumerate(edges):
            address = ARRAYS + side * 0x10000
            put(CONTOURS + side * 4, address)
            put(CONTOURS + 0x10 + side * 2, len(group), 2)
            uc.mem_write(address, b"".join(struct.pack("<3h2B", *row) for row in group))

    def observe():
        n = get(CONTEXT + 0x152, 2)
        return {"region_count": get(CONTEXT + 0x86, 2), "point_count": n,
                "points": [list(struct.unpack("<2H", uc.mem_read(CONTEXT + 0x88 + i * 4, 4))) for i in range(n)],
                "goal": list(struct.unpack("<2H", uc.mem_read(CONTEXT + 0x18, 4))),
                "flags": list(uc.mem_read(CONTEXT + 0x1C, 4)),
                "local_node_count": get(CONTEXT + 0x166, 2), "heap_peak": get(CONTEXT + 0x16A, 2)}
    for uid in (0, 7, 30, 37, 41, 65):
        for dx, dy in ((1, 0), (0, 1), (-1, 0), (0, -1)):
            for distance in (0, 1, 8, 16, 17, 63, 64, 65, 127, 128, 129, 255, 256):
                source = (512, 512)
                destination = (512 + dx * distance, 512 + dy * distance)
                fixture(uid, source, destination, [[], [], [], []])
                run = call(0x491210)
                observed = observe()
                expected_points = [[512 + dx * n, 512 + dy * n] for n in range(64, distance, 64)]
                expected_points.append(list(destination))
                assert run["returned"] == 1 and observed["points"] == expected_points, (uid, dx, dy, distance, observed, run)
                cases.append({"name": "unobstructed_{}_{}_{}_{}".format(uid, dx, dy, distance), "passed": True,
                    "inputs": {"unit_type": uid, "source": list(source), "destination": list(destination), "contours": [[], [], [], []]},
                    "runs": [run], "observed": observed,
                    "oracle": "Unobstructed cardinal goal is reached by native64-pixel local area boundaries, then the goal; full initializer retains one equal-source point"})
        print(json.dumps({"phase": "unobstructed stock type", "unit_type": uid, "cases": len(cases)}), flush=True)
    width, height = 32, 32
    tile_ids = [int(14 <= x < 18 and 12 <= y < 20) for y in range(height) for x in range(width)]
    walk = contours.generation.walk_reference(width, height, tile_ids, [[1] * 16, [0] * 16])
    edges, ignored_mutation = contours.contour_reference(walk)
    edges = [sorted(group, key=lambda row: row[:2]) for group in edges]
    for y in range(height):
        uc.mem_write(MAP + 0xC + y * 512, struct.pack("<32H", *(1 + tile_ids[y * width + x] for x in range(width))))
    put(MAP, 3, 2)
    put(MAP + 0x449FC + 128, 0x1FFD, 2)
    for uid in (0, 7, 30, 37, 41, 65):
        for source, destination in (((256, 512), (768, 512)), ((768, 512), (256, 512)),
                                    ((512, 256), (512, 768)), ((512, 768), (512, 256))):
            fixture(uid, source, destination, edges)
            run = call(0x491210)
            observed = observe()
            e = dat.entries[uid].unit_extents
            expanded_obstacle = (448 - e.right, 384 - e.down, 575 + e.left, 639 + e.up)
            points = [list(source)] + observed["points"]
            hits = [segment_hits_rectangle(a, b, expanded_obstacle) for a, b in zip(points, points[1:])]
            assert run["returned"] == 1 and observed["goal"] == list(destination) and observed["points"][-1] == list(destination)
            assert not any(hits), (uid, source, destination, observed, expanded_obstacle, hits)
            assert len(observed["points"]) > 1
            cases.append({"name": "rectangle_obstacle_{}_{}_{}".format(uid, source[0], source[1]), "passed": True,
                "runs": [run], "observed": observed, "inputs": {"unit_type": uid, "source": list(source),
                "destination": list(destination), "derived_contours": edges, "expanded_obstacle": list(expanded_obstacle)},
                "oracle": "Final goal and every exact-rational segment outside the inclusive stock-extent-expanded blocked rectangle",
                "limits": "Modeled contours from previously validated fixture generator; native local search executes completely, no geometry producer claim"})
        print(json.dumps({"phase": "rectangle detours", "unit_type": uid, "cases": len(cases)}), flush=True)

    uc.mem_write(MAP + 0xC, struct.pack("<H", 1) * 65536)
    for uid in (0, 7, 30, 37, 41, 65):
        for distance in (1, 64, 128, 256):
            source, destination = (512, 512), (512 + distance, 512)
            expected_points = [[n, 512] for n in range(576, destination[0], 64)] + [list(destination)]
            fixture(uid, source, destination, [[], [], [], []])
            put(UNIT + 0x10, destination[0] | destination[1] << 16)
            put(0x69B574, POOL)
            put(0x69B570, POOL)
            uc.mem_write(POOL, bytes(128))
            runs, consumed = [], []
            for index, expected_point in enumerate(expected_points):
                run = call(0x48EC50, ecx=UNIT, edx=CONTEXT, arguments=(destination[0] | destination[1] << 16,))
                output_point = list(struct.unpack("<2H", uc.mem_read(CONTEXT, 4)))
                assert run["returned"] == 1 and get(UNIT + 0x100) == POOL
                assert output_point == expected_point and get(POOL + 0x1F, 1) == index
                assert get(POOL + 4) == expected_point[0] | expected_point[1] << 16
                assert bool(get(UNIT + 0x20, 1) & 0x10) == (index + 1 < len(expected_points))
                runs.append(run)
                consumed.append(output_point)
                uc.mem_write(SPRITE + 0x14, struct.pack("<2H", *expected_point))
            assert runs[0]["entry_visits"]["local_path_initialize"] == 1
            assert all("local_search" not in run["entry_visits"] for run in runs[1:])
            cases.append({"name": "unit_path_build_and_consume_{}_{}".format(uid, distance), "passed": True,
                "inputs": {"unit_type": uid, "source": list(source), "destination": list(destination), "available_records": 1},
                "runs": runs, "observed": {"points_consumed": consumed, "same_record_retained": True, "last_movement_flag10": 0},
                "limits": "Sprite coordinates advance explicitly to returned point; original physical movement is not executed"})
    print(json.dumps({"phase": "connected path packing/point consumption", "cases": len(cases)}), flush=True)
    for uid in (0, 7, 30, 37, 41, 65):
        for distance in (64, 256):
            fixture(uid, (512, 512), (512 + distance, 512), [[], [], [], []])
            put(UNIT + 0x10, (512 + distance) | 512 << 16)
            put(0x69B574, POOL)
            put(0x69B570, 0)
            uc.mem_write(POOL, bytes([0xA5]) * 128)
            put(CONTEXT, 0xF00DBABE)
            run = call(0x48EC50, ecx=UNIT, edx=CONTEXT, arguments=((512 + distance) | 512 << 16,))
            assert run["returned"] == 0 and get(UNIT + 0x100) == 0 and get(0x69B570) == 0
            assert get(CONTEXT) == 0xF00DBABE and bytes(uc.mem_read(POOL, 128)) == bytes([0xA5]) * 128
            assert run["entry_visits"]["local_search"] == 1 and run["entry_visits"]["path_pack"] == 1
            assert get(UNIT + 0x20, 1) & 0x10
            cases.append({"name": "unit_path_empty_pool_{}_{}".format(uid, distance), "passed": True,
                "inputs": {"unit_type": uid, "distance": distance, "available_records": 0}, "runs": [run],
                "observed": {"returned": 0, "path": 0, "output_unchanged": True, "record_unchanged": True, "flag10_retained": True}})
    for depth in range(1, 129):
        uc.mem_write(CONTEXT, bytes(0x16C))
        reverse_chain = [[100 + i, 300 + i] for i in range(depth)]
        uc.mem_write(POOL, b"".join(struct.pack("<2H", *point) for point in reverse_chain))
        run = call(0x492860, edx=depth, arguments=(POOL,))
        observed = observe()
        expected = list(reversed(reverse_chain[:-1]))
        if len(expected) > 50:
            expected = expected[:49] + [reverse_chain[0]]
        assert observed["points"] == expected and get(CONTEXT + 0x150, 2) == 0
        cases.append({"name": "reverse_local_parent_chain_{}".format(depth), "passed": True,
            "inputs": {"node_count_including_start": depth}, "runs": [run],
            "observed": {"points": observed["points"], "point_count": observed["point_count"], "current_point": 0},
            "oracle": "Reverse parent chain excluding start; beyond50 retain first49 points and actual goal"})
    print(json.dumps({"phase": "failure and parent-chain limits", "cases": len(cases)}), flush=True)
    for regions_count in (2, 4, 8):
        stripe_width = 1024 // regions_count
        for y in range(32):
            uc.mem_write(MAP + 0xC + y * 512, struct.pack("<32H", *(1 + x * 32 // stripe_width for x in range(32))))
        put(MAP, regions_count + 1, 2)
        for index in range(1, regions_count + 1):
            record = MAP + 0x449FC + index * 64
            uc.mem_write(record, bytes(64))
            put(record + 2, 1, 2)
            neighbors = [neighbor for neighbor in (index - 1, index + 1) if 1 <= neighbor <= regions_count]
            put(record + 6, len(neighbors), 1)
            put(record + 7, len(neighbors), 1)
            put(record + 0xC, record + 0x2C)
            uc.mem_write(record + 0x2C, struct.pack("<" + "H" * len(neighbors), *neighbors))
            uc.mem_write(record + 0x10, struct.pack("<2I", ((index - 1) * stripe_width + stripe_width // 2) << 8, 512 << 8))
            uc.mem_write(record + 0x18, struct.pack("<4H", (index - 1) * stripe_width, 0, index * stripe_width, 1024))
        for uid in (0, 7, 30, 37, 41, 65):
            for source, destination in (((64, 512), (960, 512)), ((960, 512), (64, 512))):
                fixture(uid, source, destination, [[], [], [], []])
                run = call(0x491210)
                observed = observe()
                assert run["returned"] == 1 and observed["region_count"] == regions_count and observed["points"]
                assert run["entry_visits"]["region_long_path"] == 1 and run["entry_visits"]["local_search"] == 1
                step = 64 if source[0] < destination[0] else -64
                if regions_count <= 3:
                    final_x = destination[0]
                elif step > 0:
                    final_x = stripe_width
                else:
                    boundary = 1024 - stripe_width
                    final_x = next(x for x in range(source[0] + step, destination[0] - 1, step) if x < boundary)
                expected_points = [[x, 512] for x in range(source[0] + step, final_x + step, step)]
                assert observed["points"] == expected_points
                assert observed["flags"][2] == int(regions_count <= 3)
                cases.append({"name": "long_local_{}_{}_{}".format(regions_count, uid, source[0]), "passed": True,
                    "inputs": {"unit_type": uid, "region_stripes": regions_count, "source": list(source), "destination": list(destination)},
                    "runs": [run], "observed": observed,
                    "limits": "Explicit undirected same-group stripe graph/raster, no native producer or physical movement"})
                fixture(uid, source, destination, [[], [], [], []])
                put(UNIT + 0x10, destination[0] | destination[1] << 16)
                put(0x69B574, POOL)
                put(0x69B570, POOL)
                uc.mem_write(POOL, bytes(128))
                runs, consumed, long_searches, continued_searches, releases = [], [], 0, 0, 0
                expected_full = [[x, 512] for x in range(source[0] + step, destination[0] + step, step)]
                for expected_point in expected_full:
                    run = call(0x48EC50, ecx=UNIT, edx=CONTEXT, arguments=(destination[0] | destination[1] << 16,))
                    output_point = list(struct.unpack("<2H", uc.mem_read(CONTEXT, 4)))
                    assert run["returned"] == 1 and get(UNIT + 0x100) == POOL and output_point == expected_point
                    assert get(POOL + 0x1B, 1) == regions_count
                    long_searches += run["entry_visits"].get("region_long_path", 0)
                    continued_searches += run["entry_visits"].get("continue_local_initialize", 0)
                    releases += run["entry_visits"].get("path_unit_release", 0)
                    runs.append(run)
                    consumed.append(output_point)
                    uc.mem_write(SPRITE + 0x14, struct.pack("<2H", *expected_point))
                assert long_searches == 1 and continued_searches == releases
                assert continued_searches == max(regions_count - 3, 0)
                assert not get(UNIT + 0x20, 1) & 0x10
                cases.append({"name": "long_local_pack_continue_{}_{}_{}".format(regions_count, uid, source[0]), "passed": True,
                    "inputs": {"unit_type": uid, "region_stripes": regions_count, "source": list(source),
                               "destination": list(destination), "available_records": 1}, "runs": runs,
                    "observed": {"points_consumed": consumed, "long_searches": long_searches,
                                 "continued_local_searches": continued_searches, "record_releases": releases,
                                 "same_finite_record_reused": True, "last_flag10": 0},
                    "oracle": "Exact cardinal64-point chain; one original long search, N-3 local continuations and native release/reuse",
                    "limits": "Sprite advances explicitly to returned waypoints; physical movement does not execute"})
        print(json.dumps({"phase": "long/local connected continuations", "regions": regions_count, "cases": len(cases)}), flush=True)
    result = {"target_sha256": TARGET_HASH, "version": "1.17.0.1", "probe_script_sha256": assets.file_sha(Path(__file__)),
        "dependency_hashes": {Path(module.__file__).name: assets.file_sha(Path(module.__file__)) for module in
                              (assets, storage, collision, placement, creation, contours, contours.generation)},
        "case_count": len(cases), "total_native_checks": len(cases), "all_expected_results_matched": True,
        "failed_cases": 0, "complete_function_returns": sum(len(row["runs"]) for row in cases),
        "no_helper_stubs": True, "no_import_execution": True, "full_game_or_campaign_executed": False,
        "units_dat_sha256": assets.sha(dat_raw), "pyms_commit": collision.counts.PYMS,
        "maximum_instructions_per_execution": 1000000,
        "runtime": {"python": platform.python_version(), "architecture": platform.machine(), "unicorn": unicorn.__version__},
        "cases": cases}
    args.out.write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
