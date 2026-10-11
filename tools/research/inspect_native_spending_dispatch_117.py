#!/usr/bin/env python3
"""Inspect building-AI spending dispatch and bounded actual-callee paths.

Static by default. Explicit probes use no helper stubs, external APIs or game
entry point. Full dispatch table coverage is distinct from consumer recovery.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import capstone
import pefile
import inspect_native_training_117 as training
import inspect_native_research_upgrade_117 as research

TARGET_HASH=training.TARGET_HASH
PLAYER_BASE,STRIDE=training.PLAYER_BASE,training.STRIDE
WITNESSES={
 "building_spending_dispatch":(0x487110,0x48726D),
 "default_production_request":(0x4849C0,0x484C1A),
 "producer_incomplete_or_special_order":(0x4B14D0,0x4B14F2),
 "default_production_enabled":(0x483750,0x483766),
 "take_explicit_training_request":(0x482BE0,0x482C07),
 "town_building_morph_consumer":(0x488100,0x48821B),
 "observer_consumer":(0x488030,0x4880F2),
 "addon_consumer":(0x485B70,0x485C98),
 "overlord_position_service":(0x486BF0,0x486C44),
 "nuclear_silo_queue_service":(0x485390,0x4853FD),
 "comsat_spell_gate_prefix":(0x48D3A0,0x48D3C4),
 "spell_cooldown_zero":(0x4833C0,0x4833D2),
}


def dependencies():
    modules=(training,research,training.unit_reqs,research.requirements)
    return {Path(m.__file__).name:hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in modules}


def probes(data,pe):
    if platform.machine().lower() not in ("x86_64","amd64"):raise RuntimeError("Use reviewed x86_64 Rosetta runtime")
    import unicorn
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP
    unit,ai,town,sprite,stack,stop=0x800000,0x800400,0x800800,0x800C00,0x900000,0xA00000
    base,image=pe.OPTIONAL_HEADER.ImageBase,pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095
    allowed=list(WITNESSES.values())+list(training.WITNESSES.values())+list(training.unit_reqs.WITNESSES.values())+list(research.WITNESSES.values())+list(research.requirements.WITNESSES.values());cases=[]
    def w32(u,a,v):u.mem_write(a,struct.pack("<I",v&0xFFFFFFFF))
    def w16(u,a,v):u.mem_write(a,struct.pack("<H",v&65535))
    def w8(u,a,v):u.mem_write(a,bytes([v&255]))
    def r32(u,a):return struct.unpack("<I",u.mem_read(a,4))[0]
    def r8(u,a):return u.mem_read(a,1)[0]
    def new(kind=1,ident=0,producer=111,group=2,ai_type=3,stocks=(300,100),count=1):
        u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,size);u.mem_write(base,image);u.mem_map(unit,0x10000);u.mem_map(stack,0x2000);u.mem_map(stop,0x1000)
        u.mem_write(PLAYER_BASE,bytes(STRIDE*8));u.mem_write(0x529604,bytes(228*12*4));u.mem_write(0x52C0C4,struct.pack('<'+'I'*(228*12),*([1]*(228*12))))
        u.mem_write(0x52655C,bytes([1])*(228*12));u.mem_write(0x523A78,struct.pack('<228H',*([65535]*228)))
        w32(u,0x5432FC,0);w32(u,0x508D50,0);w32(u,0x67CC90,0);w8(u,0x536720,1)
        w16(u,unit+0x64,producer);w8(u,unit+0x4C,7);w8(u,unit+0x4D,1);w32(u,unit+0xDC,3);w32(u,unit+0xC,sprite);w32(u,unit+0x134,ai if ai_type else 0)
        w8(u,unit+0xC8,44);w8(u,unit+0xC9,61);w8(u,ai+8,ai_type);w32(u,ai+0x14,town)
        u.mem_write(unit+0x98,struct.pack('<5H',*([228]*5)))
        w16(u,0x523520+ident*2,50);w16(u,0x5236E8+ident*2,25);w8(u,0x523E10+ident,2);w32(u,0x521660+ident*4,0)
        w8(u,0x523C40+ident,group);w8(u,0x523C40+producer,group)
        for a,b,c in training.SUPPLY_TABLES:w32(u,a+28,400);w32(u,b+28,0);w32(u,c+28,20)
        w32(u,0x5263D0+28,stocks[0]);w32(u,0x526400+28,stocks[1])
        p=PLAYER_BASE+7*STRIDE
        for j,v in enumerate((50,25,2)):w32(u,p+j*4,v);w32(u,p+0xC+j*4,1000)
        w32(u,p+0x18,90|(kind<<8)|(ident<<16));w32(u,p+0x1C,town);w8(u,p+0x210,count)
        return u
    def call(u,start=0x487110,args=(),checkpoint=None):
        reached,count,trace,default_priorities,gate_returns=False,0,[],[],[]
        def hook(m,a,n,_):
            nonlocal reached,count
            if a==stop or a==checkpoint:reached=True;m.emu_stop();return
            if not any(lo<=a and a+n<=hi for lo,hi in allowed):raise RuntimeError(f"Unreviewed original instruction {a:#x}")
            count+=1
            if a in (0x487110,0x487DE0,0x487D30,0x485D30,0x485CA0,0x485B70,0x488100,0x488030,0x4849C0,0x486BF0,0x485390,0x48D3A0,0x482BC0,0x482C70):trace.append(hex(a))
            if a==0x4849C0:default_priorities.append(r32(m,m.reg_read(UC_X86_REG_ESP)+4))
            if a==0x4853C2:gate_returns.append(m.reg_read(UC_X86_REG_EAX))
        h=u.hook_add(UC_HOOK_CODE,hook);sp=stack+0x1800;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),stop,*args));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,unit);u.reg_write(UC_X86_REG_EDX,0)
        if start==0x4849C0:u.reg_write(UC_X86_REG_ECX,7);u.reg_write(UC_X86_REG_EDX,unit)
        u.emu_start(start,stop+1,count=100000);u.hook_del(h)
        if not reached:raise RuntimeError("No return/checkpoint within100000 instructions")
        return u.reg_read(UC_X86_REG_EAX),count,trace,default_priorities,gate_returns
    def state(u):
        p=PLAYER_BASE+7*STRIDE
        return {"queue":list(struct.unpack('<5H',u.mem_read(unit+0x98,10))),"stocks":[r32(u,0x5263D0+28),r32(u,0x526400+28)],"heap_count":r8(u,p+0x210),"demand":[r32(u,p+j*4) for j in range(3)],"available":[r32(u,p+0xC+j*4) for j in range(3)],"secondary":r8(u,unit+0xA6)}
    def observe(label,inputs,ex,got,expected,scope="complete function and actual reviewed callees for fixture path"):
        if got!=expected:raise AssertionError((label,inputs,got,expected))
        cases.append({"label":label,"fixture_inputs":inputs,"observed":got,"expected_from_static_contract":expected,"matched":True,"original_instruction_count":ex[1],"native_call_trace":ex[2],"default_generation_priorities":ex[3],"unit_requirement_gate_returns":ex[4],"execution_scope":scope,"helper_stubs":[]})
    for ai_type in (0,1,2,4,5):
        u=new(ai_type=ai_type);before=state(u);ex=call(u);observe("AI pointer/type gate retains spending",{"ai_type":ai_type},ex,{"return":ex[0],"state":state(u)},{"return":1,"state":before})
    for kind in (9,10,255):
        u=new(kind=kind);before=state(u);ex=call(u);observe("kind above8 has no consumer",{"kind":kind},ex,{"return":ex[0],"state":state(u)},{"return":1,"state":before})
    for kind,ident,producer,group in [(1,0,111,2),(2,0,111,2),(4,7,106,2),(8,84,155,4)]:
        u=new(kind,ident,producer,group);ex=call(u)
        observe("dispatch through actual consumer complete successful enqueue",{"kind":kind,"id":ident,"producer":producer},ex,{"return":ex[0],"state":state(u),"tag":r8(u,ai+9),"context":r32(u,ai+0x18)},
            {"return":1,"state":{"queue":[ident,228,228,228,228],"stocks":[250,75],"heap_count":0,"demand":[0,0,0],"available":[950,975,998],"secondary":0x26},"tag":0 if kind==4 else (5 if kind==8 else kind),"context":0 if kind in (4,8) else town})
    for failure in ("stock","unit limit","unavailable","wrong producer"):
        u=new(8,84,155,4);p=PLAYER_BASE+7*STRIDE
        if failure=="stock":w32(u,0x5263D0+28,49)
        if failure=="unit limit":w8(u,p+0x3F0+84,255)
        if failure=="unavailable":w8(u,0x52655C+7*228+84,0)
        if failure=="wrong producer":w16(u,unit+0x64,156);w8(u,0x523C40+156,4)
        ex=call(u);pop=failure in ("unit limit","unavailable")
        observe("Observer consumer payment and request failure policy",{"failure":failure,"campaign_flag":False},ex,
            {"return":ex[0],"heap_count":state(u)["heap_count"],"demand":state(u)["demand"],"queue":state(u)["queue"],"nested_dispatch_calls":ex[2].count('0x487110')},
            {"return":0,"heap_count":0 if pop else 1,"demand":[0]*3 if pop else [50,25,2],"queue":[228]*5,"nested_dispatch_calls":2 if failure=="unavailable" else 1})
    for kind,producer in ((5,122),(6,112)):
        u=new(kind,0,producer,stocks=(49,100));p=PLAYER_BASE+7*STRIDE
        w16(u,0x51F760,50);w16(u,0x51F7B8,25);w16(u,0x5248D0,50);w16(u,0x524950,3);w16(u,0x5249D0,25);w16(u,0x524A50,7)
        w8(u,0x534104+7*24,1);w8(u,0x534224+7*24,0);w8(u,0x534590+7*46,0);w8(u,0x534368+7*46,3)
        u.mem_write(0x51F8C0,struct.pack('<44H',*([65535]*44)));u.mem_write(0x524BD0,struct.pack('<61H',*([65535]*61)))
        before=state(u);ex=call(u);observe("research/upgrade dispatch returns1 despite retaining failed stock request",{"kind":kind},ex,{"return":ex[0],"state":state(u)},{"return":1,"state":before})
    for kind,ident,producer,group in ((3,132,131,1),(7,107,106,2)):
        u=new(kind,ident,producer,group);w32(u,ai+0x14,0);before=state(u);ex=call(u)
        observe("town mismatch full morph/addon consumer path",{"kind":kind,"id":ident},ex,{"return":ex[0],"state":state(u)},{"return":0 if kind==3 else 1,"state":before})
    for count in (0,1):
        u=new(kind=0,count=count);before=state(u);ex=call(u)
        observe("empty or kind0 spending invokes default priority50 then returns1",{"heap_count":count},ex,{"return":ex[0],"priorities":ex[3],"state":state(u)},{"return":1,"priorities":[50],"state":before})
    for stocks,expected_default in [((1500,1501),[]),((1501,1500),[]),((1501,1501),[160]),((0xFFFFFFFF,1501),[])]:
        u=new(stocks=stocks);ex=call(u)
        success=stocks[0]!=0xFFFFFFFF
        observe("opportunistic default generation strict signed stock threshold",{"stocks":stocks},ex,{"return":ex[0],"priorities":ex[3],"heap_count":state(u)["heap_count"]},{"return":1 if success else 0,"priorities":expected_default+([] if success else [50]),"heap_count":0})
    for busy in (False,True):
        u=new(kind=0,count=0,stocks=(1501,1501))
        if busy:w16(u,unit+0x98,0)
        before=state(u);ex=call(u)
        observe("high-resource default attempt then empty-heap default attempt",{"busy_queue":busy},ex,
            {"return":ex[0],"state":state(u),"priorities":ex[3]}, {"return":1,"state":before,"priorities":[50] if busy else [160,50]})
    for busy_field in ("queue head","research in progress"):
        u=new(stocks=(1501,1501))
        if busy_field=="queue head":w16(u,unit+0x98,0)
        else:w8(u,unit+0xC8,0)
        before=state(u);ex=call(u)
        expected={"queue":[0,0,228,228,228] if busy_field=="queue head" else [0,228,228,228,228],"stocks":[1451,1476],"heap_count":0,"demand":[0]*3,"available":[950,975,998],"secondary":0x26}
        observe("opportunistic busy predicate differs from Marine recipe",{"field":busy_field},ex,
            {"return":ex[0],"state":state(u),"priorities":ex[3]}, {"return":1,"state":expected,"priorities":[] if busy_field=="queue head" else [160]})
    for campaign in (False,True):
        u=new(stocks=(49,100));p=PLAYER_BASE+7*STRIDE;w16(u,p+0x218,0x20 if campaign else 0);ex=call(u)
        observe("training resource failure retries only noncampaign",{"campaign":campaign},ex,{"return":ex[0],"heap_count":state(u)["heap_count"],"demand":state(u)["demand"],"nested_dispatch_calls":ex[2].count('0x487110')},
            {"return":0,"heap_count":1 if campaign else 0,"demand":[50,25,2] if campaign else [0,0,0],"nested_dispatch_calls":1 if campaign else 2})
    for remaining in (False,True):
        u=new(count=2 if remaining else 1);p=PLAYER_BASE+7*STRIDE;w8(u,0x52655C+7*228,0)
        if remaining:
            w32(u,p+0x20,80|(1<<8)|(1<<16));w32(u,p+0x24,town)
            for a,v in ((0x523522,75),(0x5236EA,25)):w16(u,a,v)
            w8(u,0x523E11,2);w8(u,0x523C41,2);w32(u,0x521664,0)
            for j,v in enumerate((125,50,4)):w32(u,p+j*4,v)
        ex=call(u)
        observe("actual recursive dispatch after permanent first request failure",{"second_ghost_request":remaining,"first_unavailable_marine":True},ex,{"return":ex[0],"state":state(u),"nested_dispatch_calls":ex[2].count('0x487110')},
            {"return":0,"state":{"queue":[1,228,228,228,228] if remaining else [228]*5,"stocks":[225,75] if remaining else [300,100],"heap_count":0,"demand":[0]*3,"available":[925,975,998] if remaining else [1000]*3,"secondary":0x26 if remaining else 0},"nested_dispatch_calls":2})
    for producer in (42,107,108):
        u=new(producer=producer,ai_type=4);p=PLAYER_BASE+7*STRIDE
        w16(u,unit+0x10,1);w16(u,sprite+0x14,0);w8(u,p+0x22D,1);w8(u,p+0x3F0+14,255)
        before=state(u);ex=call(u);target={42:'0x486bf0',107:'0x48d3a0',108:'0x485390'}[producer]
        observe("special type service occurs before nonbuilding AI gate",{"producer":producer,"ai_type":4,"position_mismatch":True,"spell_cooldown":1,"nuke_limit":255},ex,
            {"return":ex[0],"state":state(u),"service_called":target in ex[2]}, {"return":1,"state":before,"service_called":True})
    for producer in (108,111):
        u=new(8,14,producer,2);ex=call(u,0x485390,checkpoint=0x4853E9)
        observe("nuclear queue service accepts any nonzero requirement result",{"producer":producer,"requested_unit":14,"standalone_helper":True},ex,
            {"gate_return":ex[4],"queue":state(u)["queue"],"stocks":state(u)["stocks"],"tag":r8(u,ai+9),"context":r32(u,ai+0x18),"heap_count":state(u)["heap_count"]},
            {"gate_return":[1 if producer==108 else 0xFFFFFFFF],"queue":[14,228,228,228,228],"stocks":[250,75],"tag":8,"context":0,"heap_count":1},"complete actual requirement/resource/enqueue prefix before primary-order issue; wrong producer is explicit synthetic helper input")
    for status,order,secondary,stocks,flags in [(0,1,0x17,(600,300),0),(1,0x6A,0x17,(600,300),0),(1,0x96,0x17,(600,300),0),(1,1,0,(600,300),0),(1,1,0x17,(599,300),0),(1,1,0x17,(600,299),0),(1,1,0x17,(600,300),4)]:
        u=new(stocks=stocks);p=PLAYER_BASE+7*STRIDE;w32(u,unit+0xDC,status);w8(u,unit+0x4D,order);w8(u,unit+0xA6,secondary);w16(u,p+0x218,flags);before=state(u)
        ex=call(u,0x4849C0,[160]);observe("default request generator actual early rejection",{"status":status,"order":order,"secondary":secondary,"stocks":stocks,"flags":flags},ex,{"return":ex[0],"state":state(u)},{"return":0,"state":before})
    return {"format_version":1,"target_sha256":TARGET_HASH,"probe_script_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),"dependency_script_sha256":dependencies(),
      "runtime_versions":{"python":platform.python_version(),"unicorn":unicorn.__version__,"capstone":capstone.__version__,"pefile":pefile.__version__,"architecture":platform.machine(),"system":platform.system()},
      "original_code_executed":True,"complete_game_executed":False,"helper_stubs":[],"allowlisted_preferred_va_ranges":[[hex(a),hex(b)] for a,b in allowed],"all_matched":True,"case_count":len(cases),"cases":cases}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--execute-isolated-original-code',action='store_true');p.add_argument('--probe-out',type=Path);a=p.parse_args()
    if a.execute_isolated_original_code and a.probe_out is None:p.error('--probe-out required')
    data=a.exe.read_bytes()
    if hashlib.sha256(data).hexdigest()!=TARGET_HASH:p.error('Wrong EXE hash')
    pe=pefile.PE(data=data);c=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    table=list(struct.unpack('<9I',pe.get_data(0x487270-0x400000,36)))
    report={'format_version':1,'target_sha256':TARGET_HASH,'inspector_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'dependency_script_sha256':dependencies(),'original_code_executed':False,'complete_game_executed':False,'dispatch_table':{'preferred_va':'0x487270','targets':[hex(x) for x in table]},'witnesses':{}}
    for name,(start,end) in WITNESSES.items():
        raw=pe.get_data(start-0x400000,end-start);report['witnesses'][name]={'start_preferred_va':hex(start),'end_exclusive_preferred_va':hex(end),'bytes_sha256':hashlib.sha256(raw).hexdigest(),'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str} for i in c.disasm(raw,start)]}
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(report,indent=2)+'\n')
    if a.execute_isolated_original_code:
        result=probes(data,pe);a.probe_out.parent.mkdir(parents=True,exist_ok=True);a.probe_out.write_text(json.dumps(result,indent=2)+'\n');print(f"{result['case_count']} native spending dispatch cases passed; no stubs")
    else:print('Wrote static spending dispatch; original code not executed')


if __name__=='__main__':main()
