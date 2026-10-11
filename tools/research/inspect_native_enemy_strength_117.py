#!/usr/bin/env python3
"""Recover regional enemy-strength rebuilding in hash-gated native StarCraft1.17.

Static by default. Explicit isolated probes execute reviewed original code and
all reached callees in private memory, without callable helper replacements.
"""
import argparse, hashlib, json, platform, struct
from pathlib import Path
import capstone, pefile

TARGET_HASH = '94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
WITNESSES = {
    'rebuild_all_computer_enemy_strength': (0x4ABF90, 0x4AC07F),
    'terrain_region_count': (0x481B00, 0x481B09),
    'global_active_unit_head': (0x425600, 0x425606),
    'inactive_unit_predicate': (0x41FE40, 0x41FE55),
    'unit_region': (0x481BA0, 0x481BB0),
    'pixel_region_including_splits': (0x481B10, 0x481B98),
    'unit_hostility': (0x429110, 0x429130),
}


def execute(pe):
    if platform.machine().lower() not in ('x86_64', 'amd64'):
        raise RuntimeError('Use the validated x86_64 runtime')
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
    units, sprites, regions, stack, stop, terrain = 0x800000, 0x805000, 0x810000, 0x900000, 0xA00000, 0xB00000
    allow = list(WITNESSES.values()); entries = {a for a, b in allow}
    image = pe.get_memory_mapped_image(); size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    cases = []
    def put(u, a, value, n=4): u.mem_write(a, (value & ((1 << (n * 8)) - 1)).to_bytes(n, 'little'))
    def get(u, a, n=4): return int.from_bytes(u.mem_read(a, n), 'little')
    def region(p, i): return regions + p * 0x1000 + i * 0x34
    def new(count=4, gate=0, types=None):
        u = Uc(UC_ARCH_X86, UC_MODE_32); u.mem_map(0x400000, size); u.mem_write(0x400000, image)
        for a, n in ((units, 0x90000), (terrain, 0x60000), (stack, 0x20000), (stop, 0x1000)): u.mem_map(a, n)
        put(u, 0x69B584, terrain); put(u, terrain, count, 2); put(u, 0x6B1A94, gate); put(u, 0x665AE0, 0)
        u.mem_write(0x6726D0, bytes(36 * 8)); u.mem_write(0x534914, bytes(12 * 8))
        for p in range(8):
            put(u, 0x6726D0 + p * 36, 1 if types is None else types[p], 1)
            put(u, 0x534914 + p * 12 + p, 1, 1); put(u, 0x6B1A74 + p * 4, regions + p * 0x1000)
            for i in range(5):
                put(u, region(p, i) + 0x18, 300 + 10 * i + p, 2)
                put(u, region(p, i) + 0x1A, 600 + 10 * i + p, 2)
        return u
    def add_units(u, specs):
        for j, spec in enumerate(specs):
            a, s = units + j * 0x150, sprites + j * 0x100
            put(u, a + 4, units + (j + 1) * 0x150 if j + 1 < len(specs) else 0)
            put(u, a + 0xC, s if spec.get('sprite', True) else 0)
            put(u, a + 0x4C, spec.get('owner', 0), 1); put(u, a + 0x4D, spec.get('order', 1), 1)
            put(u, a + 0xDC, spec.get('status', 1)); put(u, a + 0x138, spec.get('air', 20), 2)
            put(u, a + 0x13A, spec.get('ground', 30), 2); put(u, a + 0x134, 0)
            put(u, s + 0xA, spec.get('sprite_owner', spec.get('owner', 0)), 1)
            put(u, s + 0x14, 32 + j * 32, 2); put(u, s + 0x16, 32, 2)
            put(u, terrain + 0xC + 2 * (256 + 1 + j), spec.get('region', 1), 2)
            put(u, a + 0x64, spec.get('unit_id', 37), 2)
            put(u, 0x521660 + spec.get('unit_id', 37) * 4, spec.get('dat_flags', 0))
        put(u, 0x665AE0, units if specs else 0)
    def snapshot(u):
        return [[[get(u, region(p, i) + 0x18, 2), get(u, region(p, i) + 0x1A, 2)] for i in range(5)] for p in range(8)]
    def expected(u, count, gate, specs):
        result = snapshot(u)
        if gate: return result
        for p in range(8):
            for i in range(1, count): result[p][i] = [0, 0]
        for spec in specs:
            if not spec.get('sprite', True) or not spec.get('order', 1): continue
            strength = max(spec.get('air', 20), spec.get('ground', 30))
            if not strength: continue
            owner = spec.get('owner', 0); comparison_owner = spec.get('sprite_owner', owner) if owner == 11 else owner
            for p in range(8):
                if get(u, 0x6726D0 + p * 36, 1) != 1 or owner == p: continue
                if get(u, 0x534914 + p * 12 + comparison_owner, 1) != 0: continue
                index, component = spec.get('resolved_region', spec.get('region', 1)), 0 if spec.get('status', 1) & 4 else 1
                result[p][index][component] = (result[p][index][component] + strength) & 65535
        return result
    def call(u):
        count, returned, trace, writes = 0, False, [], []
        def hook(m, a, n, unused):
            nonlocal count, returned
            if a == stop: returned = True; m.emu_stop(); return
            if not any(lo <= a and a + n <= hi for lo, hi in allow): raise RuntimeError(f'Unreviewed instruction {a:#x}; {trace[-8:]}')
            count += 1
            if a in entries: trace.append({'callee': hex(a), 'ecx': m.reg_read(UC_X86_REG_ECX), 'edx': m.reg_read(UC_X86_REG_EDX)})
        def write_hook(m, access, a, n, value, unused):
            if regions <= a < regions + 8 * 0x1000:
                writes.append({'instruction': hex(m.reg_read(UC_X86_REG_EIP)), 'address': hex(a), 'size': n, 'value': value})
        h = u.hook_add(UC_HOOK_CODE, hook); wh = u.hook_add(UC_HOOK_MEM_WRITE, write_hook)
        sp = stack + 0x1F000; put(u, sp, stop); u.reg_write(UC_X86_REG_ESP, sp)
        u.reg_write(UC_X86_REG_ECX, 0x12345678); u.reg_write(UC_X86_REG_EDX, 0x87654321)
        try: u.emu_start(0x4ABF90, stop + 1, count=100000)
        except unicorn.UcError as error: raise RuntimeError(f'{error} at {u.reg_read(UC_X86_REG_EIP):#x}; {trace[-8:]}') from error
        finally: u.hook_del(h); u.hook_del(wh)
        if not returned: raise RuntimeError('No complete return within 100000 instructions')
        return {'complete_return': True, 'instruction_count': count, 'return_eax_not_boolean_api': u.reg_read(UC_X86_REG_EAX), 'native_call_trace': trace, 'enemy_strength_writes': writes}
    def run(label, specs=(), count=4, gate=0, types=None, alliances=(), split=None):
        u = new(count, gate, types); add_units(u, specs)
        for player, owner, value in alliances: put(u, 0x534914 + player * 12 + owner, value, 1)
        if split:
            encoded, mask, low, high = split
            put(u, terrain + 0x1400C + encoded * 6, mask, 2)
            put(u, terrain + 0x1400E + encoded * 6, low, 2); put(u, terrain + 0x14010 + encoded * 6, high, 2)
        before = snapshot(u); want = expected(u, count, gate, specs); ex = call(u); actual = snapshot(u)
        assert actual == want, (label, specs, actual, want)
        cases.append({'label': label, 'fixture_inputs': {'region_count': count, 'scheduler_player_gate': gate, 'player_types': types if types is not None else [1] * 8, 'units': specs, 'alliance_overrides': alliances, 'split_region_fixture': split},
                      'before_enemy_pairs_player_then_region': before, 'execution': ex, 'observed_enemy_pairs_player_then_region': actual,
                      'expected_from_static_contract': want, 'matched': True, 'helper_stubs': []})
    for gate in (1, 7, 0xFFFFFFFF): run('nonzero scheduler player gate returns without clearing or scanning', [{'air': 65535, 'ground': 1}], gate=gate)
    for count in (0, 1, 2, 4): run('clearing affects all eight arrays but only indices one through count minus one', count=count)
    for air, ground in ((10, 20), (20, 10), (20, 20), (0, 40)):
        for flying in (False, True): run('add maximum cached mode strength to physical flying category', [{'air': air, 'ground': ground, 'status': 5 if flying else 1}])
    for changes in ({'air': 0, 'ground': 0}, {'sprite': False}, {'order': 0}): run('zero strength or inactive unit skips terrain and hostility scan', [changes])
    for kind in (0, 1, 2, 3, 5, 6, 7, 8): run('only player record type one receives contributions', [{}], types=[kind] * 8)
    for alliance in (0, 1, 2, 255):
        run('nonself diplomacy requires exactly zero', [{}], alliances=[(7, 0, alliance)])
        run('same native owner is excluded even when alliance says hostile', [{'owner': 7}], alliances=[(7, 7, alliance)])
    for sprite_owner in (0, 1, 7):
        for alliance in (0, 2): run('owner eleven uses sprite owner for hostility after native-owner comparison', [{'owner': 11, 'sprite_owner': sprite_owner}], alliances=[(7, sprite_owner, alliance)])
    for status, ident, flags in ((0, 37, 0), (0x301, 37, 0), (0x4000001, 37, 0), (0x40000001, 37, 0), (3, 106, 1), (1, 7, 8), (0x21, 37, 0)):
        run('no completion detection hallucination building worker or inbuilding gate', [{'status': status, 'unit_id': ident, 'dat_flags': flags}])
    run('multiple owners regions and categories accumulate independently for all eight players', [{'owner': 0, 'air': 10, 'ground': 20, 'region': 1}, {'owner': 1, 'air': 30, 'ground': 10, 'region': 1, 'status': 5}, {'owner': 2, 'air': 2, 'ground': 3, 'region': 2}], alliances=[(7, 1, 2)])
    for region_id in (0, 1, 3): run('unsigned sixteenbit addition wraps and region zero is not cleared', [{'air': 65535, 'ground': 0, 'region': region_id}, {'air': 1, 'ground': 0, 'region': region_id}])
    for mask in (0, 1): run('actual split terrain lookup chooses one of both region words', [{'region': 0x2000, 'resolved_region': 2 if mask else 1}], split=(0x2000, mask, 1, 2))
    return {'format_version': 1, 'original_code_executed': True, 'complete_game_executed': False, 'windows_or_host_api_called': False, 'helper_stubs': [], 'all_matched': True,
            'case_count': len(cases), 'complete_return_count': len(cases), 'prefix_case_count': 0, 'cases': cases,
            'allowlisted_preferred_va_ranges': [[hex(a), hex(b)] for a, b in allow],
            'runtime_versions': {'python': platform.python_version(), 'unicorn': unicorn.__version__, 'pefile': pefile.__version__, 'capstone': capstone.__version__, 'architecture': platform.machine(), 'system': platform.system()}}


def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--exe', type=Path, required=True); parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--execute-isolated-original-code', action='store_true'); parser.add_argument('--probe-out', type=Path); args = parser.parse_args()
    if args.execute_isolated_original_code and not args.probe_out: parser.error('Execution requires --probe-out')
    raw = args.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != TARGET_HASH: parser.error('Unknown executable hash')
    pe = pefile.PE(data=raw); assert pe.OPTIONAL_HEADER.ImageBase == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); sha = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    result = {'format_version': 1, 'target_sha256': TARGET_HASH, 'target_version': '1.17.0.1', 'inspector_sha256': sha, 'dependency_script_sha256': {}, 'original_code_executed': False, 'witnesses': {}}
    for name, (lo, hi) in WITNESSES.items():
        code = pe.get_data(lo - 0x400000, hi - lo); instructions = list(md.disasm(code, lo)); assert sum(i.size for i in instructions) == len(code), name
        result['witnesses'][name] = {'start_preferred_va': hex(lo), 'end_exclusive_preferred_va': hex(hi), 'bytes_sha256': hashlib.sha256(code).hexdigest(), 'instructions': [{'address': hex(i.address), 'bytes': i.bytes.hex(), 'mnemonic': i.mnemonic, 'operands': i.op_str} for i in instructions]}
    args.out.parent.mkdir(parents=True, exist_ok=True); args.out.write_text(json.dumps(result, indent=2) + '\n')
    if args.execute_isolated_original_code:
        run = execute(pe); run.update({'target_sha256': TARGET_HASH, 'target_version': '1.17.0.1', 'probe_script_sha256': sha, 'dependency_script_sha256': {}})
        args.probe_out.parent.mkdir(parents=True, exist_ok=True); args.probe_out.write_text(json.dumps(run, indent=2) + '\n')
        print(json.dumps({'cases': run['case_count'], 'complete': run['complete_return_count'], 'sha256': sha}))
    else: print('Wrote static enemy-strength evidence; original code not executed')


if __name__ == '__main__': main()
