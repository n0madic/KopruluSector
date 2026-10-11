#!/usr/bin/env python3
"""Recover military allocation/arrival and random destinations from exact 1.17.

Static by default. Explicit execution uses original reviewed callees and private
objects/pools, without replacement helpers, complete game or guest host APIs.
"""
import argparse
import hashlib
import json
import platform
from pathlib import Path
import struct
import capstone
import pefile
import inspect_native_training_lifecycle_117 as lifecycle
import inspect_native_primary_orders_117 as primary
import inspect_native_military_ai_117 as military
import inspect_native_combat_ai_117 as combat
import probe_native_placement_117 as spatial

TARGET_HASH = lifecycle.TARGET_HASH
WITNESSES = {
    "military_arrival": (0x4A9B20, 0x4A9E36),
    "military_eligible": (0x4B1A30, 0x4B1A5C),
    "retain_input_order_caster": (0x4ABC90, 0x4ABCB3),
    "region_is_state8_or9": (0x4AB0F0, 0x4AB104),
    "angle_from_positions": (0x40BA10, 0x40BA62),
    "angle_lower_bound_lookup": (0x40B940, 0x40B982),
    "direction_vector_distance": (0x40C270, 0x40C2A2),
    "update_region_slowest": (0x4AB870, 0x4AB8FB),
    "loaded_unit_ai_order": (0x4ADA90, 0x4ADB08),
    "append_position_order": (0x429EE0, 0x429F0F),
    "connected_unit_null_return": (0x437E00, 0x437E1A),
    "siege_tank_near_enemy": (0x4317A0, 0x4317C4),
    "request_unburrow": (0x43C560, 0x43C597),
    "unlock_primary_then_done": (0x429D00, 0x429D0F),
    "append_no_target_order": (0x429EB0, 0x429ED3),
}


def dependencies():
    return {Path(m.__file__).name: hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest()
            for m in (lifecycle, primary, military, combat, spatial)}


def model_destination(pe, source, requested, seed, enabled):
    """Independent integer transcription using unchanged EXE lookup tables."""
    vectors = [struct.unpack('<ii', pe.get_data(0x5026B0 - 0x400000 + i * 8, 8)) for i in range(256)]
    thresholds = struct.unpack('<65I', pe.get_data(0x502EB0 - 0x400000, 260))
    def angle(p, q):
        dx, dy = q[0] - p[0], q[1] - p[1]
        if dx == 0:
            return 128 if dy > 0 else 0
        value = (abs(dy * 256) // abs(dx)) * (-1 if dy * dx < 0 else 1)
        index = next((i for i, t in enumerate(thresholds) if abs(value) <= t), 64)
        return ((-index if value < 0 else index) + (64 if dx >= 0 else 192)) % 256
    def vec(a, r):
        return tuple((v * r) >> 8 for v in vectors[a])
    def distance(a, b):
        lo, hi = sorted((abs(a[0] - b[0]), abs(a[1] - b[1])))
        small_term = (lo * 3) >> 3
        return hi if lo <= hi >> 2 else hi - (hi >> 4) - (hi >> 6) + small_term + (small_term >> 5)
    values = []
    state = seed
    for _ in range(3):
        if enabled:
            state = (state * 22695477 + 1) & 0xffffffff
            values.append((state >> 16) & 0x7fff)
        else:
            values.append(0)
    first_angle = (angle(source, requested) + (64 if values[0] & 8 else 192)) % 256
    offset = vec(first_angle, values[1] & 127)
    intermediate = tuple(a + b for a, b in zip(requested, offset))
    radius = max(0, distance(source, intermediate) - 64 + (values[2] & 127))
    offset = vec(angle(source, intermediate), radius)
    return {'candidate': [a + b for a, b in zip(source, offset)], 'seed_after': state,
            'raw_rng_values': values, 'first_heading': first_angle, 'intermediate': list(intermediate), 'radius': radius}


def probes(pe, orders):
    if platform.machine().lower() not in ('x86_64', 'amd64'):
        raise RuntimeError('Use validated x86_64 Rosetta Unicorn runtime')
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
    unit, sprite, imageptr, ai, regionbase, pool, orderpool = 0x800000, 0x805000, 0x806000, 0x807000, 0x810000, 0x820000, 0x830000
    mapbase, stack, stop = 0xB00000, 0x900000, 0xA00000
    image = pe.get_memory_mapped_image()
    size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    allow = list(WITNESSES.values()) + list(primary.BLOCKS.values()) + list(military.BLOCKS.values())
    allow += [lifecycle.BLOCKS['military_ai_allocate'], lifecycle.BLOCKS['raw_rng'], combat.BLOCKS['campaign_ai_flag'], combat.BLOCKS['approximate_distance']]
    allow += [lifecycle.BLOCKS[x] for x in ('trained_unit_ai', 'finished_ordinary_unit_ai', 'medic_ai_reference', 'unit_ai_region')]
    allow += list(spatial.BLOCKS.values()) + [combat.BLOCKS['radius_callback_iteration']]
    cases = []
    def put(u, a, v, n=4):
        u.mem_write(a, (v & ((1 << (n * 8)) - 1)).to_bytes(n, 'little'))
    def get(u, a, n=4):
        return int.from_bytes(u.mem_read(a, n), 'little')
    def new(state=0, ident=37, status=1, seed=1, enabled=True, campaign=True, military_count=1, order_count=4, existing=False):
        u = Uc(UC_ARCH_X86, UC_MODE_32)
        u.mem_map(0x400000, size); u.mem_write(0x400000, image)
        for a, count in ((unit, 0x90000), (mapbase, 0x60000), (stack, 0x2000), (stop, 0x1000)):
            u.mem_map(a, count)
        u.mem_write(0x67D068, bytes(0x4e8 * 8))
        put(u, 0x69b584, mapbase); put(u, mapbase, 4, 2)
        for y in range(32):
            for x in range(32):
                put(u, mapbase + 0xc + 2 * (y * 256 + x), 1, 2)
        put(u, mapbase + 0xc + 2 * (16 * 256 + 16), 2, 2)
        for idx, center in ((1, (128, 128)), (2, (512, 512)), (3, (768, 768))):
            base = mapbase + 0x449fc + idx * 64
            put(u, base + 2, 1, 2); put(u, base + 0x10, center[0] * 256); put(u, base + 0x14, center[1] * 256)
            r = regionbase + idx * 0x34
            put(u, r, idx, 2); put(u, r + 4, 7, 1); put(u, r + 5, state, 1); put(u, r + 0x2c, pool)
        put(u, mapbase + 0x449fc + 64 + 7, 1, 1)
        put(u, mapbase + 0x449fc + 64 + 0xc, mapbase + 0x58000)
        put(u, mapbase + 0x58000, 2, 2)
        put(u, 0x6b1a74 + 7 * 4, regionbase)
        put(u, 0x67D068 + 7 * 0x4e8 + 0x218, 0x20 if campaign else 0, 2)
        put(u, unit + 0x64, ident, 2); put(u, unit + 0x4c, 7, 1); put(u, unit + 0x4d, 1, 1)
        put(u, unit + 0xdc, status); put(u, unit + 0xc, sprite); put(u, unit + 0x34, 20)
        put(u, sprite + 0x14, 128, 2); put(u, sprite + 0x16, 128, 2)
        for field in (0x18, 0x1c, 0x20):
            put(u, sprite + field, imageptr)
        put(u, imageptr + 0x16, 0, 1)
        put(u, unit + 0x134, ai); put(u, ai + 8, 4, 1); put(u, ai + 0xc, unit); put(u, ai + 0x10, regionbase + 2 * 0x34)
        put(u, 0x521660 + ident * 4, 0)
        put(u, 0x543230, 1024, 2); put(u, 0x543234, 1024, 2)
        put(u, 0x673930, int(enabled)); put(u, 0x67393c, seed); put(u, 0x673938, 0); put(u, 0x673940 + 0x38 * 4, 0)
        put(u, 0x5aef00, orderpool if order_count else 0); put(u, 0x5aef04, orderpool + 20 * (order_count - 1) if order_count else 0); put(u, 0x5aef08, 0)
        for j in range(order_count):
            put(u, orderpool + j * 20, orderpool + (j - 1) * 20 if j else 0)
            put(u, orderpool + j * 20 + 4, orderpool + (j + 1) * 20 if j + 1 < order_count else 0)
        for row in orders:
            put(u, 0x51b200 + row['id'], row['interruptible'], 1); put(u, 0x51b980 + row['id'] * 2, row['highlight'], 2); put(u, 0x51b080 + row['id'], row['subunit_inheritance'], 1)
        put(u, 0x6ab4f8, orderpool + 0x1000); put(u, orderpool + 0x1000, 0, 2); put(u, 0x508d50, 0)
        for address in (0x5c99b0, 0x5d6e48, 0x5d6e5c, 0x5da38c): put(u, address, 0)
        put(u, pool + 0x4e20, ai if military_count else 0)
        put(u, ai, ai + 0x20 if military_count > 1 else 0)
        if military_count > 1: put(u, ai + 0x20 + 4, ai)
        if existing:
            r = regionbase + 2 * 0x34
            put(u, r + 0x30, ai + 0x100); put(u, r + 0x24, unit + 0x200)
            put(u, ai + 0x100 + 0xc, unit + 0x200); put(u, unit + 0x200 + 0x34, 30); put(u, unit + 0x200 + 0xdc, 1)
        return u
    def call(u, start=0x4ae760, ecx=None, edx=unit, argv=(1,)):
        ecx = regionbase + 2 * 0x34 if ecx is None else ecx
        reached, count, trace = False, 0, []
        entries = {a for a, b in allow}
        def hook(m, a, n, _):
            nonlocal reached, count
            if a == stop: reached = True; m.emu_stop(); return
            if not any(lo <= a and a + n <= hi for lo, hi in allow):
                raise RuntimeError(f'Unreviewed original instruction {a:#x}')
            count += 1
            if a in entries:
                sp = m.reg_read(UC_X86_REG_ESP)
                trace.append({'callee': hex(a), 'ecx': m.reg_read(UC_X86_REG_ECX), 'edx': m.reg_read(UC_X86_REG_EDX), 'stack_arguments': [get(m, sp + 4 + j * 4) for j in range(4)]})
        h = u.hook_add(UC_HOOK_CODE, hook)
        u.mem_write(stack + 0x1800, struct.pack('<' + 'I' * (len(argv) + 1), stop, *(v & 0xffffffff for v in argv)))
        u.reg_write(UC_X86_REG_ESP, stack + 0x1800); u.reg_write(UC_X86_REG_ECX, ecx); u.reg_write(UC_X86_REG_EDX, edx)
        try: u.emu_start(start, stop + 1, count=50000)
        except unicorn.UcError as error:
            raise RuntimeError(f'{error} at{u.reg_read(UC_X86_REG_EIP):#x}; calls{trace[-8:]}') from error
        finally: u.hook_del(h)
        if not reached: raise RuntimeError('No complete return within 50000 original instructions')
        return {'instruction_count': count, 'native_call_trace': trace, 'return_eax': u.reg_read(UC_X86_REG_EAX), 'complete_return': True}
    def record(label, inputs, ex, actual, expected):
        assert actual == expected, (label, inputs, actual, expected)
        cases.append({'label': label, 'fixture_inputs': inputs, 'observed': actual, 'expected_from_static_contract': expected, 'matched': True, 'helper_stubs': [], 'execution': ex})
    def state(u, r=regionbase + 2 * 0x34):
        return {'AI': get(u, unit + 0x134), 'assigned_region': get(u, ai + 0x10), 'military_head': get(u, r + 0x30), 'ground_count': get(u, r + 0xa, 2), 'slowest': get(u, r + 0x24), 'order': get(u, unit + 0x4d, 1), 'x': get(u, unit + 0x58, 2), 'y': get(u, unit + 0x5a, 2), 'rng_count': get(u, 0x673938), 'source_count': get(u, 0x673940 + 0x38 * 4), 'seed': get(u, 0x67393c), 'order_allocated': get(u, 0x5aef08)}
    expected_random = model_destination(pe, (128, 128), (512, 512), 1, True)
    for rs in range(11):
        for ident in (37, 34):
            for flying in (False, True):
                u = new(state=rs, ident=ident, status=5 if flying else 1)
                ex = call(u)
                expected = {'AI': ai, 'assigned_region': regionbase + 2 * 0x34, 'military_head': ai, 'ground_count': int(not flying), 'slowest': unit if rs in (1, 2, 8, 9) else 0, 'order': 177 if ident == 34 else (158 if rs in (1, 8, 9) else 157), 'x': expected_random['candidate'][0], 'y': expected_random['candidate'][1], 'rng_count': 3, 'source_count': 3, 'seed': expected_random['seed_after'], 'order_allocated': 0}
                record('complete allocation arrival regional reaction', {'region_state': rs, 'unit_id': ident, 'flying': flying, 'always_requested_region': True}, ex, state(u), expected)
    for seed in (0, 1, 42, 0x7fffffff, 0x80000000, 0xffffffff):
        for enabled in (False, True):
            u = new(seed=seed, enabled=enabled); model = model_destination(pe, (128, 128), (512, 512), seed, enabled)
            ex = call(u)
            record('three original RNG calls destination exact integer tables', {'seed': seed, 'enabled': enabled, 'derived_model': model}, ex,
                   [get(u, unit + 0x58, 2), get(u, unit + 0x5a, 2), get(u, 0x67393c), get(u, 0x673938), get(u, 0x673940 + 0x38 * 4)],
                   model['candidate'] + [model['seed_after'], 3 if enabled else 0, 3 if enabled else 0])
    for ident in (9, 45, 46, 67, 37, 34):
        u = new(state=8, ident=ident); ex = call(u)
        record('caster class preserves initial military move order in attacking region', {'id': ident, 'state': 8}, ex, get(u, unit + 0x4d, 1), 177 if ident == 34 else 157 if ident in (9, 45, 46, 67) else 158)
    for label, seed, width, height, wrong_group, requested in (
            ('candidate_x_equal_width', 0, 514, 1024, False, (512, 512)),
            ('candidate_y_equal_height', 57, 1024, 558, False, (512, 512)),
            ('candidate_negative', 1, 1024, 1024, False, (0, 0)),
            ('different_candidate_group', 42, 1024, 1024, True, (512, 512)),
            ('same_candidate_group', 42, 1024, 1024, False, (512, 512))):
        u = new(seed=seed); model = model_destination(pe, (128, 128), requested, seed, True)
        put(u, mapbase + 0x449fc + 2 * 64 + 0x10, requested[0] * 256); put(u, mapbase + 0x449fc + 2 * 64 + 0x14, requested[1] * 256)
        put(u, mapbase + 0xc + 2 * ((requested[1] // 32) * 256 + requested[0] // 32), 2, 2)
        put(u, 0x543230, width, 2); put(u, 0x543234, height, 2)
        if wrong_group:
            x, y = model['candidate']; put(u, mapbase + 0xc + 2 * ((y // 32) * 256 + x // 32), 3, 2)
            put(u, mapbase + 0x449fc + 3 * 64 + 2, 2, 2)
        candidate = model['candidate']; accept = 0 <= candidate[0] < width and 0 <= candidate[1] < height and not wrong_group
        ex = call(u)
        record('destination fallback '+label, {'seed': seed, 'bounds': [width, height], 'wrong_group': wrong_group, 'requested': requested, 'candidate': candidate}, ex,
               [get(u, unit + 0x58, 2), get(u, unit + 0x5a, 2), get(u, 0x673938)], (candidate if accept else list(requested)) + [3])
    u = new(); put(u, mapbase + 0x449fc + 2 * 64 + 2, 2, 2); ex = call(u)
    record('unreachable different terrain group uses requested center without RNG', {'source_group': 1, 'target_group': 2}, ex,
           [get(u, unit + 0x58, 2), get(u, unit + 0x5a, 2), get(u, 0x673938), get(u, 0x67393c)], [512, 512, 0, 1])
    for force in (0, 1):
        u = new(state=3); ex = call(u, argv=(force,))
        point = (512, 512) if force else (128, 128); model = model_destination(pe, (128, 128), point, 1, True)
        record('state3 reroutes to actual current region unless forced', {'force_requested_region': force}, ex,
               [get(u, ai + 0x10), get(u, regionbase + (2 if force else 1) * 0x34 + 0x30), get(u, unit + 0x58, 2), get(u, unit + 0x5a, 2)],
               [regionbase + (2 if force else 1) * 0x34, ai] + model['candidate'])
    for campaign in (False, True):
        for flying in (False, True):
            for speed in (10, 30, 40):
                u = new(state=8, campaign=campaign, status=5 if flying else 1, existing=True, military_count=2)
                put(u, unit + 0x34, speed); ex = call(u)
                winner = unit if campaign and not flying and speed < 30 else unit + 0x200
                record('campaign slowest selects ground over air and keeps cached ties', {'campaign': campaign, 'new_flying': flying, 'new_speed': speed, 'existing_ground_speed': 30}, ex,
                       [get(u, regionbase + 2 * 0x34 + 0x24), get(u, ai), get(u, ai + 0x100 + 4), get(u, pool + 0x4e20), get(u, ai + 0x20 + 4)],
                       [winner, ai + 0x100, ai, ai + 0x20, 0])
    for count in (0, 4):
        u = new(order_count=count); ex = call(u)
        record('allocation success does not imply primary order activation', {'order_pool_count': count}, ex,
               [get(u, unit + 0x134), get(u, unit + 0x4d, 1), get(u, 0x673938)], [ai, 157 if count else 1, 3])
    for order in (1, 99):
        for count in (0, 4):
            u = new(ident=30, order_count=count); put(u, unit + 0x4d, order, 1); ex = call(u)
            q = get(u, unit + 0x74)
            record('siege Tank empty native spatial query requests mode then queues arrival', {'initial_order': order, 'order_pool_count': count, 'spatial_query_count': 0}, ex,
                   [get(u, unit + 0x4d, 1), get(u, q + 8, 1) if q else None, get(u, 0x673938), get(u, unit + 0xdc), get(u, 0x5d6e48)],
                   [99 if order == 99 or count else 1, 157 if order == 1 and count else None, 3 if order != 99 else 0, 0x1001 if order == 1 and count else 1, 0])
    for target in (0, unit + 0x200):
        for order in (1, 116, 117):
            u = new(status=0x51); put(u, unit + 0x4d, order, 1); put(u, unit + 0x5c, target)
            ex = call(u); q = get(u, unit + 0x74)
            record('loaded burrowed route actual unburrow request unlock and queuedComputerAI', {'initial_target': target, 'initial_order': order, 'status': 0x51}, ex,
                   [get(u, unit + 0x4d, 1), get(u, q + 8, 1) if q else None, get(u, unit + 0xdc), get(u, 0x673938)],
                   [1 if order == 1 else 156, 156, 0x51, 0])
    for force in (0, 1):
        for flying in (False, True):
            u = new(state=3, status=5 if flying else 1, military_count=0); put(u, unit + 0x134, 0)
            ex = call(u, argv=(force,))
            record('empty military pool leaves unit without membership movement orRNG', {'forced_region': force, 'flying': flying, 'free_head': 0}, ex,
                   [get(u, unit + 0x134), get(u, unit + 0x4d, 1), get(u, regionbase + 0x34 + 0xa, 2), get(u, regionbase + 2 * 0x34 + 0xa, 2), get(u, 0x673938)], [0, 1, 0, 0, 0])
    for slot in range(5):
        for flying in (False, True):
            u = new(state=8, status=5 if flying else 1)
            producer, producer_ai = unit + 0x400, ai + 0x200
            put(u, unit + 0x134, 0); put(u, producer + 0x64, 111, 2); put(u, producer + 0x4c, 7, 1)
            put(u, producer + 0x134, producer_ai); put(u, producer + 0xa4, slot, 1)
            put(u, producer_ai + 9 + slot, 1, 1); put(u, producer_ai + 0x18 + slot * 4, regionbase + 2 * 0x34)
            put(u, 0x6726d0 + 7 * 36, 1, 1)
            ex = call(u, start=0x48d320, ecx=producer, edx=unit, argv=())
            record('complete trained child military metadata through real arrival', {'producer_slot': slot, 'tag': 1, 'child_flying': flying, 'region_state': 8}, ex,
                   [get(u, unit + 0x134), get(u, ai + 0x10), get(u, unit + 0x4d, 1), get(u, unit + 0x58, 2), get(u, unit + 0x5a, 2), get(u, regionbase + 2 * 0x34 + 0xa, 2), get(u, producer_ai + 9 + slot, 1)],
                   [ai, regionbase + 2 * 0x34, 158] + expected_random['candidate'] + [int(not flying), 1])
    for status, order in ((0, 1), (1, 106), (1, 150), (1, 98), (1, 6), (0x41, 1), (0x61, 6)):
        u = new(status=status); put(u, unit + 0x4d, order, 1); ex = call(u)
        # InBuilding with no connected unit executes real 437e00 return, then
        # bypasses the ordinary current-order6 rejection. This is a boundary
        # fixture; no authentic inconsistent connected-unit state is asserted.
        loaded = bool(status & 0x40)
        random_route = status == 0x61
        record('arrival order eligibility and loaded route preserve native allocation', {'status': status, 'current_order': order}, ex,
               [get(u, unit + 0x134), get(u, unit + 0x4d, 1), get(u, 0x673938)], [ai, 157 if random_route else 156 if loaded else order, 3 if random_route else 0])
    return {'format_version': 1, 'target_sha256': TARGET_HASH, 'target_version': '1.17.0.1', 'probe_script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'dependency_script_sha256': dependencies(), 'orders_dat_sha256': primary.ORDERS_HASH, 'runtime_versions': {'python': platform.python_version(), 'unicorn': unicorn.__version__, 'capstone': capstone.__version__, 'pefile': pefile.__version__, 'architecture': platform.machine(), 'system': platform.system()}, 'original_code_executed': True, 'complete_game_executed': False, 'helper_stubs': [], 'allowlisted_preferred_va_ranges': [[hex(a), hex(b)] for a, b in allow], 'all_matched': True, 'case_count': len(cases), 'cases': cases}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True); p.add_argument('--out', type=Path, required=True)
    p.add_argument('--orders-dat', type=Path); p.add_argument('--execute-isolated-original-code', action='store_true'); p.add_argument('--probe-out', type=Path)
    a = p.parse_args()
    if a.execute_isolated_original_code and (a.orders_dat is None or a.probe_out is None): p.error('Explicit execution requires --orders-dat and --probe-out')
    raw = a.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != TARGET_HASH: p.error('Wrong EXE hash')
    pe = pefile.PE(data=raw); c = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    report = {'format_version': 1, 'target_sha256': TARGET_HASH, 'target_version': '1.17.0.1', 'inspector_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'dependency_script_sha256': dependencies(), 'original_code_executed': False, 'complete_game_executed': False, 'witnesses': {}}
    for name, (start, end) in WITNESSES.items():
        rawcode = pe.get_data(start - 0x400000, end - start); instructions = list(c.disasm(rawcode, start))
        assert sum(i.size for i in instructions) == len(rawcode), name
        report['witnesses'][name] = {'start_preferred_va': hex(start), 'end_exclusive_preferred_va': hex(end), 'bytes_sha256': hashlib.sha256(rawcode).hexdigest(), 'instructions': [{'address': hex(i.address), 'bytes': i.bytes.hex(), 'mnemonic': i.mnemonic, 'operands': i.op_str} for i in instructions]}
    switch = pe.get_data(0x4abcbc - 0x400000, 59)
    report['caster_switch'] = {'low_id': 9, 'mapping_preferred_va': '0x4abcbc', 'mapping_bytes': list(switch), 'retains_input_order_ids': [i + 9 for i, x in enumerate(switch) if x == 0]}
    report['unchanged_lookup_tables'] = [{'name': name, 'preferred_va': hex(va), 'bytes': count, 'sha256': hashlib.sha256(pe.get_data(va - 0x400000, count)).hexdigest()} for name, va, count in (('direction_xy', 0x5026b0, 2048), ('angle_slope_lower_bounds', 0x502eb0, 260))]
    a.out.parent.mkdir(parents=True, exist_ok=True); a.out.write_text(json.dumps(report, indent=2) + '\n')
    if a.execute_isolated_original_code:
        result = probes(pe, primary.stock_orders(a.orders_dat.read_bytes()))
        a.probe_out.parent.mkdir(parents=True, exist_ok=True); a.probe_out.write_text(json.dumps(result, indent=2) + '\n')
        print(f"{result['case_count']} military-arrival complete original calls passed; no stubs")
    else: print('Wrote static military-arrival evidence; original code not executed')


if __name__ == '__main__': main()
