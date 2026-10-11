#!/usr/bin/env python3
"""Recover native regional defense recipe borrowing production and periodic consumers.

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
wave = targets.wave
arrival = wave.arrival
TARGET_HASH = targets.TARGET_HASH
WITNESSES = {
    'recruit_or_request_strength_recipe': (0x4AC3C0,0x4AC52B),
    'borrow_recipe_units': (0x4AC530,0x4AC685),
    'build_recipe_aa_address': (0x4824E0,0x4824EC),
    'build_recipe_ga_address': (0x4824F0,0x4824FC),
    'build_recipe_ag_address': (0x482500,0x48250C),
    'build_recipe_gg_address': (0x482510,0x48251C),
    'use_recipe_aa_address': (0x482CE0,0x482CEC),
    'use_recipe_ga_address': (0x482CF0,0x482CFC),
    'use_recipe_ag_address': (0x482D00,0x482D0C),
    'use_recipe_gg_address': (0x482D10,0x482D1C),
    'defense_priority_bonus': (0x4827C0,0x4827CE),
    'air_type_strength': (0x4A97E0,0x4A97E8),
    'ground_type_strength': (0x4A97F0,0x4A97F8),
}


def dependencies():
    mods = (default, default.production, default.dispatch, default.dispatch.training, default.dispatch.research, default.dispatch.training.unit_reqs, default.dispatch.research.requirements, targets, wave, targets.units, wave.attack, arrival, wave.attack.removal, wave.attack.vm,
            arrival.lifecycle, arrival.primary, arrival.military, arrival.combat, arrival.spatial)
    combined = {**recruit.dependencies(), **routes.dependencies()}
    combined.update({Path(m.__file__).name: hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in mods})
    for m in (recruit,routes,routes.paths): combined[Path(m.__file__).name]=hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest()
    combined[Path(donor.__file__).name]=hashlib.sha256(Path(donor.__file__).read_bytes()).hexdigest()
    return combined


def execute(pe, orders):
    if platform.machine().lower() not in ('x86_64', 'amd64'): raise RuntimeError('Use validated x86_64 runtime')
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
    own, enemy, sprite, imageptr, node, regions, pool, orderpool, mapbase, stack, stop = 0x800000, 0x5da3a0, 0x805000, 0x806000, 0x807000, 0x810000, 0x820000, 0x840000, 0xB00000, 0x900000, 0xA00000
    player, p = 7, 0x67D068 + 7 * 0x4e8
    image = pe.get_memory_mapped_image(); size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    allow = list(WITNESSES.values()) + list(donor.WITNESSES.values()) + list(targets.WITNESSES.values()) + list(wave.WITNESSES.values()) + list(wave.attack.WITNESSES.values()) + list(arrival.WITNESSES.values())
    allow += list(targets.units.BLOCKS.values()) + list(arrival.primary.BLOCKS.values()) + list(arrival.military.BLOCKS.values()) + list(arrival.spatial.BLOCKS.values()) + list(wave.attack.removal.WITNESSES.values())
    allow += [arrival.lifecycle.BLOCKS[x] for x in ('military_ai_allocate', 'raw_rng')]
    allow += [arrival.combat.BLOCKS[x] for x in ('campaign_ai_flag', 'read_cached_air_strength', 'read_cached_ground_strength', 'radius_callback_iteration', 'upgrade_level', 'cloak_detection_rejection', 'is_subunit', 'cannot_chase_target')]
    allow += [wave.attack.vm.WITNESSES[x] for x in ('unit_hostility', 'global_unit_head', 'player_unit_head')]
    allow += [(0x41fe40, 0x41fe55)]
    allow += list(default.WITNESSES.values()) + list(default.production.WITNESSES.values())
    allow += list(routes.WITNESSES.values()) + list(routes.paths.BLOCKS.values())
    allow += [wave.attack.vm.WITNESSES[k] for k in ("defensebuild_aa","defensebuild_ga","defensebuild_ag","defensebuild_gg","defenseuse_aa","defenseuse_ga","defenseuse_ag","defenseuse_gg")]
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
    categories={'gg':(0x2b0,0x350,0x482670,0x482e40),'ag':(0x2d8,0x378,0x482610,0x482de0),'ga':(0x300,0x3a0,0x4825b0,0x482d80),'aa':(0x328,0x3c8,0x482550,0x482d20)}
    def recipe_fixture(campaign=True,state=6,priority=77,bonus=20):
        u=setup(campaign=campaign,donor_priority=200,surplus=False)
        put(u,region(1)+5,state,1);put(u,region(1)+6,priority,1);put(u,p+0x223,bonus,1);put(u,p+0x218,(0x20 if campaign else 0)|8,2)
        for i in range(4):put(u,mapbase+0x449fc+i*64+4,100,2)
        put(u,0x665b08+player*4,0);put(u,region(3)+0x30,0);put(u,own+0x134,0)
        for ident,mineral,gas,supply,air,ground in((43,10,5,2,100,80),(37,20,0,1,60,120),(38,30,10,3,200,40),(0,5,0,1,10,10)):
            put(u,0x523520+ident*2,mineral,2);put(u,0x5236e8+ident*2,gas,2);put(u,0x523e10+ident,supply,1)
            put(u,0x6ac530+ident*4,air);put(u,0x6ac8c0+ident*4,ground)
        return u
    def write_recipe(u,category,use,ids):
        off=categories[category][int(use)];u.mem_write(p+off,struct.pack('<'+'H'*(len(ids)+1),*[i+1 for i in ids],0))
    def requested_ids(u):return sorted(row['id']for row in heap(u))
    def mode_category(air,marked):return ('aa'if air else'ga')if marked else('ag'if air else'gg')
    def totals(ids):
        costs={43:(10,5,2),37:(20,0,1),38:(30,10,3),0:(5,0,1)}
        return [sum(costs[i][j]for i in ids)for j in range(3)]
    for campaign in(False,True):
        for receiver_mark,requirement_mark in((False,False),(True,False),(False,True),(True,True)):
            for air in(False,True):
                u=recipe_fixture(campaign)
                put(u,mapbase+0x449fc+64,0x1ffd if receiver_mark else 0,2);put(u,mapbase+0x449fc+2*64,0x1ffd if requirement_mark else 0,2)
                for category,ident in zip(categories,(43,37,38,0)):write_recipe(u,category,False,[ident])
                selected=mode_category(air,receiver_mark or requirement_mark);ident={'gg':43,'ag':37,'ga':38,'aa':0}[selected]
                strength={43:(100,80),37:(60,120),38:(200,40),0:(10,10)}[ident][0 if air else 1]
                ex=call(u,0x4ac3c0,region(1),region(2),(int(air),1))
                record('recipe consumer selects airground and markedordinary list from both receiver and requirement terrain',{'campaign':campaign,'receiver_marked':receiver_mark,'requirement_marked':requirement_mark,'air_mode':air,'four_list_ids':{'gg':43,'ag':37,'ga':38,'aa':0}},ex,
                       [ex['return_eax'],requested_ids(u),[get(u,p+j*4)for j in range(3)],[row['priority']for row in heap(u)],[row['context']for row in heap(u)]],
                       [strength,[ident],totals([ident]),[97],[region(1)]])
    for campaign in(False,True):
        for air in(False,True):
            for required in(1,60,100,101,160,200,240,241,300,301):
                ids=[43,37,43];u=recipe_fixture(campaign);category=mode_category(air,False);write_recipe(u,category,False,ids)
                ex=call(u,0x4ac3c0,region(1),region(2),(int(air),required));chosen=[];accumulated=0
                for ident in ids:
                    chosen.append(ident);accumulated+={43:(100,80),37:(60,120)}[ident][0 if air else 1]
                    if campaign and accumulated>=required:break
                record('production recipe emits full noncampaign list but campaign stops at cumulative requested type strength',{'campaign':campaign,'air_mode':air,'required_strength':required,'recipe':ids},ex,
                       [ex['return_eax'],requested_ids(u),[get(u,p+j*4)for j in range(3)],get(u,p+0x210,1)],
                       [accumulated,sorted(chosen),totals(chosen),len(chosen)])
    for priority,bonus in((0,0),(77,20),(250,20),(255,255)):
        u=recipe_fixture(priority=priority,bonus=bonus);write_recipe(u,'gg',False,[43]);ex=call(u,0x4ac3c0,region(1),region(2),(0,1))
        record('defense priority byte plus bonus passed to real request heap whose stored priority truncates',{'region_priority':priority,'bonus':bonus},ex,[heap(u)[0]['priority'],ex['return_eax']],[(priority+bonus)&255,80])
    for terrain_area in(0,15,16,17):
        for use_present in(False,True):
            u=recipe_fixture();put(u,region(2)+5,4,1);put(u,mapbase+0x449fc+2*64+4,terrain_area,2);write_recipe(u,'gg',False,[43])
            if use_present:write_recipe(u,'gg',True,[43])
            ex=call(u,0x4ac3c0,region(1),region(2),(0,100))
            record('requirement state four small-area returns999 and larger-area skips borrowing directly to production',{'requirement_state':4,'terrain_word4':terrain_area,'use_list_present':use_present},ex,
                   [ex['return_eax'],requested_ids(u),sum(t['callee']=='0x4ac530'for t in ex['native_call_trace'])],[999 if terrain_area<16 else 80,[]if terrain_area<16 else[43],0])
    for campaign in(False,True):
        u=recipe_fixture(campaign);ex=call(u,0x4ac3c0,region(1),region(2),(0,100))
        record('empty borrowing and build lists return999 progress sentinel without production request',{'campaign':campaign,'empty_recipes':True},ex,[ex['return_eax'],heap(u),[get(u,p+j*4)for j in range(3)]],[999,[],[0,0,0]])
    for kind in(1,4):
        for air in(False,True):
            for available in(False,True):
                positions=[(512,512)];u,units,ais=candidates(positions,kinds=[kind],surplus=True)
                put(u,region(1)+5,6,1);put(u,region(1)+6,60,1);put(u,p+0x223,20,1);put(u,p+0x218,0x28,2)
                put(u,pool+0x4e20,spare if available else 0)
                for i in range(4):put(u,mapbase+0x449fc+i*64+4,100,2)
                put(u,0x6ac530+43*4,100);put(u,0x6ac8c0+43*4,80)
                category=mode_category(air,False);write_recipe(u,category,True,[43,43]);write_recipe(u,category,False,[37])
                ex=call(u,0x4ac3c0,region(1),region(2),(int(air),1000))
                adopts=available or kind==4;expected_ai=ais[0]if kind==4 else spare if available else 0
                record('any positive borrowed type strength suppresses production despite unmet full shortfall and duplicated recipe',{'kind':kind,'air_mode':air,'available_pool':available,'borrow_recipe':[43,43],'build_recipe':[37],'required_strength':1000,'cached_air_ground':[20,30]},ex,
                       [ex['return_eax'],heap(u),get(u,units[0]+0x134),get(u,units[0]+0x4d,1),get(u,0x673938)],
                       [100 if air else 80,[],expected_ai,157 if adopts else 1,3 if adopts else 0])
    for campaign in(False,True):
        u=recipe_fixture(campaign,state=5);write_recipe(u,'gg',False,[43]);put(u,region(1)+0xc,161,2);put(u,region(1)+0x14,0,2)
        ex=call(u,0x4af930,player)
        record('complete periodic sweep repeats production recipe until synthetic accumulated progress covers shortfall',{'campaign':campaign,'needed_ground':161,'recipe_type_ground_strength':80,'state':5},ex,
               [requested_ids(u),[get(u,p+j*4)for j in range(3)],get(u,region(1)+0x14,2)],[[43,43,43],[30,15,6],0])
    for state in(4,5,6,7,9):
        for enemy_air in(0,1):
            u=recipe_fixture(state=state);write_recipe(u,'gg',False,[43]);write_recipe(u,'ag',False,[37])
            req=region(2)if state>=7 else region(1);put(u,req+0xc,1,2);put(u,req+0x18,enemy_air,2)
            ex=call(u,0x4af930,player);ident=37 if enemy_air else 43
            record('periodic ground shortfall selects air-mode recipes when requirement enemy-air accumulator is nonzero',{'receiver_state':state,'requirement_region':2 if state>=7 else 1,'enemy_air_word':enemy_air,'needed_ground':1},ex,requested_ids(u),[ident])
    for hp in(50,100):
        u,units,ais=candidates([(512,512)]);put(u,region(1)+5,6,1);put(u,region(1)+6,60,1)
        put(u,own+8,hp*256);put(u,0x6ac530+43*4,100);put(u,0x6ac8c0+43*4,80)
        refreshed=call(u,0x4a8f30,own);cached=[get(u,own+0x138,2),get(u,own+0x13a,2)]
        write_recipe(u,'gg',True,[43]);write_recipe(u,'gg',False,[37]);ex=call(u,0x4ac3c0,region(1),region(2),(0,1000));ex['preceding_complete_cache_refresh']=refreshed
        record('actual cache refresh then borrowing credits base type strength rather than current cached health-adjusted contribution',{'life':hp,'maximum_life':100,'type_air_ground':[100,80],'required_strength':1000},ex,
               [cached,ex['return_eax'],get(u,own+0x134),get(u,node+0x10),get(u,own+0x4d,1),heap(u)],[[hp,hp*80//100],80,node,region(1),157,[]])
    for category,(buildoff,useoff,buildva,useva) in categories.items():
        marked=category in('ga','aa');air=category in('ag','aa')
        for use in(False,True):
            u=recipe_fixture();put(u,mapbase+0x449fc+64,0x1ffd if marked else 0,2)
            writer=call(u,useva if use else buildva,player,2,(43,));off=useoff if use else buildoff
            ex=call(u,0x4ac3c0,region(1),region(2),(int(air),161));ex['preceding_complete_recipe_writer']=writer
            record('actual defense recipe writer composes with selected original regional consumer',{'category':category,'use_existing':use,'count':2,'unit':43,'required':161,'no_donor':True},ex,
                   [list(struct.unpack('<20H',u.mem_read(p+off,40))),ex['return_eax'],requested_ids(u)],
                   [[44,44]+[0]*18,999 if use else 200 if air else 160,[]if use else[43,43]])
    u=recipe_fixture(False)
    writers=[call(u,0x482670,player,10,(43,)),call(u,0x482670,player,10,(43,)),call(u,0x482610,player,1,(37,))]
    ex=call(u,0x4ac3c0,region(1),region(2),(0,1));ex['preceding_complete_recipe_writers']=writers
    record('full twentyword recipe has no reserved terminator and consumer continues into adjacent category until zero',{'category':'gg','appends':[[10,43],[10,43]],'adjacent_ag_recipe':[37],'campaign':False},ex,
           [list(struct.unpack('<20H',u.mem_read(p+0x2b0,40))),ex['return_eax'],requested_ids(u),[get(u,p+j*4)for j in range(3)]],
           [[44]*20,1720,[37]+[43]*20,[220,100,41]])
    for available in(False,True):
        u,units,ais=candidates([(512,512)],kinds=[1]);put(u,region(1)+5,6,1);put(u,region(1)+6,60,1);put(u,p+0x218,0x28,2)
        put(u,pool+0x4e20,spare if available else 0);put(u,region(1)+0xe,100,2);put(u,region(1)+0x16,0,2)
        for i in range(4):put(u,mapbase+0x449fc+i*64+4,100,2)
        put(u,0x6ac530+43*4,100);write_recipe(u,'ag',True,[43]);write_recipe(u,'ag',False,[37])
        ex=call(u,0x4af930,player)
        record('full periodic borrowing progress can satisfy shortfall despite failed guard-to-military allocation',{'available_pool':available,'required_air':100,'cached_candidate_air':20,'type_air_strength':100,'guard_parent':own},ex,
               [get(u,own+0x134),get(u,node+0xc),heap(u),get(u,region(1)+0x16,2),get(u,own+0x4d,1),get(u,0x673938)],
               [spare if available else 0,0,[],0,157 if available else 1,3 if available else 0])
    for campaign in(False,True):
        u=recipe_fixture(campaign);write_recipe(u,'gg',False,[43]);put(u,0x6ac8c0+43*4,0)
        ex=call(u,0x4ac3c0,region(1),region(2),(0,1))
        record('nonempty production recipe with zero selected type strength returns zero rather than999',{'campaign':campaign,'ground_type_strength':0,'required':1,'recipe':[43]},ex,
               [ex['return_eax'],requested_ids(u),[get(u,p+j*4)for j in range(3)]],[0,[43],[10,5,2]])
    u=recipe_fixture(state=4);put(u,mapbase+0x449fc+64+4,15,2);put(u,region(1)+0xc,2000,2);write_recipe(u,'gg',False,[43])
    ex=call(u,0x4af930,player)
    recipe_calls=[t for t in ex['native_call_trace']if t['callee']=='0x4ac3c0']
    record('periodic small-state-four progress sentinel covers synthetic bookkeeping in three calls without new actual strength',{'state':4,'terrain_word4':15,'needed_ground':2000},ex,
           [[t['stack_arguments'][1]for t in recipe_calls],[t.get('returned_eax')for t in recipe_calls],heap(u),get(u,region(1)+0x14,2),get(u,region(1)+0xc,2)],[[2000,1001,2],[999,999,999],[],0,2000])
    for campaign in(False,True):
        u,units,ais=candidates([(512,512)],campaign=campaign);put(u,region(1)+5,6,1);put(u,region(1)+6,60,1);put(u,p+0x218,(0x20 if campaign else 0)|8,2)
        put(u,region(1)+0xc,161,2);put(u,region(1)+0x14,0,2)
        for i in range(4):put(u,mapbase+0x449fc+i*64+4,100,2)
        put(u,0x6ac8c0+43*4,80);put(u,0x6ac8c0+37*4,120)
        put(u,0x523520+37*2,20,2);put(u,0x5236e8+37*2,0,2);put(u,0x523e10+37,1,1)
        write_recipe(u,'gg',True,[43]);write_recipe(u,'gg',False,[37]);ex=call(u,0x4af930,player)
        recipe_calls=[t for t in ex['native_call_trace']if t['callee']=='0x4ac3c0']
        record('periodic shortfall first borrows then requests on later call with independent accumulated type strength',{'campaign':campaign,'needed_ground':161,'borrowed_type_ground':80,'requested_type_ground':120,'cached_donor_ground':30},ex,
               [[t.get('returned_eax')for t in recipe_calls],requested_ids(u),[get(u,p+j*4)for j in range(3)],get(u,node+0x10),get(u,own+0x4d,1),get(u,0x673938)],
               [[80,120],[37],[20,0,1],region(1),157,3])
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
