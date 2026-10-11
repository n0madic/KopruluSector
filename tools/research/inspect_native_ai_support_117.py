#!/usr/bin/env python3
"""Recover actual observer and transport request tail of periodic regional AI.

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
import inspect_native_defense_recipes_117 as recipes
wave = targets.wave
arrival = wave.arrival
TARGET_HASH = targets.TARGET_HASH
WITNESSES = {
    'periodic_player_recruitment_and_support_requests': (0x4AF930,0x4AFAE5),
    'all_unit_count': (0x4143C0,0x4143D4),
    'completed_unit_count': (0x4143E0,0x4143F4),
    'observer_kind_eight_request': (0x482930,0x482987),
    'support_requests_disabled': (0x483350,0x483364),
}


def dependencies():
    mods = (default, default.production, default.dispatch, default.dispatch.training, default.dispatch.research, default.dispatch.training.unit_reqs, default.dispatch.research.requirements, targets, wave, targets.units, wave.attack, arrival, wave.attack.removal, wave.attack.vm,
            arrival.lifecycle, arrival.primary, arrival.military, arrival.combat, arrival.spatial)
    combined = {**recruit.dependencies(), **routes.dependencies()}
    combined.update({Path(m.__file__).name: hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in mods})
    for m in (recruit,routes,routes.paths): combined[Path(m.__file__).name]=hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest()
    for m in (recipes,):combined[Path(m.__file__).name]=hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest()
    return combined


def execute(pe, orders):
    if platform.machine().lower() not in ('x86_64', 'amd64'): raise RuntimeError('Use validated x86_64 runtime')
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
    own, enemy, sprite, imageptr, node, regions, pool, orderpool, mapbase, stack, stop = 0x800000, 0x5da3a0, 0x805000, 0x806000, 0x807000, 0x810000, 0x820000, 0x840000, 0xB00000, 0x900000, 0xA00000
    player, p = 7, 0x67D068 + 7 * 0x4e8
    image = pe.get_memory_mapped_image(); size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    allow = list(WITNESSES.values()) + list(recruit.WITNESSES.values()) + list(targets.WITNESSES.values()) + list(wave.WITNESSES.values()) + list(wave.attack.WITNESSES.values()) + list(arrival.WITNESSES.values())
    allow += list(targets.units.BLOCKS.values()) + list(arrival.primary.BLOCKS.values()) + list(arrival.military.BLOCKS.values()) + list(arrival.spatial.BLOCKS.values()) + list(wave.attack.removal.WITNESSES.values())
    allow += [arrival.lifecycle.BLOCKS[x] for x in ('military_ai_allocate', 'raw_rng')]
    allow += [arrival.combat.BLOCKS[x] for x in ('campaign_ai_flag', 'read_cached_air_strength', 'read_cached_ground_strength', 'radius_callback_iteration', 'upgrade_level', 'cloak_detection_rejection', 'is_subunit', 'cannot_chase_target')]
    allow += [wave.attack.vm.WITNESSES[x] for x in ('unit_hostility', 'global_unit_head', 'player_unit_head')]
    allow += [(0x41fe40, 0x41fe55)]
    allow += list(default.WITNESSES.values()) + list(default.production.WITNESSES.values())
    allow += list(default.dispatch.WITNESSES.values()) + list(default.dispatch.training.WITNESSES.values()) + list(default.dispatch.training.unit_reqs.WITNESSES.values())
    allow += [arrival.lifecycle.BLOCKS[k] for k in ('trained_unit_ai','finished_ordinary_unit_ai','medic_ai_reference')]
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
    def support_fixture(campaign=True,race=1,defense_state=5,disabled=False,transports=0,observers=2,starport=1):
        u=setup(campaign=campaign);put(u,0x665ae0,0);put(u,0x5c99b0,0);put(u,0x665b08+player*4,0)
        put(u,p+0x218,(0x20 if campaign else 0)|(8 if disabled else 0),2)
        for i in(1,2,3):
            rr=region(i);put(u,rr+5,defense_state if i==1 else 0,1);put(u,rr+0x30,0);put(u,rr+0xc,0,2);put(u,rr+0xe,0,2);put(u,rr+0x14,0,2);put(u,rr+0x16,0,2)
        put(u,0x6726d1+player*36,race,1)
        put(u,0x52c0c4+(115*12+player)*4,starport)
        for ident,count in((11,transports),(69,transports),(84,observers)):
            put(u,0x529604+(ident*12+player)*4,count)
        for ident,minerals,gas,supply in((11,50,25,2),(69,100,10,4),(84,25,75,2)):
            put(u,0x523520+ident*2,minerals,2);put(u,0x5236e8+ident*2,gas,2);put(u,0x523e10+ident,supply,1)
        return u
    def canonical(u):return sorted(heap(u),key=lambda row:(row['kind'],row['id'],row['priority'],row['context']))
    def observer(n):return [{'kind':8,'id':84,'priority':80,'context':0}for unused in range(n)]
    def transport(n,race,count,context=None):
        return [{'kind':1,'id':11 if race==1 else 69,'priority':80 if count==0 else 50,'context':region(1)if context is None else context}for unused in range(n)]
    for campaign in(False,True):
        for race in(0,1,2):
            for disabled in(False,True):
                u=support_fixture(campaign,race,disabled=disabled,observers=2);ex=call(u,0x4af930,player)
                n=0 if disabled or race==0 else 2 if campaign else 5;ident=11 if race==1 else 69
                costs=(50,25,2)if race==1 else(100,10,4)
                record('whole periodic tail distinguishes race campaign transport quota and suppression flag8',{'campaign':campaign,'race':race,'disabled_bit8':disabled,'defense_state':5,'all_transports':0,'all_observers':2,'completed_starports':1},ex,
                       [canonical(u),[get(u,p+j*4)for j in range(3)],get(u,0x673938)],[transport(n,race,0),[v*n for v in costs],0])
    for campaign in(False,True):
        for race in(1,2):
            for count in(0,1,2,4,5,6,0xffffffff):
                u=support_fixture(campaign,race,transports=count);ex=call(u,0x4af930,player);n=max(0,(2 if campaign else 5)-count)
                record('transport quota uses all-unit count and priority eighty for first request otherwise fifty',{'campaign':campaign,'race':race,'all_transport_count':count,'completed_transport_count_fixture':0},ex,canonical(u),transport(n,race,count))
    for campaign in(False,True):
        for count in(0,1,2,3):
            for defense_state in(0,4,5,6):
                u=support_fixture(campaign,2,defense_state,transports=6,observers=count);ex=call(u,0x4af930,player);n=max(0,2-count)
                record('observer deficit requests kind8 before requiring a state-four-five transport context',{'campaign':campaign,'all_observer_count':count,'completed_observer_count_fixture':0,'defense_state':defense_state,'all_shuttles':6},ex,
                       [canonical(u),[get(u,p+j*4)for j in range(3)]],[observer(n),[25*n,75*n,2*n]])
    for completed,all_count in((0,0),(0,1),(1,0),(1,1)):
        u=support_fixture(race=1,starport=completed);put(u,0x529604+(115*12+player)*4,all_count);ex=call(u,0x4af930,player)
        record('Terran transport requests require completed Starport115 independently of all count',{'completed_starports':completed,'all_starports':all_count},ex,canonical(u),transport(2 if completed else 0,1,0))
    for race in(1,2):
        for campaign in(False,True):
            u=support_fixture(campaign,race);put(u,region(2)+5,4,1);put(u,region(3)+5,5,1);ex=call(u,0x4af930,player)
            record('last ascending state-four-five region supplies transport request context',{'race':race,'campaign':campaign,'states1_2_3':[5,4,5]},ex,canonical(u),transport(2 if campaign else 5,race,0,region(3)))
    for state in(0,1,2,3,6,7,9):
        u=support_fixture(defense_state=state);ex=call(u,0x4af930,player)
        record('transport request requires remembered state-four-five region and ignores other states',{'defense_state':state,'race':1,'completed_starports':1},ex,canonical(u),[])
    for disabled in(False,True):
        u=support_fixture(race=2,disabled=disabled,transports=0,observers=0);ex=call(u,0x4af930,player)
        got=canonical(u);expected=[]if disabled else transport(2,2,0)+observer(2)
        record('complete Protoss tail composes independent observer and regional shuttle demands',{'disabled_bit8':disabled,'all_observers':0,'all_shuttles':0},ex,
               [got,[get(u,p+j*4)for j in range(3)]],[expected,[0,0,0]if disabled else[250,170,12]])
    def producer_fixture(u,producer,race_group,stocks=(500,500)):
        put(u,own+0x64,producer,2);put(u,own+0xdc,3);put(u,own+0x4d,1,1);put(u,own+0xa6,0,1);put(u,own+0xc8,44,1);put(u,own+0xc9,61,1)
        put(u,own+0x134,node);put(u,node+8,3,1);put(u,node+0x14,own+0x3000);u.mem_write(own+0x98,struct.pack('<5H',*([228]*5)))
        if producer==114:put(u,own+0xc0,own+0x4000);put(u,own+0x4000+0x64,115,2)
        u.mem_write(0x52655c,bytes([1])*(228*12));u.mem_write(0x523a78,struct.pack('<228H',*([65535]*228)))
        u.mem_write(0x52c0c4,struct.pack('<'+'I'*(228*12),*([1]*(228*12))))
        put(u,0x5432fc,0);put(u,0x508d50,0);put(u,0x67cc90,0);put(u,0x536720,1,1)
        for ident in(11,69,84,producer):put(u,0x523c40+ident,race_group,1);put(u,0x521660+ident*4,4 if ident in(11,69,84)else 1)
        for maximum,used,total in default.dispatch.training.SUPPLY_TABLES:put(u,maximum+player*4,400);put(u,used+player*4,0);put(u,total+player*4,20)
        put(u,0x5263d0+player*4,stocks[0]);put(u,0x526400+player*4,stocks[1])
        for j in range(3):put(u,p+0xc+j*4,1000)
    for campaign in(False,True):
        u=support_fixture(campaign,race=2,transports=0,observers=0);producer_fixture(u,155,4)
        generation=call(u,0x4af930,player);expected_n=4 if campaign else 7
        # Noncampaign produces two Observers plus five Shuttles; five-slot queue can admit only five requests.
        admission_count=min(expected_n,5);executions=[]
        for unused in range(admission_count):executions.append(call(u,0x487110,own))
        ex=executions[-1];ex['preceding_complete_request_generation']=generation;ex['preceding_complete_admissions']=executions[:-1]
        expected_queue=[84,69,69,69,69]if not campaign else[84,69,69,84,228]
        # Equal-priority native heap pop exposes its actual ordering: inspect against independently specified queue below.
        observed={'queue':list(struct.unpack('<5H',u.mem_read(own+0x98,10))),'tags':list(u.mem_read(node+9,5)),
                  'contexts':[get(u,node+0x18+j*4)for j in range(5)],'stocks':[get(u,0x5263d0+28),get(u,0x526400+28)],
                  'heap':canonical(u),'demand':[get(u,p+j*4)for j in range(3)],'available':[get(u,p+0xc+j*4)for j in range(3)],'secondary':get(u,own+0xa6,1)}
        expected_tags=[5 if ident==84 else 1 if ident==69 else 0 for ident in expected_queue]
        expected_contexts=[region(1)if ident==69 else 0 for ident in expected_queue]
        obs_used=expected_queue.count(84);shuttle_used=expected_queue.count(69);remaining_obs=2-obs_used;remaining_shuttle=(2 if campaign else 5)-shuttle_used
        want={'queue':expected_queue,'tags':expected_tags,'contexts':expected_contexts,'stocks':[500-obs_used*25-shuttle_used*100,500-obs_used*75-shuttle_used*10],
              'heap':transport(remaining_shuttle,2,0)+observer(remaining_obs),'demand':[remaining_obs*25+remaining_shuttle*100,remaining_obs*75+remaining_shuttle*10,remaining_obs*2+remaining_shuttle*4],
              'available':[1000-obs_used*25-shuttle_used*100,1000-obs_used*75-shuttle_used*10,1000-obs_used*2-shuttle_used*4],'secondary':38}
        record('periodic support requests compose with real spending dispatch training queue payment metadata and heap pop',{'campaign':campaign,'race':2,'producer':155,'generated_observers':2,'generated_shuttles':2 if campaign else 5,'admission_calls':admission_count,'requirement_offsets_initial_lazy_sentinel':65535},ex,observed,want)
    for race,ident,producer,group,existing,observers in((1,11,114,2,1,2),(2,69,155,4,1,2),(2,84,155,4,6,1)):
        u=support_fixture(True,race,transports=existing,observers=observers);producer_fixture(u,producer,group)
        generation=call(u,0x4af930,player);ex=call(u,0x487110,own);ex['preceding_complete_request_generation']=generation
        tag=5 if ident==84 else 1;context=0 if ident==84 else region(1);cost={11:(50,25,2),69:(100,10,4),84:(25,75,2)}[ident]
        record('single generated observer or transport request consumed by matching producer through actual queue service',{'race':race,'requested_id':ident,'producer':producer,'existing_transports':existing,'all_observers':observers,'requirement_offsets_initial_lazy_sentinel':65535},ex,
               [ex['return_eax'],list(struct.unpack('<5H',u.mem_read(own+0x98,10))),get(u,node+9,1),get(u,node+0x18),[get(u,0x5263d0+28),get(u,0x526400+28)],heap(u),[get(u,p+j*4)for j in range(3)],get(u,own+0xa6,1)],
               [1,[ident,228,228,228,228],tag,context,[500-cost[0],500-cost[1]],[],[0,0,0],38])
    for ident in(84,69):
        u=support_fixture(True,2,transports=6 if ident==84 else 1,observers=1 if ident==84 else 2);producer_fixture(u,155,4)
        generation=call(u,0x4af930,player);admission=call(u,0x487110,own)
        child=own+0x200;childsprite=sprite+0x200
        u.mem_write(child,bytes(0x150));put(u,child+0x64,ident,2);put(u,child+0x4c,player,1);put(u,child+0x4d,1,1);put(u,child+0xdc,5)
        put(u,child+0xc,childsprite);put(u,child+8,100*256);put(u,child+0x34,20);put(u,childsprite+0x14,128,2);put(u,childsprite+0x16,128,2)
        for off in(0x18,0x1c,0x20):put(u,childsprite+off,imageptr)
        put(u,0x6726d0+player*36,1,1);put(u,0x521578+ident,0,1);put(u,pool+0x4e20,spare);put(u,region(1)+0x30,0)
        ex=call(u,0x48d320,own,child);ex['preceding_complete_request_generation']=generation;ex['preceding_complete_admission']=admission
        ai=get(u,child+0x134)
        record('generated support metadata reaches real completed-child AI assignment with Observer early exclusion and transport military allocation',{'constructed_completed_child':ident,'no_new_unit_creation_executed':True,'producer':155,'training_tag':5 if ident==84 else 1},ex,
               [ai,get(u,ai+8,1)if ai else 0,get(u,ai+0x10)if ai else 0,get(u,child+0x4d,1),get(u,0x673938)],
               [0,0,0,1,0]if ident==84 else[spare,4,region(1),157,3])
    for campaign in(False,True):
        for observer_request in(False,True):
            race=2;ident=84 if observer_request else 69;existing=6 if observer_request else(1 if campaign else 4)
            u=support_fixture(campaign,race,transports=existing,observers=1 if observer_request else 2);producer_fixture(u,155,4,stocks=(24 if observer_request else 99,500))
            generation=call(u,0x4af930,player);ex=call(u,0x487110,own);ex['preceding_complete_request_generation']=generation
            retained=observer_request or campaign;cost=(25,75,2)if observer_request else(100,10,4)
            record('generated support stock failure follows distinct observer-retain and ordinary campaign retry rules',{'campaign':campaign,'requested_id':ident,'stock_minerals':24 if observer_request else 99,'requirement_offsets_initial_lazy_sentinel':65535},ex,
                   [ex['return_eax'],get(u,p+0x210,1),[get(u,p+j*4)for j in range(3)],list(struct.unpack('<5H',u.mem_read(own+0x98,10))),sum(t['callee']=='0x487110'for t in ex['native_call_trace'])],
                   [0,1 if retained else 0,list(cost)if retained else[0,0,0],[228]*5,1 if retained else 2])
    u=support_fixture(False,2,transports=0,observers=0);producer_fixture(u,155,4);generation=call(u,0x4af930,player)
    admissions=[call(u,0x487110,own)for unused in range(5)];ex=call(u,0x487110,own);ex['preceding_complete_request_generation']=generation;ex['preceding_complete_admissions']=admissions
    record('sixth support service retains both requests on relative-four queue gate before resource shortage is tested',{'campaign':False,'generated_observers':2,'generated_shuttles':5,'initial_stocks':[500,500],'six_admission_calls':True},ex,
           [ex['return_eax'],list(struct.unpack('<5H',u.mem_read(own+0x98,10))),canonical(u),[get(u,p+j*4)for j in range(3)],
            [get(u,p+0xc+j*4)for j in range(3)],[get(u,0x5263d0+28),get(u,0x526400+28)],sum(t['callee']=='0x487110'for t in ex['native_call_trace']),get(u,0x6be654),sum(t['callee']=='0x4826d0'for t in ex['native_call_trace'])],
           [0,[84,69,69,69,69],transport(1,2,0)+observer(1),[125,85,6],[575,885,982],[75,385],1,3,0])
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
    entries,words_used=default.dispatch.training.unit_reqs.parse_unit_table(pe)
    result['support_original_unit_recipes']={'packed_table_preferred_va':'0x50da88','original_lazy_initializer':'0x4b3230','initial_offset_sentinel':65535,'recipes':[entry for entry in entries if entry['unit_id'] in (11,69,84)]}
    a.out.parent.mkdir(parents=True, exist_ok=True); a.out.write_text(json.dumps(result, indent=2) + '\n')
    if a.execute_isolated_original_code:
        run = execute(pe, orders); run.update({'target_sha256': TARGET_HASH, 'target_version': '1.17.0.1', 'probe_script_sha256': sha, 'dependency_script_sha256': dependencies(), 'orders_dat_sha256': hashlib.sha256(dat).hexdigest()})
        a.probe_out.parent.mkdir(parents=True, exist_ok=True); a.probe_out.write_text(json.dumps(run, indent=2) + '\n'); print(json.dumps({'cases': run['case_count'], 'complete': run['complete_return_count'], 'sha256': sha}))
    else: print('Wrote static recruitment evidence; original code not executed')


if __name__ == '__main__': main()
