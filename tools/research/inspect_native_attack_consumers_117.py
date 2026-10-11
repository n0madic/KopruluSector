#!/usr/bin/env python3
"""Compose positive regional attack-order and global wave-retarget consumers.

Static by default. Explicit probes execute reviewed original callees only,
with private pools/objects, stock orders.dat and no helper stubs.
"""
import argparse, hashlib, json, platform, struct
from pathlib import Path
import capstone, pefile
import inspect_native_strategic_targets_117 as targets
wave = targets.wave
arrival = wave.arrival
TARGET_HASH = targets.TARGET_HASH
WITNESSES = {'transport_capable_predicate': (0x4381F0, 0x43822D)}


def dependencies():
    mods = (targets, wave, targets.units, wave.attack, arrival, wave.attack.removal, wave.attack.vm,
            arrival.lifecycle, arrival.primary, arrival.military, arrival.combat, arrival.spatial)
    return {Path(m.__file__).name: hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in mods}


def execute(pe, orders):
    if platform.machine().lower() not in ('x86_64', 'amd64'): raise RuntimeError('Use validated x86_64 runtime')
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
    own, enemy, sprite, imageptr, node, regions, pool, orderpool, mapbase, stack, stop = 0x800000, 0x5da3a0, 0x805000, 0x806000, 0x807000, 0x810000, 0x820000, 0x840000, 0xB00000, 0x900000, 0xA00000
    player, p = 7, 0x67D068 + 7 * 0x4e8
    image = pe.get_memory_mapped_image(); size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    allow = list(WITNESSES.values()) + list(targets.WITNESSES.values()) + list(wave.WITNESSES.values()) + list(wave.attack.WITNESSES.values()) + list(arrival.WITNESSES.values())
    allow += list(targets.units.BLOCKS.values()) + list(arrival.primary.BLOCKS.values()) + list(arrival.military.BLOCKS.values()) + list(arrival.spatial.BLOCKS.values()) + list(wave.attack.removal.WITNESSES.values())
    allow += [arrival.lifecycle.BLOCKS[x] for x in ('military_ai_allocate', 'raw_rng')]
    allow += [arrival.combat.BLOCKS[x] for x in ('campaign_ai_flag', 'read_cached_air_strength', 'read_cached_ground_strength', 'radius_callback_iteration', 'upgrade_level', 'cloak_detection_rejection', 'is_subunit', 'cannot_chase_target')]
    allow += [wave.attack.vm.WITNESSES[x] for x in ('unit_hostility', 'global_unit_head', 'player_unit_head')]
    allow += [(0x41fe40, 0x41fe55)]
    cases = []
    def put(u, a, v, n=4): u.mem_write(a, (v & ((1 << (8 * n)) - 1)).to_bytes(n, 'little'))
    def get(u, a, n=4): return int.from_bytes(u.mem_read(a, n), 'little')
    def region(i): return regions + i * 0x34
    def new(campaign=True, enemy_x=128, enemy_y=128, orders_available=True):
        u = Uc(UC_ARCH_X86, UC_MODE_32); u.mem_map(0x400000, size); u.mem_write(0x400000, image)
        for a, n in ((own, 0x90000), (mapbase, 0x70000), (stack, 0x20000), (stop, 0x1000)): u.mem_map(a, n)
        u.mem_write(0x67d068, bytes(8 * 0x4e8)); put(u, p + 0x218, 0x20 if campaign else 0, 2); put(u, p + 0x21e, 2, 2); put(u, p + 0x228, 100)
        u.mem_write(0x534914, bytes([1]) * 144); put(u, 0x534914 + player * 12, 0, 1)
        put(u, 0x69b584, mapbase); put(u, mapbase, 4, 2); put(u, 0x6b1a74 + player * 4, regions)
        for y in range(32):
            for x in range(32): put(u, mapbase + 0xc + 2 * (y * 256 + x), 1, 2)
        put(u, mapbase + 0xc + 2 * (16 * 256 + 16), 2, 2)
        for i, center in ((0, (0, 0)), (1, (128, 128)), (2, (512, 512)), (3, (768, 768))):
            r, t = region(i), mapbase + 0x449fc + i * 64
            put(u, r, i, 2); put(u, r + 4, player, 1); put(u, r + 0x2c, pool)
            put(u, t + 2, 1, 2); put(u, t + 0x10, center[0] * 256); put(u, t + 0x14, center[1] * 256)
            u.mem_write(t + 0x18, struct.pack('<4H', max(0, center[0]-32), max(0, center[1]-32), center[0]+32, center[1]+32))
        put(u, mapbase + 0x449fc + 64 + 7, 1, 1); put(u, mapbase + 0x449fc + 64 + 0xc, mapbase + 0x60000); put(u, mapbase + 0x60000, 2, 2)
        put(u, region(1) + 0x30, node); put(u, region(1) + 0xa, 1, 2); put(u, region(1) + 5, 8, 1); put(u, region(1) + 2, 2, 2)
        for a, ident, owner, x, y, s in ((own, 37, player, 128, 128, sprite), (enemy, 0, 0, enemy_x, enemy_y, sprite + 0x100)):
            put(u, a + 0x64, ident, 2); put(u, a + 0x4c, owner, 1); put(u, a + 0x4d, 1, 1); put(u, a + 0xdc, 1); put(u, a + 8, 100 * 256); put(u, a + 0xc, s)
            put(u, a + 0x2c, x * 256); put(u, a + 0x30, y * 256); put(u, s + 0x14, x, 2); put(u, s + 0x16, y, 2)
            for f in (0x18, 0x1c, 0x20): put(u, s + f, imageptr)
            put(u, 0x521660 + ident * 4, 0); put(u, 0x520458 + ident, 0, 1); put(u, 0x520708 + ident * 4, 100 * 256)
            put(u, 0x6ac530 + ident * 4, 100); put(u, 0x6ac530 + (228 + ident) * 4, 100)
        put(u, own + 0x134, node); put(u, own + 0x138, 20, 2); put(u, own + 0x13a, 30, 2); put(u, own + 0x34, 20)
        put(u, node + 8, 4, 1); put(u, node + 0xc, own); put(u, node + 0x10, region(1))
        put(u, 0x5211d8 + 37, 35, 1); put(u, 0x5213a8 + 37, 36, 1); put(u, 0x523fe0 + 37, 0, 1)
        put(u, 0x665ae0, enemy); put(u, 0x665b08 + player * 4, own)
        for table, lo, hi in ((0x5c99b8, enemy_x-8, enemy_x+8), (0x5d03f8, enemy_y-8, enemy_y+8)):
            put(u, table, 1); put(u, table + 4, lo); put(u, table + 8, 1); put(u, table + 12, hi)
        for off, v in ((0x13c, 0), (0x140, 1), (0x144, 0), (0x148, 1)): put(u, enemy + off, v)
        put(u, 0x5c99b0, 2)
        for a in (0x5d6e48, 0x5d6e5c, 0x5da38c, 0x508d50): put(u, a, 0)
        put(u, 0x5349d8, 100); put(u, 0x543230, 1024, 2); put(u, 0x543234, 1024, 2)
        put(u, 0x673930, 1); put(u, 0x67393c, 1); put(u, 0x673938, 0); put(u, 0x673940 + 56 * 4, 0)
        for ident in (69, 11, 42): put(u, 0x52c0c4 + (ident * 12 + player) * 4, 0)
        put(u, 0x534590 + player * 46 + 24, 0, 1)
        put(u, 0x5aef00, orderpool if orders_available else 0); put(u, 0x5aef04, orderpool + 19 * 20 if orders_available else 0); put(u, 0x5aef08, 0)
        for j in range(20):
            put(u, orderpool + j * 20, orderpool + (j - 1) * 20 if j else 0); put(u, orderpool + j * 20 + 4, orderpool + (j + 1) * 20 if j < 19 else 0)
        for row in orders:
            put(u, 0x51b200 + row['id'], row['interruptible'], 1); put(u, 0x51b980 + row['id'] * 2, row['highlight'], 2); put(u, 0x51b080 + row['id'], row['subunit_inheritance'], 1)
        put(u, 0x6ab4f8, orderpool + 0x1000); put(u, orderpool + 0x1000, 0, 2)
        return u
    def call(u, start, ecx=None, edx=0):
        count, reached, trace = 0, False, []; entries = {a for a, b in allow}
        def hook(m, a, n, _):
            nonlocal count, reached
            if a == stop: reached = True; m.emu_stop(); return
            if not any(lo <= a and a + n <= hi for lo, hi in allow): raise RuntimeError(f'Unreviewed instruction {a:#x}; trace{trace[-8:]}')
            count += 1
            if a in entries:
                sp = m.reg_read(UC_X86_REG_ESP); trace.append({'callee': hex(a), 'ecx': m.reg_read(UC_X86_REG_ECX), 'edx': m.reg_read(UC_X86_REG_EDX), 'stack_arguments': [get(m, sp + 4 + j * 4) for j in range(3)]})
        h = u.hook_add(UC_HOOK_CODE, hook); sp = stack + 0x1e000
        put(u, sp, stop); u.reg_write(UC_X86_REG_ESP, sp); u.reg_write(UC_X86_REG_ECX, region(1) if ecx is None else ecx); u.reg_write(UC_X86_REG_EDX, edx)
        try: u.emu_start(start, stop + 1, count=100000)
        except unicorn.UcError as e: raise RuntimeError(f'{e} at{u.reg_read(UC_X86_REG_EIP):#x}; trace{trace[-8:]}') from e
        finally: u.hook_del(h)
        if not reached: raise RuntimeError('No complete return within 100000 instructions')
        return {'complete_return': True, 'instruction_count': count, 'native_call_trace': trace}
    def record(label, inputs, ex, actual, expected):
        assert actual == expected, (label, inputs, actual, expected)
        cases.append({'label': label, 'fixture_inputs': inputs, 'execution': ex, 'observed': actual, 'expected_from_static_contract': expected, 'matched': True, 'helper_stubs': []})
    for current_order, status, weapon, target_pointer, order_available in ((1, 1, 35, 0, True), (14, 1, 35, 0, True), (157, 1, 35, 0, True), (158, 1, 35, 0, True), (1, 0x1001, 35, 0, True), (1, 0x2001, 35, 0, True), (1, 1, 130, 0, True), (1, 1, 35, enemy, True), (1, 1, 35, own + 0x200, True), (1, 1, 35, 0, False)):
        u = new(orders_available=order_available); put(u, own + 0x4d, current_order, 1); put(u, own + 0xdc, status); put(u, own + 0x5c, target_pointer); put(u, 0x5211d8 + 37, weapon, 1)
        ex = call(u, 0x4adcf0)
        issues = current_order not in (14, 157, 158) and not status & 0x3000 and weapon != 130 and target_pointer == 0 and order_available
        record('positive regional targetupdate real ComputerAI order issuance and native suppression gates', {'current_order': current_order, 'status': status, 'ground_weapon': weapon, 'current_target': target_pointer, 'order_pool_available': order_available}, ex,
               [get(u, region(1) + 0x1c), get(u, region(1) + 0x20), get(u, own + 0x4d, 1), get(u, own + 0x5c), get(u, own + 0x134), get(u, 0x5d6e48), get(u, 0x5d6e5c)], [enemy, enemy, 156 if issues else current_order, 0 if issues else target_pointer, node, 0, 0])
    for campaign in (False, True):
        for reachable in (False, True):
            u = new(campaign, 512, 512); put(u, mapbase + 0x449fc + 64, 0x1ffd, 2); put(u, mapbase + 0x449fc + 2 * 64 + 2, 1 if reachable else 2, 2)
            put(u, own + 0x105, 1, 1); put(u, enemy + 0xdc, 3); put(u, enemy + 0x64, 106, 2)
            ex = call(u, 0x4b0ce0, player, 1)
            record('actual state8 global positive retarget createsstate9 and real military transfer', {'campaign': campaign, 'terrain_marker': 0x1ffd, 'enemy_region': 2, 'enemy_reachable': reachable, 'enemy_building_status': True}, ex,
                   [get(u, region(1) + 5, 1), get(u, region(2) + 5, 1), get(u, region(2) + 6, 1), get(u, node + 0x10), get(u, own + 0x4d, 1), get(u, 0x673938), get(u, p + 0x21e, 2), get(u, p + 0x228)], [0, 9 if reachable else 0, (50 if campaign else 160) if reachable else 0, region(2) if reachable else region(1), 158 if reachable else 157, 3, 2, 100])
    for gate in ('nonmilitary_AI', 'parent_mismatch', 'building_property', 'transport_capacity'):
        u = new()
        if gate == 'nonmilitary_AI': put(u, node + 8, 1, 1)
        elif gate == 'parent_mismatch':
            put(u, own + 0x134, node + 0x100); put(u, node + 0x100 + 8, 4, 1); put(u, node + 0x100 + 0xc, own + 0x200)
        elif gate == 'building_property': put(u, 0x521660 + 37 * 4, 1)
        elif gate == 'transport_capacity': put(u, 0x523fe0 + 37, 1, 1)
        ex = call(u, 0x4adcf0)
        record('positive regional targetfields preserve order at AIparent building transport gates', {'gate': gate, 'fixture_transport_capacity_only': gate == 'transport_capacity'}, ex,
               [get(u, region(1) + 0x1c), get(u, region(1) + 0x20), get(u, own + 0x4d, 1)], [enemy, enemy, 1])
    for detected in (False, True):
        u = new(); put(u, enemy + 0xdc, 0x301); put(u, enemy + 0xe4, 128 if detected else 0)
        ex = call(u, 0x4adcf0)
        record('positive region targetupdate composes invisibility playerdetection and actual canattack', {'enemy_invisible_status': 0x300, 'detected_player7': detected}, ex,
               [get(u, region(1) + 0x1c), get(u, region(1) + 0x20), get(u, own + 0x4d, 1)], [enemy if detected else 0, enemy if detected else 0, 156 if detected else 1])
    return {'format_version': 1, 'original_code_executed': True, 'complete_game_executed': False, 'windows_or_host_api_called': False, 'helper_stubs': [], 'all_matched': True, 'case_count': len(cases), 'complete_return_count': len(cases), 'prefix_case_count': 0, 'cases': cases,
            'allowlisted_preferred_va_ranges': [[hex(a), hex(b)] for a, b in allow], 'runtime_versions': {'python': platform.python_version(), 'unicorn': unicorn.__version__, 'capstone': capstone.__version__, 'pefile': pefile.__version__, 'architecture': platform.machine(), 'system': platform.system()}}


def main():
    p = argparse.ArgumentParser(description=__doc__); p.add_argument('--exe', type=Path, required=True); p.add_argument('--orders-dat', type=Path, required=True); p.add_argument('--out', type=Path, required=True)
    p.add_argument('--execute-isolated-original-code', action='store_true'); p.add_argument('--probe-out', type=Path); a = p.parse_args()
    if a.execute_isolated_original_code and not a.probe_out: p.error('Execution requires --probe-out')
    raw = a.exe.read_bytes(); dat = a.orders_dat.read_bytes()
    if hashlib.sha256(raw).hexdigest() != TARGET_HASH: p.error('Unknown executable hash')
    orders = arrival.primary.stock_orders(dat); pe = pefile.PE(data=raw); assert pe.OPTIONAL_HEADER.ImageBase == 0x400000 and pe.FILE_HEADER.Machine == 0x14c
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); sha = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    result = {'target_sha256': TARGET_HASH, 'target_version': '1.17.0.1', 'inspector_sha256': sha, 'dependency_script_sha256': dependencies(), 'original_code_executed': False, 'witnesses': {}}
    for n, (lo, hi) in WITNESSES.items():
        code = pe.get_data(lo - 0x400000, hi - lo); ins = list(md.disasm(code, lo)); assert sum(i.size for i in ins) == len(code), n
        result['witnesses'][n] = {'start_preferred_va': hex(lo), 'end_exclusive_preferred_va': hex(hi), 'bytes_sha256': hashlib.sha256(code).hexdigest(), 'instructions': [{'address': hex(i.address), 'bytes': i.bytes.hex(), 'mnemonic': i.mnemonic, 'operands': i.op_str} for i in ins]}
    a.out.parent.mkdir(parents=True, exist_ok=True); a.out.write_text(json.dumps(result, indent=2) + '\n')
    if a.execute_isolated_original_code:
        run = execute(pe, orders); run.update({'target_sha256': TARGET_HASH, 'target_version': '1.17.0.1', 'probe_script_sha256': sha, 'dependency_script_sha256': dependencies(), 'orders_dat_sha256': hashlib.sha256(dat).hexdigest()})
        a.probe_out.parent.mkdir(parents=True, exist_ok=True); a.probe_out.write_text(json.dumps(run, indent=2) + '\n'); print(json.dumps({'cases': run['case_count'], 'complete': run['complete_return_count'], 'sha256': sha}))
    else: print('Wrote static attack-consumer evidence; original code not executed')


if __name__ == '__main__': main()
