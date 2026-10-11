#!/usr/bin/env python3
"""Execute original local paths with declared positive CUnit blockers.

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
CONTEXT, UNIT, SPRITE, CONTOURS, ARRAYS, POOL = 0x800000, 0x5DA3A0, 0x810200, 0x820000, 0x830000, 0x880000
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
    for uid, row in enumerate(dat.entries):
        put(0x521660 + uid * 4, row.special_ability_flags)
    put(0x5D6E40, 256, 2)
    put(0x5D6E44, 256, 2)
    blocker, blocker_sprite = UNIT + 0x150, SPRITE + 0x40

    def setup_blocker(uid, blocker_type, source, destination, status=0x100003, finder=1, moving=False):
        fixture(uid, source, destination, [[], [], [], []])
        put(UNIT + 0xDC, 0x100003)
        put(UNIT + 0x4D, 6, 1)
        put(UNIT + 0x4C, 0, 1)
        uc.mem_write(blocker, bytes(0x150))
        uc.mem_write(blocker_sprite, bytes(0x40))
        for offset, value, width in ((0xC, blocker_sprite, 4), (0x64, blocker_type, 2), (0xDC, status, 4),
                (0x105, finder, 1), (0x4D, 1, 1), (0x4C, 1, 1), (0x10, (576 if moving else 512) | 512 << 16, 4)):
            put(blocker + offset, value, width)
        put(blocker_sprite + 0xA, 1, 1)
        uc.mem_write(blocker_sprite + 0x14, struct.pack('<2H', 512, 512))
        for axis, table in enumerate((0x5C99B8, 0x5D03F8)):
            records = []
            for index, unit_type, point in ((1, uid, source), (2, blocker_type, (512, 512))):
                e = dat.entries[unit_type].unit_extents
                records.extend(((point[axis] - (e.left if axis == 0 else e.up), index, 0),
                                (point[axis] + (e.right if axis == 0 else e.down), index, 1)))
            records.sort(key=lambda row: row[0])
            records = [(-32768, 0, 0)] + records + [(32767, 0, 1)]
            for position, (coordinate, index, high) in enumerate(records):
                uc.mem_write(table + position * 8, struct.pack('<Ii', index, coordinate))
                if index:
                    put(UNIT + (index - 1) * 0x150 + (0x144 if axis else 0x13C) + high * 4, position)
        put(0x5C99B0, 6)
        put(0x5D6E48, 0)
        put(0x5D6E5C, 0)
        put(0x69B57C, 0)
        put(0x69B578, 0)
        uc.mem_write(0x534914, bytes([2] * 144))

    for uid in (0, 7, 30, 37, 41, 65):
        for blocker_type in (0, 106):
            for source, destination in (((256, 512), (768, 512)), ((768, 512), (256, 512)),
                                        ((512, 256), (512, 768)), ((512, 768), (512, 256))):
                setup_blocker(uid, blocker_type, source, destination)
                policy = call(0x42E490, ecx=blocker, edx=UNIT)
                assert policy['returned'] == 1
                run = call(0x491210)
                observed = observe()
                assert run['returned'] == 1 and observed['points'][-1] == list(destination)
                assert run['entry_visits'].get('path_collision_policy', 0) > 0
                assert run['entry_visits'].get('local_contour_insert', 0) > 0
                e, b = dat.entries[uid].unit_extents, dat.entries[blocker_type].unit_extents
                expanded = (512 - b.left - e.right, 512 - b.up - e.down,
                            512 + b.right + e.left, 512 + b.down + e.up)
                points = [list(source)] + observed['points']
                intersections = [segment_hits_rectangle(first, last, expanded) for first, last in zip(points, points[1:])]
                assert not any(intersections), (uid, blocker_type, source, observed, expanded, intersections)
                assert get(0x5D6E48) == get(0x5D6E5C) == 0
                cases.append({'name': 'positive_route_{}_{}_{}'.format(uid, blocker_type, source[0] | source[1] << 16),
                    'passed': True, 'inputs': {'unit_type': uid, 'blocker_type': blocker_type, 'source': list(source),
                        'destination': list(destination), 'blocker_position': [512, 512], 'expanded_obstacle': list(expanded)},
                    'runs': [policy, run], 'observed': observed, 'segment_intersections': intersections,
                    'oracle': 'Full native positive candidate/policy/contour/search chain; actual destination and exact independent rational obstacle avoidance',
                    'limits': 'Declared sorted interval records, no native index insertion or physical movement'})
        print(json.dumps({'phase': 'positive blocker routes', 'unit': uid, 'cases': len(cases)}), flush=True)
    profiles = ('stationary', 'moving', 'moving_target', 'moving_mode', 'no_finder', 'no_collision',
                'enemy_pass', 'enemy_block', 'self', 'null_mover', 'status80000')
    for uid in (0, 7, 30, 35, 37, 41, 65):
        for blocker_type in (0, 35, 106, 205, 206, 207, 208):
            for profile in profiles:
                status = 0x100003 | (0x200000 if profile == 'no_collision' else 0) | (0x400000 if profile.startswith('enemy_') else 0) | (0x80000 if profile == 'status80000' else 0)
                finder = int(profile != 'no_finder')
                moving = profile.startswith('moving')
                setup_blocker(uid, blocker_type, (256, 512), (768, 512), status, finder, moving)
                if profile == 'moving_target': put(0x69B57C, blocker)
                if profile == 'moving_mode': put(0x69B578, 1)
                relation = 0 if profile == 'enemy_pass' else 1 if profile == 'enemy_block' else 2
                put(0x534914 + 1, relation, 1)
                mover = 0 if profile == 'null_mover' else blocker if profile == 'self' else UNIT
                block_item = {'type': blocker_type, 'status': status, 'order': 1, 'owner': 1}
                move_item = {'type': uid, 'status': 0x100003, 'order': 6, 'owner': 0}
                active = not moving or profile in ('moving_target', 'moving_mode')
                expected = int(active and blocker_type not in (205, 206, 207, 208) and finder and not status & 0x200000
                    and mover != blocker and (mover == 0 or collision.pair_allowed(block_item, move_item, relation)))
                run = call(0x42E490, ecx=blocker, edx=mover)
                assert run['returned'] == expected, (uid, blocker_type, profile, expected, run)
                cases.append({'name': 'policy_{}_{}_{}'.format(uid, blocker_type, profile), 'passed': True,
                    'inputs': {'mover_type': uid, 'blocker_type': blocker_type, 'profile': profile, 'status': status,
                        'finder': finder, 'moving': moving, 'diplomacy': relation, 'mover_pointer': hex(mover)},
                    'runs': [run], 'observed': {'allowed': expected},
                    'oracle': 'Native-derived stationary/current-target/path-mode gate, exact door IDs, finder/collision/self gates and independently expressed pair policy'})
    result = {'target_sha256': TARGET_HASH, 'version': '1.17.0.1', 'probe_script_sha256': assets.file_sha(Path(__file__)),
        'dependency_hashes': {Path(module.__file__).name: assets.file_sha(Path(module.__file__)) for module in
                              (assets, storage, collision, placement, creation, contours, contours.generation)},
        'case_count': len(cases), 'total_native_checks': len(cases), 'all_expected_results_matched': True,
        'failed_cases': 0, 'complete_function_returns': sum(len(row['runs']) for row in cases),
        'no_helper_stubs': True, 'no_import_execution': True, 'full_game_or_campaign_executed': False,
        'units_dat_sha256': assets.sha(dat_raw), 'pyms_commit': collision.counts.PYMS,
        'maximum_instructions_per_execution': 1000000,
        'runtime': {'python': platform.python_version(), 'architecture': platform.machine(), 'unicorn': unicorn.__version__},
        'cases': cases}
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'cases': len(cases), 'complete_returns': result['complete_function_returns']}))


if __name__ == '__main__':
    main()
