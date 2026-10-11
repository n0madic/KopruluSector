#!/usr/bin/env python3
"""Recover military flood reachability and strategic route acceptance in hash-gated native StarCraft1.17.

Static by default. Explicit isolated probes execute reviewed original code and
all reached callees in private memory, without callable helper replacements.
"""
import argparse, hashlib, json, platform, struct
from pathlib import Path
import capstone, pefile
import probe_native_region_paths_117 as paths
import inspect_native_campaign_recruitment_117 as recruit

TARGET_HASH = '94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
WITNESSES = {
    'military_flood_reachability_rebuild': (0x4ABE90, 0x4ABF8E),
    'strategic_intermediate_region_route_filter': (0x496AC0, 0x496BE5),
}


def dependencies():
    mods=(paths,recruit,recruit.wave,recruit.arrival,recruit.arrival.military,recruit.arrival.combat,recruit.targets.units,recruit.default,recruit.wave.attack.vm,recruit.wave.attack)
    return {Path(m.__file__).name:hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest()for m in mods}


def execute(pe):
    if platform.machine().lower()not in('x86_64','amd64'):raise RuntimeError('Use validated x86_64 runtime')
    import unicorn
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP,UC_X86_REG_EIP
    regions,terrain,neighbor,unit,sprite,node,stack,stop=0x810000,0xB00000,0xB60000,0x800000,0x805000,0x806000,0x900000,0xA00000
    allow=list(WITNESSES.values())+list(paths.BLOCKS.values())+list(recruit.WITNESSES.values())+list(recruit.arrival.military.BLOCKS.values())+list(recruit.targets.units.BLOCKS.values())+list(recruit.wave.attack.WITNESSES.values())+list(recruit.default.WITNESSES.values())
    allow += [recruit.wave.attack.vm.WITNESSES[k]for k in('player_unit_head','unit_hostility')]+[recruit.arrival.combat.BLOCKS[k]for k in('campaign_ai_flag','read_cached_air_strength','read_cached_ground_strength')]+[recruit.wave.WITNESSES['air_weapon_with_subunit'],recruit.wave.WITNESSES['unit_target_hostility']]
    size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095;image=pe.get_memory_mapped_image();cases=[];player=7;p=0x67D068+7*0x4e8
    def put(u,a,v,n=4):u.mem_write(a,(v&((1<<(n*8))-1)).to_bytes(n,'little'))
    def get(u,a,n=4):return int.from_bytes(u.mem_read(a,n),'little')
    def r(i):return regions+i*0x34
    def new(graph,groups=None,spacing=128):
        u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,size);u.mem_write(0x400000,image)
        for a,n in((unit,0x90000),(terrain,0x80000),(stack,0x20000),(stop,0x1000)):u.mem_map(a,n)
        u.mem_write(0x67d068,bytes(8*0x4e8));put(u,p+0x218,0x20,2);put(u,0x69b584,terrain);put(u,terrain,max(graph)+1,2)
        put(u,0x6b1a74+28,regions);put(u,0x5349d8,100);put(u,0x6b68c0+28,0);put(u,0x6b68e0+28,0)
        u.mem_write(0x6b1aa0+7*2500,bytes(2500));put(u,0x6d319c,0);put(u,0x50f310,0)
        cursor=neighbor
        for i,ids in graph.items():
            t=terrain+0x449fc+i*64;put(u,t+2,(groups or {}).get(i,1),2);put(u,t+6,len(ids),1);put(u,t+7,len(ids),1);put(u,t+0xc,cursor)
            put(u,t+0x10,i*spacing*256);put(u,t+0x14,128*256);put(u,r(i),i,2);put(u,r(i)+4,player,1)
            if ids:u.mem_write(cursor,struct.pack('<'+'H'*len(ids),*ids))
            cursor+=len(ids)*2
            put(u,terrain+0xc+2*(4*256+i*spacing//32),i,2)
        return u
    def call(u,start,ecx=7,edx=1,args=()):
        count,returned,trace=0,False,[];entries={a for a,b in allow}
        def hook(m,a,n,unused):
            nonlocal count,returned
            if a==stop:returned=True;m.emu_stop();return
            if not any(lo<=a and a+n<=hi for lo,hi in allow):raise RuntimeError(f'Unreviewed instruction{a:#x};trace{trace[-8:]}')
            count+=1
            if a in entries:
                sp=m.reg_read(UC_X86_REG_ESP);trace.append({'callee':hex(a),'ecx':m.reg_read(UC_X86_REG_ECX),'edx':m.reg_read(UC_X86_REG_EDX),'stack_arguments':[get(m,sp+4+j*4)for j in range(3)]})
        h=u.hook_add(UC_HOOK_CODE,hook);sp=stack+0x1e000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),stop,*args));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
        try:u.emu_start(start,stop+1,count=100000)
        except unicorn.UcError as e:raise RuntimeError(f'{e}at{u.reg_read(UC_X86_REG_EIP):#x};trace{trace[-8:]}')from e
        finally:u.hook_del(h)
        if not returned:raise RuntimeError('No complete return within100000instructions')
        return {'complete_return':True,'instruction_count':count,'return_eax':u.reg_read(UC_X86_REG_EAX),'native_call_trace':trace}
    def record(label,inputs,ex,got,want):
        assert got==want,(label,inputs,got,want)
        cases.append({'label':label,'fixture_inputs':inputs,'execution':ex,'observed':got,'expected_from_static_contract':want,'matched':True,'helper_stubs':[]})
    def cached(u,n=6):return list(u.mem_read(0x6b1aa0+7*2500,n))
    chain={0:[],1:[2],2:[1,3],3:[2,4],4:[3]}
    for blocker in (0,1,2,3,4):
        for ground in(False,True):
            u=new(chain)
            if blocker:put(u,r(blocker)+(0x20 if ground else 0x1c),unit)
            ex=call(u,0x4abe90)
            reached=4 if blocker in(0,1,4)else blocker
            expected=[0]+[2 if i<=reached else 0 for i in range(1,5)]+[0]
            record('flood crosses unthreatened neighbors but marks threatened boundary without expanding it',{'blocker_region':blocker,'ground_target':ground,'source':1,'ground_prefix_graph':chain},ex,[cached(u),get(u,0x6b68c0+28),get(u,0x6b68e0+28)],[expected,100,1])
    for graph in ({0:[],1:[3],2:[],3:[2],4:[2]}, {0:[],1:[2],2:[3],3:[1],4:[]}):
        u=new(graph);ex=call(u,0x4abe90)
        want=[0,2,2,2,0,0]
        record('flood repeats ascending region scans for backwards IDs and closes cycles',{'graph':graph},ex,cached(u),want)
    u=new(chain);ex=call(u,0x4abe90,edx=0)
    record('source region zero is marked pending but excluded from flood scan',{'source':0},ex,cached(u),[1,0,0,0,0,0])
    for state,flags in ((3,0),(6,0x20),(0,0x20),(8,0x40)):
        u=new(chain);put(u,r(2)+5,state,1);put(u,r(2)+8,flags,1);ex=call(u,0x4abe90)
        record('military flood blocker ignores region state and flags without target pointers',{'intermediate_state':state,'flags':flags},ex,cached(u),[0,2,2,2,2,0])
    for changed_source,elapsed in ((False,109),(False,110),(True,109),(True,110)):
        u=new(chain);first=call(u,0x4ab330,args=(4,));put(u,r(2)+0x20,unit);put(u,0x5349d8,elapsed)
        source=2 if changed_source else 1;ex=call(u,0x4ab330,edx=source,args=(4,));ex['preceding_complete_call']=first
        record('military reachability cache expires at inclusive ten or changed source',{'cached_source':1,'new_source':source,'now':elapsed,'new_blocker':2},ex,
               [first['return_eax'],ex['return_eax'],get(u,0x6b68c0+28),get(u,0x6b68e0+28)], [1,1 if changed_source or elapsed<110 else 0,elapsed if changed_source or elapsed>=110 else 100,source])
    for stamp,now in ((100,50),(0xfffffffA,0),(0xfffffffA,4)):
        u=new(chain);put(u,r(2)+0x20,unit);put(u,0x6b68c0+28,stamp);put(u,0x6b68e0+28,1);put(u,0x6b1aa0+7*2500+4,2,1);put(u,0x5349d8,now)
        ex=call(u,0x4ab330,args=(4,));fresh=now>=((stamp+10)&0xffffffff)
        record('cached flood timer compares unsigned now against wrapping stored time plus ten',{'stamp':stamp,'now':now,'cached_target_class':2,'current_blocker':2},ex,[ex['return_eax'],get(u,0x6b68c0+28)],[0 if fresh else 1,now if fresh else stamp])
    for weapon in(0,130):
        u=new(chain);put(u,r(1)+0x1c,unit);put(u,r(2)+0x20,unit);put(u,unit+0x64,37,2);put(u,0x5213a8+37,weapon,1)
        ex=call(u,0x4ab330,args=(4,));record('source target with real airweapon bypasses blocked flood reachability',{'source_air_target_weapon':weapon,'intermediate2_target':True},ex,ex['return_eax'],int(weapon!=130))
    for variant in('player8','player11','equal_ids','different_groups','direct_neighbor'):
        graph={0:[],1:[4],2:[],3:[],4:[1]}if variant=='direct_neighbor'else chain
        u=new(graph,{4:2}if variant=='different_groups'else None);put(u,r(4)+0x20,unit)
        ex=call(u,0x4ab330,8 if variant=='player8'else 11 if variant=='player11'else 7,args=(1 if variant=='equal_ids'else 4,))
        record('military reachability early player equalgroup and directneighbor outcomes',{'variant':variant,'target_threat_present':True},ex,
               [ex['return_eax'],sum(t['callee']=='0x4abe90'for t in ex['native_call_trace'])], [0 if variant=='different_groups'else 1,0])
    for target,state,flags,air,ground in ((1,3,0,False,False),(4,0,0,False,False),(4,3,0,False,False),(4,8,0,False,False),(4,0,0x20,False,False),(4,0,0x40,False,False),(4,0,0,True,False),(4,0,0,False,True)):
        u=new(chain);put(u,r(2)+5,state,1);put(u,r(2)+8,flags,1);put(u,r(2)+0x1c,unit if air else 0);put(u,r(2)+0x20,unit if ground else 0)
        ex=call(u,0x496ac0,args=(target,));reject=target!=1 and(state==3 or flags&0x20 or air or ground)
        record('strategic route accepts IDs and rejects only marked intermediate regions',{'source':1,'target':target,'intermediate2_state':state,'flags':flags,'air_target':air,'ground_target':ground},ex,ex['return_eax'],int(not reject))
    for mode in('different_group','no_path','direct_edge','threatened_endpoint'):
        graph={0:[],1:[2],2:[1,3],3:[2,4],4:[3]}if mode=='threatened_endpoint'else{0:[],1:[],2:[],3:[],4:[]}
        if mode=='direct_edge':graph[1]=[4];graph[4]=[1]
        u=new(graph,{4:2}if mode=='different_group'else None)
        if mode=='threatened_endpoint':put(u,r(4)+5,3,1);put(u,r(4)+8,0x20,1);put(u,r(4)+0x20,unit)
        ex=call(u,0x496ac0,args=(4,));record('strategic route accepts different groups no-path short path and threatened endpoint',{'mode':mode},ex,ex['return_eax'],1)
    for blocker in(2,49,50,300):
        graph={0:[]};graph.update({i:[i+1]if i<351 else[]for i in range(1,352)});u=new(graph,spacing=8);put(u,r(blocker)+5,3,1)
        ex=call(u,0x496ac0,args=(351,))
        record('route checks only interior of retained fiftyregion source prefix on arena exhaustion',{'requested_goal':351,'blocked_region':blocker,'chain_length':351,'stored_prefix_limit':50,'center_spacing_pixels':8},ex,ex['return_eax'],int(blocker not in(2,49)))
    u=new(chain);put(u,r(2)+5,3,1);put(u,r(1)+5,8,1);put(u,r(1)+2,2,2);put(u,r(4)+5,4,1);put(u,r(4)+0x14,31,2)
    put(u,unit+0xc,sprite);put(u,unit+0x64,37,2);put(u,unit+0x4c,7,1);put(u,unit+0x4d,1,1);put(u,unit+0xdc,1);put(u,unit+0x134,node);put(u,unit+0x13a,30,2)
    put(u,node+8,4,1);put(u,node+0xc,unit);put(u,node+0x10,r(4));put(u,sprite+0x14,512,2);put(u,sprite+0x16,128,2)
    put(u,0x665b08+28,unit);put(u,0x521660+37*4,0);put(u,0x521578+37,0,1)
    route=call(u,0x496ac0,edx=4,args=(1,));ex=call(u,0x4ac230,r(1),37,(128,128,r(2)));ex['preceding_complete_route']=route
    record('ground surplus selector invokes blocked route but does not test its zero return',{'donor_region':4,'staging_region':1,'intermediate_blocker':2,'ground_surplus':1},ex,[route['return_eax'],ex['return_eax']],[0,node])
    return {'format_version':1,'original_code_executed':True,'complete_game_executed':False,'windows_or_host_api_called':False,'helper_stubs':[],'all_matched':True,'case_count':len(cases),'complete_return_count':len(cases),'prefix_case_count':0,'cases':cases,
            'allowlisted_preferred_va_ranges':[[hex(a),hex(b)]for a,b in allow],'runtime_versions':{'python':platform.python_version(),'unicorn':unicorn.__version__,'pefile':pefile.__version__,'capstone':capstone.__version__,'architecture':platform.machine(),'system':platform.system()}}


def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--exe', type=Path, required=True); parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--execute-isolated-original-code', action='store_true'); parser.add_argument('--probe-out', type=Path); args = parser.parse_args()
    if args.execute_isolated_original_code and not args.probe_out: parser.error('Execution requires --probe-out')
    raw = args.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != TARGET_HASH: parser.error('Unknown executable hash')
    pe = pefile.PE(data=raw); assert pe.OPTIONAL_HEADER.ImageBase == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); sha = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    result = {'format_version': 1, 'target_sha256': TARGET_HASH, 'target_version': '1.17.0.1', 'inspector_sha256': sha, 'dependency_script_sha256': dependencies(), 'original_code_executed': False, 'witnesses': {}}
    for name, (lo, hi) in WITNESSES.items():
        code = pe.get_data(lo - 0x400000, hi - lo); instructions = list(md.disasm(code, lo)); assert sum(i.size for i in instructions) == len(code), name
        result['witnesses'][name] = {'start_preferred_va': hex(lo), 'end_exclusive_preferred_va': hex(hi), 'bytes_sha256': hashlib.sha256(code).hexdigest(), 'instructions': [{'address': hex(i.address), 'bytes': i.bytes.hex(), 'mnemonic': i.mnemonic, 'operands': i.op_str} for i in instructions]}
    args.out.parent.mkdir(parents=True, exist_ok=True); args.out.write_text(json.dumps(result, indent=2) + '\n')
    if args.execute_isolated_original_code:
        run = execute(pe); run.update({'target_sha256': TARGET_HASH, 'target_version': '1.17.0.1', 'probe_script_sha256': sha, 'dependency_script_sha256': dependencies()})
        args.probe_out.parent.mkdir(parents=True, exist_ok=True); args.probe_out.write_text(json.dumps(run, indent=2) + '\n')
        print(json.dumps({'cases': run['case_count'], 'complete': run['complete_return_count'], 'sha256': sha}))
    else: print('Wrote static AI-route evidence; original code not executed')


if __name__ == '__main__': main()
