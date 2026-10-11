#!/usr/bin/env python3
"""Recover initial campaign roster recruitment through original donor selectors.

Static by default. Explicit probes execute reviewed original callees only,
with private pools/objects, stock orders.dat and no helper stubs.
"""
import argparse, hashlib, json, platform, struct
from pathlib import Path
import capstone, pefile
import inspect_native_strategic_targets_117 as targets
import inspect_native_default_production_117 as default
wave = targets.wave
arrival = wave.arrival
TARGET_HASH = targets.TARGET_HASH
WITNESSES = {
    'periodic_player_recruitment': (0x4AF930, 0x4AFAE5),
    'recruit_missing_attack_roster': (0x4AB040, 0x4AB0E1),
    'adopt_one_requested_type': (0x4AE960, 0x4AEA65),
    'choose_surplus_military': (0x4AC230, 0x4AC3BB),
    'choose_lower_priority_unit': (0x4AC0D0, 0x4AC226),
    'recruitable_unit': (0x4AAF70, 0x4AB038),
    'attacking_region_predicate': (0x4AB0F0, 0x4AB104),
    'currently_threatened_by_target': (0x41FD90, 0x41FE17),
    'attack_recent_or_never_predicate': (0x483570, 0x483594),
    'transport_requests_disabled': (0x483350, 0x483364),
    'recruit_nearby_guard_callback': (0x4ADA40, 0x4ADA8A),
    'terrain_region_count': (0x481B00, 0x481B09),
    'clear_request_flag_and_decrement_counter': (0x482900, 0x482930),
    'region_route_identical_ids_return': (0x496AC0, 0x496AE5),
}


def dependencies():
    mods = (default, default.production, default.dispatch, default.dispatch.training, default.dispatch.research, default.dispatch.training.unit_reqs, default.dispatch.research.requirements, targets, wave, targets.units, wave.attack, arrival, wave.attack.removal, wave.attack.vm,
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
    allow += list(default.WITNESSES.values()) + list(default.production.WITNESSES.values())
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
    def call(u, start, ecx=None, edx=0, args=()):
        count, reached, trace = 0, False, []; entries = {a for a, b in allow}
        def hook(m, a, n, _):
            nonlocal count, reached
            if a == stop: reached = True; m.emu_stop(); return
            if not any(lo <= a and a + n <= hi for lo, hi in allow): raise RuntimeError(f'Unreviewed instruction {a:#x}; trace{trace[-8:]}')
            count += 1
            if a in entries:
                sp = m.reg_read(UC_X86_REG_ESP); trace.append({'callee': hex(a), 'ecx': m.reg_read(UC_X86_REG_ECX), 'edx': m.reg_read(UC_X86_REG_EDX), 'stack_arguments': [get(m, sp + 4 + j * 4) for j in range(3)]})
        h = u.hook_add(UC_HOOK_CODE, hook); sp = stack + 0x1e000
        u.mem_write(sp, struct.pack('<'+'I'*(len(args)+1), stop, *args)); u.reg_write(UC_X86_REG_ESP, sp); u.reg_write(UC_X86_REG_ECX, region(1) if ecx is None else ecx); u.reg_write(UC_X86_REG_EDX, edx)
        try: u.emu_start(start, stop + 1, count=100000)
        except unicorn.UcError as e: raise RuntimeError(f'{e} at{u.reg_read(UC_X86_REG_EIP):#x}; trace{trace[-8:]}') from e
        finally: u.hook_del(h)
        if not reached: raise RuntimeError('No complete return within 100000 instructions')
        return {'complete_return': True, 'instruction_count': count, 'return_eax': u.reg_read(UC_X86_REG_EAX), 'native_call_trace': trace}
    def record(label, inputs, ex, actual, expected):
        assert actual == expected, (label, inputs, actual, expected)
        cases.append({'label': label, 'fixture_inputs': inputs, 'execution': ex, 'observed': actual, 'expected_from_static_contract': expected, 'matched': True, 'helper_stubs': []})
    spare = node + 0x1000
    def setup(campaign=True, ai_type=4, donor_priority=40, surplus=True, available=True, roster=(44,), recent=False):
        u = new(campaign, enemy_x=128, enemy_y=128)
        put(u, 0x665ae0, 0); put(u, 0x5c99b0, 0)
        put(u, own + 0x64, 43, 2); put(u, own + 0xdc, 5); put(u, own + 0x5c, 0)
        put(u, 0x521660 + 43*4, 4); put(u, 0x521578 + 43, 0, 1)
        put(u, 0x520458 + 43, 0, 1); put(u, 0x520708 + 43*4, 100*256)
        put(u, 0x5211d8 + 43, 130, 1); put(u, 0x5213a8 + 43, 130, 1)
        put(u, sprite + 0x14, 512, 2); put(u, sprite + 0x16, 512, 2)
        put(u, mapbase + 0xc + 2*(16*256+16), 3, 2)
        put(u, mapbase + 0x449fc + 3*64 + 7, 1, 1)
        put(u, mapbase + 0x449fc + 3*64 + 0xc, mapbase + 0x60010)
        put(u, mapbase + 0x60010, 1, 2)
        put(u, region(1) + 0x30, 0); put(u, region(1) + 0xa, 0, 2)
        put(u, region(3) + 5, 4, 1); put(u, region(3) + 6, donor_priority, 1)
        put(u, region(3) + 0x30, node if ai_type==4 else 0)
        put(u, region(3) + 0x14, 100 if surplus else 30, 2); put(u, region(3) + 0x16, 100 if surplus else 20, 2)
        put(u, node + 8, ai_type, 1); put(u, node + 0x10, region(3)); put(u, pool + 0x4e20, spare if available else 0)
        put(u, p + 0x218, (0x20 if campaign else 0) | 8, 2)
        put(u, p + 0x224, 80 if recent else 0)
        u.mem_write(p + 0x230, struct.pack('<'+'H'*(len(roster)+1), *roster, 0))
        put(u, 0x523520 + 43*2, 10, 2); put(u, 0x5236e8 + 43*2, 5, 2); put(u, 0x523e10 + 43, 2, 1)
        u.mem_write(own + 0x98, struct.pack('<5H', *([228]*5)))
        return u
    def heap(u):
        return [{'priority': get(u,p+0x18+j*8,1),'kind':get(u,p+0x19+j*8,1),'id':get(u,p+0x1a+j*8,2),'context':get(u,p+0x1c+j*8)} for j in range(get(u,p+0x210,1))]
    def result(u):
        a=get(u,own+0x134)
        return {'unit_ai':a, 'assigned_region':get(u,a+0x10) if a else 0, 'guard_parent':get(u,node+0xc) if get(u,node+8,1)==1 else None,
                'order':get(u,own+0x4d,1),'destination_head':get(u,region(1)+0x30),'donor_head':get(u,region(3)+0x30),
                'donor_strengths':[get(u,region(3)+off,2) for off in (0x10,0x12,0x14,0x16)],
                'heap':heap(u),'demand':[get(u,p+j*4)for j in range(3)],'rng_calls':get(u,0x673938)}
    for campaign in (False,True):
        for ai_type in (1,4):
            for available in (False,True):
                u=setup(campaign,ai_type,available=available); ex=call(u,0x4ab040)
                adopted=available or ai_type==4
                expected_ai=(node if ai_type==4 else spare) if adopted else 0
                want={'unit_ai':expected_ai,'assigned_region':region(1) if adopted else 0,'guard_parent':0 if ai_type==1 else None,
                      'order':158 if adopted else 1,'destination_head':expected_ai,'donor_head':0,'donor_strengths':[0,0,0,0],
                      'heap':[],'demand':[0,0,0],'rng_calls':3 if adopted else 0}
                record('actual missingroster adoption from guard or surplus military including failed destination allocation', {'campaign':campaign,'AI_type':ai_type,'initial_destination_pool_available':available,'requested':43},ex,result(u),want)
    for campaign,recent in ((True,False),(False,False),(False,True)):
        for priority in (59,60,100,159,160):
            u=setup(campaign,donor_priority=priority,surplus=False,recent=recent); ex=call(u,0x4ab040)
            threshold=60 if campaign or recent else 160; adopts=priority<threshold
            want={'unit_ai':node,'assigned_region':region(1) if adopts else region(3),'guard_parent':None,
                  'order':158 if adopts else 1,'destination_head':node if adopts else 0,'donor_head':0 if adopts else node,
                  'donor_strengths':[0,0,0,0] if adopts else [0,0,30,20],
                  'heap':[] if adopts else [{'priority':60,'kind':1,'id':43,'context':region(1)}],
                  'demand':[0,0,0] if adopts else [10,5,2],'rng_calls':3 if adopts else 0}
            record('fallback priority strictcomparison campaign60 or noncampaign160 recent30 gate', {'campaign':campaign,'recent_last_attack':recent,'donor_priority':priority,'surplus':False},ex,result(u),want)
    for gate in ('incomplete','dying','property10','other_type','low_health','invalid_AI_class'):
        u=setup(ai_type=1)
        if gate=='incomplete':put(u,own+0xdc,4)
        elif gate=='dying':put(u,own+0x4d,0,1)
        elif gate=='property10':put(u,0x521660+43*4,0x14)
        elif gate=='other_type':put(u,own+0x64,44,2)
        elif gate=='low_health':put(u,own+8,49*256)
        elif gate=='invalid_AI_class':put(u,node+8,2,1)
        ex=call(u,0x4ab040)
        actual=result(u)
        requests=[] if gate=='incomplete' else [{'priority':60,'kind':1,'id':43,'context':region(1)}]
        record('candidate exclusion or campaign incomplete-roster satisfaction',{'gate':gate},ex,
               [actual['unit_ai'],actual['assigned_region'],actual['order'],actual['heap'],actual['demand'],actual['rng_calls']],
               [node,region(3),0 if gate=='dying'else 1,requests,[10,5,2] if requests else [0,0,0],0])
    for roster in ((),(44,44),(230,)):
        u=setup(ai_type=1,roster=roster);ex=call(u,0x4ab040);actual=result(u)
        adopts=bool(roster and roster[0]==44)
        wants=[]if len(roster)<2 else [{'priority':60,'kind':1,'id':43,'context':region(1)}]
        record('empty roster sentinel and duplicate requests preserve matching count',{'roster_words':roster},ex,
               [actual['assigned_region'],actual['order'],actual['heap'],actual['demand']],
               [region(1)if adopts else region(3),158 if adopts else 1,wants,[10,5,2]if wants else[0,0,0]])
    for all_air,all_ground,donor_state in ((20,31,4),(19,31,4),(20,30,4),(20,31,7),(20,31,8),(20,31,9)):
        u=setup(donor_priority=200);put(u,region(3)+0x16,all_air,2);put(u,region(3)+0x14,all_ground,2);put(u,region(3)+5,donor_state,1)
        ex=call(u,0x4ab040);actual=result(u);adopts=all_air>=20 and all_ground>30 and donor_state not in (7,8,9)
        record('surplus donor air permits equality ground requires strict surplus and states789 reject primary selector',{'all_air':all_air,'all_ground':all_ground,'donor_state':donor_state,'priority':200},ex,
               [actual['assigned_region'],actual['order'],actual['heap']],
               [region(1)if adopts else region(3),158 if adopts else 1,[]if adopts else[{'priority':60,'kind':1,'id':43,'context':region(1)}]])
    for hp in (49,50,51):
        u=setup(ai_type=1);put(u,own+8,hp*256);ex=call(u,0x4ab040);actual=result(u)
        record('ordinary recruit health threshold admits exact half maximum life',{'hp':hp,'max_life':100},ex,
               [actual['assigned_region'],actual['heap']],
               [region(1)if hp>=50 else region(3),[]if hp>=50 else[{'priority':60,'kind':1,'id':43,'context':region(1)}]])
    u=setup(donor_priority=200,roster=(38,));put(u,own+0x64,37,2);put(u,own+0xdc,1);put(u,own+0x138,0,2)
    put(u,0x521660+37*4,0);put(u,0x521578+37,0,1);put(u,sprite+0x14,128,2);put(u,sprite+0x16,128,2)
    put(u,region(3)+0x14,31,2);put(u,region(3)+0x16,0,2);put(u,region(3)+0xa,1,2)
    ex=call(u,0x4ab040)
    record('ground donor assignment differs from actual position and route helper returns through identical IDs',{'id':37,'actual_region':1,'old_assigned_region':3,'route_ids':[1,1]},ex,
           [get(u,node+0x10),get(u,own+0x4d,1),get(u,region(1)+0xa,2),get(u,region(3)+0xa,2),heap(u)], [region(1),158,1,0,[]])
    for campaign in (False,True):
        u=setup(campaign,ai_type=1);ex=call(u,0x4af930,player)
        record('complete original periodic player sweep enters state8 recruitment and original allocation arrival',{'campaign':campaign,'other_region_states':[0,0,4],'transport_requests_flag8':True},ex,
               [get(u,own+0x134),get(u,spare+0x10),get(u,own+0x4d,1),heap(u),get(u,0x673938)], [spare,region(1),158,[],3])
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
    else: print('Wrote static recruitment evidence; original code not executed')


if __name__ == '__main__': main()
