#!/usr/bin/env python3
"""Recover native strategic target scores and positive selector paths.

Static by default; explicit probes execute allowlisted original instructions
and real callees only in private fixture memory, without helper stubs.
"""
import argparse, hashlib, json, platform, struct
from pathlib import Path
import capstone, pefile
import inspect_native_campaign_wave_117 as wave
import inspect_native_unit_ai_117 as units

TARGET_HASH = wave.TARGET_HASH
WITNESSES = {
    'strategic_target_score': (0x4A9460, 0x4A9515),
    'score_unit_modifier': (0x4A9520, 0x4A95B3),
    'critter_predicate': (0x429070, 0x42909B),
    'active_detector': (0x431A90, 0x431ADF),
    'air_region_target': (0x4A9800, 0x4A981D),
    'ground_region_target': (0x4A9820, 0x4A983D),
    'choose_region_target': (0x4A90D0, 0x4A9326),
    'update_region_targets': (0x4ADCF0, 0x4ADDD1),
    'pick_global_attack_target': (0x4ABA70, 0x4ABC8A),
}


def dependencies():
    mods = (wave, units, wave.attack, wave.arrival, wave.attack.removal, wave.attack.vm,
            wave.arrival.lifecycle, wave.arrival.primary, wave.arrival.military, wave.arrival.combat, wave.arrival.spatial)
    return {Path(m.__file__).name: hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in mods}


def execute(pe):
    if platform.machine().lower() not in ('x86_64', 'amd64'): raise RuntimeError('Use validated x86_64 runtime')
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
    objects, sprites, candidate_list, regions, mapbase, stack, stop = 0x800000, 0x805000, 0x806000, 0x810000, 0xB00000, 0x900000, 0xA00000
    player, p = 7, 0x67D068 + 7 * 0x4e8
    size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095; image = pe.get_memory_mapped_image()
    allow = list(WITNESSES.values()) + list(wave.WITNESSES.values()) + list(wave.attack.WITNESSES.values())
    allow += list(units.BLOCKS.values()) + list(wave.arrival.spatial.BLOCKS.values()) + list(wave.arrival.military.BLOCKS.values()) + list(wave.attack.removal.WITNESSES.values())
    allow += [wave.arrival.combat.BLOCKS['campaign_ai_flag'], wave.attack.vm.WITNESSES['unit_hostility'], wave.attack.vm.WITNESSES['global_unit_head']]
    allow += [wave.arrival.combat.BLOCKS['cloak_detection_rejection'], wave.arrival.combat.BLOCKS['is_subunit'], wave.arrival.combat.BLOCKS['cannot_chase_target'], wave.arrival.combat.BLOCKS['upgrade_level']]
    allow += [(0x41fe40, 0x41fe55)]
    cases = []
    def put(u, a, v, n=4): u.mem_write(a, (v & ((1 << (8 * n)) - 1)).to_bytes(n, 'little'))
    def get(u, a, n=4): return int.from_bytes(u.mem_read(a, n), 'little')
    def region(i): return regions + i * 0x34
    def unit(u, a=objects, ident=37, hp=100, owner=0, status=1, x=128, y=128, strength=100):
        s = sprites + ((a - objects) // 0x150 if objects <= a < objects + 0x1000 else 8) * 0x100
        put(u, a + 0x64, ident, 2); put(u, a + 8, hp * 256); put(u, a + 0xdc, status); put(u, a + 0x4c, owner, 1); put(u, a + 0x4d, 1, 1); put(u, a + 0xc, s)
        put(u, s + 0x14, x, 2); put(u, s + 0x16, y, 2)
        put(u, a + 0x2c, x * 256); put(u, a + 0x30, y * 256)
        put(u, 0x520458 + ident, 0, 1); put(u, 0x520708 + ident * 4, 100 * 256); put(u, 0x521660 + ident * 4, 0)
        put(u, 0x6ac530 + ident * 4, strength); put(u, 0x6ac530 + (228 + ident) * 4, strength)
        return a
    def new(campaign=True):
        u = Uc(UC_ARCH_X86, UC_MODE_32); u.mem_map(0x400000, size); u.mem_write(0x400000, image)
        for a, n in ((objects, 0x90000), (mapbase, 0x60000), (stack, 0x20000), (stop, 0x1000)): u.mem_map(a, n)
        u.mem_write(0x67d068, bytes(8 * 0x4e8)); put(u, p + 0x218, 0x20 if campaign else 0, 2)
        u.mem_write(0x534914, bytes([1]) * 144); put(u, 0x534914 + player * 12, 0, 1)
        put(u, 0x69b584, mapbase); put(u, mapbase, 4, 2); put(u, 0x6b1a74 + player * 4, regions)
        for y in range(32):
            for x in range(32): put(u, mapbase + 0xc + 2 * (y * 256 + x), 1, 2)
        for i in range(4):
            r, t = region(i), mapbase + 0x449fc + i * 64
            put(u, r, i, 2); put(u, r + 4, player, 1); put(u, t + 2, 1, 2)
            put(u, t + 0x10, (128 + i * 64) * 256); put(u, t + 0x14, 128 * 256)
            u.mem_write(t + 0x18, struct.pack('<4H', 96, 96, 160, 160))
        for a in (0x5c99b0, 0x5d6e48, 0x5d6e5c, 0x5da38c, 0x665ae0): put(u, a, 0)
        for ident in (69, 11, 42): put(u, 0x52c0c4 + (ident * 12 + player) * 4, 0)
        put(u, 0x534590 + player * 46 + 24, 0, 1)
        return u
    def call(u, start, ecx=objects, edx=0, args=()):
        count, reached, trace = 0, False, []; entries = {a for a, b in allow}
        def hook(m, a, n, _):
            nonlocal count, reached
            if a == stop: reached = True; m.emu_stop(); return
            if not any(lo <= a and a + n <= hi for lo, hi in allow): raise RuntimeError(f'Unreviewed instruction {a:#x}; trace{trace[-8:]}')
            count += 1
            if a in entries:
                sp = m.reg_read(UC_X86_REG_ESP); trace.append({'callee': hex(a), 'ecx': m.reg_read(UC_X86_REG_ECX), 'edx': m.reg_read(UC_X86_REG_EDX), 'stack_arguments': [get(m, sp + 4 + j * 4) for j in range(3)]})
        h = u.hook_add(UC_HOOK_CODE, hook); sp = stack + 0x1e000
        u.mem_write(sp, struct.pack('<' + 'I' * (len(args) + 1), stop, *(v & 0xffffffff for v in args)))
        u.reg_write(UC_X86_REG_ESP, sp); u.reg_write(UC_X86_REG_ECX, ecx); u.reg_write(UC_X86_REG_EDX, edx)
        try: u.emu_start(start, stop + 1, count=100000)
        except unicorn.UcError as e: raise RuntimeError(f'{e} at{u.reg_read(UC_X86_REG_EIP):#x}; trace{trace[-8:]}') from e
        finally: u.hook_del(h)
        if not reached: raise RuntimeError('No complete return within 100000 instructions')
        return {'complete_return': True, 'instruction_count': count, 'return_eax': u.reg_read(UC_X86_REG_EAX), 'native_call_trace': trace}
    def record(name, inputs, ex, actual, expected):
        assert actual == expected, (name, inputs, actual, expected)
        cases.append({'label': name, 'fixture_inputs': inputs, 'execution': ex, 'observed': actual, 'expected_from_static_contract': expected, 'matched': True, 'helper_stubs': []})
    for ident, order, extra in ((37, 1, 0), (1, 122, 0), (1, 123, 0), (1, 129, 0), (1, 130, 0), (2, 1, 3), (30, 1, 0), (34, 1, 0), (84, 1, 0), (11, 1, 0), (42, 1, 0), (57, 1, 0), (69, 1, 0)):
        for ground in (0, 1):
            u = new(); unit(u, ident=ident); put(u, objects + 0x4d, order, 1); put(u, objects + 0xc0, extra, 1)
            put(u, 0x6ac530 + ground * 228 * 4, 50)
            modified = 100
            if ident == 1 and 123 <= order <= 129: modified = 1000
            elif ident == 2: modified += extra * 100
            elif ident == 30: modified += modified >> 2
            elif ident == 34: modified += 50
            elif ident == 84: modified += 50
            ex = call(u, 0x4a9460, edx=ground)
            record('native score unitmodifier switch and healthratio', {'id': ident, 'order': order, 'ground': ground, 'base_strength': 100, 'hp': 100, 'mines': extra, 'cargo_empty': True, 'Marine_type_strength': 50}, ex, ex['return_eax'], modified * 128 // 100)
    for ident, hp, strength, status in ((35, 100, 100, 1), (36, 100, 100, 1), (59, 100, 100, 1), (97, 100, 100, 1), (89, 100, 100, 1), (37, 0, 100, 1), (37, 100, 0, 1), (37, 100, 0, 0x20001), (37, 50, 100, 0x40000001), (37, 100, 100, 0x40000001)):
        u = new(); unit(u, ident=ident, hp=hp, strength=strength, status=status); ex = call(u, 0x4a9460)
        value = 0 if ident in (35, 36, 59, 97) or status & 0x40000000 and hp < 100 else 1 if ident == 89 else strength * 128 // max(hp, 1)
        if value == 0 and ident not in (35, 36, 59, 97) and not status & 0x40000000: value = 2 if status & 0x20000 else 1
        record('native score exclusions hallucination floors and zerohealth denominator', {'id': ident, 'hp': hp, 'strength': strength, 'status': status}, ex, ex['return_eax'], value)
    for ident in (11, 42, 57, 69, 125):
        for ground in (0, 1):
            u = new(); unit(u, ident=ident); cargo = unit(u, 0x5da4f0, ident=0)
            put(u, cargo + 0xa5, 3, 1); put(u, 0x523ef8, 2, 1); put(u, objects + 0xb0, 2 + (3 << 11), 2)
            put(u, 0x6ac530 + ground * 228 * 4, 50)
            ex = call(u, 0x4a9460, edx=ground); numerator = 100 if ident == 125 else 300
            record('native score transport cargo bonus and Bunker multiplied Marine strength', {'id': ident, 'ground': ground, 'cargo_packed_id': 2 + (3 << 11), 'cargo_native_unit': cargo, 'cargo_space_used': 2, 'type_strength': 100, 'Marine_type_strength': 50}, ex, ex['return_eax'], numerator * 128 // 100)
    for values, status_flags, detection, campaign, owners in (([100, 50], [1, 1], [0, 0], True, [0, 0]), ([50, 100], [1, 1], [0, 0], True, [0, 0]), ([100, 100], [1, 1], [0, 0], True, [0, 0]), ([100, 50], [0x4000001, 1], [0, 0], True, [0, 0]), ([100, 50], [0x301, 1], [0, 0], True, [0, 0]), ([100, 50], [0x301, 1], [128, 0], True, [0, 0]), ([100, 50], [1, 1], [0, 0], True, [7, 0])):
        u = new(campaign); candidates = []
        for j, hp in enumerate(values):
            a = objects + j * 0x150; unit(u, a, ident=37, hp=hp, owner=owners[j], status=status_flags[j]); put(u, a + 0xe4, detection[j]); candidates.append(a)
        u.mem_write(candidate_list, struct.pack('<III', *candidates, 0)); put(u, 0x5d6e48, 1); put(u, 0x5d6e4c, 0); put(u, 0x5d6e5c, 3)
        valid = [j for j in range(2) if owners[j] == 0 and not status_flags[j] & 0x4000000 and (not status_flags[j] & 0x300 or detection[j] & 128)]
        selected = max(valid, key=lambda j: 100 * 128 // values[j]) if valid else None
        ex = call(u, 0x4a90d0, player, 0, (1, candidate_list, 1))
        record('complete positive regional selector maximum score firsttie filters and queryrelease', {'hp': values, 'status': status_flags, 'detection_masks': detection, 'campaign': campaign, 'owners': owners, 'candidate_list_fixture': candidates}, ex,
               [ex['return_eax'], get(u, 0x5d6e48), get(u, 0x5d6e5c)], [candidates[selected] if selected is not None else 0, 0, 0])
    for campaign in (False, True):
        u = new(campaign); a = unit(u, 0x5da3a0, ident=37)
        for table, lo, hi in ((0x5c99b8, 120, 136), (0x5d03f8, 120, 136)):
            put(u, table, 1); put(u, table + 4, lo); put(u, table + 8, 1); put(u, table + 12, hi)
        for off, value in ((0x13c, 0), (0x140, 1), (0x144, 0), (0x148, 1)): put(u, a + off, value)
        put(u, 0x5c99b0, 2); ex = call(u, 0x4adcf0, region(1))
        record('complete positive regional targetupdate actual spatialquery score and release', {'campaign': campaign, 'enemy_unit': a, 'empty_military_list': True}, ex,
               [get(u, region(1) + 0x1c), get(u, region(1) + 0x20), get(u, 0x5d6e48), get(u, 0x5d6e5c)], [a, a, 0, 0])
    for campaign in (False, True):
        for hostile in (False, True):
            u = new(campaign); a = unit(u, ident=89, owner=0 if hostile else player)
            u.mem_write(candidate_list, struct.pack('<II', a, 0)); put(u, 0x5d6e48, 1); put(u, 0x5d6e4c, 0)
            ex = call(u, 0x4a90d0, player, 0, (1, candidate_list, 1))
            record('regional critter acceptance differs by campaign and permits alliedcritter noncampaign', {'campaign': campaign, 'critter_id': 89, 'owner': 0 if hostile else player}, ex, ex['return_eax'], 0 if campaign else a)
    for ground in (0, 1):
        for flying in (False, True):
            u = new(); first, second = objects, objects + 0x150
            unit(u, first, ident=37, status=5 if flying else 1); unit(u, second, ident=38, status=5 if flying else 1)
            put(u, 0x6ac530 + 37 * 4, 100); put(u, 0x6ac530 + 38 * 4, 200)
            put(u, 0x6ac530 + (228 + 37) * 4, 200); put(u, 0x6ac530 + (228 + 38) * 4, 100)
            u.mem_write(candidate_list, struct.pack('<III', first, second, 0)); put(u, 0x5d6e48, 1); put(u, 0x5d6e4c, 0)
            ex = call(u, 0x4a90d0, player, 0, (1, candidate_list, ground))
            record('regional airground selector indexes threatstrength not candidate flyingcategory', {'ground_score_mode': ground, 'both_candidates_flying': flying, 'air_strengths': [100, 200], 'ground_strengths': [200, 100]}, ex, ex['return_eax'], first if ground else second)
    for reacts, disabled, blind in ((False, False, False), (True, False, False), (True, True, False), (True, False, True)):
        u = new(); first, second, source = objects, objects + 0x150, objects + 0x300
        unit(u, first, ident=37); unit(u, second, ident=15, status=1 | (0x20000 if reacts else 0) | (0x400 if disabled else 0))
        unit(u, source, ident=8, owner=player, status=5); put(u, 0x521660 + 15 * 4, 0x8000); put(u, second + 0x123, int(blind), 1)
        put(u, 0x5211d8 + 8, 0, 1); put(u, 0x5213a8 + 8, 0, 1)
        u.mem_write(candidate_list, struct.pack('<III', first, second, 0)); put(u, 0x5d6e48, 1); put(u, 0x5d6e4c, 0)
        ex = call(u, 0x4a90d0, player, source, (1, candidate_list, 1))
        record('Wraith caller adds1000 for active Reacts detector using real attackanddetector predicates', {'source_id': 8, 'candidate_detector_property': True, 'detector_reacts': reacts, 'detector_disabled': disabled, 'detector_blind': blind}, ex, ex['return_eax'], second if reacts and not disabled and not blind else first)
    for x in (256, 320, 321, 384):
        u = new(); target, source, guard = objects, objects + 0x150, objects + 0x1000
        unit(u, target, x=x); unit(u, source, ident=43, owner=player, status=5)
        put(u, 0x5211d8 + 43, 0, 1); put(u, 0x5213a8 + 43, 0, 1)
        put(u, source + 0x134, guard); put(u, guard + 8, 1, 1); put(u, guard + 0x12, 128, 2); put(u, guard + 0x14, 128, 2)
        put(u, mapbase + 0xc + 2 * (4 * 256 + x // 32), 2, 2)
        u.mem_write(candidate_list, struct.pack('<II', target, 0)); put(u, 0x5d6e48, 1); put(u, 0x5d6e4c, 0)
        ex = call(u, 0x4a90d0, player, source, (1, candidate_list, 1))
        record('guard caller target inanotherregion requires both units within192ofhome', {'guard_home': [128, 128], 'source_position': [128, 128], 'enemy_position': [x, 128], 'target_region': 2, 'home_region': 1}, ex, ex['return_eax'], target if x <= 320 else 0)
    u = new(); a = unit(u, hp=1, strength=0x01000000)
    u.mem_write(candidate_list, struct.pack('<II', a, 0)); put(u, 0x5d6e48, 1); put(u, 0x5d6e4c, 0)
    ex = call(u, 0x4a90d0, player, 0, (1, candidate_list, 1))
    record('regional signed score maximum rejects highbit score against initialminusone', {'hp': 1, 'strength': 0x01000000, 'score_after_u32_shift': 0x80000000}, ex, ex['return_eax'], 0)
    for building_status, weapons, reverse in ((3, True, False), (3, True, True), (0x303, True, False), (0x4000003, True, False), (3, False, False), (1, True, False)):
        u = new(); own, enemy_building, enemy_other, military = objects, objects + 0x150, objects + 0x2a0, objects + 0x1000
        unit(u, own, ident=43, owner=player, status=5)
        unit(u, enemy_building, ident=106, status=building_status, x=128)
        unit(u, enemy_other, ident=0, status=1, x=160)
        put(u, region(1) + 0x30, military); put(u, military + 0xc, own)
        put(u, 0x5211d8 + 43, 0 if weapons else 130, 1); put(u, 0x5213a8 + 43, 0 if weapons else 130, 1)
        first, second = (enemy_other, enemy_building) if reverse else (enemy_building, enemy_other)
        put(u, 0x665ae0, first); put(u, first + 4, second); put(u, second + 4, 0)
        ex = call(u, 0x4aba70, region(1))
        expected = first if not weapons else enemy_building if building_status & 2 else enemy_other
        record('global attacktarget prefers building tier over nearerunit and retains unmatched fallback', {'enemy_building_status': building_status, 'military_has_weapons': weapons, 'global_order_reversed': reverse, 'source_flying': True, 'building_position': [128, 128], 'unit_position': [160, 128], 'source_center': [192, 128]}, ex, ex['return_eax'], expected)
    for reverse in (False, True):
        u = new(); own, first_building, second_building, military = objects, objects + 0x150, objects + 0x2a0, objects + 0x1000
        unit(u, own, ident=43, owner=player, status=5); unit(u, first_building, ident=106, status=3, x=128); unit(u, second_building, ident=106, status=3, x=256)
        put(u, region(1) + 0x30, military); put(u, military + 0xc, own); put(u, 0x5211d8 + 43, 0, 1); put(u, 0x5213a8 + 43, 0, 1)
        first, second = (second_building, first_building) if reverse else (first_building, second_building)
        put(u, 0x665ae0, first); put(u, first + 4, second); put(u, second + 4, 0); ex = call(u, 0x4aba70, region(1))
        record('global nearest building strictdistance equality preserves firstlist candidate', {'buildings_positions': [[128, 128], [256, 128]], 'source_center': [192, 128], 'global_order_reversed': reverse}, ex, ex['return_eax'], first)
    for group_matches in (False, True):
        u = new(); own, enemy, military = objects, objects + 0x150, objects + 0x1000
        unit(u, own, ident=37, owner=player, status=1); unit(u, enemy, ident=106, status=3, x=512, y=512)
        put(u, mapbase + 0xc + 2 * (16 * 256 + 16), 2, 2); put(u, mapbase + 0x449fc + 2 * 64 + 2, 1 if group_matches else 2, 2)
        put(u, region(1) + 0x30, military); put(u, military + 0xc, own); put(u, own + 0x105, 1, 1)
        put(u, 0x5211d8 + 37, 0, 1); put(u, 0x5213a8 + 37, 0, 1); put(u, 0x665ae0, enemy)
        ex = call(u, 0x4aba70, region(1))
        record('ground global selector complete actual transportaccessors and pointgroup acceptance', {'source_region': 1, 'enemy_region': 2, 'terrain_groups_match': group_matches, 'completed_transports': 0, 'movement_check_flag': True}, ex, ex['return_eax'], enemy if group_matches else 0)
    return {'format_version': 1, 'original_code_executed': True, 'complete_game_executed': False, 'windows_or_host_api_called': False, 'helper_stubs': [], 'all_matched': True,
            'case_count': len(cases), 'complete_return_count': len(cases), 'prefix_case_count': 0, 'cases': cases,
            'allowlisted_preferred_va_ranges': [[hex(a), hex(b)] for a, b in allow],
            'runtime_versions': {'python': platform.python_version(), 'unicorn': unicorn.__version__, 'pefile': pefile.__version__, 'capstone': capstone.__version__, 'architecture': platform.machine(), 'system': platform.system()}}


def main():
    p = argparse.ArgumentParser(description=__doc__); p.add_argument('--exe', type=Path, required=True); p.add_argument('--out', type=Path, required=True)
    p.add_argument('--execute-isolated-original-code', action='store_true'); p.add_argument('--probe-out', type=Path); a = p.parse_args()
    if a.execute_isolated_original_code and not a.probe_out: p.error('Execution requires --probe-out')
    raw = a.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != TARGET_HASH: p.error('Unknown executable hash')
    pe = pefile.PE(data=raw); assert pe.OPTIONAL_HEADER.ImageBase == 0x400000 and pe.FILE_HEADER.Machine == 0x14c
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); sha = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    result = {'format_version': 1, 'target_sha256': TARGET_HASH, 'target_version': '1.17.0.1', 'inspector_sha256': sha, 'dependency_script_sha256': dependencies(), 'original_code_executed': False, 'witnesses': {}}
    for n, (lo, hi) in WITNESSES.items():
        code = pe.get_data(lo - 0x400000, hi - lo); ins = list(md.disasm(code, lo)); assert sum(i.size for i in ins) == len(code), n
        result['witnesses'][n] = {'start_preferred_va': hex(lo), 'end_exclusive_preferred_va': hex(hi), 'bytes_sha256': hashlib.sha256(code).hexdigest(), 'instructions': [{'address': hex(i.address), 'bytes': i.bytes.hex(), 'mnemonic': i.mnemonic, 'operands': i.op_str} for i in ins]}
    targets = struct.unpack('<7I', pe.get_data(0x4a95b4 - 0x400000, 28)); mapping = pe.get_data(0x4a95d0 - 0x400000, 84)
    result['score_unit_modifier_switch'] = {'target_table': '0x4a95b4', 'mapping_table': '0x4a95d0', 'unit_id1_to84': [hex(targets[x]) for x in mapping], 'unit_id0_or_above84_default': '0x4a95aa'}
    a.out.parent.mkdir(parents=True, exist_ok=True); a.out.write_text(json.dumps(result, indent=2) + '\n')
    if a.execute_isolated_original_code:
        run = execute(pe); run.update({'target_sha256': TARGET_HASH, 'target_version': '1.17.0.1', 'probe_script_sha256': sha, 'dependency_script_sha256': dependencies()})
        a.probe_out.parent.mkdir(parents=True, exist_ok=True); a.probe_out.write_text(json.dumps(run, indent=2) + '\n'); print(json.dumps({'cases': run['case_count'], 'complete': run['complete_return_count'], 'sha256': sha}))
    else: print('Wrote static strategic-target evidence; original code not executed')


if __name__ == '__main__': main()
