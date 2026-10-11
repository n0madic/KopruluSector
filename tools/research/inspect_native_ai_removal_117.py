#!/usr/bin/env python3
"""Recover generic AI removal, killed-unit transitions, roster and town cleanup.

Static by default. Explicit bounded execution uses reviewed original callees
and private memory/pools only, without helper stubs, full-game or guest APIs.
"""
import argparse,hashlib,json,platform,struct
from pathlib import Path
import capstone,pefile
import inspect_native_guard_transfer_117 as transfer
import probe_native_placement_117 as placement
import inspect_native_training_lifecycle_117 as lifecycle

TARGET_HASH=transfer.TARGET_HASH
PLAYER_BASE,STRIDE=transfer.PLAYER_BASE,transfer.STRIDE
WITNESSES={
 "region_has_owned_building_or_worker":(0x4861A0,0x4861F7),"units_in_region":(0x4222D0,0x422323),
 "region_bounds":(0x481A00,0x481A22),"attack_roster_remove_first_marker":(0x4839A0,0x4839EB),
 "worker_list_release":(0x483B70,0x483BC1),"building_list_release":(0x483B10,0x483B61),
 "town_empty_or_migrate":(0x485840,0x485963),"delete_empty_town":(0x4856A0,0x485755),
 "clear_resource_area_used_flag":(0x497380,0x49738E),
}


def dependencies():
    mods=(transfer,placement,lifecycle,transfer.guards,transfer.primary,transfer.workers,transfer.combat,transfer.opcodes,
      transfer.guards.default,transfer.guards.selectors,transfer.guards.default.dispatch,transfer.guards.default.production,
      transfer.guards.default.dispatch.training,transfer.guards.default.dispatch.research,
      transfer.guards.default.dispatch.training.unit_reqs,transfer.guards.default.dispatch.research.requirements)
    return {Path(m.__file__).name:hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in mods}


def probes(data,pe):
    if platform.machine().lower() not in ('x86_64','amd64'):raise RuntimeError('Use reviewed x86_64 Rosetta runtime')
    import unicorn
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP,UC_X86_REG_EIP
    unit,sprite,ai,regions,town,workerpool,buildingpool,townpool,scriptpool=0x800000,0x805000,0x806000,0x810000,0x830000,0x840000,0x850000,0x860000,0x880000
    mapbase,stack,stop=0xB00000,0x900000,0xA00000
    image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095;cases=[]
    allowed=list(WITNESSES.values())+list(transfer.WITNESSES.values())+list(placement.BLOCKS.values())+list(lifecycle.BLOCKS.values())
    allowed+=list(transfer.workers.BLOCKS.values())+list(transfer.primary.BLOCKS.values())+list(transfer.guards.WITNESSES.values())+list(transfer.guards.default.WITNESSES.values())+list(transfer.guards.selectors.WITNESSES.values())+list(transfer.opcodes.WITNESSES.values())
    for mod in (transfer.guards.default.production,transfer.guards.default.dispatch,transfer.guards.default.dispatch.training,transfer.guards.default.dispatch.training.unit_reqs,transfer.guards.default.dispatch.research,transfer.guards.default.dispatch.research.requirements):allowed+=list(mod.WITNESSES.values())
    allowed += [transfer.combat.BLOCKS['change_region_state']]
    def w32(u,a,v):u.mem_write(a,struct.pack('<I',v&0xffffffff))
    def w16(u,a,v):u.mem_write(a,struct.pack('<H',v&65535))
    def w8(u,a,v):u.mem_write(a,bytes([v&255]))
    def r32(u,a):return struct.unpack('<I',u.mem_read(a,4))[0]
    def r16(u,a):return struct.unpack('<H',u.mem_read(a,2))[0]
    def r8(u,a):return u.mem_read(a,1)[0]
    def new(ident=37,ai_type=4,region_state=4,properties=0):
        u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,size);u.mem_write(0x400000,image);u.mem_map(unit,0xA0000);u.mem_map(mapbase,0x60000);u.mem_map(stack,0x2000);u.mem_map(stop,0x1000)
        u.mem_write(PLAYER_BASE,bytes(STRIDE*8));u.mem_write(0x6726d0,bytes(0x120));u.mem_write(0x534914,bytes([1])*144)
        w8(u,0x6726d0+7*36,1);w32(u,0x69b584,mapbase);w16(u,mapbase,5)
        for p in range(8):w32(u,0x6b1a74+p*4,regions+p*0x1000);w32(u,0x69b518+p*8,townpool);w32(u,0x69b51c+p*8,0)
        for y in range(8):
            for x in range(8):w16(u,mapbase+0xc+2*(y*256+x),3)
        w16(u,mapbase+0xc+2*(1*256+1),1)
        for idx in range(5):
            u.mem_write(mapbase+0x44a14+idx*64,struct.pack('<4H',0,0,64,64))
        u.mem_write(unit+0x98,struct.pack('<5H',*([228]*5)));w16(u,unit+0x64,ident);w8(u,unit+0x4c,7);w8(u,unit+0x4d,1);w32(u,unit+0xdc,1);w32(u,unit+0xc,sprite);w16(u,sprite+0x14,96);w16(u,sprite+0x16,96)
        w32(u,unit+0x134,ai);w8(u,ai+8,ai_type);w32(u,ai+0xc,unit);w32(u,ai+0x10,regions+7*0x1000+0x34);w32(u,ai+0x14,town)
        region=regions+7*0x1000+0x34;w16(u,region,1);w8(u,region+4,7);w8(u,region+5,region_state);w8(u,region+6,88);w8(u,region+8,0x21);w16(u,region+0xa,1);w16(u,region+0x14,100);w32(u,region+0x2c,0x820000);w32(u,region+0x30,ai)
        w32(u,0x521660+ident*4,properties);w32(u,0x69b558,0);w32(u,0x665ae0,0);w32(u,0x665b08+28,unit)
        w32(u,town+8,workerpool);w32(u,town+0x10,buildingpool);w8(u,town+0x18,7);w32(u,0x69b51c+56,town)
        w32(u,0x6a2e6c,scriptpool);w32(u,0x6a2e70,0)
        w32(u,0x5c99b0,0);w32(u,0x5d6e48,0);w32(u,0x5d6e5c,0);w32(u,0x5da38c,0);w32(u,0x5349d8,100)
        return u
    def spatial_blocker(u,owner=7,properties=1,sprite_region=1):
        a=0x5da3a0;s=sprite+0x100;w16(u,a+0x64,106);w8(u,a+0x4c,owner);w32(u,a+0xc,s);w16(u,s+0x14,48);w16(u,s+0x16,48);w32(u,0x521660+106*4,properties)
        w16(u,mapbase+0xc+2*(1*256+1),sprite_region)
        for table,lo,hi in ((0x5c99b8,40,56),(0x5d03f8,40,56)):
            w32(u,table,1);w32(u,table+4,lo);w32(u,table+8,1);w32(u,table+12,hi)
        for f,v in ((0x13c,0),(0x140,1),(0x144,0),(0x148,1)):w32(u,a+f,v)
        w32(u,0x5c99b0,2)
    def call(u,start=0x48dbc0,ecx=unit,edx=0,args=()):
        reached,count,trace=False,0,[];entries={a for a,b in allowed}
        def hook(m,a,n,_):
            nonlocal reached,count
            if a==stop:reached=True;m.emu_stop();return
            if not any(lo<=a and a+n<=hi for lo,hi in allowed):raise RuntimeError(f'Unreviewed original instruction {a:#x}')
            count+=1
            if a in entries:trace.append(hex(a))
        h=u.hook_add(UC_HOOK_CODE,hook);u.mem_write(stack+0x1800,struct.pack('<'+'I'*(len(args)+1),stop,*args));u.reg_write(UC_X86_REG_ESP,stack+0x1800);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
        try:u.emu_start(start,stop+1,count=200000)
        except unicorn.UcError as error:raise RuntimeError(f'Guest error {error} at {u.reg_read(UC_X86_REG_EIP):#x}; calls{trace[-12:]}') from error
        finally:u.hook_del(h)
        if not reached:raise RuntimeError('No complete return within200000 original instructions')
        return u.reg_read(UC_X86_REG_EAX),count,trace
    def observe(label,inputs,ex,got,want):
        if got!=want:raise AssertionError((label,inputs,got,want))
        cases.append({'label':label,'fixture_inputs':inputs,'observed':got,'expected_from_static_contract':want,'matched':True,'original_instruction_count':ex[1],'native_call_trace':ex[2],'execution_scope':'complete reviewed functions and actual callees for fixture path','helper_stubs':[]})
    region=regions+7*0x1000+0x34
    for state in range(11):
        for killed in (0,1):
            for same in (False,True):
                u=new(region_state=state)
                if same:w16(u,mapbase+0xc+2*(3*256+3),1)
                ex=call(u,edx=killed)
                changed=bool(killed) and not same and state not in (0,1,2,8,9)
                observe('killed military crossregion transition excludes native states and same region',{'state':state,'was_killed':killed,'same_region':same},ex,
                  {'unitAI':r32(u,unit+0x134),'region_state':r8(u,region+5),'priority':r8(u,region+6),'flags':r8(u,region+8),'military_free':r32(u,0x820000+0x4e20),'spatial_depth':r32(u,0x5d6e48)},
                  {'unitAI':0,'region_state':3 if changed else state,'priority':10 if changed else 88,'flags':0 if changed else 0x21,'military_free':ai,'spatial_depth':0})
    for owner,properties,sprite_region in ((7,1,1),(7,8,1),(7,9,1),(7,0,1),(0,1,1),(7,1,3)):
        u=new();spatial_blocker(u,owner,properties,sprite_region);ex=call(u,edx=1);blocked=owner==7 and properties&9 and sprite_region==1
        observe('killed crossregion transition actual spatial query ownedBuildingWorker gate',{'blocker_owner':owner,'blocker_properties':properties,'blocker_region':sprite_region},ex,
          {'region_state':r8(u,region+5),'priority':r8(u,region+6),'spatial_depth':r32(u,0x5d6e48),'query_offset':r32(u,0x5d6e5c)},
          {'region_state':4 if blocked else 3,'priority':88 if blocked else 10,'spatial_depth':0,'query_offset':0})
    for ident,roster in ((37,[38,38,0]),(30,[31,6,6,0]),(5,[6,6,0]),(37,[0]),(37,[1]*63)):
        for killed in (0,1):
            u=new(ident=ident,region_state=8);p=PLAYER_BASE+7*STRIDE;arr=roster+[0]*(64-len(roster));u.mem_write(p+0x230,struct.pack('<64H',*arr));ex=call(u,edx=killed)
            expected=arr[:];target=(5 if ident==30 else ident)+1
            if target in expected[:63]:expected[expected.index(target)]=229
            observe('state8 military removal replaces only first normalized attack entry by229',{'id':ident,'roster':roster,'was_killed':killed},ex,list(struct.unpack('<64H',u.mem_read(p+0x230,128))),expected)
    for stored,first,second in ((unit,34,34),(unit,7,34),(unit,7,7),(unit+0x400,34,34),(0,34,34)):
        u=new(ident=34,ai_type=1);w8(u,ai+9,99);p=PLAYER_BASE+7*STRIDE;w32(u,p+0x4d4,stored);w16(u,unit+0x64,first);w32(u,unit+0x6c,unit+0x200);w16(u,unit+0x200+0x64,second)
        ex=call(u);expected=stored
        if first==34 and stored==unit:expected=unit
        observe('Medic replacement sees current player list including removed unit',{'stored_free_medic':stored,'first_type':first,'second_type':second},ex,{'free_medic':r32(u,p+0x4d4),'unitAI':r32(u,unit+0x134)}, {'free_medic':expected,'unitAI':0})
    for roster in ([],[(7,0)],[(7,0),(34,0)],[(34,0)]):
        u=new(ident=34,ai_type=1);p=PLAYER_BASE+7*STRIDE;w32(u,p+0x4d4,unit);w32(u,0x665b08+28,unit+0x200 if roster else 0)
        for j,(ident,_) in enumerate(roster):a=unit+0x200+j*0x200;w16(u,a+0x64,ident);w32(u,a+0x6c,a+0x200 if j+1<len(roster) else 0)
        ex=call(u);selected=next((unit+0x200+j*0x200 for j,(ident,_) in enumerate(roster) if ident==34),0)
        observe('Medic reference clears then selects first type34 with noAI requirement',{'replacement_types':[v[0] for v in roster]},ex,r32(u,p+0x4d4),selected)
    u=new();w32(u,unit+0x134,0);w32(u,region+0x1c,unit);ex=call(u)
    observe('nullAI still visitscomputerregion references before returning',{'initial_AI':0,'region_target':unit},ex,{'unitAI':r32(u,unit+0x134),'region_target':r32(u,region+0x1c)}, {'unitAI':0,'region_target':0})
    for ident,properties in ((7,8),(106,1),(35,0),(36,0),(42,0),(188,1)):
        for keep_town in (False,True):
            u=new(ident=ident,ai_type=2 if properties&8 else 3,properties=properties);w32(u,ai+0x10,unit);w32(u,ai+0x14,town);w8(u,town+0x1a,1)
            w32(u,town+0xc if properties&8 else town+0x14,ai);w32(u,town+0x24,unit);w32(u,town+0x28,unit)
            if keep_town:
                w32(u,town+0x14,ai+0x100);w32(u,ai+0x100+0x10,unit+0x400);w16(u,unit+0x400+0x64,111)
                if not properties&8:w32(u,ai,ai+0x100);w32(u,ai+0x100+4,ai);w32(u,town+0x14,ai)
            ex=call(u)
            released=bool(properties&8 or properties&1 and ident!=188 or ident in (35,36,42))
            observe('worker or building release retained versus empty donor town',{'unit_id':ident,'properties':properties,'keep_other_building':keep_town},ex,
              {'unitAI':r32(u,unit+0x134),'town_head':r32(u,0x69b51c+56),'town_free':r32(u,townpool+0xb3b0),'worker_free':r32(u,workerpool+0x5dc0),'building_free':r32(u,buildingpool+0xabe0),'worker_count':r8(u,town+0x1a),'main':r32(u,town+0x24),'building_scv':r32(u,town+0x28)},
              {'unitAI':0,'town_head':town if keep_town or not released else 0,'town_free':0 if keep_town or not released else town,'worker_free':ai if released and properties&8 else 0,'building_free':ai if released and not properties&8 else 0,'worker_count':0 if released and properties&8 else 1,'main':0 if released else unit,'building_scv':0 if released else unit})
    for count in (0,1,255):
        u=new(ident=7,ai_type=2,properties=8);w32(u,ai+0x10,unit);w32(u,ai+0x14,town);w32(u,town+0xc,ai);w8(u,town+0x1a,count);ex=call(u)
        observe('worker count decrementsbyte and town emptiness useslists not count',{'worker_count':count,'one_worker_record':True,'buildings':[]},ex,
          {'count':r8(u,town+0x1a),'worker_head':r32(u,town+0xc),'town_free':r32(u,townpool+0xb3b0)}, {'count':(count-1)&255,'worker_head':0,'town_free':town})
    for count,area in ((0,0),(2,0),(2,1),(2,250)):
        u=new(ident=106,ai_type=3,properties=1);w32(u,ai+0x10,unit);w32(u,ai+0x14,town);w32(u,town+0x14,ai);w8(u,town+0x1c,area)
        s1,s2=scriptpool,scriptpool+0x34
        if count:
            w32(u,0x6a2e70,s1);w32(u,s1,s2);w32(u,s2+4,s1);w32(u,s1+0x2c,town);w32(u,s2+0x2c,town+0x200)
        w32(u,0x665ae0,unit+0x200);w32(u,unit+0x200+0xc,sprite+0x100);w8(u,unit+0x200+0x4d,1);w16(u,unit+0x200+0x64,176);w32(u,0x521660+176*4,0x2000);w8(u,unit+0x200+0xd9,77)
        w8(u,0x6a2e8f+area*48,0xff);ex=call(u)
        observe('empty town cleanup cancels matching scripts resetsglobal resource marker and area flag',{'script_count':count,'resource_area':area},ex,
          {'active_script_head':r32(u,0x6a2e70),'script_free':r32(u,scriptpool+0x1450),'resource_marker':r8(u,unit+0x200+0xd9),'area_flags':r8(u,0x6a2e8f+area*48),'town_free':r32(u,townpool+0xb3b0)},
          {'active_script_head':s2 if count else 0,'script_free':s1 if count else 0,'resource_marker':0,'area_flags':0xfd if area else 0xff,'town_free':town})
    for workers_count,overlords_count in ((1,0),(2,0),(0,1),(0,2),(1,1),(2,2)):
        for destination_positions in ([],[64], [64,16], [64,-64]):
            u=new(ident=106,ai_type=3,properties=1);w32(u,ai+0x10,unit);w32(u,ai+0x14,town);w32(u,town+0x14,ai);w8(u,town+0x1a,workers_count);w16(u,PLAYER_BASE+7*STRIDE+0x218,0x20)
            w32(u,ai,ai+0x100 if overlords_count else 0)
            w32(u,town+0xc,ai+0x500 if workers_count else 0)
            for j in range(overlords_count):
                node=ai+0x100+j*0x40;a=unit+0x200+j*0x200
                w32(u,node,node+0x40 if j+1<overlords_count else 0);w32(u,node+4,ai if j==0 else node-0x40);w8(u,node+8,3);w32(u,node+0x10,a);w32(u,node+0x14,town)
                w32(u,a+0x134,node);w16(u,a+0x64,42);w8(u,a+0x4c,7);w8(u,a+0x4d,1);w32(u,a+0xdc,5);w32(u,a+0xc,sprite+0x200+j*0x100);w32(u,0x521660+42*4,0)
            for j in range(workers_count):
                node=ai+0x500+j*0x20;a=unit+0x800+j*0x200
                w32(u,node,node+0x20 if j+1<workers_count else 0);w32(u,node+4,node-0x20 if j else 0);w8(u,node+8,2);w32(u,node+0x10,a);w32(u,node+0x14,town)
                w32(u,a+0x134,node);w16(u,a+0x64,7);w8(u,a+0x4c,7);w8(u,a+0x4d,1);w32(u,a+0xdc,1);w32(u,a+0xc,sprite+0x500+j*0x100);w32(u,0x521660+7*4,8)
            w32(u,town,town+0x200 if destination_positions else 0)
            for j,x in enumerate(destination_positions):
                target=town+0x200+j*0x200;w32(u,target,target+0x200 if j+1<len(destination_positions) else 0);w32(u,target+4,town if not j else target-0x200)
                w32(u,target+8,workerpool);w32(u,target+0x10,buildingpool);w8(u,target+0x18,7);w16(u,target+0x20,x)
            ex=call(u)
            selected=town+0x200+(0 if len(destination_positions)<2 or abs(destination_positions[0])<=abs(destination_positions[1]) else 1)*0x200 if destination_positions else town
            moved=bool(destination_positions)
            observe('last ordinary building removal migrates workerOverlord-only town to nearest with firsttie',{'remaining_workers':workers_count,'remaining_overlords':overlords_count,'destination_x':destination_positions},ex,
              {'owner_town_head':r32(u,0x69b51c+56),'town_free':r32(u,townpool+0xb3b0),'migration_lock':r32(u,0x69b558),'worker_town':[r32(u,ai+0x500+j*0x20+0x14) for j in range(workers_count)],'overlord_town':[r32(u,ai+0x100+j*0x40+0x14) for j in range(overlords_count)],'source_worker_count':r8(u,town+0x1a),'recipient_worker_count':r8(u,selected+0x1a) if moved else 0,'source_worker_head':r32(u,town+0xc),'source_building_head':r32(u,town+0x14)},
              {'owner_town_head':town+0x200 if moved else town,'town_free':town if moved else 0,'migration_lock':0,'worker_town':[selected]*workers_count,'overlord_town':[selected]*overlords_count,'source_worker_count':0 if moved else workers_count,'recipient_worker_count':workers_count if moved else 0,'source_worker_head':0 if moved else ai+0x500 if workers_count else 0,'source_building_head':0 if moved else ai+0x100 if overlords_count else 0})
    return {'format_version':1,'target_sha256':TARGET_HASH,'probe_script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'dependency_script_sha256':dependencies(),'runtime_versions':{'python':platform.python_version(),'unicorn':unicorn.__version__,'capstone':capstone.__version__,'pefile':pefile.__version__,'architecture':platform.machine(),'system':platform.system()},'original_code_executed':True,'complete_game_executed':False,'helper_stubs':[],'allowlisted_preferred_va_ranges':[[hex(a),hex(b)] for a,b in allowed],'all_matched':True,'case_count':len(cases),'cases':cases}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--execute-isolated-original-code',action='store_true');p.add_argument('--probe-out',type=Path);a=p.parse_args()
    if a.execute_isolated_original_code and a.probe_out is None:p.error('--probe-out required')
    data=a.exe.read_bytes()
    if hashlib.sha256(data).hexdigest()!=TARGET_HASH:p.error('Wrong EXE hash')
    pe=pefile.PE(data=data);c=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    report={'format_version':1,'target_sha256':TARGET_HASH,'inspector_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'dependency_script_sha256':dependencies(),'original_code_executed':False,'complete_game_executed':False,'witnesses':{},'remaining_dependencies':['higher nativeAI allocation/destruction callers and pool restoration','construction eligibility variants in transplanted BuildingAI allocation; selectedOverlordcampaign-flying path executes fully','alternate force-building argument of standalone486af0 is not used by48dbc0']}
    for name,(start,end) in WITNESSES.items():
        raw=pe.get_data(start-0x400000,end-start);report['witnesses'][name]={'start_preferred_va':hex(start),'end_exclusive_preferred_va':hex(end),'bytes_sha256':hashlib.sha256(raw).hexdigest(),'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str} for i in c.disasm(raw,start)]}
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(report,indent=2)+'\n')
    if a.execute_isolated_original_code:
        result=probes(data,pe);a.probe_out.parent.mkdir(parents=True,exist_ok=True);a.probe_out.write_text(json.dumps(result,indent=2)+'\n');print(f"{result['case_count']} native AI-removal cases passed; no stubs")
    else:print('Wrote static AI-removal witnesses; original code not executed')


if __name__=='__main__':main()
