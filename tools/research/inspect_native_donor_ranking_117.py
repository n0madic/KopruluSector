#!/usr/bin/env python3
"""Recover multi-donor ranking and complete blocked-route adoption in native StarCraft1.17.

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
    'strategic_route_filter': (0x496AC0, 0x496BE5),
    'military_flood_reachability': (0x4ABE90, 0x4ABF8E),
}


def dependencies():
    mods = (default, default.production, default.dispatch, default.dispatch.training, default.dispatch.research, default.dispatch.training.unit_reqs, default.dispatch.research.requirements, targets, wave, targets.units, wave.attack, arrival, wave.attack.removal, wave.attack.vm,
            arrival.lifecycle, arrival.primary, arrival.military, arrival.combat, arrival.spatial)
    combined = {**recruit.dependencies(), **routes.dependencies()}
    combined.update({Path(m.__file__).name: hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in mods})
    for m in (recruit,routes,routes.paths): combined[Path(m.__file__).name]=hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest()
    return combined


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
    def candidates(positions, kinds=None, donor_ids=None, priorities=None, states=None, surplus=True, campaign=True):
        u=setup(campaign=campaign,donor_priority=40,surplus=surplus)
        kinds=kinds or [4]*len(positions);donor_ids=donor_ids or [3]*len(positions)
        priorities=priorities or {};states=states or {}
        template=bytes(u.mem_read(own,0x150));s_template=bytes(u.mem_read(sprite,0x24));ais=[];units=[];heads={}
        put(u,mapbase,5,2)
        for idx,(xy,kind,donor) in enumerate(zip(positions,kinds,donor_ids)):
            a=own+idx*0x200;s=sprite+idx*0x100;n=node+idx*0x40;units.append(a);ais.append(n)
            u.mem_write(a,template);u.mem_write(s,s_template);u.mem_write(n,bytes(0x20))
            put(u,a+0xc,s);put(u,a+0x134,n);put(u,a+0x6c,own+(idx+1)*0x200 if idx+1<len(positions) else 0)
            put(u,a+0x2c,xy[0]*256);put(u,a+0x30,xy[1]*256);put(u,s+0x14,xy[0],2);put(u,s+0x16,xy[1],2)
            put(u,mapbase+0xc+2*((xy[1]//32)*256+xy[0]//32),donor,2)
            put(u,n+8,kind,1);put(u,n+0xc,a);put(u,n+0x10,region(donor))
            if kind==4:
                members=heads.setdefault(donor,[]);members.append(n)
        for donor in set(donor_ids):
            rr=region(donor);t=mapbase+0x449fc+donor*64;put(u,rr,donor,2);put(u,rr+4,player,1);put(u,rr+0x2c,pool)
            put(u,rr+5,states.get(donor,4),1);put(u,rr+6,priorities.get(donor,40),1)
            put(u,rr+0x14,200 if surplus else 30,2);put(u,rr+0x16,200 if surplus else 20,2)
            put(u,t+2,1,2);put(u,t+0x10,donor*128*256);put(u,t+0x14,128*256)
            put(u,t+6,1,1);put(u,t+7,1,1);put(u,t+0xc,mapbase+0x60100+donor*2);put(u,mapbase+0x60100+donor*2,1,2)
            members=heads.get(donor,[]);put(u,rr+0x30,members[0] if members else 0)
            for j,n in enumerate(members):put(u,n,members[j+1] if j+1<len(members) else 0);put(u,n+4,members[j-1] if j else 0)
        return u,units,ais
    def distance(a,b=(128,128)):
        small,big=sorted((abs(a[0]-b[0]),abs(a[1]-b[1])))
        term=small*3>>3
        return big if small<=big>>2 else big-(big>>4)-(big>>6)+term+(term>>5)
    ranking_positions=[[(256,128),(192,128)],[(192,128),(256,128)],[(64,128),(192,128)],[(192,128),(64,128)],[(224,128),(128,224),(160,192)],[(160,192),(128,224),(224,128)],[(64,64),(192,192)]]
    for selector in(0x4ac230,0x4ac0d0):
        for positions in ranking_positions:
            for donors in([3]*len(positions),[3,4]+[3]*(len(positions)-2)):
                u,units,ais=candidates(positions,surplus=selector==0x4ac230,donor_ids=donors)
                argv=(128,128,region(2)) if selector==0x4ac230 else(43,128,128,region(2))
                ex=call(u,selector,region(1),43 if selector==0x4ac230 else 60,argv)
                selected=min(range(len(positions)),key=lambda j:distance(positions[j]))
                record('native distance ranking preserves first player-list candidate on equal score',{'selector':hex(selector),'positions':positions,'assigned_regions':donors,'approximate_distances':[distance(x)for x in positions]},ex,
                       [ex['return_eax'],[get(u,a+0x134)for a in units],get(u,0x673938)], [ais[selected],ais,0])
    for selector in(0x4ac230,0x4ac0d0):
        for protected in(1,2,7,8,9):
            for campaign in(False,True):
                positions=[(160,128),(256,128)];u,units,ais=candidates(positions,donor_ids=[3,4],states={3:protected},surplus=selector==0x4ac230,campaign=campaign)
                argv=(128,128,region(2))if selector==0x4ac230 else(43,128,128,region(2))
                ex=call(u,selector,region(1),43 if selector==0x4ac230 else 60,argv)
                rejects=protected in(7,8,9)if selector==0x4ac230 else(not campaign and protected in(1,2))
                record('closer ineligible donor does not displace farther eligible donor',{'selector':hex(selector),'closer_state':protected,'campaign':campaign,'positions':positions},ex,ex['return_eax'],ais[1 if rejects else 0])
    for selector in (0x4ac230,0x4ac0d0):
        for destination_state in (7,8,9):
            u,units,ais=candidates([(160,128),(256,128)],donor_ids=[2,3],surplus=selector==0x4ac230)
            put(u,region(1)+5,destination_state,1)
            argv=(128,128,region(2)) if selector==0x4ac230 else (43,128,128,region(2))
            ex=call(u,selector,region(1),43 if selector==0x4ac230 else 60,argv)
            record('destination-associated donor region is protected for states seven eight and nine',{'selector':hex(selector),'receiver_state':destination_state,'receiver_destination_region':2,'candidate_assigned_regions':[2,3]},ex,ex['return_eax'],ais[1])
    for priorities_ in ({3:59,4:0},{3:0,4:59}):
        u,units,ais=candidates([(160,128),(256,128)],donor_ids=[3,4],priorities=priorities_,surplus=False)
        ex=call(u,0x4ac0d0,region(1),60,(43,128,128,region(2)))
        record('source priority is eligibility threshold rather than donor distance score',{'priorities':priorities_,'threshold':60,'positions':[(160,128),(256,128)]},ex,ex['return_eax'],ais[0])
    for first_priority in(59,60,160):
        u,units,ais=candidates([(160,128),(256,128)],donor_ids=[3,4],priorities={3:first_priority,4:59},surplus=False)
        ex=call(u,0x4ac0d0,region(1),60,(43,128,128,region(2)))
        record('fallback priority rejects closer equal-or-greater threshold donor before distance ranking',{'priorities':[first_priority,59],'threshold':60},ex,ex['return_eax'],ais[0 if first_priority<60 else 1])
    for kinds in([1,4],[4,1],[1,1]):
        for surplus in(False,True):
            positions=[(160,128),(256,128)];u,units,ais=candidates(positions,kinds=kinds,surplus=surplus)
            ex=call(u,0x4ae960,region(1),43,(60,))
            selected=next((j for j,k in enumerate(kinds)if k==4),0)if surplus else 0
            selected_ai=ais[selected]if kinds[selected]==4 else spare
            observed={'return':ex['return_eax'],'assigned_regions':[get(u,get(u,a+0x134)+0x10)for a in units],
                      'orders':[get(u,a+0x4d,1)for a in units],'destination_head':get(u,region(1)+0x30),'selected_AI':get(u,units[selected]+0x134),
                      'guard_parents':[get(u,n+0xc)for n,k in zip(ais,kinds)if k==1],
                      'fallback_selector_calls':sum(t['callee']=='0x4ac0d0'for t in ex['native_call_trace']),'rng_calls':get(u,0x673938)}
            want={'return':1,'assigned_regions':[region(1)if j==selected else region(3)for j in range(2)],'orders':[158 if j==selected else 1 for j in range(2)],
                  'destination_head':selected_ai,'selected_AI':selected_ai,'guard_parents':[0 if j==selected else units[j]for j,k in enumerate(kinds)if k==1],
                  'fallback_selector_calls':0 if surplus and 4 in kinds else 1,'rng_calls':3}
            record('composed adoption gives eligible surplus military precedence over closer fallback guard',{'kinds':kinds,'surplus':surplus,'positions':positions},ex,observed,want)
    for reverse in(False,True):
        positions=[(64,128),(192,128)]
        if reverse:positions.reverse()
        u,units,ais=candidates(positions)
        ex=call(u,0x4ae960,region(1),43,(60,))
        record('equal-distance military adoption retains first player-list node through real release allocation and orders',{'positions_in_player_list':positions},ex,
               [ex['return_eax'],get(u,ais[0]+0x10),get(u,ais[1]+0x10),get(u,units[0]+0x4d,1),get(u,units[1]+0x4d,1),get(u,region(3)+0x30),get(u,region(1)+0x30),get(u,0x673938)],
               [1,region(1),region(3),158,1,ais[1],ais[0],3])
    for blocked_state,flag in((0,0),(3,0),(0,0x20),(8,0),(0,0x40)):
        u,units,ais=candidates([(512,128)],donor_ids=[4])
        put(u,own+0x64,37,2);put(u,own+0xdc,1);put(u,own+0x138,0,2);put(u,own+0x13a,30,2)
        put(u,0x521660+37*4,0);put(u,0x521578+37,0,1);put(u,0x520458+37,0,1)
        put(u,region(4)+0x14,31,2);put(u,region(4)+0x16,0,2);put(u,region(4)+0xa,1,2)
        put(u,region(2)+5,blocked_state,1);put(u,region(2)+8,flag,1)
        chain={1:[2],2:[1,3],3:[2,4],4:[3]};cursor=mapbase+0x60200
        for i,neighbors_ in chain.items():
            t=mapbase+0x449fc+i*64;put(u,t+6,len(neighbors_),1);put(u,t+7,len(neighbors_),1);put(u,t+0xc,cursor)
            u.mem_write(cursor,struct.pack('<'+'H'*len(neighbors_),*neighbors_));cursor+=len(neighbors_)*2
            put(u,t+0x10,i*128*256);put(u,t+0x14,128*256)
        put(u,0x6b68c0+28,0);put(u,0x6b68e0+28,0);u.mem_write(0x6b1aa0+7*2500,bytes(2500));put(u,0x6d319c,0);put(u,0x50f310,0)
        route=call(u,0x496ac0,player,4,(1,));ex=call(u,0x4ae960,region(1),37,(60,));ex['preceding_complete_route_call']=route
        rtrace=[t for t in ex['native_call_trace']if t['callee']=='0x496ac0']
        rejects=blocked_state==3 or bool(flag&0x20)
        observed={'route_return':route['return_eax'],'adoption_return':ex['return_eax'],'assigned_region':get(u,ais[0]+0x10),'active_order':get(u,own+0x4d,1),
                  'old_member_count':get(u,region(4)+0xa,2),'new_member_count':get(u,region(1)+0xa,2),
                  'old_head':get(u,region(4)+0x30),'new_head':get(u,region(1)+0x30),'rng_calls':get(u,0x673938),
                  'surplus_route_returns':[t.get('returned_eax')for t in rtrace],
                  'fallback_calls':sum(t['callee']=='0x4ac0d0'for t in ex['native_call_trace'])}
        want={'route_return':0 if rejects else 1,'adoption_return':1,'assigned_region':region(1),'active_order':158,
              'old_member_count':0,'new_member_count':1,'old_head':0,'new_head':ais[0],'rng_calls':3,'surplus_route_returns':[0 if rejects else 1],'fallback_calls':0}
        record('complete grounded military adoption ignores strategic blocked-route result and issues native arrival order',{'intermediate2_state':blocked_state,'flags':flag,'source_actual_and_assigned_region':4,'staging_region':1,'graph':chain},ex,observed,want)
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
