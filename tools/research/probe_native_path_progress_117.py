#!/usr/bin/env python3
"""Execute original waypoint consumer and movement state17 in private memory.

Default mode records static witnesses only. Execution uses reviewed unchanged
functions, stock extents, finite native node/heap arrays and no imported calls.
"""

import argparse
from collections import Counter
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


BLOCKS.update({name: creation.BLOCKS[name] for name in ('set_unit_destination', 'set_flingy_destination', 'mark_path_update', 'clamp_unit_position')})
BLOCKS.update({'consume_waypoint': (0x48EB90, 0x48EC1D), 'movement_state17': (0x42C400, 0x42C463),
              'set_unit_next_target': (0x43A260, 0x43A269), 'set_flingy_next_target': (0x40CD30, 0x40CD73),
              'direct_path_record': (0x48EF10, 0x48EFC7), 'game_raw_rng': (0x478200, 0x478232),
              'game_range_rng': (0x478270, 0x4782C4)})


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

    for uid, row in enumerate(dat.entries):
        put(0x521660 + uid * 4, row.special_ability_flags)

    def setup(uid, source, destination, available=True, collision_status=True, current_target=None):
        fixture(uid, source, destination, [[], [], [], []])
        put(UNIT + 0xDC, 0x100001 if collision_status else 1)
        put(UNIT + 0x10, (current_target or destination)[0] | (current_target or destination)[1] << 16)
        for offset in (0x18, 0x1C):
            put(UNIT + offset, source[0] | source[1] << 16)
        put(UNIT + 0x20, 0x10, 1)
        put(UNIT + 0x93, 0, 1)
        put(UNIT + 0x97, 17, 1)
        put(UNIT + 0x27, 0, 1)
        put(UNIT + 0x134, 0)
        uc.mem_write(POOL, b'\xA5' * 128)
        put(POOL, 0)
        put(0x69B574, POOL)
        put(0x69B570, POOL if available else 0)
        put(0x673930, 0)

    def snapshot():
        path = get(UNIT + 0x100)
        return {'next_target': list(struct.unpack('<2H', uc.mem_read(UNIT + 0x18, 4))),
            'attack_waypoint': list(struct.unpack('<2H', uc.mem_read(UNIT + 0x1C, 4))),
            'move_target': list(struct.unpack('<2H', uc.mem_read(UNIT + 0x10, 4))),
            'path_pointer': hex(path), 'flags20': get(UNIT + 0x20, 1), 'flags93': get(UNIT + 0x93, 1),
            'movement_state': get(UNIT + 0x97, 1), 'retry_delay': get(path + 0x19, 1) if path else None}

    for uid in (0, 7, 30, 37, 41, 65):
        for dx, dy in ((1, 0), (0, 1), (-1, 0), (0, -1)):
            for distance in (1, 64, 128, 256):
                source, destination = (512, 512), (512 + dx * distance, 512 + dy * distance)
                setup(uid, source, destination)
                runs, states = [], []
                expected_points = [(512 + dx * d, 512 + dy * d) for d in range(64, distance, 64)] + [destination]
                for point in expected_points:
                    run = call(0x48EB90, ecx=UNIT, arguments=(destination[0] | destination[1] << 16,))
                    actual = snapshot()
                    assert run['returned'] == 1 and actual['next_target'] == list(point) and actual['attack_waypoint'] == list(point)
                    assert actual['path_pointer'] == hex(POOL)
                    assert bool(actual['flags20'] & 0x10) == (point != destination)
                    before = bytes(uc.mem_read(POOL, 128))
                    repeat = call(0x48EB90, ecx=UNIT, arguments=(destination[0] | destination[1] << 16,))
                    assert repeat['returned'] == 1 and bytes(uc.mem_read(POOL, 128)) == before
                    assert repeat['entry_visits']['consume_waypoint'] == repeat['entry_visits']['waypoint_reuse'] == 1
                    assert not repeat['entry_visits'].get('unit_local_path') and not repeat['entry_visits'].get('set_unit_next_target')
                    runs.extend((run, repeat))
                    states.append(actual)
                    uc.mem_write(SPRITE + 0x14, struct.pack('<2H', *point))
                assert sum(r['entry_visits'].get('local_search', 0) for r in runs) == 1
                cases.append({'name': 'ground_points_{}_{}_{}_{}'.format(uid, dx, dy, distance), 'passed': True,
                    'inputs': {'unit_type': uid, 'source': list(source), 'destination': list(destination), 'available_records': 1},
                    'runs': runs, 'observed': states, 'oracle': 'Exact native64-point chain, real next/attack-waypoint setter, one local search, immediate repeat reuses path without writes',
                    'limits': 'Sprite advances explicitly between consumed points; no physical velocity/world step'})
            destination = 512 + dx * 128, 512 + dy * 128
            setup(uid, (512, 512), destination, available=False)
            run = call(0x48EB90, ecx=UNIT, arguments=(destination[0] | destination[1] << 16,))
            observed = snapshot()
            assert run['returned'] == 0 and observed['path_pointer'] == '0x0' and observed['next_target'] == [512, 512]
            cases.append({'name': 'ground_no_pool_{}_{}_{}'.format(uid, dx, dy), 'passed': True,
                'inputs': {'unit_type': uid, 'destination': list(destination), 'available_records': 0}, 'runs': [run],
                'observed': observed, 'oracle': 'Completed positive search cannot publish a path without an actual finite record; caller returns0'})
            for available in (False, True):
                setup(uid, (512, 512), destination, available=available)
                run = call(0x42C400, ecx=UNIT)
                observed = snapshot()
                assert run['returned'] == int(available) and observed['movement_state'] == (25 if available else 15)
                cases.append({'name': 'state17_{}_{}_{}_{}'.format(uid, dx, dy, available), 'passed': True,
                    'inputs': {'unit_type': uid, 'destination': list(destination), 'available_records': int(available)},
                    'runs': [run], 'observed': observed, 'oracle': 'Complete movement17→actual waypoint builder; successstate25/failurestate15'})
        print(json.dumps({'phase': 'ground waypoint consumer/state17', 'unit': uid, 'cases': len(cases)}), flush=True)
    for uid in (0, 7, 30, 37, 41, 65):
        for available in (False, True):
            for dx, dy in ((1, 0), (0, 1), (-1, 0), (0, -1)):
                destination = 512 + dx * 256, 512 + dy * 256
                for already_target in (False, True):
                    current_target = destination if already_target else (512, 512)
                    setup(uid, (512, 512), destination, available=available, collision_status=False, current_target=current_target)
                    run = call(0x48EB90, ecx=UNIT, arguments=(destination[0] | destination[1] << 16,))
                    observed = snapshot()
                    assert run['returned'] == 1 and observed['move_target'] == list(destination)
                    assert observed['path_pointer'] == (hex(POOL) if available else '0x0')
                    assert observed['next_target'] == ([512, 512] if already_target else list(destination))
                    if available:
                        assert get(POOL + 0x1C) == 0x10001 and get(POOL + 0x1B, 1) == 1
                        assert get(POOL + 0x20) == destination[0] | destination[1] << 16
                    cases.append({'name': 'noncollision_{}_{}_{}_{}_{}'.format(uid, available, dx, dy, already_target), 'passed': True,
                        'inputs': {'unit_type': uid, 'destination': list(destination), 'collision_status100000': 0,
                            'available_records': int(available), 'move_target_already_equal': already_target}, 'runs': [run],
                        'observed': observed, 'oracle': 'Direct noncollision target/one-point record branch reports1 even with empty pool; equal old target skips its target setter'})
    for uid in (0, 7, 30, 37, 41, 65):
        for another_free in (False, True):
            setup(uid, (512, 512), (768, 512), available=False, collision_status=False, current_target=(512, 512))
            put(UNIT + 0x100, POOL)
            put(POOL + 4, 512 | 512 << 16)
            uc.mem_write(POOL + 128, b'\x5A' * 128)
            put(POOL + 128, 0)
            put(0x69B570, POOL + 128 if another_free else 0)
            run = call(0x48EB90, ecx=UNIT, arguments=(768 | 512 << 16,))
            observed = snapshot()
            observed['free_head'] = hex(get(0x69B570))
            assert run['returned'] == 1 and observed['path_pointer'] == hex(POOL)
            assert observed['free_head'] == (hex(POOL + 128) if another_free else '0x0')
            assert observed['next_target'] == [768, 512] and get(POOL + 0x1C) == 0x10001, (uid, another_free, observed, hex(get(POOL + 0x1C)), run)
            cases.append({'name': 'direct_record_reuse_{}_{}'.format(uid, another_free), 'passed': True,
                'inputs': {'unit_type': uid, 'existing_record': hex(POOL), 'other_free_record': another_free},
                'runs': [run], 'observed': observed,
                'oracle': 'Full noncollision wrapper releases/pops the same existing finite record and preserves the other free head'})
    for uid in (0, 7, 30, 37, 41, 65):
        for enabled in (0, 1):
            for seed in (0, 1, 0xFFFFFFF8, 0xFFFFFFFF):
                setup(uid, (512, 512), (640, 512))
                put(UNIT + 0x93, 0xA7, 1)
                put(UNIT + 0x27, 2, 1)
                put(0x673930, enabled)
                put(0x67393C, seed)
                put(0x673938, 0)
                put(0x673940 + 51 * 4, 0)
                next_seed = (seed * 22695477 + 1) & 0xFFFFFFFF if enabled else seed
                draw = (next_seed >> 16) & 0x7FFF if enabled else 0
                expected = (draw * 3) >> 15
                run = call(0x42C400, ecx=UNIT)
                observed = snapshot()
                observed['rng_seed'] = get(0x67393C)
                observed['rng_count'] = get(0x673938)
                observed['rng_source51_count'] = get(0x673940 + 51 * 4)
                assert run['returned'] == 1 and observed['movement_state'] == 18 and observed['flags93'] == 0xA5
                assert observed['retry_delay'] == expected and observed['rng_seed'] == next_seed
                assert observed['rng_count'] == observed['rng_source51_count'] == enabled
                cases.append({'name': 'state17_retry_{}_{}_{}'.format(uid, enabled, seed), 'passed': True,
                    'inputs': {'unit_type': uid, 'flags93': 0xA7, 'movement_type27': 2, 'rng_enabled': enabled, 'seed': seed},
                    'runs': [run], 'observed': observed, 'oracle': 'Complete retrybranchstate18; clearonlyflag2; actual source51 range0..2 RNG and path timer19'})
        for flags, movement_type in ((0xA7, 0), (0xA5, 2)):
            setup(uid, (512, 512), (640, 512))
            put(UNIT + 0x93, flags, 1)
            put(UNIT + 0x27, movement_type, 1)
            run = call(0x42C400, ecx=UNIT)
            observed = snapshot()
            assert observed['movement_state'] == 25 and observed['flags93'] == 0xA5
            assert not run['entry_visits'].get('game_range_rng')
            cases.append({'name': 'state17_no_retry_{}_{}_{}'.format(uid, flags, movement_type), 'passed': True,
                'inputs': {'unit_type': uid, 'flags93': flags, 'movement_type27': movement_type}, 'runs': [run],
                'observed': observed, 'oracle': 'Other flag/movement-type combinations continue tostate25 without RNG'})
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
