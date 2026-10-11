#!/usr/bin/env python3
"""Run original local searches on authenticated original campaign geometry.

Graphs and terrain bytes are authenticated against prior original producers.
No guest imports, replaced helpers, full world or physical movement execute.
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
import probe_native_local_paths_117 as local
import probe_native_campaign_terrain_sweep_117 as sweep
import probe_native_contour_fit_117 as fit
import probe_native_terrain_import_117 as terrain
import probe_native_campaign_regions_117 as graphs

assets, contours, generation = local.assets, local.contours, local.contours.generation
TARGET_HASH = local.TARGET_HASH
CONTEXT, UNIT, SPRITE, CONTOURS, ARRAYS = local.CONTEXT, local.UNIT, local.SPRITE, local.CONTOURS, local.ARRAYS
STACK, STOP, MAP = local.STACK, local.STOP, local.MAP
FLAGS, IDS, VF4 = 0xC00000, 0xD00000, 0xD40000
BLOCKS = dict(local.BLOCKS)
BLOCKS.update(fit.BLOCKS)


def segment_hits(a, b, rectangle):
    """Exact rational intersection with a closed forbidden integer rectangle."""
    low, high = Fraction(0), Fraction(1)
    for axis in (0, 1):
        delta = b[axis] - a[axis]
        if delta == 0:
            if not rectangle[axis] <= a[axis] <= rectangle[axis + 2]:
                return False
        else:
            first, last = sorted((Fraction(rectangle[axis] - a[axis], delta),
                                  Fraction(rectangle[axis + 2] - a[axis], delta)))
            low, high = max(low, first), min(high, last)
            if low > high:
                return False
    return low <= high


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    for name in ('terrain-report', 'terrain-import-report', 'contours-report', 'campaign-regions-report',
                 'terrain-data', 'campaign-data', 'graph-scratch', 'pyms', 'static-out', 'out'):
        p.add_argument('--' + name, type=Path)
    p.add_argument('--map-limit', type=int)
    p.add_argument('--execute-original-code', action='store_true')
    a = p.parse_args()
    raw = a.exe.read_bytes()
    if assets.sha(raw) != TARGET_HASH:
        p.error('Unknown executable hash')
    pe = pefile.PE(data=raw)
    base = pe.OPTIONAL_HEADER.ImageBase
    assert base == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    reviewed, witnesses = {}, {}
    for name, (lo, hi) in BLOCKS.items():
        rows = list(md.disasm(pe.get_data(lo - base, hi - lo), lo))
        assert rows[-1].address + rows[-1].size == hi, name
        reviewed.update({r.address: r.size for r in rows})
        witnesses[name] = [{'va': hex(r.address), 'bytes': r.bytes.hex(), 'mnemonic': r.mnemonic,
                            'operands': r.op_str} for r in rows]
    if a.static_out:
        a.static_out.write_text(json.dumps({'target_sha256': TARGET_HASH, 'version': '1.17.0.1',
            'evidence': 'static; analytical labels', 'witnesses': witnesses}, indent=2) + '\n')
    if not a.execute_original_code:
        print(json.dumps({'mode': 'static only', 'blocks': len(BLOCKS)}))
        return
    assert all(getattr(a, name) for name in ('terrain_report', 'terrain_import_report', 'contours_report',
        'campaign_regions_report', 'terrain_data', 'campaign_data', 'graph_scratch', 'pyms', 'out'))
    deps = {Path(m.__file__).name: assets.file_sha(Path(m.__file__)) for m in
            (local, sweep, fit, terrain, graphs, assets, contours, generation, local.storage,
             local.collision, local.placement, local.creation)}
    provenance = json.loads(a.terrain_report.read_text())
    imported = json.loads(a.terrain_import_report.read_text())
    contour_report = json.loads(a.contours_report.read_text())
    graph_report = json.loads(a.campaign_regions_report.read_text())
    for report, source in ((provenance, assets), (imported, terrain), (contour_report, contours), (graph_report, graphs)):
        assert report.get('script_sha256', report.get('probe_script_sha256')) == deps[Path(source.__file__).name]
    for report in (imported, contour_report, graph_report):
        assert report['terrain_report_sha256'] == assets.file_sha(a.terrain_report)
        assert report['all_expected_results_matched']
    assert contour_report['authentic_contour_maps'] == graph_report['authentic_graph_count'] == 68
    assert subprocess.check_output(['git', '-C', str(a.pyms), 'rev-parse', 'HEAD'], text=True).strip() == fit.PYMS
    sys.path.insert(0, str(a.pyms))
    from PyMS.FileFormats.DAT.UnitsDAT import UnitsDAT
    dat_bytes = (a.campaign_data / 'arr/units.dat').read_bytes()
    assert assets.sha(dat_bytes) == 'da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba'
    dat = UnitsDAT()
    dat.load_data(dat_bytes)
    extents = [(r.unit_extents.left, r.unit_extents.up, r.unit_extents.right, r.unit_extents.down) for r in dat.entries]
    known_imports = {c['name'].removeprefix('campaign_import_'): c['observed'] for c in imported['cases']
                     if c['name'].startswith('campaign_import_')}
    known_contours = {c['name'].removeprefix('authentic_contours_'): c for c in contour_report['cases']
                      if c['name'].startswith('authentic_contours_')}
    known_graphs = {c['name'].removeprefix('campaign_graph_'): c['observed'] for c in graph_report['cases']
                    if c['name'].startswith('campaign_graph_')}
    decoded = {}
    for tileset in assets.TILESETS:
        payloads = {}
        for suffix in ('cv5', 'vf4'):
            payload = (a.terrain_data / 'tileset' / (tileset + '.' + suffix)).read_bytes()
            assert assets.sha(payload) == provenance['effective']['tileset\\{}.{}'.format(tileset, suffix)]['sha256']
            payloads[suffix] = payload
        decoded[tileset] = (list(struct.iter_unpack('<26H', payloads['cv5'])),
                            list(struct.iter_unpack('<16H', payloads['vf4'])), payloads['vf4'])
    import unicorn
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EDI
    image = pe.get_memory_mapped_image()
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    entries = {lo: name for name, (lo, hi) in BLOCKS.items()}
    cases, setups = [], []

    def put(uc, address, value, width=4):
        uc.mem_write(address, (value & ((1 << (width * 8)) - 1)).to_bytes(width, 'little'))

    def get(uc, address, width=4):
        return int.from_bytes(uc.mem_read(address, width), 'little')

    def call(uc, entry, ecx=0, edx=0, arguments=()):
        sp = STACK + 0xF000
        uc.mem_write(sp, struct.pack('<' + 'I' * (len(arguments) + 1), STOP,
                                    *(v & 0xFFFFFFFF for v in arguments)))
        for register, value in ((UC_X86_REG_ESP, sp), (UC_X86_REG_ECX, ecx), (UC_X86_REG_EDX, edx)):
            uc.reg_write(register, value)
        count, trace, cache = 0, Counter(), {}
        def gate(cpu, address, size, unused):
            nonlocal count
            if address == STOP:
                cpu.emu_stop()
                return
            key = address, size
            if key not in cache:
                cursor, names, length = address, [], 0
                while cursor < address + size:
                    width = reviewed.get(cursor)
                    if width is None or cursor + width > address + size:
                        raise RuntimeError('Unreviewed original instruction ' + hex(cursor))
                    if cursor in entries:
                        names.append(entries[cursor])
                    cursor += width
                    length += 1
                cache[key] = length, names
            length, names = cache[key]
            count += length
            trace.update(names)
        capacities = Counter()
        def observe_capacity(cpu, address, size, unused):
            if address in (0x492170, 0x49219B, 0x4925DC):
                cursor = cpu.reg_read(UC_X86_REG_EDI)
                capacities[(hex(address), (cursor - 0x69B788) // 32, get(cpu, 0x69D924))] += 1
        hook = uc.hook_add(unicorn.UC_HOOK_BLOCK, gate)
        capacity_hook = (uc.hook_add(unicorn.UC_HOOK_CODE, observe_capacity, begin=0x492170, end=0x4925DC)
                         if entry == 0x491210 else None)
        try:
            uc.emu_start(entry, STOP, count=10000000)
            assert uc.reg_read(UC_X86_REG_EIP) == STOP
            cleanup = 0 if entry == 0x4D4490 else len(arguments) * 4
            assert uc.reg_read(UC_X86_REG_ESP) == sp + 4 + cleanup
        finally:
            uc.hook_del(hook)
            if capacity_hook is not None:
                uc.hook_del(capacity_hook)
        return {'entry': hex(entry), 'scope': 'complete original function', 'instructions': count,
                'entry_visits': dict(trace), 'returned': uc.reg_read(UC_X86_REG_EAX),
                'capacity_observations': [{'pc': pc, 'allocated_nodes': nodes, 'heap_count': heap_count, 'visits': visits}
                    for (pc, nodes, heap_count), visits in sorted(capacities.items())]}

    identities = provenance['campaigns'][:a.map_limit] if a.map_limit else provenance['campaigns']
    for ordinal, identity in enumerate(identities):
        chk = (a.campaign_data / identity['path'].replace('\\', '/')).read_bytes()
        assert assets.sha(chk) == identity['chk_sha256']
        width, height = identity['width'], identity['height']
        cv5, records, vf4 = decoded[identity['tileset']]
        raw_tiles = [r[0] for r in struct.iter_unpack('<H', assets.sections(chk)[b'MTXM'][0])]
        ids = [cv5[v >> 4 & 0x7FF][10 + (v & 15)] | (v & 0x8000) for v in raw_tiles]
        flags = [0xFFFF | (cv5[v >> 4 & 0x7FF][1] & 0xD8FA) << 16 |
                 generation.megatile_flags_reference(records[m & 0x7FFF]) | (0x400000 if v & 0x8000 else 0)
                 for v, m in zip(raw_tiles, ids)]
        fb, ib = struct.pack('<' + 'I' * len(flags), *flags), struct.pack('<' + 'H' * len(ids), *ids)
        assert assets.sha(fb) == known_imports[identity['path']]['native_flags_sha256']
        assert assets.sha(ib) == known_imports[identity['path']]['native_megatile_ids_sha256']
        walk = generation.walk_reference(width, height, ids, records)
        assert assets.sha(bytes(v for row in walk for v in row)) == known_contours[identity['path']]['inputs']['native_producer_walk_sha256']
        original_edges, ignored = contours.contour_reference(walk)
        graph_bytes = (a.graph_scratch / (str(ordinal).zfill(2) + '.json')).read_bytes()
        assert assets.sha(graph_bytes) == known_graphs[identity['path']]['snapshot_sha256']
        graph = json.loads(graph_bytes)
        assert graph['width'] == width and graph['height'] == height
        uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        for address, size in ((base, image_size), (CONTEXT, 0x100000), (STACK, 0x10000), (STOP, 0x1000),
                              (MAP, 0x100000), (FLAGS, 0x100000), (IDS, 0x100000)):
            uc.mem_map(address, size)
        uc.mem_write(base, image)
        uc.mem_write(FLAGS, fb)
        uc.mem_write(IDS, ib)
        uc.mem_write(VF4, vf4)
        for address, value, size in ((0x6D319C, 2, 4), (0x5264B4, width, 2), (0x5264B6, height, 2),
                (0x543230, width * 32, 2), (0x543234, height * 32, 2), (0x666454, FLAGS, 4),
                (0x54323C, IDS, 4), (0x66644C, VF4, 4), (0x69B584, MAP, 4), (MAP + 0x97A1C, CONTOURS, 4),
                (MAP, len(graph['regions']), 2), (MAP + 8, MAP + 0x2000C + 6 * len(graph['splits']), 4)):
            put(uc, address, value, size)
        for uid, e in enumerate(extents):
            uc.mem_write(0x522C38 + uid * 8, struct.pack('<4H', *e))
        for y, row in enumerate(graph['raster']):
            uc.mem_write(MAP + 0xC + y * 512, struct.pack('<' + 'H' * len(row), *row))
        uc.mem_write(MAP + 0x2000C, b''.join(struct.pack('<3H', *r) for r in graph['splits']))
        next_neighbor = MAP + 0x92BFC
        for region in graph['regions']:
            address = MAP + 0x449FC + region['id'] * 64
            for offset, value, size in ((0, region['flags'], 2), (2, region['group'], 2), (4, region['tiles'], 2),
                    (6, region['ground_neighbors'], 1), (7, len(region['neighbors']), 1), (0x20, region['cut_rank'], 1)):
                put(uc, address + offset, value, size)
            uc.mem_write(address + 0x10, struct.pack('<2i', *region['center_fixed8']))
            uc.mem_write(address + 0x18, struct.pack('<4h', *region['bounds_pixels']))
            pointer = address + 0x2C if len(region['neighbors']) <= 10 else next_neighbor
            put(uc, address + 0xC, pointer)
            uc.mem_write(pointer, struct.pack('<' + 'H' * len(region['neighbors']), *region['neighbors']))
            if len(region['neighbors']) > 10:
                next_neighbor += 2 * len(region['neighbors'])
        assert next_neighbor <= MAP + 0x97A1C
        edges, setup_runs = [], []
        for side, group in enumerate(original_edges):
            address = ARRAYS + side * 0x10000
            assert len(group) * 8 < 0x10000
            put(uc, CONTOURS + side * 4, address)
            put(uc, CONTOURS + 0x10 + side * 2, len(group), 2)
            uc.mem_write(address, b''.join(struct.pack('<3h2B', *r) for r in group))
            setup_runs.append(call(uc, 0x4D4490, arguments=(address, len(group), 8, 0x48AD40)))
            edges.append([list(struct.unpack('<3h2B', uc.mem_read(address + i * 8, 8))) for i in range(len(group))])
        eb = b''.join(struct.pack('<3h2B', *r) for group in edges for r in group)
        assert assets.sha(eb) == known_contours[identity['path']]['observed']['native_sorted_contours_sha256']
        put(uc, UNIT + 0xC, SPRITE)
        put(uc, UNIT + 0xDC, 0x100001)
        put(uc, UNIT + 0x105, 1, 1)
        def region_at(point):
            value = graph['raster'][point[1] // 32][point[0] // 32]
            if value >= 0x2000:
                mask, first, second = graph['splits'][value - 0x2000]
                value = second if mask & (1 << ((point[1] // 8 & 3) * 4 + (point[0] // 8 & 3))) else first
            return value
        for uid in sweep.TYPES:
            left, up, right, down = extents[uid]
            forbidden = []
            for side, group in enumerate(edges):
                for axis, start, end, direction, information in group:
                    rectangle = ((start - right, axis + 1, end + left, axis + up) if side == 0 else
                                 (axis - right, start - down, axis - 1, end + up) if side == 1 else
                                 (start - right, axis - down, end + left, axis - 1) if side == 2 else
                                 (axis + 1, start - down, axis + left, end + up))
                    if rectangle[0] <= rectangle[2] and rectangle[1] <= rectangle[3]:
                        forbidden.append(rectangle)
            for source in sweep.source_points(uid, extents[uid], (width, height), edges, flags, ids, records):
                group = graph['regions'][region_at(source)]['group']
                destination = None
                for distance in (512, 256, 128, 64, 32, 16, 8, 1):
                    for dx, dy in ((1, 1), (-1, 1), (1, -1), (-1, -1), (1, 0), (0, 1), (-1, 0), (0, -1)):
                        candidate = source[0] + dx * distance, source[1] + dy * distance
                        left, up, right, down = extents[uid]
                        if not (left <= candidate[0] < width * 32 - right and up <= candidate[1] < height * 32 - down):
                            continue
                        if (sweep.point_walkable(candidate, (width, height), flags, ids, records) and
                            fit.fits(*candidate, extents[uid], edges) and graph['regions'][region_at(candidate)]['group'] == group):
                            destination = candidate
                            break
                    if destination:
                        break
                assert destination is not None, (ordinal, uid, source)
                uc.mem_write(CONTEXT, bytes(0x16C))
                put(uc, CONTEXT, UNIT)
                uc.mem_write(CONTEXT + 4, struct.pack('<4H', *source, *destination))
                put(uc, UNIT + 0x64, uid, 2)
                uc.mem_write(SPRITE + 0x14, struct.pack('<2H', *source))
                runs = [call(uc, 0x42ED50, ecx=uid, edx=point[0], arguments=(point[1],)) for point in (source, destination)]
                assert all(r['returned'] == 1 for r in runs)
                runs.append(call(uc, 0x491210, ecx=CONTEXT))
                n = get(uc, CONTEXT + 0x152, 2)
                points = [list(struct.unpack('<2H', uc.mem_read(CONTEXT + 0x88 + i * 4, 4))) for i in range(n)]
                observed = {'points': points, 'region_count': get(uc, CONTEXT + 0x86, 2),
                    'flags': list(uc.mem_read(CONTEXT + 0x1C, 4)), 'local_nodes': get(uc, CONTEXT + 0x166, 2),
                    'local_heap_peak': get(uc, CONTEXT + 0x16A, 2), 'segment_goal': list(struct.unpack('<2H', uc.mem_read(CONTEXT + 0x18, 4)))}
                assert n <= 50 and observed['region_count'] <= 50
                assert bool(runs[-1]['returned']) == bool(n), (ordinal, uid, observed, runs[-1])
                checks = []
                for point in points:
                    native = call(uc, 0x42ED50, ecx=uid, edx=point[0], arguments=(point[1],))
                    independent = sweep.point_walkable(point, (width, height), flags, ids, records) and fit.fits(*point, extents[uid], edges)
                    checks.append({'point': point, 'native_fits': native['returned'], 'independent_fits': bool(independent)})
                    assert bool(native['returned']) == bool(independent), (ordinal, uid, point, checks[-1])
                    runs.append(native)
                segment_checks = []
                route = [list(source)] + points
                for first, last in zip(route, route[1:]):
                    collisions = [list(rectangle) for rectangle in forbidden
                        if max(first[0], last[0]) >= rectangle[0] and min(first[0], last[0]) <= rectangle[2]
                        and max(first[1], last[1]) >= rectangle[1] and min(first[1], last[1]) <= rectangle[3]
                        and segment_hits(first, last, rectangle)]
                    segment_checks.append({'source': first, 'destination': last, 'forbidden_intersections': collisions})
                cases.append({'name': 'campaign_local_{}_{}_{}'.format(ordinal, uid, source[0] | source[1] << 16),
                    'passed': True, 'inputs': {'map': identity['path'], 'unit_type': uid, 'source': list(source),
                        'destination': list(destination), 'source_region': region_at(source), 'destination_region': region_at(destination),
                        'graph_sha256': assets.sha(graph_bytes), 'native_contours_sha256': assets.sha(eb)},
                    'runs': runs, 'observed': observed, 'point_checks': checks, 'segment_geometry_diagnostics': segment_checks,
                    'oracle': 'Complete original initializer; authentic graph/contour hashes; source/goal native fit and independent/native agreement for all returned points',
                    'limits': 'First local segment only; no physical movement, positive unit blockers or full world initialization'})
        setups.append({'map': identity['path'], 'runs': setup_runs, 'graph_sha256': assets.sha(graph_bytes),
                       'native_contours_sha256': assets.sha(eb)})
        print(json.dumps({'maps': ordinal + 1, 'cases': len(cases), 'map': identity['path']}), flush=True)
    result = {'target_sha256': TARGET_HASH, 'version': '1.17.0.1', 'probe_script_sha256': assets.file_sha(Path(__file__)),
        'dependency_hashes': deps, 'terrain_report_sha256': assets.file_sha(a.terrain_report),
        'terrain_import_report_sha256': assets.file_sha(a.terrain_import_report), 'contours_report_sha256': assets.file_sha(a.contours_report),
        'campaign_regions_report_sha256': assets.file_sha(a.campaign_regions_report), 'units_dat_sha256': assets.sha(dat_bytes),
        'case_count': len(cases), 'total_native_checks': len(cases), 'all_expected_results_matched': True, 'failed_cases': 0,
        'authentic_map_count': len(identities), 'all_audited_maps_included': len(identities) == 68,
        'complete_function_returns': sum(len(c['runs']) for c in cases) + sum(len(s['runs']) for s in setups),
        'maximum_instructions_per_execution': 10000000, 'no_helper_stubs': True, 'no_import_execution': True,
        'full_game_or_campaign_executed': False, 'pyms_commit': fit.PYMS,
        'runtime': {'python': platform.python_version(), 'architecture': platform.machine(), 'unicorn': unicorn.__version__},
        'native_input_setups': setups, 'cases': cases}
    a.out.write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
