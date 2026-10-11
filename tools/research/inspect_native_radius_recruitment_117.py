#!/usr/bin/env python3
"""Recover positive radius guard recruitment and native regional cursor sweep.

Static by default. Explicit probes execute reviewed original callees only,
with private pools/objects, stock orders.dat and no helper stubs.
"""
import argparse, hashlib, json, platform, struct
from pathlib import Path
import capstone, pefile
import inspect_native_strategic_targets_117 as targets
import inspect_native_default_production_117 as default
import inspect_native_campaign_recruitment_117 as recruit
import inspect_native_enemy_strength_117 as threat
wave = targets.wave
arrival = wave.arrival
TARGET_HASH = targets.TARGET_HASH
WITNESSES = {
    'recruit_nearby_guard_callback': (0x4ADA40, 0x4ADA8A),
    'regional_cursor_sweep': (0x4B0D70, 0x4B0E10),
}



def dependencies():
    mods = (recruit, threat, wave.attack.removal.transfer, wave.attack.removal.transfer.guards, wave.attack.removal.transfer.workers, default, default.production, default.dispatch, default.dispatch.training, default.dispatch.research, default.dispatch.training.unit_reqs, default.dispatch.research.requirements, targets, wave, targets.units, wave.attack, arrival, wave.attack.removal, wave.attack.vm,
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
    allow += list(recruit.WITNESSES.values()) + list(threat.WITNESSES.values())
    allow += list(wave.attack.removal.transfer.WITNESSES.values()) + list(wave.attack.removal.transfer.guards.WITNESSES.values()) + list(wave.attack.removal.transfer.workers.BLOCKS.values())
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
        count, reached, trace, slots = 0, False, [], 0; entries = {a for a, b in allow}
        def hook(m, a, n, _):
            nonlocal count, reached, slots
            if a == stop: reached = True; m.emu_stop(); return
            if not any(lo <= a and a + n <= hi for lo, hi in allow): raise RuntimeError(f'Unreviewed instruction {a:#x}; trace{trace[-8:]}')
            count += 1
            if a == 0x4b0d84: slots += 1
            if a in entries:
                sp = m.reg_read(UC_X86_REG_ESP); trace.append({'callee': hex(a), 'ecx': m.reg_read(UC_X86_REG_ECX), 'edx': m.reg_read(UC_X86_REG_EDX), 'stack_arguments': [get(m, sp + 4 + j * 4) for j in range(3)]})
        h = u.hook_add(UC_HOOK_CODE, hook); sp = stack + 0x1e000
        u.mem_write(sp, struct.pack('<'+'I'*(len(args)+1), stop, *args)); u.reg_write(UC_X86_REG_ESP, sp); u.reg_write(UC_X86_REG_ECX, region(1) if ecx is None else ecx); u.reg_write(UC_X86_REG_EDX, edx)
        try: u.emu_start(start, stop + 1, count=100000)
        except unicorn.UcError as e: raise RuntimeError(f'{e} at{u.reg_read(UC_X86_REG_EIP):#x}; trace{trace[-8:]}') from e
        finally: u.hook_del(h)
        if not reached: raise RuntimeError('No complete return within 100000 instructions')
        return {'complete_return': True, 'instruction_count': count, 'return_eax': u.reg_read(UC_X86_REG_EAX), 'native_call_trace': trace, 'regional_slot_visits': slots}
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
    candidate = 0x5da3a0
    def radius_fixture(available=True, gate=None, counter=0):
        u=setup(ai_type=1,available=available);u.mem_write(candidate,bytes(u.mem_read(own,0x150)))
        put(u,candidate+0xc,sprite);put(u,candidate+0x134,node);put(u,node+0xc,candidate);put(u,node+9,counter,1)
        put(u,sprite+0x14,128,2);put(u,sprite+0x16,128,2);put(u,0x665b08+player*4,candidate)
        u.mem_write(0x6726d0,bytes(36*8));put(u,0x6726d0+player*36,1,1)
        for off in (0x1c,0x20,0x24,0x28):put(u,region(1)+off,candidate)
        for table,lo,hi in ((0x5c99b8,120,136),(0x5d03f8,120,136)):
            put(u,table,1);put(u,table+4,lo);put(u,table+8,1);put(u,table+12,hi)
        for off,value in ((0x13c,0),(0x140,1),(0x144,0),(0x148,1)):put(u,candidate+off,value)
        put(u,0x5c99b0,2)
        if gate=='no_AI':put(u,candidate+0x134,0)
        elif gate=='other_AI':put(u,node+8,4,1)
        elif gate=='in_building':put(u,candidate+0xdc,0x25)
        elif gate=='other_owner':put(u,candidate+0x4c,0,1)
        elif gate=='other_region':put(u,mapbase+0xc+2*(4*256+4),2,2)
        elif gate=='incomplete':put(u,candidate+0xdc,4)
        elif gate=='zero_order':put(u,candidate+0x4d,0,1)
        return u
    def radius_result(u):
        return [get(u,candidate+0x134),get(u,spare+0x10),get(u,node+0xc),get(u,node+9,1),get(u,candidate+0x4d,1),get(u,region(1)+0x30),get(u,0x673938),[get(u,region(1)+off)for off in(0x1c,0x20,0x24,0x28)],get(u,0x5d6e48),get(u,0x5d6e5c)]
    for available in (False,True):
        for counter in (0,99,100,255):
            u=radius_fixture(available,counter=counter);ex=call(u,0x4ada40,candidate,region(1))
            want=[spare if available else 0,region(1)if available else 0,0,counter+1 if counter<100 else counter,158 if available else 1,spare if available else 0,3 if available else 0,[0,0,candidate,0] if available else [0]*4,0,0]
            record('positive guard radiuscallback executes full genericdetach counterupdate and actual militaryallocation',{'pool_available':available,'guard_counter':counter,'direct_callback':True},ex,radius_result(u),want)
    for gate in ('no_AI','other_AI','in_building','other_owner','other_region','incomplete','zero_order'):
        u=radius_fixture(gate=gate);ex=call(u,0x4ada40,candidate,region(1));adopts=gate in ('incomplete','zero_order')
        issues=adopts and gate!='incomplete'
        want=[spare if adopts else 0 if gate=='no_AI'else node,region(1)if adopts else 0,0 if adopts else candidate,1 if adopts else 0,0 if gate=='zero_order' else 158 if issues else 1,spare if adopts else 0,3 if issues else 0,[0,0,candidate,0] if adopts else[candidate]*4,0,0]
        record('radiuscallback exact AI owner region status gates omit completion and activeorder filter',{'gate':gate,'direct_callback':True},ex,radius_result(u),want)
    for available in (False,True):
        u=radius_fixture(available);ex=call(u,0x4b15d0,region(3),region(1),(1,1,1))
        record('complete transfer extraRadius executes actual positive spatial query and guard callback',{'source_military_empty':True,'pool_available':available,'arguments':[1,1,1]},ex,radius_result(u),
               [spare if available else 0,region(1)if available else 0,0,1,158 if available else 1,spare if available else 0,3 if available else 0,[0,0,candidate,0] if available else [0]*4,0,0])
    second,second_guard,second_free=0x5da4f0,node+0x80,spare+0x20
    for free_count in (0,1,2):
        u=radius_fixture(free_count>0);u.mem_write(second,bytes(u.mem_read(candidate,0x150)))
        put(u,second+0xc,sprite+0x100);put(u,second+0x134,second_guard);put(u,second+0x6c,0)
        put(u,second_guard+8,1,1);put(u,second_guard+0xc,second);put(u,candidate+0x6c,second)
        u.mem_write(sprite+0x100,bytes(u.mem_read(sprite,0x28)));put(u,sprite+0x100+0x14,144,2);put(u,sprite+0x100+0x16,144,2)
        for table in (0x5c99b8,0x5d03f8):
            u.mem_write(table,struct.pack('<8I',1,120,1,136,2,136,2,152))
        for a,indices in ((candidate,(0,1,0,1)),(second,(2,3,2,3))):
            for off,value in zip((0x13c,0x140,0x144,0x148),indices):put(u,a+off,value)
        put(u,0x5c99b0,4)
        if free_count==2:put(u,spare,second_free);put(u,second_free+4,spare)
        ex=call(u,0x4b15d0,region(3),region(1),(1,1,1))
        record('positive radius recruits two guards in query order and preserves first allocation on later pool exhaustion',{'private_free_nodes':free_count,'guard_positions':[[128,128],[144,144]],'sorted_edges':[[1,120],[1,136],[2,136],[2,152]]},ex,
               [get(u,candidate+0x134),get(u,second+0x134),get(u,node+0xc),get(u,second_guard+0xc),get(u,node+9,1),get(u,second_guard+9,1),get(u,candidate+0x4d,1),get(u,second+0x4d,1),get(u,region(1)+0x30),get(u,0x673938),get(u,0x5d6e48),get(u,0x5d6e5c)],
               [spare if free_count else 0,second_free if free_count==2 else 0,0,0,1,1,158 if free_count else 1,158 if free_count==2 else 1,second_free if free_count==2 else spare if free_count else 0,free_count*3,0,0])
    for start_player,start_region,campaign in ((0,1,True),(0,2300,True),(3,2498,True),(7,2498,True),(7,2498,False)):
        u=setup(campaign,roster=());u.mem_write(0x6726d0,bytes(36*8));put(u,0x6b1a94,start_player);put(u,0x6b1a98,start_region)
        for q in range(8):
            base=regions+0x10000+q*0x1000;put(u,0x6b1a74+q*4,base)
            put(u,base+0x34+0x18,123,2)
        ex=call(u,0x4b0d70,0)
        total=start_region+333;next_player=start_player;next_region=total
        if total>=2500:next_player=(start_player+1)%8;next_region=total-2499
        rebuild_calls=sum(t['callee']=='0x4abf90'for t in ex['native_call_trace'])
        record('regional sweep performs333 slots advancesat2500 and rebuild only successful playerzero wrap',{'start_player':start_player,'start_region':start_region,'all_player_types_zero':True},ex,
               [get(u,0x6b1a94),get(u,0x6b1a98),rebuild_calls,ex['regional_slot_visits'],[get(u,regions+0x10000+q*0x1000+0x34+0x18,2)for q in range(8)]],
               [next_player,next_region,1 if total>=2500 else 0,333,[0 if next_player==0 and total>=2500 else 123]*8])
    for state in (8,9,10):
        u=setup();put(u,0x6b1a94,7);put(u,0x6b1a98,1);u.mem_write(0x6726d0,bytes(36*8));put(u,0x6726d0+7*36,1,1)
        put(u,region(1)+5,state,1);put(u,region(1)+7,2,1);put(u,region(1)+8,4,1)
        # Only the first region is admitted; later slots are outside count2.
        put(u,mapbase,2,2);put(u,p+0x230,44,2);put(u,0x665b08+28,0)
        ex=call(u,0x4b0d70)
        record('actual scheduler admits exactly one dispatcher slot retaining prepared rosterwait or invalidstate',{'state':state,'region_count':2,'retry_byte':2,'missing_roster':44},ex,
               [get(u,0x6b1a94),get(u,0x6b1a98),get(u,region(1)+5,1),get(u,region(1)+7,1),get(u,region(1)+8,1),ex['regional_slot_visits']], [7,334,state,1,0,333])
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
    else: print('Wrote static radius/sweep evidence; original code not executed')


if __name__ == '__main__': main()
