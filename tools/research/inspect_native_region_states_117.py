#!/usr/bin/env python3
"""Recover original regional-state zero through seven selected composed paths.

Static by default. Explicit probes execute reviewed original callees only,
with private pools/objects, stock orders.dat and no helper stubs.
"""
import argparse, hashlib, json, platform, struct
from pathlib import Path
import capstone, pefile
import inspect_native_strategic_targets_117 as targets
import inspect_native_default_production_117 as default
import inspect_native_campaign_recruitment_117 as recruit
import inspect_native_ai_routes_117 as routes
import inspect_native_donor_ranking_117 as donor
import inspect_native_attack_consumers_117 as consumers
wave = targets.wave
arrival = wave.arrival
TARGET_HASH = targets.TARGET_HASH
WITNESSES = {
    'state0_strength_and_decay': (0x4AC9B0,0x4AC9F1),
    'state1_and2_reserve_mobilization': (0x4AC7C0,0x4AC863),
    'state3_neighbor_support_selection': (0x4ACA00,0x4ACBBD),
    'state4_and5_targets_and_decay': (0x4AC740,0x4AC7B4),
    'state6_combat_or_departure': (0x4AC690,0x4AC737),
    'state7_support_arrival_and_timeout': (0x4ACBC0,0x4ACC95),
    'requirement_decay': (0x4AC080,0x4AC0CE),
    'first_town_position': (0x4872A0,0x4872CC),
}


def dependencies():
    mods = (default, default.production, default.dispatch, default.dispatch.training, default.dispatch.research, default.dispatch.training.unit_reqs, default.dispatch.research.requirements, targets, wave, targets.units, wave.attack, arrival, wave.attack.removal, wave.attack.vm,
            arrival.lifecycle, arrival.primary, arrival.military, arrival.combat, arrival.spatial)
    combined = {**recruit.dependencies(), **routes.dependencies()}
    combined.update({Path(m.__file__).name: hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in mods})
    for m in (recruit,routes,routes.paths): combined[Path(m.__file__).name]=hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest()
    for m in (donor,consumers):combined[Path(m.__file__).name]=hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest()
    return combined


def execute(pe, orders):
    if platform.machine().lower() not in ('x86_64', 'amd64'): raise RuntimeError('Use validated x86_64 runtime')
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
    own, enemy, sprite, imageptr, node, regions, pool, orderpool, mapbase, stack, stop = 0x800000, 0x5da3a0, 0x805000, 0x806000, 0x807000, 0x810000, 0x820000, 0x840000, 0xB00000, 0x900000, 0xA00000
    player, p = 7, 0x67D068 + 7 * 0x4e8
    image = pe.get_memory_mapped_image(); size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    allow = list(WITNESSES.values()) + list(donor.WITNESSES.values()) + list(consumers.WITNESSES.values()) + list(targets.WITNESSES.values()) + list(wave.WITNESSES.values()) + list(wave.attack.WITNESSES.values()) + list(arrival.WITNESSES.values())
    allow += list(targets.units.BLOCKS.values()) + list(arrival.primary.BLOCKS.values()) + list(arrival.military.BLOCKS.values()) + list(arrival.spatial.BLOCKS.values()) + list(wave.attack.removal.WITNESSES.values())
    allow += [arrival.lifecycle.BLOCKS[x] for x in ('military_ai_allocate', 'raw_rng')]
    allow += [arrival.combat.BLOCKS[x] for x in ('campaign_ai_flag', 'read_cached_air_strength', 'read_cached_ground_strength', 'radius_callback_iteration', 'upgrade_level', 'cloak_detection_rejection', 'is_subunit', 'cannot_chase_target')]
    allow += [wave.attack.vm.WITNESSES[x] for x in ('unit_hostility', 'global_unit_head', 'player_unit_head')]
    allow += [(0x41fe40, 0x41fe55)]
    allow += list(default.WITNESSES.values()) + list(default.production.WITNESSES.values())
    allow += list(routes.WITNESSES.values()) + list(routes.paths.BLOCKS.values())
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
        count, reached, trace, pending = 0, False, [], []; entries = {a for a, b in allow}
        def hook(m, a, n, _):
            nonlocal count, reached
            while pending and a == pending[-1][0]:
                ret, idx = pending.pop(); trace[idx]['returned_eax'] = m.reg_read(UC_X86_REG_EAX)
            if a == stop: reached = True; m.emu_stop(); return
            if not any(lo <= a and a + n <= hi for lo, hi in allow): raise RuntimeError(f'Unreviewed instruction {a:#x}; trace{trace[-8:]}')
            count += 1
            if a in entries:
                sp = m.reg_read(UC_X86_REG_ESP); trace.append({'callee': hex(a), 'ecx': m.reg_read(UC_X86_REG_ECX), 'edx': m.reg_read(UC_X86_REG_EDX), 'stack_arguments': [get(m, sp + 4 + j * 4) for j in range(4)]})
                pending.append((get(m, sp),len(trace)-1))
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
    def state_fixture(state,campaign=True,spatial_kind='none',member=True):
        u=new(campaign)
        put(u,0x665ae0,0);put(u,0x5c99b0,0 if spatial_kind=='none' else 2)
        put(u,region(1)+5,state,1);put(u,region(1)+6,77,1);put(u,region(1)+2,2,2);put(u,region(1)+8,4,1)
        put(u,region(1)+0x30,node if member else 0);put(u,region(1)+0xa,1 if member else 0,2)
        put(u,0x665b08+player*4,own if member else 0);put(u,0x69b51c+player*8,0)
        if spatial_kind=='worker':
            put(u,enemy+0x4c,player,1);put(u,0x521660,8)
        elif spatial_kind=='enemy':
            put(u,0x5211d8,0,1);put(u,0x5213a8,0,1)
        return u
    def decay(v,campaign):
        if not campaign:return max(0,v-300)
        return v-13 if v>1000 else v-6 if v>200 else v-2 if v>2 else 0
    values=(0,1,2,3,199,200,201,299,300,301,999,1000,1001,65535)
    for campaign in(False,True):
        for value in values:
            u=state_fixture(0,campaign);put(u,region(1)+0xc,value,2)
            ex=call(u,0x4ac080,region(1)+0xc,int(not campaign))
            record('native piecewise requirement decay exact branch boundaries',{'campaign':campaign,'initial_word':value},ex,get(u,region(1)+0xc,2),decay(value,campaign))
    for state in(0,4,5):
        for campaign in(False,True):
            for flag in(0,0x40):
                for ground,air in((3,201),(301,1001),(1000,65535)):
                    u=state_fixture(state,campaign);put(u,region(1)+0xc,ground,2);put(u,region(1)+0xe,air,2);put(u,region(1)+8,flag|4,1)
                    ex=call(u,0x4b0ce0,player,1)
                    record('full dispatcher states zero four five update strength then decay unless flag40',{'state':state,'campaign':campaign,'needed_ground':ground,'needed_air':air,'flags':flag|4,'no_enemy_targets':True},ex,
                           [get(u,region(1)+5,1),get(u,region(1)+8,1),get(u,region(1)+0xc,2),get(u,region(1)+0xe,2),[get(u,region(1)+off,2)for off in(0x10,0x12,0x14,0x16)],get(u,0x673938)],
                           [state,flag,ground if flag else decay(ground,campaign),air if flag else decay(air,campaign),[30,20,30,20],0])
    for state in(4,5):
        for campaign in(False,True):
            for flag in(0,0x40):
                u=state_fixture(state,campaign,'enemy');put(u,region(1)+0xc,301,2);put(u,region(1)+0xe,201,2);put(u,region(1)+8,flag|4,1)
                ex=call(u,0x4b0ce0,player,1)
                record('states four five real hostile target selection enters combat and still performs current-call decay',{'state':state,'campaign':campaign,'flags':flag|4,'enemy_owner':0},ex,
                       [get(u,region(1)+5,1),get(u,region(1)+6,1),get(u,region(1)+0x1c),get(u,region(1)+0x20),get(u,region(1)+0xc,2),get(u,region(1)+0xe,2),get(u,own+0x4d,1),get(u,0x5d6e48),get(u,0x5d6e5c)],
                       [6,100,enemy,enemy,301 if flag else decay(301,campaign),201 if flag else decay(201,campaign),156,0,0])
    for state in(1,2):
        for campaign in(False,True):
            u=state_fixture(state,campaign,member=False);ex=call(u,0x4b0ce0,player,1)
            record('states one two empty military list reset to zero through real state initializer',{'state':state,'campaign':campaign,'military_head':0},ex,[get(u,region(1)+5,1),get(u,region(1)+6,1),get(u,region(1)+8,1)],[0,10,0])
    for campaign in(False,True):
        for ground_count in(19,20,21):
            u=state_fixture(1,campaign);put(u,region(1)+0xa,ground_count,2);ex=call(u,0x4b0ce0,player,1)
            promoted=ground_count>=20
            record('state one promotes reserve at groundcount twenty before target selection',{'campaign':campaign,'ground_member_count':ground_count,'positive_military_head':True,'no_enemy':True},ex,
                   [get(u,region(1)+5,1),get(u,region(1)+6,1),sum(t['callee']=='0x4aba70'for t in ex['native_call_trace']),get(u,own+0x4d,1)],
                   [2 if promoted else 0,(70 if campaign else 160) if promoted else 10,0 if promoted else 1,1])
    for state in(1,2):
        for campaign in(False,True):
            u=state_fixture(state,campaign,'enemy');ex=call(u,0x4b0ce0,player,1)
            record('positive reserve target retained when native same-region weapon-route predicate returns zero',{'state':state,'campaign':campaign,'ground_weapons':35,'air_weapons':36},ex,
                   [get(u,region(1)+5,1),get(u,region(1)+0x24),get(u,region(1)+0x1c),get(u,region(1)+0x20),get(u,own+0x4d,1),sum(t['callee']=='0x4aba70'for t in ex['native_call_trace'])],[state,own if campaign else 0,enemy,enemy,156,0])
    for campaign in(False,True):
        for retry in(0,1,2):
            for marker in(0,0x1ffd):
                for worker in(False,True):
                    u=state_fixture(6,campaign,'worker' if worker else 'none');put(u,region(1)+7,retry,1);put(u,region(1)+0xc,20,2);put(u,region(1)+0xe,30,2);put(u,mapbase+0x449fc+64,marker,2)
                    ex=call(u,0x4b0ce0,player,1);active=retry<=1
                    resulting_state=6 if not active else (0 if campaign and not worker else 5)
                    needed_ground=1000 if active and not campaign and marker else 20
                    needed_air=1000 if active and not campaign and not marker else 30
                    record('combat retry after dispatcher decrement gates no-target departure and noncampaign1000 minimum',{'campaign':campaign,'initial_retry':retry,'terrain_marker':marker,'owned_worker':worker},ex,
                           [get(u,region(1)+5,1),get(u,region(1)+7,1),get(u,region(1)+0xc,2),get(u,region(1)+0xe,2),get(u,region(1)+6,1),get(u,0x5d6e48),get(u,0x5d6e5c)],
                           [resulting_state,max(0,retry-1),needed_ground,needed_air,77 if not active else 10 if resulting_state==0 else 50,0,0])
    for campaign in(False,True):
        for retry in(0,2):
            u=state_fixture(6,campaign,'enemy');put(u,region(1)+7,retry,1);put(u,region(1)+0xc,0,2);put(u,region(1)+0xe,0,2)
            ex=call(u,0x4b0ce0,player,1)
            record('combat positive-target path retains state and noncampaign first-Zergling strength exception',{'campaign':campaign,'initial_retry':retry,'first_member_id':37},ex,
                   [get(u,region(1)+5,1),get(u,region(1)+7,1),get(u,region(1)+0xe,2),get(u,own+0x4d,1)],[6,max(0,retry-1),1000 if not campaign and retry==0 else 0,156])
    for campaign in(False,True):
        for counter in(0,8,9,10,255):
            for enough in(False,True):
                u=state_fixture(7,campaign);put(u,region(1)+9,counter,1);put(u,region(2)+5,3,1);put(u,region(2)+0xc,30 if enough else 31,2);put(u,region(2)+0xe,20 if enough else 21,2)
                ex=call(u,0x4b0ce0,player,1);transfers=counter>=10 or enough
                # The ninth-to-tenth increment still performs the strength gate; timeout is read next visit.
                want=[5 if transfers else 7,6 if transfers else 3,50,5 if transfers else 0, min(255,counter+1) if counter<10 else counter,
                      region(2)if transfers else region(1),157 if transfers else 1,3 if transfers else 0]
                record('support state seven transfers on inclusive local-strength gate or preincrement timeout ten',{'campaign':campaign,'initial_counter':counter,'destination_need':[30 if enough else 31,20 if enough else 21],'actual_local':[30,20],'empty_radius_index':True},ex,
                       [get(u,region(1)+5,1),get(u,region(2)+5,1),get(u,region(1)+6,1),get(u,region(2)+7,1),get(u,region(1)+9,1),get(u,node+0x10),get(u,own+0x4d,1),get(u,0x673938)],want)
    def neighbors_fixture(ids,scores,campaign=True,ground_count=None,marker=0,neighbor_states=None):
        u=state_fixture(3,campaign,member=False);put(u,mapbase,5,2);put(u,mapbase+0x449fc+64,marker,2)
        t=mapbase+0x449fc+64;put(u,t+6,len(ids)if ground_count is None else ground_count,1);put(u,t+7,len(ids),1);put(u,t+0xc,mapbase+0x60300)
        u.mem_write(mapbase+0x60300,struct.pack('<'+'H'*len(ids),*ids))
        for i,value in zip(ids,scores):
            rr=region(i);tt=mapbase+0x449fc+i*64;put(u,rr,i,2);put(u,rr+4,player,1);put(u,rr+5,(neighbor_states or {}).get(i,4),1);put(u,rr+6,77,1);put(u,rr+0x2c,pool)
            put(u,tt+2,1,2);put(u,tt+4,value,2);put(u,tt+0x10,i*128*256);put(u,tt+0x14,128*256);u.mem_write(tt+0x18,struct.pack('<4H',i*128-32,96,i*128+32,160))
        return u
    score_orders=((100,150,200),(200,150,100),(150,200,100),(100,200,150),(200,100,150),(150,100,200),(200,200,200),(0,0,0),(200,0,0),(0,200,100))
    for campaign in(False,True):
        for scores in score_orders:
            ids=[2,3,4];u=neighbors_fixture(ids,scores,campaign);ex=call(u,0x4b0ce0,player,1)
            best_score,other_score,best,other=0,0,None,None
            for i,score in zip(ids,scores):
                if score>best_score:best_score,best=score,i
                elif score>other_score:other_score,other=score,i
            selected=other if other is not None else best
            record('state three original two-slot ranking does not demote an old maximum when a later maximum appears',{'campaign':campaign,'neighbor_ids_in_order':ids,'tile_count_scores':scores},ex,
                   [[get(u,region(i)+5,1),get(u,region(i)+2,2),get(u,region(i)+6,1)]for i in ids],
                   [[7 if i==selected else 4,1 if i==selected else 0,100 if i==selected else 77]for i in ids])
    for states_ in({2:0,3:4,4:5},{2:0,3:0,4:0},{2:6,3:8,4:9}):
        for campaign in(False,True):
            ids=[2,3,4];scores=[200,150,100];u=neighbors_fixture(ids,scores,campaign,neighbor_states=states_);ex=call(u,0x4b0ce0,player,1)
            valid=[i for i in ids if states_[i]in(0,4,5)and not(campaign and states_[i]==0)]
            best_score,other_score,best,other=0,0,None,None
            for i,score in zip(ids,scores):
                if i not in valid:continue
                if score>best_score:best_score,best=score,i
                elif score>other_score:other_score,other=score,i
            selected=other if other is not None else best
            record('state three neighbors restrict to zero four five and campaign skips zero',{'campaign':campaign,'states_by_neighbor':states_,'scores':scores},ex,
                   [get(u,region(i)+5,1)for i in ids],[7 if i==selected else states_[i]for i in ids])
    for marker in(0,0x1ffd):
        u=neighbors_fixture([2,3,4],[100,200,150],ground_count=1,marker=marker);ex=call(u,0x4b0ce0,player,1)
        selected=4 if marker else 2
        record('state three literal marked terrain uses all neighbors ordinary terrain uses ground prefix',{'terrain_marker':marker,'ground_prefix_count':1,'full_count':3,'scores':[100,200,150]},ex,[get(u,region(i)+5,1)for i in(2,3,4)],[7 if i==selected else 4 for i in(2,3,4)])
    for blocker_destination in(1,2):
        u=neighbors_fixture([2,3,4],[100,200,150]);put(u,region(2)+5,7,1);put(u,region(2)+2,blocker_destination,2);ex=call(u,0x4b0ce0,player,1)
        record('state three does not create a second support group when any neighbor already supports it',{'existing_state7_neighbor':2,'existing_destination':blocker_destination,'current_region':1},ex,
               [get(u,region(i)+5,1)for i in(2,3,4)],[7,4,4]if blocker_destination==1 else[7,4,7])
    for town_present,blocked in((False,False),(True,False),(True,True)):
        u=neighbors_fixture([2,3,4],[100,200,150]);put(u,0x69b51c+player*8,own+0x3000 if town_present else 0)
        put(u,own+0x3000+0x20,128,2);put(u,own+0x3000+0x22,128,2)
        # Direct neighbors make route one-to-neighbor paths have no tested interiors.
        for i in(2,3,4):
            t=mapbase+0x449fc+i*64;put(u,t+6,1,1);put(u,t+7,1,1);put(u,t+0xc,mapbase+0x60400+i*2);put(u,mapbase+0x60400+i*2,1,2)
        if blocked:put(u,region(3)+0x1c,enemy)
        ex=call(u,0x4b0ce0,player,1);route_calls=sum(t['callee']=='0x496ac0'for t in ex['native_call_trace'])
        # A targeted neighbor is excluded before town/route query, leaving2(best100),4(newbest150), hence4.
        record('state three real first-town coordinates and direct native route calls gate eligible neighbors',{'town_present':town_present,'target_on_neighbor3':blocked},ex,
               [[get(u,region(i)+5,1)for i in(2,3,4)],route_calls],[[4,4,7],2 if town_present and blocked else 3 if town_present else 0])
    for state in(1,2):
        for campaign in(False,True):
            u=state_fixture(state,campaign);put(u,0x665ae0,enemy);put(u,own+0x64,43,2);put(u,own+0xdc,5);put(u,region(1)+0xa,0,2)
            put(u,0x521660+43*4,4);put(u,0x5211d8+43,0,1);put(u,0x5213a8+43,0,1)
            put(u,enemy+0x64,106,2);put(u,enemy+0xdc,3);put(u,sprite+0x100+0x14,512,2);put(u,sprite+0x100+0x16,512,2)
            ex=call(u,0x4b0ce0,player,1)
            record('reserve no-local-target path selects actual remote hostile building and transfers into same destination state',{'source_state':state,'campaign':campaign,'enemy_building_region':2,'empty_local_spatial_index':True},ex,
                   [get(u,region(1)+5,1),get(u,region(2)+5,1),get(u,region(2)+6,1),get(u,node+0x10),get(u,own+0x4d,1),get(u,0x673938)],
                   [0,state,70 if campaign else 160,region(2),158 if state==1 else 157,3])
    for campaign in(False,True):
        u=state_fixture(7,campaign,'enemy');put(u,region(1)+9,9,1);put(u,region(2)+5,3,1);ex=call(u,0x4b0ce0,player,1)
        record('support group encountering own noncritter target becomes combat before timeout and transfer',{'campaign':campaign,'initial_counter':9},ex,
               [get(u,region(1)+5,1),get(u,region(1)+6,1),get(u,region(1)+9,1),get(u,region(2)+5,1),get(u,node+0x10),get(u,own+0x4d,1),get(u,0x673938)], [6,100,9,3,region(1),156,0])
    for campaign in(False,True):
        u=state_fixture(6,campaign,'enemy');put(u,own+0x64,43,2);put(u,own+0xdc,5);put(u,region(1)+0xa,0,2)
        put(u,0x521660+43*4,4);put(u,0x5211d8+43,0,1);put(u,0x5213a8+43,0,1)
        put(u,region(1)+0xc,40,2);put(u,region(1)+0xe,30,2);ex=call(u,0x4b0ce0,player,1)
        record('combat insufficient unowned region disperses real military node into current region for nonexempt member',{'campaign':campaign,'first_member_id':43,'needed_ground_air':[40,30],'cached_ground_air':[30,20]},ex,
               [get(u,region(1)+5,1),get(u,node+0x10),get(u,own+0x4d,1),get(u,0x673938)], [3,region(1),157,3])
    for state in(4,5):
        for campaign in(False,True):
            u=state_fixture(state,campaign,'enemy');put(u,enemy+0x64,89,2);put(u,0x521660+89*4,0);put(u,0x520458+89,0,1);put(u,0x520708+89*4,100*256)
            put(u,0x6ac530+89*4,100);put(u,0x6ac530+(228+89)*4,100);ex=call(u,0x4b0ce0,player,1)
            record('critter-only target does not promote state four five to combat and campaign omits its target',{'state':state,'campaign':campaign,'critter_id':89},ex,
                   [get(u,region(1)+5,1),get(u,region(1)+0x1c),get(u,region(1)+0x20)], [state,0 if campaign else enemy,0 if campaign else enemy])
    for campaign in(False,True):
        u=state_fixture(7,campaign);put(u,region(1)+8,0x24,1);put(u,region(1)+0xc,40,2);put(u,region(1)+0xe,30,2);put(u,region(2)+5,3,1)
        put(u,region(2)+0xc,31,2);put(u,region(2)+0xe,21,2);ex=call(u,0x4b0ce0,player,1)
        record('support flag20 shortage can disperse before destination processing with noncampaign first-Zergling exemption',{'campaign':campaign,'first_member_id':37,'flags':0x24,'current_need':[40,30]},ex,
               [get(u,region(1)+5,1),get(u,region(1)+8,1),get(u,region(1)+6,1),get(u,region(1)+9,1),get(u,own+0x4d,1),get(u,0x673938)],
               [3 if campaign else 7,0 if campaign else 0x20,10 if campaign else 50,0 if campaign else 1,157 if campaign else 1,3 if campaign else 0])
    for campaign in(False,True):
        u=state_fixture(7,campaign);put(u,region(2)+5,3,1);put(u,region(2)+0x1c,enemy);put(u,region(2)+0xc,31,2);put(u,region(2)+0xe,21,2)
        ex=call(u,0x4b0ce0,player,1)
        record('destination target preserves support priority before deficient-strength wait',{'campaign':campaign,'destination_air_target':enemy,'source_targets':False},ex,
               [get(u,region(1)+5,1),get(u,region(1)+6,1),get(u,region(1)+9,1),get(u,node+0x10)],[7,77,1,region(1)])
    return {'format_version':1,'original_code_executed':True,'complete_game_executed':False,'windows_or_host_api_called':False,'helper_stubs':[],
            'all_matched':True,'case_count':len(cases),'complete_return_count':len(cases),'prefix_case_count':0,'cases':cases,
            'allowlisted_preferred_va_ranges':[[hex(a),hex(b)]for a,b in allow],
            'runtime_versions':{'python':platform.python_version(),'unicorn':unicorn.__version__,'capstone':capstone.__version__,'pefile':pefile.__version__,'architecture':platform.machine(),'system':platform.system()}}


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
