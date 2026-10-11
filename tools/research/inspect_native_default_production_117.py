#!/usr/bin/env python3
"""Recover default producer choices, request insertion and needed-guard helpers.

Static by default. Explicit original-code probes use only private memory and
reviewed actual callees, with no helper stubs or game/API execution.
"""
import argparse, hashlib, json, platform, struct
from pathlib import Path
import capstone, pefile
import inspect_native_spending_dispatch_117 as dispatch
import inspect_native_production_117 as production

TARGET_HASH=dispatch.TARGET_HASH
PLAYER_BASE,STRIDE=dispatch.PLAYER_BASE,dispatch.STRIDE
WITNESSES={
 "producer_ai_region":(0x4B0960,0x4B097B),"producer_prerequisite_or_limit_reject":(0x4878D0,0x4878FB),
 "insert_default_spending_request":(0x482990,0x482A36),"insert_morph_parent_requests":(0x482130,0x48225D),
 "accumulate_unit_demand":(0x483680,0x4836C4),"unit_region":(0x481BA0,0x481BB0),
 "position_region":(0x481B10,0x481B98),"needed_guard_region_eligible":(0x4AEA70,0x4AEA9B),
 "matching_needed_guard_available":(0x4B25F0,0x4B2640),"mark_needed_guard_retry":(0x4B2750,0x4B2792),
}
CHOICES={35:[38,43],111:[0,1],113:[3,5],114:[8,12],155:[83],160:[66,65],167:[70,72]}


def dependencies():
    mods=(dispatch,production,dispatch.training,dispatch.research,dispatch.training.unit_reqs,dispatch.research.requirements)
    return {Path(m.__file__).name:hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in mods}


def probes(data,pe):
    if platform.machine().lower() not in ('x86_64','amd64'):raise RuntimeError('Use reviewed x86_64 Rosetta runtime')
    import unicorn
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP
    unit,ai,sprite,regions,guards,mapbase,stack,stop=0x800000,0x800400,0x800800,0x810000,0x820000,0xB00000,0x900000,0xA00000
    image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095;cases=[]
    allowed=list(WITNESSES.values())+list(production.WITNESSES.values())+list(dispatch.WITNESSES.values())+list(dispatch.training.WITNESSES.values())+list(dispatch.training.unit_reqs.WITNESSES.values())+list(dispatch.research.WITNESSES.values())+list(dispatch.research.requirements.WITNESSES.values())
    def w32(u,a,v):u.mem_write(a,struct.pack('<I',v&0xffffffff))
    def w16(u,a,v):u.mem_write(a,struct.pack('<H',v&65535))
    def w8(u,a,v):u.mem_write(a,bytes([v&255]))
    def r32(u,a):return struct.unpack('<I',u.mem_read(a,4))[0]
    def r16(u,a):return struct.unpack('<H',u.mem_read(a,2))[0]
    def r8(u,a):return u.mem_read(a,1)[0]
    def new(producer=111,stocks=(600,300)):
        u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,size);u.mem_write(0x400000,image);u.mem_map(unit,0x40000);u.mem_map(mapbase,0x60000);u.mem_map(stack,0x2000);u.mem_map(stop,0x1000)
        u.mem_write(PLAYER_BASE,bytes(STRIDE*8));u.mem_write(0x529604,bytes(228*12*4));u.mem_write(0x52c0c4,struct.pack('<'+'I'*(228*12),*([1]*(228*12))))
        u.mem_write(0x52655c,bytes([1])*(228*12));u.mem_write(0x523a78,struct.pack('<228H',*([65535]*228)));w32(u,0x5432fc,0);w32(u,0x508d50,0);w32(u,0x67cc90,0);w8(u,0x536720,1)
        w16(u,unit+0x64,producer);w8(u,unit+0x4c,7);w8(u,unit+0x4d,1);w32(u,unit+0xdc,3);w32(u,unit+0xc,sprite);w32(u,unit+0x134,ai);w8(u,ai+8,3);w8(u,unit+0xa6,0x17)
        w8(u,unit+0xc8,44);w8(u,unit+0xc9,61);u.mem_write(unit+0x98,struct.pack('<5H',*([228]*5)))
        if producer in (113,114):
            w32(u,unit+0xc0,unit+0xc00);w16(u,unit+0xc00+0x64,120 if producer==113 else 115)
        w32(u,0x5263d0+28,stocks[0]);w32(u,0x526400+28,stocks[1]);w32(u,0x69b584,mapbase);w32(u,0x6b1a74+28,regions)
        w16(u,sprite+0x14,64);w16(u,sprite+0x16,96);w16(u,mapbase+0xc+2*(3*256+2),3)
        w32(u,0x6be614+7*8,0)
        for ident in range(228):
            w16(u,0x523520+ident*2,10);w16(u,0x5236e8+ident*2,5);w8(u,0x523e10+ident,2);w32(u,0x521660+ident*4,0)
        w8(u,0x523c40,2);w8(u,0x523c40+111,2)
        p=PLAYER_BASE+7*STRIDE
        for j in range(3):w32(u,p+0xc+j*4,1000)
        for a,b,c in dispatch.training.SUPPLY_TABLES:w32(u,a+28,400);w32(u,b+28,0);w32(u,c+28,20)
        return u
    def call(u,start=0x4849c0,ecx=7,edx=unit,args=(160,)):
        reached,n,trace=False,0,[]
        def hook(m,a,s,_):
            nonlocal reached,n
            if a==stop:reached=True;m.emu_stop();return
            if not any(lo<=a and a+s<=hi for lo,hi in allowed):raise RuntimeError(f'Unreviewed original instruction {a:#x}')
            n+=1
            if a in (0x4b0960,0x482990,0x482130,0x4b25f0,0x4b2750,0x4878d0,0x482040):trace.append(hex(a))
        h=u.hook_add(UC_HOOK_CODE,hook);u.mem_write(stack+0x1800,struct.pack('<'+'I'*(len(args)+1),stop,*args));u.reg_write(UC_X86_REG_ESP,stack+0x1800);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
        u.emu_start(start,stop+1,count=100000);u.hook_del(h)
        if not reached:raise RuntimeError('No normal return within100000 original instructions')
        return u.reg_read(UC_X86_REG_EAX),n,trace
    def observe(label,inputs,ex,got,expected):
        if got!=expected:raise AssertionError((label,inputs,got,expected))
        cases.append({'label':label,'fixture_inputs':inputs,'observed':got,'expected_from_static_contract':expected,'matched':True,'original_instruction_count':ex[1],'native_call_trace':ex[2],'execution_scope':'complete reviewed functions and actual callees for fixture path','helper_stubs':[]})
    def heap(u):
        p=PLAYER_BASE+7*STRIDE
        return sorted([{'priority':r8(u,p+0x18+j*8),'kind':r8(u,p+0x19+j*8),'id':struct.unpack('<H',u.mem_read(p+0x1a+j*8,2))[0],'context':r32(u,p+0x1c+j*8)} for j in range(r8(u,p+0x210))],key=lambda e:(e['id'],e['priority']))
    def account(u):return [r32(u,PLAYER_BASE+7*STRIDE+j*4) for j in range(3)]
    context=regions+3*0x34
    for producer,choices in CHOICES.items():
        for blocked in range(len(choices)+1):
            u=new(producer);p=PLAYER_BASE+7*STRIDE
            for ident in choices[:blocked]:w8(u,p+0x3f0+ident,255)
            ex=call(u);ident=choices[blocked] if blocked<len(choices) else None
            expected=[] if ident is None else [{'priority':160,'kind':1,'id':ident,'context':context}]
            observe('producer default ordered alternatives and blocked limits',{'producer':producer,'blocked_ids':choices[:blocked]},ex,{'return':ex[0],'heap':heap(u),'demand':account(u)}, {'return':int(ident is not None),'heap':expected,'demand':[0,0,0] if ident is None else [10,5,2]})
    for producer in (0,34,36,106,112,168,227):
        u=new(producer);ex=call(u);observe('producer outside default choice set',{'producer':producer},ex,{'return':ex[0],'heap':heap(u)},{'return':0,'heap':[]})
    for availability in (False,True):
        u=new(111);w8(u,0x52655c+7*228,int(availability));ex=call(u)
        observe('Marine primary choice tests limit but not availability',{'available_marine':availability},ex,{'return':ex[0],'heap':heap(u)}, {'return':1,'heap':[{'priority':160,'kind':1,'id':0,'context':context}]})
    for ident in (0,44,62,63,68,103,228):
        for priority in (0,160):
            u=new();p=PLAYER_BASE+7*STRIDE;w16(u,p+0x220,ident+1);ex=call(u,args=[priority])
            parents={44:[43],62:[43],63:[61,61],68:[67,67],103:[38]}.get(ident,[])
            expected=[] if ident==228 else [{'priority':priority,'kind':1,'id':ident,'context':context}]+[{'priority':(priority-1)&255,'kind':1,'id':x,'context':context} for x in parents]
            expected.sort(key=lambda e:(e['id'],e['priority']));amount=0 if ident==228 else 1+len(parents)
            observe('explicit request consumption and morph-parent emission',{'explicit_id':ident,'priority':priority,'needed_guard_list':[]},ex,
                {'return':ex[0],'heap':heap(u),'demand':account(u),'explicit_word':struct.unpack('<H',u.mem_read(p+0x220,2))[0]}, {'return':int(ident!=228),'heap':expected,'demand':[10*amount,5*amount,2*amount],'explicit_word':0})
    for explicit in (False,True):
        u=new();p=PLAYER_BASE+7*STRIDE;w16(u,p+0x218,0x200);w16(u,p+0x220,1 if explicit else 0);ex=call(u)
        observe('explicit bypasses flag200 automatic request suppression',{'explicit':explicit,'flags':0x200},ex,{'return':ex[0],'heap':heap(u),'demand':account(u)},
            {'return':int(explicit),'heap':[{'priority':160,'kind':1,'id':0,'context':context}] if explicit else [],'demand':[10,5,2] if explicit else [0]*3})
    for clear_idle in (0,1):
        for force in (0,1):
            for flags in (1,0x201):
                u=new();p=PLAYER_BASE+7*STRIDE;w16(u,p+0x218,flags)
                ex=call(u,0x482990,7,context,[0,300,clear_idle,force])
                suppressed=not force and bool(flags&0x200)
                expected_flags=flags if suppressed else (flags if clear_idle else flags&~1)|0x40
                observe('default insertion arg3 flag1 and arg4 suppression with priority byte width',{'arg3':clear_idle,'arg4':force,'flags':flags,'priority':300},ex,
                    {'flags':r16(u,p+0x218),'heap':heap(u),'demand':account(u)},
                    {'flags':expected_flags,'heap':[] if suppressed else [{'priority':44,'kind':1,'id':0,'context':context}],'demand':[0]*3 if suppressed else [10,5,2]})
    for blocked in ([44],[43],[44,43]):
        u=new();p=PLAYER_BASE+7*STRIDE;w16(u,p+0x220,45)
        for ident in blocked:w8(u,p+0x3f0+ident,255)
        ex=call(u);expected=[{'priority':priority,'kind':1,'id':ident,'context':context} for ident,priority in ((44,160),(43,159)) if ident not in blocked]
        expected.sort(key=lambda e:(e['id'],e['priority']))
        observe('cost added before rejected main or morph-parent heap push',{'explicit_id':44,'blocked_ids':blocked},ex,
            {'return':ex[0],'heap':heap(u),'demand':account(u)}, {'return':int(bool(expected)),'heap':expected,'demand':[20,10,4]})
    for state,flags,parent,timer in ((0,4,0,9),(3,4,0,9),(0,4,unit,9),(0,0x200,0,0)):
        u=new(stocks=(0,0));p=PLAYER_BASE+7*STRIDE;w16(u,p+0x220,2);w16(u,p+0x218,flags)
        w32(u,0x6be614+7*8,guards);w32(u,guards+0xc,parent);w16(u,guards+0x10,1);w16(u,guards+0x16,64);w16(u,guards+0x18,96);w32(u,guards+0x1c,timer);w8(u,context+5,state)
        redirected=not parent and state!=3;chosen=0 if redirected else 1;suppressed=redirected and flags&0x200
        ex=call(u);expected=[] if suppressed else [{'priority':160,'kind':1,'id':chosen,'context':context}]
        observe('matching guard redirects explicit request into automatic chooser bypassing stocks and flag4',{'explicit_id':1,'stocks':[0,0],'flags':flags,'guard_parent':parent,'guard_timer':timer,'region_state':state},ex,
            {'return':ex[0],'heap':heap(u),'demand':account(u),'guard_timer':r32(u,guards+0x1c),'explicit_word':r16(u,p+0x220)},
            {'return':int(not suppressed),'heap':expected,'demand':[0]*3 if suppressed else [10,5,2],'guard_timer':1 if redirected and timer else timer,'explicit_word':0})
    for region_state in range(11):
        for guard_parent,region_need,flags in ((0,0,0),(unit,0,0),(0,1,0),(0,0,0x20)):
            u=new();w32(u,0x6be614+7*8,guards);w32(u,guards+0xc,guard_parent);w16(u,guards+0x10,0);w16(u,guards+0x16,64);w16(u,guards+0x18,96)
            w8(u,context+5,region_state);w32(u,context+0x1c,region_need);w8(u,context+8,flags)
            ex=call(u,0x4b25f0,7,0,[]);expected=int(not guard_parent and region_state!=3 and not region_need and not flags)
            observe('needed guard exact ID unassigned and region eligibility',{'region_state':region_state,'parent':guard_parent,'region_need':region_need,'region_flags':flags},ex,ex[0],expected)
    for requested in (5,30,31):
        for types,timestamps,assigned in [([5,30,5],[3,4,0],[0,0,0]),([5,30,5],[3,4,5],[unit,0,0])]:
            u=new();w32(u,0x6be614+7*8,guards)
            for j,(ident,timer,parent) in enumerate(zip(types,timestamps,assigned)):
                g=guards+j*0x40;w32(u,g,g+0x40 if j<2 else 0);w32(u,g+0xc,parent);w16(u,g+0x10,ident);w32(u,g+0x1c,timer)
            ex=call(u,0x4b2750,7,requested,[]);expected=[1 if not parent and timer and (5 if ident==30 else ident)==requested else timer for ident,timer,parent in zip(types,timestamps,assigned)]
            observe('needed guard retry marking normalizes storedTank30 to5',{'requested_id':requested,'types':types,'timestamps':timestamps,'parents':assigned},ex,[r32(u,guards+j*0x40+0x1c) for j in range(3)],expected)
    for desired,stored,need20 in ((5,30,0),(30,30,0),(0x10000,0,0),(0,0,1)):
        u=new();w32(u,0x6be614+7*8,guards);w16(u,guards+0x10,stored);w16(u,guards+0x16,64);w16(u,guards+0x18,96);w32(u,context+0x20,need20)
        ex=call(u,0x4b25f0,7,desired,[]);observe('guard availability exact full requested ID and second need field',{'desired_id':desired,'stored_id':stored,'region_need20':need20},ex,ex[0],int(desired==stored and not need20))
    u=new();w32(u,0x6be614+7*8,guards);w16(u,guards+0x10,30);w32(u,guards+0x1c,9)
    ex=call(u,0x4b2750,7,0x10005,[]);observe('guard retry marker truncates requested ID to LOW16',{'requested_id':0x10005,'stored_id':30,'timer':9},ex,r32(u,guards+0x1c),1)
    u=new(stocks=(600,300));p=PLAYER_BASE+7*STRIDE;ex=call(u,0x487110,unit,0,[])
    observe('empty spending building AI actual default generate enqueue pay pop',{'producer':111,'original_marine_recipe':True},ex,
      {'return':ex[0],'queue':list(struct.unpack('<5H',u.mem_read(unit+0x98,10))),'stocks':[r32(u,0x5263d0+28),r32(u,0x526400+28)],'heap':heap(u),'demand':account(u),'available':[r32(u,p+0xc+j*4) for j in range(3)]},
      {'return':1,'queue':[0,228,228,228,228],'stocks':[590,295],'heap':[],'demand':[0]*3,'available':[990,995,998]})
    return {'format_version':1,'target_sha256':TARGET_HASH,'probe_script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'dependency_script_sha256':dependencies(),'runtime_versions':{'python':platform.python_version(),'unicorn':unicorn.__version__,'capstone':capstone.__version__,'pefile':pefile.__version__,'architecture':platform.machine(),'system':platform.system()},'original_code_executed':True,'complete_game_executed':False,'helper_stubs':[],'allowlisted_preferred_va_ranges':[[hex(a),hex(b)] for a,b in allowed],'all_matched':True,'case_count':len(cases),'cases':cases}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--execute-isolated-original-code',action='store_true');p.add_argument('--probe-out',type=Path);a=p.parse_args()
    if a.execute_isolated_original_code and a.probe_out is None:p.error('--probe-out required')
    data=a.exe.read_bytes()
    if hashlib.sha256(data).hexdigest()!=TARGET_HASH:p.error('Wrong EXE hash')
    pe=pefile.PE(data=data);c=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);targets=struct.unpack('<8I',pe.get_data(0x484c1c-0x400000,32));indices=pe.get_data(0x484c3c-0x400000,133)
    report={'format_version':1,'target_sha256':TARGET_HASH,'inspector_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'dependency_script_sha256':dependencies(),'original_code_executed':False,'complete_game_executed':False,'producer_switch':{'minimum_type':35,'maximum_type':167,'targets_preferred_va':'0x484c1c','indices_preferred_va':'0x484c3c','type_target_map':{str(35+i):hex(targets[k]) for i,k in enumerate(indices)}},'witnesses':{}}
    for name,(start,end) in WITNESSES.items():
        raw=pe.get_data(start-0x400000,end-start);report['witnesses'][name]={'start_preferred_va':hex(start),'end_exclusive_preferred_va':hex(end),'bytes_sha256':hashlib.sha256(raw).hexdigest(),'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str} for i in c.disasm(raw,start)]}
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(report,indent=2)+'\n')
    if a.execute_isolated_original_code:
        result=probes(data,pe);a.probe_out.parent.mkdir(parents=True,exist_ok=True);a.probe_out.write_text(json.dumps(result,indent=2)+'\n');print(f"{result['case_count']} native default-production/guard cases passed; no stubs")
    else:print('Wrote static default-production witnesses; original code not executed')


if __name__=='__main__':main()
