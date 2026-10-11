#!/usr/bin/env python3
"""Recover strategic region state changes and campaign attack preparation.

Static by default; explicit bounded probes execute only original reviewed
instructions and actual callees in private data memory, with no helper stubs.
"""
import argparse, hashlib, json, platform, struct
from pathlib import Path
import capstone, pefile
import inspect_native_military_arrival_117 as arrival
import inspect_native_ai_removal_117 as removal
import inspect_ai_opcodes_117 as vm

TARGET_HASH = arrival.TARGET_HASH
WITNESSES = {
    'change_region_state': (0x4AB900, 0x4ABA3F),
    'literal_region_marker': (0x481BB0, 0x481BD1),
    'default_min_byte': (0x4837D0, 0x4837DE),
    'attack_prepare': (0x4AF300, 0x4AF48D),
    'attack_enabled_predicate': (0x4833E0, 0x4833F2),
    'grouping_region_plus_one': (0x483800, 0x48380E),
    'classify_own_base_regions': (0x4AB760, 0x4AB805),
    'classify_enemy_regions': (0x4AB6D0, 0x4AB754),
    'expand_neighbor_classes': (0x4AAB90, 0x4AAC19),
    'promote_border_classes': (0x4AAB20, 0x4AAB8F),
    'attack_roster_address': (0x4837F0, 0x4837FC),
    'attack_preparation_flag': (0x4832D0, 0x4832E3),
    'select_staging_region': (0x4B1710, 0x4B180F),
    'staging_target_search': (0x4B1810, 0x4B1A2D),
    'ground_region_neighbors': (0x481AB0, 0x481ACC),
    'set_grouping_and_attack_time': (0x483A60, 0x483AA8),
    'disable_future_attacks': (0x483500, 0x48350E),
    'native_stack_probe': (0x4D2200, 0x4D222B),
    'native_scalar_or_rep_memset': (0x4D0C60, 0x4D0CF0),
}


def dependencies():
    mods = (arrival, removal, vm, arrival.lifecycle, arrival.primary, arrival.military, arrival.combat, arrival.spatial)
    return {Path(m.__file__).name: hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in mods}


def probes(pe):
    if platform.machine().lower() not in ('x86_64', 'amd64'): raise RuntimeError('Use validated x86_64 Rosetta runtime')
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
    regions, mapbase, units, sprites, script, program, stack, stop = 0x800000, 0xB00000, 0x810000, 0x820000, 0x830000, 0x840000, 0x900000, 0xA00000
    player, p = 7, 0x67D068 + 7 * 0x4E8
    image = pe.get_memory_mapped_image(); size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    allow = list(WITNESSES.values()) + list(arrival.military.BLOCKS.values()) + list(arrival.spatial.BLOCKS.values()) + list(removal.WITNESSES.values())
    allow += [arrival.combat.BLOCKS[x] for x in ('campaign_ai_flag', 'approximate_distance', 'has_weapon')]
    allow += [vm.WITNESSES[x] for x in ('dispatch_and_all_handlers', 'byte_reader', 'word_reader', 'script_scheduler', 'attack_clear_and_default_build', 'attack_clear_region_effects', 'global_unit_head', 'player_unit_head', 'unit_hostility')]
    allow += [(0x481B00, 0x481B09), (0x41FE40, 0x41FE55)]
    cases = []
    def put(u, a, v, n=4): u.mem_write(a, (v & ((1 << (n * 8)) - 1)).to_bytes(n, 'little'))
    def get(u, a, n=4): return int.from_bytes(u.mem_read(a, n), 'little')
    def region(idx): return regions + idx * 0x34
    def new(campaign=True, graph=None):
        graph = graph or {0: [], 1: [], 2: [], 3: []}
        u = Uc(UC_ARCH_X86, UC_MODE_32); u.mem_map(0x400000, size); u.mem_write(0x400000, image)
        for a, count in ((regions, 0x80000), (mapbase, 0x80000), (stack, 0x20000), (stop, 0x1000)): u.mem_map(a, count)
        u.mem_write(p, bytes(0x4e8)); put(u, p + 0x218, 0x20 if campaign else 0, 2); put(u, p + 0x222, 200, 1)
        put(u, 0x69b584, mapbase); put(u, mapbase, max(graph) + 1, 2); put(u, 0x6b1a74 + player * 4, regions)
        put(u, 0x6d319c, 0); put(u, 0x50f310, 0)
        for a in (0x665ae0, 0x665b08 + player * 4, 0x5c99b0, 0x5d6e48, 0x5d6e5c, 0x5da38c): put(u, a, 0)
        put(u, 0x5349d8, 100); put(u, 0x673930, 1); put(u, 0x67393c, 1); put(u, 0x673938, 0)
        put(u, 0x673940 + 40 * 4, 0); u.mem_write(0x534914, bytes([1]) * 144)
        cursor = mapbase + 0x60000
        for idx, neighbors in graph.items():
            r, terrain = region(idx), mapbase + 0x449fc + idx * 64
            put(u, r, idx, 2); put(u, r + 4, player, 1)
            put(u, terrain + 2, 1, 2); put(u, terrain + 4, 100, 2)
            put(u, terrain + 6, len(neighbors), 1); put(u, terrain + 7, len(neighbors), 1); put(u, terrain + 0xc, cursor)
            put(u, terrain + 0x10, idx * 128 * 256); put(u, terrain + 0x14, 128 * 256)
            put(u, mapbase + 0xc + 2 * (4 * 256 + idx * 4), idx, 2)
            u.mem_write(terrain + 0x18, struct.pack('<4H', idx * 128, 96, idx * 128 + 64, 160))
            if neighbors: u.mem_write(cursor, struct.pack('<' + 'H' * len(neighbors), *neighbors))
            cursor += len(neighbors) * 2
        return u
    def call(u, start, ecx=player, edx=0, args=()):
        reached, count, calls = False, 0, []
        entries = {a for a, b in allow}
        def hook(m, a, width, _):
            nonlocal reached, count
            if a == stop: reached = True; m.emu_stop(); return
            if not any(lo <= a and a + width <= hi for lo, hi in allow): raise RuntimeError(f'Unreviewed original instruction {a:#x}')
            count += 1
            if a in entries:
                sp = m.reg_read(UC_X86_REG_ESP)
                calls.append({'callee': hex(a), 'ecx': m.reg_read(UC_X86_REG_ECX), 'edx': m.reg_read(UC_X86_REG_EDX), 'stack_arguments': [get(m, sp + 4 + j * 4) for j in range(5)]})
        h = u.hook_add(UC_HOOK_CODE, hook); sp = stack + 0x1e000
        u.mem_write(sp, struct.pack('<' + 'I' * (len(args) + 1), stop, *(x & 0xffffffff for x in args)))
        u.reg_write(UC_X86_REG_ESP, sp); u.reg_write(UC_X86_REG_ECX, ecx); u.reg_write(UC_X86_REG_EDX, edx)
        try: u.emu_start(start, stop + 1, count=500000)
        except unicorn.UcError as e: raise RuntimeError(f'{e} at{u.reg_read(UC_X86_REG_EIP):#x}; calls{calls[-8:]}') from e
        finally: u.hook_del(h)
        if not reached: raise RuntimeError('No complete return within 500000 original instructions')
        return {'complete_return': True, 'instruction_count': count, 'return_eax': u.reg_read(UC_X86_REG_EAX), 'native_call_trace': calls}
    def record(label, inputs, ex, actual, expected):
        assert actual == expected, (label, inputs, actual, expected)
        cases.append({'label': label, 'fixture_inputs': inputs, 'observed': actual, 'expected_from_static_contract': expected, 'matched': True, 'helper_stubs': [], 'execution': ex})
    for old in (0, 4):
        for target in range(11):
            for campaign in (False, True):
                u = new(campaign); r = region(1); put(u, r + 2, 2, 2); put(u, r + 5, old, 1); put(u, r + 6, 77, 1); put(u, r + 8, 0x61, 1); put(u, r + 9, 88, 1)
                put(u, r + 0xc, 123, 2); put(u, r + 0xe, 456, 2)
                for off in range(0x10, 0x2c, 4): put(u, r + off, 0x11110000 + off)
                ex = call(u, 0x4ab900, r, target)
                priority = 70 if campaign else 160
                if target in (8, 9): priority = 50 if campaign else 160
                elif target == 0: priority = 10 if campaign else 40
                elif target in (3, 10): priority = 10
                elif target == 4: priority = 40
                elif target == 5: priority = 50
                elif target in (6, 7): priority = 100
                record('state transition initialization priority and preserved payload', {'old': old, 'target': target, 'campaign': campaign, 'flags': 0x61, 'default_min': 200}, ex,
                       {'state': get(u, r + 5, 1), 'priority': get(u, r + 6, 1), 'flags': get(u, r + 8, 1), 'counter9': get(u, r + 9, 1), 'destination': get(u, r + 2, 2), 'needed_ground': get(u, r + 0xc, 2), 'needed_air': get(u, r + 0xe, 2), 'payload': [get(u, r + off) for off in range(0x10, 0x2c, 4)]},
                       {'state': target, 'priority': priority, 'flags': 0x40 if target == 3 else 0x41, 'counter9': 0 if target == 7 else 88, 'destination': 2 if old != 0 or target == 7 else 0, 'needed_ground': (200 if campaign else 0) if old == 0 else 123, 'needed_air': 0 if old == 0 else 456, 'payload': [0] * 7 if old == 0 else [0x11110000 + off for off in range(0x10, 0x2c, 4)]})
    for marker in (0, 0x1ffd):
        for minimum in (0, 255):
            u = new(); put(u, mapbase + 0x449fc + 64, marker, 2); put(u, p + 0x222, minimum, 1); ex = call(u, 0x4ab900, region(1), 1)
            record('oldzero literal marker moves defaultMin to air or ground', {'marker': marker, 'default_min': minimum}, ex,
                   [get(u, region(1) + 0xc, 2), get(u, region(1) + 0xe, 2)], [0, minimum] if marker else [minimum, 0])
    for target in (0, 3, 7, 255):
        u = new(); put(u, region(0) + 5, 4, 1); before = bytes(u.mem_read(region(0), 0x34)); ex = call(u, 0x4ab900, region(0), target)
        record('regionIDzero is exact no-op', {'target': target}, ex, bytes(u.mem_read(region(0), 0x34)).hex(), before.hex())
    for target in (6, 7):
        for blocker_region in (1, 2):
            u = new(); r = region(1); put(u, r + 5, 4, 1); put(u, r + 2, 2, 2); put(u, r + 9, 88, 1)
            a, s, x = 0x5da3a0, sprites + 0x300, blocker_region * 128 + 16
            put(u, a + 0x64, 7, 2); put(u, a + 0x4c, player, 1); put(u, a + 0xc, s); put(u, s + 0x14, x, 2); put(u, s + 0x16, 128, 2)
            put(u, 0x521660 + 7 * 4, 8)
            for table, lo, hi in ((0x5c99b8, x-8, x+8), (0x5d03f8, 120, 136)):
                put(u, table, 1); put(u, table+4, lo); put(u, table+8, 1); put(u, table+12, hi)
            for off, value in ((0x13c, 0), (0x140, 1), (0x144, 0), (0x148, 1)): put(u, a + off, value)
            put(u, 0x5c99b0, 2)
            put(u, mapbase + 0xc + 2 * (4 * 256 + x // 32), blocker_region, 2)
            ex = call(u, 0x4ab900, r, target)
            record('state6or7 priority actual ownedWorker spatial gate', {'target_state': target, 'worker_region': blocker_region, 'destination_region': 2}, ex,
                   [get(u, r + 6, 1), get(u, r + 9, 1), get(u, 0x5d6e48), get(u, 0x5d6e5c)], [110 if target == 7 or blocker_region == 1 else 100, 0 if target == 7 else 88, 0, 0])
    for campaign in (False, True):
        for flags in (0, 0x20, 0x40, 0x60):
            u = new(campaign)
            for idx, rs in ((0, 8), (1, 8), (2, 9), (3, 8)):
                put(u, region(idx) + 5, rs, 1); put(u, region(idx) + 6, 77, 1); put(u, region(idx) + 8, flags, 1)
            u.mem_write(p + 0x230, bytes([0x99]) * 128); put(u, p + 0x21e, 4, 2); put(u, p + 0x228, 88)
            u.mem_write(program, bytes([11, 2, 1, 0])); put(u, 0x6a2e74, program); put(u, 0x6a2e70, script)
            put(u, script + 0x10, player); ex = call(u, 0x4938e0)
            record('nativeVM attackClear zeros roster and selectedstate8 regions only', {'campaign': campaign, 'region_flags': flags, 'script_bytes': [11, 2, 1, 0]}, ex,
                   {'roster': bytes(u.mem_read(p + 0x230, 128)).hex(), 'grouping': get(u, p + 0x21e, 2), 'last_attack': get(u, p + 0x228), 'regions': [[get(u, region(idx) + 5, 1), get(u, region(idx) + 6, 1), get(u, region(idx) + 8, 1)] for idx in range(4)], 'PC': get(u, script + 8), 'wait': get(u, script + 0xc)},
                   {'roster': bytes(128).hex(), 'grouping': 0, 'last_attack': 0, 'regions': [[8, 77, flags], [0, 40 if flags & 0x40 and not campaign else 10, flags & ~0x20], [9, 77, flags], [0, 40 if flags & 0x40 and not campaign else 10, flags & ~0x20]], 'PC': 4, 'wait': 1})
    for disabled in (False, True):
        for grouping, rs in ((0, 0), (2, 8), (2, 9), (2, 0)):
            u = new(); put(u, p + 0x22e, int(disabled), 1); put(u, p + 0x21e, grouping, 2); put(u, region(1) + 5, rs, 1)
            ex = call(u, 0x4af300, player, 128, (128, 1, 1))
            record('attackPrepare disabled existinggroup versus empty-world full search', {'disabled': disabled, 'grouping_plus1': grouping, 'grouping_state': rs}, ex,
                   [ex['return_eax'], get(u, p + 0x218, 2), get(u, p + 0x22e, 1)], [0 if not disabled and grouping and rs == 8 else 1, 0x120 if not disabled and not grouping else 0x20, 1 if disabled or not grouping else 0])
    for campaign in (False, True):
        u = new(campaign, {0: [], 1: [2, 3], 2: [1, 3], 3: [1, 2]})
        put(u, 0x665b08 + player * 4, units); put(u, 0x665ae0, units)
        for idx, owner, nextptr, uid in ((0, player, units + 0x200, 106), (1, 0, 0, 144)):
            a, s = units + idx * 0x200, sprites + idx * 0x100
            put(u, a + 0xc, s); put(u, a + 0x64, uid, 2); put(u, a + 0x4c, owner, 1); put(u, a + 0x4d, 1, 1); put(u, a + 4, nextptr)
            put(u, s + 0x14, (idx + 1) * 128, 2); put(u, s + 0x16, 128, 2)
            put(u, 0x521660 + uid * 4, 1); put(u, 0x521008 + uid, 255, 1); put(u, 0x5210f0 + uid, 255, 1)
        put(u, 0x534914 + player * 12, 0, 1)
        put(u, p + 0x230, 38, 2)
        ex = call(u, 0x4af300, player, 128, (128, 1, 1))
        pointer = next(c['edx'] for c in ex['native_call_trace'] if c['callee'] == '0x4b1710')
        record('complete original positive attack preparation picksneutralstaging nearenemy border', {'campaign': campaign, 'graph': {1: [2, 3], 2: [1, 3], 3: [1, 2]}, 'own_building_region': 1, 'enemy_building_region': 2, 'requested_roster': [38]}, ex,
               {'return': ex['return_eax'], 'flags': get(u, p + 0x218, 2), 'grouping_plus1': get(u, p + 0x21e, 2), 'last_attack': get(u, p + 0x228), 'region_states_priorities_targets': [[get(u, region(i) + 5, 1), get(u, region(i) + 6, 1), get(u, region(i) + 2, 2)] for i in range(4)], 'classes': list(u.mem_read(pointer, 4)), 'roster_first': get(u, p + 0x230, 2)},
               {'return': 0, 'flags': 0x120 if campaign else 0x100, 'grouping_plus1': 4, 'last_attack': 100, 'region_states_priorities_targets': [[0, 0, 0], [0, 0, 0], [0, 0, 0], [8, 50 if campaign else 160, 2]], 'classes': [0, 5, 3, 6], 'roster_first': 38})
        put(u, p + 0x21e, 0, 2); put(u, p + 0x228, 0); put(u, region(3) + 5, 0, 1)
        u.mem_write(program, bytes([13, 2, 1, 0])); put(u, 0x6a2e74, program); put(u, 0x6a2e70, script)
        put(u, script + 0x10, player); put(u, script + 0x24, 128); put(u, script + 0x28, 128)
        prepared = call(u, 0x4938e0)
        record('nativeVM attackPrepare continues after originalpositive grouping result', {'campaign': campaign, 'code': [13, 2, 1, 0], 'initial_grouping': 0}, prepared,
               [get(u, script + 8), get(u, script + 0xc), get(u, p + 0x21e, 2), get(u, p + 0x228), get(u, region(3) + 5, 1), get(u, region(3) + 2, 2)], [4, 1, 4, 100, 8, 2])
        u.mem_write(program, bytes([14, 11, 2, 1, 0])); put(u, 0x6a2e74, program); put(u, 0x6a2e70, script)
        put(u, script + 8, 0); put(u, script + 0xc, 0)
        retry = call(u, 0x4938e0)
        record('nativeVM attackDo waits30 while existingstaging isstate8', {'campaign': campaign, 'existing_staging': 3, 'staging_state': 8, 'code': [14, 11, 2, 1, 0]}, retry,
               [get(u, script + 8), get(u, script + 0xc), get(u, p + 0x21e, 2), get(u, p + 0x230, 2)], [0, 30, 4, 38])
        # External region-state change is a declared fixture, not a stubbed
        # StepRegion execution. It discriminates the interpreter's ready branch.
        put(u, region(3) + 5, 9, 1); put(u, script + 0xc, 0)
        ready = call(u, 0x4938e0)
        record('nativeVM attackDo continues then attackClear whenstaging leavesstate8', {'campaign': campaign, 'externally_fixture_staging_state': 9, 'wait_reset_fixture': 0}, ready,
               [get(u, script + 8), get(u, script + 0xc), get(u, p + 0x21e, 2), get(u, p + 0x228), bytes(u.mem_read(p + 0x230, 128)).hex(), get(u, region(3) + 5, 1)], [5, 1, 0, 0, bytes(128).hex(), 9])
    return {'format_version': 1, 'target_sha256': TARGET_HASH, 'target_version': '1.17.0.1', 'probe_script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'dependency_script_sha256': dependencies(), 'runtime_versions': {'python': platform.python_version(), 'unicorn': unicorn.__version__, 'capstone': capstone.__version__, 'pefile': pefile.__version__, 'architecture': platform.machine(), 'system': platform.system()}, 'original_code_executed': True, 'complete_game_executed': False, 'helper_stubs': [], 'allowlisted_preferred_va_ranges': [[hex(a), hex(b)] for a, b in allow], 'all_matched': True, 'case_count': len(cases), 'cases': cases}


def main():
    p = argparse.ArgumentParser(description=__doc__); p.add_argument('--exe', type=Path, required=True); p.add_argument('--out', type=Path, required=True)
    p.add_argument('--execute-isolated-original-code', action='store_true'); p.add_argument('--probe-out', type=Path); a = p.parse_args()
    if a.execute_isolated_original_code and a.probe_out is None: p.error('Execution requires --probe-out')
    raw = a.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != TARGET_HASH: p.error('Wrong EXE hash')
    pe = pefile.PE(data=raw); c = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    result = {'format_version': 1, 'target_sha256': TARGET_HASH, 'target_version': '1.17.0.1', 'inspector_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'dependency_script_sha256': dependencies(), 'original_code_executed': False, 'complete_game_executed': False, 'witnesses': {}}
    for name, (lo, hi) in WITNESSES.items():
        code = pe.get_data(lo - 0x400000, hi - lo); ins = list(c.disasm(code, lo)); assert sum(i.size for i in ins) == len(code), name
        result['witnesses'][name] = {'start_preferred_va': hex(lo), 'end_exclusive_preferred_va': hex(hi), 'bytes_sha256': hashlib.sha256(code).hexdigest(), 'instructions': [{'address': hex(i.address), 'bytes': i.bytes.hex(), 'mnemonic': i.mnemonic, 'operands': i.op_str} for i in ins]}
    result['state_switch'] = {'preferred_va': '0x4aba40', 'targets': list(map(hex, struct.unpack('<10I', pe.get_data(0x4aba40 - 0x400000, 40))))}
    result['staging_search_depth_order'] = list(struct.unpack('<5I', pe.get_data(0x50da70 - 0x400000, 20)))
    a.out.parent.mkdir(parents=True, exist_ok=True); a.out.write_text(json.dumps(result, indent=2) + '\n')
    if a.execute_isolated_original_code:
        output = probes(pe); a.probe_out.parent.mkdir(parents=True, exist_ok=True); a.probe_out.write_text(json.dumps(output, indent=2) + '\n'); print(f"{output['case_count']} complete campaign attack checks passed; no stubs")
    else: print('Wrote static campaign attack evidence; original code not executed')


if __name__ == '__main__': main()
